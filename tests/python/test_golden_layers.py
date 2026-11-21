"""strata-golden's layers (tools/strata-golden, see its README.md).

Dependencies point inward: the model, the text helper, the core (golden) and
the ports never include an adapter, the composition root (service) or the
command line (main). Each port header and each adapter header declares
exactly one class. No library or build is needed.
"""

import re
import unittest
from pathlib import Path

GOLDEN = Path(__file__).resolve().parents[2] / "tools" / "strata-golden"
INNER = ["model.hpp", "text.hpp", "golden.hpp", "golden.cpp"]
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
TOP_LEVEL_CLASS = re.compile(r"^(?:class|struct)\s+(\w+)(?:\s+final)?\s*(?::[^{;]*)?\{", re.M)


def includes(path):
    return INCLUDE.findall(path.read_text(encoding="utf-8"))


def classes(path):
    # Top-level within the namespace: the repo indents members, never types.
    return TOP_LEVEL_CLASS.findall(COMMENT.sub("", path.read_text(encoding="utf-8")))


class GoldenLayers(unittest.TestCase):
    def ports(self):
        return sorted((GOLDEN / "ports").glob("*.hpp"))

    def test_inner_layers_never_include_adapters_service_or_main(self):
        files = [GOLDEN / name for name in INNER] + self.ports()
        for f in files:
            self.assertTrue(f.is_file(), f)
            for inc in includes(f):
                with self.subTest(file=f.name, include=inc):
                    self.assertNotRegex(inc, r"(^|/)(adapters/|service\.hpp$|main\.cpp$)")

    def test_ports_include_only_the_model(self):
        for f in self.ports():
            for inc in includes(f):
                with self.subTest(file=f.name, include=inc):
                    self.assertEqual(inc, "model.hpp")

    def test_one_class_per_port_and_adapter_header(self):
        headers = self.ports() + sorted((GOLDEN / "adapters").glob("*.hpp"))
        self.assertGreaterEqual(len(headers), 10)
        for f in headers:
            with self.subTest(file=f.name):
                self.assertEqual(len(classes(f)), 1, classes(f))


if __name__ == "__main__":
    unittest.main()
