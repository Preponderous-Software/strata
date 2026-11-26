"""The glTF module through the Python binding (bindings/python/strata/gltf.py).

Struct sizes are checked without a library. With a libstrata and a
libstrata_gltf beside it (STRATA_NATIVE_DIR, or STRATA_LIBRARY and
STRATA_GLTF_LIBRARY) and headless GL, every synthetic asset in
modules/gltf/tests/assets is loaded, drawn and unloaded. Set
STRATA_PY_NO_RENDER=1 to skip the drawing where there is no GL.
"""

import ctypes
import json
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "bindings" / "python"))

import strata  # noqa: E402
from strata import gltf  # noqa: E402

ASSETS = ROOT / "modules" / "gltf" / "tests" / "assets"
HAVE_LIBRARY = bool(os.environ.get("STRATA_LIBRARY") or os.environ.get("STRATA_NATIVE_DIR"))
RENDER = HAVE_LIBRARY and os.environ.get("STRATA_PY_NO_RENDER") != "1"


def gltf_library_present() -> bool:
    try:
        lib_path = strata.resolve_library_path()
        return gltf.resolve_library_path(beside=lib_path).is_file()
    except Exception:
        return False


class Layouts(unittest.TestCase):
    def test_sizes_match_strata_gltf_h(self):
        self.assertEqual(ctypes.sizeof(gltf.Options), 40)
        self.assertEqual(ctypes.sizeof(gltf.Info), 80)
        self.assertEqual(ctypes.sizeof(gltf.MeshInfo), 52)
        self.assertEqual(gltf.Info.vertices.offset, 40)
        self.assertEqual(gltf.Info.bounds_min.offset, 56)

    def test_constants_match_strata_gltf_h(self):
        header = (ROOT / "modules" / "gltf" / "include" / "strata" / "gltf" / "strata_gltf.h").read_text(encoding="utf-8")
        defined = {name: int(value, 0) for name, value in
                   re.findall(r"#define STRATA_GLTF_([A-Z_]+) (0x[0-9a-fA-F]+|[0-9]+)u?\b", header)}
        self.assertGreater(len(defined), 10)
        for name, value in defined.items():
            self.assertEqual(getattr(gltf, name), value, name)


@unittest.skipUnless(RENDER and gltf_library_present(), "needs libstrata, libstrata_gltf and headless GL")
class LoadAndDraw(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = strata.load()
        cls.loader = gltf.load_library(beside=cls.lib)

    def draw(self, engine):
        engine.set_camera(self.lib.look_at((3, 2.5, 5), (0, 0.5, 0), fov_y_degrees=45, aspect=160 / 90, far_plane=50))
        engine.set_lighting(self.lib.lighting_defaults(strata.LIGHTING_LEGACY))
        engine.begin_frame(clear_color=(0, 0, 0, 1))
        engine.end_frame()
        rgba, width, height = engine.read_pixels()
        drawn = sum(1 for i in range(0, len(rgba), 4) if rgba[i] or rgba[i + 1] or rgba[i + 2])
        return drawn / (width * height)

    def test_every_asset_loads_draws_and_unloads(self):
        with strata.Engine(self.lib, 160, 90) as engine:
            for name in ("cube.gltf", "textured_quad.glb", "hierarchy.gltf", "vertex_colors.glb", "alpha_modes.gltf"):
                with self.subTest(asset=name):
                    scene = self.loader.load(engine, ASSETS / name)
                    self.assertGreater(scene.info.mesh_count, 0)
                    self.assertEqual(len(scene.mesh_ids), scene.info.mesh_count)
                    self.assertIn("meshes out:", scene.report)
                    self.assertEqual(json.loads(scene.report_json)["meshes_out"], scene.info.mesh_count)
                    self.assertGreater(self.draw(engine), 0.02)
                    scene.unload()
                    self.assertEqual(self.draw(engine), 0.0)

    def test_budget_and_errors(self):
        with strata.Engine(self.lib, 160, 90) as engine:
            with self.loader.load(engine, ASSETS / "hierarchy.gltf", max_meshes=1) as scene:
                self.assertEqual(scene.info.mesh_count, 1)
                self.assertEqual(scene.info.atlased, 1)
            with self.assertRaises(gltf.GltfError) as caught:
                self.loader.load(engine, ASSETS / "missing.glb")
            self.assertEqual(caught.exception.status, 5)
            with self.assertRaises(gltf.GltfError) as caught:
                self.loader.load(engine, ASSETS / "alpha_modes.gltf", max_meshes=1)
            self.assertEqual(caught.exception.status, 7)
            with self.loader.load(engine, ASSETS / "alpha_modes.gltf", mask=gltf.MASK_OPAQUE) as scene:
                mask = scene.meshes[1]
                self.assertEqual(mask.alpha_mode, gltf.ALPHA_MASK)
                self.assertAlmostEqual(mask.alpha_cutoff, 0.5)
                self.assertEqual(mask.flags & strata.MESH_TRANSLUCENT, 0)


if __name__ == "__main__":
    unittest.main()
