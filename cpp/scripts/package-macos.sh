#!/usr/bin/env bash
#
# package-macos.sh — produce a distributable .dmg of FIM Config Tool for
# Apple Silicon (arm64) Macs. Bundles Qt frameworks via macdeployqt and
# applies an ad-hoc code signature so Gatekeeper accepts the .app.
#
# This is NOT a release-grade build — it's not notarized, so first-time
# launch on a recipient machine will still require either right-click ->
# Open or `xattr -dr com.apple.quarantine` after copying the .app out of
# the .dmg. See README in this directory for the tester instructions.
#
# Usage:
#   cpp/scripts/package-macos.sh                # builds, packages, signs
#   cpp/scripts/package-macos.sh --skip-build   # skip cmake --build step
#
# Output: cpp/build/dist/FIM-Config-Tool-<version>.dmg
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/cpp/build"
APP_NAME="fim-config-tool.app"
APP_PATH="${BUILD_DIR}/${APP_NAME}"
DIST_DIR="${BUILD_DIR}/dist"

SKIP_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 2 ;;
    esac
done

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

if [[ ! -d "$APP_PATH" ]]; then
    echo "ERROR: $APP_PATH does not exist after build." >&2
    exit 1
fi

# ---- version string from git ----
VERSION="$(cd "$REPO_ROOT" && git describe --tags --always --dirty 2>/dev/null || echo unknown)"
echo "Packaging version: $VERSION"

# ---- bundle Qt frameworks (no -dmg yet; we sign first) ----
echo "Running macdeployqt..."
"$MACDEPLOYQT" "$APP_PATH" -verbose=1

# ---- ad-hoc sign the entire bundle (after macdeployqt rewrites binaries) ----
# --force: overwrite any existing signature inherited from Qt
# --deep: sign nested frameworks/helpers
# --sign -: ad-hoc signature, no Apple Developer cert required
echo "Ad-hoc signing the bundle..."
codesign --force --deep --sign - "$APP_PATH"

# Verify the signature is structurally valid (it won't pass strict
# notarization checks, but spctl-assess --type execute should at least
# parse it).
codesign --verify --deep --strict "$APP_PATH" || {
    echo "ERROR: codesign --verify failed" >&2
    exit 1
}

# ---- pack into a .dmg ----
mkdir -p "$DIST_DIR"
DMG_PATH="${DIST_DIR}/FIM-Config-Tool-${VERSION}.dmg"
rm -f "$DMG_PATH"

# Stage a temp folder so the .dmg has just the .app + a Applications
# symlink (the standard "drag-to-install" layout).
STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT
cp -R "$APP_PATH" "$STAGE_DIR/"
ln -s /Applications "$STAGE_DIR/Applications"

echo "Creating $DMG_PATH..."
hdiutil create \
    -volname "FIM Config Tool" \
    -srcfolder "$STAGE_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH" >/dev/null

echo
echo "Done. Distributable .dmg:"
echo "  $DMG_PATH"
echo
echo "Tell testers: after copying the .app out of the .dmg, run once:"
echo "  xattr -dr com.apple.quarantine \"/Applications/FIM Config Tool.app\""
echo "or right-click the .app -> Open and click Open in the dialog."
