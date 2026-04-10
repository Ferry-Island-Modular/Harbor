# Phase 3a Implementation Plan — DSP infrastructure (audio I/O + STFT)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lay the foundational DSP layer in `cpp/src/dsp/` — a WAV loader for input files, a Hann window utility, a low-level PFFFT wrapper for real FFTs, and an STFT analysis class. Pure C++, no Qt, no semantic DSP yet. Phase 3b will build the actual single-WAV wavetable generator on top of these pieces.

**Architecture:** Four independent units in a new `fim::dsp` namespace, each testable in isolation with Catch2. `WavLoader` (dr_wav + mono mix + optional peak normalization, no resampling); `HannWindow` (simple symmetric Hann); `RealFft` (wraps PFFFT with standard `std::complex<float>` bin arrays in/out, handles PFFFT's packed format internally); `Stft` (frame-by-frame windowed FFT using `RealFft` + `HannWindow`, matches `scipy.signal.stft(boundary=None, padded=False)`). No existing code is modified except `CMakeLists.txt`.

**Tech Stack:** C++20, PFFFT (already vendored at `cpp/third_party/pffft/`), dr_wav (already vendored at `cpp/third_party/dr_libs/`), Catch2 v3 for tests. No new dependencies.

**Spec deviations from Python reference:**

- **Hann window is symmetric (`sym=True`).** The Python code passes `windows.hann(fft_size)` which defaults to symmetric. scipy's STFT function *also* auto-generates periodic windows when given a string name, but the Python code's explicit `windows.hann(fft_size)` call bypasses that. Phase 3a matches the explicit call.
- **STFT has no boundary handling.** `scipy.signal.stft` defaults to `boundary='zeros', padded=True`, which pads the signal with zeros on both ends so the first and last frames have a window-centered alignment. Phase 3a uses `boundary=None, padded=False` — simpler, fewer frames, no center alignment. Phase 3c may revisit if oracle parity testing demands scipy's default behavior.
- **No input resampling.** The Python single-WAV path (`_analyze_audio`) uses `scipy.io.wavfile.read` which returns samples at the file's native sample rate and does NOT resample. Phase 3a `WavLoader` does the same. Phase 3b will decide whether/how to force everything to 44100 Hz.
- **`RealFft::Inverse` does NOT normalize by 1/N.** Matches PFFFT / FFTW convention. Callers are responsible for scaling if they want the true mathematical inverse. This keeps the wrapper thin and avoids a hidden multiply that may not be what the caller wants.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files:**

- `cpp/src/dsp/window.h` / `window.cpp` — `fim::dsp::HannWindow(size_t length) -> std::vector<float>`. Symmetric Hann window generator. Header-only would be fine but a .cpp is included for consistency with the other DSP pieces and so the test target doesn't need special include wiring.
- `cpp/src/dsp/wav_loader.h` / `wav_loader.cpp` — `fim::dsp::LoadWav(path, normalize)` returning `std::optional<LoadedAudio>` with `{samples (mono), sample_rate, original_channels}`. Uses `dr_wav` for parsing. Mixes multichannel down to mono by averaging. Optionally divides by peak magnitude.
- `cpp/src/dsp/real_fft.h` / `real_fft.cpp` — `fim::dsp::RealFft` class. Wraps PFFFT. Fixed `fft_size` per instance. `Forward(real*, complex*)` and `Inverse(complex*, real*)` in/out standard formats. Internally owns aligned scratch buffers and handles PFFFT's packed DC/Nyquist format.
- `cpp/src/dsp/stft.h` / `stft.cpp` — `fim::dsp::Stft` class with `Analyze(audio)` plus free-function helpers `Magnitude(bins)` and `Phase(bins)`.
- `cpp/tests/dsp_window_test.cpp` — 3 tests: length, endpoints zero, known midpoint value.
- `cpp/tests/dsp_wav_loader_test.cpp` — 5 tests: round-trip a known WAV, stereo→mono mixing, normalization on/off, sample_rate preserved, nonexistent file returns nullopt.
- `cpp/tests/dsp_real_fft_test.cpp` — 4 tests: DC-only signal transform, pure sine transform, round-trip forward→inverse, impulse transform.
- `cpp/tests/dsp_stft_test.cpp` — 4 tests: NumFrames formula, audio shorter than fft_size produces zero frames, single-frame audio produces one frame, pure sine concentrates energy at expected bin.

**Modified files:**

- `cpp/CMakeLists.txt` — add 4 new DSP source files to the `fim-config-tool` target.
- `cpp/tests/CMakeLists.txt` — add 4 new test files and their implementation dependencies to the `fim-tests` target; add `pffft` and `dr_wav_headers` (already linked) to the DSP source compilation.

**Deleted files:** none.

---

## Task 1: HannWindow utility

A symmetric Hann window generator. Free function, no class. Simplest of the four Phase 3a pieces — tackle it first so later tasks can depend on it.

**Files:**
- Create: `cpp/src/dsp/window.h`
- Create: `cpp/src/dsp/window.cpp`
- Create: `cpp/tests/dsp_window_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_window_test.cpp`:

```cpp
#include "dsp/window.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("HannWindow has the requested length", "[dsp][window]") {
    const auto w = fim::dsp::HannWindow(2048);
    REQUIRE(w.size() == 2048);
}

TEST_CASE("HannWindow endpoints are zero (symmetric definition)",
          "[dsp][window]") {
    const auto w = fim::dsp::HannWindow(2048);
    REQUIRE_THAT(w.front(), WithinAbs(0.0f, 1e-6));
    REQUIRE_THAT(w.back(), WithinAbs(0.0f, 1e-6));
}

TEST_CASE("HannWindow peak value matches scipy.signal.windows.hann(2048)",
          "[dsp][window]") {
    // scipy.signal.windows.hann(2048) peak is between index 1023 and 1024.
    // At index 1023 the value is 0.5 * (1 - cos(2*pi*1023/2047)) ≈ 0.9999988...
    const auto w = fim::dsp::HannWindow(2048);
    const float expected_1023 =
        0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * 1023.0f / 2047.0f));
    REQUIRE_THAT(w[1023], WithinAbs(expected_1023, 1e-6));
}

TEST_CASE("HannWindow length 1 is a single zero", "[dsp][window]") {
    // Degenerate case: N=1. (N-1)=0 in the denominator would divide by zero,
    // so the implementation must special-case it. scipy returns [1.0] for
    // hann(1) but arguably [0.0] is more consistent with the symmetric
    // formula. Either is acceptable — this test just asserts it doesn't
    // crash or produce NaN.
    const auto w = fim::dsp::HannWindow(1);
    REQUIRE(w.size() == 1);
    REQUIRE(std::isfinite(w[0]));
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`. Update the `add_executable(fim-tests ...)` block to include the new test source and the new DSP source:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    stub_bank_writer_test.cpp
    dsp_window_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
    ../src/app/services/stub_bank_writer.cpp
    ../src/dsp/window.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/window.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/window.h`:

```cpp
#pragma once

#include <cstddef>
#include <vector>

namespace fim::dsp {

// Generates a symmetric Hann window of length `length`. Matches
// scipy.signal.windows.hann(length) with its default sym=True parameter.
//
// Formula: w[n] = 0.5 * (1 - cos(2*pi*n / (length - 1))) for n in [0, length).
//
// Notes:
// - For length == 0, returns an empty vector.
// - For length == 1, returns {0.0f} — the formula would divide by zero, so
//   this edge case is handled explicitly. (scipy returns {1.0f} for this;
//   we choose 0.0f because the window is only ever used with length >> 1
//   and the choice is irrelevant in practice.)
// - The peak value is at the middle of the window. For even lengths, the
//   peak is between indices (length/2 - 1) and (length/2), neither exactly
//   1.0. This is the standard symmetric Hann definition.
std::vector<float> HannWindow(std::size_t length);

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/window.cpp`:

```cpp
#include "dsp/window.h"

#include <cmath>

namespace fim::dsp {

std::vector<float> HannWindow(std::size_t length) {
    if (length == 0) {
        return {};
    }
    if (length == 1) {
        return {0.0f};
    }
    std::vector<float> w(length);
    const float denom = static_cast<float>(length - 1);
    constexpr float kTwoPi = 6.28318530717958647692f;
    for (std::size_t n = 0; n < length; ++n) {
        w[n] = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(n) / denom));
    }
    return w;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block to include `src/dsp/window.cpp`:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/app/services/stub_bank_writer.cpp
    src/app/services/single_wav_service.cpp
    src/dsp/window.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/main_window.cpp
    src/ui/launcher_screen.cpp
    src/ui/any_wav_screen.cpp
    src/ui/widgets/card_button.cpp
    src/ui/widgets/file_drop_widget.cpp
    src/ui/widgets/axis_morph_selector.cpp
    src/ui/widgets/custom_progress_bar.cpp
    src/ui/widgets/preview_controls_widget.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 24 tests pass (20 from Phase 2 + 4 new Hann window tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/window.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_window_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/window.h cpp/src/dsp/window.cpp cpp/tests/dsp_window_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add Hann window generator in fim::dsp"
```

---

## Task 2: WavLoader

Loads a WAV file from disk via `dr_wav`, mixes to mono if needed, optionally normalizes by peak. Preserves the file's native sample rate — does NOT resample. Matches the Python single-WAV path's `_analyze_audio` behavior, which also uses native sample rate.

**Files:**
- Create: `cpp/src/dsp/wav_loader.h`
- Create: `cpp/src/dsp/wav_loader.cpp`
- Create: `cpp/tests/dsp_wav_loader_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_wav_loader_test.cpp`:

```cpp
#include "dsp/wav_loader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "dr_wav.h"

using Catch::Matchers::WithinAbs;

namespace {

// Writes a small test WAV file using dr_wav directly. Returns the path.
// Used by fixtures to produce known inputs for the loader tests.
std::filesystem::path WriteTestWavMono(const std::string& basename,
                                       const std::vector<float>& samples,
                                       uint32_t sample_rate) {
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    return path;
}

std::filesystem::path WriteTestWavStereo(const std::string& basename,
                                         const std::vector<float>& left,
                                         const std::vector<float>& right,
                                         uint32_t sample_rate) {
    REQUIRE(left.size() == right.size());
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 2;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    std::vector<float> interleaved(left.size() * 2);
    for (std::size_t i = 0; i < left.size(); ++i) {
        interleaved[2 * i] = left[i];
        interleaved[2 * i + 1] = right[i];
    }
    drwav_write_pcm_frames(&wav, left.size(), interleaved.data());
    drwav_uninit(&wav);
    return path;
}

class WavLoaderFixture {
public:
    ~WavLoaderFixture() {
        for (const auto& p : to_cleanup_) {
            std::filesystem::remove(p);
        }
    }
    void Track(const std::filesystem::path& p) { to_cleanup_.push_back(p); }

private:
    std::vector<std::filesystem::path> to_cleanup_;
};

}  // namespace

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav round-trips a mono file",
                 "[dsp][wav_loader]") {
    const std::vector<float> input{0.25f, 0.5f, 0.75f, -0.5f, -0.25f};
    const auto path = WriteTestWavMono("fim_wav_loader_mono", input, 48000);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/false);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->sample_rate == 48000);
    REQUIRE(loaded->original_channels == 1);
    REQUIRE(loaded->samples.size() == input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE_THAT(loaded->samples[i], WithinAbs(input[i], 1e-6));
    }
}

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav mixes stereo down to mono",
                 "[dsp][wav_loader]") {
    const std::vector<float> left{1.0f, 0.5f, 0.0f, -0.5f};
    const std::vector<float> right{0.0f, 0.5f, 1.0f, 0.5f};
    const auto path = WriteTestWavStereo("fim_wav_loader_stereo", left, right, 44100);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/false);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->sample_rate == 44100);
    REQUIRE(loaded->original_channels == 2);
    REQUIRE(loaded->samples.size() == left.size());
    // Mono = average of left and right.
    REQUIRE_THAT(loaded->samples[0], WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(loaded->samples[1], WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(loaded->samples[2], WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(loaded->samples[3], WithinAbs(0.0f, 1e-6));
}

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav normalizes to peak 1.0",
                 "[dsp][wav_loader]") {
    const std::vector<float> input{0.1f, -0.2f, 0.4f, -0.05f};
    const auto path = WriteTestWavMono("fim_wav_loader_norm", input, 44100);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/true);
    REQUIRE(loaded.has_value());

    float peak = 0.0f;
    for (float s : loaded->samples) {
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE_THAT(peak, WithinAbs(1.0f, 1e-6));

    // Samples should be scaled proportionally: input peak is 0.4, so the
    // output at the peak index should be 1.0 (or -1.0) and others scaled.
    REQUIRE_THAT(loaded->samples[2], WithinAbs(1.0f, 1e-6));
    REQUIRE_THAT(loaded->samples[0], WithinAbs(0.25f, 1e-6));
}

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav preserves exact values when not normalizing",
                 "[dsp][wav_loader]") {
    const std::vector<float> input{0.1f, 0.2f, 0.3f};
    const auto path = WriteTestWavMono("fim_wav_loader_exact", input, 22050);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/false);
    REQUIRE(loaded.has_value());
    REQUIRE_THAT(loaded->samples[0], WithinAbs(0.1f, 1e-6));
    REQUIRE_THAT(loaded->samples[1], WithinAbs(0.2f, 1e-6));
    REQUIRE_THAT(loaded->samples[2], WithinAbs(0.3f, 1e-6));
}

TEST_CASE("LoadWav returns nullopt for a nonexistent file", "[dsp][wav_loader]") {
    const auto result =
        fim::dsp::LoadWav("/tmp/this_path_definitely_does_not_exist_xyz_12345.wav");
    REQUIRE_FALSE(result.has_value());
}
```

- [ ] **Step 2: Add the test and DSP source to the test target**

Modify `cpp/tests/CMakeLists.txt`. Update the `add_executable(fim-tests ...)` block to add `dsp_wav_loader_test.cpp` and `../src/dsp/wav_loader.cpp`:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    stub_bank_writer_test.cpp
    dsp_window_test.cpp
    dsp_wav_loader_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
    ../src/app/services/stub_bank_writer.cpp
    ../src/dsp/window.cpp
    ../src/dsp/wav_loader.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/wav_loader.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/wav_loader.h`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace fim::dsp {

// A loaded mono audio buffer plus metadata about the source file. Samples
// are always float in [-1, 1] (pre-normalize) or scaled to [-1, 1] peak if
// normalization was requested.
struct LoadedAudio {
    std::vector<float> samples;       // mono
    std::uint32_t sample_rate = 0;    // file's native rate, unresampled
    std::uint32_t original_channels = 0;  // channels in the source file
};

// Load a WAV file from disk via dr_wav. If the source is multichannel, all
// channels are averaged into a single mono channel. If `normalize` is true,
// the samples are scaled so the maximum absolute value is exactly 1.0 (or
// left at zero if the file is silent). The file's native sample rate is
// preserved — no resampling is performed.
//
// Returns std::nullopt if the file cannot be opened or parsed.
//
// This loader is a DSP-layer input loader. It is distinct from the engine
// layer's `fim::engine::LoadWavMono` (which reads previously-generated
// output banks for playback) and returns richer metadata.
std::optional<LoadedAudio> LoadWav(const std::filesystem::path& path,
                                   bool normalize = true);

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/wav_loader.cpp`:

```cpp
#include "dsp/wav_loader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "dr_wav.h"

namespace fim::dsp {

std::optional<LoadedAudio> LoadWav(const std::filesystem::path& path, bool normalize) {
    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drwav_uint64 total_frames = 0;
    float* raw = drwav_open_file_and_read_pcm_frames_f32(
        path.string().c_str(), &channels, &sample_rate, &total_frames, nullptr);
    if (raw == nullptr) {
        return std::nullopt;
    }

    LoadedAudio result;
    result.sample_rate = sample_rate;
    result.original_channels = channels;
    result.samples.resize(static_cast<std::size_t>(total_frames));

    // Mix down to mono by averaging all channels.
    for (drwav_uint64 frame = 0; frame < total_frames; ++frame) {
        float sum = 0.0f;
        for (unsigned int ch = 0; ch < channels; ++ch) {
            sum += raw[frame * channels + ch];
        }
        result.samples[static_cast<std::size_t>(frame)] =
            sum / static_cast<float>(channels);
    }

    drwav_free(raw, nullptr);

    if (normalize && !result.samples.empty()) {
        float peak = 0.0f;
        for (float s : result.samples) {
            peak = std::max(peak, std::abs(s));
        }
        if (peak > 0.0f) {
            const float scale = 1.0f / peak;
            for (float& s : result.samples) {
                s *= scale;
            }
        }
    }

    return result;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block to include `src/dsp/wav_loader.cpp` right after `src/dsp/window.cpp`:

```cmake
    src/dsp/window.cpp
    src/dsp/wav_loader.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 29 tests pass (24 from after Task 1 + 5 new WavLoader tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/wav_loader.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/wav_loader.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_wav_loader_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/wav_loader.h cpp/src/dsp/wav_loader.cpp cpp/tests/dsp_wav_loader_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add DSP-layer WAV loader with mono mix and normalize"
```

---

## Task 3: RealFft

Wraps PFFFT's real FFT transform. Fixed `fft_size` per instance. Owns aligned scratch buffers internally so callers can pass plain `std::complex<float>*` and `float*`. Handles PFFFT's packed DC/Nyquist format under the hood.

**Files:**
- Create: `cpp/src/dsp/real_fft.h`
- Create: `cpp/src/dsp/real_fft.cpp`
- Create: `cpp/tests/dsp_real_fft_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_real_fft_test.cpp`. The tests use fft_size=64 (the smallest PFFFT supports for real transforms is 32; 64 gives us a few extra bins for peak detection and is still fast).

```cpp
#include "dsp/real_fft.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {
constexpr std::size_t kTestN = 64;
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

TEST_CASE("RealFft forward transforms a DC signal to bin 0 only",
          "[dsp][real_fft]") {
    fim::dsp::RealFft fft(kTestN);
    REQUIRE(fft.fft_size() == kTestN);
    REQUIRE(fft.num_bins() == kTestN / 2 + 1);

    std::vector<float> input(kTestN, 1.0f);
    std::vector<std::complex<float>> bins(fft.num_bins());
    fft.Forward(input.data(), bins.data());

    // Unnormalized FFT: DC bin magnitude = N for all-ones input.
    REQUIRE_THAT(bins[0].real(), WithinAbs(static_cast<float>(kTestN), 1e-3));
    REQUIRE_THAT(bins[0].imag(), WithinAbs(0.0f, 1e-4));
    for (std::size_t k = 1; k < fft.num_bins(); ++k) {
        REQUIRE_THAT(std::abs(bins[k]), WithinAbs(0.0f, 1e-3));
    }
}

TEST_CASE("RealFft forward transforms an impulse to a flat spectrum",
          "[dsp][real_fft]") {
    fim::dsp::RealFft fft(kTestN);
    std::vector<float> input(kTestN, 0.0f);
    input[0] = 1.0f;
    std::vector<std::complex<float>> bins(fft.num_bins());
    fft.Forward(input.data(), bins.data());

    // Impulse at t=0 → every bin has magnitude 1.
    for (std::size_t k = 0; k < fft.num_bins(); ++k) {
        REQUIRE_THAT(std::abs(bins[k]), WithinAbs(1.0f, 1e-4));
    }
}

TEST_CASE("RealFft forward concentrates a pure sine at its frequency bin",
          "[dsp][real_fft]") {
    fim::dsp::RealFft fft(kTestN);
    // Place a sine at exactly bin 4. Period = N/4 = 16 samples.
    // Phase is sin(2*pi*4*n/N) so bin 4 should have magnitude N/2.
    constexpr std::size_t kBin = 4;
    std::vector<float> input(kTestN);
    for (std::size_t n = 0; n < kTestN; ++n) {
        input[n] = std::sin(2.0f * kPi * kBin * n / kTestN);
    }
    std::vector<std::complex<float>> bins(fft.num_bins());
    fft.Forward(input.data(), bins.data());

    const float expected_peak_mag = static_cast<float>(kTestN) / 2.0f;
    REQUIRE_THAT(std::abs(bins[kBin]), WithinAbs(expected_peak_mag, 1e-2));
    // Other bins should be approximately zero.
    for (std::size_t k = 0; k < fft.num_bins(); ++k) {
        if (k == kBin) continue;
        REQUIRE_THAT(std::abs(bins[k]), WithinAbs(0.0f, 1e-2));
    }
}

TEST_CASE("RealFft forward then inverse recovers the original (modulo 1/N)",
          "[dsp][real_fft]") {
    fim::dsp::RealFft fft(kTestN);
    // Arbitrary test signal.
    std::vector<float> original(kTestN);
    for (std::size_t n = 0; n < kTestN; ++n) {
        original[n] = std::sin(2.0f * kPi * 3.0f * n / kTestN) +
                      0.5f * std::cos(2.0f * kPi * 7.0f * n / kTestN);
    }

    std::vector<std::complex<float>> bins(fft.num_bins());
    fft.Forward(original.data(), bins.data());

    std::vector<float> recovered(kTestN, 0.0f);
    fft.Inverse(bins.data(), recovered.data());

    // PFFFT inverse is unnormalized; scale by 1/N.
    const float inv_n = 1.0f / static_cast<float>(kTestN);
    for (std::size_t n = 0; n < kTestN; ++n) {
        REQUIRE_THAT(recovered[n] * inv_n, WithinAbs(original[n], 1e-4));
    }
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    stub_bank_writer_test.cpp
    dsp_window_test.cpp
    dsp_wav_loader_test.cpp
    dsp_real_fft_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
    ../src/app/services/stub_bank_writer.cpp
    ../src/dsp/window.cpp
    ../src/dsp/wav_loader.cpp
    ../src/dsp/real_fft.cpp
)
```

The `pffft` library is already a link target for `fim-tests` (it's in the existing `target_link_libraries` block), so no changes there.

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/real_fft.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/real_fft.h`:

```cpp
#pragma once

#include <complex>
#include <cstddef>
#include <memory>

namespace fim::dsp {

// Real-input FFT backed by PFFFT. A single instance is tied to one transform
// size; construct once and reuse across many transforms to amortize PFFFT's
// setup cost.
//
// Conventions:
// - `Forward` takes `fft_size()` real samples and produces `num_bins()`
//   complex bins in standard order (DC at index 0, Nyquist at fft_size/2,
//   positive frequencies in between).
// - `Inverse` takes `num_bins()` complex bins in the same standard order and
//   produces `fft_size()` real samples. The result is NOT scaled by 1/N.
//   Callers that want the true mathematical inverse must divide by fft_size.
// - The caller's buffers may be any alignment — internal PFFFT calls go
//   through aligned scratch buffers owned by this class.
//
// PFFFT supports real FFT sizes that are multiples of 32 with factors only
// {2, 3, 5}. 2048 and 64 (used by tests) are both supported.
class RealFft {
public:
    explicit RealFft(std::size_t fft_size);
    ~RealFft();

    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;
    RealFft(RealFft&&) = delete;
    RealFft& operator=(RealFft&&) = delete;

    std::size_t fft_size() const { return fft_size_; }
    std::size_t num_bins() const { return fft_size_ / 2 + 1; }

    // Forward transform. `input` must point to fft_size() real samples.
    // `output` must point to num_bins() std::complex<float> slots.
    void Forward(const float* input, std::complex<float>* output) const;

    // Inverse transform. `input` must point to num_bins() complex bins.
    // `output` must point to fft_size() real samples. The result is
    // UNNORMALIZED — divide by fft_size() if you want the mathematical
    // inverse.
    void Inverse(const std::complex<float>* input, float* output) const;

private:
    struct Impl;
    std::size_t fft_size_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/real_fft.cpp`:

```cpp
#include "dsp/real_fft.h"

#include <cassert>
#include <cstring>

#include "pffft.h"

namespace fim::dsp {

// PFFFT's real FFT output (via pffft_transform_ordered) is packed as:
//   [dc_real, nyquist_real, real_1, imag_1, real_2, imag_2, ...,
//    real_{N/2-1}, imag_{N/2-1}]
// for a total of N floats (where N = fft_size). DC and Nyquist have no
// imaginary component so they share the first two slots. We translate this
// to a standard std::complex<float> array of length N/2+1 for callers.

struct RealFft::Impl {
    PFFFT_Setup* setup = nullptr;
    float* pack_buffer = nullptr;  // N floats, packed PFFFT format
    float* work_buffer = nullptr;  // N floats, PFFFT work area
    std::size_t n;

    explicit Impl(std::size_t fft_size) : n(fft_size) {
        setup = pffft_new_setup(static_cast<int>(n), PFFFT_REAL);
        assert(setup != nullptr && "PFFFT does not support this fft_size");
        pack_buffer = static_cast<float*>(pffft_aligned_malloc(n * sizeof(float)));
        work_buffer = static_cast<float*>(pffft_aligned_malloc(n * sizeof(float)));
    }

    ~Impl() {
        if (work_buffer) pffft_aligned_free(work_buffer);
        if (pack_buffer) pffft_aligned_free(pack_buffer);
        if (setup) pffft_destroy_setup(setup);
    }
};

RealFft::RealFft(std::size_t fft_size)
    : fft_size_(fft_size), impl_(std::make_unique<Impl>(fft_size)) {}

RealFft::~RealFft() = default;

void RealFft::Forward(const float* input, std::complex<float>* output) const {
    // Step 1: copy caller's input into aligned scratch (can't use `input`
    // directly — it may be unaligned).
    std::memcpy(impl_->pack_buffer, input, fft_size_ * sizeof(float));

    // Step 2: in-place-friendly transform. We reuse `pack_buffer` as both
    // input and output to PFFFT — the API supports that when work_buffer is
    // provided and distinct.
    pffft_transform_ordered(impl_->setup, impl_->pack_buffer, impl_->pack_buffer,
                            impl_->work_buffer, PFFFT_FORWARD);

    // Step 3: unpack PFFFT's format into standard std::complex bins.
    const std::size_t half = fft_size_ / 2;
    output[0] = std::complex<float>(impl_->pack_buffer[0], 0.0f);       // DC
    output[half] = std::complex<float>(impl_->pack_buffer[1], 0.0f);    // Nyquist
    for (std::size_t k = 1; k < half; ++k) {
        output[k] = std::complex<float>(impl_->pack_buffer[2 * k],
                                        impl_->pack_buffer[2 * k + 1]);
    }
}

void RealFft::Inverse(const std::complex<float>* input, float* output) const {
    // Step 1: pack the standard complex bins into PFFFT's packed format.
    const std::size_t half = fft_size_ / 2;
    impl_->pack_buffer[0] = input[0].real();     // DC
    impl_->pack_buffer[1] = input[half].real();  // Nyquist
    for (std::size_t k = 1; k < half; ++k) {
        impl_->pack_buffer[2 * k] = input[k].real();
        impl_->pack_buffer[2 * k + 1] = input[k].imag();
    }

    // Step 2: inverse transform. Again in-place on pack_buffer.
    pffft_transform_ordered(impl_->setup, impl_->pack_buffer, impl_->pack_buffer,
                            impl_->work_buffer, PFFFT_BACKWARD);

    // Step 3: copy the aligned scratch to the caller's unaligned output.
    std::memcpy(output, impl_->pack_buffer, fft_size_ * sizeof(float));
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block to include `src/dsp/real_fft.cpp` right after `src/dsp/wav_loader.cpp`:

```cmake
    src/dsp/window.cpp
    src/dsp/wav_loader.cpp
    src/dsp/real_fft.cpp
```

The main executable already links `pffft` via its existing `target_link_libraries(fim-config-tool PRIVATE ... pffft ...)` block, so no link changes are needed.

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 33 tests pass (29 from after Task 2 + 4 new RealFft tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/real_fft.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/real_fft.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_real_fft_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/real_fft.h cpp/src/dsp/real_fft.cpp cpp/tests/dsp_real_fft_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add RealFft PFFFT wrapper with aligned scratch"
```

---

## Task 4: Stft

The frame-by-frame windowed short-time Fourier transform. Built on top of `RealFft` and `HannWindow`. Produces a 2D array of complex bins (one row per frame, one column per bin). Provides free-function helpers to derive magnitude and phase from the complex bin output.

**Files:**
- Create: `cpp/src/dsp/stft.h`
- Create: `cpp/src/dsp/stft.cpp`
- Create: `cpp/tests/dsp_stft_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_stft_test.cpp`:

```cpp
#include "dsp/stft.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {
constexpr std::size_t kFft = 64;
constexpr std::size_t kHop = 32;
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

TEST_CASE("Stft NumFrames returns 0 for audio shorter than fft_size",
          "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    REQUIRE(stft.NumFrames(0) == 0);
    REQUIRE(stft.NumFrames(kFft - 1) == 0);
}

TEST_CASE("Stft NumFrames returns 1 for exactly fft_size samples",
          "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    REQUIRE(stft.NumFrames(kFft) == 1);
}

TEST_CASE("Stft NumFrames matches floor((n - fft_size) / hop_size) + 1",
          "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    // n = fft_size + hop_size → 2 frames
    REQUIRE(stft.NumFrames(kFft + kHop) == 2);
    // n = fft_size + 2*hop_size → 3 frames
    REQUIRE(stft.NumFrames(kFft + 2 * kHop) == 3);
    // n = fft_size + hop_size + (hop_size - 1) → still 2 frames (last one not full)
    REQUIRE(stft.NumFrames(kFft + kHop + kHop - 1) == 2);
}

TEST_CASE("Stft Analyze on audio shorter than fft_size returns empty",
          "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    std::vector<float> audio(kFft - 1, 0.5f);
    const auto bins = stft.Analyze(audio);
    REQUIRE(bins.empty());
}

TEST_CASE("Stft Analyze on exactly fft_size samples produces one frame",
          "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    std::vector<float> audio(kFft, 0.0f);
    // Pure sine at bin 4. The Hann window will attenuate the edges, so the
    // peak bin magnitude won't be the ideal N/2 — it'll be roughly N/4 (the
    // Hann window's coherent gain is 0.5). We check the peak is at bin 4
    // with a loose tolerance.
    for (std::size_t n = 0; n < kFft; ++n) {
        audio[n] = std::sin(2.0f * kPi * 4.0f * n / kFft);
    }
    const auto bins = stft.Analyze(audio);
    REQUIRE(bins.size() == 1);
    REQUIRE(bins[0].size() == stft.num_bins());

    // Peak should be at bin 4.
    std::size_t peak_idx = 0;
    float peak_mag = 0.0f;
    for (std::size_t k = 0; k < bins[0].size(); ++k) {
        const float mag = std::abs(bins[0][k]);
        if (mag > peak_mag) {
            peak_mag = mag;
            peak_idx = k;
        }
    }
    REQUIRE(peak_idx == 4);
}

TEST_CASE("Magnitude derives bin-wise absolute values", "[dsp][stft]") {
    std::vector<std::vector<std::complex<float>>> bins = {
        {std::complex<float>(3.0f, 4.0f), std::complex<float>(0.0f, 0.0f)},
        {std::complex<float>(-5.0f, 0.0f), std::complex<float>(0.0f, -2.0f)},
    };
    const auto mag = fim::dsp::Magnitude(bins);
    REQUIRE(mag.size() == 2);
    REQUIRE(mag[0].size() == 2);
    REQUIRE_THAT(mag[0][0], WithinAbs(5.0f, 1e-6));
    REQUIRE_THAT(mag[0][1], WithinAbs(0.0f, 1e-6));
    REQUIRE_THAT(mag[1][0], WithinAbs(5.0f, 1e-6));
    REQUIRE_THAT(mag[1][1], WithinAbs(2.0f, 1e-6));
}

TEST_CASE("Phase derives bin-wise arctangents", "[dsp][stft]") {
    std::vector<std::vector<std::complex<float>>> bins = {
        {std::complex<float>(1.0f, 0.0f), std::complex<float>(0.0f, 1.0f)},
    };
    const auto ph = fim::dsp::Phase(bins);
    REQUIRE(ph.size() == 1);
    REQUIRE(ph[0].size() == 2);
    REQUIRE_THAT(ph[0][0], WithinAbs(0.0f, 1e-6));
    REQUIRE_THAT(ph[0][1], WithinAbs(kPi / 2.0f, 1e-6));
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    stub_bank_writer_test.cpp
    dsp_window_test.cpp
    dsp_wav_loader_test.cpp
    dsp_real_fft_test.cpp
    dsp_stft_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
    ../src/app/services/stub_bank_writer.cpp
    ../src/dsp/window.cpp
    ../src/dsp/wav_loader.cpp
    ../src/dsp/real_fft.cpp
    ../src/dsp/stft.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/stft.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/stft.h`:

```cpp
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/real_fft.h"

namespace fim::dsp {

// Short-Time Fourier Transform of a mono audio buffer using a symmetric
// Hann window.
//
// Matches Python's:
//   freqs, times, Zxx = scipy.signal.stft(
//       audio, nperseg=fft_size, noverlap=fft_size - hop_size,
//       window=scipy.signal.windows.hann(fft_size),
//       boundary=None, padded=False)
//
// Boundary and padding are intentionally disabled here: there is no zero-
// padding at either end of the audio, and the first frame begins at sample 0.
// Frame count formula: 0 if audio has fewer than fft_size samples, otherwise
// floor((n_samples - fft_size) / hop_size) + 1.
//
// Construction allocates a RealFft setup and precomputes the Hann window, so
// reuse a single Stft across many Analyze calls when possible.
class Stft {
public:
    Stft(std::size_t fft_size, std::size_t hop_size);

    std::size_t fft_size() const { return fft_size_; }
    std::size_t hop_size() const { return hop_size_; }
    std::size_t num_bins() const { return fft_size_ / 2 + 1; }

    // Number of frames that will be produced for an input of `n_samples`
    // samples. Useful for pre-allocating.
    std::size_t NumFrames(std::size_t n_samples) const;

    // Perform the STFT. Returns a vector of frames; each frame is a vector
    // of num_bins() complex bins in standard order (DC, positive freqs,
    // Nyquist). Empty if the input is shorter than fft_size.
    std::vector<std::vector<std::complex<float>>> Analyze(
        const std::vector<float>& audio) const;

private:
    std::size_t fft_size_;
    std::size_t hop_size_;
    std::vector<float> window_;
    RealFft fft_;
};

// Compute the bin-wise magnitudes of a 2D complex bin array. Output shape
// matches the input.
std::vector<std::vector<float>> Magnitude(
    const std::vector<std::vector<std::complex<float>>>& bins);

// Compute the bin-wise phases (atan2) of a 2D complex bin array. Output
// shape matches the input.
std::vector<std::vector<float>> Phase(
    const std::vector<std::vector<std::complex<float>>>& bins);

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/stft.cpp`:

```cpp
#include "dsp/stft.h"

#include <cmath>

#include "dsp/window.h"

namespace fim::dsp {

Stft::Stft(std::size_t fft_size, std::size_t hop_size)
    : fft_size_(fft_size),
      hop_size_(hop_size),
      window_(HannWindow(fft_size)),
      fft_(fft_size) {}

std::size_t Stft::NumFrames(std::size_t n_samples) const {
    if (n_samples < fft_size_) {
        return 0;
    }
    return (n_samples - fft_size_) / hop_size_ + 1;
}

std::vector<std::vector<std::complex<float>>> Stft::Analyze(
    const std::vector<float>& audio) const {
    const std::size_t num_frames = NumFrames(audio.size());
    std::vector<std::vector<std::complex<float>>> bins(num_frames);

    std::vector<float> frame(fft_size_);
    for (std::size_t f = 0; f < num_frames; ++f) {
        const std::size_t start = f * hop_size_;
        // Copy the windowed frame into a scratch buffer.
        for (std::size_t i = 0; i < fft_size_; ++i) {
            frame[i] = audio[start + i] * window_[i];
        }
        bins[f].resize(num_bins());
        fft_.Forward(frame.data(), bins[f].data());
    }
    return bins;
}

std::vector<std::vector<float>> Magnitude(
    const std::vector<std::vector<std::complex<float>>>& bins) {
    std::vector<std::vector<float>> result(bins.size());
    for (std::size_t f = 0; f < bins.size(); ++f) {
        result[f].resize(bins[f].size());
        for (std::size_t k = 0; k < bins[f].size(); ++k) {
            result[f][k] = std::abs(bins[f][k]);
        }
    }
    return result;
}

std::vector<std::vector<float>> Phase(
    const std::vector<std::vector<std::complex<float>>>& bins) {
    std::vector<std::vector<float>> result(bins.size());
    for (std::size_t f = 0; f < bins.size(); ++f) {
        result[f].resize(bins[f].size());
        for (std::size_t k = 0; k < bins[f].size(); ++k) {
            result[f][k] = std::arg(bins[f][k]);
        }
    }
    return result;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block to include `src/dsp/stft.cpp`:

```cmake
    src/dsp/window.cpp
    src/dsp/wav_loader.cpp
    src/dsp/real_fft.cpp
    src/dsp/stft.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 40 tests pass (33 from after Task 3 + 7 new STFT tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/stft.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/stft.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_stft_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/stft.h cpp/src/dsp/stft.cpp cpp/tests/dsp_stft_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add Stft analysis class with Magnitude/Phase helpers"
```

---

## Task 5: Push and verify CI

The four DSP pieces are independent and self-contained. Push the branch and let CI confirm the new tests pass on Linux and Windows as well as macOS.

- [ ] **Step 1: Push the branch**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
```

- [ ] **Step 2: Watch CI**

```bash
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId --jq '.[0].databaseId'
# copy the returned id and pass it to:
gh run watch <id> --exit-status
```

Expected: macOS, Ubuntu, Windows, and clang-format jobs all green. The 4 new DSP tests run alongside the 20 existing ones for a total of 40 tests.

- [ ] **Step 3: Confirm test count on each platform**

The CI output should show `100% tests passed, 0 tests failed out of 40` on each build job. If any platform fails a test that passed locally on macOS — particularly the RealFft tests, which depend on PFFFT's SIMD code path — open the failing run's log and investigate before proceeding to Phase 3b.

---

## Phase 3a done when:

1. ✅ `fim::dsp::HannWindow` produces a correct symmetric Hann window
2. ✅ `fim::dsp::LoadWav` round-trips WAV files, mixes stereo to mono, and optionally normalizes
3. ✅ `fim::dsp::RealFft` forward + inverse work correctly for a real-input transform
4. ✅ `fim::dsp::Stft` produces a frame-by-frame windowed complex spectrum matching scipy's `boundary=None, padded=False` mode
5. ✅ `Magnitude` and `Phase` free-function helpers derive the right values
6. ✅ All 40 Catch2 tests pass locally
7. ✅ CI green on macOS, Ubuntu, Windows, and clang-format

Phase 3b picks up from here with `SingleWavGenerator` — the actual spectral modifications + waveform extraction that turn this infrastructure into a working bank writer.
