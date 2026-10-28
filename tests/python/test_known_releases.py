"""tools/known_releases.py and the KNOWN_RELEASES table it keeps.

A release's binding must load_release() its own version with no hash. The
release job enforces that with `known_releases.py check <version> SHA256SUMS
--require`; these tests cover the tool itself (no library, no network) and the
invariants that keep it honest: the table is in the one-entry-per-line form
the tool reads, every release before the current version is pinned, and the
binding's version is the project's.
"""

import importlib.util
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "bindings" / "python"))

import strata  # noqa: E402

_spec = importlib.util.spec_from_file_location("known_releases", str(ROOT / "tools" / "known_releases.py"))
known_releases = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(known_releases)

LINUX = "a" * 64
WINDOWS = "b" * 64


def sums(version="0.0.0-ci", linux=LINUX, windows=WINDOWS):
    return (
        "%s  libstrata-%s-linux-x64.so\n"
        "%s  strata-%s-linux-x64.tar.gz\n"
        "%s  strata-%s-win-x64.dll\n"
        "%s  strata-%s-win-x64.zip\n"
    ) % (linux, version, "c" * 64, version, windows, version, "d" * 64, version)


def version_key(v):
    return tuple(int(part) for part in v.split("."))


class Tool(unittest.TestCase):
    def test_reads_the_bare_libraries_from_ci_and_release_sums(self):
        expected = {"linux-x64": LINUX, "win-x64": WINDOWS}
        self.assertEqual(known_releases.parse_sha256sums(sums()), expected)
        self.assertEqual(known_releases.parse_sha256sums(sums("0.7.0")), expected)
        # sha256sum's binary-mode marker
        self.assertEqual(known_releases.parse_sha256sums(sums().replace("  ", " *")), expected)

    def test_sums_without_a_library_are_an_input_error(self):
        only_linux = "%s  libstrata-0.7.0-linux-x64.so\n" % LINUX
        with self.assertRaises(known_releases.InputError):
            known_releases.parse_sha256sums(only_linux)

    def test_a_missing_entry_fails_only_when_required(self):
        hashes = {"linux-x64": LINUX, "win-x64": WINDOWS}
        self.assertEqual(known_releases.check("9.9.9", hashes, {}, require=False), [])
        problems = known_releases.check("9.9.9", hashes, {}, require=True)
        self.assertEqual(len(problems), 2)
        self.assertIn("no ('9.9.9', 'linux-x64') entry", problems[0])

    def test_a_disagreeing_entry_always_fails(self):
        hashes = {"linux-x64": LINUX, "win-x64": WINDOWS}
        known = {("9.9.9", "linux-x64"): LINUX, ("9.9.9", "win-x64"): "e" * 64}
        for require in (False, True):
            problems = known_releases.check("9.9.9", hashes, known, require=require)
            self.assertEqual(len(problems), 1)
            self.assertIn("win-x64", problems[0])

    def test_matching_entries_pass(self):
        hashes = {"linux-x64": LINUX, "win-x64": WINDOWS}
        known = {("9.9.9", "linux-x64"): LINUX, ("9.9.9", "win-x64"): WINDOWS}
        self.assertEqual(known_releases.check("9.9.9", hashes, known, require=True), [])

    def test_pin_adds_and_replaces_entries_and_keeps_the_rest(self):
        source = known_releases.LOADER.read_text()
        before = known_releases.read_known(source)
        hashes = {"linux-x64": LINUX, "win-x64": WINDOWS}
        once = known_releases.pin(source, "9.9.9", hashes)
        after = known_releases.read_known(once)
        self.assertEqual({k: v for k, v in after.items() if k[0] != "9.9.9"}, before)
        self.assertEqual(after[("9.9.9", "linux-x64")], LINUX)
        # Pinning again replaces rather than duplicates.
        twice = known_releases.pin(once, "9.9.9", {"linux-x64": "f" * 64, "win-x64": WINDOWS})
        self.assertEqual(known_releases.read_known(twice)[("9.9.9", "linux-x64")], "f" * 64)
        self.assertEqual(twice.count('"9.9.9"'), 2)
        # The result is still the module: it compiles and its table is a dict.
        namespace = {"__name__": "pinned"}
        exec(compile(twice, "_loader.py", "exec"), namespace)
        self.assertEqual(namespace["KNOWN_RELEASES"][("9.9.9", "win-x64")], WINDOWS)


class Table(unittest.TestCase):
    def test_the_tool_reads_the_table_the_binding_uses(self):
        self.assertEqual(known_releases.read_known(known_releases.LOADER.read_text()), strata.KNOWN_RELEASES)

    def test_the_binding_version_is_the_project_version(self):
        self.assertEqual(strata.__version__, known_releases.project_version())

    def test_every_earlier_release_is_pinned_for_both_platforms(self):
        # Bare library assets exist from v0.4.0 on. The current version is
        # pinned in the release prep and enforced by the release job.
        changelog = (ROOT / "CHANGELOG.md").read_text()
        current = version_key(known_releases.project_version())
        released = [v for v in re.findall(r"^## ([0-9]+\.[0-9]+\.[0-9]+)\s*$", changelog, re.M)
                    if (0, 4, 0) <= version_key(v) < current]
        self.assertIn("0.6.0", released)
        for version in released:
            for platform in ("linux-x64", "win-x64"):
                self.assertIn((version, platform), strata.KNOWN_RELEASES, "v%s is not pinned" % version)


if __name__ == "__main__":
    unittest.main()
