# Follow-ups

Things noticed during implementation that aren't blocking but should be revisited later. Append to this list as new ones come up; remove (with a commit reference) when fixed.

## ⚠️ KNOWN ISSUES — must address before any release

### Pangram font is NOT bundled with the app

**Severity:** Distribution blocker.

`cpp/styles/input.scss` declares `font-family: 'Pangram'` for every text element in the UI, but **no Pangram font file ships with the app**. Qt resolves the family name from the OS font cache at runtime, which means:

- On the developer's Mac (where Pangram is installed system-wide), the app looks correct.
- On every other machine — CI runners, end-user downloads, fresh installs — Qt silently falls back to the system default sans, and the app does NOT match the designs in `designs/`.

This is a real bug that affects every release. It must be resolved before tagging a public version.

**Possible fixes (decision for the project owner + designer):**

1. **License + bundle Pangram.** The font is commercial (Pangram Pangram Foundry). Buy a license that allows redistribution as part of an application, then drop the font files into `cpp/resources/fonts/` and load them via `QFontDatabase::addApplicationFont` from a Qt resource. Cost: licensing fee. Most accurate to the design.
2. **Substitute a permissively-licensed lookalike sans.** Candidates: Inter (SIL OFL, very common), Public Sans (SIL OFL), Geist Sans (MIT). Pick one, update `cpp/styles/input.scss`, register via `QFontDatabase::addApplicationFont`. Cost: free, requires designer signoff.
3. **Accept system fallback as shipped behavior.** The app looks slightly different on every OS. Cost: zero, but the design becomes "approximate".

The Python tool currently has the exact same bug — it also doesn't bundle Pangram. So this isn't a regression introduced by the rewrite; it's a pre-existing issue we now have to acknowledge and fix.

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
