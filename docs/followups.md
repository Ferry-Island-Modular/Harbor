# Follow-ups

Things noticed during implementation that aren't blocking but should be revisited later. Append to this list as new ones come up; remove (with a commit reference) when fixed.

## Phase 4

- **Frame resampling — offer sample-domain as an alternative to the default frequency-domain approach.** Phase 4a implements Serum-mode frame resampling in the frequency domain (interpolate source frame FFTs, take phases from the nearest source). This is cleaner for non-smoothly-varying wavetables but deviates from Python's sample-domain linear interpolation. A future revision could expose both as a user-selectable option in the Serum screen — the two produce audibly different results for chopped/percussive Serum files and the user might prefer either. ~50 LOC to add the alternative code path.

## Phase 2

- **Serum mode is fully deferred.** The launcher's Serum card opens a `QMessageBox` saying "not yet implemented." Designs are pending; revisit when they land.
- **Three-wavs mode is permanently `kComingSoon`.** Will be revisited only if there's user demand.
- **`SingleWavService` does not validate the input file.** It accepts any path and passes it straight to `SingleWavGenerator`, which may fail if the file is not a supported WAV. Add explicit validation with a user-friendly error.
- **`SingleWavService::Generate()` is not cancellable.** Real DSP generation can take several seconds on a slow machine — add a `Cancel()` slot, an atomic `cancel_requested_` flag checked between pages, and a UI button to trigger it.
- **Stub bank output dir is hardcoded to `QStandardPaths::AppLocalDataLocation/audio_resynth`.** Plumb this through `Settings::OutputDir()` and let the user override via a directory picker.
- **`engine_->LoadBank()` runs on the GUI thread.** Currently called from `AnyWavScreen::SetState(kDonePreviewAvailable)` after generation finishes. Fast for typical banks (~2MB total) — move to a `QRunnable` only if it ever stalls the UI noticeably.
- **Styling needs manual tuning.** The QSS port is functional but visually wonky compared to the design SVGs — paddings, spacings, color choices need adjustment. Will be done iteratively against the designs.

## Audio engine

- **Aggressive slider scans still leak some zipper noise** — `WavetableVoice` applies a per-sample one-pole smoother (`kParamSmoothingCoeff = 0.005f`, ~4ms time constant) to X/Y/Z and frequency. Slow drags are clean, but slamming a slider back and forth can still produce audible artifacts because the parameter changes happen faster than the smoothing can mask. Possible fixes if it ever bothers users in real use: tune the coefficient (try `0.002` for more masking at the cost of sluggishness), crossfade between two voice instances at audio-block boundaries, or apply adaptive smoothing that locks in when slider velocity is high. Phase 1 ships with the current setting because the artifact only appears in torture-test scenarios.
- **`std::atomic_load` / `std::atomic_store` for `std::shared_ptr` in `WavetableVoice`** is deprecated in C++20 (we're on C++20 since `cf58197`). Migrate to `std::atomic<std::shared_ptr<T>>` when convenient. The deprecated form still compiles silently with our current flags so this is purely cleanup.
- **`WavetableEngine` (offline rendering) was not vendored from `bindings.cpp@8d36c15`** — Phase 1 only needed the realtime path. Revisit during Phase 3 if we want offline rendering for DSP comparison testing against the Python reference.

## Build / CI

- **CI annotations**: `actions/checkout@v4` and `actions/setup-node@v4` are flagged as Node.js 20 actions, deprecated by GitHub June 2026. Bump to whatever version supports Node.js 24 closer to that date.
- **Apple clang on `macos-latest` is older than the local Homebrew clang 22**, so the `-Wc++20-designator` warning we hit on MSVC didn't show up on macOS CI. If we ever bump the minimum clang version we should re-verify the engine builds clean.
- **macos-13 (Intel) coverage was dropped from CI** because `macos-13` is deprecated and `macos-14-large` is a paid runner. If Intel Mac compatibility ever matters, revisit by either generating a universal binary at release time via `lipo` or paying for the runner.
- **macos-26 coverage was dropped from CI** because Qt 6.8 still references the deprecated Apple AGL framework in its CMake config and Apple removed AGL in macOS 26. Re-enable when Qt fixes the cmake config OR when `macos-latest` itself rotates to macOS 26+.

## Editor experience

- **Old `/usr/local/bin/clangd`** still on the dev machine (Intel Homebrew leftover, probably). Zed is now configured to use `/opt/homebrew/opt/llvm/bin/clangd` explicitly via `.zed/settings.json` → `lsp.clangd.binary.path`. If the old binary causes issues for other tools, consider removing it.
