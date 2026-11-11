"""Lengths the Python binding checks before calling the library (#79).

The C ABI takes bare pointers for normals, uvs, colours and texels, so a short
Python sequence would be read past its end in native code. These checks run
before any call, so no library or GL context is needed: the engine is a bare
object whose _call records what reached it.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bindings" / "python"))

import strata  # noqa: E402

QUAD = dict(positions=[0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0], indices=[0, 1, 2, 0, 2, 3])


def fake_engine():
    engine = object.__new__(strata.Engine)
    engine._handle = None
    engine.calls = []
    engine._call = lambda name, *args: engine.calls.append(name)
    return engine


class MeshLengths(unittest.TestCase):
    def test_short_normals_raise(self):
        engine = fake_engine()
        with self.assertRaisesRegex(ValueError, r"normals has 3 floats; 4 vertices need 12"):
            engine.add_mesh(normals=[0, 1, 0], **QUAD)
        self.assertEqual(engine.calls, [])

    def test_short_uvs_raise(self):
        engine = fake_engine()
        with self.assertRaisesRegex(ValueError, r"uvs has 6 floats; 4 vertices need 8"):
            engine.add_mesh(uvs=[0, 0, 1, 0, 1, 1], **QUAD)
        self.assertEqual(engine.calls, [])

    def test_short_colors_raise(self):
        engine = fake_engine()
        with self.assertRaisesRegex(ValueError, r"colors has 9 floats; 4 vertices need 12"):
            engine.add_mesh(colors=[1] * 9, **QUAD)
        self.assertEqual(engine.calls, [])

    def test_full_lengths_reach_the_library(self):
        engine = fake_engine()
        engine.add_mesh(normals=[0, 0, 1] * 4, colors=[1] * 12, **QUAD)
        engine.add_mesh(normals=(0.0, 0.0, 1.0) * 4, uvs=[0, 0, 1, 0, 1, 1, 0, 1], **QUAD)
        self.assertEqual(engine.calls, ["strata_add_mesh", "strata_add_textured_mesh"])


class TextureLengths(unittest.TestCase):
    def test_short_create_raises(self):
        engine = fake_engine()
        with self.assertRaisesRegex(ValueError, r"rgba has 4 bytes; 1024 x 1024 texels need 4194304"):
            engine.create_texture_rgba8(1024, 1024, b"\xff" * 4)
        self.assertEqual(engine.calls, [])

    def test_short_update_raises(self):
        engine = fake_engine()
        with self.assertRaisesRegex(ValueError, r"rgba has 15 bytes; 2 x 2 texels need 16"):
            engine.update_texture_rgba8(1, 0, 0, 2, 2, b"\x00" * 15)
        self.assertEqual(engine.calls, [])

    def test_full_lengths_reach_the_library(self):
        engine = fake_engine()
        engine.create_texture_rgba8(2, 2, bytes(16))
        engine.update_texture_rgba8(1, 1, 1, 1, 1, bytearray(4))
        self.assertEqual(engine.calls, ["strata_create_texture_rgba8", "strata_update_texture_rgba8"])


if __name__ == "__main__":
    unittest.main()
