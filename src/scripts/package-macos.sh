#!/usr/bin/env bash
#
# package-macos.sh — produce a distributable .dmg of Harbor.
#
# By default this creates an ad-hoc-signed internal build. Pass --notarize
# after configuring a Developer ID Application certificate and a notarytool
# Keychain profile to create a public Gatekeeper-ready release.
#
# Usage:
#   src/scripts/package-macos.sh
#   src/scripts/package-macos.sh --skip-build --build-dir build
#   src/scripts/package-macos.sh --notarize
#
# Environment:
#   APPLE_DEVELOPER_ID              Developer ID Application identity
#   APPLE_NOTARY_KEYCHAIN_PROFILE   Profile created by notarytool
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR=""
APP_NAME="Harbor.app"
SKIP_BUILD=0
NOTARIZE=0
SIGN_IDENTITY="${APPLE_DEVELOPER_ID:-}"
NOTARY_PROFILE="${APPLE_NOTARY_KEYCHAIN_PROFILE:-}"

usage() {
    cat <<'EOF'
Usage: src/scripts/package-macos.sh [options]

Options:
  --skip-build                 Package an existing build
  --build-dir PATH             CMake build directory (default: src/build)
  --identity NAME              Developer ID Application signing identity
  --notarize                   Submit, wait, and staple the DMG
  --keychain-profile NAME      notarytool stored-credentials profile
  --help                       Show this help

APPLE_DEVELOPER_ID and APPLE_NOTARY_KEYCHAIN_PROFILE provide the corresponding
values without exposing credentials in the command or repository.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) SKIP_BUILD=1 ;;
        --notarize) NOTARIZE=1 ;;
        --build-dir|--identity|--keychain-profile)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: $1 requires a value" >&2
                exit 2
            fi
            case "$1" in
                --build-dir) BUILD_DIR="$2" ;;
                --identity) SIGN_IDENTITY="$2" ;;
                --keychain-profile) NOTARY_PROFILE="$2" ;;
            esac
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="${REPO_ROOT}/src/build"
fi
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
fi

APP_SOURCE_PATH="${BUILD_DIR}/${APP_NAME}"
DIST_DIR="${BUILD_DIR}/dist"

if [[ "$NOTARIZE" -eq 1 && -z "$SIGN_IDENTITY" ]]; then
    echo "ERROR: --notarize requires --identity or APPLE_DEVELOPER_ID" >&2
    exit 2
fi
if [[ "$NOTARIZE" -eq 1 && -z "$NOTARY_PROFILE" ]]; then
    echo "ERROR: --notarize requires --keychain-profile or" >&2
    echo "       APPLE_NOTARY_KEYCHAIN_PROFILE" >&2
    exit 2
fi

# ---- locate macdeployqt ----
MACDEPLOYQT="$(command -v macdeployqt || true)"
if [[ -z "$MACDEPLOYQT" ]]; then
    for candidate in /opt/homebrew/opt/qt/bin/macdeployqt /opt/homebrew/bin/macdeployqt; do
        if [[ -x "$candidate" ]]; then
            MACDEPLOYQT="$candidate"
            break
        fi
    done
fi
if [[ -z "$MACDEPLOYQT" ]]; then
    echo "ERROR: macdeployqt not found. Install Qt via Homebrew: brew install qt" >&2
    exit 1
fi
echo "Using macdeployqt: $MACDEPLOYQT"
QTPATHS="$(dirname "$MACDEPLOYQT")/qtpaths"
if [[ ! -x "$QTPATHS" ]]; then
    QTPATHS="$(command -v qtpaths || true)"
fi
QT_PLUGIN_DIR=""
if [[ -n "$QTPATHS" ]]; then
    QT_PLUGIN_DIR="$("$QTPATHS" --query QT_INSTALL_PLUGINS 2>/dev/null || true)"
fi
if [[ -z "$QT_PLUGIN_DIR" || ! -d "$QT_PLUGIN_DIR" ]]; then
    echo "ERROR: unable to locate the Qt plug-in directory with qtpaths" >&2
    exit 1
fi

# ---- arch sanity check ----
HOST_ARCH="$(uname -m)"
if [[ "$HOST_ARCH" != "arm64" ]]; then
    echo "WARNING: host arch is $HOST_ARCH, not arm64. The resulting .dmg" >&2
    echo "will only run on $HOST_ARCH machines, not Apple Silicon." >&2
fi

# ---- build ----
if [[ "$SKIP_BUILD" -eq 0 ]]; then
    if [[ ! -f "${BUILD_DIR}/build.ninja" && ! -f "${BUILD_DIR}/Makefile" ]]; then
        echo "ERROR: build dir not configured. Run cmake -G Ninja first." >&2
        exit 1
    fi
    echo "Building (Release)..."
    cmake --build "$BUILD_DIR" --config Release
fi

if [[ ! -d "$APP_SOURCE_PATH" ]]; then
    echo "ERROR: $APP_SOURCE_PATH does not exist after build." >&2
    exit 1
fi

# ---- version string from git ----
VERSION="$(cd "$REPO_ROOT" && git describe --tags --always --dirty 2>/dev/null || echo unknown)"
echo "Packaging version: $VERSION"

# Work only on a staging copy. macdeployqt adds frameworks and plug-ins, which
# must never leak back into the development bundle.
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
APP_PATH="${WORK_DIR}/${APP_NAME}"
cp -R "$APP_SOURCE_PATH" "$APP_PATH"

# ---- bundle Qt frameworks (no -dmg yet; we sign first) ----
# Some macdeployqt versions replace Contents/Info.plist. Preserve the CMake
# bundle metadata outside the app and restore it before the final signature.
INFO_PLIST="${APP_PATH}/Contents/Info.plist"
INFO_PLIST_BAK="${WORK_DIR}/Info.plist"
if [[ -f "$INFO_PLIST" ]]; then
    cp "$INFO_PLIST" "$INFO_PLIST_BAK"
fi

echo "Running macdeployqt..."
MACDEPLOYQT_ARGS=(-verbose=1 -no-plugins)
MACDEPLOYQT_HELP="$("$MACDEPLOYQT" -help 2>&1 || true)"
if [[ "$MACDEPLOYQT_HELP" == *"-no-codesign"* ]]; then
    MACDEPLOYQT_ARGS+=(-no-codesign)
else
    echo "macdeployqt does not support -no-codesign; its temporary ad-hoc" \
         "signature will be replaced after plug-in pruning."
fi
"$MACDEPLOYQT" "$APP_PATH" "${MACDEPLOYQT_ARGS[@]}"

# macdeployqt otherwise copies every globally installed Qt image/input plug-in,
# pulling optional QtPdf, QtSvg, and Virtual Keyboard dependencies into a
# Widgets-only app. Harbor needs just the Cocoa platform and native macOS style
# plug-ins. Give each the app-local Frameworks rpath macdeployqt normally adds.
for plugin in platforms/libqcocoa.dylib styles/libqmacstyle.dylib; do
    plugin_source="${QT_PLUGIN_DIR}/${plugin}"
    plugin_destination="${APP_PATH}/Contents/PlugIns/${plugin}"
    if [[ ! -f "$plugin_source" ]]; then
        echo "ERROR: required Qt plug-in not found: $plugin_source" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$plugin_destination")"
    cp "$plugin_source" "$plugin_destination"
    chmod u+w "$plugin_destination"
    install_name_tool -add_rpath "@loader_path/../../Frameworks" "$plugin_destination"
done

# Restore Info.plist so the bundle has the correct CFBundleIconFile,
# CFBundleName, CFBundleIdentifier, etc.
if [[ -f "$INFO_PLIST_BAK" ]]; then
    rm -f "$INFO_PLIST"
    cp "$INFO_PLIST_BAK" "$INFO_PLIST"
    echo "Restored Info.plist"
fi

# ---- sign the entire staged bundle after deployment ----
if [[ -n "$SIGN_IDENTITY" ]]; then
    echo "Signing with Developer ID: $SIGN_IDENTITY"
    SIGN_ARGS=(--force --options runtime --timestamp --sign "$SIGN_IDENTITY")
else
    echo "Ad-hoc signing the bundle (internal testing only)..."
    # Hardened library validation requires nested code to share an Apple Team
    # ID. Ad-hoc identities have no Team ID, so internal builds cannot mirror
    # that flag without disabling library validation.
    SIGN_ARGS=(--force --sign -)
fi

# Sign from the inside out. This gives every nested code object its own valid
# signature and avoids relying on codesign's deprecated --deep signing mode.
while IFS= read -r -d '' framework; do
    codesign "${SIGN_ARGS[@]}" "$framework"
done < <(find "${APP_PATH}/Contents/Frameworks" -maxdepth 1 -type d -name '*.framework' -print0)
while IFS= read -r -d '' library; do
    codesign "${SIGN_ARGS[@]}" "$library"
done < <(find "${APP_PATH}/Contents/Frameworks" -maxdepth 1 -type f -name '*.dylib' -print0)
while IFS= read -r -d '' plugin; do
    codesign "${SIGN_ARGS[@]}" "$plugin"
done < <(find "${APP_PATH}/Contents/PlugIns" -type f -perm -111 -print0)
codesign "${SIGN_ARGS[@]}" "${APP_PATH}/Contents/MacOS/Harbor"
codesign "${SIGN_ARGS[@]}" "$APP_PATH"

codesign --verify --deep --strict "$APP_PATH" || {
    echo "ERROR: codesign --verify failed" >&2
    exit 1
}
if [[ "$NOTARIZE" -eq 1 ]]; then
    SIGNATURE_DETAILS="$(codesign --display --verbose=4 "$APP_PATH" 2>&1)"
    if ! grep -q '^Authority=Developer ID Application:' <<<"$SIGNATURE_DETAILS"; then
        echo "ERROR: the app is not signed with a Developer ID Application certificate" >&2
        exit 1
    fi
    if ! grep -Eq '^CodeDirectory .*flags=.*runtime' <<<"$SIGNATURE_DETAILS"; then
        echo "ERROR: the app signature does not have hardened runtime enabled" >&2
        exit 1
    fi
fi

# ---- pack into a .dmg ----
mkdir -p "$DIST_DIR"
DMG_PATH="${DIST_DIR}/Harbor-${VERSION}.dmg"
rm -f "$DMG_PATH"

# Stage a folder so the .dmg has just the .app + an Applications
# symlink (the standard "drag-to-install" layout).
STAGE_DIR="${WORK_DIR}/dmg"
mkdir -p "$STAGE_DIR"
cp -R "$APP_PATH" "$STAGE_DIR/"
ln -s /Applications "$STAGE_DIR/Applications"
cp "${REPO_ROOT}/src/resources/dist/READ ME FIRST.txt" "$STAGE_DIR/"

echo "Creating $DMG_PATH..."
hdiutil create \
    -volname "Harbor" \
    -srcfolder "$STAGE_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH" >/dev/null

if [[ -n "$SIGN_IDENTITY" ]]; then
    echo "Signing disk image..."
    codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG_PATH"
    codesign --verify --strict "$DMG_PATH"
fi

if [[ "$NOTARIZE" -eq 1 ]]; then
    echo "Submitting to Apple's notary service..."
    xcrun notarytool submit "$DMG_PATH" \
        --keychain-profile "$NOTARY_PROFILE" \
        --wait

    echo "Stapling notarization ticket..."
    xcrun stapler staple "$DMG_PATH"
    xcrun stapler validate "$DMG_PATH"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$DMG_PATH"
fi

echo
if [[ "$NOTARIZE" -eq 1 ]]; then
    echo "Done. Developer ID signed and notarized .dmg:"
elif [[ -n "$SIGN_IDENTITY" ]]; then
    echo "Done. Developer ID signed, NOT notarized .dmg:"
else
    echo "Done. Ad-hoc-signed internal .dmg:"
fi
echo "  $DMG_PATH"
if [[ "$NOTARIZE" -eq 0 ]]; then
    echo
    echo "This build is not notarized. Gatekeeper may require right-click -> Open"
    echo "or: xattr -dr com.apple.quarantine \"/Applications/Harbor.app\""
fi
