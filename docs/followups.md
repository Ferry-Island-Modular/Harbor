# Follow-ups

Things noticed during implementation that aren't blocking but should be revisited later. Append to this list as new ones come up; remove (with a commit reference) when fixed.

## Phase 4

- **Frame resampling — offer sample-domain as an alternative to the default frequency-domain approach.** Phase 4a implements Serum-mode frame resampling in the frequency domain (interpolate source frame FFTs, take phases from the nearest source). This is cleaner for non-smoothly-varying wavetables but deviates from Python's sample-domain linear interpolation. A future revision could expose both as a user-selectable option in the Serum screen — the two produce audibly different results for chopped/percussive Serum files and the user might prefer either. ~50 LOC to add the alternative code path.

## Phase 2

- **`SingleWavService` does not validate the input file.** It accepts any path and passes it straight to `SingleWavGenerator`, which may fail if the file is not a supported WAV. Add explicit validation with a user-friendly error.
- **`SingleWavService::Generate()` is not cancellable.** Real DSP generation can take several seconds on a slow machine — add a `Cancel()` slot, an atomic `cancel_requested_` flag checked between pages, and a UI button to trigger it.
- **`engine_->LoadBank()` runs on the GUI thread.** Currently called from `AnyWavScreen::SetState(kDonePreviewAvailable)` after generation finishes. Fast for typical banks (~2MB total) — move to a `QRunnable` only if it ever stalls the UI noticeably.
- **Styling needs a full design pass.** Phase 5 addressed the worst offenders (equal-width columns, min window size, three-wav column layout) but the QSS port is still visually rough compared to the Penpot SVGs — paddings, spacings, color choices, font sizes all need iterative tuning against `designs/`.

## Phase 5

- **Help dialog content needs review.** `cpp/resources/help/help.html` was written from a developer's mental model. After the first round of beta testers, rewrite based on the questions they actually ask. May want to add screenshots once styling is finalized.
- **AboutDialog version string is hardcoded to "(beta)".** Once we cut a non-beta release, drop the suffix and pull the channel from a CMake option (e.g. `-DHARBOR_RELEASE_CHANNEL=stable`).
- ~~**`package-macos.sh` contaminates the dev .app bundle.**~~ Fixed: the
  packaging script now runs `macdeployqt` against a temporary staging copy and
  leaves the development bundle untouched.
- **GitHub URL in About dialog and help.html will change.** Currently hardcoded to `github.com/jgoney/fim-config-tool`. Update when the repo is renamed or moved to an org.

## Preview widget

- **Add an oscilloscope and spectrogram view to the preview widget.** Inspired by iZotope RX's combined waveform/spectrogram display. The oscilloscope would show the live output buffer (the same samples being sent to the audio device), and the spectrogram would show a rolling FFT of the same. Helps users see what the wavetable bank is actually doing as they sweep the X/Y/Z sliders, not just hear it. Likely a custom QWidget with a per-frame `QPainter::drawPolyline` for the scope and a scrolling QImage for the spectrogram, fed by an audio-thread → GUI-thread sample tap (lock-free ring buffer).

## Audio engine

- **Aggressive slider scans still leak some zipper noise** — `WavetableVoice` applies a per-sample one-pole smoother (`kParamSmoothingCoeff = 0.005f`, ~4ms time constant) to X/Y/Z and frequency. Slow drags are clean, but slamming a slider back and forth can still produce audible artifacts because the parameter changes happen faster than the smoothing can mask. Possible fixes if it ever bothers users in real use: tune the coefficient (try `0.002` for more masking at the cost of sluggishness), crossfade between two voice instances at audio-block boundaries, or apply adaptive smoothing that locks in when slider velocity is high. Phase 1 ships with the current setting because the artifact only appears in torture-test scenarios.
- **`std::atomic_load` / `std::atomic_store` for `std::shared_ptr` in `WavetableVoice`** is deprecated in C++20. The intended migration target — `std::atomic<std::shared_ptr<T>>` (P0718R2) — is not yet available in Apple libc++ (libc++ on the macOS 26.2 SDK still requires `T` to be trivially copyable). Revisit when libc++ ships the partial specialization, or switch to libstdc++ (which already has it). The deprecated free-function form still compiles silently with our current flags so this is purely cleanup.
- **`WavetableEngine` (offline rendering) was not vendored from `bindings.cpp@8d36c15`** — Phase 1 only needed the realtime path. Revisit if we want offline rendering for DSP comparison testing against the Python reference.

## Distribution

- **macOS release credentials.** The packaging script now supports Developer ID
  signing, hardened runtime, notarization, and stapling behind `--notarize`.
  The remaining external setup is Apple Developer Program enrollment, a
  Developer ID Application certificate, and a `notarytool` Keychain profile.
- **Universal macOS binary.** Current `package-macos.sh` builds host-arch only (arm64 on dev machines). For Intel coverage we'd need to build twice with `-DCMAKE_OSX_ARCHITECTURES=arm64` and `=x86_64`, then `lipo -create` the executables and every dylib in `Contents/Frameworks/`. Skipped for now since all current testers are M1+.
- **Windows public-release signing.** CI now emits an unsigned NSIS installer
  and portable ZIP for beta testers. For public distribution, add an MSIX
  package for Microsoft Store signing or use Microsoft Artifact Signing for
  direct downloads. Buying EV solely for SmartScreen is no longer justified.
- **Linux packaging.** No script yet. AppImage is the path of least resistance for "drop in Slack and run anywhere"; alternatively a Flatpak for proper distro integration.

## Build / CI

- **CI annotations**: `actions/checkout@v4` and `actions/setup-node@v4` are flagged as Node.js 20 actions, deprecated by GitHub June 2026. Bump to whatever version supports Node.js 24 closer to that date.
- **Apple clang on `macos-latest` is older than the local Homebrew clang 22**, so the `-Wc++20-designator` warning we hit on MSVC didn't show up on macOS CI. If we ever bump the minimum clang version we should re-verify the engine builds clean.
- **macos-13 (Intel) coverage was dropped from CI** because `macos-13` is deprecated and `macos-14-large` is a paid runner. If Intel Mac compatibility ever matters, revisit by either generating a universal binary at release time via `lipo` or paying for the runner.
- **macos-26 coverage was dropped from CI** because Qt 6.8 still references the deprecated Apple AGL framework in its CMake config and Apple removed AGL in macOS 26. Re-enable when Qt fixes the cmake config OR when `macos-latest` itself rotates to macOS 26+.

## Editor experience

- **Old `/usr/local/bin/clangd`** still on the dev machine (Intel Homebrew leftover, probably). Zed is now configured to use `/opt/homebrew/opt/llvm/bin/clangd` explicitly via `.zed/settings.json` → `lsp.clangd.binary.path`. If the old binary causes issues for other tools, consider removing it.
