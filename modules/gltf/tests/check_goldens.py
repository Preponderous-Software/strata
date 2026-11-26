#!/usr/bin/env python3
"""Golden images for the glTF module, by docs/GOLDEN_IMAGES.md's rules.

Each case renders one synthetic asset (tests/assets, from make_fixtures.py)
with strata-gltf-view and compares it with
modules/gltf/tests/golden/<name>/<platform>.png using strata-imgcmp (CIE76
delta E over 4.0 counts as different; at most 0.5 % of pixels may differ).

    check_goldens.py --view build/modules/gltf/strata-gltf-view \\
                     --imgcmp build/strata-imgcmp --out build/gltf-golden-out [--bless] [--only NAME]

Writes <out>/<name>.actual.png and, on a failure, <name>.diff.png. Exit 0
when every case passes, 1 on a failure, 77 with --skip-without-gl when the
engine cannot be created (no headless GL), 2 on a usage error.
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSETS = HERE / "assets"
GOLDEN = HERE / "golden"

# name, asset, strata-gltf-view arguments
CASES = [
    ("gltf_cube", "cube.gltf", []),
    ("gltf_textured_quad", "textured_quad.glb", ["--camera", "front", "--sun", "noon"]),
    ("gltf_hierarchy", "hierarchy.gltf", ["--sun", "morning"]),
    ("gltf_vertex_colors", "vertex_colors.glb", ["--sun", "noon"]),
    ("gltf_alpha_modes", "alpha_modes.gltf", ["--camera", "front", "--sun", "noon", "--zoom", "0.8"]),
    # Over budget: one mesh, so the repeating grid texture becomes its average colour.
    ("gltf_hierarchy_one_mesh", "hierarchy.gltf", ["--sun", "morning", "--max-meshes", "1"]),
    # Over budget: one texture, so the quad is atlased and the repeating ground dropped.
    ("gltf_textured_quad_atlas", "textured_quad.glb", ["--camera", "front", "--sun", "noon", "--max-textures", "1"]),
]


def main(argv) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--view", required=True, help="the strata-gltf-view executable")
    parser.add_argument("--imgcmp", required=True, help="the strata-imgcmp executable")
    parser.add_argument("--out", required=True, help="directory for actual and diff images")
    parser.add_argument("--platform", default="linux-llvmpipe")
    parser.add_argument("--only", help="run one case")
    parser.add_argument("--bless", action="store_true", help="write the actual images as the goldens")
    parser.add_argument("--skip-without-gl", action="store_true", help="exit 77 when no engine can be created")
    args = parser.parse_args(argv)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cases = [c for c in CASES if not args.only or c[0] == args.only]
    if not cases:
        print("no case named %s" % args.only, file=sys.stderr)
        return 2
    failed = []
    for name, asset, extra in cases:
        actual = out / (name + ".actual.png")
        run = subprocess.run([args.view, str(ASSETS / asset), str(actual), "--quiet"] + extra,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        if run.returncode != 0:
            if args.skip_without_gl and "strata_create failed" in run.stderr:
                print("skipped: no headless GL here\n" + run.stderr, file=sys.stderr)
                return 77
            print("%s: strata-gltf-view failed (%d)\n%s" % (name, run.returncode, run.stderr), file=sys.stderr)
            failed.append(name)
            continue
        golden = GOLDEN / name / (args.platform + ".png")
        if args.bless:
            golden.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(actual, golden)
            print("  blessed %s" % golden)
            continue
        if not golden.is_file():
            print("  FAIL %s: no golden at %s" % (name, golden))
            failed.append(name)
            continue
        cmp = subprocess.run([args.imgcmp, str(golden), str(actual), "--diff", str(out / (name + ".diff.png"))],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        status = "ok  " if cmp.returncode == 0 else "FAIL"
        print("  %s %s: %s" % (status, name, cmp.stdout.strip()))
        if cmp.returncode != 0:
            failed.append(name)
    if failed:
        print("%d of %d glTF golden(s) failed: %s" % (len(failed), len(cases), ", ".join(failed)))
        return 1
    print("%d glTF golden(s) %s" % (len(cases), "blessed" if args.bless else "passed"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
