#!/usr/bin/env bash
# Builds upstream spandsp (freeswitch/spandsp) as a shared library into a
# prefix. Windows (MSYS2 UCRT64) and macOS have no usable spandsp package, so
# their packaging scripts call this first.
#
# Usage: scripts/build-spandsp.sh [prefix]     (default: build/spandsp/prefix)
#
# Environment:
#   SPANDSP_REF   git ref to build (default: master)
#
# Nothing is rebuilt when the prefix already holds a build of the same ref.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${1:-$REPO_DIR/build/spandsp/prefix}"
REF="${SPANDSP_REF:-master}"
SRC="$REPO_DIR/build/spandsp/src"

if [ -f "$PREFIX/lib/pkgconfig/spandsp.pc" ] && [ "$(cat "$PREFIX/.faxwerk-ref" 2>/dev/null)" = "$REF" ]; then
    echo "spandsp $REF is already installed in $PREFIX"
    exit 0
fi

mkdir -p "$(dirname "$SRC")"
if [ ! -d "$SRC/.git" ]; then
    git clone https://github.com/freeswitch/spandsp.git "$SRC"
fi
git -C "$SRC" fetch --quiet origin
git -C "$SRC" checkout --quiet "$REF"
git -C "$SRC" log -1 --format='spandsp: %H %cd'

MAKE_ARGS=()
case "$(uname -s)" in
    Darwin)
        # Homebrew installs GNU libtoolize as glibtoolize; spandsp needs libtiff's headers.
        if command -v brew >/dev/null; then
            export PATH="$(brew --prefix libtool)/libexec/gnubin:$PATH"
            TIFF_PREFIX="$(brew --prefix libtiff)"
            export CPPFLAGS="-I$TIFF_PREFIX/include ${CPPFLAGS:-}"
            export LDFLAGS="-L$TIFF_PREFIX/lib ${LDFLAGS:-}"
        fi
        JOBS="$(sysctl -n hw.ncpu)"
        ;;
    MINGW*)
        # spandsp's headers declare its functions as DLL imports unless
        # LIBSPANDSP_EXPORTS is set, and upstream doesn't set it for MinGW:
        # set it for the library and for the code generators that "make"
        # builds from spandsp sources. Programs using the DLL leave it unset.
        export CPPFLAGS="-DLIBSPANDSP_EXPORTS ${CPPFLAGS:-}"
        MAKE_ARGS=("CC_FOR_BUILD=gcc -DLIBSPANDSP_EXPORTS")
        JOBS="$(nproc)"
        ;;
    *)
        JOBS="$(nproc)"
        ;;
esac

cd "$SRC"
git clean -fdxq
# What bootstrap.sh/autogen.sh do after their version checks. The checks find
# "which" through whereis, which yields its man page on MSYS2.
mkdir -p config
autoreconf -fi
./configure --prefix="$PREFIX" --disable-static --enable-shared
make -j"$JOBS" ${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}
make install ${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}
echo "$REF" > "$PREFIX/.faxwerk-ref"
echo "spandsp installed in $PREFIX"
