# Phase 1 Complete

As of this commit, Phase 1 of the C++/Qt rewrite — the realtime preview escape-hatch checkpoint — is complete. **Audio is coming out of the C++ rewrite via the real FourSeas engine.**

## What got built

- **`fim::engine::LoadWavMono`** — `dr_wav`-based mono float WAV reader. Replaces the hand-rolled `fstream`+`WavHeader` from `bindings.cpp@8d36c15`. ~30 lines.
- **`fim::engine::WavetableBank`** — loads 8 wavetable WAV pages (`1.wav`–`8.wav`) into one contiguous `std::vector<float>` with the `float**` pointer table the FourSeas oscillator's `Init()` expects. Immutable after construction.
- **`fim::engine::WavetableVoice`** — wraps `fourseas::WavetableOscillator<2048, false, false>` with thread-safe atomic parameter setters and a callback-friendly `RenderBlock()`. Uses `std::shared_ptr<const WavetableBank>` accessed via free-function `std::atomic_load`/`store` for **lock-free double-buffered bank swap** — the audio thread takes a local snapshot of the shared_ptr at the start of each block, the loader thread atomically installs new banks, and old banks are released only when no callback is still holding them. Per-sample one-pole smoothing on X/Y/Z and frequency to mask zipper noise on slider drags.
- **`fim::engine::RealtimeAudioEngine`** — owns the voice plus a miniaudio playback device. Implements the `PlayMode` state machine (steady/sweep/arpeggio), MIDI-note→Hz conversion (`440 * 2^((note-69)/12)`), volume + invert (matching the FourSeas hardware's inverting op-amp), and exponential one-pole volume smoothing in the audio callback. Audio I/O via miniaudio.
- **`fim::ui::PreviewWindow`** — minimal Qt `QMainWindow` with X/Y/Z position sliders, MIDI pitch slider, volume slider, Load Bank button (uses `QFileDialog`), Play/Stop button, status label. **Throwaway** — Phase 2 replaces it entirely with the production UI from `designs/start-state.svg`.
- **`main.cpp`** rewired to construct a `PreviewWindow` instead of the Phase 0 hello-world.
- **13 Catch2 tests** (all passing on macOS, Ubuntu, and Windows MSVC):
  - 2 from Phase 0 (engine instantiation + arithmetic sanity)
  - 2 for `LoadWavMono` (round-trip a known WAV, missing-file case)
  - 2 for `WavetableBank::Load` (synthetic 8-page bank, missing dir)
  - 2 for `WavetableVoice` (silence vs nonzero rendering)
  - 5 for `RealtimeAudioEngine` math helpers (`MidiToFrequency`, sweep position math, sweep clamp at end, arpeggio index math, arpeggio interval table)

## Escape hatch cleared

The Phase 1 checkpoint was: **"can we hear audio coming out of the C++ rewrite via the real FourSeas engine?"** Answer: **yes**. Manual verification on macOS (Apple Silicon):

- ✅ App launches as a 560×380 Qt window
- ✅ "Load Bank…" opens a `QFileDialog` and loads the existing `output_waves/audio_resynth/` bank from the Python tree without errors
- ✅ Play button starts the miniaudio playback device
- ✅ Audio is audible and matches the FourSeas firmware's expected output (verified by ear; scope verification pending)
- ✅ X/Y/Z slider drags change timbre in real time
- ✅ Pitch slider changes the note via MIDI conversion
- ✅ Volume slider changes loudness without audible clicks (one-pole smoothing)
- ✅ Slow slider drags are artifact-free; aggressive scans leak minor zipper noise (documented in `docs/followups.md`)

## Cross-platform CI

The full engine layer + Qt UI compiles cleanly on all CI runners:

| Job | Status | Notes |
|---|---|---|
| `clang-format check` | ✅ | |
| `Build - macos-latest` (clang) | ✅ | |
| `Build - ubuntu-latest` (gcc) | ✅ | |
| `Build - windows-latest` (MSVC2022) | ✅ | Required `/std:c++20` because designated initializers in `wavetable_voice.cpp` are a C++17 extension on clang but a hard error on MSVC. Bump done in `cf58197`. |

## Spec deviations worth noting

- **`WavetableEngine` (offline rendering) was NOT vendored** from `bindings.cpp@8d36c15` even though the Phase 1 spec text mentioned it. It's not on the realtime preview path — only the offline `render_position`/`render_sweep`/`render_lfo_modulation` flow uses it. Deferred to Phase 3 if/when offline rendering becomes useful for DSP comparison testing against the Python reference. See `docs/followups.md`.
- **C++ standard bumped 17 → 20** to support designated initializers under MSVC strict mode. Fine in 2026 — Qt 6.8, FourSeas firmware, and all our CI compilers handle C++20 happily.

## Phase 1 commit log (12 commits since Phase 0 cutover)

```
feat(cpp): smooth X/Y/Z and frequency in audio callback
chore(cpp): silence clang 22 c2y-extensions warning in clangd
feat(cpp): wire main to Phase 1 PreviewWindow
feat(cpp): add Phase 1 PreviewWindow
fix(cpp): bump C++ standard 17 -> 20 (MSVC needs designated initializers)
feat(cpp): add RealtimeAudioEngine wrapping miniaudio + voice
feat(cpp): add WavetableVoice with atomic bank swap
feat(cpp): add WavetableBank loader
feat(cpp): add dr_wav-based mono WAV loader
feat(cpp): add single-header library implementation TU
docs: add Phase 1 implementation plan (realtime preview)
```

## Next phase

Phase 2 — **Main UI, no DSP**. Replace the throwaway `PreviewWindow` with the production UI from `designs/start-state.svg`, including the 7 custom widgets (`FileDropWidget`, `CardButton`, `AxisMorphSelector`, `CustomProgressBar`, etc.), the QSettings persistence layer, the menu bar (File / Audio / Settings), and a stubbed Generate button that writes placeholder WAV files. The styled QSS pipeline already works from Phase 0; Phase 2 wires it to real widgets.
