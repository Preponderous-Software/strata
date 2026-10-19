#!/usr/bin/env bash
# Prints the prebuilt OpenUSD artifact key: openusd-<tag>-<toolchain>-<hash>,
# where <hash> covers the lock file and the build script.
#
#   key.sh            Linux (build-openusd.sh, toolchain from the lock)
#   key.sh windows    Windows (build-openusd-windows.sh, MSVC on windows-2022)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source <(grep -E '^[A-Z_]+=' "$here/openusd.lock" | sed 's/^\([A-Z_]*\)=\(.*\)$/\1="\2"/')
if [ "${1:-}" = windows ]; then
  # Not in the lock: a line there would change the Linux key too.
  toolchain=windows2022-msvc143
  script="$here/build-openusd-windows.sh"
else
  toolchain="$OPENUSD_TOOLCHAIN"
  script="$here/build-openusd.sh"
fi
hash="$(cat "$here/openusd.lock" "$script" | sha256sum | cut -c1-12)"
echo "openusd-${OPENUSD_TAG}-${toolchain}-${hash}"
