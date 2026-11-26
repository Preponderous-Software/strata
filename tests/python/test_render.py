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
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
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

    def test_wide_lines_are_as_wide_as_asked(self):  # ABI 1.13
        with strata.Engine(self.lib, 160, 90) as engine:
            engine.set_camera(self.lib.look_at((0, 0, 10), (0, 0, 0), fov_y_degrees=60, aspect=160 / 90, far_plane=50))
            engine.set_lighting(self.lib.lighting_defaults(strata.LIGHTING_LEGACY))
            white = (1, 1, 1, 1)
            engine.begin_frame(clear_color=(0, 0, 0, 1))
            # A horizontal line 10 pixels wide across the middle of the frame.
            engine.submit_wide_lines(
                [strata.WideLineVertex((-20, 0, 0), white, 10.0, 0.0), strata.WideLineVertex((20, 0, 0), white, 10.0, 0.0)],
                flags=strata.LINES_ROUND_CAPS,
            )
            engine.end_frame()
            rgba, w, h = engine.read_pixels()
            column = [rgba[(y * w + 80) * 4] for y in range(h)]
            lit = [y for y, red in enumerate(column) if red > 127]
            self.assertEqual(len(lit), 10, column)
            self.assertLessEqual(abs((lit[0] + lit[-1]) / 2 - (h - 1) / 2), 1.0)
            with self.assertRaises(strata.StrataError) as caught:
                engine.begin_frame()
                engine.submit_wide_lines([strata.WideLineVertex((0, 0, 0), white, -1.0, 0.0)] * 2)
            self.assertEqual(caught.exception.status, strata.ERROR_INVALID_ARGUMENT)
            self.assertIn("width_pixels", str(caught.exception))
            engine.end_frame()

    def test_project_point_and_world_labels(self):
        """ABI 1.14: the projection matches where a label is drawn, and a label
        anchored behind the camera draws nothing."""
        with strata.Engine(self.lib, 160, 90) as engine:
            engine.set_camera(self.lib.look_at((0, 0, 10), (0, 0, 0), fov_y_degrees=60, aspect=160 / 90))
            x, y, depth = engine.project_point((0, 0, 0))
            self.assertAlmostEqual(x, 80.0, places=3)
            self.assertAlmostEqual(y, 45.0, places=3)
            self.assertAlmostEqual(depth, 10.0, places=4)
            up = engine.project_point((0, 1, 0))
            self.assertLess(up[1], 45.0)  # up in the world is up on screen: a smaller y
            self.assertIsNone(engine.project_point((0, 0, 20)))  # behind the eye

            def draw(anchor):
                engine.begin_frame(clear_color=(0, 0, 0, 1))
                engine.submit_world_text([strata.Text(-8, -8, 16, 0, (1, 1, 1, 1), b"#")], [anchor])
                engine.end_frame()
                rgba, w, _h = engine.read_pixels()
                return [(i // 4 % w, i // 4 // w) for i in range(0, len(rgba), 4) if rgba[i] > 128]

            lit = draw((2, 1, 0))
            ax, ay, _ = engine.project_point((2, 1, 0))
            self.assertTrue(lit)
            # Pixel centres (px + 0.5) of the glyph all fall inside its 16 x 16 cell at the anchor.
            self.assertTrue(all(ax - 8 <= px + 0.5 <= ax + 8 and ay - 8 <= py + 0.5 <= ay + 8 for px, py in lit))
            self.assertEqual(draw((0, 0, 20)), [])

            with self.assertRaises(ValueError):
                engine.submit_world_text([strata.Text(0, 0, 8, 0, (1, 1, 1, 1), b"x")], [])

    def test_rgba_vertex_colours_with_the_flag_draw_as_rgb(self):
        # ABI 1.15: the same quad, coloured per vertex as RGB triples and as
        # RGBA quadruples with MESH_COLORS_RGBA, draws the same pixels.
        quad = dict(positions=[-30, -20, -5, 30, -20, -5, 30, 20, -5, -30, 20, -5], indices=[0, 1, 2, 0, 2, 3])
        rgb = [0.9, 0.1, 0.1, 0.1, 0.9, 0.1, 0.1, 0.1, 0.9, 0.9, 0.9, 0.1]
        rgba = [c for v in range(4) for c in rgb[v * 3 : v * 3 + 3] + [0.5]]
        frames = []
        for colors, flags in ((rgb, 0), (rgba, strata.MESH_COLORS_RGBA)):
            with strata.Engine(self.lib, 64, 36) as engine:
                engine.set_camera(self.lib.look_at((0, 0, 10), (0, 0, 0), fov_y_degrees=60, aspect=64 / 36, far_plane=50))
                engine.set_lighting(self.lib.lighting_defaults(strata.LIGHTING_LEGACY))
                engine.add_mesh(colors=colors, flags=flags, **quad)
                engine.begin_frame(clear_color=(0, 0, 0, 1))
                engine.end_frame()
                frames.append(engine.read_pixels()[0])
        self.assertEqual(frames[0], frames[1])
        corners = {frames[0][i * 4 : i * 4 + 3] for i in (0, 63, 35 * 64, 35 * 64 + 63)}
        self.assertEqual(len(corners), 4)  # four different corner colours: the colours are per vertex
        with strata.Engine(self.lib, 16, 16) as engine:
            with self.assertWarns(RuntimeWarning):
                engine.add_mesh(colors=rgba, **quad)  # RGBA without the flag: read as RGB
            with self.assertRaises(ValueError):
                engine.add_mesh(colors=rgb[:-3], **quad)  # one vertex short

    def test_hidden_meshes_draw_nothing_and_are_not_picked(self):
        """ABI 1.19 (#94): a hidden mesh keeps its id and buffers but draws
        nothing, casts no shadow and is not hit; showing it brings it back."""
        if not self.lib.has("strata_set_mesh_visible"):
            self.skipTest("library older than ABI 1.19")
        quad = dict(positions=[-30, -20, -5, 30, -20, -5, 30, 20, -5, -30, 20, -5], indices=[0, 1, 2, 0, 2, 3])
        with strata.Engine(self.lib, 32, 18) as engine:
            engine.set_camera(self.lib.look_at((0, 0, 10), (0, 0, 0), fov_y_degrees=60, aspect=32 / 18, far_plane=50))
            engine.set_lighting(self.lib.lighting_defaults(strata.LIGHTING_LEGACY))
            mesh = engine.add_mesh(color=(0.9, 0.2, 0.1, 1), **quad)

            def frame():
                engine.begin_frame(clear_color=(0, 0, 0, 1))
                engine.end_frame()
                return engine.read_pixels()[0]

            shown = frame()
            self.assertEqual(engine.raycast_meshes((0, 0, 10), (0, 0, -1)).mesh_id, mesh)
            engine.set_mesh_visible(mesh, False)
            self.assertEqual(set(frame()[i] for i in range(0, 32 * 18 * 4, 4)), {0})  # only the clear colour
            self.assertEqual(engine.raycast_meshes((0, 0, 10), (0, 0, -1)).mesh_id, 0)
            engine.set_mesh_visible(mesh, True)
            self.assertEqual(frame(), shown)
            with self.assertRaises(strata.StrataError) as caught:
                engine.set_mesh_visible(mesh + 1, False)  # no such mesh
            self.assertEqual(caught.exception.status, strata.ERROR_INVALID_ARGUMENT)

    def test_flat_normals_flag_matches_host_made_flat_normals(self):
        """ABI 1.19 (#95): MESH_FLAT_NORMALS on shared vertices draws what
        unshared vertices with host-made face normals draw, and a degenerate
        triangle is accepted instead of refused."""
        if not self.lib.has("strata_set_mesh_visible"):
            self.skipTest("library older than ABI 1.19")
        # A tent seen from above-front: two faces sharing a ridge, plus a degenerate triangle.
        shared = [-3, 0, 0, 0, 2, 2, 0, 2, -2, 3, 0, 0]
        indices = [0, 1, 2, 3, 2, 1, 3, 3, 2]

        def cross(a, b, c):
            u = [b[i] - a[i] for i in range(3)]
            v = [c[i] - a[i] for i in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            length = sum(x * x for x in n) ** 0.5
            return [x / length for x in n] if length > 0 else [0, 1, 0]

        corner = [shared[i * 3 : i * 3 + 3] for i in range(4)]
        unshared, normals = [], []
        for t in range(0, len(indices), 3):
            a, b, c = (corner[i] for i in indices[t : t + 3])
            for p in (a, b, c):
                unshared += p
                normals += cross(a, b, c)
        frames = []
        for kwargs in (dict(positions=unshared, indices=list(range(9)), normals=normals),
                       dict(positions=shared, indices=indices, flags=strata.MESH_FLAT_NORMALS)):
            with strata.Engine(self.lib, 48, 32) as engine:
                engine.set_camera(self.lib.look_at((0, 6, 8), (0, 1, 0), fov_y_degrees=50, aspect=48 / 32, far_plane=50))
                engine.set_lighting(self.lib.lighting_defaults(strata.LIGHTING_LEGACY))
                engine.add_mesh(color=(0.7, 0.7, 0.7, 1), **kwargs)
                engine.begin_frame(clear_color=(0, 0, 0, 1))
                engine.end_frame()
                frames.append(engine.read_pixels()[0])
        self.assertEqual(frames[0], frames[1])
        with strata.Engine(self.lib, 16, 16) as engine:
            with self.assertRaises(strata.StrataError) as caught:
                engine.add_mesh(positions=unshared, indices=list(range(9)), normals=normals[:-9] + [0] * 9)
            self.assertIn("(triangle 2, indices 6 7 8) has zero length", str(caught.exception))

    def test_abi_1_20_materials_spot_lights_output_and_ortho(self):
        """ABI 1.20: a quad's emission shows with no light, its alpha cutout
        drops the texels below the cutoff, NULL restores the default, a spot
        light lights inside its cone only, the default colour output changes
        nothing, and an orthographic camera draws the same quad the same
        size wherever it sits along the view axis."""
        if not self.lib.has("strata_set_mesh_material"):
            self.skipTest("library older than ABI 1.20")
        quad = dict(positions=[-4, -4, 0, 4, -4, 0, 4, 4, 0, -4, 4, 0], indices=[0, 1, 2, 0, 2, 3],
                    uvs=[0, 1, 1, 1, 1, 0, 0, 0])
        # Left half opaque, right half transparent.
        rgba = bytes([255, 255, 255, 255, 255, 255, 255, 0] * 2)
        with strata.Engine(self.lib, 16, 16) as engine:
            engine.set_camera(self.lib.look_at((0, 0, 10), (0, 0, 0), fov_y_degrees=60, aspect=1, far_plane=50))
            space = self.lib.lighting_defaults(strata.LIGHTING_SPACE)
            space.legacy_ambient = 0.0
            engine.set_lighting(space)
            texture = engine.create_texture_rgba8(2, 2, rgba, strata.TEXTURE_NEAREST)
            mesh = engine.add_mesh(color=(1, 1, 1, 1), texture=texture, **quad)

            def frame():
                engine.begin_frame(clear_color=(0, 0, 0, 1))
                engine.end_frame()
                return engine.read_pixels()[0]

            def pixel(pixels, x, y):
                return tuple(pixels[(y * 16 + x) * 4 : (y * 16 + x) * 4 + 3])

            dark = frame()
            self.assertEqual(pixel(dark, 4, 8), (0, 0, 0))  # no light, no ambient
            material = strata.Material()
            material.emissive = (0.0, 1.0, 0.0)
            material.emissive_strength = 0.5
            engine.set_mesh_material(mesh, material)
            glow = frame()
            self.assertEqual(pixel(glow, 4, 8)[0], 0)
            self.assertAlmostEqual(pixel(glow, 4, 8)[1], 128, delta=1)
            self.assertEqual(pixel(glow, 11, 8), pixel(glow, 4, 8))  # no cutout yet: both halves glow
            material.alpha_cutoff = 0.5
            engine.set_mesh_material(mesh, material)
            cut = frame()
            self.assertEqual(pixel(cut, 4, 8), pixel(glow, 4, 8))
            self.assertEqual(pixel(cut, 11, 8), (0, 0, 0))  # the transparent half is discarded
            engine.set_mesh_material(mesh, None)
            self.assertEqual(frame(), dark)
            engine.set_color_output(0, 1.0)
            self.assertEqual(frame(), dark)
            with self.assertRaises(strata.StrataError) as caught:
                bad = strata.Material()
                bad.normal_texture = texture + 1  # no such texture
                engine.set_mesh_material(mesh, bad)
            self.assertEqual(caught.exception.status, strata.ERROR_INVALID_ARGUMENT)
            with self.assertRaises(strata.StrataError):
                engine.set_color_output(strata.OUTPUT_TONEMAP_ACES | strata.OUTPUT_TONEMAP_REINHARD, 1.0)

            spot = strata.SpotLight()
            spot.position = (-2.0, 0.0, 3.0)
            spot.direction = (0.0, 0.0, -1.0)  # straight at the quad: a disc around x = -2
            spot.range = 10.0
            spot.color = (1.0, 1.0, 1.0)
            spot.intensity = 2.0
            spot.inner_cone_degrees = 10.0
            spot.outer_cone_degrees = 20.0
            engine.set_spot_lights([spot])
            lit = frame()
            self.assertGreater(pixel(lit, 5, 8)[0], 50)  # in the cone
            self.assertEqual(pixel(lit, 13, 8), (0, 0, 0))  # outside it
            engine.set_spot_lights([])
            self.assertEqual(frame(), dark)

        sizes = []
        for z in (0.0, -20.0):
            with strata.Engine(self.lib, 32, 32) as engine:
                engine.set_camera(self.lib.ortho((0, 0, 10), (0, 0, 0), half_height=8, aspect=1, far_plane=50))
                engine.set_lighting(self.lib.lighting_defaults(strata.LIGHTING_LEGACY))
                mesh = engine.add_mesh(color=(1, 1, 1, 1), positions=[-4, -4, z, 4, -4, z, 4, 4, z, -4, 4, z],
                                       indices=[0, 1, 2, 0, 2, 3])
                engine.begin_frame(clear_color=(0, 0, 0, 1))
                engine.end_frame()
                pixels = engine.read_pixels()[0]
                sizes.append(sum(1 for i in range(0, len(pixels), 4) if pixels[i] > 0))
        self.assertEqual(sizes[0], sizes[1])
        self.assertAlmostEqual(sizes[0], 16 * 16, delta=16)  # half the frame across: a quarter of the pixels

    def test_ambient_and_sun_levels(self):
        """ABI 1.16: the defaults draw the same bytes; a lift and a dim sun change them."""
        ground = strata.RenderItem(strata.PRIMITIVE_CUBE, (0, -0.5, 0), (20, 1, 20), (0.6, 0.6, 0.6, 1))
        block = strata.RenderItem(strata.PRIMITIVE_CUBE, (0, 1, 0), (2, 2, 2), (0.8, 0.5, 0.3, 1))
        items = (strata.RenderItem * 2)(ground, block)

        def draw(engine, levels=None):
            if levels:
                levels(engine)
            for _ in range(2):
                engine.begin_frame(clear_color=(0, 0, 0, 1))
                engine.submit_primitives(items)
                engine.end_frame()
            return engine.read_pixels()[0]

        def mean(rgba):
            return sum(rgba[i] + rgba[i + 1] + rgba[i + 2] for i in range(0, len(rgba), 4)) / len(rgba)

        with strata.Engine(self.lib, 96, 64) as engine:
            engine.set_camera(self.lib.look_at((6, 5, 8), (0, 0.5, 0), fov_y_degrees=50, aspect=96 / 64))
            lighting = self.lib.lighting_defaults(strata.LIGHTING_SUN)
            lighting.time_of_day = 0.35
            lighting.cascade_count = 0
            engine.set_lighting(lighting)
            before = draw(engine)
            same = draw(engine, lambda e: (e.set_ambient((1, 1, 1), (0, 0, 0)), e.set_sun_intensity(1, (1, 1, 1))))
            self.assertEqual(bytes(before), bytes(same))
            lifted = draw(engine, lambda e: e.set_ambient(lift=(0.3, 0.3, 0.3)))
            self.assertGreater(mean(lifted), mean(before) + 5)
            dimmed = draw(engine, lambda e: (e.set_ambient(), e.set_sun_intensity(0.2)))
            self.assertLess(mean(dimmed), mean(before) - 5)
            engine.set_lighting(lighting)  # the levels stay across set_lighting
            self.assertEqual(bytes(draw(engine)), bytes(dimmed))
            with self.assertRaises(strata.StrataError) as caught:
                engine.set_ambient(tint=(1, 1, -1))
            self.assertEqual(caught.exception.status, strata.ERROR_INVALID_ARGUMENT)
            with self.assertRaises(strata.StrataError):
                engine.set_sun_intensity(float("nan"))
            engine.set_sun_intensity()
            self.assertEqual(bytes(draw(engine)), bytes(before))  # nothing else had changed

    def test_errors_carry_status_and_text(self):
        with strata.Engine(self.lib, 32, 32) as engine:
            with self.assertRaises(strata.StrataError) as caught:
                engine.end_frame()  # no begin_frame
            self.assertEqual(caught.exception.status, strata.ERROR_INVALID_STATE)
            self.assertIn("begin_frame", str(caught.exception))
            with self.assertRaises(strata.StrataError) as caught:
                engine.set_relative_mouse_mode(True)  # headless: a window-only call
            self.assertEqual(caught.exception.status, strata.ERROR_UNSUPPORTED)
            if self.lib.has("strata_set_fullscreen"):  # ABI 1.17
                with self.assertRaises(strata.StrataError) as caught:
                    engine.set_fullscreen(True)  # headless: a window-only call
                self.assertEqual(caught.exception.status, strata.ERROR_UNSUPPORTED)
                self.assertFalse(engine.fullscreen())
                self.assertEqual(engine.drawable_size(), (32, 32))  # nothing changed
        with self.assertRaises(strata.StrataError):
            engine.end_frame()  # closed


if __name__ == "__main__":
    unittest.main()
