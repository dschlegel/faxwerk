#!/usr/bin/env bash
# Builds upstream spandsp (freeswitch/spandsp, the 3.x API that is needed on
# Windows/macOS) from GitHub into build/upstream/prefix and then configures,
# builds and tests Faxwerk against it. Meant to run inside the dev container.
#
# Usage: scripts/build-upstream-spandsp.sh [git-ref]
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REF="${1:-master}"
WORK="$REPO_DIR/build/upstream"
SRC="$WORK/spandsp"
PREFIX="$WORK/prefix"
FAXWERK_BUILD="$WORK/faxwerk"

mkdir -p "$WORK"
if [ ! -d "$SRC/.git" ]; then
    git clone https://github.com/freeswitch/spandsp.git "$SRC"
fi
git -C "$SRC" fetch --quiet origin
git -C "$SRC" checkout --quiet "$REF"
git -C "$SRC" log -1 --format='spandsp upstream: %H %cd'

if [ ! -f "$PREFIX/lib/pkgconfig/spandsp.pc" ] || [ "$SRC/.git/HEAD" -nt "$PREFIX/lib/pkgconfig/spandsp.pc" ]; then
    (
        cd "$SRC"
        ./bootstrap.sh >/dev/null
        ./configure --prefix="$PREFIX" --disable-static --enable-shared >/dev/null
        make -j"$(nproc)" >/dev/null
        make install >/dev/null
    )
fi

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cmake -S "$REPO_DIR" -B "$FAXWERK_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFAXWERK_WERROR=ON \
    -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build "$FAXWERK_BUILD"
grep -q 'FAXWERK_SPANDSP_NEW_API:INTERNAL=1' "$FAXWERK_BUILD/CMakeCache.txt" \
    && echo "Detected new (upstream) spandsp API." \
    || { echo "error: upstream spandsp API was not detected" >&2; exit 1; }
ctest --test-dir "$FAXWERK_BUILD" --output-on-failure
