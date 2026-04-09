#!/usr/bin/env bash
#
# generate-icon.sh — produce Harbor.icns from cpp/resources/icon/harbor-source.png.
#
# The source is a square PNG (white logo on black). This script resizes
# it to the canonical 1024px master, applies a rounded-corner mask so the
# icon matches the macOS Big Sur+ "squircle" dock shape, and emits a
# multi-resolution .icns bundle.
#
# Re-run this script after editing the source PNG.
#
# Requires: ImageMagick (magick), iconutil (macOS)
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ICON_DIR="${REPO_ROOT}/cpp/resources/icon"
SOURCE_PNG="${ICON_DIR}/harbor-source.png"
OUTPUT_ICNS="${ICON_DIR}/Harbor.icns"
ICONSET_DIR="${ICON_DIR}/Harbor.iconset"

# Corner radius as a percentage of the master (1024px) canvas. macOS
# Big Sur+ icons use roughly 22-23% of the canvas for the corner radius
# of the squircle shape. We approximate the squircle with a rounded
# rectangle — close enough that nobody can tell at dock sizes.
CORNER_RADIUS_PERCENT=22

if [[ ! -f "$SOURCE_PNG" ]]; then
    echo "ERROR: $SOURCE_PNG not found" >&2
    exit 1
fi

for tool in magick iconutil; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: $tool not found in PATH" >&2
        exit 1
    fi
done

rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

# Step 1: resize the source to a 1024x1024 master.
magick "$SOURCE_PNG" -resize 1024x1024 "${TMP_DIR}/resized.png"

# Step 2: build a rounded-corner mask of the same dimensions. White
# inside the rounded rect, transparent outside. We then composite this
# mask onto the resized image as the alpha channel — pixels outside the
# rounded rect become transparent so the dock background shows through.
CORNER_RADIUS=$((1024 * CORNER_RADIUS_PERCENT / 100))
magick -size 1024x1024 xc:none \
    -fill white \
    -draw "roundrectangle 0,0 1023,1023 ${CORNER_RADIUS},${CORNER_RADIUS}" \
    "${TMP_DIR}/mask.png"

magick "${TMP_DIR}/resized.png" "${TMP_DIR}/mask.png" \
    -alpha off -compose CopyOpacity -composite \
    "${TMP_DIR}/master-1024.png"

# Step 3: emit the iconset entries at the standard sizes.
generate() {
    local name=$1
    local size=$2
    magick "${TMP_DIR}/master-1024.png" -resize "${size}x${size}" \
        "${ICONSET_DIR}/${name}"
}

generate icon_16x16.png       16
generate icon_16x16@2x.png    32
generate icon_32x32.png       32
generate icon_32x32@2x.png    64
generate icon_128x128.png     128
generate icon_128x128@2x.png  256
generate icon_256x256.png     256
generate icon_256x256@2x.png  512
generate icon_512x512.png     512
generate icon_512x512@2x.png  1024

iconutil --convert icns "$ICONSET_DIR" --output "$OUTPUT_ICNS"
rm -rf "$ICONSET_DIR"

echo "Wrote $OUTPUT_ICNS"
ls -lh "$OUTPUT_ICNS"
