#!/usr/bin/env bash
# Builds Faxwerk.app for macOS and packages it as a disk image. Needs Homebrew
# and the Xcode command line tools:
#
#   brew install qt libtiff jpeg-turbo autoconf automake libtool ninja pkgconf cmake
#
# Output in out/:
#   Faxwerk-<version>-macos-<arch>.dmg
# and build/macos/dist/Faxwerk.app, which can also be copied to /Applications
# directly.
#
# Without an Apple Developer ID the app is only signed ad hoc (required on
# Apple Silicon). Gatekeeper then blocks the first start of a downloaded copy;
# see README.md, "Installing on macOS".
#
# Environment:
#   FAXWERK_VERSION  version in the file name (default: git describe)
#   SKIP_TESTS=1     do not run ctest
#   QT_PREFIX        Qt installation (default: brew --prefix qt)
#   OUT_DIR          output directory (default: out/)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$REPO_DIR/build/macos"
DIST="$WORK/dist"
OUT_DIR="${OUT_DIR:-$REPO_DIR/out}"
SPANDSP_PREFIX="${SPANDSP_PREFIX:-$REPO_DIR/build/spandsp/prefix}"
QT_PREFIX="${QT_PREFIX:-$(brew --prefix qt)}"
TIFF_PREFIX="$(brew --prefix libtiff)"
ARCH="$(uname -m)"

VERSION="${FAXWERK_VERSION:-$(git -C "$REPO_DIR" describe --tags --always --dirty 2>/dev/null || echo dev)}"
VERSION="${VERSION#v}"

# --- Build and test ------------------------------------------------------------------
"$REPO_DIR/scripts/build-spandsp.sh" "$SPANDSP_PREFIX"

cmake -S "$REPO_DIR" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX;$TIFF_PREFIX;$SPANDSP_PREFIX"
cmake --build "$WORK/build"
if [ "${SKIP_TESTS:-0}" != "1" ]; then
    ctest --test-dir "$WORK/build" --output-on-failure
fi

# --- App bundle ------------------------------------------------------------------------
APP_SRC="$(find "$WORK/build/src/gui" -maxdepth 1 -name '*.app' | head -n 1)"
APP_NAME="$(basename "$APP_SRC")"
APP="$DIST/$APP_NAME"
rm -rf "$DIST"
mkdir -p "$DIST"
cp -R "$APP_SRC" "$APP"

# The command line tool lives in the bundle too, so that it uses the bundled
# libraries. Contents/MacOS is no option: "faxwerk" and "Faxwerk" are the same
# name on the usual case-insensitive file system.
mkdir -p "$APP/Contents/Helpers"
cp "$WORK/build/src/cli/faxwerk" "$APP/Contents/Helpers/faxwerk"

# Copies Qt and the other libraries (spandsp, libtiff, ...) into the bundle.
"$QT_PREFIX/bin/macdeployqt" "$APP" \
    -executable="$APP/Contents/Helpers/faxwerk" \
    -always-overwrite

# macdeployqt changes the binaries, which breaks their signatures. Sign
# everything again ad hoc ("-"): Apple Silicon refuses unsigned code.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "App: $APP"

# --- Disk image ------------------------------------------------------------------------
mkdir -p "$OUT_DIR"
DMG="$OUT_DIR/Faxwerk-$VERSION-macos-$ARCH.dmg"
STAGE="$WORK/dmg"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
cp "$REPO_DIR/LICENSE" "$STAGE/LICENSE.txt"
ln -s /Applications "$STAGE/Programme"
hdiutil create -volname "${APP_NAME%.app}" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Disk image: $DMG"
