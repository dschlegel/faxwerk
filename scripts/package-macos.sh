#!/usr/bin/env bash
# Builds Faxwerk.app for macOS and packages it as a disk image and as an
# installer package. Needs Homebrew and the Xcode command line tools:
#
#   brew install qt libtiff jpeg-turbo autoconf automake libtool ninja pkgconf cmake
#
# Output in out/:
#   Faxwerk-<version>-macos-<arch>.dmg   app to drag to /Applications
#   Faxwerk-<version>-macos-<arch>.pkg   installer that puts the app into /Applications
#                                        and the command line tool into /usr/local/bin
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
#   PKG_SIGN_IDENTITY
#                    "Developer ID Installer: ..." certificate in the keychain to
#                    sign the .pkg with (default: unsigned)
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

# macdeployqt misses libraries that other non-Qt libraries load through
# @rpath (e.g. libsharpyuv, used by libwebp of Qt's image plugins), and the
# app then dies at launch with "Library not loaded" on every Mac without
# Homebrew. Copy what is missing into Contents/Frameworks, point the
# references there and stop if anything still refers outside the bundle.
FRAMEWORKS="$APP/Contents/Frameworks"
BREW_LIB="$(brew --prefix)/lib"

macho_files() {
    find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) | while IFS= read -r file; do
        file -b "$file" | grep -q 'Mach-O' && echo "$file"
    done
}

# The libraries a binary loads, without its own install name.
dependencies() {
    otool -L "$1" | tail -n +2 | awk '{ print $1 }' | grep -vx "$(otool -D "$1" | tail -n +2)"
}

# macdeployqt copies some plugins without the Qt frameworks they need (the
# virtual keyboard, the SVG icon engine). Faxwerk uses neither: drop plugins
# whose frameworks are not in the bundle, as Qt couldn't load them anyway.
drop_incomplete_plugins() {
    local plugin dep
    find "$APP/Contents/PlugIns" -name '*.dylib' | while IFS= read -r plugin; do
        while IFS= read -r dep; do
            case "$dep" in
                @rpath/*.framework/*)
                    if [ ! -e "$FRAMEWORKS/${dep#@rpath/}" ]; then
                        echo "Removing ${plugin#$APP/} (needs ${dep#@rpath/})"
                        rm -f "$plugin"
                        break
                    fi
                    ;;
            esac
        done < <(dependencies "$plugin")
    done
}

bundle_libraries() {
    local changed=1 missing
    while [ "$changed" = 1 ]; do
        changed=0
        while IFS= read -r file; do
            while IFS= read -r dep; do
                local name="${dep##*/}" source=""
                case "$dep" in
                    /System/*|/usr/lib/*|@executable_path/*|@loader_path/*) continue ;;
                    @rpath/*.framework/*) continue ;; # Qt frameworks, deployed by macdeployqt
                    @rpath/*) [ -e "$FRAMEWORKS/$name" ] || source="$BREW_LIB/$name" ;;
                    /*) source="$dep" ;;
                esac
                if [ -n "$source" ] && [ ! -e "$FRAMEWORKS/$name" ]; then
                    # Keg-only formulas are not linked into $(brew --prefix)/lib.
                    [ -e "$source" ] || source="$(find "$(brew --prefix)/opt/" -maxdepth 3 -name "$name" | head -n 1)"
                    [ -e "$source" ] || { echo "error: $file needs $dep, not found" >&2; exit 1; }
                    echo "Adding $name (needed by ${file#$APP/})"
                    cp -L "$source" "$FRAMEWORKS/$name"
                    chmod u+w "$FRAMEWORKS/$name"
                    install_name_tool -id "@executable_path/../Frameworks/$name" "$FRAMEWORKS/$name" 2>/dev/null
                    changed=1
                fi
                echo "Pointing ${file#$APP/} to Frameworks/$name"
                install_name_tool -change "$dep" "@executable_path/../Frameworks/$name" "$file" 2>/dev/null
            done < <(dependencies "$file")
        done < <(macho_files)
    done

    # Every reference must now resolve inside the bundle or to the system.
    missing=0
    while IFS= read -r file; do
        while IFS= read -r dep; do
            case "$dep" in
                /System/*|/usr/lib/*) ;;
                @rpath/*.framework/*) [ -e "$FRAMEWORKS/${dep#@rpath/}" ] || { echo "missing: $dep in $file" >&2; missing=1; } ;;
                @executable_path/../Frameworks/*) [ -e "$FRAMEWORKS/${dep##*/}" ] || { echo "missing: $dep in $file" >&2; missing=1; } ;;
                @loader_path/*) [ -e "$(dirname "$file")/${dep#@loader_path/}" ] || { echo "missing: $dep in $file" >&2; missing=1; } ;;
                *) echo "outside the bundle: $dep in $file" >&2; missing=1 ;;
            esac
        done < <(dependencies "$file")
    done < <(macho_files)
    [ "$missing" = 0 ] || { echo "error: the app bundle is incomplete" >&2; exit 1; }
}
drop_incomplete_plugins
bundle_libraries

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

# --- Installer package ---------------------------------------------------------------
# Installs the app into /Applications. Files put there by the installer get no
# quarantine flag, so after the package itself is allowed once the app starts
# directly. Not relocatable: the installer must not look for another copy of
# the app elsewhere and update that one instead.
PKG="$OUT_DIR/Faxwerk-$VERSION-macos-$ARCH.pkg"
PKG_ROOT="$WORK/pkgroot"
PKG_WORK="$WORK/pkg"
BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist")"
# Installer versions must be numeric: the project version from CMakeLists.txt,
# not git describe (e.g. 0.1.0-3-gabc1234).
PKG_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")"
rm -rf "$PKG_ROOT" "$PKG_WORK" "$PKG"
mkdir -p "$PKG_ROOT" "$PKG_WORK"
ditto "$APP" "$PKG_ROOT/$APP_NAME"
pkgbuild --analyze --root "$PKG_ROOT" "$PKG_WORK/component.plist"
/usr/libexec/PlistBuddy -c 'Set :0:BundleIsRelocatable false' "$PKG_WORK/component.plist"
# postinstall: the command line tool as /usr/local/bin/faxwerk. A small
# script rather than a symlink, so that the tool keeps finding the bundle's
# Qt plugins through its real path. A file there that is not ours stays.
mkdir -p "$PKG_WORK/scripts"
cat > "$PKG_WORK/scripts/postinstall" <<'POSTINSTALL'
#!/bin/sh
# $3: target volume
BIN_DIR="${3%/}/usr/local/bin"
TOOL="$BIN_DIR/faxwerk"
MARKER="# Faxwerk command line tool, installed by the Faxwerk package"
mkdir -p "$BIN_DIR" || exit 0
if [ -e "$TOOL" ] || [ -L "$TOOL" ]; then
    grep -q "$MARKER" "$TOOL" 2>/dev/null || exit 0
    rm -f "$TOOL"
fi
cat > "$TOOL" <<EOF
#!/bin/sh
$MARKER
exec "/Applications/@APP_NAME@/Contents/Helpers/faxwerk" "\$@"
EOF
chmod 755 "$TOOL"
exit 0
POSTINSTALL
sed -i '' "s|@APP_NAME@|$APP_NAME|" "$PKG_WORK/scripts/postinstall"
chmod 755 "$PKG_WORK/scripts/postinstall"
pkgbuild \
    --root "$PKG_ROOT" \
    --scripts "$PKG_WORK/scripts" \
    --component-plist "$PKG_WORK/component.plist" \
    --install-location /Applications \
    --identifier "$BUNDLE_ID" \
    --version "$PKG_VERSION" \
    "$PKG_WORK/Faxwerk-component.pkg"
sign_args=()
if [ -n "${PKG_SIGN_IDENTITY:-}" ]; then
    sign_args=(--sign "$PKG_SIGN_IDENTITY")
fi
productbuild \
    --package "$PKG_WORK/Faxwerk-component.pkg" \
    --identifier "$BUNDLE_ID.pkg" \
    --version "$PKG_VERSION" \
    "${sign_args[@]+"${sign_args[@]}"}" \
    "$PKG"
pkgutil --check-signature "$PKG" || true
echo "Installer package: $PKG"
