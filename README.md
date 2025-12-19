# FIM Config Tool

PySide6 desktop application for generating wavetables from audio files using spectral resynthesis.

## Development

### Running

```bash
uv run python src/config_tool/main.py
```

### Styling (SCSS → QSS)

Styles are written in `src/config_tool/input.scss` and compiled to `app.qss`:

```bash
# One-time compile
npx sass src/config_tool/input.scss src/config_tool/app.qss --no-source-map --style=expanded

# Watch mode (auto-recompile on changes)
npx sass src/config_tool/input.scss src/config_tool/app.qss --no-source-map --style=expanded --watch
```

### Assets & Resources

Assets (icons, stylesheets) are embedded using Qt Resource System:

1. Add new assets to `src/config_tool/assets.qrc`
2. Recompile: `cd src/config_tool && uv run pyside6-rcc assets.qrc -o assets_rc.py`
3. Use in code: `QIcon(":/assets/icon.svg")` or `QFile(":/app.qss")`

## Building & Distribution

### macOS

```bash
# Build .app bundle
./build.sh

# Create distributable DMG
./create_dmg.sh

# Output: dist/FIM-Config-Tool-0.1.0.dmg
```

### Windows/Linux

Build on target platform using PyInstaller:

```bash
uv run pyinstaller "FIM Config Tool.spec" --noconfirm
```

See `BUILD.md` for detailed build instructions and troubleshooting.
