#!/bin/bash
# Create DMG for distribution

set -e  # Exit on error

echo "=== Creating DMG for FIM Config Tool ==="
echo ""

# Check if app bundle exists
if [ ! -d "dist/FIM Config Tool.app" ]; then
    echo "Error: App bundle not found. Run ./build.sh first"
    exit 1
fi

# Get version from pyproject.toml
VERSION=$(grep '^version = ' pyproject.toml | cut -d'"' -f2)
DMG_NAME="FIM-Config-Tool-${VERSION}.dmg"

echo "Creating: ${DMG_NAME}"
echo ""

# Remove old DMG if it exists
rm -f "dist/${DMG_NAME}"

# Create DMG using hdiutil (built into macOS)
echo "1. Creating temporary DMG..."
hdiutil create -volname "FIM Config Tool" \
    -srcfolder "dist/FIM Config Tool.app" \
    -ov -format UDZO \
    "dist/${DMG_NAME}"

echo ""
echo "=== DMG Created Successfully! ==="
echo ""
echo "Location: dist/${DMG_NAME}"
echo "Size: $(du -h "dist/${DMG_NAME}" | cut -f1)"
echo ""
echo "To test:"
echo "  open 'dist/${DMG_NAME}'"
echo ""
