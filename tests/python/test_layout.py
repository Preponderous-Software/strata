"""The Python binding's struct layouts, the same checks as the C# layout tests.

Sizes are asserted against fixed numbers (64-bit targets) without a library.
With a library (STRATA_LIBRARY or STRATA_NATIVE_DIR), every field offset is
asserted against strata_struct_field_offset, and so is the field count.
"""

import ctypes
import hashlib
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bindings" / "python"))

import strata  # noqa: E402
from strata import _loader  # noqa: E402

HAVE_LIBRARY = bool(os.environ.get("STRATA_LIBRARY") or os.environ.get("STRATA_NATIVE_DIR"))
IS_64_BIT = ctypes.sizeof(ctypes.c_void_p) == 8


class FixedSizes(unittest.TestCase):
    def test_sizes_documented_in_strata_h(self):
        self.assertEqual(ctypes.sizeof(strata.RenderItem), 44)
        self.assertEqual(ctypes.sizeof(strata.PointLight), 36)
        self.assertEqual(ctypes.sizeof(strata.LineVertex), 28)
        self.assertEqual(ctypes.sizeof(strata.Point), 40)
        self.assertEqual(ctypes.sizeof(strata.SolarPosition), 32)
        self.assertEqual(ctypes.sizeof(strata.RayHit), 36)
        self.assertEqual(ctypes.sizeof(strata.Rect), 32)

    @unittest.skipUnless(IS_64_BIT, "pointer-holding structs are documented for 64-bit targets")
    def test_pointer_holding_sizes_on_64_bit(self):
        self.assertEqual(ctypes.sizeof(strata.TerrainTile), 56)
        self.assertEqual(ctypes.sizeof(strata.Mesh), 56)
        self.assertEqual(ctypes.sizeof(strata.TexturedMesh), 72)
        self.assertEqual(ctypes.sizeof(strata.Text), 40)

    def test_render_item_offsets_are_beyond_nations(self):
        self.assertEqual(strata.RenderItem.kind.offset, 0)
        self.assertEqual(strata.RenderItem.position.offset, 4)
        self.assertEqual(strata.RenderItem.scale.offset, 16)
        self.assertEqual(strata.RenderItem.color.offset, 28)

    def test_struct_ids_are_in_order(self):
        self.assertEqual([cls.STRUCT_ID for cls in strata.STRUCTS], list(range(len(strata.STRUCTS))))

    def test_every_function_in_strata_h_is_declared(self):
        header = (Path(__file__).resolve().parents[2] / "include" / "strata" / "strata.h").read_text()
        in_header = set(re.findall(r"STRATA_API [^(]*?\b(strata_[a-z0-9_]+)\(", header))
        self.assertGreater(len(in_header), 40)
        self.assertEqual(set(strata.FUNCTIONS), in_header)

    def test_every_declared_function_names_its_minor(self):
        for name, (minor, _restype, _argtypes) in strata.FUNCTIONS.items():
            self.assertTrue(name.startswith("strata_"), name)
            self.assertLessEqual(minor, strata.ABI_MINOR, name)


@unittest.skipUnless(HAVE_LIBRARY, "set STRATA_LIBRARY or STRATA_NATIVE_DIR to check against a libstrata")
class NativeLayouts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = strata.load()

    def assert_layout(self, struct, *fields):
        self.assertEqual(self.lib.struct_size(struct.STRUCT_ID), ctypes.sizeof(struct))
        self.assertEqual([name for name, _ in struct._fields_], list(fields))
        for field_id, name in enumerate(fields):
            self.assertEqual(self.lib.struct_field_offset(struct.STRUCT_ID, field_id), getattr(struct, name).offset, name)
        self.assertEqual(self.lib.struct_field_offset(struct.STRUCT_ID, len(fields)), -1)

    def test_versions(self):
        self.assertEqual(self.lib.abi_version, strata.ABI_VERSION)
        self.assertGreaterEqual(self.lib.abi_minor, strata.ABI_MINOR)
        self.assertEqual(self.lib.structs_checked, len(strata.STRUCTS))
        for name in strata.FUNCTIONS:
            self.assertTrue(self.lib.has(name), name)

    def test_every_struct(self):
        self.assert_layout(strata.RenderItem, "kind", "position", "scale", "color")
        self.assert_layout(
            strata.EngineDesc, "struct_size", "abi_version", "surface", "width", "height", "flags", "title",
            "get_proc_address", "get_proc_address_user",
        )
        self.assert_layout(strata.Camera, "view", "projection", "eye", "near_plane", "far_plane")
        self.assert_layout(
            strata.Lighting, "mode", "legacy_direction", "legacy_ambient", "time_of_day", "latitude_degrees",
            "cascade_count", "shadow_map_size", "shadow_distance", "split_lambda", "fog",
        )
        self.assert_layout(strata.FrameDesc, "clear_flags", "clear_color")
        self.assert_layout(
            strata.FrameStats, "draw_calls", "instances", "instances_per_kind", "items_rejected", "shadow_cascades",
            "cpu_ms_shadow", "cpu_ms_opaque", "cpu_ms_sky", "cpu_ms_frame", "gpu_finish_ms",
        )
        self.assert_layout(
            strata.Event, "type", "scancode", "modifiers", "repeat", "button", "width", "height", "x", "y", "dx", "dy",
            "text",
        )
        self.assert_layout(strata.PointLight, "position", "range", "color", "intensity", "flags")
        self.assert_layout(
            strata.TerrainTile, "origin", "cell_size", "samples_x", "samples_z", "heights", "colors", "color"
        )
        self.assert_layout(
            strata.Mesh, "positions", "normals", "colors", "indices", "vertex_count", "index_count", "color"
        )
        self.assert_layout(strata.LineVertex, "position", "color")
        self.assert_layout(strata.Point, "position", "size_pixels", "world_diameter", "color", "flags")
        self.assert_layout(strata.Text, "x", "y", "pixel_size", "flags", "color", "utf8")
        self.assert_layout(
            strata.SolarPosition, "to_sun", "elevation_degrees", "azimuth_degrees", "declination_degrees",
            "hour_angle_degrees", "equation_of_time_minutes",
        )
        self.assert_layout(strata.TexturedMesh, "mesh", "uvs", "texture", "flags")
        self.assert_layout(strata.RayHit, "mesh_id", "triangle", "distance", "point", "normal")
        self.assert_layout(strata.Rect, "x", "y", "width", "height", "color")

    def test_calls_reject_a_null_engine(self):
        raw = self.lib.raw
        mesh = strata.Mesh()
        out = ctypes.c_uint32()
        self.assertEqual(raw.strata_add_mesh(None, ctypes.byref(mesh), ctypes.byref(out)), strata.ERROR_INVALID_ARGUMENT)
        self.assertEqual(raw.strata_end_frame(None), strata.ERROR_INVALID_ARGUMENT)
        depth = (ctypes.c_float * 4)()
        w, h = ctypes.c_int32(), ctypes.c_int32()
        self.assertEqual(
            raw.strata_read_depth_f32(None, depth, 16, ctypes.byref(w), ctypes.byref(h)), strata.ERROR_INVALID_ARGUMENT
        )
        self.assertIn("NULL", self.lib.last_error())
        event = strata.Event(type=2)
        seconds = ctypes.c_double()
        self.assertEqual(raw.strata_push_event(None, ctypes.byref(event)), strata.ERROR_INVALID_ARGUMENT)
        self.assertEqual(raw.strata_frame_seconds(None, ctypes.byref(seconds)), strata.ERROR_INVALID_ARGUMENT)
        self.assertEqual(raw.strata_is_driven(None), 0)

    def test_calls_that_need_no_engine(self):
        # 2026-06-21 12:00 UTC on the prime meridian at 40 N: the June solstice
        # noon sun stands about 73.4 degrees high (the C# test's case).
        sun = self.lib.solar_position(1782043200.0, 40.0, 0.0)
        self.assertAlmostEqual(sun.elevation_degrees, 73.4, delta=0.2)
        camera = self.lib.look_at((0, 0, 5), (0, 0, 0), fov_y_degrees=60, aspect=1.0, near_plane=0.1, far_plane=50)
        self.assertAlmostEqual(camera.eye[2], 5.0)
        self.assertAlmostEqual(camera.near_plane, 0.1, places=6)
        legacy = self.lib.lighting_defaults(strata.LIGHTING_LEGACY)
        self.assertEqual(legacy.mode, strata.LIGHTING_LEGACY)
        with self.assertRaises(strata.StrataError) as caught:
            self.lib.lighting_defaults(99)
        self.assertEqual(caught.exception.status, strata.ERROR_INVALID_ARGUMENT)


class FetchRelease(unittest.TestCase):
    """fetch_release against a file:// stand-in for the releases page: no network."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.releases = root / "releases"
        (self.releases / "v9.9.9").mkdir(parents=True)
        self.payload = b"not really a library"
        (self.releases / "v9.9.9" / "libstrata-9.9.9-linux-x64.so").write_bytes(self.payload)
        self.cache = root / "cache"
        self.saved = _loader.RELEASES_URL
        _loader.RELEASES_URL = self.releases.as_uri()

    def tearDown(self):
        _loader.RELEASES_URL = self.saved
        self.tmp.cleanup()

    def test_pinned_hash_is_fetched_and_cached(self):
        pin = hashlib.sha256(self.payload).hexdigest()
        path = strata.fetch_release("9.9.9", pin, self.cache, platform="linux-x64")
        self.assertEqual(path.read_bytes(), self.payload)
        self.assertEqual(path.name, "libstrata-9.9.9-linux-x64.so")
        # A second call re-hashes the cached copy instead of downloading.
        (self.releases / "v9.9.9" / "libstrata-9.9.9-linux-x64.so").unlink()
        self.assertEqual(strata.fetch_release("9.9.9", pin, self.cache, platform="linux-x64"), path)

    def test_a_wrong_hash_deletes_the_download(self):
        with self.assertRaises(strata.StrataLoadError):
            strata.fetch_release("9.9.9", "0" * 64, self.cache, platform="linux-x64")
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_a_tampered_cache_is_replaced(self):
        pin = hashlib.sha256(self.payload).hexdigest()
        self.cache.mkdir()
        (self.cache / "libstrata-9.9.9-linux-x64.so").write_bytes(b"tampered")
        path = strata.fetch_release("9.9.9", pin, self.cache, platform="linux-x64")
        self.assertEqual(path.read_bytes(), self.payload)

    def test_an_unpinned_unknown_release_is_refused(self):
        with self.assertRaises(strata.StrataLoadError):
            strata.fetch_release("9.9.9", None, self.cache, platform="linux-x64")

    def test_known_releases_are_full_hashes(self):
        for (version, platform), digest in strata.KNOWN_RELEASES.items():
            self.assertEqual(len(digest), 64, (version, platform))
            int(digest, 16)

    def test_loading_nothing_explains_the_options(self):
        saved = {k: os.environ.pop(k, None) for k in ("STRATA_LIBRARY", "STRATA_NATIVE_DIR")}
        try:
            with self.assertRaises(strata.StrataLoadError) as caught:
                strata.load()
            self.assertIn("STRATA_LIBRARY", str(caught.exception))
        finally:
            for k, v in saved.items():
                if v is not None:
                    os.environ[k] = v


if __name__ == "__main__":
    unittest.main()
