#!/usr/bin/env bash
# Windows counterpart of build-openusd.sh: builds OpenUSD as pinned in
# openusd.lock with MSVC and packs <out>/<key>.tar.gz (top-level <key>/: an
# install prefix with lib/, bin/, include/, pxrConfig.cmake). Run from Git
# Bash in an environment set up by vcvars64.bat; needs git, python, cmake.
#
#   build-openusd-windows.sh <out dir> [jobs]
#
# At run time OpenUSD finds its plugins relative to its own DLLs
# (lib/usd/plugInfo.json), so consumers put <prefix>\lib and <prefix>\bin on
# PATH rather than copying the DLLs beside their executable.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mkdir -p "$1" && cd "$1" && pwd)"
jobs="${2:-4}"
# shellcheck disable=SC1091
source <(grep -E '^[A-Z_]+=' "$here/openusd.lock" | sed 's/^\([A-Z_]*\)=\(.*\)$/\1="\2"/')
key="$("$here/key.sh" windows)"
# A short root: MSVC and MSBuild still trip over paths near MAX_PATH.
work="${OPENUSD_WORK:-/c/ousd}"
rm -rf "$work"
mkdir -p "$work"
git clone --depth 1 --branch "$OPENUSD_TAG" https://github.com/PixarAnimationStudios/OpenUSD.git "$work/src"
actual="$(git -C "$work/src" rev-parse HEAD)"
if [ "$actual" != "$OPENUSD_COMMIT" ]; then
  echo "::error::tag $OPENUSD_TAG is $actual, expected $OPENUSD_COMMIT" >&2
  exit 1
fi
prefix="$work/$key"
# build_usd.py is a Windows Python: hand it Windows paths.
# shellcheck disable=SC2086
python "$(cygpath -w "$work/src/build_scripts/build_usd.py")" $OPENUSD_FLAGS -j "$jobs" \
  --src "$(cygpath -w "$work/deps-src")" "$(cygpath -w "$prefix")" > "$work/build.log" 2>&1 \
  || { tail -200 "$work/build.log"; exit 1; }
tail -5 "$work/build.log"
# Dependencies (oneTBB) install into the same prefix, so the tarball is self-contained.
for f in lib/usd_usd.lib lib/usd_usd.dll bin/tbb12.dll lib/cmake/TBB/TBBConfig.cmake include/oneapi/tbb.h pxrConfig.cmake; do
  [ -e "$prefix/$f" ] || { echo "::error::prebuilt is missing $f" >&2; exit 1; }
done
# Ship the licences beside the libraries (TOST 1.0 / Apache-2.0 notices).
mkdir -p "$prefix/share/licenses"
cp "$work/src/LICENSE.txt" "$prefix/share/licenses/OpenUSD-LICENSE.txt"
find "$work/deps-src" -maxdepth 2 -iname 'LICENSE*' -path '*oneTBB*' -exec cp {} "$prefix/share/licenses/oneTBB-LICENSE.txt" \; || true
cp "$here/openusd.lock" "$prefix/openusd.lock"
rm -rf "$prefix/src" "$prefix/build" "$prefix/share/doc" 2>/dev/null || true
find "$prefix" -name '*.pdb' -delete || true
tar -C "$work" -czf "$out/$key.tar.gz" "$key"
du -sh "$prefix" "$out/$key.tar.gz"
echo "$key" > "$out/key.txt"
