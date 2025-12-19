# -*- mode: python ; coding: utf-8 -*-
"""
PyInstaller spec file for FIM Config Tool

Packages the PySide6 application with fourseas-preview C++ extension.
"""

from PyInstaller.utils.hooks import collect_data_files, collect_submodules, collect_all
import sys
from pathlib import Path

# Find the fourseas_preview C++ extension
fourseas_path = Path('../FourSeas/python/fourseas_preview')
if not fourseas_path.exists():
    raise RuntimeError("fourseas-preview not found. Run 'uv sync' first.")

# Locate the C++ extension binary
ext_files = list(fourseas_path.glob('_fourseas_preview*.so'))
if not ext_files:
    ext_files = list(fourseas_path.glob('_fourseas_preview*.pyd'))  # Windows
if not ext_files:
    raise RuntimeError("fourseas_preview C++ extension not built. Run 'uv sync' first.")

ext_binary = str(ext_files[0])
print(f"Found fourseas_preview extension: {ext_binary}")

# Collect all scipy data files and binaries
scipy_datas, scipy_binaries, scipy_hiddenimports = collect_all('scipy')
librosa_datas, librosa_binaries, librosa_hiddenimports = collect_all('librosa')

# Collect data files
datas = []
datas += scipy_datas
datas += librosa_datas

# Binaries to include
binaries = [
    (ext_binary, 'fourseas_preview'),  # Include C++ extension
]
binaries += scipy_binaries
binaries += librosa_binaries

# Hidden imports for Python modules that PyInstaller might miss
hiddenimports = [
    'fourseas_preview',
    'fourseas_preview._fourseas_preview',
    'fourseas_preview.preview',
    'sounddevice',
    'numpy',
    'PySide6.QtCore',
    'PySide6.QtGui',
    'PySide6.QtWidgets',
]
# Add collected hidden imports from scipy and librosa
hiddenimports += scipy_hiddenimports
hiddenimports += librosa_hiddenimports

a = Analysis(
    ['src/config_tool/main.py'],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'matplotlib',  # Exclude if not used
        'IPython',
        'jupyter',
        'pytest',
        'tkinter',
    ],
    noarchive=False,
    optimize=1,  # Enable basic optimizations
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='FIM Config Tool',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,  # Windowed application (no console)
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='src/config_tool/assets/AppIcon.icns',
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='FIM Config Tool',
)

# macOS app bundle
app = BUNDLE(
    coll,
    name='FIM Config Tool.app',
    icon='src/config_tool/assets/AppIcon.icns',
    bundle_identifier='com.ferryislandmodular.configtool',
    version='0.1.0',
    info_plist={
        'NSPrincipalClass': 'NSApplication',
        'NSHighResolutionCapable': 'True',
        'LSMinimumSystemVersion': '10.14.0',
    },
)
