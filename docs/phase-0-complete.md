# Phase 0 Complete

As of this commit, Phase 0 of the C++/Qt rewrite (the scaffold + cross-platform CI gate) is complete.

## What got built

- `cpp/` staging directory with CMake-based Qt 6 project (Qt 6.8.3 LTS minimum)
- `cpp/src/app/main.cpp` — minimal Qt window that loads a styled QSS theme
- `cpp/src/engine/engine_smoke.cpp` — proves `fourseas::WavetableOscillator<2048>` instantiates
- FourSeas engine wired in via `cpp/third_party/Four-Seas/` git submodule (recursive — pulls DaisySP, libDaisy, stmlib)
- Vendored single-header libs in `cpp/third_party/`: miniaudio 0.11.22, dr_wav wav-0.14.5, PFFFT (Pommier original)
- FetchContent dependencies: libsamplerate 0.2.2, Catch2 v3.5.4, spdlog v1.14.1
- SCSS → QSS transpilation pipeline (`cpp/styles/input.scss` → build-time `app.qss` via `npx sass`, embedded as Qt resource)
- Catch2 test scaffold (`cpp/tests/`) with passing engine smoke test
- `cpp/.clang-format` (Google-derived, 4-space, mandatory braces via `InsertBraces: true`)
- `cpp/.clangd` pointing clangd at `build/compile_commands.json`

## Cross-platform CI

`.github/workflows/cpp-ci.yml` runs on every push to `cpp-qt-rewrite` and on pull requests targeting it. Three build runners + a clang-format lint job, all green:

| Job | Status |
|---|---|
| `clang-format check` | ✅ |
| `Build - macos-latest` (macOS 15 arm64) | ✅ |
| `Build - ubuntu-latest` | ✅ |
| `Build - windows-latest` (MSVC2022) | ✅ |

## Known omissions, deliberate

- **macOS Intel (x86_64)** — `macos-13` is deprecated and `macos-14-large` is a paid runner. We can produce a universal binary at release time via `lipo` if needed.
- **macOS 26 (newest)** — Qt 6.8 still references the deprecated Apple AGL framework in its CMake config; AGL was removed in macOS 26. Will revisit when Qt fixes this OR when `macos-latest` itself rotates to macOS 26+.
- **Node.js 20 deprecation warnings** in CI annotations — `actions/checkout@v4` and `actions/setup-node@v4` will auto-update by June 2026.

## The escape hatch was not triggered

Phase 0's GO/NO-GO checkpoint: "all four CI runners build and produce a working artifact." We hit it cleanly with three runners (not four — see above). The Windows MSVC + Qt 6.8 build works, which was the riskiest unknown.

## Next phase

`docs/superpowers/plans/` will get a `phase-1-...` plan added when Phase 1 starts. Phase 1 is "realtime preview end-to-end" — vendoring the binding's C++ classes into `cpp/src/engine/`, wrapping them with miniaudio, and producing a one-window app that plays audio from an existing `output_waves/audio_resynth/` bank. That's the next escape-hatch checkpoint.
