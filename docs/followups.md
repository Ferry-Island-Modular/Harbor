# Follow-ups

Things noticed during implementation that aren't blocking but should be revisited later. Append to this list as new ones come up; remove (with a commit reference) when fixed.

## Phase 3b (Phase 3d candidate fixes)

- **Silent/near-silent cells at X axis extremes — suggested fix in Phase 3d.** `frame_selection = int(x / 7.0 * (num_frames - 1))` means X=0 picks the first STFT frame and X=7 picks the last. For audio files that have fade-ins or fade-outs at the ends (most loop libraries do), the first and last 2048-sample STFT frames can be silent or near-silent. Two cliff behaviors result, both faithful to Python:
  - **Exactly-zero tail** (e.g. `MCH_MachineRoom_FabricFactory_CT.wav`): all 2048 samples of the last frame are exact zeros → STFT bins are zero → `_extract_single_cycle` hits `peak == 0` and returns zeros → the entire x=7 column of the wavetable grid is silent across all 8 Z pages.
  - **Near-zero tail with dither** (e.g. `MCH_MachineRoom_CarFactoryAssemblyLine_CT.wav`): a handful of samples have values near the noise floor (~1 bit in 24-bit units, ~1e-7 normalized) → STFT has tiny nonzero magnitudes → `peak > 0` so the normalization step runs → the noise floor gets amplified to 0 dBFS → x=7 cells contain "what the LSB dither sounds like at full scale". Technically audible, but musically meaningless.
  - **Suggested fix:** add a per-cell energy threshold in `CycleExtractor::Extract` before the normalization step. If the cycle's RMS or peak is below some floor (e.g. -60 dBFS relative to the file's global peak, or a fixed threshold like 1e-4), treat the cell as silent (zero) instead of normalizing the dither up to full scale. Optionally: in `SingleWavGenerator`, if the requested STFT frame's total magnitude is below the threshold, fall back to the nearest populated frame. The two together would make X extremes degrade gracefully on files with fades instead of producing silent-or-noise cliff behavior. ~40 LOC. This is a divergence from Python behavior but an improvement — Python has the same quirks and nobody's happy about them.

## Phase 2

- **Serum mode is fully deferred.** The launcher's Serum card opens a `QMessageBox` saying "not yet implemented." Designs are pending; revisit when they land.
- **Three-wavs mode is permanently `kComingSoon`.** Will be revisited only if there's user demand.
- **Y/Z axis option labels are placeholders** ("First option / Second option / Third option"). Real labels and behaviors land in Phase 3 with the DSP port.
- **`SingleWavService` does not validate the input file.** It accepts any path and feeds it to `StubBankWriter`, which ignores the input entirely (just writes sine waves). Phase 3 wires real DSP and adds input validation.
- **`SingleWavService::Generate()` is not cancellable.** Phase 2's stub generation only takes ~1 second of simulated work, so cancellation has no practical value. Real cancellation matters in Phase 3 when DSP generation takes 30+ seconds — add a `Cancel()` slot, an atomic `cancel_requested_` flag checked between progress steps, and a UI button to trigger it.
- **`Settings` is wired but not used by the screens yet.** MainWindow / AnyWavScreen don't read or write any settings. Revisit during Phase 3 when there are real values worth persisting (last-used output directory, last-used Y/Z morph indices, audio device, preview volume).
- **Stub bank output dir is hardcoded to `QStandardPaths::AppLocalDataLocation/audio_resynth`.** Phase 3 should plumb this through `Settings::OutputDir()` and let the user override via a directory picker.
- **`engine_->LoadBank()` runs on the GUI thread.** Currently called from `AnyWavScreen::SetState(kDonePreviewAvailable)` after generation finishes. Fast for the placeholder sine banks (~2MB total) but real DSP output is also small enough that this is unlikely to bite. Move to a `QRunnable` if it ever stalls the UI noticeably.
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
