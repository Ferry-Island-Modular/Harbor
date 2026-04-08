# Phase 2 Complete

Phase 2 of the C++/Qt rewrite — the main UI without DSP — is in. The throwaway Phase 1 `PreviewWindow` has been deleted; the app now boots into a launcher screen and routes through a state-machine UI for the "any wav" mode, with a stubbed Generate flow that produces playable placeholder banks.

## What got built

- **`fim::app::Settings`** — `QSettings` wrapper with typed getters/setters for `output_dir`, `samples_per_frame`, `mode`, `audio_device`, `preview_volume`, `y_morph`, `z_morph`. 4 Catch2 tests covering round-trip and persistence (uses a `TEST_CASE_METHOD` fixture that wipes the QSettings in-memory cache between cases).
- **`fim::app::StubBankWriter`** — writes 8 placeholder wavetable WAV files containing sine waves at 8 distinct frequencies, one period per cycle on page 1 up to eight on page 8. Pure `dr_wav`, no Qt deps. 3 Catch2 tests.
- **`fim::app::SingleWavService`** — `QObject` orchestration. Owns the input file path, queues a `QRunnable` on `QThreadPool::globalInstance()` that emits progress over ~1 second of simulated work, then calls `StubBankWriter::WriteSineBank`. Phase 3 swaps in real DSP without changing this API.
- **5 reusable Qt widgets** in `cpp/src/ui/widgets/`:
  - `CardButton` — mode chooser card, three states (default/chosen/coming-soon)
  - `FileDropWidget` — dashed-border drop area with `QFileDialog` fallback on click
  - `AxisMorphSelector` — title + radio button row, used for Y and Z axes
  - `CustomProgressBar` — `QLabel` + `QProgressBar` composite
  - `PreviewControlsWidget` — extracted from Phase 1's `PreviewWindow`, now embeddable, holds a non-owning pointer to `RealtimeAudioEngine`
- **`fim::ui::LauncherScreen`** — header + three `CardButton`s. Emits `modeChosen(Mode)`.
- **`fim::ui::AnyWavScreen`** — internal `QStackedWidget` with five sub-pages (`kEmpty`, `kFileSet`, `kGenerating`, `kDoneMessage`, `kDonePreviewAvailable`). Owns a `SingleWavService`, the axis selectors, the progress bar, and the embedded preview controls.
- **`fim::ui::MainWindow`** — top-level `QMainWindow` with a `QStackedWidget` router. Owns the `RealtimeAudioEngine` instance. Basic menu bar with placeholder File / Audio / Settings menus.
- **SCSS additions** to `cpp/styles/input.scss` for every new widget and screen, transpiled at build time and embedded via `qt_add_resources`. Stale Python-era class-selector rules cleaned up as each widget landed.
- **Phase 1's `PreviewWindow` deleted.**

## Catch2 test count

| Phase | Tests |
|---|---|
| Phase 0 (smoke) | 2 |
| Phase 1 (engine layer) | 11 |
| Phase 2 (Settings + StubBankWriter) | 7 |
| **Total** | **20** |

UI code is compiled but not unit-tested — Qt widgets are verified manually.

## Bugs caught during manual verification

- **Stub output landed in `cwd=/`** when launched as a `.app` bundle. Fixed by switching to `QStandardPaths::writableLocation(AppLocalDataLocation)` (`ba1e94b`). Also wired the `generationFailed` signal to a `QMessageBox` so silent failures don't get swallowed in future.
- **Preview Play/Stop button stayed labeled "Stop"** after `OnGenerateClicked` stopped the engine externally. Fixed by adding `PreviewControlsWidget::RefreshPlayButton()` (`5f226cd`).

## ⚠️ Known issue — Pangram font is NOT bundled

**Distribution blocker.** The SCSS references `font-family: 'Pangram'` but no font file ships with the app. The developer's Mac has Pangram installed system-wide; on every other machine Qt silently falls back to the system default sans. The Python tool has the exact same pre-existing issue. Must be resolved before any release tag — three options documented in `docs/followups.md` (license + bundle, substitute lookalike, accept system fallback). Decision pending designer input.

## Other spec deviations

- **`AxisMorphSelector` implemented in Phase 2** even though the spec assigned it to Serum-only — the new "any wav" designs require Y/Z selectors there too.
- **Serum mode fully deferred** until designs are ready.
- **`Settings` is implemented but not yet used** by the screens. Will be wired in Phase 3.
- **Styling needs manual visual tuning.** The QSS is functional but doesn't yet match the design SVGs precisely.

## Next phase

Phase 3 — **single-wav DSP port**. Replace `StubBankWriter` with a real audio resynthesis port from `lib/audio_resynthesis.py`. Set up the Python oracle test harness. Wire the Y/Z axis option labels to actual DSP behaviors. The `SingleWavService` API stays the same; only its internal worker changes.
