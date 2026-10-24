"""Drawing through the Python binding on a headless surface.

Needs a libstrata (STRATA_LIBRARY or STRATA_NATIVE_DIR) and a GL 4.5 headless
context (EGL; llvmpipe in CI). Set STRATA_PY_NO_RENDER=1 to skip these where
there is no GL, as the Windows CI job does. With STRATA_IMGCMP pointing at
strata-imgcmp, the Beyond Nations scene drawn from Python is compared with
the same golden image the C++ and C# paths must match.
"""

import ctypes
import math
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "bindings" / "python"))

import strata  # noqa: E402

HAVE_LIBRARY = bool(os.environ.get("STRATA_LIBRARY") or os.environ.get("STRATA_NATIVE_DIR"))
RENDER = HAVE_LIBRARY and os.environ.get("STRATA_PY_NO_RENDER") != "1"

KINDS = {
    "cube": strata.PRIMITIVE_CUBE,
    "capsule": strata.PRIMITIVE_CAPSULE,
    "cylinder": strata.PRIMITIVE_CYLINDER,
    "sphere": strata.PRIMITIVE_SPHERE,
}


def load_primitives_scene(path):
    """The subset of the scene format bn_primitives_legacy uses."""
    scene = {"size": (640, 360), "items": []}
    for raw in Path(path).read_text().splitlines():
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        word, args = words[0], words[1:]
        if word == "size":
            scene["size"] = (int(args[0]), int(args[1]))
        elif word == "camera":
            v = [float(a) for a in args]
            scene["camera"] = (v[0:3], v[3:6], v[6], v[7], v[8])
        elif word == "lighting" and args[0] == "legacy":
            v = [float(a) for a in args[1:]]
            scene["lighting"] = (v[0:3], v[3])
        elif word == "clear":
            scene["clear"] = [float(a) for a in args[:4]]
        elif word == "item":
            v = [float(a) for a in args[1:]]
            color = v[6:10] if len(v) >= 10 else v[6:9] + [1.0]
            scene["items"].append(strata.RenderItem(KINDS[args[0]], tuple(v[0:3]), tuple(v[3:6]), tuple(color)))
        else:
            raise ValueError("%s: unsupported directive %r" % (path, word))
    return scene


@unittest.skipUnless(RENDER, "needs a libstrata and a headless GL context")
class Render(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = strata.load()

    def test_beyond_nations_scene_matches_the_golden(self):
        scene = load_primitives_scene(ROOT / "tests" / "scenes" / "bn_primitives_legacy.scene")
        width, height = scene["size"]
        with strata.Engine(self.lib, width, height) as engine:
            eye, target, fov, near, far = scene["camera"]
            engine.set_camera(self.lib.look_at(eye, target, (0, 1, 0), fov, width / height, near, far))
            lighting = self.lib.lighting_defaults(strata.LIGHTING_LEGACY)
            direction, ambient = scene["lighting"]
            lighting.legacy_direction = (ctypes.c_float * 3)(*direction)
            lighting.legacy_ambient = ambient
            engine.set_lighting(lighting)
            items = (strata.RenderItem * len(scene["items"]))(*scene["items"])
            for _ in range(2):  # as strata-golden does: the second frame is captured
                engine.begin_frame(clear_color=scene["clear"])
                engine.submit_primitives(items)  # a ctypes array crosses as a pointer, no copy
                engine.end_frame()
            stats = engine.frame_stats()
            self.assertEqual(stats.instances, len(scene["items"]))
            self.assertEqual(stats.items_rejected, 0)
            rgba, w, h = engine.read_pixels()
            self.assertEqual((w, h), (width, height))
            self.assertEqual(len(rgba), width * height * 4)
            with tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / "bn_primitives_legacy.python.png"
                engine.save_screenshot_png(out)
                self.assertTrue(out.stat().st_size > 1000)
                imgcmp = os.environ.get("STRATA_IMGCMP")
                if imgcmp:
                    golden = ROOT / "tests" / "golden" / "bn_primitives_legacy" / "linux-llvmpipe.png"
                    result = subprocess.run([imgcmp, str(golden), str(out)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_meshes_textures_picking_overlays_and_depth(self):
        with strata.Engine(self.lib, 160, 90) as engine:
            self.assertIn("|", engine.gpu_description())
            self.assertEqual(engine.drawable_size(), (160, 90))
            engine.set_camera(self.lib.look_at((0, 0, 10), (0, 0, 0), fov_y_degrees=60, aspect=160 / 90, far_plane=50))
            engine.set_lighting(self.lib.lighting_defaults(strata.LIGHTING_LEGACY))
            # A quad 15 units away, wider than the view, so every pixel reads 15.
            wall = engine.add_mesh(
                positions=[-30, -20, -5, 30, -20, -5, 30, 20, -5, -30, 20, -5],
                indices=[0, 1, 2, 0, 2, 3],
                color=(0.6, 0.6, 0.6, 1),
            )
            texture = engine.create_texture_rgba8(2, 2, bytes([255, 0, 0, 255] * 4))
            engine.update_texture_rgba8(texture, 0, 0, 1, 1, bytes([0, 255, 0, 255]))
            card = engine.add_mesh(
                positions=[-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0],
                indices=[0, 1, 2, 0, 2, 3],
                uvs=[0, 1, 1, 1, 1, 0, 0, 0],
                texture=texture,
                flags=strata.MESH_UNLIT,
            )
            self.assertNotEqual(wall, card)
            engine.set_mesh_transform(card, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 2, 1])  # moved to z = 2

            hit = engine.raycast_meshes((0, 0, 10), (0, 0, -1))
            self.assertEqual(hit.mesh_id, card)
            self.assertAlmostEqual(hit.distance, 8.0, places=4)
            miss = engine.raycast_meshes((100, 0, 10), (0, 0, -1))
            self.assertEqual(miss.mesh_id, 0)

            engine.begin_frame(clear_color=(0, 0, 0, 1))
            engine.submit_lines(
                [strata.LineVertex((-3, -3, 1), (1, 1, 0, 1)), strata.LineVertex((3, 3, 1), (1, 1, 0, 1))]
            )
            engine.submit_points([strata.Point((2, 2, 1), 6.0, 0.0, (1, 1, 1, 1), 0)])
            engine.submit_lines(
                [strata.LineVertex((-3, 3, -9), (1, 0, 1, 1)), strata.LineVertex((3, 3, -9), (1, 0, 1, 1))],
                flags=strata.LINES_ON_TOP,
            )
            engine.submit_rects([strata.Rect(2, 2, 40, 12, (0, 0, 0, 0.5))])
            engine.submit_text([strata.Text(4, 4, 8, strata.TEXT_SHADOW, (1, 1, 1, 1), b"py")])
            engine.end_frame()

            depth, w, h = engine.read_depth()
            self.assertEqual((w, h), (160, 90))
            self.assertAlmostEqual(depth[0], 15.0, delta=0.015)  # top-left: the wall
            self.assertAlmostEqual(depth[45 * 160 + 80], 8.0, delta=0.008)  # centre: the card
            self.assertTrue(all(math.isfinite(d) for d in depth))

            rgba, w, h = engine.read_pixels()
            centre = (45 * 160 + 80) * 4
            r, g, b = rgba[centre : centre + 3]
            self.assertGreater(r + g, 200)  # the unlit red / green card, not the grey wall
            self.assertLess(b, 40)

            engine.remove_mesh(card)
            engine.destroy_texture(texture)
            engine.clear_meshes()
            self.assertIsNone(engine.poll_event())

    def test_errors_carry_status_and_text(self):
        with strata.Engine(self.lib, 32, 32) as engine:
            with self.assertRaises(strata.StrataError) as caught:
                engine.end_frame()  # no begin_frame
            self.assertEqual(caught.exception.status, strata.ERROR_INVALID_STATE)
            self.assertIn("begin_frame", str(caught.exception))
            with self.assertRaises(strata.StrataError) as caught:
                engine.set_relative_mouse_mode(True)  # headless: a window-only call
            self.assertEqual(caught.exception.status, strata.ERROR_UNSUPPORTED)
        with self.assertRaises(strata.StrataError):
            engine.end_frame()  # closed


if __name__ == "__main__":
    unittest.main()
