#!/usr/bin/env bash
# Builds Faxwerk for Windows and packages it. Run it in an MSYS2 UCRT64 shell
# with these packages installed:
#
#   pacman -S --needed git make autoconf automake libtool \
#       mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,libtiff,qt6-base,qt6-pdf}
#
# Output in out/:
#   Faxwerk-<version>-windows-x64-setup.exe   installer (needs Inno Setup 6)
# and the ready-to-run program folder in build/windows/dist.
#
# Environment:
#   FAXWERK_VERSION  version in the file names (default: git describe)
#   SKIP_TESTS=1     do not run ctest
#   ISCC             path of the Inno Setup compiler (default: searched)
#   OUT_DIR          output directory (default: out/)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$REPO_DIR/build/windows"
DIST="$WORK/dist"
OUT_DIR="${OUT_DIR:-$REPO_DIR/out}"
SPANDSP_PREFIX="${SPANDSP_PREFIX:-$REPO_DIR/build/spandsp/prefix}"

if [ "${MSYSTEM:-}" != "UCRT64" ]; then
    echo "error: run this script in an MSYS2 UCRT64 shell" >&2
    exit 2
fi

VERSION="${FAXWERK_VERSION:-$(git -C "$REPO_DIR" describe --tags --always --dirty 2>/dev/null || echo dev)}"
VERSION="${VERSION#v}"
# Windows file versions must be numeric.
if [[ "$VERSION" =~ ^([0-9]+\.[0-9]+\.[0-9]+) ]]; then
    NUMERIC_VERSION="${BASH_REMATCH[1]}"
else
    NUMERIC_VERSION="0.0.0"
fi

# --- Build and test ------------------------------------------------------------------
"$REPO_DIR/scripts/build-spandsp.sh" "$SPANDSP_PREFIX"
export PATH="$SPANDSP_PREFIX/bin:$PATH"

cmake -S "$REPO_DIR" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$SPANDSP_PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$WORK/install"
cmake --build "$WORK/build"
if [ "${SKIP_TESTS:-0}" != "1" ]; then
    ctest --test-dir "$WORK/build" --output-on-failure
fi

# --- Program folder --------------------------------------------------------------------
rm -rf "$WORK/install" "$DIST"
cmake --install "$WORK/build"
mkdir -p "$DIST"
cp "$WORK/install/bin/faxwerk-gui.exe" "$WORK/install/bin/faxwerk.exe" "$DIST/"

WINDEPLOYQT="$(command -v windeployqt6 || command -v windeployqt)"
"$WINDEPLOYQT" --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
    "$DIST/faxwerk-gui.exe" "$DIST/faxwerk.exe"

# windeployqt only copies Qt. Add every other DLL the programs and plugins load
# (MinGW runtime, spandsp, libtiff and their dependencies) until nothing is missing.
while true; do
    added=0
    while IFS= read -r dll; do
        name="$(basename "$dll")"
        if [ ! -e "$DIST/$name" ]; then
            cp "$dll" "$DIST/"
            added=1
        fi
    done < <(find "$DIST" -iname '*.exe' -o -iname '*.dll' | while IFS= read -r file; do
                 ldd "$file" | awk '$3 ~ /^\// { print $3 }'
             done | grep -viE '^/c/windows/' | sort -u)
    [ "$added" = 0 ] && break
done

cp "$REPO_DIR/README.md" "$DIST/README.md"
echo "Program folder: $DIST"

# --- Installer -------------------------------------------------------------------------
if [ -z "${ISCC:-}" ]; then
    for candidate in "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
        [ -x "$candidate" ] && ISCC="$candidate" && break
    done
    [ -z "${ISCC:-}" ] && ISCC="$(command -v iscc || true)"
fi
if [ -z "${ISCC:-}" ]; then
    echo "Inno Setup not found (set ISCC); skipping the installer" >&2
    exit 0
fi

mkdir -p "$OUT_DIR"
OUTPUT_BASE="Faxwerk-$VERSION-windows-x64-setup"
# MSYS2 would rewrite the /D options as paths.
MSYS2_ARG_CONV_EXCL='*' "$ISCC" \
    "/DAppVersion=$VERSION" \
    "/DAppNumericVersion=$NUMERIC_VERSION" \
    "/DSourceDir=$(cygpath -w "$DIST")" \
    "/DOutputDir=$(cygpath -w "$OUT_DIR")" \
    "/DOutputBaseName=$OUTPUT_BASE" \
    "$(cygpath -w "$REPO_DIR/packaging/windows/faxwerk.iss")"
echo "Installer: $OUT_DIR/$OUTPUT_BASE.exe"
