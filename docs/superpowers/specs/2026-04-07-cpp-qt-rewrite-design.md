# fim-config-tool C++/Qt Rewrite — Design Spec

**Date:** 2026-04-07
**Branch:** `cpp-qt-rewrite`
**Status:** Design approved, pending implementation plan

## Summary

Rewrite `fim-config-tool` from PySide6/Python to native C++/Qt 6. The new version statically links the existing FourSeas C++ wavetable synthesis engine (header-only template class) and hand-ports the DSP and UI layers. The existing `fourseas-preview` Python/pybind11 binding is not used — the underlying C++ classes are vendored directly. The rewrite targets macOS, Windows, and Linux as a first-class concern from Phase 0 via a CI matrix, rather than at the end of the project. The current Python tool has only been validated on macOS, so cross-platform support is a *new* guarantee this rewrite delivers, not a preserved one.

## Motivation

The Python tool is structurally inelegant for its domain: it wraps a C++ synth engine via pybind11, drags in ~200 MB of numpy/scipy/librosa/sklearn at distribution time, and signs/notarizes painfully via PyInstaller. Performance is acceptable but not great because the DSP scaffolding runs in Python. Bundle size, distribution flakiness, and the structural awkwardness of a Python host around a C++ core are the real drivers — not raw speed.

The existing project owner (sole maintainer, embedded C++ background, no prior desktop C++ experience, plans to outsource bulk implementation to AI assistance) has explicitly chosen C++/Qt over both "fix Python signing" and "rewrite in Rust." The decision is final for this spec.

## Goals

1. Single, signed-or-unsigned native binary per platform, ~25–40 MB.
2. Cross-platform from day one: macOS (arm64 + x86_64), Windows, Linux.
3. Direct use of the FourSeas C++ engine with no Python/pybind11 layer.
4. Feature parity with the current Python tool (single-wav mode, Serum mode, preview widget).
5. Open-source under a permissive license (MIT or Apache-2.0 — matches FourSeas firmware).
6. Maintainability for a solo developer: small files, minimal dependencies, simple build.

## Non-goals

1. **Bit-exact parity** with the Python DSP output. The Python tool has never been published, so its output is a reference, not gospel. "Sounds right and produces plausible wavetables" is the acceptance bar.
2. **The "three-wavs" input mode.** Marked "Coming soon!" in the design and unwired in the current Python. The button stays disabled in the rewrite.
3. **New features during the rewrite.** Translate, don't redesign. Improvements go in `docs/followups.md` for after the cutover tag.
4. **Plugin format support (VST/AU/CLAP).** Standalone tool only.
5. **MIDI input from a controller.** The MIDI-note slider stays a slider.
6. **Undo/redo.** Not present in Python; not added.
7. **Localization.** English-only, hard-coded strings.
8. **Automated GUI/integration tests.** DSP gets Catch2 coverage; UI gets manual smoke testing.
9. **Code signing infrastructure on day one.** CI signs conditionally on env vars; default builds are unsigned with README workarounds.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  fim-config-tool (C++/Qt 6)                             │
│                                                          │
│  ┌────────────────┐    ┌──────────────────────────┐    │
│  │   Qt UI layer  │◄──►│  Application controller  │    │
│  │  (QMainWindow, │    │  (mode dispatch, threads,│    │
│  │   widgets,     │    │   settings persistence)  │    │
│  │   QSS theme)   │    └────────┬─────────────────┘    │
│  └────────────────┘             │                       │
│                                 ▼                       │
│  ┌──────────────────────┐  ┌──────────────────────┐    │
│  │  Wavetable generator │  │  Realtime preview    │    │
│  │  (C++ port of        │  │  engine (audio I/O   │    │
│  │   audio_resynth.py + │  │  + WavetableOsc)     │    │
│  │   serum_converter.py)│  │                      │    │
│  └──────┬───────────────┘  └──────┬───────────────┘    │
│         │                          │                    │
│         ▼                          ▼                    │
│  ┌──────────────┐       ┌──────────────────────────┐   │
│  │  PFFFT +     │       │  WavetableOscillator<>   │   │
│  │  libsampler- │       │  (firmware header-only)  │   │
│  │  ate + dr_wav│       │  + stmlib (header-only)  │   │
│  └──────────────┘       │  + miniaudio (audio I/O) │   │
│                         └──────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

**Key architectural insight:** the public FourSeas GitHub repository (`github.com/Ferry-Island-Modular/Four-Seas`) contains `firmware/wavetable_oscillator.h` as a header-only C++17 template class with no libDaisy linker dependency. It drops into any CMake project via include path. The Python binding's C++ classes (`WavReader`, `WavetableEngine`, `StreamingEngine`) live only in the private monorepo on branch `python-preview` at commit `8d36c15` — we vendor those files into this repo (stripped of pybind11) rather than depend on the private binding source.

## Tech stack

| Concern | Choice | Notes |
|---|---|---|
| Language | C++17 | Matches `wavetable_oscillator.h` which uses `if constexpr` |
| GUI | Qt 6.7+ Widgets (not QML) | LGPL, cross-platform, existing QSS theme is reusable |
| Build | CMake 3.25+ | What Qt 6, stmlib, and the firmware already use |
| Package mgmt | Git submodules + CMake FetchContent | No Conan/vcpkg — deliberately simple |
| FFT | PFFFT (BSD) | Avoids FFTW's GPL; fast, small, proven (used by VCV Rack and WaveEdit) |
| Audio I/O | miniaudio (public domain, single-header) | Simpler and more reliable than Qt's QAudioSink |
| Resampling | libsamplerate (BSD-2) | SRC_SINC_BEST_QUALITY mode; bit-exact match to scipy not required |
| WAV I/O | dr_wav (public domain, single-header) | Pairs with miniaudio; handles all formats we care about |
| DSP utilities | `stmlib/` (header-only, via FourSeas submodule) + hand-written Hann window | |
| Threading | Qt `QThreadPool` + `QRunnable` (UI/background); `std::atomic` + `std::jthread` (realtime audio) | Same patterns the Python app uses |
| Testing | Catch2 v3 via FetchContent | Lighter than GoogleTest |
| Persistent settings | `QSettings` | Platform-native, zero code, same semantics as current Python |
| Font bundling | `QFontDatabase::addApplicationFont` via `.qrc` resource | Pangram family, exact same mechanism as current app |
| Logging | spdlog (MIT) | Via FetchContent |
| Stylesheet pipeline | `styles/input.scss` → `app.qss` via `npx sass` as CMake pre-build step | Reused verbatim from current Python repo; dart-sass is language-agnostic |
| C++ interop w/ firmware | Direct `#include` of `wavetable_oscillator.h` from submodule path | No binding layer |

Developers need Node/npm installed to run `npx sass` during build. The transpiled `app.qss` is **not** checked in — it is regenerated on every build. This keeps `input.scss` as the single source of truth.

## Repository layout

```
fim-config-tool/
├── CMakeLists.txt
├── .github/workflows/ci.yml
├── third_party/
│   ├── Four-Seas/                         (git submodule, public repo, pinned SHA)
│   ├── libsamplerate/                     (git submodule, BSD-2)
│   └── (miniaudio.h, dr_wav.h, pffft.{c,h})  (vendored via FetchContent)
├── styles/
│   └── input.scss                         (copied verbatim from current repo)
├── resources/
│   ├── fonts/Pangram-*.woff2
│   └── app.qrc
├── src/
│   ├── engine/
│   │   ├── wav_reader.{h,cpp}             (vendored from bindings.cpp@8d36c15)
│   │   ├── wavetable_engine.{h,cpp}       (vendored from bindings.cpp@8d36c15)
│   │   └── realtime_audio_engine.{h,cpp}  (C++ port of streaming.py)
│   ├── dsp/
│   │   ├── wavetable_generator_base.{h,cpp}
│   │   ├── audio_resynth_generator.{h,cpp}
│   │   ├── serum_converter.{h,cpp}
│   │   └── morph_types.{h,cpp}
│   ├── ui/
│   │   ├── main_window.{h,cpp}
│   │   ├── file_drop_widget.{h,cpp}
│   │   ├── card_button.{h,cpp}
│   │   ├── axis_morph_selector.{h,cpp}
│   │   ├── custom_progress_bar.{h,cpp}
│   │   ├── preview_widget.{h,cpp}
│   │   └── splash_screen.{h,cpp}
│   └── app/
│       ├── config_app.{h,cpp}
│       ├── settings.{h,cpp}
│       ├── services/
│       │   ├── single_wav_service.{h,cpp}
│       │   └── serum_service.{h,cpp}
│       └── main.cpp
├── tests/
│   ├── data/                              (golden input WAVs + reference outputs)
│   └── dsp/                               (Catch2 smoke tests, no bit-exact)
├── docs/
│   ├── superpowers/
│   │   ├── specs/2026-04-07-cpp-qt-rewrite-design.md
│   │   └── plans/2026-04-07-cpp-qt-rewrite.md  (created in next step)
│   ├── followups.md
│   └── README.md
├── designs/                               (Penpot SVG exports, current location)
└── legacy/                                (empty until Phase 6)
```

Files vendored from `bindings.cpp@8d36c15` carry a one-line header comment noting provenance. They are not kept in sync with upstream; modifications are made freely.

## Phase plan

### Phase 0 — Scaffold + cross-platform CI matrix (3–4 days)

Goal: a CMake project that builds a do-nothing Qt window with the FourSeas engine include path wired in, on **all four** CI runners.

- Initialize CMake project, `find_package(Qt6 6.7+)`.
- Add `third_party/Four-Seas` as git submodule (public repo, pinned SHA).
- Vendor/FetchContent miniaudio, dr_wav, PFFFT, libsamplerate, Catch2, spdlog.
- Add `styles/input.scss` + CMake `add_custom_command` that runs `npx sass` at build time to produce `generated/app.qss`.
- `main.cpp`: empty `QMainWindow` displaying "Hello, FourSeas" styled via the transpiled QSS.
- Compile-only `static_assert` or dummy instantiation of `fourseas::WavetableOscillator<2048>` from the submodule include path to prove template instantiation works on clang, gcc, and MSVC.
- GitHub Actions matrix: `{macos-latest, macos-13, ubuntu-latest, windows-latest}`. All four must build and produce an artifact.
- CI also runs clang-format on every push.

**🚦 Phase 0 escape hatch:** hello-world must build on all four runners. If a platform is actively hostile (Windows MSVC + Qt deployment is the usual suspect), decide: fix, drop that platform, or abort the rewrite entirely and stay on Python.

### Phase 1 — Realtime preview end-to-end (1 week)

Goal: a one-window C++ app with X/Y/Z/pitch/volume sliders that plays audio from the existing `output_waves/audio_resynth/` bank using the real FourSeas engine via miniaudio. Proves the toolchain can hear audio from our engine.

- Vendor `WavReader`, `WavetableEngine` from `bindings.cpp@8d36c15` into `src/engine/`, strip pybind11 calls.
- Write `RealtimeAudioEngine` class wrapping a miniaudio playback device. Port sweep/arpeggio/MIDI-note logic from `streaming.py@8d36c15` (~375 lines of straightforward imperative code, ports roughly one-to-one).
- Minimal Qt UI: `QMainWindow` + sliders + Load Bank button + Play/Stop.
- **Parameter smoothing via exponential one-pole** in the audio callback from the start, preventing zipper noise on slider changes.
- **Double-buffered bank swap on load** via atomic pointer — no races, no "click and hope" tolerance.

**🚦 Phase 1 escape hatch:** if audio isn't coming out cleanly after a week, bail back to Python and fix the signing pipeline instead. This is the secondary escape hatch after Phase 0.

### Phase 2 — Main UI, no DSP (1.5 weeks)

Goal: the app visually matches `designs/start-state.svg`, has a working menu bar, and the Generate button is a stub that writes placeholder WAV files.

- Implement the launcher screen (three mode cards + header).
- Port seven custom widgets: `FileDropWidget`, `FileDropWrapper`, `CardButton`, `AxisMorphSelector`, `AxisMorphSelectorWrapper`, `CustomProgressBar`, `SplashScreen`. (Preview widget is already up from Phase 1.)
- Copy `input.scss` into `styles/`, wire the CMake pre-build rule, embed the generated `app.qss` via Qt resources.
- `QFontDatabase::addApplicationFont` for Pangram via `.qrc`.
- Menu bar: File (Set Output Directory), Audio (device picker, populated from miniaudio), Settings (Samples per Frame radio menu).
- `QSettings`-backed persistence for: `output_dir`, `samples_per_frame`, `mode`, `y_morph`, `z_morph`, `audio_device`, `preview_volume`.
- `QThreadPool` + cancellable `QRunnable` for the (still stubbed) Generate button.
- Progress bar hooked up to fake progress signals.

### Phase 3 — Single-wav DSP port (2–3 weeks)

Goal: single-wav Generate flow produces output WAVs that sound right when loaded in the preview widget.

- Set up 5 golden input WAVs in `tests/data/` (diverse: sine sweep, noise, recorded instrument, sample loop, edge-case short clip).
- Port `WavetableGeneratorBaseClass` (`baseclass.py`, 92 lines):
  - Oversampled grid assembly
  - Kaiser polyphase downsampling via `libsamplerate` (`SRC_SINC_BEST_QUALITY`)
  - Peak normalization, int16 quantization, multi-wave WAV write via dr_wav
- Port `AudioResynthWavetableGenerator` (`audio_resynthesis.py`, 334 lines):
  - `_analyze_audio`: STFT with Hann window 50% overlap via PFFFT
  - `_spectral_modifications`: spectral tilt (Y), envelope warping (X), phase dispersion (Z)
  - `_extract_single_cycle`: phase vocoder frame extraction, zero-crossing alignment, Hann-windowed fade
  - `_cross_audio_resynth`: additive synthesis from spectral peaks
  - `_generate_wavetable_from_audio` and `generate_audio_page` orchestration
- Catch2 tests that load each golden input, generate output, and verify:
  - Correct file count (8 output WAVs per bank)
  - Correct sample count per output (64 waves × `samples_per_frame`)
  - No NaN, no Inf, no clipping
  - RMS within a plausible band
  - Peak amplitude locations fall within the expected 8×8 grid structure
- Manual listening pass at end of phase: load each golden bank in the preview widget, verify it sounds sensible.
- Wire the real generator behind the Generate button.

### Phase 4 — Serum mode & morph types (3–4 weeks)

Goal: all four Serum morph types work correctly on Y and Z axes; Serum mode generates valid banks from real Serum wavetable files.

- Port `SerumWavetableConverter` (`serum_converter.py`, 596 lines) to `src/dsp/serum_converter.{h,cpp}`:
  - `load_serum_wavetable`: dr_wav + float32/int16 detection + per-frame DC removal
  - `resample_frames_to_8`: linear interpolation in frame-space
  - `_compute_fft_cache`: rFFT per frame, store amplitudes + phases
  - `generate_morphed_wave`: FFT morph pipeline (Y morph → reconstruct → Z morph → irFFT → DC remove → normalize → resample → oversample)
  - `generate_page` and `generate_all_pages` orchestration
- Port four morph types to `src/dsp/morph_types.{h,cpp}`:
  - `FormantScale`: linear harmonic warping with fractional-bin interpolation
  - `PhaseDisperse`: parabolic phase shift centered at harmonic 24 with scale constant 0.05
  - `Smear`: running-average amplitude smoothing with `(i + 0.25) / i` decay factor
  - `HarmonicStretch`: octave-space non-linear stretching (max multiplier 12)
- Y/Z morph composition must run **sequentially**: Y applied first, intermediate FFT reconstructed, Z applied to the Y-morphed state. Order matters.
- `SerumService` in `src/app/services/` with the same signal flow as the Python version.
- UI wiring: the Y and Z `AxisMorphSelector` widgets become visible only when mode is Serum (already implemented in Phase 2 but stubbed).
- Catch2 smoke tests per morph type.
- Manual listening pass: test file × all 4 Y morphs × all 4 Z morphs × a sample of Z pages.

### Phase 5 — Distribution polish (3 days)

Goal: release artifacts build and launch on clean VMs for each platform.

- macOS: `.app` bundle via `macdeployqt`, universal binary (arm64 + x86_64), optional codesign + notarize if `APPLE_DEVELOPER_ID` + `APPLE_NOTARY_*` env vars are present in CI.
- Linux: AppImage via `linuxdeploy-plugin-qt`.
- Windows: NSIS installer via `windeployqt` + NSIS script, optional signtool if `WINDOWS_CERT_*` env vars are present.
- GitHub release on tag push: upload all three artifacts.
- README section documenting unsigned-build workarounds: "macOS: right-click → Open. Windows SmartScreen: More info → Run anyway."

### Phase 6 — Cutover (1 day)

- Merge `cpp-qt-rewrite` → `master`.
- Move Python tree to `legacy/python/` with a short README explaining why.
- Update top-level README for the new build instructions.
- Tag `v2.0.0`.

## DSP port strategy

The DSP is ported hand-for-hand from the Python source, with three explicit license-of-engineering freedoms:

1. **We are not bound to bit-exactness.** The Python output is a reference, not a contract. Where the Python does something that looks ad-hoc (e.g., the phase vocoder frame selection, the spectral envelope warping's manual interpolation loop), we may clean it up during the port rather than faithfully reproducing it.
2. **We use libsamplerate instead of matching `scipy.signal.resample_poly` bit-exactly.** `SRC_SINC_BEST_QUALITY` is very close to Kaiser polyphase and is trusted by many audio projects. If the output diverges audibly we investigate; if it doesn't we ship.
3. **Testing is structural, not numerical.** Catch2 checks file validity, shape, amplitude range, and absence of pathological values (NaN, Inf, silence, clipping). Final arbitration is manual listening.

The Python source stays readable at `legacy/python/` post-cutover as the canonical reference implementation. The `generate_all_pages` progress callback is the only piece of surface API that touches the UI thread — we keep the same shape (`std::function<void(int current, int total)>`) and call it from the worker.

## Testing strategy

| Layer | Approach | Tools |
|---|---|---|
| DSP generators | Golden WAV in, golden WAV out, structural assertions | Catch2 v3 |
| Engine (vendored) | Compile-only smoke tests + a fixed bank render that produces a known-length audio buffer | Catch2 v3 |
| Realtime preview | Manual: load bank, scrub sliders, listen | — |
| UI widgets | Manual smoke test per widget | — |
| Settings persistence | Catch2: write settings, destruct, read back | Catch2 v3 + `QCoreApplication::organizationName` override |
| End-to-end flows | Manual per platform before each release tag | — |
| Cross-platform | CI matrix builds on every push | GitHub Actions |

**No QTest, no Squish, no headless X11 GUI testing.** The cost/value ratio is wrong for a solo project and Qt GUIs are notoriously fragile to automate.

## Success criteria

The rewrite can cut over to `master` when **all** of the following are true:

1. CI green on all four runners (macOS arm64, macOS x86_64, Ubuntu, Windows) for at least 3 consecutive pushes.
2. The start-state launcher screen visually matches `designs/start-state.svg` (manual comparison).
3. Single-wav mode generates valid banks that load in the preview widget and play audio cleanly, judged by ear against the Python version.
4. Serum mode generates valid banks for all four morph types on Y and Z, judged the same way.
5. Preview widget plays without audible artifacts on macOS and at least one other platform.
6. `QSettings` persistence works across app restarts for all persisted fields: `output_dir`, `samples_per_frame`, `mode`, `y_morph`, `z_morph`, `audio_device`, `preview_volume`.
7. Release artifacts build and launch on clean macOS/Windows/Linux VMs. Unsigned is acceptable with the documented workaround.
8. No open bugs in `docs/issues.md` marked `blocker` (bugs that cause crashes, data loss, or produce unusable output).

## Known risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Windows build fights us in Phase 0 (MSVC, Qt deployment, miniaudio) | Medium | High | Phase 0 CI matrix catches it in day 2. Escape hatch: fix, drop Windows, or abort. |
| libsamplerate output diverges audibly from scipy | Low-Medium | Medium | Not chasing bit-exactness. Manual listening at end of Phase 3 is the judge. If divergence is bad, swap to hand-written Kaiser polyphase. |
| Hidden Serum-path bugs masked by "good enough" testing | Low | Medium | Test matrix: 4 morph types × 8 Y × 8 Z-pages; listen to a sample of each. |
| Qt 6 resource system fights the dart-sass build step | Low | Low | Worst case, invoke `rcc` manually instead of `qt_add_resources`. |
| Custom Pangram font embedding fails silently on one platform | Low | Low | On first launch, verify `QFontDatabase::families()` contains "Pangram"; log a warning if not. |
| PFFFT or miniaudio has a platform blocker bug | Low | Medium | Both are mature single-file libraries with many shipping users. Fallbacks: KissFFT for FFT, RtAudio for I/O. |
| Developer-tool friction from the Node/dart-sass dependency | Low | Low | Documented in top-level README; `npx sass` is the only new prerequisite. |

## Deferred decisions

These are recorded here rather than in the implementation plan so they don't become TODOs-in-disguise:

1. **License choice (MIT vs Apache-2.0).** Either works. Pick before Phase 0 commits land. FourSeas firmware is MIT → MIT is the simpler choice.
2. **Git LFS for golden test WAVs.** Only needed if golden data grows past ~10 MB. Evaluate in Phase 3 when real golden data exists.
3. **Code-signing cert acquisition.** Orthogonal to the rewrite; CI is written to sign conditionally on env vars, so acquiring a cert later is a zero-code-change addition.
4. **Whether to upstream the vendored `bindings.cpp` C++ classes to the public FourSeas repo.** After cutover, consider a PR carving `host/{wav_reader,wavetable_engine}.{h,cpp}` out of the private binding so the submodule carries them automatically. Clean but not on the critical path.

## Implementation handoff

Once this spec is approved and committed, the next step is to invoke the `superpowers:writing-plans` skill to produce `docs/superpowers/plans/2026-04-07-cpp-qt-rewrite.md` — a detailed, TDD-structured, bite-sized task plan that implements the phases above. Execution of that plan uses either `superpowers:subagent-driven-development` or `superpowers:executing-plans`, to be decided at plan-writing time.
