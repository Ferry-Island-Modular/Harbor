# Phase 4a Implementation Plan — Refactor shared infrastructure + Serum DSP core

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lay the groundwork for Serum mode by (1) extracting shared base classes for services and mode screens from the existing single-wav code, (2) backporting the Smear morph mode to single-wav for feature parity, and (3) porting the Serum wavetable converter's DSP core (loader, morpher, generator) as a new `fim::dsp` subsystem. Phase 4b builds the Serum UI on top of these pieces.

**Architecture:** Two new base classes carve out the parts of `SingleWavService` and `AnyWavScreen` that are mode-agnostic — `GenerateServiceBase` (QObject with `QThreadPool` machinery + progress/finished/failed signals + virtual `DoGenerate`) and `ModeScreenBase` (QWidget with the 5-state `QStackedWidget` machine + back button + title + progress bar + embedded preview controls + virtual content hooks). `AnyWavScreen` and `SingleWavService` shrink to the any-wav-specific bits and inherit the shared scaffolding. The Serum DSP core lives in `fim::dsp::` as `SerumLoader` (reads Serum-format WAV files and splits into `N × 2048` sample frames), `SerumMorpher` (holds the 4 Vital-derived morph implementations: formant scale, phase disperse, smear, harmonic stretch, plus a frequency-domain frame resampler that interpolates `N` source frames' FFTs directly to 8 target FFT caches), and `SerumGenerator` (top-level orchestration with a `GenerateOptions`-style struct, running the full 8-page × 64-cell pipeline and writing 8 WAV files via dr_wav). A `SerumWavService` inheriting the new base completes the plumbing for Phase 4b's UI wiring.

**Tech Stack:** C++20, Qt 6.8, existing Phase 3 DSP layer (`RealFft`, `WavLoader`, `HannWindow`, `FftResampler`), `dr_wav`, Catch2 v3. No new dependencies.

**Spec deviations from Python reference:**

- **Frequency-domain frame resampling instead of sample-domain.** Python's `SerumWavetableConverter.resample_frames_to_8` does per-sample linear interpolation across frames in the time domain. Phase 4a replaces this with frequency-domain interpolation: compute `rfft` once per source frame, then linearly interpolate the **magnitudes** between the two nearest source frames for each of the 8 target slots, and take **phases** from the nearest source frame (no phase unwrapping). This is better quality for non-smoothly-varying wavetables (no frame-dimension aliasing), more efficient (the 8 output FFT caches are produced directly without materializing intermediate time-domain frames), and sidesteps the phase-misalignment issue inherent in sample-domain interpolation. Deviates from Python bit-for-bit but matches perceptually for typical wavetables. A Phase 4c or later polish pass could offer both as a user-selectable option.
- **DC and Nyquist bins are NOT rotated by phase disperse.** Python's `phase_disperse_morph` applies phase rotation to bin 0 (DC) and bin N/2 (Nyquist), which is technically invalid since those bins must be real in a real-signal rfft. Python works anyway because `numpy.fft.irfft` silently discards the imaginary parts of bin 0 and Nyquist — the "rotation" becomes an amplitude scaling by `cos(phase_shift)`. Phase 4a explicitly skips rotating DC and Nyquist to keep those bins properly real. Inaudible difference, cleaner math.
- **Bidirectional formant scaling, not monotonic-up.** Python's formant scale uses `scale = 1.0 + amount * 3.0` (range 1 to 4, always shifts formants up). Phase 4a uses `scale = 2^(4 * amount - 2)` — range 0.25 to 4, identity at `amount = 0.5`. Matches the "err toward extreme" design principle by giving twice as much range and making both directions available. Deviates from Python but is strictly more useful musically.
- **`phase_disperse_morph` reserved for Serum only.** Single-wav already has `ZMode::kDisperse` from Phase 3d, which uses my own parabolic phase shift (different center, different scaling). We're NOT unifying the Phase 3d implementation with Serum's Vital-derived version. Single-wav and Serum each have their own bespoke phase-disperse implementation tuned to their data shape (2D STFT vs 1D single-frame rfft). This accepts a small amount of algorithmic duplication in exchange for not touching shipped single-wav behavior.
- **Single-wav's Smear is bespoke, NOT the Vital version.** Similarly, the Phase 4a Smear backport to single-wav's `SpectralModifier` uses a simple adjacent-bin averaging filter rather than Vital's running-average-with-`(i+0.25)/i`-scaling. Serum's Smear uses Vital's version. Same rationale — single-wav already has its own morph-mode aesthetic from Phase 3d.
- **Refactor tasks come FIRST in the phase, Serum DSP SECOND.** `GenerateServiceBase` and `ModeScreenBase` are extracted before any Serum code lands, so the any-wav regression check happens on a bisect-clean commit. Building Serum on top of the refactored bases proves the abstractions work on the first real consumer.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files (created by this phase):**

- `cpp/src/app/services/generate_service_base.h` / `generate_service_base.cpp` — abstract `fim::app::GenerateServiceBase` QObject. Holds `input_file_`, `output_directory_`, `generating_` atomic, the `Generate()` slot that queues work on `QThreadPool::globalInstance()`, and the three signals (`progressChanged`, `generationFinished`, `generationFailed`). Subclasses implement the pure virtual `DoGenerate(input, output, progress_cb) -> bool`.
- `cpp/src/ui/mode_screen_base.h` / `mode_screen_base.cpp` — `fim::ui::ModeScreenBase` QWidget. Holds the `QStackedWidget` with five states (`kEmpty`, `kFileSet`, `kGenerating`, `kDoneMessage`, `kDonePreviewAvailable`), back button, title label, progress bar, preview controls widget, done-message timer. Exposes protected virtual hooks: `ModeTitle()`, `BuildEmptyPageContent(page)`, `BuildFileSetPageContent(page)`, `Service()`, `OnGenerateRequested()`, `OnFileDropped(path)`, `OnClearRequested()`, `OnResetHook()`. Subclasses fill in the mode-specific bits.
- `cpp/src/dsp/serum_loader.h` / `serum_loader.cpp` — `fim::dsp::SerumLoader::Load(path) -> std::optional<SerumFrames>`. Reads a Serum-format WAV (via existing `WavLoader`), validates the sample count is a multiple of 2048, splits into `N × 2048` frames, removes DC per frame. Does NOT interpolate to 8 frames — that's the morpher's job. Returns the raw frames plus the source sample rate.
- `cpp/src/dsp/serum_morpher.h` / `serum_morpher.cpp` — `fim::dsp::SerumMorpher` class. Holds a `RealFft(2048)` for forward + inverse transforms and scratch buffers. Provides:
  - `enum class SerumMode { kFormant, kPhase, kSmear, kStretch }` and `struct SerumFftCache { amplitudes, phases, normalized_real, normalized_imag }` (all `std::vector<float>` of length 1025)
  - `ComputeCache(frame) -> SerumFftCache` — rfft a single frame and fill the cache
  - `InterpolateCaches(source_caches, output_count) -> std::vector<SerumFftCache>` — frequency-domain frame resampling (lerp magnitudes, nearest-frame phases)
  - `Apply(cache, mode, amount) -> std::vector<std::complex<float>>` — the 4 Vital-derived morph ops
- `cpp/src/dsp/serum_generator.h` / `serum_generator.cpp` — `fim::dsp::SerumGenerator` class + `SerumGenerateOptions` struct. Top-level orchestration: load via SerumLoader, compute source FFT caches, frequency-interp to 8 caches, loop 8 pages × 64 cells applying Y then Z morph (re-derive cache between them), inverse FFT → DC removal → peak normalize → append to page. Global page normalize + WAV write per page. No tests — end-to-end verified via the UI in Phase 4b.
- `cpp/src/app/services/serum_wav_service.h` / `serum_wav_service.cpp` — `fim::app::SerumWavService` inheriting `GenerateServiceBase`. Caches a `SerumGenerateOptions`, exposes `SetYMode(SerumMode)` / `SetZMode(SerumMode)` setters, overrides `DoGenerate` to call `SerumGenerator::Generate`.
- `cpp/tests/dsp_serum_loader_test.cpp` — 3 tests: valid 2-frame Serum WAV loads, invalid length (not multiple of 2048) returns nullopt, DC per frame is approximately zero after loading.
- `cpp/tests/dsp_serum_morpher_test.cpp` — 5 tests: formant scale bidirectional (identity at amount=0.5, shifts up at amount=1, shifts down at amount=0), phase disperse leaves DC and Nyquist amplitudes unchanged, smear reduces magnitude variance, harmonic stretch preserves DC, frame cache interpolation produces correct count.

**Modified files:**

- `cpp/src/dsp/spectral_modifier.h` — add `YMode::kSmear` to the enum.
- `cpp/src/dsp/spectral_modifier.cpp` — add `ApplySmear` function and a case in the Y switch. Bespoke implementation (adjacent-bin averaging), NOT Vital's running average.
- `cpp/tests/dsp_spectral_modifier_test.cpp` — add 1 test: `YMode::kSmear` reduces adjacent-bin magnitude variance.
- `cpp/src/ui/any_wav_screen.cpp` — update `y_options` QStringList to include `"Smear"` as a fourth option. Update `YModeFromIndex` helper to handle index 3.
- `cpp/src/app/services/single_wav_service.h` / `single_wav_service.cpp` — change base class from `QObject` to `fim::app::GenerateServiceBase`. Remove the duplicated `input_file_`, `output_directory_`, `generating_`, `Generate()`, and signal declarations. Override `DoGenerate`. Keep `SetYMode`/`SetZMode`/`options_`.
- `cpp/src/ui/any_wav_screen.h` / `any_wav_screen.cpp` — change base class from `QWidget` to `fim::ui::ModeScreenBase`. Remove duplicated state machine, back button, title, progress bar, and preview controls ownership. Implement the protected virtual hooks. Keep the any-wav-specific service, file drop widget, axis selectors.
- `cpp/CMakeLists.txt` — add 7 new source files.
- `cpp/tests/CMakeLists.txt` — add 2 new test files and their implementation source dependencies.
- `docs/followups.md` — add a new entry: "Sample-domain vs frequency-domain frame resampling — offer both as a user option in a future revision".

**Deleted files:** none.

---

## Task 1: Backport Smear to single-wav SpectralModifier

Add `YMode::kSmear` to the enum and implement the corresponding branch in `SpectralModifier::Apply`. Bespoke implementation (adjacent-bin averaging), not Vital's. Update the any-wav screen's Y axis selector to show 4 buttons. This task is first because it's the smallest and most isolated — no refactor risk, proves the test infrastructure is healthy before we start moving code around.

**Files:**
- Modify: `cpp/src/dsp/spectral_modifier.h`
- Modify: `cpp/src/dsp/spectral_modifier.cpp`
- Modify: `cpp/src/ui/any_wav_screen.cpp`
- Modify: `cpp/tests/dsp_spectral_modifier_test.cpp`

- [ ] **Step 1: Add the new enum value**

Modify `cpp/src/dsp/spectral_modifier.h`. Update the `YMode` enum to add `kSmear`:

```cpp
enum class YMode {
    kTilt,
    kFormant,
    kStretch,
    kSmear,
};
```

And update the docstring above the enum:

```cpp
// Y-axis morph modes. The original Phase 3b behavior is kTilt.
//
// - kTilt:    exponential brightness shift (darkens or brightens uniformly)
// - kFormant: shifts the spectral envelope up or down in frequency while
//             preserving its shape — sounds like a formant shift on vowels
// - kStretch: non-linear log-frequency remapping that stretches or
//             compresses the harmonic spacing — inharmonic/bell-like character
// - kSmear:   adjacent-bin magnitude averaging — softens spectral peaks,
//             creates a diffuse/blurred texture
```

- [ ] **Step 2: Write the failing test**

Append to `cpp/tests/dsp_spectral_modifier_test.cpp`:

```cpp
TEST_CASE("SpectralModifier smear mode reduces adjacent-bin magnitude variance",
          "[dsp][spectral_modifier]") {
    // Build a spiky spectrum with every other bin at mag=1 and the rest at 0.
    const std::size_t num_bins = 16;
    const std::size_t num_frames = 2;
    std::vector<std::vector<float>> magnitude(num_bins,
                                              std::vector<float>(num_frames, 0.0f));
    std::vector<std::vector<float>> phase(num_bins,
                                          std::vector<float>(num_frames, 0.0f));
    for (std::size_t k = 0; k < num_bins; ++k) {
        if (k % 2 == 0) {
            magnitude[k][0] = magnitude[k][1] = 1.0f;
        }
    }

    // Compute variance before.
    auto variance = [](const std::vector<std::vector<float>>& mag) {
        float sum = 0.0f;
        float count = 0.0f;
        for (const auto& row : mag) {
            for (float v : row) {
                sum += v;
                count += 1.0f;
            }
        }
        const float mean = sum / count;
        float var = 0.0f;
        for (const auto& row : mag) {
            for (float v : row) {
                var += (v - mean) * (v - mean);
            }
        }
        return var / count;
    };
    const float variance_before = variance(magnitude);

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/7, /*z=*/0,
                   fim::dsp::YMode::kSmear, fim::dsp::ZMode::kRandom);

    const float variance_after = variance(magnitude);
    // Smear should significantly reduce the bin-to-bin variance.
    REQUIRE(variance_after < variance_before * 0.5f);
}
```

- [ ] **Step 3: Verify the test fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `YMode::kSmear` not a valid case in the switch statement (the switch in `SpectralModifier::Apply` doesn't handle it yet).

- [ ] **Step 4: Implement ApplySmear**

Modify `cpp/src/dsp/spectral_modifier.cpp`. Add the `ApplySmear` helper in the anonymous namespace after `ApplyHarmonicStretch`:

```cpp
// Y3: smear. Softens spectral peaks by averaging each bin with its
// neighbors, weighted by y_norm. At y_norm=0 this is identity; at y_norm=1
// each bin is fully replaced by the average of its 5-bin neighborhood.
//
// Bespoke implementation — NOT Vital's running-average-with-(i+0.25)/i
// scaling. That version lives in SerumMorpher for Serum mode. Both are
// "smear" but tuned to their data shapes (single-wav's 2D STFT vs Serum's
// 1D single-frame rfft).
void ApplySmear(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins < 3 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    // Per frame: compute a 5-tap moving average of the magnitude column,
    // then lerp from the original into the smoothed version by y_norm.
    std::vector<float> smoothed(num_bins, 0.0f);
    for (std::size_t f = 0; f < num_frames; ++f) {
        // 5-tap moving average with edge clamping.
        for (std::size_t k = 0; k < num_bins; ++k) {
            float sum = 0.0f;
            int count = 0;
            for (int offset = -2; offset <= 2; ++offset) {
                const long idx = static_cast<long>(k) + offset;
                if (idx >= 0 && idx < static_cast<long>(num_bins)) {
                    sum += magnitude[static_cast<std::size_t>(idx)][f];
                    count += 1;
                }
            }
            smoothed[k] = sum / static_cast<float>(count);
        }
        // Lerp original → smoothed by y_norm.
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude[k][f] = magnitude[k][f] * (1.0f - y_norm) + smoothed[k] * y_norm;
        }
    }
}
```

- [ ] **Step 5: Add the kSmear case to the Y switch**

Modify `cpp/src/dsp/spectral_modifier.cpp`. In `SpectralModifier::Apply`, update the Y switch to handle `kSmear`:

```cpp
    // ---- Y: morph mode dispatch (runs FIRST, matches Phase 3b/Python) ----
    switch (y_mode) {
        case YMode::kTilt:
            ApplyTilt(magnitude, y_norm);
            break;
        case YMode::kFormant:
            ApplyFormant(magnitude, y_norm);
            break;
        case YMode::kStretch:
            ApplyHarmonicStretch(magnitude, y_norm);
            break;
        case YMode::kSmear:
            ApplySmear(magnitude, y_norm);
            break;
    }
```

- [ ] **Step 6: Update the UI to show 4 Y options**

Modify `cpp/src/ui/any_wav_screen.cpp`. Update `YModeFromIndex` to handle index 3:

```cpp
fim::dsp::YMode YModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::YMode::kFormant;
        case 2:
            return fim::dsp::YMode::kStretch;
        case 3:
            return fim::dsp::YMode::kSmear;
        case 0:
        default:
            return fim::dsp::YMode::kTilt;
    }
}
```

And in `BuildFileSetPage`, update the `y_options` list:

```cpp
    // Y axis selector with real morph mode labels.
    const QStringList y_options{"Tilt", "Formant", "Stretch", "Smear"};
    y_selector_ = new AxisMorphSelector("Y axis", y_options, page);
    y_selector_->SetCurrentIndex(settings_->YMorph());
    layout->addWidget(y_selector_);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnYModeChanged);
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 60 tests pass (59 from Phase 3d + 1 new smear test).

- [ ] **Step 8: Run the app and confirm the UI shows 4 Y buttons**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Navigate to any-wav mode, drop a .wav file, verify the Y axis shows four buttons: Tilt / Formant / Stretch / Smear. Pick Smear, generate, listen — should sound like a diffuse/softened version of the input spectrum.

- [ ] **Step 9: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/spectral_modifier.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/spectral_modifier.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_spectral_modifier_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/spectral_modifier.h cpp/src/dsp/spectral_modifier.cpp \
        cpp/src/ui/any_wav_screen.cpp cpp/tests/dsp_spectral_modifier_test.cpp
git commit -m "feat(cpp): backport Smear as a fourth Y mode for single-wav"
```

---

## Task 2: Extract GenerateServiceBase

Pull the thread-pool dispatch, input/output path storage, generating flag, progress/finished/failed signals, and `Generate()` slot into a new abstract QObject base class. `SingleWavService` becomes a thin subclass that overrides `DoGenerate` to call `SingleWavGenerator::Generate`.

**Files:**
- Create: `cpp/src/app/services/generate_service_base.h`
- Create: `cpp/src/app/services/generate_service_base.cpp`
- Modify: `cpp/src/app/services/single_wav_service.h`
- Modify: `cpp/src/app/services/single_wav_service.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the base class header**

Create `cpp/src/app/services/generate_service_base.h`:

```cpp
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>

#include <QObject>
#include <QString>

namespace fim::app {

// Abstract base for wavetable generation services. Each mode (single-wav,
// Serum, three-wav) has its own concrete subclass that plugs in a specific
// DSP generator via the DoGenerate virtual. The base class owns the
// common scaffolding: input/output path storage, the "generating" atomic,
// the QThreadPool dispatch, and the three Qt signals that the UI connects
// to.
//
// Lifecycle:
//   1. Construct (owned by a QObject parent, typically the mode screen).
//   2. SetInputFile / SetOutputDirectory as the user provides values.
//   3. Subclass configures its own mode-specific options (e.g. Y/Z morph
//      selections via its own setters).
//   4. Generate() is called — the base class queues work on the global
//      thread pool and calls DoGenerate from the worker thread.
//   5. Signals fire for progress and completion on the GUI thread via
//      Qt's auto-queued cross-thread signals.
class GenerateServiceBase : public QObject {
    Q_OBJECT

public:
    explicit GenerateServiceBase(QObject* parent = nullptr);
    ~GenerateServiceBase() override = default;

    void SetInputFile(const QString& path);
    QString InputFile() const;

    void SetOutputDirectory(const QString& path);
    QString OutputDirectory() const;

    bool IsGenerating() const;

public slots:
    // Queue the generation work on QThreadPool::globalInstance(). Returns
    // immediately. Calling Generate() while already generating is a no-op.
    void Generate();

signals:
    // Emitted from the worker thread. Qt auto-queues cross-thread signal
    // emits onto the GUI thread when the receiver lives there.
    void progressChanged(int percent);
    void generationFinished();
    void generationFailed(const QString& error);

protected:
    // Type of the progress callback passed to DoGenerate. Subclasses call
    // this from inside their DSP work to report progress.
    using ProgressCallback = std::function<void(int percent)>;

    // Subclass implementation of the actual DSP work. Called from the
    // worker thread. Return true on success, false on error. The service
    // base class emits the appropriate finished/failed signals based on
    // the return value. The progress_cb should be invoked at intervals
    // with values in [0, 100].
    //
    // The subclass is responsible for reading any mode-specific state
    // it needs (e.g. cached options) at the start of DoGenerate. The
    // worker thread runs concurrently with the GUI thread so the subclass
    // should snapshot its state atomically if needed.
    virtual bool DoGenerate(const std::filesystem::path& input,
                            const std::filesystem::path& output,
                            const ProgressCallback& progress_cb) = 0;

private:
    QString input_file_;
    QString output_directory_;
    std::atomic<bool> generating_{false};
};

}  // namespace fim::app
```

- [ ] **Step 2: Write the base class implementation**

Create `cpp/src/app/services/generate_service_base.cpp`:

```cpp
#include "app/services/generate_service_base.h"

#include <filesystem>

#include <QThreadPool>

namespace fim::app {

GenerateServiceBase::GenerateServiceBase(QObject* parent) : QObject(parent) {}

void GenerateServiceBase::SetInputFile(const QString& path) {
    input_file_ = path;
}

QString GenerateServiceBase::InputFile() const {
    return input_file_;
}

void GenerateServiceBase::SetOutputDirectory(const QString& path) {
    output_directory_ = path;
}

QString GenerateServiceBase::OutputDirectory() const {
    return output_directory_;
}

bool GenerateServiceBase::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void GenerateServiceBase::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    const QString in_file = input_file_;
    const QString out_dir = output_directory_;

    QThreadPool::globalInstance()->start([this, in_file, out_dir]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path output_path(out_dir.toStdString());

        const bool ok = DoGenerate(input_path, output_path, [this](int percent) {
            // Qt auto-queues cross-thread signal emits onto the GUI thread.
            emit progressChanged(percent);
        });

        generating_.store(false, std::memory_order_release);

        if (ok) {
            emit generationFinished();
        } else {
            emit generationFailed(
                QString("Failed to generate wavetable bank from %1").arg(in_file));
        }
    });
}

}  // namespace fim::app
```

- [ ] **Step 3: Slim down SingleWavService header**

Rewrite `cpp/src/app/services/single_wav_service.h`:

```cpp
#pragma once

#include "app/services/generate_service_base.h"
#include "dsp/generate_options.h"

namespace fim::app {

// Single-wav wavetable service. Inherits the common scaffolding from
// GenerateServiceBase and adds Y/Z morph mode setters plus a cached
// GenerateOptions for the DSP generator.
class SingleWavService : public GenerateServiceBase {
    Q_OBJECT

public:
    explicit SingleWavService(QObject* parent = nullptr);
    ~SingleWavService() override = default;

    // Configure the morph modes used by the next Generate() call.
    void SetYMode(fim::dsp::YMode mode);
    void SetZMode(fim::dsp::ZMode mode);

protected:
    bool DoGenerate(const std::filesystem::path& input,
                    const std::filesystem::path& output,
                    const ProgressCallback& progress_cb) override;

private:
    fim::dsp::GenerateOptions options_;
};

}  // namespace fim::app
```

- [ ] **Step 4: Slim down SingleWavService implementation**

Rewrite `cpp/src/app/services/single_wav_service.cpp`:

```cpp
#include "app/services/single_wav_service.h"

#include "dsp/single_wav_generator.h"

namespace fim::app {

SingleWavService::SingleWavService(QObject* parent) : GenerateServiceBase(parent) {}

void SingleWavService::SetYMode(fim::dsp::YMode mode) {
    options_.y_mode = mode;
}

void SingleWavService::SetZMode(fim::dsp::ZMode mode) {
    options_.z_mode = mode;
}

bool SingleWavService::DoGenerate(const std::filesystem::path& input,
                                  const std::filesystem::path& output,
                                  const ProgressCallback& progress_cb) {
    // Snapshot options at the start — the UI thread may change them
    // while the worker runs, but we want the in-flight generation to use
    // a consistent set.
    const fim::dsp::GenerateOptions opts = options_;

    fim::dsp::SingleWavGenerator generator;
    return generator.Generate(input, output, opts, progress_cb);
}

}  // namespace fim::app
```

- [ ] **Step 5: Add the new sources to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/app/services/generate_service_base.cpp` to the `qt_add_executable(fim-config-tool ...)` block. It should come before `single_wav_service.cpp`:

```cmake
    src/app/services/generate_service_base.cpp
    src/app/services/single_wav_service.cpp
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 60 tests still pass. No behavior change — the refactor is pure code motion. If anything fails, the issue is mechanical (missing header, signal still declared in the derived class, etc.).

- [ ] **Step 7: Manual regression check**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Walk through the any-wav flow: drop a file, click Generate, watch the progress bar fill, listen to the output. Any-wav mode should be bit-identical to before — this is a pure refactor.

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/generate_service_base.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/generate_service_base.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/single_wav_service.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/single_wav_service.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/generate_service_base.h \
        cpp/src/app/services/generate_service_base.cpp \
        cpp/src/app/services/single_wav_service.h \
        cpp/src/app/services/single_wav_service.cpp \
        cpp/CMakeLists.txt
git commit -m "refactor(cpp): extract GenerateServiceBase from SingleWavService"
```

---

## Task 3: Extract ModeScreenBase

Carve out the five-state `QStackedWidget` machine, the common header (back button + title), the progress bar, the done-message timer, and the embedded preview controls panel into a `ModeScreenBase` QWidget. `AnyWavScreen` inherits and provides only the any-wav-specific content: the file drop widget, the Y/Z axis selectors, and the service instance.

This is the biggest refactor in Phase 4a. The goal is to make `SerumWavScreen` a natural sibling: same base class, same hooks, different content.

**Files:**
- Create: `cpp/src/ui/mode_screen_base.h`
- Create: `cpp/src/ui/mode_screen_base.cpp`
- Modify: `cpp/src/ui/any_wav_screen.h`
- Modify: `cpp/src/ui/any_wav_screen.cpp`
- Modify: `cpp/CMakeLists.txt`

**Important design constraint before the code:** the base class can't call pure virtual hooks (`Service`, `ModeTitle`, etc.) from its constructor — that would dispatch to the base's still-unset vtable and trigger a pure-virtual call. The solution is a protected `FinishInit()` method that the subclass calls from its own constructor body, AFTER its own members (especially its service instance) are fully constructed. The base class's constructor is deliberately minimal.

- [ ] **Step 1: Write the base class header**

Create `cpp/src/ui/mode_screen_base.h`:

```cpp
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::app {
class GenerateServiceBase;
class Settings;
}  // namespace fim::app

namespace fim::ui {

class CustomProgressBar;
class PreviewControlsWidget;

// Abstract base for mode screens (AnyWavScreen, SerumWavScreen, etc.).
// Holds the 5-state QStackedWidget machine, the back-button header, the
// progress bar used during generation, and the embedded preview controls
// that play the generated output. Subclasses provide the mode-specific
// content for the "empty" and "file-set" pages, plus a reference to a
// concrete GenerateServiceBase subclass.
//
// State transitions (same for all modes):
//   kEmpty           ──── file dropped ────► kFileSet
//   kFileSet         ──── Generate clicked ─► kGenerating
//   kGenerating      ──── service done ─────► kDoneMessage
//   kDoneMessage     ──── after ~800ms ─────► kDonePreviewAvailable
//   kDoneMessage,
//   kDonePreviewAvailable
//                    ──── Generate clicked ─► kGenerating
class ModeScreenBase : public QWidget {
    Q_OBJECT

public:
    enum class State {
        kEmpty,
        kFileSet,
        kGenerating,
        kDoneMessage,
        kDonePreviewAvailable,
    };

    ModeScreenBase(fim::engine::RealtimeAudioEngine* engine,
                   fim::app::Settings* settings, QWidget* parent = nullptr);
    ~ModeScreenBase() override = default;

    // Resets the screen back to the empty state. Called when the user
    // navigates away and returns. Also calls the subclass OnResetHook so
    // mode-specific state (file path, selector positions) can reset too.
    void Reset();

signals:
    void backRequested();

protected:
    // ---- Hooks subclasses must implement ----

    // The title shown at the top of every state page (e.g. "Use any .wav
    // file to create your wavetable bank").
    virtual QString ModeTitle() const = 0;

    // Builds the mode-specific content for the empty page (below the
    // title). Typically a file drop widget. The base class owns the outer
    // layout and the back button; the subclass returns a single QWidget
    // that it builds and populates.
    virtual QWidget* BuildEmptyPageContent(QWidget* parent) = 0;

    // Builds the mode-specific content for the file-set page (below the
    // title and filename row). Typically the axis selectors and the
    // Generate button.
    virtual QWidget* BuildFileSetPageContent(QWidget* parent) = 0;

    // Returns a pointer to the subclass's service instance. The base
    // class wires the service's signals (progressChanged /
    // generationFinished / generationFailed) to its own slots. Called
    // once during base-class construction.
    virtual fim::app::GenerateServiceBase* Service() = 0;

    // Called from OnClearClicked when the user clicks the Clear button
    // in the file-set state. Subclasses override to clear their cached
    // input file path and reset the filename label to "(no file)".
    virtual void OnClearHook() = 0;

    // Called from Reset(). Subclasses override to re-sync mode-specific
    // UI state (e.g. axis selector positions) from Settings.
    virtual void OnResetHook() = 0;

    // Returns the directory where generated output lives. Used by the
    // base class to call engine_->LoadBank() after generation completes.
    virtual QString OutputDirForPreview() const = 0;

    // Subclass MUST call this from its own constructor body (not
    // initializer list) after its members — especially its service
    // instance — are fully constructed. Builds the state pages using
    // the subclass's virtual content hooks, wires the service signals,
    // and transitions to the initial kEmpty state.
    void FinishInit();

    // ---- Helpers exposed to subclasses ----

    // Subclasses call this when their mode-specific file drop widget
    // emits its "file chosen" signal. Updates the filename label and
    // transitions the state machine to kFileSet.
    void OnFileChosen(const QString& path);

    // Subclasses call this when their Generate button is clicked.
    // Transitions to kGenerating and calls service->Generate().
    void OnGenerateClicked();

    // Protected accessors for subclass use.
    fim::engine::RealtimeAudioEngine* engine() const { return engine_; }
    fim::app::Settings* settings() const { return settings_; }
    QLabel* filename_label() const { return filename_label_; }
    void set_current_file(const QString& path) { current_file_ = path; }
    const QString& current_file() const { return current_file_; }

private slots:
    void OnClearClicked();
    void OnExportClicked();
    void OnProgressChanged(int percent);
    void OnGenerationFinished();

private:
    void SetState(State state);
    QWidget* BuildEmptyPage();
    QWidget* BuildFileSetPage();
    QWidget* BuildGeneratingPage();
    QWidget* BuildDonePage(bool with_preview);
    QPushButton* MakeBackButton(QWidget* parent);

    fim::engine::RealtimeAudioEngine* engine_;  // non-owning
    fim::app::Settings* settings_;              // non-owning
    QString current_file_;

    QStackedWidget* stack_ = nullptr;
    int empty_page_index_ = -1;
    int file_set_page_index_ = -1;
    int generating_page_index_ = -1;
    int done_message_page_index_ = -1;
    int done_preview_page_index_ = -1;

    QLabel* filename_label_ = nullptr;
    CustomProgressBar* progress_bar_ = nullptr;
    PreviewControlsWidget* preview_controls_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the base class implementation**

Create `cpp/src/ui/mode_screen_base.cpp`:

```cpp
#include "ui/mode_screen_base.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "app/services/generate_service_base.h"
#include "app/settings.h"
#include "engine/realtime_audio_engine.h"
#include "ui/widgets/custom_progress_bar.h"
#include "ui/widgets/preview_controls_widget.h"

namespace fim::ui {

namespace {

constexpr int kDoneMessageHoldMs = 800;
constexpr int kNumPages = 8;

}  // namespace

ModeScreenBase::ModeScreenBase(fim::engine::RealtimeAudioEngine* engine,
                               fim::app::Settings* settings, QWidget* parent)
    : QWidget(parent), engine_(engine), settings_(settings) {
    // Intentionally minimal — can't call virtual hooks (ModeTitle,
    // BuildEmptyPageContent, Service, etc.) from the base constructor
    // because the vtable hasn't been set up to dispatch to the derived
    // class yet. Subclass calls FinishInit() from its own constructor
    // body after its members are fully constructed.
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(32, 32, 32, 32);
    root_layout->setSpacing(16);

    stack_ = new QStackedWidget(this);
    root_layout->addWidget(stack_);
}

void ModeScreenBase::FinishInit() {
    empty_page_index_ = stack_->addWidget(BuildEmptyPage());
    file_set_page_index_ = stack_->addWidget(BuildFileSetPage());
    generating_page_index_ = stack_->addWidget(BuildGeneratingPage());
    done_message_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/false));
    done_preview_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/true));

    auto* service = Service();
    connect(service, &fim::app::GenerateServiceBase::progressChanged, this,
            &ModeScreenBase::OnProgressChanged);
    connect(service, &fim::app::GenerateServiceBase::generationFinished, this,
            &ModeScreenBase::OnGenerationFinished);
    connect(service, &fim::app::GenerateServiceBase::generationFailed, this,
            [this](const QString& error) {
                QMessageBox::warning(this, "Generation failed", error);
                SetState(current_file_.isEmpty() ? State::kEmpty : State::kFileSet);
            });

    SetState(State::kEmpty);
}

void ModeScreenBase::Reset() {
    current_file_.clear();
    OnResetHook();
    SetState(State::kEmpty);
}

void ModeScreenBase::SetState(State state) {
    switch (state) {
        case State::kEmpty:
            stack_->setCurrentIndex(empty_page_index_);
            break;
        case State::kFileSet:
            stack_->setCurrentIndex(file_set_page_index_);
            break;
        case State::kGenerating:
            stack_->setCurrentIndex(generating_page_index_);
            progress_bar_->SetProgress(0);
            break;
        case State::kDoneMessage:
            stack_->setCurrentIndex(done_message_page_index_);
            QTimer::singleShot(kDoneMessageHoldMs, this,
                               [this]() { SetState(State::kDonePreviewAvailable); });
            break;
        case State::kDonePreviewAvailable:
            stack_->setCurrentIndex(done_preview_page_index_);
            engine_->LoadBank(OutputDirForPreview().toStdString());
            break;
    }
}

QPushButton* ModeScreenBase::MakeBackButton(QWidget* parent) {
    auto* button = new QPushButton("← Back", parent);
    button->setObjectName("backButton");
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QWidget* ModeScreenBase::BuildEmptyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

    layout->addWidget(BuildEmptyPageContent(page));
    layout->addStretch();
    return page;
}

QWidget* ModeScreenBase::BuildFileSetPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

    // Filename row — shared between all modes.
    auto* file_row = new QHBoxLayout();
    filename_label_ = new QLabel("(no file)", page);
    filename_label_->setObjectName("anyWavFilename");
    auto* clear_button = new QPushButton("Clear", page);
    clear_button->setObjectName("clearButton");
    file_row->addWidget(filename_label_);
    file_row->addStretch();
    file_row->addWidget(clear_button);
    layout->addLayout(file_row);
    connect(clear_button, &QPushButton::clicked, this, &ModeScreenBase::OnClearClicked);

    layout->addWidget(BuildFileSetPageContent(page));
    layout->addStretch();
    return page;
}

QWidget* ModeScreenBase::BuildGeneratingPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

    progress_bar_ = new CustomProgressBar(page);
    layout->addWidget(progress_bar_);

    layout->addStretch();
    return page;
}

QWidget* ModeScreenBase::BuildDonePage(bool with_preview) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

    if (!with_preview) {
        auto* done_label = new QLabel("Done!", page);
        done_label->setObjectName("anyWavDoneMessage");
        layout->addWidget(done_label);
    } else {
        auto* button_row = new QHBoxLayout();
        auto* generate_button = new QPushButton("Generate wavetable bank", page);
        generate_button->setObjectName("generateButton");
        auto* export_button = new QPushButton("Export wavetable bank", page);
        export_button->setObjectName("exportButton");
        button_row->addWidget(generate_button);
        button_row->addWidget(export_button);
        button_row->addStretch();
        layout->addLayout(button_row);
        connect(generate_button, &QPushButton::clicked, this,
                &ModeScreenBase::OnGenerateClicked);
        connect(export_button, &QPushButton::clicked, this,
                &ModeScreenBase::OnExportClicked);

        preview_controls_ = new PreviewControlsWidget(engine_, page);
        layout->addWidget(preview_controls_);
    }

    layout->addStretch();
    return page;
}

void ModeScreenBase::OnFileChosen(const QString& path) {
    current_file_ = path;
    if (filename_label_) {
        filename_label_->setText(QFileInfo(path).fileName());
    }
    Service()->SetInputFile(path);
    SetState(State::kFileSet);
}

void ModeScreenBase::OnClearClicked() {
    current_file_.clear();
    OnClearHook();
    SetState(State::kEmpty);
}

void ModeScreenBase::OnGenerateClicked() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
        if (preview_controls_) {
            preview_controls_->RefreshPlayButton();
        }
    }
    SetState(State::kGenerating);
    Service()->Generate();
}

void ModeScreenBase::OnExportClicked() {
    const QString dest = QFileDialog::getExistingDirectory(
        this, "Export bank to…", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dest.isEmpty()) {
        return;
    }
    const QString src_dir = OutputDirForPreview();
    for (int i = 1; i <= kNumPages; ++i) {
        const QString src = QString("%1/%2.wav").arg(src_dir).arg(i);
        const QString dst = QString("%1/%2.wav").arg(dest).arg(i);
        QFile::copy(src, dst);
    }
}

void ModeScreenBase::OnProgressChanged(int percent) {
    if (progress_bar_) {
        progress_bar_->SetProgress(percent);
    }
}

void ModeScreenBase::OnGenerationFinished() {
    SetState(State::kDoneMessage);
}

}  // namespace fim::ui
```

- [ ] **Step 3: Slim down AnyWavScreen header**

Rewrite `cpp/src/ui/any_wav_screen.h`:

```cpp
#pragma once

#include <QString>

#include "ui/mode_screen_base.h"

namespace fim::app {
class SingleWavService;
}

namespace fim::ui {

class AxisMorphSelector;

// The "any wav" mode screen. Uses a SingleWavService to run the real
// single-wav DSP pipeline. Provides the mode-specific content (file drop
// widget, Y/Z axis selectors) via the ModeScreenBase hooks; the base
// class handles the 5-state machine, back button, progress bar, preview
// controls, and export.
class AnyWavScreen : public ModeScreenBase {
    Q_OBJECT

public:
    AnyWavScreen(fim::engine::RealtimeAudioEngine* engine,
                 fim::app::Settings* settings, QWidget* parent = nullptr);

protected:
    QString ModeTitle() const override;
    QWidget* BuildEmptyPageContent(QWidget* parent) override;
    QWidget* BuildFileSetPageContent(QWidget* parent) override;
    fim::app::GenerateServiceBase* Service() override;
    void OnClearHook() override;
    void OnResetHook() override;
    QString OutputDirForPreview() const override;

private slots:
    void OnYModeChanged(int index);
    void OnZModeChanged(int index);

private:
    fim::app::SingleWavService* service_ = nullptr;
    AxisMorphSelector* y_selector_ = nullptr;
    AxisMorphSelector* z_selector_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 4: Slim down AnyWavScreen implementation**

Rewrite `cpp/src/ui/any_wav_screen.cpp`:

```cpp
#include "ui/any_wav_screen.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include "app/services/single_wav_service.h"
#include "app/settings.h"
#include "dsp/spectral_modifier.h"
#include "ui/widgets/axis_morph_selector.h"
#include "ui/widgets/file_drop_widget.h"

namespace fim::ui {

namespace {

QString SingleWavOutputDir() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("audio_resynth");
}

fim::dsp::YMode YModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::YMode::kFormant;
        case 2:
            return fim::dsp::YMode::kStretch;
        case 3:
            return fim::dsp::YMode::kSmear;
        case 0:
        default:
            return fim::dsp::YMode::kTilt;
    }
}

fim::dsp::ZMode ZModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::ZMode::kDisperse;
        case 2:
            return fim::dsp::ZMode::kCrush;
        case 0:
        default:
            return fim::dsp::ZMode::kRandom;
    }
}

}  // namespace

AnyWavScreen::AnyWavScreen(fim::engine::RealtimeAudioEngine* engine,
                           fim::app::Settings* settings, QWidget* parent)
    : ModeScreenBase(engine, settings, parent) {
    setObjectName("anyWavScreen");

    // Construct the service BEFORE calling FinishInit — the base class's
    // FinishInit queries Service() and wires its signals.
    service_ = new fim::app::SingleWavService(this);
    service_->SetOutputDirectory(SingleWavOutputDir());
    service_->SetYMode(YModeFromIndex(settings->YMorph()));
    service_->SetZMode(ZModeFromIndex(settings->ZMorph()));

    FinishInit();
}

QString AnyWavScreen::ModeTitle() const {
    return "Use any .wav file to create your wavetable bank";
}

QWidget* AnyWavScreen::BuildEmptyPageContent(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("anyWavInnerCard");
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(32, 32, 32, 32);

    auto* drop = new FileDropWidget(card);
    connect(drop, &FileDropWidget::fileDropped, this,
            [this](const QString& path) { OnFileChosen(path); });
    card_layout->addWidget(drop);

    return card;
}

QWidget* AnyWavScreen::BuildFileSetPageContent(QWidget* parent) {
    auto* content = new QWidget(parent);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // X axis (fixed descriptor)
    auto* x_label = new QLabel("X axis", content);
    x_label->setObjectName("anyWavAxisLabel");
    layout->addWidget(x_label);
    auto* x_descriptor = new QLabel("Scans the wave", content);
    x_descriptor->setObjectName("anyWavAxisDescriptor");
    layout->addWidget(x_descriptor);

    // Y axis selector with 4 morph modes.
    const QStringList y_options{"Tilt", "Formant", "Stretch", "Smear"};
    y_selector_ = new AxisMorphSelector("Y axis", y_options, content);
    y_selector_->SetCurrentIndex(settings()->YMorph());
    layout->addWidget(y_selector_);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnYModeChanged);

    // Z axis selector with 3 morph modes.
    const QStringList z_options{"Random", "Disperse", "Crush"};
    z_selector_ = new AxisMorphSelector("Z axis", z_options, content);
    z_selector_->SetCurrentIndex(settings()->ZMorph());
    layout->addWidget(z_selector_);
    connect(z_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnZModeChanged);

    auto* generate_row = new QHBoxLayout();
    auto* generate_button = new QPushButton("Generate wavetable bank", content);
    generate_button->setObjectName("generateButton");
    generate_row->addStretch();
    generate_row->addWidget(generate_button);
    layout->addLayout(generate_row);
    connect(generate_button, &QPushButton::clicked, this,
            [this]() { OnGenerateClicked(); });

    return content;
}

fim::app::GenerateServiceBase* AnyWavScreen::Service() {
    return service_;
}

void AnyWavScreen::OnClearHook() {
    if (filename_label()) {
        filename_label()->setText("(no file)");
    }
}

void AnyWavScreen::OnResetHook() {
    if (y_selector_ != nullptr) {
        y_selector_->SetCurrentIndex(settings()->YMorph());
    }
    if (z_selector_ != nullptr) {
        z_selector_->SetCurrentIndex(settings()->ZMorph());
    }
}

QString AnyWavScreen::OutputDirForPreview() const {
    return SingleWavOutputDir();
}

void AnyWavScreen::OnYModeChanged(int index) {
    service_->SetYMode(YModeFromIndex(index));
    settings()->SetYMorph(index);
}

void AnyWavScreen::OnZModeChanged(int index) {
    service_->SetZMode(ZModeFromIndex(index));
    settings()->SetZMorph(index);
}

}  // namespace fim::ui
```

- [ ] **Step 5: Add the new sources to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/mode_screen_base.cpp` to the `qt_add_executable(fim-config-tool ...)` block. It should come before `any_wav_screen.cpp`:

```cmake
    src/ui/mode_screen_base.cpp
    src/ui/any_wav_screen.cpp
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 60 tests pass. The refactor is pure code motion — no behavior change.

- [ ] **Step 7: Manual regression check — any-wav must work end-to-end**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Walk through the full any-wav flow:
1. Launcher → "Choose" on Any wav card
2. Drop a .wav file
3. Verify filename shown, Y/Z selectors present with 4 Y buttons and 3 Z buttons
4. Pick Formant on Y, Crush on Z
5. Click Generate — progress bar fills
6. "Done!" then preview controls appear
7. Click Play — audible output
8. Drag Z slider — different pages
9. Click Export → pick a directory → verify 8 files appear
10. Click ← Back → return to launcher → re-enter any-wav → verify selected modes are remembered

If any step fails, the refactor has a bug. Investigate before proceeding to Task 4.

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/mode_screen_base.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/mode_screen_base.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/mode_screen_base.h cpp/src/ui/mode_screen_base.cpp \
        cpp/src/ui/any_wav_screen.h cpp/src/ui/any_wav_screen.cpp \
        cpp/CMakeLists.txt
git commit -m "refactor(cpp): extract ModeScreenBase from AnyWavScreen"
```

---

## Task 4: SerumLoader

A `dr_wav`-backed loader for Serum-format wavetable files. Splits the input into `2048`-sample frames, validates the sample count is a multiple of 2048, removes DC per frame. Does NOT interpolate to 8 frames — that's `SerumMorpher`'s job and happens in the frequency domain.

**Files:**
- Create: `cpp/src/dsp/serum_loader.h`
- Create: `cpp/src/dsp/serum_loader.cpp`
- Create: `cpp/tests/dsp_serum_loader_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_serum_loader_test.cpp`:

```cpp
#include "dsp/serum_loader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "dr_wav.h"

using Catch::Matchers::WithinAbs;

namespace {

// Write a Serum-like WAV file: `num_frames` frames of 2048 samples each.
// Each frame is a sine wave at a different frequency so we can
// distinguish frames in tests.
std::filesystem::path WriteTestSerumWav(const std::string& basename,
                                        std::size_t num_frames) {
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    constexpr std::size_t kFrameSize = 2048;
    constexpr float kPi = 3.14159265358979323846f;
    std::vector<float> samples(num_frames * kFrameSize);
    for (std::size_t f = 0; f < num_frames; ++f) {
        const float freq_mult = 1.0f + static_cast<float>(f);
        for (std::size_t i = 0; i < kFrameSize; ++i) {
            const float phase = 2.0f * kPi * static_cast<float>(i) / kFrameSize;
            samples[f * kFrameSize + i] = std::sin(phase * freq_mult);
        }
    }

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = 44100;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    return path;
}

// Same as above but with a non-multiple-of-2048 sample count to test
// invalid-length handling.
std::filesystem::path WriteTestInvalidWav(const std::string& basename) {
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    // 3000 samples — not a multiple of 2048.
    std::vector<float> samples(3000, 0.5f);

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = 44100;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    return path;
}

class SerumLoaderFixture {
public:
    ~SerumLoaderFixture() {
        for (const auto& p : to_cleanup_) {
            std::filesystem::remove(p);
        }
    }
    void Track(const std::filesystem::path& p) { to_cleanup_.push_back(p); }

private:
    std::vector<std::filesystem::path> to_cleanup_;
};

}  // namespace

TEST_CASE_METHOD(SerumLoaderFixture, "SerumLoader loads a valid 4-frame file",
                 "[dsp][serum_loader]") {
    const auto path = WriteTestSerumWav("fim_serum_4frames", 4);
    Track(path);

    const auto loaded = fim::dsp::SerumLoader::Load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->frames.size() == 4);
    for (const auto& frame : loaded->frames) {
        REQUIRE(frame.size() == 2048);
    }
    REQUIRE(loaded->source_sample_rate == 44100);
}

TEST_CASE_METHOD(SerumLoaderFixture,
                 "SerumLoader returns nullopt for invalid sample count",
                 "[dsp][serum_loader]") {
    const auto path = WriteTestInvalidWav("fim_serum_invalid");
    Track(path);

    const auto loaded = fim::dsp::SerumLoader::Load(path);
    REQUIRE_FALSE(loaded.has_value());
}

TEST_CASE_METHOD(SerumLoaderFixture, "SerumLoader removes DC per frame",
                 "[dsp][serum_loader]") {
    const auto path = WriteTestSerumWav("fim_serum_dc", 2);
    Track(path);

    const auto loaded = fim::dsp::SerumLoader::Load(path);
    REQUIRE(loaded.has_value());
    for (const auto& frame : loaded->frames) {
        float sum = 0.0f;
        for (float s : frame) {
            sum += s;
        }
        REQUIRE_THAT(sum / static_cast<float>(frame.size()), WithinAbs(0.0f, 1e-5));
    }
}

TEST_CASE("SerumLoader returns nullopt for a nonexistent file",
          "[dsp][serum_loader]") {
    const auto result =
        fim::dsp::SerumLoader::Load("/tmp/this_serum_file_does_not_exist_xyz.wav");
    REQUIRE_FALSE(result.has_value());
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`. Add `dsp_serum_loader_test.cpp` and `../src/dsp/serum_loader.cpp`:

```cmake
    dsp_post_effects_test.cpp
    dsp_serum_loader_test.cpp
    ../src/engine/single_header_impls.cpp
```

And in the implementation sources:

```cmake
    ../src/dsp/post_effects.cpp
    ../src/dsp/serum_loader.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/serum_loader.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/serum_loader.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace fim::dsp {

// Result of loading a Serum-format wavetable file. Each frame is exactly
// 2048 samples (Serum's fixed frame size), mono, float in [-1, 1] with
// per-frame DC removed.
struct SerumFrames {
    std::vector<std::vector<float>> frames;  // [frame_index][sample_index]
    std::uint32_t source_sample_rate = 0;
};

// Loader for Serum-format wavetable files. Serum stores a wavetable as
// a sequence of `N × 2048` single-cycle waveforms concatenated in a WAV
// file. There's no explicit frame count marker — the loader infers it
// from the total sample count.
class SerumLoader {
public:
    // Load and split a Serum WAV file into 2048-sample frames. Returns
    // std::nullopt if the file can't be loaded or its sample count isn't
    // a multiple of 2048. Stereo files are mixed to mono before framing.
    // DC offset is removed from each frame individually.
    static std::optional<SerumFrames> Load(const std::filesystem::path& path);
};

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/serum_loader.cpp`:

```cpp
#include "dsp/serum_loader.h"

#include <cstddef>

#include "dsp/wav_loader.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kSerumFrameSize = 2048;

}  // namespace

std::optional<SerumFrames> SerumLoader::Load(const std::filesystem::path& path) {
    // Use the existing WAV loader to handle dr_wav invocation, stereo
    // mixdown, and float conversion. Don't normalize — Serum files are
    // already normalized by convention, and normalizing across the whole
    // file would be wrong if some frames are quieter than others.
    auto loaded = LoadWav(path, /*normalize=*/false);
    if (!loaded.has_value()) {
        return std::nullopt;
    }
    if (loaded->samples.empty()) {
        return std::nullopt;
    }
    if (loaded->samples.size() % kSerumFrameSize != 0) {
        return std::nullopt;
    }

    const std::size_t num_frames = loaded->samples.size() / kSerumFrameSize;
    if (num_frames == 0) {
        return std::nullopt;
    }

    SerumFrames result;
    result.source_sample_rate = loaded->sample_rate;
    result.frames.resize(num_frames);
    for (std::size_t f = 0; f < num_frames; ++f) {
        result.frames[f].resize(kSerumFrameSize);
        const std::size_t start = f * kSerumFrameSize;
        // Copy.
        for (std::size_t i = 0; i < kSerumFrameSize; ++i) {
            result.frames[f][i] = loaded->samples[start + i];
        }
        // Remove DC per frame.
        float sum = 0.0f;
        for (float s : result.frames[f]) {
            sum += s;
        }
        const float dc = sum / static_cast<float>(kSerumFrameSize);
        for (float& s : result.frames[f]) {
            s -= dc;
        }
    }

    return result;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/dsp/serum_loader.cpp` to the `qt_add_executable(fim-config-tool ...)` block:

```cmake
    src/dsp/post_effects.cpp
    src/dsp/serum_loader.cpp
    src/dsp/single_wav_generator.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 64 tests pass (60 from Task 1/2/3 + 4 new serum_loader tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/serum_loader.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/serum_loader.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_serum_loader_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/serum_loader.h cpp/src/dsp/serum_loader.cpp \
        cpp/tests/dsp_serum_loader_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add SerumLoader for Serum-format wavetable files"
```

---

## Task 5: SerumMorpher

The heart of Serum mode DSP. `SerumMorpher` holds a `RealFft(2048)` for forward + inverse transforms, provides the `SerumFftCache` struct, implements the frequency-domain frame resampling (interpolates N source caches to 8 output caches), and implements the 4 Vital-derived morph operations (formant, phase, smear, stretch).

**Files:**
- Create: `cpp/src/dsp/serum_morpher.h`
- Create: `cpp/src/dsp/serum_morpher.cpp`
- Create: `cpp/tests/dsp_serum_morpher_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_serum_morpher_test.cpp`:

```cpp
#include "dsp/serum_morpher.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

constexpr std::size_t kFftSize = 2048;
constexpr std::size_t kNumBins = kFftSize / 2 + 1;
constexpr float kPi = std::numbers::pi_v<float>;

// Build a frame with a pure cosine at the specified bin. Peak magnitude
// at bin k, zero elsewhere.
std::vector<float> MakeCosineFrame(std::size_t bin) {
    std::vector<float> frame(kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        frame[i] = std::cos(2.0f * kPi * static_cast<float>(bin) * i / kFftSize);
    }
    return frame;
}

}  // namespace

TEST_CASE("SerumMorpher cache contains correct amplitudes for a cosine",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    const auto frame = MakeCosineFrame(4);
    const auto cache = morpher.ComputeCache(frame);

    REQUIRE(cache.amplitudes.size() == kNumBins);
    REQUIRE(cache.phases.size() == kNumBins);
    // Peak at bin 4, ~N/2 magnitude.
    const float expected_peak = static_cast<float>(kFftSize) / 2.0f;
    REQUIRE_THAT(cache.amplitudes[4], WithinAbs(expected_peak, 1.0f));
    // Other bins near zero.
    REQUIRE_THAT(cache.amplitudes[0], WithinAbs(0.0f, 1.0f));
    REQUIRE_THAT(cache.amplitudes[10], WithinAbs(0.0f, 1.0f));
}

TEST_CASE("SerumMorpher interpolates N source caches to exactly 8 output caches",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    std::vector<fim::dsp::SerumFftCache> sources;
    // 4 source frames with peaks at bins 2, 4, 6, 8 — interpolating to 8
    // output slots should produce caches whose peaks lerp between these.
    for (int f = 0; f < 4; ++f) {
        const std::size_t bin = 2 + f * 2;
        sources.push_back(morpher.ComputeCache(MakeCosineFrame(bin)));
    }

    const auto interpolated = morpher.InterpolateCaches(sources, 8);
    REQUIRE(interpolated.size() == 8);
    for (const auto& cache : interpolated) {
        REQUIRE(cache.amplitudes.size() == kNumBins);
    }
    // First output slot should match source 0 exactly (bin 2 peak).
    float peak_mag = 0.0f;
    std::size_t peak_bin = 0;
    for (std::size_t k = 0; k < kNumBins; ++k) {
        if (interpolated[0].amplitudes[k] > peak_mag) {
            peak_mag = interpolated[0].amplitudes[k];
            peak_bin = k;
        }
    }
    REQUIRE(peak_bin == 2);
    // Last output slot should match source 3 exactly (bin 8 peak).
    peak_mag = 0.0f;
    peak_bin = 0;
    for (std::size_t k = 0; k < kNumBins; ++k) {
        if (interpolated[7].amplitudes[k] > peak_mag) {
            peak_mag = interpolated[7].amplitudes[k];
            peak_bin = k;
        }
    }
    REQUIRE(peak_bin == 8);
}

TEST_CASE("SerumMorpher formant scale at amount=0.5 is approximately identity",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    const auto frame = MakeCosineFrame(16);
    const auto cache = morpher.ComputeCache(frame);
    const auto result =
        morpher.Apply(cache, fim::dsp::SerumMode::kFormant, /*amount=*/0.5f);

    REQUIRE(result.size() == kNumBins);
    // Peak should still be at bin 16.
    float peak_mag = 0.0f;
    std::size_t peak_bin = 0;
    for (std::size_t k = 0; k < kNumBins; ++k) {
        const float m = std::abs(result[k]);
        if (m > peak_mag) {
            peak_mag = m;
            peak_bin = k;
        }
    }
    REQUIRE(peak_bin == 16);
}

TEST_CASE("SerumMorpher formant scale at amount=1 shifts peak up",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    const auto frame = MakeCosineFrame(16);
    const auto cache = morpher.ComputeCache(frame);
    const auto result =
        morpher.Apply(cache, fim::dsp::SerumMode::kFormant, /*amount=*/1.0f);

    // amount=1 → scale=4 → bin 16 peak should move to ~bin 61 (15*4+1).
    float peak_mag = 0.0f;
    std::size_t peak_bin = 0;
    for (std::size_t k = 0; k < kNumBins; ++k) {
        const float m = std::abs(result[k]);
        if (m > peak_mag) {
            peak_mag = m;
            peak_bin = k;
        }
    }
    REQUIRE(peak_bin > 16);
}

TEST_CASE("SerumMorpher phase disperse preserves DC and Nyquist amplitudes",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    std::vector<float> frame(kFftSize, 0.0f);
    frame[0] = 1.0f;  // impulse → flat spectrum magnitude-wise
    const auto cache = morpher.ComputeCache(frame);
    const float dc_before = cache.amplitudes[0];
    const float nyq_before = cache.amplitudes[kNumBins - 1];

    const auto result =
        morpher.Apply(cache, fim::dsp::SerumMode::kPhase, /*amount=*/1.0f);

    REQUIRE_THAT(std::abs(result[0]), WithinAbs(dc_before, 1e-4));
    REQUIRE_THAT(std::abs(result[kNumBins - 1]), WithinAbs(nyq_before, 1e-4));
}

TEST_CASE("SerumMorpher smear reduces magnitude variance",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    // Spiky spectrum: impulse in time → flat magnitudes, then add a few
    // extra tones so there's clear variance.
    const auto frame = MakeCosineFrame(16);
    const auto cache = morpher.ComputeCache(frame);

    const auto result =
        morpher.Apply(cache, fim::dsp::SerumMode::kSmear, /*amount=*/1.0f);

    // After full smear, the peak at bin 16 should have leaked into
    // neighbors — so max_mag < original max_mag.
    float original_peak = 0.0f;
    for (float a : cache.amplitudes) {
        original_peak = std::max(original_peak, a);
    }
    float smeared_peak = 0.0f;
    for (const auto& c : result) {
        smeared_peak = std::max(smeared_peak, std::abs(c));
    }
    REQUIRE(smeared_peak < original_peak);
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`:

```cmake
    dsp_serum_loader_test.cpp
    dsp_serum_morpher_test.cpp
    ../src/engine/single_header_impls.cpp
```

And in the implementation sources:

```cmake
    ../src/dsp/serum_loader.cpp
    ../src/dsp/serum_morpher.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/serum_morpher.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/serum_morpher.h`:

```cpp
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/real_fft.h"

namespace fim::dsp {

// Morph types for Serum mode. Mirrored from Python's MorphType enum,
// clean-room implementations of Vital's spectral morph operations.
enum class SerumMode {
    kFormant,  // Bidirectional formant shift (0.25× down to 4× up)
    kPhase,    // Frequency-dependent phase rotation (comb-filter-like)
    kSmear,    // Running-average amplitude smoothing
    kStretch,  // Octave-based inharmonic stretching (1× to 12×)
};

// Pre-computed FFT data for a single 2048-sample frame. Holds both the
// amplitude and phase arrays plus the normalized real/imag components
// (cos(phase) / sin(phase)) that several morph ops consume directly.
struct SerumFftCache {
    std::vector<float> amplitudes;       // length 1025
    std::vector<float> phases;           // length 1025
    std::vector<float> normalized_real;  // length 1025, == cos(phases)
    std::vector<float> normalized_imag;  // length 1025, == sin(phases)
};

// Applies Serum-mode spectral morph operations to frame FFT data.
// Holds a RealFft(2048) for efficient cache computation and reuse across
// many frames. Thread-unsafe; each worker thread should own its own
// instance.
class SerumMorpher {
public:
    SerumMorpher();
    ~SerumMorpher();

    SerumMorpher(const SerumMorpher&) = delete;
    SerumMorpher& operator=(const SerumMorpher&) = delete;

    // Compute an FFT cache from a single 2048-sample frame. The frame
    // must have exactly 2048 samples.
    SerumFftCache ComputeCache(const std::vector<float>& frame);

    // Interpolate N source FFT caches to `output_count` caches via
    // frequency-domain linear interpolation. Magnitudes are linearly
    // lerped between the two nearest source caches; phases are taken
    // from the NEAREST source cache (no phase unwrapping).
    //
    // If output_count == N, returns copies of the sources.
    // If output_count > N, upsamples via linear interp.
    // If output_count < N, downsamples via linear interp at the lerp
    //   positions — no anti-aliasing filter in the frame dimension, but
    //   this is fine for smoothly-varying source wavetables.
    std::vector<SerumFftCache> InterpolateCaches(
        const std::vector<SerumFftCache>& sources, std::size_t output_count);

    // Apply a morph operation to an FFT cache. Returns a complex
    // spectrum (length 1025) that can be fed to irfft to produce the
    // morphed waveform. The caller re-derives a new SerumFftCache from
    // the result if chaining another morph.
    std::vector<std::complex<float>> Apply(const SerumFftCache& cache,
                                           SerumMode mode, float amount);

    // Inverse FFT a complex spectrum back to a time-domain frame of
    // length 2048. Exposed so callers can chain Apply results without
    // needing their own RealFft.
    std::vector<float> InverseFft(const std::vector<std::complex<float>>& bins);

private:
    RealFft fft_;
};

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/serum_morpher.cpp`. This is the longest file in the plan — all four morph ops plus the cache and interpolation helpers.

```cpp
#include "dsp/serum_morpher.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fim::dsp {

namespace {

constexpr std::size_t kFftSize = 2048;
constexpr std::size_t kNumBins = kFftSize / 2 + 1;  // 1025

// Constants from Python's SerumWavetableConverter (which mirror Vital's).
constexpr float kPhaseDisperseCenter = 24.0f;
constexpr float kPhaseDisperseScale = 0.05f;
constexpr float kMaxHarmonicStretch = 12.0f;
constexpr int kFrequencyBins = 10;

}  // namespace

SerumMorpher::SerumMorpher() : fft_(kFftSize) {}

SerumMorpher::~SerumMorpher() = default;

SerumFftCache SerumMorpher::ComputeCache(const std::vector<float>& frame) {
    assert(frame.size() == kFftSize);

    std::vector<std::complex<float>> bins(kNumBins);
    fft_.Forward(frame.data(), bins.data());

    SerumFftCache cache;
    cache.amplitudes.resize(kNumBins);
    cache.phases.resize(kNumBins);
    cache.normalized_real.resize(kNumBins);
    cache.normalized_imag.resize(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        cache.amplitudes[k] = std::abs(bins[k]);
        cache.phases[k] = std::arg(bins[k]);
        cache.normalized_real[k] = std::cos(cache.phases[k]);
        cache.normalized_imag[k] = std::sin(cache.phases[k]);
    }
    return cache;
}

std::vector<SerumFftCache> SerumMorpher::InterpolateCaches(
    const std::vector<SerumFftCache>& sources, std::size_t output_count) {
    std::vector<SerumFftCache> result(output_count);
    if (sources.empty() || output_count == 0) {
        return result;
    }
    if (sources.size() == 1) {
        // All outputs are copies of the single source.
        for (std::size_t i = 0; i < output_count; ++i) {
            result[i] = sources[0];
        }
        return result;
    }

    const float source_last = static_cast<float>(sources.size() - 1);
    const float output_last = static_cast<float>(output_count - 1);

    for (std::size_t out_idx = 0; out_idx < output_count; ++out_idx) {
        // Fractional position in the source range.
        const float src_pos =
            (output_count == 1) ? 0.0f : (static_cast<float>(out_idx) / output_last) * source_last;
        const std::size_t src_floor = static_cast<std::size_t>(std::floor(src_pos));
        const std::size_t src_ceil = std::min(src_floor + 1, sources.size() - 1);
        const float t = src_pos - static_cast<float>(src_floor);
        const std::size_t nearest = (t < 0.5f) ? src_floor : src_ceil;

        result[out_idx].amplitudes.resize(kNumBins);
        result[out_idx].phases.resize(kNumBins);
        result[out_idx].normalized_real.resize(kNumBins);
        result[out_idx].normalized_imag.resize(kNumBins);

        for (std::size_t k = 0; k < kNumBins; ++k) {
            // Magnitudes: linear interpolation between the two nearest
            // source caches.
            result[out_idx].amplitudes[k] =
                sources[src_floor].amplitudes[k] * (1.0f - t) +
                sources[src_ceil].amplitudes[k] * t;
            // Phases: take from the nearest source cache. This avoids
            // phase unwrapping complications.
            result[out_idx].phases[k] = sources[nearest].phases[k];
            result[out_idx].normalized_real[k] = sources[nearest].normalized_real[k];
            result[out_idx].normalized_imag[k] = sources[nearest].normalized_imag[k];
        }
    }

    return result;
}

namespace {

// FORMANT_SCALE: shift harmonics up or down by a scale factor. Scale in
// [0.25, 4.0] computed from amount in [0, 1]: scale = 2^(4*amount - 2),
// so amount=0 gives 0.25 (shift down 2 octaves), amount=0.5 gives 1.0
// (identity), amount=1 gives 4.0 (shift up 2 octaves).
//
// Bidirectional extension of Python's monotonic-up implementation. The
// distribution math is identical — for each source harmonic, compute
// the destination position and distribute its amplitude to the two
// surrounding destination bins via linear interpolation.
std::vector<std::complex<float>> ApplyFormantScale(const SerumFftCache& cache,
                                                   float amount) {
    const float scale = std::pow(2.0f, 4.0f * amount - 2.0f);
    const float safe_scale = std::max(scale, 0.001f);

    std::vector<float> new_real(kNumBins, 0.0f);
    std::vector<float> new_imag(kNumBins, 0.0f);

    // DC unchanged.
    new_real[0] = cache.amplitudes[0] * cache.normalized_real[0];
    new_imag[0] = cache.amplitudes[0] * cache.normalized_imag[0];

    const std::size_t max_harmonics = std::min(
        kNumBins,
        static_cast<std::size_t>(static_cast<float>(kNumBins - 1) / safe_scale + 1.0f));

    for (std::size_t i = 1; i < max_harmonics; ++i) {
        const float shifted_index =
            std::max(1.0f, (static_cast<float>(i) - 1.0f) * safe_scale + 1.0f);
        const std::size_t dest_index = static_cast<std::size_t>(shifted_index);
        if (dest_index >= kNumBins - 1) {
            break;
        }

        const float t = shifted_index - static_cast<float>(dest_index);
        const float amplitude = cache.amplitudes[i];
        const float r = cache.normalized_real[i];
        const float m = cache.normalized_imag[i];

        const float a1 = (1.0f - t) * amplitude;
        const float a2 = t * amplitude;

        new_real[dest_index] += a1 * r;
        new_imag[dest_index] += a1 * m;
        new_real[dest_index + 1] += a2 * r;
        new_imag[dest_index + 1] += a2 * m;
    }

    std::vector<std::complex<float>> result(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        result[k] = std::complex<float>(new_real[k], new_imag[k]);
    }
    return result;
}

// PHASE_DISPERSE: frequency-dependent phase rotation with parabolic
// profile centered at harmonic 24. Matches Python's formula except that
// we explicitly skip DC (bin 0) and Nyquist (bin N/2) since those must
// stay real in a real-signal rfft.
std::vector<std::complex<float>> ApplyPhaseDisperse(const SerumFftCache& cache,
                                                    float amount) {
    const float center = kPhaseDisperseCenter;
    const float offset = -((center - 1.0f) * (center - 1.0f)) * amount;

    std::vector<std::complex<float>> result(kNumBins);

    // DC and Nyquist stay real, unchanged.
    result[0] = std::complex<float>(cache.amplitudes[0] * cache.normalized_real[0], 0.0f);
    result[kNumBins - 1] = std::complex<float>(
        cache.amplitudes[kNumBins - 1] * cache.normalized_real[kNumBins - 1], 0.0f);

    for (std::size_t i = 1; i < kNumBins - 1; ++i) {
        const float fi = static_cast<float>(i);
        const float delta_phase = (fi - center) * (fi - center) * amount + offset;
        const float phase_shift = delta_phase * kPhaseDisperseScale;

        const float cos_p = std::cos(phase_shift);
        const float sin_p = std::sin(phase_shift);

        const float orig_r = cache.normalized_real[i];
        const float orig_m = cache.normalized_imag[i];

        // Rotate the phasor.
        const float new_r = orig_r * cos_p - orig_m * sin_p;
        const float new_m = orig_r * sin_p + orig_m * cos_p;

        const float amplitude = cache.amplitudes[i];
        result[i] = std::complex<float>(amplitude * new_r, amplitude * new_m);
    }

    return result;
}

// SMEAR: running-average amplitude smoothing with Vital's peculiar
// (i+0.25)/i scaling. Phases unchanged. This is the Vital version;
// single-wav has its own bespoke 5-tap moving average in SpectralModifier.
std::vector<std::complex<float>> ApplySmear(const SerumFftCache& cache,
                                            float amount) {
    std::vector<std::complex<float>> result(kNumBins);

    // First harmonic: amplitude scaled by (1 - amount).
    float running_amplitude = cache.amplitudes[0] * (1.0f - amount);
    result[0] = std::complex<float>(
        running_amplitude * cache.normalized_real[0],
        running_amplitude * cache.normalized_imag[0]);

    for (std::size_t i = 1; i < kNumBins; ++i) {
        const float original_amplitude = cache.amplitudes[i];
        running_amplitude =
            (1.0f - amount) * original_amplitude + amount * running_amplitude;

        result[i] = std::complex<float>(
            running_amplitude * cache.normalized_real[i],
            running_amplitude * cache.normalized_imag[i]);

        // Vital's per-step scaling. Preserved as-is despite being
        // mathematically mysterious — changing it produces a different
        // sound.
        running_amplitude *= (static_cast<float>(i) + 0.25f) / static_cast<float>(i);
    }

    return result;
}

// HARMONIC_STRETCH: octave-based nonlinear stretch. Matches Python's
// formula with MAX_HARMONIC_STRETCH=12 and FREQUENCY_BINS=10.
std::vector<std::complex<float>> ApplyHarmonicStretch(const SerumFftCache& cache,
                                                      float amount) {
    const float mult = 1.0f + amount * (kMaxHarmonicStretch - 1.0f);

    std::vector<float> new_real(kNumBins, 0.0f);
    std::vector<float> new_imag(kNumBins, 0.0f);

    // DC unchanged.
    new_real[0] = cache.amplitudes[0] * cache.normalized_real[0];
    new_imag[0] = cache.amplitudes[0] * cache.normalized_imag[0];

    for (std::size_t i = 1; i < kNumBins; ++i) {
        const float octave = std::log2(static_cast<float>(i));
        const float power = octave / static_cast<float>(kFrequencyBins - 1);
        const float shift = std::pow(mult, power);
        const float shifted_index =
            std::max(1.0f, shift * (static_cast<float>(i) - 1.0f) + 1.0f);

        const std::size_t dest_index = static_cast<std::size_t>(shifted_index);
        if (dest_index >= kNumBins - 1) {
            continue;
        }

        const float t = shifted_index - static_cast<float>(dest_index);
        const float amplitude = cache.amplitudes[i];
        const float r = cache.normalized_real[i];
        const float m = cache.normalized_imag[i];

        const float a1 = (1.0f - t) * amplitude;
        const float a2 = t * amplitude;

        new_real[dest_index] += a1 * r;
        new_imag[dest_index] += a1 * m;
        new_real[dest_index + 1] += a2 * r;
        new_imag[dest_index + 1] += a2 * m;
    }

    std::vector<std::complex<float>> result(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        result[k] = std::complex<float>(new_real[k], new_imag[k]);
    }
    return result;
}

}  // namespace

std::vector<std::complex<float>> SerumMorpher::Apply(const SerumFftCache& cache,
                                                     SerumMode mode, float amount) {
    switch (mode) {
        case SerumMode::kFormant:
            return ApplyFormantScale(cache, amount);
        case SerumMode::kPhase:
            return ApplyPhaseDisperse(cache, amount);
        case SerumMode::kSmear:
            return ApplySmear(cache, amount);
        case SerumMode::kStretch:
            return ApplyHarmonicStretch(cache, amount);
    }
    // Unreachable but required by some compilers to avoid a warning.
    return std::vector<std::complex<float>>(kNumBins, std::complex<float>(0.0f, 0.0f));
}

std::vector<float> SerumMorpher::InverseFft(
    const std::vector<std::complex<float>>& bins) {
    assert(bins.size() == kNumBins);
    std::vector<float> output(kFftSize);
    fft_.Inverse(bins.data(), output.data());
    // PFFFT inverse is unnormalized — divide by N to get the mathematical
    // inverse.
    const float inv_n = 1.0f / static_cast<float>(kFftSize);
    for (float& s : output) {
        s *= inv_n;
    }
    return output;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/dsp/serum_morpher.cpp`:

```cmake
    src/dsp/serum_loader.cpp
    src/dsp/serum_morpher.cpp
    src/dsp/single_wav_generator.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 70 tests pass (64 from after Task 4 + 6 new serum_morpher tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/serum_morpher.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/serum_morpher.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_serum_morpher_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/serum_morpher.h cpp/src/dsp/serum_morpher.cpp \
        cpp/tests/dsp_serum_morpher_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add SerumMorpher with 4 Vital-derived morph ops"
```

---

## Task 6: SerumGenerator

Top-level orchestration for Serum mode. Loads the source file via `SerumLoader`, computes source FFT caches, interpolates to 8 caches, loops 8 pages × 64 cells applying Y then Z morph (re-deriving the cache between morphs), inverse FFT, DC removal, normalization, and writes 8 WAV files. No unit tests — end-to-end verification happens in Phase 4b via the UI.

**Files:**
- Create: `cpp/src/dsp/serum_generator.h`
- Create: `cpp/src/dsp/serum_generator.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/dsp/serum_generator.h`:

```cpp
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include "dsp/serum_morpher.h"

namespace fim::dsp {

// Options bundle for Serum generation. Default values are placeholders;
// SerumWavService sets them from the UI selections.
struct SerumGenerateOptions {
    SerumMode y_mode = SerumMode::kFormant;
    SerumMode z_mode = SerumMode::kPhase;
};

// Top-level Serum mode wavetable generator. Loads a Serum-format WAV,
// interpolates its frames down to 8 via frequency-domain linear
// interpolation, then runs the 8 pages × 64 cells generation loop
// applying Y then Z morphs to each cell. Writes 8 WAV files (1.wav..8.wav)
// into the output directory.
//
// Defaults match Python:
//   samples     = 2048 (per-cycle final length, fixed by Serum's format)
//   num_pages   = 8    (Z dimension, fixed by FourSeas hardware)
class SerumGenerator {
public:
    explicit SerumGenerator(std::size_t samples = 2048, std::size_t num_pages = 8);

    using ProgressCallback = std::function<void(int percent)>;

    // Run the full pipeline. Returns false on any I/O or DSP error.
    bool Generate(const std::filesystem::path& input_audio_path,
                  const std::filesystem::path& output_directory,
                  const SerumGenerateOptions& options = {},
                  const ProgressCallback& on_progress = {}) const;

    std::size_t samples() const { return samples_; }
    std::size_t num_pages() const { return num_pages_; }

private:
    bool WritePageToWav(const std::filesystem::path& path,
                        const std::vector<float>& page_samples) const;

    std::size_t samples_;
    std::size_t num_pages_;
};

}  // namespace fim::dsp
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/dsp/serum_generator.cpp`:

```cpp
#include "dsp/serum_generator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dr_wav.h"
#include "dsp/serum_loader.h"
#include "dsp/serum_morpher.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kCellsPerSide = 8;
constexpr std::size_t kCellsPerPage = kCellsPerSide * kCellsPerSide;  // 64
constexpr std::uint32_t kOutputSampleRate = 44100;

}  // namespace

SerumGenerator::SerumGenerator(std::size_t samples, std::size_t num_pages)
    : samples_(samples), num_pages_(num_pages) {}

bool SerumGenerator::Generate(const std::filesystem::path& input_audio_path,
                              const std::filesystem::path& output_directory,
                              const SerumGenerateOptions& options,
                              const ProgressCallback& on_progress) const {
    // Step 1: load and split the Serum WAV into N × 2048 frames.
    auto loaded = SerumLoader::Load(input_audio_path);
    if (!loaded.has_value() || loaded->frames.empty()) {
        return false;
    }

    // Step 2: ensure output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    // Step 3: compute source FFT caches, one per loaded frame.
    SerumMorpher morpher;
    std::vector<SerumFftCache> source_caches;
    source_caches.reserve(loaded->frames.size());
    for (const auto& frame : loaded->frames) {
        source_caches.push_back(morpher.ComputeCache(frame));
    }

    // Step 4: frequency-domain interpolate to exactly 8 caches (one per
    // X position in the output wavetable grid).
    const auto x_caches = morpher.InterpolateCaches(source_caches, kCellsPerSide);

    // Step 5: generate 8 pages.
    for (std::size_t z = 0; z < num_pages_; ++z) {
        // Each (x, y) cell is computed independently: take the X cache,
        // apply the Y morph with y_amount = y/7, re-derive the cache,
        // apply the Z morph with z_amount = z/7, inverse FFT, DC remove,
        // normalize.
        std::vector<float> page_samples;
        page_samples.reserve(samples_ * kCellsPerPage);

        const float z_amount = static_cast<float>(z) / 7.0f;

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            const float y_amount = static_cast<float>(y) / 7.0f;
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                // Apply Y morph.
                const auto y_morphed =
                    morpher.Apply(x_caches[x], options.y_mode, y_amount);

                // Re-derive the cache from the Y-morphed spectrum.
                SerumFftCache mid_cache;
                mid_cache.amplitudes.resize(y_morphed.size());
                mid_cache.phases.resize(y_morphed.size());
                mid_cache.normalized_real.resize(y_morphed.size());
                mid_cache.normalized_imag.resize(y_morphed.size());
                for (std::size_t k = 0; k < y_morphed.size(); ++k) {
                    mid_cache.amplitudes[k] = std::abs(y_morphed[k]);
                    mid_cache.phases[k] = std::arg(y_morphed[k]);
                    mid_cache.normalized_real[k] = std::cos(mid_cache.phases[k]);
                    mid_cache.normalized_imag[k] = std::sin(mid_cache.phases[k]);
                }

                // Apply Z morph.
                const auto z_morphed =
                    morpher.Apply(mid_cache, options.z_mode, z_amount);

                // Inverse FFT back to time domain.
                auto cell = morpher.InverseFft(z_morphed);

                // Remove DC from the cell.
                float dc_sum = 0.0f;
                for (float s : cell) {
                    dc_sum += s;
                }
                const float dc = dc_sum / static_cast<float>(cell.size());
                for (float& s : cell) {
                    s -= dc;
                }

                // Normalize the cell to peak 1.0.
                float peak = 0.0f;
                for (float s : cell) {
                    peak = std::max(peak, std::abs(s));
                }
                if (peak > 0.0f) {
                    const float inv = 1.0f / peak;
                    for (float& s : cell) {
                        s *= inv;
                    }
                }

                // Append to page buffer. Serum cells are already at
                // samples_ length (2048) since we ifft'd at 2048 and the
                // Serum output doesn't need additional resampling.
                page_samples.insert(page_samples.end(), cell.begin(), cell.end());
            }
        }

        // Globally normalize the page so the max abs value across all
        // 131072 samples is 1.0.
        float page_peak = 0.0f;
        for (float s : page_samples) {
            page_peak = std::max(page_peak, std::abs(s));
        }
        if (page_peak > 0.0f) {
            const float inv = 1.0f / page_peak;
            for (float& s : page_samples) {
                s *= inv;
            }
        }

        const auto path = output_directory / (std::to_string(z + 1) + ".wav");
        if (!WritePageToWav(path, page_samples)) {
            return false;
        }

        if (on_progress) {
            const int percent = static_cast<int>((z + 1) * 100 / num_pages_);
            on_progress(percent);
        }
    }

    return true;
}

bool SerumGenerator::WritePageToWav(const std::filesystem::path& path,
                                    const std::vector<float>& page_samples) const {
    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = kOutputSampleRate;
    format.bitsPerSample = 16;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr)) {
        return false;
    }

    std::vector<std::int16_t> int_samples(page_samples.size());
    for (std::size_t i = 0; i < page_samples.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, page_samples[i]));
        int_samples[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }

    const drwav_uint64 frames_written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);
    return frames_written == int_samples.size();
}

}  // namespace fim::dsp
```

- [ ] **Step 3: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`:

```cmake
    src/dsp/serum_morpher.cpp
    src/dsp/serum_generator.cpp
    src/dsp/single_wav_generator.cpp
```

- [ ] **Step 4: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. Tests from earlier tasks still pass (70/70).

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/serum_generator.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/serum_generator.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/serum_generator.h cpp/src/dsp/serum_generator.cpp \
        cpp/CMakeLists.txt
git commit -m "feat(cpp): add SerumGenerator orchestrating the Serum DSP pipeline"
```

---

## Task 7: SerumWavService

Concrete subclass of `GenerateServiceBase` for Serum mode. Adds `SetYMode`/`SetZMode` for `SerumMode`, caches a `SerumGenerateOptions`, overrides `DoGenerate` to call `SerumGenerator::Generate`. Mirrors the shape of `SingleWavService` exactly.

**Files:**
- Create: `cpp/src/app/services/serum_wav_service.h`
- Create: `cpp/src/app/services/serum_wav_service.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/app/services/serum_wav_service.h`:

```cpp
#pragma once

#include "app/services/generate_service_base.h"
#include "dsp/serum_generator.h"

namespace fim::app {

// Serum mode wavetable service. Inherits the common scaffolding from
// GenerateServiceBase and adds Y/Z morph mode setters plus a cached
// SerumGenerateOptions for the DSP generator.
class SerumWavService : public GenerateServiceBase {
    Q_OBJECT

public:
    explicit SerumWavService(QObject* parent = nullptr);
    ~SerumWavService() override = default;

    // Configure the Serum morph modes used by the next Generate() call.
    void SetYMode(fim::dsp::SerumMode mode);
    void SetZMode(fim::dsp::SerumMode mode);

protected:
    bool DoGenerate(const std::filesystem::path& input,
                    const std::filesystem::path& output,
                    const ProgressCallback& progress_cb) override;

private:
    fim::dsp::SerumGenerateOptions options_;
};

}  // namespace fim::app
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/app/services/serum_wav_service.cpp`:

```cpp
#include "app/services/serum_wav_service.h"

namespace fim::app {

SerumWavService::SerumWavService(QObject* parent) : GenerateServiceBase(parent) {}

void SerumWavService::SetYMode(fim::dsp::SerumMode mode) {
    options_.y_mode = mode;
}

void SerumWavService::SetZMode(fim::dsp::SerumMode mode) {
    options_.z_mode = mode;
}

bool SerumWavService::DoGenerate(const std::filesystem::path& input,
                                 const std::filesystem::path& output,
                                 const ProgressCallback& progress_cb) {
    const fim::dsp::SerumGenerateOptions opts = options_;
    fim::dsp::SerumGenerator generator;
    return generator.Generate(input, output, opts, progress_cb);
}

}  // namespace fim::app
```

- [ ] **Step 3: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`:

```cmake
    src/app/services/generate_service_base.cpp
    src/app/services/serum_wav_service.cpp
    src/app/services/single_wav_service.cpp
```

- [ ] **Step 4: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. Tests still pass (70/70). The service is compiled but not yet wired to a UI — that's Phase 4b.

- [ ] **Step 5: Add the sample-domain resampling followup**

Modify `docs/followups.md`. Add a new entry under the "Phase 2" section (or create a new "Phase 4" section at the top):

```markdown
## Phase 4

- **Frame resampling — offer sample-domain as an alternative to the default frequency-domain approach.** Phase 4a implements Serum-mode frame resampling in the frequency domain (interpolate source frame FFTs, take phases from the nearest source). This is cleaner for non-smoothly-varying wavetables but deviates from Python's sample-domain linear interpolation. A future revision could expose both as a user-selectable option in the Serum screen — the two produce audibly different results for chopped/percussive Serum files and the user might prefer either. ~50 LOC to add the alternative code path.
```

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/serum_wav_service.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/serum_wav_service.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/serum_wav_service.h \
        cpp/src/app/services/serum_wav_service.cpp \
        cpp/CMakeLists.txt docs/followups.md
git commit -m "feat(cpp): add SerumWavService inheriting GenerateServiceBase"
```

---

## Task 8: Push and verify CI

Push the branch and verify explicitly — NOT trusting the background `gh run watch` exit code per the memory from Phase 3a.

- [ ] **Step 1: Push the branch**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
```

- [ ] **Step 2: Watch CI**

```bash
sleep 5
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId,status --jq '.[0]'
# copy the returned id and pass it to:
gh run watch <id> --exit-status
```

- [ ] **Step 3: VERIFY CI status explicitly**

```bash
gh run view <id>
```

Expected: all four jobs ✓ — `clang-format check`, `Build - macos-latest`, `Build - ubuntu-latest`, `Build - windows-latest`. Phase 4a is done when all green.

Watch for clang-format version-skew issues (the Phase 3b failure mode — unicode em-dash or different include-ordering rules between local and CI clang-format). If the CI clang-format fails on something the local one accepts, the fix is usually to remove unicode from source/comments and re-run `clang-format -i` on the affected files.

---

## Phase 4a done when:

1. ✅ Single-wav has 4 Y modes (Tilt, Formant, Stretch, Smear)
2. ✅ `fim::app::GenerateServiceBase` exists and `SingleWavService` inherits from it
3. ✅ `fim::ui::ModeScreenBase` exists and `AnyWavScreen` inherits from it
4. ✅ Any-wav mode passes manual regression verification (full flow works end-to-end)
5. ✅ `fim::dsp::SerumLoader` loads and validates Serum-format WAV files
6. ✅ `fim::dsp::SerumMorpher` implements the 4 Vital-derived morph operations with correct DC/Nyquist handling
7. ✅ `fim::dsp::SerumMorpher::InterpolateCaches` performs frequency-domain frame resampling
8. ✅ `fim::dsp::SerumGenerator` runs the full Serum pipeline and writes 8 WAV files
9. ✅ `fim::app::SerumWavService` inherits `GenerateServiceBase` and exposes Serum mode setters
10. ✅ All 70 Catch2 tests pass locally
11. ✅ CI green on macOS, Ubuntu, Windows, and clang-format

After Phase 4a ships, all the plumbing for Serum mode exists but nothing in the UI routes to it yet. Phase 4b builds `SerumWavScreen` using `ModeScreenBase`, updates `MainWindow` to route the Serum launcher card to it, and removes the "not yet implemented" QMessageBox.
