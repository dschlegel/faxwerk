#!/usr/bin/env bash
# Builds a Linux AppImage (x86_64) of the current working tree into out/.
# Meant to run inside the dev container: scripts/dev.sh appimage
#
# The AppImage contains the desktop app (faxwerk-gui, started by default) and
# the command line tool. Qt, spandsp, libtiff, QtKeychain and libappimageupdate
# (for the self-update of the desktop app) are bundled by linuxdeploy.
# QtKeychain loads libsecret of the system at runtime (Secret Service) or
# talks to KWallet over D-Bus; neither is bundled.
#
# Environment:
#   FAXWERK_UPDATE_INFORMATION  AppImage update information, embedded so that
#                               AppImageUpdate can fetch new versions. Default:
#                                 gh-releases-zsync|dschlegel|faxwerk|latest|Faxwerk-*-x86_64.AppImage.zsync
#                               (the latest GitHub release of dschlegel/faxwerk).
#                               "none": no update information, no .zsync file.
#   FAXWERK_APPIMAGE_VERSION    version in the file name (default: git describe)
#   FAXWERK_APPIMAGEUPDATE      CMake option for the self-update (default: ON, i.e.
#                               fail without libappimageupdate; OFF builds without)
#   OUT_DIR                     output directory (default: out/)
#   LINUXDEPLOY_URL, LINUXDEPLOY_QT_URL
#                               download locations of the tools (default: continuous builds)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$REPO_DIR/build/appimage"
TOOLS="$WORK/tools"
APPDIR="$WORK/AppDir"
OUT_DIR="${OUT_DIR:-$REPO_DIR/out}"
ARCH=x86_64

LINUXDEPLOY_URL="${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage}"
LINUXDEPLOY_QT_URL="${LINUXDEPLOY_QT_URL:-https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage}"

VERSION="${FAXWERK_APPIMAGE_VERSION:-$(git -C "$REPO_DIR" describe --tags --always --dirty 2>/dev/null || echo dev)}"
VERSION="${VERSION#v}"
OUTPUT="Faxwerk-$VERSION-$ARCH.AppImage"
DEFAULT_UPDATE_INFORMATION="gh-releases-zsync|dschlegel|faxwerk|latest|Faxwerk-*-$ARCH.AppImage.zsync"
UPDATE_INFO="${FAXWERK_UPDATE_INFORMATION:-$DEFAULT_UPDATE_INFORMATION}"

# --- Tools -------------------------------------------------------------------------
mkdir -p "$TOOLS"
fetch() {
    local url="$1" file="$2"
    if [ ! -x "$TOOLS/$file" ]; then
        echo "Downloading $file"
        curl -fsSL -o "$TOOLS/$file.part" "$url"
        chmod +x "$TOOLS/$file.part"
        mv "$TOOLS/$file.part" "$TOOLS/$file"
    fi
}
fetch "$LINUXDEPLOY_URL" "linuxdeploy-$ARCH.AppImage"
fetch "$LINUXDEPLOY_QT_URL" "linuxdeploy-plugin-qt-$ARCH.AppImage"

# --- Build and install into the AppDir ---------------------------------------------------
cmake -S "$REPO_DIR" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DFAXWERK_BUILD_TESTS=OFF \
    -DFAXWERK_KEYCHAIN=ON \
    -DFAXWERK_APPIMAGEUPDATE="${FAXWERK_APPIMAGEUPDATE:-ON}"
cmake --build "$WORK/build"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$WORK/build"

# --- Package ---------------------------------------------------------------------------
# Containers usually have no FUSE: let the tool AppImages unpack themselves.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$TOOLS:$PATH"
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
# The command line tool uses the offscreen platform when there is no display.
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export LDAI_OUTPUT="$OUTPUT"
export OUTPUT
if [ "$UPDATE_INFO" != none ]; then
    export LDAI_UPDATE_INFORMATION="$UPDATE_INFO"
    export UPDATE_INFORMATION="$UPDATE_INFO"
    echo "Update information: $UPDATE_INFO"
else
    unset LDAI_UPDATE_INFORMATION UPDATE_INFORMATION
    echo "Update information disabled (FAXWERK_UPDATE_INFORMATION=none); building without .zsync"
fi

mkdir -p "$OUT_DIR"
cd "$OUT_DIR"
rm -f "$OUTPUT" "$OUTPUT.zsync"
# Two steps: deploy first, then package. In between the AppDir gets a PNG
# .DirIcon, because appimagetool (since its build of 2026-10-31) no longer
# accepts an AppDir whose only top-level icon is the SVG.
"linuxdeploy-$ARCH.AppImage" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/faxwerk-gui" \
    --executable "$APPDIR/usr/bin/faxwerk" \
    --desktop-file "$APPDIR/usr/share/applications/faxwerk.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/faxwerk.svg" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/faxwerk.png" \
    --plugin qt
rm -f "$APPDIR/.DirIcon"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/faxwerk.png" "$APPDIR/.DirIcon"
"linuxdeploy-$ARCH.AppImage" --appdir "$APPDIR" --output appimage

if [ "${FAXWERK_APPIMAGEUPDATE:-ON}" != OFF ] && ! compgen -G "$APPDIR/usr/lib/libappimageupdate.so*" >/dev/null; then
    echo "libappimageupdate was not bundled into the AppImage" >&2
    exit 1
fi

echo
echo "Built: $OUT_DIR/$OUTPUT"
[ -f "$OUTPUT.zsync" ] && echo "Update file: $OUT_DIR/$OUTPUT.zsync"
exit 0
