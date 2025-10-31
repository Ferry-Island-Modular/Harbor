# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Ferry Island Modular Config Tool is a PySide6-based desktop application for generating wavetables from audio files using spectral resynthesis. The tool takes three audio files (X, Y, Z axes) and generates 8 wavetable files with 64 waves each (8x8 grid per file).

## Development Setup

This project uses `uv` for dependency management and requires Python 3.12+.

### Running the Application

```bash
# Run the application directly
python src/config_tool/main.py

# Or use the package script (if installed)
uv run fim-config-tool
```

### Building Standalone Executable

The project uses PyInstaller for creating standalone builds:

```bash
# Build with PyInstaller
pyinstaller src/config_tool/main.py --name "FIM Config Tool" --windowed
```

## Architecture

### Core Components

**ConfigApp** (`src/config_tool/main.py`): Main application controller that:
- Manages UI lifecycle and signal connections
- Coordinates between UI file drops and wavetable generation
- Configures generation parameters (NUM_SAMPLES=2048, NUM_WAVES=64, Z_LENGTH=8)
- Generates 8 output files (1.wav through 8.wav) representing Z-axis progression

**MainWindow & FileDropWidget** (`src/config_tool/ui.py`): PySide6 UI components that:
- Provide three drop zones for X, Y, Z audio files
- Emit signals when files are dropped or generation is triggered
- Display progress bar during wavetable generation

**AudioResynthWavetableGenerator** (`src/config_tool/lib/audio_resynthesis.py`): Signal processing engine that:
- Analyzes audio using STFT (Short-Time Fourier Transform)
- Implements spectral modification based on X, Y, Z parameters:
  - X: Controls balance/blend between audio sources
  - Y: Controls harmonic complexity and spectral tilt
  - Z: Controls phase manipulation and which audio pairs to blend
- Performs cross-synthesis between multiple audio sources using librosa
- Applies oversampling (4x by default) and resampling for anti-aliasing

**WavetableGeneratorBaseClass** (`src/config_tool/lib/baseclass.py`): Abstract base class that:
- Manages wavetable page assembly using transfer functions
- Handles oversampling, resampling, normalization, and WAV file output
- Uses 8x8 grid structure (64 waves per page)
- Implements Kaiser window resampling for quality downsampling

### Signal Processing Pipeline

1. Audio files loaded and analyzed via STFT
2. Spectral features (magnitude, phase) extracted and cached per file
3. For each Z-value (0-7), generate an 8x8 grid of wavetables
4. Each grid position (x,y) applies different spectral modifications
5. Cross-synthesis interpolates between audio sources based on parameters
6. Oversampled waveforms are resampled down and normalized
7. Final output: 8 WAV files, each containing 64 concatenated single-cycle waveforms

### Key Data Flow

```
User drops 3 audio files → Files validated → Generate button enabled →
User clicks generate → For each Z (0-7):
  → Generate 64 wavetables (8x8 grid) via cross-synthesis
  → Save as Z.wav in output_waves/audio_resynth/
  → Update progress bar
```

## Output Format

Generated files are saved to `output_waves/audio_resynth/` with filenames `1.wav` through `8.wav`. Each file contains 64 single-cycle waveforms (2048 samples each at 44.1kHz) concatenated into a single audio file suitable for wavetable synthesis modules.

## Important Constants

- `NUM_SAMPLES = 2048`: Samples per wavetable cycle (before oversampling)
- `NUM_WAVES = 64`: Total waves per output file (8x8 grid)
- `Z_LENGTH = 8`: Number of output files to generate
- `oversample_factor = 4`: Internal oversampling ratio for anti-aliasing
- Output sample rate: 44100 Hz, 16-bit signed integer WAV format
