"""The drive channel's layers (src/core/drive, see its README.md).

Dependencies point inward: the model, the settings parser, the Drive core and
the ports never include an adapter or the composition root (service). Each
port header and each adapter header declares exactly one class. No library or
build is needed.
"""

import re
import unittest
from pathlib import Path

DRIVE = Path(__file__).resolve().parents[2] / "src" / "core" / "drive"
INNER = ["model.hpp", "settings.hpp", "settings.cpp", "drive.hpp", "drive.cpp"]
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
TOP_LEVEL_CLASS = re.compile(r"^(?:class|struct)\s+(\w+)(?:\s+final)?\s*(?::[^{;]*)?\{", re.M)


def includes(path):
    return INCLUDE.findall(path.read_text(encoding="utf-8"))


def classes(path):
    # Top-level within the namespace: the repo indents members, never types.
    text = COMMENT.sub("", path.read_text(encoding="utf-8"))
    return TOP_LEVEL_CLASS.findall(text)


class DriveLayers(unittest.TestCase):
    def inner_files(self):
        files = [DRIVE / name for name in INNER] + sorted((DRIVE / "ports").glob("*.hpp"))
        for f in files:
            self.assertTrue(f.is_file(), f)
        return files

    def test_inner_layers_never_include_adapters_or_the_service(self):
        for f in self.inner_files():
            for inc in includes(f):
                with self.subTest(file=f.name, include=inc):
                    self.assertNotRegex(inc, r"(^|/)drive/(adapters/|service\.hpp)")

    def test_ports_include_only_the_model(self):
        for f in sorted((DRIVE / "ports").glob("*.hpp")):
            for inc in includes(f):
                with self.subTest(file=f.name, include=inc):
                    self.assertTrue(inc == "core/drive/model.hpp" or "/" not in inc, inc)

    def test_one_class_per_port_and_adapter_header(self):
        headers = sorted((DRIVE / "ports").glob("*.hpp")) + sorted((DRIVE / "adapters").glob("*.hpp"))
        self.assertGreaterEqual(len(headers), 4)
        for f in headers:
            with self.subTest(file=f.name):
                self.assertEqual(len(classes(f)), 1, classes(f))


if __name__ == "__main__":
    unittest.main()
