# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Harbor is a C++/Qt desktop application that generates wavetable banks for Ferry Island Modular hardware and other wavetable synth hosts. It started as a Python project (PySide6 + librosa) and was rewritten in C++ for native performance and distribution simplicity. The Python source was removed when the C++ rewrite reached feature parity.

## Tech Stack

- C++20, Qt 6.8 (Widgets, Core, Gui)
- CMake + Ninja
- libsamplerate, dr_wav, miniaudio, PFFFT (vendored or via FetchContent)
- Catch2 v3 for tests
- No logging library; diagnostics go to stderr via `std::cerr`
- FourSeas firmware engine vendored as a git submodule under `src/third_party/Four-Seas`

## Repository Layout

- `src/` — the application source and build
  - `src/src/app/` — application/services layer (settings, generate services, export writer)
  - `src/src/dsp/` — DSP cores (STFT, FFT, generators, resamplers)
  - `src/src/engine/` — realtime audio engine (preview playback)
  - `src/src/ui/` — Qt Widgets UI (screens, dialogs, custom widgets)
  - `src/tests/` — Catch2 tests
  - `src/scripts/` — packaging and icon generation scripts
  - `src/resources/` — fonts, icons, help HTML, dist artifacts
  - `src/third_party/` — vendored single-header libs and FourSeas submodule
- `docs/` — design notes, implementation plans, followups
- `designs/` — Penpot SVG exports of the UI mockups
- `.github/workflows/` — CI configuration

## Build

```bash
git submodule update --init --recursive
cd src
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The macOS bundle ends up at `src/build/Harbor.app`.

## Distribution

```bash
src/scripts/package-macos.sh
```

Produces `src/build/dist/Harbor-<git-version>.dmg` — ad-hoc signed (not notarized). See `src/scripts/README.md` for details.

## Key Constants

- `NUM_SAMPLES = 2048`: Samples per wavetable cycle
- `NUM_WAVES = 64`: Total waves per output file (8x8 grid)
- `NUM_PAGES = 8`: Number of output files to generate (Z axis)
- Output sample rate: 44100 Hz, 16-bit signed integer WAV format
