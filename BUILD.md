# Build Instructions for FIM Config Tool

This guide covers building a standalone macOS application bundle for distribution.

## Prerequisites

- macOS (for building .app bundle and DMG)
- Python 3.12+
- `uv` package manager installed
- PyInstaller (included in dependencies)

## Quick Build

### 1. Build the Application

```bash
./build.sh
```

This script will:
1. Clean previous builds
2. Sync all dependencies (including `fourseas-preview` C++ extension)
3. Verify the C++ extension is available
4. Build the application with PyInstaller
5. Create `dist/FIM Config Tool.app`

**Output:** `dist/FIM Config Tool.app` (~150-200 MB)

### 2. Create DMG (Optional)

```bash
./create_dmg.sh
```

This creates a distributable DMG file: `dist/FIM-Config-Tool-0.1.0.dmg`

## Manual Build

If you prefer to build manually:

```bash
# 1. Clean
rm -rf build dist

# 2. Sync dependencies
uv sync

# 3. Build
uv run pyinstaller "FIM Config Tool.spec" --noconfirm

# 4. Test
open "dist/FIM Config Tool.app"
```

## Troubleshooting

### "fourseas_preview C++ extension not found"

**Solution:**
```bash
cd ../FourSeas/python
uv pip install -e .
cd ../../fim-config-tool
./build.sh
```

### "PySide6 plugins not found" at runtime

This is usually handled by PyInstaller automatically, but if you see Qt plugin errors:

1. Check that PySide6 is installed: `uv run python -c "import PySide6; print(PySide6.__version__)"`
2. Try adding `--clean` flag: `uv run pyinstaller "FIM Config Tool.spec" --noconfirm --clean`

### App crashes on startup

**Debug mode:**
```bash
# Build with console enabled (in spec file: console=True)
# Or run from terminal to see errors:
dist/FIM\ Config\ Tool.app/Contents/MacOS/FIM\ Config\ Tool
```

### Large app size (>500 MB)

PyInstaller includes all dependencies. To reduce size:

1. Edit `FIM Config Tool.spec`
2. Add more excludes in the `excludes=[]` list:
   ```python
   excludes=[
       'matplotlib',
       'IPython',
       'jupyter',
       'pandas',  # Add if not used
       'pytest',  # Add if not used
   ]
   ```

## Build Output Structure

```
dist/
├── FIM Config Tool.app/
│   ├── Contents/
│   │   ├── MacOS/
│   │   │   └── FIM Config Tool          # Main executable
│   │   ├── Resources/
│   │   │   └── ...                       # Qt plugins, libraries
│   │   └── Info.plist                    # macOS bundle info
│   └── ...
└── FIM-Config-Tool-0.1.0.dmg            # (After running create_dmg.sh)
```

## Distribution

### For Beta Testing

Share the `.app` bundle:
```bash
# Create a zip for distribution
cd dist
zip -r "FIM-Config-Tool-0.1.0-macOS.zip" "FIM Config Tool.app"
```

### For Production

Use the DMG:
```bash
./create_dmg.sh
# Share: dist/FIM-Config-Tool-0.1.0.dmg
```

## Code Signing (Optional)

For distribution outside of personal use, you should sign the app:

```bash
# Sign the app
codesign --deep --force --verify --verbose \
  --sign "Developer ID Application: Your Name (TEAM_ID)" \
  "dist/FIM Config Tool.app"

# Verify signature
codesign --verify --verbose "dist/FIM Config Tool.app"
spctl -a -vv "dist/FIM Config Tool.app"

# Notarize (requires Apple Developer account)
xcrun notarytool submit "dist/FIM-Config-Tool-0.1.0.dmg" \
  --apple-id "your@email.com" \
  --password "app-specific-password" \
  --team-id "TEAM_ID"
```

## Cross-Platform Building

### Windows

PyInstaller works on Windows, but you need to build on a Windows machine:

```powershell
# Install dependencies
uv sync

# Build
uv run pyinstaller "FIM Config Tool.spec" --noconfirm

# Output: dist/FIM Config Tool.exe
```

**Note:** The spec file will need minor adjustments for Windows (`.pyd` instead of `.so`).

### Linux

```bash
# Install dependencies
uv sync

# Build
uv run pyinstaller "FIM Config Tool.spec" --noconfirm

# Output: dist/FIM Config Tool (binary)
```

## CI/CD Integration

For automated builds with GitHub Actions:

```yaml
# .github/workflows/build.yml
name: Build
on: [push, pull_request]

jobs:
  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-python@v4
        with:
          python-version: '3.12'
      - name: Install uv
        run: pip install uv
      - name: Build
        run: ./build.sh
      - name: Create DMG
        run: ./create_dmg.sh
      - uses: actions/upload-artifact@v3
        with:
          name: FIM-Config-Tool-macOS
          path: dist/*.dmg
```

## Maintenance

### Updating Dependencies

```bash
# Update all dependencies
uv sync --upgrade

# Rebuild
./build.sh
```

### Updating fourseas-preview

```bash
# Rebuild the C++ extension
cd ../FourSeas/python
uv pip install -e . --force-reinstall --no-deps

# Rebuild the app
cd ../../fim-config-tool
./build.sh
```
