#!/usr/bin/env bash
# Builds OpenUSD as pinned in openusd.lock and packs <out>/<key>.tar.gz, whose
# top-level directory is <key>/ (an install prefix: lib/, include/, plugin/,
# pxrConfig.cmake). Run inside OPENUSD_IMAGE; needs git, python3, cmake, g++.
#
#   build-openusd.sh <out dir> [jobs]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mkdir -p "$1" && cd "$1" && pwd)"
jobs="${2:-4}"
# shellcheck disable=SC1091
source <(grep -E '^[A-Z_]+=' "$here/openusd.lock" | sed 's/^\([A-Z_]*\)=\(.*\)$/\1="\2"/')
key="$("$here/key.sh")"
work="$(mktemp -d)"
git clone --depth 1 --branch "$OPENUSD_TAG" https://github.com/PixarAnimationStudios/OpenUSD.git "$work/src"
actual="$(git -C "$work/src" rev-parse HEAD)"
if [ "$actual" != "$OPENUSD_COMMIT" ]; then
  echo "::error::tag $OPENUSD_TAG is $actual, expected $OPENUSD_COMMIT" >&2
  exit 1
fi
prefix="$work/$key"
# shellcheck disable=SC2086
python3 "$work/src/build_scripts/build_usd.py" $OPENUSD_FLAGS -j "$jobs" \
  --src "$work/deps-src" --inst "$work/deps-inst" "$prefix" > "$work/build.log" 2>&1 \
  || { tail -200 "$work/build.log"; exit 1; }
tail -5 "$work/build.log"
# Ship the licences beside the libraries (TOST 1.0 / Apache-2.0 notices).
mkdir -p "$prefix/share/licenses"
cp "$work/src/LICENSE.txt" "$prefix/share/licenses/OpenUSD-LICENSE.txt"
find "$work/deps-src" -maxdepth 2 -iname 'LICENSE*' -path '*oneTBB*' -exec cp {} "$prefix/share/licenses/oneTBB-LICENSE.txt" \; || true
cp "$here/openusd.lock" "$prefix/openusd.lock"
find "$prefix/lib" -name '*.so*' -type f -exec strip --strip-debug {} + || true
rm -rf "$prefix/src" "$prefix/build" "$prefix/share/doc" 2>/dev/null || true
tar -C "$work" -czf "$out/$key.tar.gz" "$key"
du -sh "$prefix" "$out/$key.tar.gz"
echo "$key" > "$out/key.txt"
