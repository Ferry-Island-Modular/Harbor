# Phase 4c Implementation Plan — Three-wav mode (axis-as-weight cross-synthesis)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable the "3 .wav files" launcher card. The user drops 3 arbitrary audio files, one per axis (X file, Y file, Z file). Each cell in the 8×8×8 wavetable grid is a weighted blend of the 3 files' time-averaged magnitude spectra, where the weights are the axis positions themselves. Reconstructed with zero phase (all cosines) via inverse FFT for a bright, pitched wavetable character. No morph modes — all the spectral variety comes from which 3 files the user picks.

**Architecture:** `ThreeWavGenerator` loads 3 files via `WavLoader`, **resamples each to 44.1 kHz via libsamplerate** (files with mismatched rates are transparently converted, not rejected), runs STFT on each, computes the time-averaged magnitude spectrum per file (Python's `mean(|stft|, axis=time)` trick), then loops 8 pages × 64 cells building each cell as a weighted magnitude sum + zero-phase iFFT. A new `fim::dsp::ResampleTo` free function wraps libsamplerate's `src_simple` for one-shot resampling. `ThreeWavScreen` shows 3 file slots + X/Y/Z axis descriptors + Generate — no mode selectors, simpler than any other mode screen. `ModeScreenBase` gains three small concessions: `Reset()` becomes virtual, `SetState()` moves to protected, and a new `ShowDefaultFilenameRow()` hook lets subclasses opt out of the base's single-file row. `MainWindow` constructs and routes the new screen; `LauncherScreen` drops the `kComingSoon` state on the three-wavs card.

**Tech Stack:** Existing Phase 3/4 DSP layer (`WavLoader`, `Stft`, `RealFft`), libsamplerate (already vendored, newly used), `dr_wav` for output, Catch2 v3 for tests.

**Spec deviations from Python reference:**

- **Completely different algorithm from Python's `_cross_audio_resynth`.** Python's approach is weird: Z picks a pair of adjacent files, X crossfades within the pair, Y sets an additive-synthesis peak threshold. The Python author's own comment in `main.py:210` is `# Z isn't doing enough here`. Phase 4c replaces this with a clean "one file per axis, axis position = weight" model that the user explicitly requested.
- **No morph modes.** Three-wav mode has no Y/Z morph mode selectors. All variety comes from (a) which 3 files the user chooses and (b) the 8^3 = 512 unique (X, Y, Z) weight combinations. Simpler UI than any-wav or Serum.
- **Time-averaging borrowed from Python.** We DO take Python's time-averaging trick (`mag = mean(|stft|, axis=time)`) — each file becomes one static magnitude spectrum. This means we don't scan time within each file; each file is "one color" that the axis blends.
- **Zero-phase reconstruction.** At each cell, we do `iFFT(weighted_mag * exp(i * 0))` — all phases are zero, meaning the output is a sum of cosines. This produces a bright, pitched character suitable for wavetable synthesis. Alternative phase strategies (random, single-file-sourced) are enumerated in `~/.claude/projects/.../memory/reference_three_wav_approaches.md` as iteration candidates.
- **ε-floor on weights.** Instead of pure `w_x = x/7`, we use `w_x = x/7 + 0.1`. This ensures the (0, 0, 0) corner cell isn't dead silent — you get a uniform-ish blend of all 3 files at the corners instead of a zero cell. Makes the lowest corner of the grid still musically meaningful.
- **Files with mismatched sample rates are silently resampled to 44.1 kHz.** Python rejects mismatched rates (implicit — librosa resamples). Phase 4c uses libsamplerate's `SRC_SINC_MEDIUM_QUALITY` converter to transparently bring all 3 files to a common rate before STFT. This is called out as non-negotiable by the user — Eurorack audiences may not know what sample rate even means, so an error like "sample rate mismatch" would be confusing.
- **Three-wav output directory is separate from single-wav and Serum.** `${AppLocalDataLocation}/three_wav_resynth`. Each mode writes its own banks so nothing overwrites.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files:**

- `cpp/src/dsp/resample.h` / `resample.cpp` — `fim::dsp::ResampleTo(input, input_rate, target_rate)` free function. Thin wrapper around libsamplerate's `src_simple` with `SRC_SINC_MEDIUM_QUALITY`. One-shot resampling; not for streaming. Returns the resampled vector.
- `cpp/src/dsp/three_wav_generator.h` / `three_wav_generator.cpp` — `fim::dsp::ThreeWavGenerator` class. Loads 3 files, resamples each to 44.1 kHz, STFT each, time-averages magnitudes, loops 8 pages × 64 cells with weighted-blend + zero-phase iFFT + normalize + write.
- `cpp/src/app/services/three_wav_service.h` / `three_wav_service.cpp` — `fim::app::ThreeWavService` inheriting `GenerateServiceBase`. Manages a 3-slot array of input paths. No options struct (no morph modes). Overrides `DoGenerate` to call `ThreeWavGenerator::Generate`.
- `cpp/src/ui/three_wav_screen.h` / `three_wav_screen.cpp` — `fim::ui::ThreeWavScreen` inheriting `ModeScreenBase`. 3 drop slots in the file-set page (no kEmpty state used), per-slot filenames and clear buttons, X/Y/Z descriptors (no morph mode selectors), Generate button disabled until all 3 files loaded.
- `cpp/tests/dsp_resample_test.cpp` — 3 tests: identity (same rate), 2× upsample, 2× downsample. No need to test non-integer ratios specifically — libsamplerate's own tests cover that.

**Modified files:**

- `cpp/src/ui/mode_screen_base.h` / `mode_screen_base.cpp` — make `Reset()` virtual, move `SetState(State)` from private to protected, add protected virtual `ShowDefaultFilenameRow() const` (default true), skip filename row in `BuildFileSetPage()` when override returns false.
- `cpp/src/ui/launcher_screen.cpp` — change three-wavs card state from `kComingSoon` to `kDefault` and wire its `chosen()` to emit `modeChosen(Mode::kThreeWavs)`.
- `cpp/src/ui/main_window.h` / `main_window.cpp` — add `ThreeWavScreen* three_wav_screen_` member, construct and add to stack, wire `backRequested`, route `kThreeWavs` in `OnModeChosen`.
- `cpp/CMakeLists.txt` — add 4 new source files. The `SampleRate::samplerate` link target is already present.
- `cpp/tests/CMakeLists.txt` — add the resample test and its implementation source.
- `docs/followups.md` — remove the stale `Three-wavs mode is permanently kComingSoon` entry.

**Deleted files:** none.

---

## Task 1: DSP sample rate converter

A free function wrapper around libsamplerate's `src_simple` for one-shot resampling. Used by `ThreeWavGenerator` to bring all 3 input files to a common 44.1 kHz rate before STFT analysis.

**Files:**
- Create: `cpp/src/dsp/resample.h`
- Create: `cpp/src/dsp/resample.cpp`
- Create: `cpp/tests/dsp_resample_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_resample_test.cpp`:

```cpp
#include "dsp/resample.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
}

TEST_CASE("ResampleTo identity (same rate) returns the input",
          "[dsp][resample]") {
    std::vector<float> input(1024);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 10.0f * i / input.size());
    }

    const auto output = fim::dsp::ResampleTo(input, 44100, 44100);
    REQUIRE(output.size() == input.size());
    // Tolerance is loose because libsamplerate is not strictly an
    // identity for equal-rate conversions (it still runs through the
    // sinc filter).
    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE_THAT(output[i], WithinAbs(input[i], 0.01f));
    }
}

TEST_CASE("ResampleTo 2x upsample doubles the length", "[dsp][resample]") {
    std::vector<float> input(512);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 5.0f * i / input.size());
    }

    const auto output = fim::dsp::ResampleTo(input, 22050, 44100);
    // Allow a small tolerance on length — libsamplerate may produce
    // one or two frames off exactly 2x due to its transient handling.
    REQUIRE(output.size() >= input.size() * 2 - 2);
    REQUIRE(output.size() <= input.size() * 2 + 2);
}

TEST_CASE("ResampleTo 2x downsample halves the length", "[dsp][resample]") {
    std::vector<float> input(1024);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 5.0f * i / input.size());
    }

    const auto output = fim::dsp::ResampleTo(input, 88200, 44100);
    REQUIRE(output.size() >= input.size() / 2 - 2);
    REQUIRE(output.size() <= input.size() / 2 + 2);
}

TEST_CASE("ResampleTo handles non-integer ratio (48k → 44.1k)",
          "[dsp][resample]") {
    // 48000 → 44100 is the most common real-world resample (consumer
    // audio to CD rate). Ratio ≈ 0.91875. No exact output length to
    // check; just verify the result is non-empty and roughly the right
    // size.
    std::vector<float> input(48000);  // 1 second at 48 kHz
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
    }

    const auto output = fim::dsp::ResampleTo(input, 48000, 44100);
    // Expect ~44100 output samples, +/- a few for transient handling.
    REQUIRE(output.size() >= 44090);
    REQUIRE(output.size() <= 44110);
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`. Add the test source and the implementation source:

```cmake
    dsp_serum_morpher_test.cpp
    dsp_resample_test.cpp
    ../src/engine/single_header_impls.cpp
```

And in the implementation sources list:

```cmake
    ../src/dsp/serum_morpher.cpp
    ../src/dsp/resample.cpp
)
```

Also add `SampleRate::samplerate` to the test target's link libraries. Find the existing `target_link_libraries(fim-tests ...)` block and add it:

```cmake
target_link_libraries(fim-tests PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
    SampleRate::samplerate
    spdlog::spdlog
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/resample.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/resample.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fim::dsp {

// One-shot sample rate conversion using libsamplerate's SRC_SINC_MEDIUM_
// QUALITY converter. Appropriate for file loading (non-streaming,
// non-realtime). For streaming use, wrap libsamplerate's state-based API
// directly.
//
// If input_rate == target_rate, returns a copy of the input (still
// routed through libsamplerate but effectively identity).
//
// Returns an empty vector if libsamplerate fails internally (rare —
// usually only on allocation failure).
std::vector<float> ResampleTo(const std::vector<float>& input,
                              std::uint32_t input_rate,
                              std::uint32_t target_rate);

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/resample.cpp`:

```cpp
#include "dsp/resample.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <samplerate.h>

namespace fim::dsp {

std::vector<float> ResampleTo(const std::vector<float>& input,
                              std::uint32_t input_rate,
                              std::uint32_t target_rate) {
    if (input.empty() || input_rate == 0 || target_rate == 0) {
        return {};
    }

    const double ratio =
        static_cast<double>(target_rate) / static_cast<double>(input_rate);

    // Allocate output with a small headroom beyond ceil(input_length *
    // ratio) — libsamplerate can produce one or two extra frames at the
    // transient.
    const std::size_t expected = static_cast<std::size_t>(
        std::ceil(static_cast<double>(input.size()) * ratio));
    std::vector<float> output(expected + 8, 0.0f);

    SRC_DATA data = {};
    data.data_in = input.data();
    data.input_frames = static_cast<long>(input.size());
    data.data_out = output.data();
    data.output_frames = static_cast<long>(output.size());
    data.src_ratio = ratio;
    data.end_of_input = 1;

    const int err = src_simple(&data, SRC_SINC_MEDIUM_QUALITY, /*channels=*/1);
    if (err != 0) {
        return {};
    }

    // Trim to the actual number of frames libsamplerate produced.
    output.resize(static_cast<std::size_t>(data.output_frames_gen));
    return output;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/dsp/resample.cpp` to the `qt_add_executable(fim-config-tool ...)` block right after `src/dsp/wav_loader.cpp`:

```cmake
    src/dsp/wav_loader.cpp
    src/dsp/resample.cpp
    src/dsp/real_fft.cpp
```

The main executable already links `SampleRate::samplerate`, so no changes to `target_link_libraries(fim-config-tool ...)`.

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 77 tests pass (73 from Phase 4b + 4 new resample tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/resample.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/resample.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_resample_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/resample.h cpp/src/dsp/resample.cpp \
        cpp/tests/dsp_resample_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add ResampleTo via libsamplerate for arbitrary-rate conversion"
```

---

## Task 2: ModeScreenBase hooks for multi-file-drop subclasses

Three small concessions to `ModeScreenBase` so `ThreeWavScreen` can build its 3-slot file-set page without fighting the base class. Pure mechanical refactor — no behavior change for single-wav or Serum since they keep the defaults.

**Files:**
- Modify: `cpp/src/ui/mode_screen_base.h`
- Modify: `cpp/src/ui/mode_screen_base.cpp`

- [ ] **Step 1: Update the header**

Modify `cpp/src/ui/mode_screen_base.h`. Three changes:

Change 1: Make `Reset()` virtual (public section):

```cpp
public:
    ModeScreenBase(fim::engine::RealtimeAudioEngine* engine,
                   fim::app::Settings* settings, QWidget* parent = nullptr);
    ~ModeScreenBase() override = default;

    // Resets the screen back to the empty state. Called when the user
    // navigates away and returns. Also calls the subclass OnResetHook so
    // mode-specific state (file path, selector positions) can reset too.
    //
    // Virtual so multi-file modes can override to handle their own
    // state (e.g. ThreeWavScreen clears 3 slots and stays in kFileSet).
    virtual void Reset();
```

Change 2: Move `SetState(State)` from the private section to the protected section. Grep for the existing private declaration:

```cpp
private:
    void SetState(State state);
```

Remove it from there and add to the protected section right after `OnGenerateClicked()`:

```cpp
protected:
    // ... existing protected helpers ...

    // Transition the state machine. Subclasses occasionally need this
    // (e.g. ThreeWavScreen jumping to kFileSet from its constructor
    // since it doesn't use the kEmpty state at all).
    void SetState(State state);
```

Change 3: Add `ShowDefaultFilenameRow()` virtual in the protected section:

```cpp
    // Whether the file-set page should show the default "single filename
    // label + clear button" row above the subclass content. Single-file
    // modes (any-wav, Serum) return true (default). Multi-file modes
    // (three-wav) return false to suppress the row and manage filenames
    // inside their own content.
    virtual bool ShowDefaultFilenameRow() const { return true; }
```

- [ ] **Step 2: Update `BuildFileSetPage` to use the hook**

Modify `cpp/src/ui/mode_screen_base.cpp`. Find the `BuildFileSetPage` method and wrap the filename row in a conditional:

```cpp
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

    // Filename row — shared between single-file modes. Three-wav and
    // other multi-file modes override ShowDefaultFilenameRow() to skip.
    if (ShowDefaultFilenameRow()) {
        auto* file_row = new QHBoxLayout();
        filename_label_ = new QLabel("(no file)", page);
        filename_label_->setObjectName("anyWavFilename");
        auto* clear_button = new QPushButton("Clear", page);
        clear_button->setObjectName("clearButton");
        file_row->addWidget(filename_label_);
        file_row->addStretch();
        file_row->addWidget(clear_button);
        layout->addLayout(file_row);
        connect(clear_button, &QPushButton::clicked, this,
                &ModeScreenBase::OnClearClicked);
    }

    layout->addWidget(BuildFileSetPageContent(page));
    layout->addStretch();
    return page;
}
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 77 tests still pass. Pure code motion — no behavior change.

- [ ] **Step 4: Manual regression check — any-wav and Serum must work**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Quick walk-through: launcher → any-wav → drop → generate → play → back → launcher → Serum → drop → generate → play → back. Both should work identically to before.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/mode_screen_base.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/mode_screen_base.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/mode_screen_base.h cpp/src/ui/mode_screen_base.cpp
git commit -m "refactor(cpp): add ModeScreenBase hooks for multi-file-drop subclasses"
```

---

## Task 3: ThreeWavGenerator (Option D)

The DSP core. Loads 3 files via `WavLoader`, resamples each to 44.1 kHz via `ResampleTo`, runs STFT on each, time-averages the magnitude spectrum per file, then loops 8 pages × 64 cells building each cell as `iFFT(weighted_sum(3 mags))` with all phases set to zero.

**Files:**
- Create: `cpp/src/dsp/three_wav_generator.h`
- Create: `cpp/src/dsp/three_wav_generator.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/dsp/three_wav_generator.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

namespace fim::dsp {

// Three-wav mode wavetable generator. Takes 3 arbitrary .wav files, one
// per axis, and builds an 8×8×8 wavetable grid where each cell's
// spectrum is a weighted sum of the 3 files' time-averaged magnitudes,
// with weights proportional to the cell's (X, Y, Z) position. Zero-
// phase inverse FFT produces the output waveform.
//
// Input files are silently resampled to 44.1 kHz if needed — users
// shouldn't have to think about sample rates.
//
// This is Phase 4c's "Option D" — see
// ~/.claude/projects/.../memory/reference_three_wav_approaches.md for
// the enumerated list of alternative algorithms we considered.
class ThreeWavGenerator {
public:
    explicit ThreeWavGenerator(std::size_t samples = 2048,
                               std::size_t num_pages = 8);

    using ProgressCallback = std::function<void(int percent)>;

    // Run the full pipeline. Returns false on any I/O or DSP error.
    // The 3 input paths are required; all must be non-empty and loadable.
    bool Generate(const std::array<std::filesystem::path, 3>& input_paths,
                  const std::filesystem::path& output_directory,
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

Create `cpp/src/dsp/three_wav_generator.cpp`:

```cpp
#include "dsp/three_wav_generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dr_wav.h"
#include "dsp/real_fft.h"
#include "dsp/resample.h"
#include "dsp/stft.h"
#include "dsp/wav_loader.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kFftSize = 2048;
constexpr std::size_t kHopSize = 1024;
constexpr std::size_t kNumBins = kFftSize / 2 + 1;  // 1025
constexpr std::size_t kCellsPerSide = 8;
constexpr std::size_t kCellsPerPage = kCellsPerSide * kCellsPerSide;  // 64
constexpr std::uint32_t kOutputSampleRate = 44100;
constexpr float kWeightEpsilon = 0.1f;

// Time-average the magnitude spectrum across all STFT frames. Each bin's
// output is the mean of that bin's magnitude across all time frames.
// Returns a vector of length num_bins.
std::vector<float> TimeAverageMagnitude(
    const std::vector<std::vector<std::complex<float>>>& stft_frames) {
    std::vector<float> avg(kNumBins, 0.0f);
    if (stft_frames.empty()) {
        return avg;
    }
    for (const auto& frame : stft_frames) {
        for (std::size_t k = 0; k < kNumBins && k < frame.size(); ++k) {
            avg[k] += std::abs(frame[k]);
        }
    }
    const float inv = 1.0f / static_cast<float>(stft_frames.size());
    for (float& v : avg) {
        v *= inv;
    }
    return avg;
}

// Zero-phase inverse FFT of a magnitude spectrum. All phases set to 0,
// so the output is a sum of cosines. Returns a length-2048 real signal.
std::vector<float> ZeroPhaseIfft(const std::vector<float>& magnitude,
                                 RealFft& fft) {
    std::vector<std::complex<float>> bins(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        bins[k] = std::complex<float>(magnitude[k], 0.0f);
    }
    std::vector<float> output(kFftSize);
    fft.Inverse(bins.data(), output.data());
    // PFFFT inverse is unnormalized — divide by N.
    const float inv_n = 1.0f / static_cast<float>(kFftSize);
    for (float& s : output) {
        s *= inv_n;
    }
    return output;
}

}  // namespace

ThreeWavGenerator::ThreeWavGenerator(std::size_t samples, std::size_t num_pages)
    : samples_(samples), num_pages_(num_pages) {}

bool ThreeWavGenerator::Generate(
    const std::array<std::filesystem::path, 3>& input_paths,
    const std::filesystem::path& output_directory,
    const ProgressCallback& on_progress) const {
    // Step 1: load and resample each of the 3 files to 44.1 kHz.
    // Normalizing at load means differing file levels don't cause one
    // file to dominate the blend.
    std::array<std::vector<float>, 3> resampled_audio;
    for (std::size_t i = 0; i < 3; ++i) {
        auto loaded = LoadWav(input_paths[i], /*normalize=*/true);
        if (!loaded.has_value() || loaded->samples.empty()) {
            return false;
        }
        if (loaded->sample_rate == kOutputSampleRate) {
            resampled_audio[i] = std::move(loaded->samples);
        } else {
            resampled_audio[i] =
                ResampleTo(loaded->samples, loaded->sample_rate, kOutputSampleRate);
            if (resampled_audio[i].empty()) {
                return false;
            }
        }
    }

    // Step 2: ensure the output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    // Step 3: STFT each file, then time-average the magnitude spectrum.
    // Each file collapses to one 1025-bin magnitude vector — "one color
    // per file", matching Python's cross-synthesis trick.
    Stft stft(kFftSize, kHopSize);
    std::array<std::vector<float>, 3> file_avg_magnitudes;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto frames = stft.Analyze(resampled_audio[i]);
        if (frames.empty()) {
            // File is shorter than one STFT window.
            return false;
        }
        file_avg_magnitudes[i] = TimeAverageMagnitude(frames);
    }

    // Step 4: prepare the iFFT instance (reused across all cells).
    RealFft ifft(kFftSize);

    // Step 5: generate 8 pages. Each cell is a weighted sum of the 3
    // per-file magnitude spectra, weights = axis positions + epsilon.
    for (std::size_t z = 0; z < num_pages_; ++z) {
        std::vector<float> page_samples;
        page_samples.reserve(samples_ * kCellsPerPage);

        const float z_weight = static_cast<float>(z) / 7.0f + kWeightEpsilon;

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            const float y_weight = static_cast<float>(y) / 7.0f + kWeightEpsilon;
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                const float x_weight =
                    static_cast<float>(x) / 7.0f + kWeightEpsilon;

                // Weighted sum of the 3 per-file magnitude spectra.
                std::vector<float> combined_mag(kNumBins, 0.0f);
                for (std::size_t k = 0; k < kNumBins; ++k) {
                    combined_mag[k] =
                        x_weight * file_avg_magnitudes[0][k] +
                        y_weight * file_avg_magnitudes[1][k] +
                        z_weight * file_avg_magnitudes[2][k];
                }

                // Zero-phase iFFT → time-domain cell.
                auto cell = ZeroPhaseIfft(combined_mag, ifft);

                // Remove DC.
                float dc_sum = 0.0f;
                for (float s : cell) {
                    dc_sum += s;
                }
                const float dc = dc_sum / static_cast<float>(cell.size());
                for (float& s : cell) {
                    s -= dc;
                }

                // Normalize to peak 1.0 so each cell is individually
                // well-scaled.
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

                page_samples.insert(page_samples.end(), cell.begin(), cell.end());
            }
        }

        // Globally normalize the page.
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

bool ThreeWavGenerator::WritePageToWav(
    const std::filesystem::path& path,
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

Modify `cpp/CMakeLists.txt`. Add `src/dsp/three_wav_generator.cpp` to the `qt_add_executable(fim-config-tool ...)` block after `src/dsp/serum_generator.cpp`:

```cmake
    src/dsp/serum_generator.cpp
    src/dsp/three_wav_generator.cpp
    src/dsp/single_wav_generator.cpp
```

- [ ] **Step 4: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/three_wav_generator.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/three_wav_generator.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/three_wav_generator.h cpp/src/dsp/three_wav_generator.cpp \
        cpp/CMakeLists.txt
git commit -m "feat(cpp): add ThreeWavGenerator (axis-weighted cross-synthesis)"
```

---

## Task 4: ThreeWavService

Thin `GenerateServiceBase` subclass holding a 3-slot array of input paths. No options struct because three-wav has no persistent mode selections.

**Files:**
- Create: `cpp/src/app/services/three_wav_service.h`
- Create: `cpp/src/app/services/three_wav_service.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/app/services/three_wav_service.h`:

```cpp
#pragma once

#include <array>

#include <QString>

#include "app/services/generate_service_base.h"

namespace fim::app {

// Three-wav mode wavetable service. Replaces the single input file from
// GenerateServiceBase with an array of 3 paths. The inherited
// SetInputFile(path) from the base class is unused for three-wav —
// subclass just doesn't call it.
class ThreeWavService : public GenerateServiceBase {
    Q_OBJECT

public:
    explicit ThreeWavService(QObject* parent = nullptr);
    ~ThreeWavService() override = default;

    // Set one of the 3 input file slots (0..2). Replaces any previously-
    // set file at that slot.
    void SetInputFileAt(int slot, const QString& path);
    QString InputFileAt(int slot) const;

    // True when all 3 slots are populated with non-empty paths.
    bool AllFilesSet() const;

protected:
    bool DoGenerate(const std::filesystem::path& input,
                    const std::filesystem::path& output,
                    const ProgressCallback& progress_cb) override;

private:
    std::array<QString, 3> files_;
};

}  // namespace fim::app
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/app/services/three_wav_service.cpp`:

```cpp
#include "app/services/three_wav_service.h"

#include <array>
#include <filesystem>

#include "dsp/three_wav_generator.h"

namespace fim::app {

ThreeWavService::ThreeWavService(QObject* parent) : GenerateServiceBase(parent) {}

void ThreeWavService::SetInputFileAt(int slot, const QString& path) {
    if (slot < 0 || slot >= 3) {
        return;
    }
    files_[slot] = path;
}

QString ThreeWavService::InputFileAt(int slot) const {
    if (slot < 0 || slot >= 3) {
        return QString();
    }
    return files_[slot];
}

bool ThreeWavService::AllFilesSet() const {
    for (const auto& f : files_) {
        if (f.isEmpty()) {
            return false;
        }
    }
    return true;
}

bool ThreeWavService::DoGenerate(const std::filesystem::path& /*input*/,
                                 const std::filesystem::path& output,
                                 const ProgressCallback& progress_cb) {
    // Ignore the base-class input parameter — three-wav uses its own
    // 3-slot array instead. Snapshot it for thread safety.
    std::array<std::filesystem::path, 3> input_paths;
    for (std::size_t i = 0; i < 3; ++i) {
        input_paths[i] = std::filesystem::path(files_[i].toStdString());
    }

    fim::dsp::ThreeWavGenerator generator;
    return generator.Generate(input_paths, output, progress_cb);
}

}  // namespace fim::app
```

- [ ] **Step 3: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/app/services/three_wav_service.cpp` right after `src/app/services/serum_wav_service.cpp`:

```cmake
    src/app/services/serum_wav_service.cpp
    src/app/services/three_wav_service.cpp
    src/app/services/single_wav_service.cpp
```

- [ ] **Step 4: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/three_wav_service.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/three_wav_service.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/three_wav_service.h \
        cpp/src/app/services/three_wav_service.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): add ThreeWavService with 3-slot input file management"
```

---

## Task 5: ThreeWavScreen

The UI screen. 3 file drop slots, no morph mode selectors (they don't exist for this mode), X/Y/Z axis descriptors. Uses `ShowDefaultFilenameRow=false` so the base's single-file row is hidden; manages per-slot filenames and clear buttons inside its own content. Overrides `Reset()` to clear all 3 slots and stay in `kFileSet` instead of returning to `kEmpty`. Jumps straight to `kFileSet` in its constructor since the kEmpty state is never meaningful for three-wav.

**Files:**
- Create: `cpp/src/ui/three_wav_screen.h`
- Create: `cpp/src/ui/three_wav_screen.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/three_wav_screen.h`:

```cpp
#pragma once

#include <array>

#include <QString>

#include "ui/mode_screen_base.h"

class QLabel;
class QPushButton;

namespace fim::app {
class ThreeWavService;
}

namespace fim::ui {

class FileDropWidget;

// The three-wav mode screen. Takes 3 audio files (one per slot, mapped
// to X / Y / Z axes) and blends their time-averaged magnitude spectra
// via ThreeWavGenerator's axis-weighted cross-synthesis algorithm.
//
// Differs from AnyWavScreen / SerumWavScreen in several ways:
//   - No morph mode selectors (three-wav has no morph modes)
//   - 3 file drop slots instead of 1
//   - ShowDefaultFilenameRow() returns false (per-slot filenames inside
//     the content instead of the base's single-file row)
//   - Reset() override clears slots and stays in kFileSet
//   - Constructor jumps straight to kFileSet (no kEmpty state used)
class ThreeWavScreen : public ModeScreenBase {
    Q_OBJECT

public:
    ThreeWavScreen(fim::engine::RealtimeAudioEngine* engine,
                   fim::app::Settings* settings, QWidget* parent = nullptr);

    // Override Reset to clear all 3 slots and stay in kFileSet.
    void Reset() override;

protected:
    QString ModeTitle() const override;
    QWidget* BuildEmptyPageContent(QWidget* parent) override;
    QWidget* BuildFileSetPageContent(QWidget* parent) override;
    fim::app::GenerateServiceBase* Service() override;
    void OnClearHook() override;
    void OnResetHook() override;
    QString OutputDirForPreview() const override;
    bool ShowDefaultFilenameRow() const override { return false; }

private:
    void OnSlotFileDropped(int slot_index, const QString& path);
    void OnSlotClearClicked(int slot_index);
    void RefreshGenerateEnabled();

    fim::app::ThreeWavService* service_ = nullptr;

    std::array<FileDropWidget*, 3> drop_widgets_{nullptr, nullptr, nullptr};
    std::array<QLabel*, 3> filename_labels_{nullptr, nullptr, nullptr};
    QPushButton* generate_button_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/three_wav_screen.cpp`:

```cpp
#include "ui/three_wav_screen.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include "app/services/three_wav_service.h"
#include "app/settings.h"
#include "ui/widgets/file_drop_widget.h"

namespace fim::ui {

namespace {

QString ThreeWavOutputDir() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("three_wav_resynth");
}

}  // namespace

ThreeWavScreen::ThreeWavScreen(fim::engine::RealtimeAudioEngine* engine,
                               fim::app::Settings* settings, QWidget* parent)
    : ModeScreenBase(engine, settings, parent) {
    setObjectName("threeWavScreen");

    service_ = new fim::app::ThreeWavService(this);
    service_->SetOutputDirectory(ThreeWavOutputDir());

    FinishInit();

    // Three-wav skips kEmpty — the file-set page already shows 3 drop
    // widgets so there's no "drop one file first" state. Jump straight
    // to kFileSet after FinishInit has built the pages.
    SetState(State::kFileSet);
}

void ThreeWavScreen::Reset() {
    for (int i = 0; i < 3; ++i) {
        service_->SetInputFileAt(i, QString());
        if (filename_labels_[i] != nullptr) {
            filename_labels_[i]->setText("(no file)");
        }
    }
    RefreshGenerateEnabled();
    // Stay in kFileSet — don't go back to kEmpty.
    SetState(State::kFileSet);
}

QString ThreeWavScreen::ModeTitle() const {
    return "Use three .wav files to create your wavetable bank";
}

QWidget* ThreeWavScreen::BuildEmptyPageContent(QWidget* parent) {
    // Never shown — the constructor jumps straight to kFileSet.
    return new QWidget(parent);
}

QWidget* ThreeWavScreen::BuildFileSetPageContent(QWidget* parent) {
    auto* content = new QWidget(parent);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // 3 file slots, each mapped to an axis. Each slot: axis title,
    // filename label + clear button row, drop widget.
    static const QStringList kSlotTitles{"X axis — file 1", "Y axis — file 2",
                                         "Z axis — file 3"};
    for (int i = 0; i < 3; ++i) {
        auto* slot_title = new QLabel(kSlotTitles[i], content);
        slot_title->setObjectName("anyWavAxisLabel");
        layout->addWidget(slot_title);

        auto* file_row = new QHBoxLayout();
        filename_labels_[i] = new QLabel("(no file)", content);
        filename_labels_[i]->setObjectName("anyWavFilename");
        auto* clear_button = new QPushButton("Clear", content);
        clear_button->setObjectName("clearButton");
        file_row->addWidget(filename_labels_[i]);
        file_row->addStretch();
        file_row->addWidget(clear_button);
        layout->addLayout(file_row);
        connect(clear_button, &QPushButton::clicked, this,
                [this, i]() { OnSlotClearClicked(i); });

        drop_widgets_[i] = new FileDropWidget(content);
        layout->addWidget(drop_widgets_[i]);
        connect(drop_widgets_[i], &FileDropWidget::fileDropped, this,
                [this, i](const QString& path) { OnSlotFileDropped(i, path); });
    }

    auto* generate_row = new QHBoxLayout();
    generate_button_ = new QPushButton("Generate wavetable bank", content);
    generate_button_->setObjectName("generateButton");
    generate_button_->setEnabled(false);
    generate_row->addStretch();
    generate_row->addWidget(generate_button_);
    layout->addLayout(generate_row);
    connect(generate_button_, &QPushButton::clicked, this,
            [this]() { OnGenerateClicked(); });

    return content;
}

fim::app::GenerateServiceBase* ThreeWavScreen::Service() {
    return service_;
}

void ThreeWavScreen::OnClearHook() {
    // Unused — three-wav's ShowDefaultFilenameRow=false means the base
    // class's clear button doesn't exist, so this hook is never called.
}

void ThreeWavScreen::OnResetHook() {
    // Unused — three-wav overrides Reset() directly.
}

QString ThreeWavScreen::OutputDirForPreview() const {
    return ThreeWavOutputDir();
}

void ThreeWavScreen::OnSlotFileDropped(int slot_index, const QString& path) {
    service_->SetInputFileAt(slot_index, path);
    if (filename_labels_[slot_index] != nullptr) {
        filename_labels_[slot_index]->setText(QFileInfo(path).fileName());
    }
    RefreshGenerateEnabled();
}

void ThreeWavScreen::OnSlotClearClicked(int slot_index) {
    service_->SetInputFileAt(slot_index, QString());
    if (filename_labels_[slot_index] != nullptr) {
        filename_labels_[slot_index]->setText("(no file)");
    }
    RefreshGenerateEnabled();
}

void ThreeWavScreen::RefreshGenerateEnabled() {
    if (generate_button_ != nullptr) {
        generate_button_->setEnabled(service_->AllFilesSet());
    }
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/three_wav_screen.cpp` right after `src/ui/serum_wav_screen.cpp`:

```cmake
    src/ui/any_wav_screen.cpp
    src/ui/serum_wav_screen.cpp
    src/ui/three_wav_screen.cpp
```

- [ ] **Step 4: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/three_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/three_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/three_wav_screen.h cpp/src/ui/three_wav_screen.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): add ThreeWavScreen with 3-slot file-drop UI"
```

---

## Task 6: Launcher unlock + MainWindow routing + manual verification

Remove the `kComingSoon` state on the three-wavs card, route the launcher card click to the new screen, and manually verify the flow with 3 real .wav files.

**Files:**
- Modify: `cpp/src/ui/launcher_screen.cpp`
- Modify: `cpp/src/ui/main_window.h`
- Modify: `cpp/src/ui/main_window.cpp`
- Modify: `docs/followups.md`

- [ ] **Step 1: Unlock the three-wavs card**

Modify `cpp/src/ui/launcher_screen.cpp`. Change the three-wavs card construction and wire its signal:

```cpp
    three_wavs_card_ =
        new CardButton("Use three .wav files to create your wavetable bank",
                       CardButton::State::kDefault, this);

    cards_row->addWidget(any_wav_card_);
    cards_row->addWidget(serum_card_);
    cards_row->addWidget(three_wavs_card_);
    root_layout->addLayout(cards_row);
    root_layout->addStretch();

    connect(any_wav_card_, &CardButton::chosen, this,
            [this]() { emit modeChosen(Mode::kAnyWav); });
    connect(serum_card_, &CardButton::chosen, this,
            [this]() { emit modeChosen(Mode::kSerum); });
    connect(three_wavs_card_, &CardButton::chosen, this,
            [this]() { emit modeChosen(Mode::kThreeWavs); });
```

- [ ] **Step 2: Add ThreeWavScreen to MainWindow**

Modify `cpp/src/ui/main_window.h`. Forward-declare and add the member:

```cpp
namespace fim::ui {

class AnyWavScreen;
class LauncherScreen;
class SerumWavScreen;
class ThreeWavScreen;
```

And in the private section, after `serum_wav_screen_`:

```cpp
    SerumWavScreen* serum_wav_screen_ = nullptr;
    ThreeWavScreen* three_wav_screen_ = nullptr;
    int launcher_index_ = -1;
    int any_wav_index_ = -1;
    int serum_wav_index_ = -1;
    int three_wav_index_ = -1;
};
```

- [ ] **Step 3: Wire up ThreeWavScreen in MainWindow.cpp**

Modify `cpp/src/ui/main_window.cpp`. Add the include at the top:

```cpp
#include "ui/three_wav_screen.h"
```

After constructing `serum_wav_screen_`, construct the three-wav screen and add it to the stack:

```cpp
    any_wav_screen_ = new AnyWavScreen(engine_.get(), &settings_, this);
    serum_wav_screen_ = new SerumWavScreen(engine_.get(), &settings_, this);
    three_wav_screen_ = new ThreeWavScreen(engine_.get(), &settings_, this);

    launcher_index_ = stack_->addWidget(launcher_screen_);
    any_wav_index_ = stack_->addWidget(any_wav_screen_);
    serum_wav_index_ = stack_->addWidget(serum_wav_screen_);
    three_wav_index_ = stack_->addWidget(three_wav_screen_);
```

Wire the three-wav `backRequested` signal after the Serum one:

```cpp
    connect(serum_wav_screen_, &SerumWavScreen::backRequested, this,
            &MainWindow::OnBackToLauncher);
    connect(three_wav_screen_, &ThreeWavScreen::backRequested, this,
            &MainWindow::OnBackToLauncher);
```

And update `OnModeChosen` to handle `kThreeWavs`:

```cpp
void MainWindow::OnModeChosen(int mode) {
    const auto m = static_cast<LauncherScreen::Mode>(mode);
    if (m == LauncherScreen::Mode::kAnyWav) {
        any_wav_screen_->Reset();
        stack_->setCurrentIndex(any_wav_index_);
    } else if (m == LauncherScreen::Mode::kSerum) {
        serum_wav_screen_->Reset();
        stack_->setCurrentIndex(serum_wav_index_);
    } else if (m == LauncherScreen::Mode::kThreeWavs) {
        three_wav_screen_->Reset();
        stack_->setCurrentIndex(three_wav_index_);
    }
}
```

Remove the stale `// kThreeWavs is permanently disabled in the launcher.` comment.

- [ ] **Step 4: Remove the stale followups entry**

Modify `docs/followups.md`. Delete:

```markdown
- **Three-wavs mode is permanently `kComingSoon`.** Will be revisited only if there's user demand.
```

- [ ] **Step 5: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Run the app and verify end-to-end**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Walk through the three-wav flow:

1. Launcher: verify the three-wavs card is clickable (says "Choose", not "Coming soon!"). Click it.
2. Screen transitions to `ThreeWavScreen`. Verify:
   - "← Back" button
   - Title: "Use three .wav files to create your wavetable bank"
   - No default filename row (base's single-file row is suppressed)
   - **Three** file slots, each with: "X axis / Y axis / Z axis — file N" title, "(no file)" filename row with Clear button, drop widget below
   - NO Y/Z morph mode selectors
   - "Generate wavetable bank" button — **disabled** (grayed out)
3. Drop a .wav file into the X slot. Filename appears. Generate stays disabled.
4. Drop a second .wav file into the Y slot. Generate stays disabled.
5. Drop a third .wav file into the Z slot. Generate **becomes enabled**.
6. (Optional) Test the sample rate mismatch case: drop files with different sample rates (e.g. a 44.1k and a 48k and a 22.05k). Expect Generate to succeed silently — libsamplerate resamples them all to 44.1k. No error dialog.
7. Click Clear on the Y slot. Generate disables. Re-drop a file. Generate re-enables.
8. Click Generate. Progress bar fills. "Done!" then preview panel.
9. Click Play — **listen for audible cross-synthesis output** that blends the 3 files. The output should have a bright, pitched character (zero-phase iFFT gives a "sum of cosines" aesthetic).
10. Drag X / Y / Z sliders in the preview. At corners like (7, 0, 0) you should hear file X dominating; at (7, 7, 7) all 3 files blended; at (0, 0, 0) a soft uniform mix (because of the ε floor).
11. Back to launcher → re-enter three-wav → verify all 3 slots are empty (Reset cleared them). Y/Z not applicable since there are no morph mode selectors to restore.
12. Test mode isolation: generate in any-wav mode, switch to three-wav, generate there, switch to Serum, generate. Each mode's preview should load its own output (`audio_resynth` / `three_wav_resynth` / `serum_resynth`).

If any step fails, likely culprits:
- `ResampleTo` failing silently on edge cases → check that the per-file resample succeeds
- `TimeAverageMagnitude` producing zeros if an STFT frame has unexpected shape
- `ZeroPhaseIfft` producing NaN if the combined magnitude has negative values (shouldn't — they're sums of positive magnitudes + positive weights, but worth checking)
- `ShowDefaultFilenameRow=false` not taking effect (you'd see a stray single filename row at the top)
- `Reset()` override not working (screen goes to kEmpty placeholder instead of staying in kFileSet)

- [ ] **Step 7: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/launcher_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/launcher_screen.cpp cpp/src/ui/main_window.h \
        cpp/src/ui/main_window.cpp docs/followups.md
git commit -m "feat(cpp): unlock three-wav launcher card and route to ThreeWavScreen"
```

---

## Task 7: Push and verify CI

Push the branch and verify explicitly with `gh run view`. Never trust the background `gh run watch` exit code alone.

- [ ] **Step 1: Push the branch**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
```

- [ ] **Step 2: Watch CI**

```bash
sleep 5
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId,status --jq '.[0]'
# copy the returned id:
gh run watch <id> --exit-status
```

- [ ] **Step 3: VERIFY CI status explicitly**

```bash
gh run view <id>
```

Expected: all four jobs ✓ — clang-format, macos-latest, ubuntu-latest, windows-latest. Phase 4c is done when all green.

---

## Phase 4c done when:

1. ✅ `fim::dsp::ResampleTo` wraps libsamplerate and resamples arbitrary-rate input to target
2. ✅ `fim::dsp::ThreeWavGenerator` loads 3 files, resamples each to 44.1 kHz, time-averages magnitudes, and produces 8 WAV pages via axis-weighted zero-phase iFFT
3. ✅ `fim::app::ThreeWavService` manages 3-slot input file state
4. ✅ `fim::ui::ThreeWavScreen` shows 3 drop slots with per-slot filenames, Generate-disabled-until-full, no morph mode selectors
5. ✅ `ModeScreenBase::Reset` is virtual, `SetState` is protected, `ShowDefaultFilenameRow` virtual exists
6. ✅ Any-wav and Serum still work (no regression from base class tweaks)
7. ✅ Launcher three-wavs card is clickable and routes to the new screen
8. ✅ Manual verification: dropping 3 .wav files (any rates), clicking Generate, and playing produces audible cross-synthesis output
9. ✅ Files with mismatched sample rates generate successfully (no error dialog)
10. ✅ All three modes use separate output directories (`audio_resynth`, `serum_resynth`, `three_wav_resynth`)
11. ✅ `docs/followups.md` no longer mentions three-wavs `kComingSoon`
12. ✅ All 77 Catch2 tests pass locally
13. ✅ CI green on macOS, Ubuntu, Windows, and clang-format

After Phase 4c ships, all three wavetable modes are feature-complete. The three-wav algorithm (Option D) is a v1 choice expected to iterate — see `~/.claude/projects/.../memory/reference_three_wav_approaches.md` for the 6 alternative approaches documented during brainstorming.
