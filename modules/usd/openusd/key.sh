#!/usr/bin/env bash
# Prints the prebuilt OpenUSD artifact key: openusd-<tag>-<toolchain>-<hash>,
# where <hash> covers the lock file and the build script.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source <(grep -E '^[A-Z_]+=' "$here/openusd.lock" | sed 's/^\([A-Z_]*\)=\(.*\)$/\1="\2"/')
hash="$(cat "$here/openusd.lock" "$here/build-openusd.sh" | sha256sum | cut -c1-12)"
echo "openusd-${OPENUSD_TAG}-${OPENUSD_TOOLCHAIN}-${hash}"
