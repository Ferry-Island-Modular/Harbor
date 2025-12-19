#!/bin/bash
# Build script for FIM Config Tool

set -e  # Exit on error

echo "=== FIM Config Tool Build Script ==="
echo ""

# Check if we're in the right directory
if [ ! -f "pyproject.toml" ]; then
    echo "Error: Must run from fim-config-tool directory"
    exit 1
fi

# Clean previous builds
echo "1. Cleaning previous builds..."
rm -rf build dist "FIM Config Tool.app"
echo "   ✓ Clean complete"
echo ""

# Sync dependencies (ensure fourseas-preview is built)
echo "2. Syncing dependencies..."
uv sync
echo "   ✓ Dependencies synced"
echo ""

# Verify fourseas_preview is available
echo "3. Verifying fourseas_preview..."
if ! uv run python -c "import fourseas_preview._fourseas_preview" 2>/dev/null; then
    echo "   ✗ Error: fourseas_preview C++ extension not found"
    echo "   Run: cd ../FourSeas/python && uv pip install -e ."
    exit 1
fi
echo "   ✓ fourseas_preview available"
echo ""

# Build with PyInstaller
echo "4. Building with PyInstaller..."
uv run pyinstaller "FIM Config Tool.spec" --noconfirm
echo "   ✓ Build complete"
echo ""

# Check output
if [ -d "dist/FIM Config Tool.app" ]; then
    echo "=== Build Successful! ==="
    echo ""
    echo "macOS App Bundle: dist/FIM Config Tool.app"
    echo ""
    echo "To test:"
    echo "  open 'dist/FIM Config Tool.app'"
    echo ""
    echo "To create a DMG:"
    echo "  ./create_dmg.sh"
    echo ""
else
    echo "=== Build Failed ==="
    echo "Check the output above for errors"
    exit 1
fi
