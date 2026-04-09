# Follow-ups

Things noticed during implementation that aren't blocking but should be revisited later. Append to this list as new ones come up; remove (with a commit reference) when fixed.

## Phase 4

- **Frame resampling — offer sample-domain as an alternative to the default frequency-domain approach.** Phase 4a implements Serum-mode frame resampling in the frequency domain (interpolate source frame FFTs, take phases from the nearest source). This is cleaner for non-smoothly-varying wavetables but deviates from Python's sample-domain linear interpolation. A future revision could expose both as a user-selectable option in the Serum screen — the two produce audibly different results for chopped/percussive Serum files and the user might prefer either. ~50 LOC to add the alternative code path.

## Phase 2

- **`SingleWavService` does not validate the input file.** It accepts any path and passes it straight to `SingleWavGenerator`, which may fail if the file is not a supported WAV. Add explicit validation with a user-friendly error.
- **`SingleWavService::Generate()` is not cancellable.** Real DSP generation can take several seconds on a slow machine — add a `Cancel()` slot, an atomic `cancel_requested_` flag checked between pages, and a UI button to trigger it.
- **Stub bank output dir is hardcoded to `QStandardPaths::AppLocalDataLocation/audio_resynth`.** Plumb this through `Settings::OutputDir()` and let the user override via a directory picker.
- **`engine_->LoadBank()` runs on the GUI thread.** Currently called from `AnyWavScreen::SetState(kDonePreviewAvailable)` after generation finishes. Fast for typical banks (~2MB total) — move to a `QRunnable` only if it ever stalls the UI noticeably.
- **Styling needs manual tuning.** The QSS port is functional but visually wonky compared to the design SVGs — paddings, spacings, color choices need adjustment. Will be done iteratively against the designs.

## Preview widget

- **Add an oscilloscope and spectrogram view to the preview widget.** Inspired by iZotope RX's combined waveform/spectrogram display. The oscilloscope would show the live output buffer (the same samples being sent to the audio device), and the spectrogram would show a rolling FFT of the same. Helps users see what the wavetable bank is actually doing as they sweep the X/Y/Z sliders, not just hear it. Likely a custom QWidget with a per-frame `QPainter::drawPolyline` for the scope and a scrolling QImage for the spectrogram, fed by an audio-thread → GUI-thread sample tap (lock-free ring buffer).

## Audio engine

- **Aggressive slider scans still leak some zipper noise** — `WavetableVoice` applies a per-sample one-pole smoother (`kParamSmoothingCoeff = 0.005f`, ~4ms time constant) to X/Y/Z and frequency. Slow drags are clean, but slamming a slider back and forth can still produce audible artifacts because the parameter changes happen faster than the smoothing can mask. Possible fixes if it ever bothers users in real use: tune the coefficient (try `0.002` for more masking at the cost of sluggishness), crossfade between two voice instances at audio-block boundaries, or apply adaptive smoothing that locks in when slider velocity is high. Phase 1 ships with the current setting because the artifact only appears in torture-test scenarios.
- **`std::atomic_load` / `std::atomic_store` for `std::shared_ptr` in `WavetableVoice`** is deprecated in C++20. The intended migration target — `std::atomic<std::shared_ptr<T>>` (P0718R2) — is not yet available in Apple libc++ (libc++ on the macOS 26.2 SDK still requires `T` to be trivially copyable). Revisit when libc++ ships the partial specialization, or switch to libstdc++ (which already has it). The deprecated free-function form still compiles silently with our current flags so this is purely cleanup.
- **`WavetableEngine` (offline rendering) was not vendored from `bindings.cpp@8d36c15`** — Phase 1 only needed the realtime path. Revisit if we want offline rendering for DSP comparison testing against the Python reference.

## Distribution

- **macOS notarized release.** `cpp/scripts/package-macos.sh` produces an ad-hoc-signed `.dmg` suitable for internal team testing, but recipients have to clear the quarantine attribute manually. For a public release we need: (1) Apple Developer Program membership, (2) a "Developer ID Application" certificate, (3) `codesign` with hardened runtime, (4) `notarytool submit --wait`, (5) `xcrun stapler staple`. Wire this into the script behind a `--notarize` flag once we have the cert.
- **Universal macOS binary.** Current `package-macos.sh` builds host-arch only (arm64 on dev machines). For Intel coverage we'd need to build twice with `-DCMAKE_OSX_ARCHITECTURES=arm64` and `=x86_64`, then `lipo -create` the executables and every dylib in `Contents/Frameworks/`. Skipped for now since all current testers are M1+.
- **Windows packaging.** No script yet. Will need `windeployqt` plus an installer (Inno Setup or WiX) and ideally an EV code-signing cert (otherwise SmartScreen will warn).
- **Linux packaging.** No script yet. AppImage is the path of least resistance for "drop in Slack and run anywhere"; alternatively a Flatpak for proper distro integration.

## Build / CI

- **CI annotations**: `actions/checkout@v4` and `actions/setup-node@v4` are flagged as Node.js 20 actions, deprecated by GitHub June 2026. Bump to whatever version supports Node.js 24 closer to that date.
- **Apple clang on `macos-latest` is older than the local Homebrew clang 22**, so the `-Wc++20-designator` warning we hit on MSVC didn't show up on macOS CI. If we ever bump the minimum clang version we should re-verify the engine builds clean.
- **macos-13 (Intel) coverage was dropped from CI** because `macos-13` is deprecated and `macos-14-large` is a paid runner. If Intel Mac compatibility ever matters, revisit by either generating a universal binary at release time via `lipo` or paying for the runner.
- **macos-26 coverage was dropped from CI** because Qt 6.8 still references the deprecated Apple AGL framework in its CMake config and Apple removed AGL in macOS 26. Re-enable when Qt fixes the cmake config OR when `macos-latest` itself rotates to macOS 26+.

## Editor experience

- **Old `/usr/local/bin/clangd`** still on the dev machine (Intel Homebrew leftover, probably). Zed is now configured to use `/opt/homebrew/opt/llvm/bin/clangd` explicitly via `.zed/settings.json` → `lsp.clangd.binary.path`. If the old binary causes issues for other tools, consider removing it.
