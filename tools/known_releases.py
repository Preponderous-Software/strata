#!/usr/bin/env python3
"""Keeps the Python binding's KNOWN_RELEASES in step with the release assets.

A release's binding must be able to strata.load_release() its own version
with no hash, so the release's own SHA-256s have to be in
bindings/python/strata/_loader.py at the tagged commit. The libraries are
built reproducibly (the same sources give the same bytes on every CI run), and
the binding is not part of them, so the hashes a pull request's CI computes
are the hashes the tag will publish.

  known_releases.py check VERSION SHA256SUMS [--require]
      Compares KNOWN_RELEASES' VERSION entries with the bare libraries in a
      SHA256SUMS file (a release's, or the `release-assets` artifact of any CI
      run, whose names say 0.0.0-ci). An entry that disagrees always fails.
      With --require, a missing entry fails too: the release job uses this, so
      a tag whose binding cannot load it never becomes a release. Prints the
      lines to paste either way.

  known_releases.py pin VERSION SHA256SUMS
      Writes VERSION's entries into _loader.py (adding or replacing them).

  known_releases.py version
      Prints the project version from CMakeLists.txt.

Exit status: 0 ok, 1 a check failed, 2 usage or input error.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple

ROOT = Path(__file__).resolve().parents[1]
LOADER = ROOT / "bindings" / "python" / "strata" / "_loader.py"
CMAKELISTS = ROOT / "CMakeLists.txt"

#: The bare library assets the binding fetches, by platform (any version, so a
#: CI run's 0.0.0-ci names match too).
ASSET_PATTERNS = {
    "linux-x64": re.compile(r"^libstrata-[^/]+-linux-x64\.so$"),
    "win-x64": re.compile(r"^strata-[^/]+-win-x64\.dll$"),
}

ENTRY = re.compile(r'^\s*\("(?P<version>[^"]+)",\s*"(?P<platform>[^"]+)"\):\s*"(?P<sha>[0-9a-fA-F]{64})",\s*$')


class InputError(Exception):
    pass


def project_version(cmakelists: Path = CMAKELISTS) -> str:
    match = re.search(r"project\(\s*strata\b[^)]*?\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmakelists.read_text(), re.S)
    if not match:
        raise InputError("no project(strata VERSION x.y.z) in %s" % cmakelists)
    return match.group(1)


def parse_sha256sums(text: str) -> Dict[str, str]:
    """{platform: sha256} for the bare libraries listed in a SHA256SUMS file."""
    found: Dict[str, str] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        digest, name = parts[0].lower(), parts[1].lstrip("*")
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            continue
        for platform, pattern in ASSET_PATTERNS.items():
            if pattern.match(name):
                if platform in found and found[platform] != digest:
                    raise InputError("SHA256SUMS lists two different %s libraries" % platform)
                found[platform] = digest
    missing = sorted(set(ASSET_PATTERNS) - set(found))
    if missing:
        raise InputError("SHA256SUMS has no bare library for %s" % ", ".join(missing))
    return found


def _table_span(source: str) -> Tuple[int, int]:
    start = source.find("KNOWN_RELEASES = {\n")
    if start < 0:
        raise InputError("no 'KNOWN_RELEASES = {' in %s" % LOADER)
    body = start + len("KNOWN_RELEASES = {\n")
    end = source.find("\n}\n", body)
    if end < 0:
        raise InputError("KNOWN_RELEASES in %s is not closed by a '}' line" % LOADER)
    return body, end + 1


def read_known(source: str) -> Dict[Tuple[str, str], str]:
    body, end = _table_span(source)
    known: Dict[Tuple[str, str], str] = {}
    for line in source[body:end].splitlines():
        match = ENTRY.match(line)
        if match:
            known[(match["version"], match["platform"])] = match["sha"].lower()
        elif line.strip():
            raise InputError("cannot read this KNOWN_RELEASES line: %s" % line.strip())
    return known


def entry_lines(version: str, hashes: Dict[str, str]) -> str:
    return "".join('    ("%s", "%s"): "%s",\n' % (version, platform, hashes[platform]) for platform in sorted(hashes))


def check(version: str, hashes: Dict[str, str], known: Dict[Tuple[str, str], str], require: bool) -> list:
    """The problems, as sentences; empty when the table agrees."""
    problems = []
    for platform in sorted(hashes):
        pinned: Optional[str] = known.get((version, platform))
        if pinned is None:
            if require:
                problems.append("KNOWN_RELEASES has no (%r, %r) entry" % (version, platform))
        elif pinned != hashes[platform]:
            problems.append(
                "KNOWN_RELEASES pins (%r, %r) to %s, but the library built is %s"
                % (version, platform, pinned, hashes[platform])
            )
    return problems


def pin(source: str, version: str, hashes: Dict[str, str]) -> str:
    body, end = _table_span(source)
    kept = [line for line in source[body:end].splitlines(True) if not (ENTRY.match(line) and ENTRY.match(line)["version"] == version)]
    return source[:body] + "".join(kept) + entry_lines(version, hashes) + source[end:]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("check", "pin"):
        p = sub.add_parser(name)
        p.add_argument("version")
        p.add_argument("sha256sums", type=Path)
        if name == "check":
            p.add_argument("--require", action="store_true", help="a missing entry fails too")
    sub.add_parser("version")
    args = parser.parse_args(argv)

    try:
        if args.command == "version":
            print(project_version())
            return 0
        hashes = parse_sha256sums(args.sha256sums.read_text())
        source = LOADER.read_text()
        if args.command == "pin":
            LOADER.write_text(pin(source, args.version, hashes))
            print("pinned v%s in %s:\n%s" % (args.version, LOADER.relative_to(ROOT), entry_lines(args.version, hashes)), end="")
            return 0
        problems = check(args.version, hashes, read_known(source), args.require)
    except (InputError, OSError) as e:
        print("known_releases: %s" % e, file=sys.stderr)
        return 2

    print("KNOWN_RELEASES lines for v%s, from %s:\n%s" % (args.version, args.sha256sums, entry_lines(args.version, hashes)), end="")
    for problem in problems:
        print("known_releases: %s" % problem, file=sys.stderr)
    if problems:
        print(
            "known_releases: pin them with `python3 tools/known_releases.py pin %s <SHA256SUMS>` "
            "and commit bindings/python/strata/_loader.py" % args.version,
            file=sys.stderr,
        )
        return 1
    print("KNOWN_RELEASES agrees with the libraries for v%s" % args.version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
