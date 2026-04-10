# Phase 1 Implementation Plan — Realtime Preview End-to-End

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A one-window C++/Qt app that loads a FourSeas wavetable bank from disk and plays audio through the system output via the real `fourseas::WavetableOscillator` template, with X/Y/Z position sliders, MIDI-note pitch slider, volume slider, and a Play/Stop toggle. Hearing audio out of this binary clears Phase 1's escape hatch.

**Architecture:** Three small focused classes plus a minimal Qt window:

- `WavetableBank` — owns the per-bank float storage; loaded from a directory of `1.wav`–`8.wav` files via `dr_wav`. Immutable once constructed.
- `WavetableVoice` — wraps `fourseas::WavetableOscillator<2048, false, false>` with thread-safe atomic parameter setters and a callback-friendly `RenderBlock(float*, size_t)` method. Uses `std::shared_ptr<const WavetableBank>` accessed via `std::atomic_load`/`store` for lock-free double-buffered bank swaps.
- `RealtimeAudioEngine` — owns a `WavetableVoice` plus a `miniaudio` playback device. Implements the `PlayMode` state machine (`STEADY`/`SWEEP`/`ARPEGGIO`), MIDI-note→Hz conversion, volume + invert (matching the FourSeas hardware's inverting op-amp), and exponential one-pole parameter smoothing in the audio callback to eliminate zipper noise.
- `PreviewWindow` — a `QMainWindow` with sliders, buttons, a `QFileDialog` for bank loading, and a status label. The Phase 2 main UI will replace this; Phase 1's window is intentionally throwaway.

**Tech Stack:** C++17, Qt 6.8 Widgets, miniaudio (vendored), dr_wav (vendored), the FourSeas engine submodule, Catch2 v3 for unit tests, ninja, clang-format.

**Spec deviation:** The Phase 1 section of the spec mentions vendoring `WavetableEngine` (the offline-rendering class from `bindings.cpp@8d36c15`). That class isn't needed for realtime preview — it's only used for one-shot rendering to numpy arrays. We skip it here and revisit in Phase 3 if/when offline rendering becomes useful for DSP comparison testing. This narrows Phase 1 scope without losing any realtime capability.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

New files:

- `cpp/src/engine/single_header_impls.cpp` — single translation unit defining `MINIAUDIO_IMPLEMENTATION` and `DR_WAV_IMPLEMENTATION` so the vendored single-header libs compile.
- `cpp/src/engine/wav_loader.h` / `wav_loader.cpp` — small free-function `LoadWavMono(path) -> std::optional<std::vector<float>>` using `dr_wav`.
- `cpp/src/engine/wavetable_bank.h` / `wavetable_bank.cpp` — `WavetableBank` class. `Load(bank_directory)` factory returns `std::unique_ptr<WavetableBank>`.
- `cpp/src/engine/wavetable_voice.h` / `wavetable_voice.cpp` — `WavetableVoice` class. Owns `fourseas::WavetableOscillator`, exposes `RenderBlock(float*, size_t)` and atomic position/freq/playing setters. Holds `std::shared_ptr<const WavetableBank>` via atomic accessors.
- `cpp/src/engine/realtime_audio_engine.h` / `realtime_audio_engine.cpp` — `RealtimeAudioEngine` class. Owns a `WavetableVoice` and a `ma_device`. Implements `PlayMode`, sweep/arpeggio state, MIDI conversion, volume/invert, parameter smoothing. Provides `Start()`/`Stop()`, `LoadBank(path)`, `SetX/Y/Z`, `SetMidiNote`, `SetVolume`, `SetMode`, `SetSweepTarget`.
- `cpp/src/ui/preview_window.h` / `preview_window.cpp` — `PreviewWindow` Qt class. `QMainWindow` subclass.
- `cpp/tests/wav_loader_test.cpp` — Catch2 test. Writes a known WAV to a temp dir and reads it back.
- `cpp/tests/wavetable_bank_test.cpp` — Catch2 test. Builds a synthetic bank on disk and loads it.
- `cpp/tests/wavetable_voice_test.cpp` — Catch2 test. Renders with/without `playing_=true`, verifies silence vs nonzero.
- `cpp/tests/realtime_audio_engine_test.cpp` — Catch2 test. Verifies sweep position math and arpeggio note transitions using a fake clock.

Modified files:

- `cpp/CMakeLists.txt` — add new sources to the executable target, add new test sources to the test target, no new dependencies (miniaudio + dr_wav are already vendored from Phase 0).
- `cpp/tests/CMakeLists.txt` — add the new test sources.
- `cpp/src/app/main.cpp` — replace the hello-world body with construction of a `PreviewWindow`.

---

## Task 1: Single-header library implementation TU

This creates the one file that defines `MINIAUDIO_IMPLEMENTATION` and `DR_WAV_IMPLEMENTATION` so the vendored libs actually link. Without this, every later task that uses miniaudio or dr_wav will fail with "undefined symbol".

**Files:**
- Create: `cpp/src/engine/single_header_impls.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Create the implementation TU**

Write `cpp/src/engine/single_header_impls.cpp`:

```cpp
// Single translation unit that instantiates the implementation portions of
// the vendored single-header libraries. Each #define MUST appear exactly
// once in the entire program; this file is the only place they live.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
```

- [ ] **Step 2: Add the source to the executable target**

Modify `cpp/CMakeLists.txt`. Find the `qt_add_executable(fim-config-tool ...)` block and add the new source:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
)
```

- [ ] **Step 3: Reconfigure and build to verify it compiles**

```bash
cmake -B /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt) /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. miniaudio and dr_wav each emit ~1-2k lines of code into the .o file but no errors. The hello-world Qt window still works.

**Watchpoint:** miniaudio's implementation includes platform-specific audio backend code (CoreAudio on macOS, ALSA on Linux, WASAPI on Windows). On macOS this needs `-framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework CoreFoundation`. CMake's Qt integration may already pull these in, but if linking fails, add them via `target_link_libraries`. Same for Linux: `-lpthread -ldl -lm`.

- [ ] **Step 4: Format and commit**

```bash
clang-format -i /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/single_header_impls.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/engine/single_header_impls.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): add single-header library implementation TU"
```

---

## Task 2: WAV loader (dr_wav-based)

A small free function that loads a mono float audio file. Replaces the hand-rolled `WavReader` from `bindings.cpp@8d36c15` with a 15-line dr_wav call. This is the leaf dependency — no other engine code depends on anything else.

**Files:**
- Create: `cpp/src/engine/wav_loader.h`
- Create: `cpp/src/engine/wav_loader.cpp`
- Create: `cpp/tests/wav_loader_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/wav_loader_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/wav_loader.h"

namespace {

// Writes a minimal 16-bit PCM mono WAV file to `path` containing `samples`.
// Used by tests to avoid depending on dr_wav for fixture creation.
void WriteRawWav16(const std::filesystem::path& path,
                   const std::vector<int16_t>& samples,
                   uint32_t sample_rate) {
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t fmt_chunk_size = 16;
    const uint32_t riff_size = 36 + data_bytes;

    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riff_size), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    f.write(reinterpret_cast<const char*>(&fmt_chunk_size), 4);
    const uint16_t audio_format = 1;  // PCM
    const uint16_t num_channels = 1;
    const uint32_t byte_rate = sample_rate * num_channels * 2;
    const uint16_t block_align = num_channels * 2;
    const uint16_t bits_per_sample = 16;
    f.write(reinterpret_cast<const char*>(&audio_format), 2);
    f.write(reinterpret_cast<const char*>(&num_channels), 2);
    f.write(reinterpret_cast<const char*>(&sample_rate), 4);
    f.write(reinterpret_cast<const char*>(&byte_rate), 4);
    f.write(reinterpret_cast<const char*>(&block_align), 2);
    f.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&data_bytes), 4);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}

}  // namespace

TEST_CASE("LoadWavMono reads a 16-bit PCM mono WAV", "[wav_loader]") {
    auto temp = std::filesystem::temp_directory_path() / "fim_test_simple.wav";

    // Write 100 samples of a triangle wave at full int16 amplitude.
    std::vector<int16_t> samples(100);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>((i % 50 < 25 ? 16384 : -16384));
    }
    WriteRawWav16(temp, samples, 48000);

    auto loaded = fim::engine::LoadWavMono(temp.string());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 100);

    // 16384 / 32768 = 0.5 (or close to it accounting for normalization)
    REQUIRE(std::abs((*loaded)[0] - 0.5f) < 0.01f);
    REQUIRE(std::abs((*loaded)[25] - (-0.5f)) < 0.01f);

    std::filesystem::remove(temp);
}

TEST_CASE("LoadWavMono returns nullopt for missing file", "[wav_loader]") {
    auto loaded = fim::engine::LoadWavMono("/tmp/this_file_does_not_exist_12345.wav");
    REQUIRE_FALSE(loaded.has_value());
}
```

- [ ] **Step 2: Add the test to the test executable**

Modify `cpp/tests/CMakeLists.txt`. Replace the existing `add_executable(fim-tests ...)` and `target_link_libraries(fim-tests ...)` blocks with:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
)

target_include_directories(fim-tests PRIVATE
    ../src
)

target_link_libraries(fim-tests PRIVATE
    Catch2::Catch2WithMain
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
    SampleRate::samplerate
    spdlog::spdlog
)

# Disable Qt AUTOMOC for the test target — it has no Q_OBJECTs.
set_target_properties(fim-tests PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)

include(Catch)
catch_discover_tests(fim-tests)
```

The `target_include_directories` line allows tests to `#include "engine/wav_loader.h"` from `cpp/src/`. The `miniaudio_headers` and `dr_wav_headers` link entries are needed because `single_header_impls.cpp` `#includes` both. The AUTOMOC/Catch lines are unchanged from Phase 0 — repeated here so this whole block can be safely overwritten in one shot.

- [ ] **Step 3: Run the build to verify the test fails to compile**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `wav_loader.h` does not exist. Good.

- [ ] **Step 4: Write the header**

Create `cpp/src/engine/wav_loader.h`:

```cpp
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fim::engine {

// Loads a WAV file and returns its samples as float in [-1.0, 1.0]. If the
// file has multiple channels, only the first channel is returned. Supports
// 8-bit, 16-bit, 24-bit PCM, and 32-bit float formats (whatever dr_wav
// supports). Returns std::nullopt if the file cannot be opened, parsed, or
// fully read.
std::optional<std::vector<float>> LoadWavMono(const std::string& path);

}  // namespace fim::engine
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/engine/wav_loader.cpp`:

```cpp
#include "engine/wav_loader.h"

#include "dr_wav.h"

namespace fim::engine {

std::optional<std::vector<float>> LoadWavMono(const std::string& path) {
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), nullptr)) {
        return std::nullopt;
    }

    const drwav_uint64 total_frames = wav.totalPCMFrameCount;
    const unsigned channels = wav.channels;

    // Read all frames as interleaved float, then deinterleave to mono.
    std::vector<float> interleaved(total_frames * channels);
    const drwav_uint64 frames_read =
        drwav_read_pcm_frames_f32(&wav, total_frames, interleaved.data());
    drwav_uninit(&wav);

    if (frames_read != total_frames) {
        return std::nullopt;
    }

    if (channels == 1) {
        return interleaved;
    }

    // Multi-channel: take the first channel only.
    std::vector<float> mono(total_frames);
    for (drwav_uint64 i = 0; i < total_frames; ++i) {
        mono[i] = interleaved[i * channels];
    }
    return mono;
}

}  // namespace fim::engine
```

- [ ] **Step 6: Build and run the tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 4 tests pass (the 2 existing smoke tests plus 2 new wav_loader tests).

- [ ] **Step 7: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/wav_loader.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/wav_loader.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/wav_loader_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/engine/wav_loader.h cpp/src/engine/wav_loader.cpp \
        cpp/tests/wav_loader_test.cpp cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add dr_wav-based mono WAV loader"
```

---

## Task 3: WavetableBank

Loads 8 wavetable WAV files (`1.wav`..`8.wav`) from a directory into a single contiguous float array, with the wave-pointer table the FourSeas oscillator's `Init(float**)` method expects. Immutable after construction.

**Files:**
- Create: `cpp/src/engine/wavetable_bank.h`
- Create: `cpp/src/engine/wavetable_bank.cpp`
- Create: `cpp/tests/wavetable_bank_test.cpp`
- Modify: `cpp/tests/CMakeLists.txt`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/wavetable_bank_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "engine/wavetable_bank.h"

namespace {

constexpr size_t kWavetableSize = 2048;
constexpr size_t kWavesPerPage = 64;  // 8x8 grid
constexpr size_t kPagesPerBank = 8;
constexpr uint32_t kSampleRate = 44100;

// Writes a 16-bit mono WAV containing kWavesPerPage * kWavetableSize samples
// of a simple sine wave (so the bank loader has something nontrivial to find).
void WriteFakeBankPage(const std::filesystem::path& path) {
    const size_t total = kWavesPerPage * kWavetableSize;
    std::vector<int16_t> samples(total);
    for (size_t i = 0; i < total; ++i) {
        const float t = static_cast<float>(i) / kWavetableSize;
        samples[i] = static_cast<int16_t>(std::sin(t * 2.0f * 3.14159f) * 16384.0f);
    }

    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t fmt_size = 16;
    const uint32_t riff_size = 36 + data_bytes;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riff_size), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    f.write(reinterpret_cast<const char*>(&fmt_size), 4);
    const uint16_t audio_format = 1;
    const uint16_t channels = 1;
    const uint32_t byte_rate = kSampleRate * 2;
    const uint16_t block_align = 2;
    const uint16_t bits = 16;
    f.write(reinterpret_cast<const char*>(&audio_format), 2);
    f.write(reinterpret_cast<const char*>(&channels), 2);
    f.write(reinterpret_cast<const char*>(&kSampleRate), 4);
    f.write(reinterpret_cast<const char*>(&byte_rate), 4);
    f.write(reinterpret_cast<const char*>(&block_align), 2);
    f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&data_bytes), 4);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}

std::filesystem::path MakeFakeBank() {
    auto dir = std::filesystem::temp_directory_path() / "fim_test_bank";
    std::filesystem::create_directories(dir);
    for (size_t z = 1; z <= kPagesPerBank; ++z) {
        WriteFakeBankPage(dir / (std::to_string(z) + ".wav"));
    }
    return dir;
}

}  // namespace

TEST_CASE("WavetableBank::Load reads 8 pages and exposes wave pointers",
          "[wavetable_bank]") {
    auto dir = MakeFakeBank();

    auto bank = fim::engine::WavetableBank::Load(dir.string());
    REQUIRE(bank != nullptr);

    REQUIRE(bank->total_waves() == 512);
    REQUIRE(bank->wavetable_size() == 2048);

    // Wave pointers must be non-null and point at non-zero data
    // (we wrote sine waves, so the first sample of wave 0 should be ~0.0
    // and the sample at quarter-period should be ~1.0).
    float** waves = bank->wavetable_pointers();
    REQUIRE(waves != nullptr);
    REQUIRE(waves[0] != nullptr);

    // Wraparound sample requirement (FourSeas expects WAVETABLE_SIZE+1 floats):
    // last sample == first sample.
    REQUIRE(waves[0][2048] == waves[0][0]);

    std::filesystem::remove_all(dir);
}

TEST_CASE("WavetableBank::Load returns nullptr for missing directory",
          "[wavetable_bank]") {
    auto bank = fim::engine::WavetableBank::Load("/tmp/this_dir_does_not_exist_xyz");
    REQUIRE(bank == nullptr);
}
```

- [ ] **Step 2: Add the test source and impl source to test target**

Modify `cpp/tests/CMakeLists.txt`. Update the `add_executable(fim-tests ...)` block:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
)
```

`fim-tests` already links `fourseas_engine` from Phase 0, so the FourSeas headers (`wavetable_oscillator.h`, etc.) are reachable.

- [ ] **Step 3: Verify the test fails to compile**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `wavetable_bank.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/engine/wavetable_bank.h`:

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace fim::engine {

// A loaded FourSeas wavetable bank: 8 pages × 64 waves per page × 2048
// samples per wave = 131072 samples per page = 1048576 samples total.
// Each wave has an extra wraparound sample (so 2049 floats per wave) so that
// fourseas::WavetableOscillator's interpolation can read past the end without
// a modulo. Storage is one contiguous std::vector with a parallel
// std::vector<float*> of pointers into it (matching the float** API the
// oscillator expects).
//
// Immutable after construction. Loading a new bank produces a new instance;
// the old one is released only when the audio thread is no longer using it.
class WavetableBank {
public:
    static constexpr size_t kWavetableSize = 2048;
    static constexpr size_t kNumWavesX = 8;
    static constexpr size_t kNumWavesY = 8;
    static constexpr size_t kNumWavesZ = 8;
    static constexpr size_t kTotalWaves = kNumWavesX * kNumWavesY * kNumWavesZ;
    static constexpr size_t kWaveStride = kWavetableSize + 1;  // +1 for wraparound

    // Loads `<bank_directory>/{1..8}.wav` and returns a fully-populated bank.
    // Returns nullptr if the directory doesn't exist, any page is missing,
    // or any page has an unexpected sample count.
    static std::unique_ptr<WavetableBank> Load(const std::string& bank_directory);

    // Pointer table for fourseas::WavetableOscillator::Init(float**).
    // Stable for the lifetime of this bank instance.
    float** wavetable_pointers() { return wave_ptrs_.data(); }
    const float* const* wavetable_pointers() const { return wave_ptrs_.data(); }

    static constexpr size_t total_waves() { return kTotalWaves; }
    static constexpr size_t wavetable_size() { return kWavetableSize; }

private:
    WavetableBank() = default;

    std::vector<float> samples_;     // size: kTotalWaves * kWaveStride
    std::vector<float*> wave_ptrs_;  // size: kTotalWaves; each points into samples_
};

}  // namespace fim::engine
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/engine/wavetable_bank.cpp`:

```cpp
#include "engine/wavetable_bank.h"

#include <filesystem>
#include <iostream>

#include "engine/wav_loader.h"

namespace fim::engine {

std::unique_ptr<WavetableBank> WavetableBank::Load(const std::string& bank_directory) {
    namespace fs = std::filesystem;
    fs::path dir(bank_directory);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return nullptr;
    }

    auto bank = std::unique_ptr<WavetableBank>(new WavetableBank());
    bank->samples_.assign(kTotalWaves * kWaveStride, 0.0f);
    bank->wave_ptrs_.resize(kTotalWaves);
    for (size_t i = 0; i < kTotalWaves; ++i) {
        bank->wave_ptrs_[i] = bank->samples_.data() + i * kWaveStride;
    }

    constexpr size_t kSamplesPerPage = kWavetableSize * 64;  // 64 waves per page

    for (size_t z = 0; z < kNumWavesZ; ++z) {
        const fs::path page_path = dir / (std::to_string(z + 1) + ".wav");

        auto page_samples = LoadWavMono(page_path.string());
        if (!page_samples.has_value()) {
            std::cerr << "WavetableBank::Load: failed to read " << page_path << "\n";
            return nullptr;
        }
        if (page_samples->size() != kSamplesPerPage) {
            std::cerr << "WavetableBank::Load: " << page_path
                      << " has " << page_samples->size()
                      << " samples; expected " << kSamplesPerPage << "\n";
            return nullptr;
        }

        for (size_t y = 0; y < kNumWavesY; ++y) {
            for (size_t x = 0; x < kNumWavesX; ++x) {
                const size_t wave_idx = x + y * kNumWavesX + z * (kNumWavesX * kNumWavesY);
                const size_t src_offset = (y * kNumWavesX + x) * kWavetableSize;

                float* dst = bank->wave_ptrs_[wave_idx];
                for (size_t i = 0; i < kWavetableSize; ++i) {
                    dst[i] = (*page_samples)[src_offset + i];
                }
                // Wraparound sample for the FourSeas oscillator's interpolation.
                dst[kWavetableSize] = dst[0];
            }
        }
    }

    return bank;
}

}  // namespace fim::engine
```

- [ ] **Step 6: Add the source to the main executable as well**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 6 tests pass (4 from before + 2 new wavetable_bank tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/wavetable_bank.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/wavetable_bank.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/wavetable_bank_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/engine/wavetable_bank.h cpp/src/engine/wavetable_bank.cpp \
        cpp/tests/wavetable_bank_test.cpp cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add WavetableBank loader"
```

---

## Task 4: WavetableVoice

Wraps the FourSeas oscillator with thread-safe atomic params and a callback-friendly `RenderBlock`. Holds the current bank as `std::shared_ptr<const WavetableBank>` accessed via free-function `std::atomic_load` / `std::atomic_store` for the lock-free double-buffered swap the spec mandates.

**Files:**
- Create: `cpp/src/engine/wavetable_voice.h`
- Create: `cpp/src/engine/wavetable_voice.cpp`
- Create: `cpp/tests/wavetable_voice_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/wavetable_voice_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "engine/wavetable_bank.h"
#include "engine/wavetable_voice.h"

namespace {

// Build a synthetic bank (sine wave per page) so the voice has something to
// play. Same fixture as the bank test, duplicated to keep tests self-contained.
constexpr size_t kPageSamples = 64 * 2048;
constexpr uint32_t kSampleRate = 44100;

void WritePage(const std::filesystem::path& path) {
    std::vector<int16_t> samples(kPageSamples);
    for (size_t i = 0; i < kPageSamples; ++i) {
        const float t = static_cast<float>(i) / 2048.0f;
        samples[i] = static_cast<int16_t>(std::sin(t * 2.0f * 3.14159f) * 16384.0f);
    }
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t fmt_size = 16;
    const uint32_t riff_size = 36 + data_bytes;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riff_size), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    f.write(reinterpret_cast<const char*>(&fmt_size), 4);
    const uint16_t audio_format = 1;
    const uint16_t channels = 1;
    const uint32_t byte_rate = kSampleRate * 2;
    const uint16_t block_align = 2;
    const uint16_t bits = 16;
    f.write(reinterpret_cast<const char*>(&audio_format), 2);
    f.write(reinterpret_cast<const char*>(&channels), 2);
    f.write(reinterpret_cast<const char*>(&kSampleRate), 4);
    f.write(reinterpret_cast<const char*>(&byte_rate), 4);
    f.write(reinterpret_cast<const char*>(&block_align), 2);
    f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&data_bytes), 4);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}

std::filesystem::path MakeBank() {
    auto dir = std::filesystem::temp_directory_path() / "fim_voice_test_bank";
    std::filesystem::create_directories(dir);
    for (size_t z = 1; z <= 8; ++z) {
        WritePage(dir / (std::to_string(z) + ".wav"));
    }
    return dir;
}

}  // namespace

TEST_CASE("WavetableVoice renders silence when not playing", "[wavetable_voice]") {
    auto dir = MakeBank();
    auto bank = fim::engine::WavetableBank::Load(dir.string());
    REQUIRE(bank != nullptr);

    fim::engine::WavetableVoice voice(48000.0f);
    voice.SetBank(std::shared_ptr<const fim::engine::WavetableBank>(std::move(bank)));
    // playing_ defaults to false; do not call SetPlaying(true).

    std::vector<float> out(256, 1.234f);  // pre-filled with non-zero
    voice.RenderBlock(out.data(), out.size());

    for (float s : out) {
        REQUIRE(s == 0.0f);
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("WavetableVoice produces nonzero audio when playing", "[wavetable_voice]") {
    auto dir = MakeBank();
    auto bank = fim::engine::WavetableBank::Load(dir.string());
    REQUIRE(bank != nullptr);

    fim::engine::WavetableVoice voice(48000.0f);
    voice.SetBank(std::shared_ptr<const fim::engine::WavetableBank>(std::move(bank)));
    voice.SetX(0.0f);
    voice.SetY(0.0f);
    voice.SetZ(0.0f);
    voice.SetFrequency(440.0f);
    voice.SetPlaying(true);

    std::vector<float> out(2048, 0.0f);
    voice.RenderBlock(out.data(), out.size());

    bool any_nonzero = false;
    for (float s : out) {
        if (std::abs(s) > 0.001f) {
            any_nonzero = true;
            break;
        }
    }
    REQUIRE(any_nonzero);

    std::filesystem::remove_all(dir);
}
```

- [ ] **Step 2: Add the test and impl sources to the test target**

Modify `cpp/tests/CMakeLists.txt`. Update the `add_executable(fim-tests ...)` block:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `wavetable_voice.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/engine/wavetable_voice.h`:

```cpp
#pragma once

#include <atomic>
#include <memory>

#include "wavetable_oscillator.h"

namespace fim::engine {

class WavetableBank;

// A single FourSeas wavetable voice. Wraps fourseas::WavetableOscillator with
// thread-safe atomic parameter setters and a callback-friendly RenderBlock.
//
// Bank ownership uses std::shared_ptr<const WavetableBank> accessed via free-
// function std::atomic_load / std::atomic_store. The audio thread takes a
// local snapshot of the shared_ptr at the start of each block; the loader
// thread atomically installs a new bank. The old bank is destroyed only when
// no audio block is still holding it, eliminating the use-after-free risk
// the original mutex-based StreamingEngine had.
class WavetableVoice {
public:
    explicit WavetableVoice(float sample_rate);

    // Atomically installs a new bank. Safe to call from any thread, including
    // while audio is playing.
    void SetBank(std::shared_ptr<const WavetableBank> bank);

    // Renders `num_samples` mono float samples into `out`. Safe to call from
    // an audio callback. If no bank is loaded or playing_ is false, fills the
    // buffer with zeros.
    void RenderBlock(float* out, size_t num_samples);

    // Thread-safe parameter setters. Range-clamped internally.
    void SetX(float x);
    void SetY(float y);
    void SetZ(float z);
    void SetFrequency(float hz);
    void SetPlaying(bool playing);

    float x() const { return x_.load(std::memory_order_relaxed); }
    float y() const { return y_.load(std::memory_order_relaxed); }
    float z() const { return z_.load(std::memory_order_relaxed); }
    float frequency() const { return frequency_.load(std::memory_order_relaxed); }
    bool playing() const { return playing_.load(std::memory_order_relaxed); }

    float sample_rate() const { return sample_rate_; }

private:
    static constexpr size_t kWavetableSize = 2048;

    float sample_rate_;
    fourseas::WavetableOscillator<kWavetableSize, false, false> osc_;

    // Bank pointer. Use std::atomic_load / std::atomic_store to access
    // (free-function form for shared_ptr — works in C++17).
    std::shared_ptr<const WavetableBank> bank_;

    // Pointer to the bank we last initialized the oscillator with. Only
    // touched by the audio thread; non-atomic.
    const WavetableBank* last_inited_bank_ = nullptr;

    std::atomic<float> x_{0.0f};
    std::atomic<float> y_{0.0f};
    std::atomic<float> z_{0.0f};
    std::atomic<float> frequency_{440.0f};
    std::atomic<bool> playing_{false};
};

}  // namespace fim::engine
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/engine/wavetable_voice.cpp`:

```cpp
#include "engine/wavetable_voice.h"

#include <algorithm>
#include <cstring>

#include "engine/wavetable_bank.h"
#include "src/params.h"

namespace fim::engine {

namespace {

constexpr float kPositionMin = 0.0f;
constexpr float kPositionMax = 6.9999f;  // matches StreamingEngine clamping

float ClampPosition(float v) {
    return std::clamp(v, kPositionMin, kPositionMax);
}

}  // namespace

WavetableVoice::WavetableVoice(float sample_rate) : sample_rate_(sample_rate) {}

void WavetableVoice::SetBank(std::shared_ptr<const WavetableBank> bank) {
    std::atomic_store(&bank_, std::move(bank));
}

void WavetableVoice::SetX(float x) { x_.store(ClampPosition(x), std::memory_order_relaxed); }
void WavetableVoice::SetY(float y) { y_.store(ClampPosition(y), std::memory_order_relaxed); }
void WavetableVoice::SetZ(float z) { z_.store(ClampPosition(z), std::memory_order_relaxed); }
void WavetableVoice::SetFrequency(float hz) {
    frequency_.store(hz, std::memory_order_relaxed);
}
void WavetableVoice::SetPlaying(bool playing) {
    playing_.store(playing, std::memory_order_relaxed);
}

void WavetableVoice::RenderBlock(float* out, size_t num_samples) {
    auto bank = std::atomic_load(&bank_);

    if (!bank || !playing_.load(std::memory_order_relaxed)) {
        std::memset(out, 0, num_samples * sizeof(float));
        return;
    }

    // Re-init the oscillator if the bank pointer changed since last block.
    // Audio-thread only; no synchronization needed.
    if (bank.get() != last_inited_bank_) {
        // Init takes a non-const float** because the firmware oscillator may
        // mutate its bank pointer (SetBank). We never call SetBank, so the
        // const_cast is safe in our usage. Localized to this one line to
        // make the contract explicit.
        osc_.Init(const_cast<float**>(bank->wavetable_pointers()));
        last_inited_bank_ = bank.get();
    }

    const float x = x_.load(std::memory_order_relaxed);
    const float y = y_.load(std::memory_order_relaxed);
    const float z = z_.load(std::memory_order_relaxed);
    const float freq = frequency_.load(std::memory_order_relaxed);
    const float norm_freq = freq / sample_rate_;

    for (size_t i = 0; i < num_samples; ++i) {
        fourseas::Params::Values values = {};
        values.frequency = norm_freq;
        values.x = x;
        values.y = y;
        values.z = z;
        values.osc_mod_amount = 0.0f;

        fourseas::OscillatorParams params = {
            .values = values,
            .interpolate = true,
            .mod_state = 0,
            .mod_input = 0.0f,
            .sync_state = 0,
            .sync_input = false,
        };

        osc_.Render(params, &out[i]);
    }
}

}  // namespace fim::engine
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 8 tests pass.

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/wavetable_voice.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/wavetable_voice.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/wavetable_voice_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/engine/wavetable_voice.h cpp/src/engine/wavetable_voice.cpp \
        cpp/tests/wavetable_voice_test.cpp cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add WavetableVoice with atomic bank swap"
```

---

## Task 5: RealtimeAudioEngine

Owns a `WavetableVoice` plus a miniaudio playback device. Implements the `PlayMode` state machine, MIDI conversion, volume + invert, and exponential one-pole parameter smoothing.

**Files:**
- Create: `cpp/src/engine/realtime_audio_engine.h`
- Create: `cpp/src/engine/realtime_audio_engine.cpp`
- Create: `cpp/tests/realtime_audio_engine_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/realtime_audio_engine_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "engine/realtime_audio_engine.h"

using fim::engine::PlayMode;
using fim::engine::RealtimeAudioEngine;

TEST_CASE("MidiToFrequency converts A4 (note 69) to 440 Hz", "[realtime]") {
    REQUIRE(std::abs(RealtimeAudioEngine::MidiToFrequency(69) - 440.0f) < 0.01f);
    REQUIRE(std::abs(RealtimeAudioEngine::MidiToFrequency(60) - 261.626f) < 0.5f);
    REQUIRE(std::abs(RealtimeAudioEngine::MidiToFrequency(81) - 880.0f) < 0.5f);
}

TEST_CASE("Sweep position progresses linearly with time", "[realtime]") {
    // Use 1.0 second sweep duration for easy math.
    auto pos = RealtimeAudioEngine::SweepPositionAt(
        /*elapsed=*/0.5f, /*duration=*/1.0f,
        /*target_x=*/4.0f, /*target_y=*/2.0f, /*target_z=*/6.0f);
    REQUIRE(std::abs(pos.x - 2.0f) < 1e-4f);
    REQUIRE(std::abs(pos.y - 1.0f) < 1e-4f);
    REQUIRE(std::abs(pos.z - 3.0f) < 1e-4f);
}

TEST_CASE("Sweep position clamps to target at end", "[realtime]") {
    auto pos = RealtimeAudioEngine::SweepPositionAt(
        /*elapsed=*/2.0f, /*duration=*/1.0f,
        /*target_x=*/4.0f, /*target_y=*/2.0f, /*target_z=*/6.0f);
    REQUIRE(pos.x == 4.0f);
    REQUIRE(pos.y == 2.0f);
    REQUIRE(pos.z == 6.0f);
}

TEST_CASE("Arpeggio note index advances with elapsed time", "[realtime]") {
    // ARPEGGIO_NOTE_DURATION is 0.25s. After 0.6s of elapsed time, the
    // arpeggio should be on its 3rd note (index 2).
    const int idx = RealtimeAudioEngine::ArpeggioIndexAt(/*elapsed=*/0.6f);
    REQUIRE(idx == 2);
}

TEST_CASE("Arpeggio interval table is the major-triad up-down pattern", "[realtime]") {
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[0] == 0);   // root
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[1] == 4);   // major 3rd
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[2] == 7);   // 5th
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[3] == 12);  // octave
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[4] == 7);   // back down
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[5] == 4);
}
```

These tests cover the pure-math helpers (MIDI conversion, sweep position, arpeggio index) without touching miniaudio or actually opening an audio device, so they run cleanly in headless CI.

- [ ] **Step 2: Add the test and impl sources to the test target**

Modify `cpp/tests/CMakeLists.txt`. Update the `add_executable(fim-tests ...)` block:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
)

target_link_libraries(fim-tests PRIVATE
    Catch2::Catch2WithMain
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
    SampleRate::samplerate
    spdlog::spdlog
)
```

(The `target_link_libraries` line replaces the existing one — we need to add `miniaudio_headers` and `dr_wav_headers` because the engine sources include those headers.)

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `realtime_audio_engine.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/engine/realtime_audio_engine.h`:

```cpp
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "engine/wavetable_voice.h"

struct ma_device;  // forward decl from miniaudio.h

namespace fim::engine {

class WavetableBank;

enum class PlayMode {
    kSteady,
    kSweep,
    kArpeggio,
};

struct Position3D {
    float x;
    float y;
    float z;
};

// Real-time audio engine. Owns a WavetableVoice plus a miniaudio playback
// device. Wires the device's audio callback into the voice's RenderBlock,
// applies volume + invert, and runs the PlayMode state machine (steady tone,
// sweep, arpeggio) plus exponential one-pole parameter smoothing to avoid
// zipper noise on slider drags.
//
// Lifetime: construct, optionally LoadBank, Start (opens device), eventually
// Stop (closes device), destruct.
class RealtimeAudioEngine {
public:
    // The major-triad up-and-down arpeggio pattern, in semitones from the
    // base note. Matches RealtimeAudioEngine in streaming.py.
    static constexpr std::array<int, 6> kArpeggioIntervals{0, 4, 7, 12, 7, 4};
    static constexpr float kArpeggioNoteDurationSec = 0.25f;

    explicit RealtimeAudioEngine(float sample_rate = 48000.0f, size_t block_size = 512);
    ~RealtimeAudioEngine();

    RealtimeAudioEngine(const RealtimeAudioEngine&) = delete;
    RealtimeAudioEngine& operator=(const RealtimeAudioEngine&) = delete;

    // Loads a wavetable bank from a directory of 1.wav..8.wav files. Returns
    // false on any I/O or format error. Safe to call while playing — the new
    // bank atomically replaces the old one without dropouts.
    bool LoadBank(const std::string& bank_directory);

    // Opens the audio device and begins playback. Returns false if no bank
    // is loaded or the device cannot be opened.
    bool Start();

    // Stops playback and closes the audio device.
    void Stop();

    bool IsPlaying() const;
    bool IsLoaded() const;

    // Position controls (0.0..6.9999, clamped internally).
    void SetX(float x) { voice_.SetX(x); }
    void SetY(float y) { voice_.SetY(y); }
    void SetZ(float z) { voice_.SetZ(z); }
    void SetPosition(float x, float y, float z) {
        voice_.SetX(x);
        voice_.SetY(y);
        voice_.SetZ(z);
    }

    // Pitch controls.
    void SetFrequency(float hz) { voice_.SetFrequency(hz); }
    void SetMidiNote(int midi_note);
    int midi_note() const { return midi_base_note_.load(std::memory_order_relaxed); }

    // Volume in [0.0, 1.0]; clamped internally.
    void SetVolume(float volume);
    float volume() const { return volume_.load(std::memory_order_relaxed); }

    // Whether to invert the output (matches the FourSeas hardware's
    // inverting op-amp). Defaults to true.
    void SetInvert(bool invert) { invert_.store(invert, std::memory_order_relaxed); }
    bool invert() const { return invert_.load(std::memory_order_relaxed); }

    // Playback mode.
    void SetMode(PlayMode mode);
    PlayMode mode() const;

    // Sweep target. The sweep starts at (0,0,0) and progresses linearly to
    // (target_x, target_y, target_z) over `duration` seconds.
    void SetSweepTarget(float target_x, float target_y, float target_z, float duration);

    float sample_rate() const { return sample_rate_; }

    // ---- Pure-math helpers (testable without opening audio devices) ----

    static float MidiToFrequency(int midi_note);

    // Returns position at time `elapsed` into a sweep of total `duration`,
    // starting from (0,0,0) and ending at (target_x, target_y, target_z).
    // Clamps to the target if elapsed > duration.
    static Position3D SweepPositionAt(float elapsed, float duration,
                                      float target_x, float target_y, float target_z);

    // Returns the arpeggio index for time `elapsed` since arpeggio start.
    // Wraps around the kArpeggioIntervals array.
    static int ArpeggioIndexAt(float elapsed);

private:
    void AudioCallback(float* output, size_t num_frames);
    static void MiniaudioDataCallback(ma_device* device, void* output,
                                       const void* input, unsigned frame_count);

    float sample_rate_;
    size_t block_size_;

    WavetableVoice voice_;
    std::unique_ptr<ma_device> device_;  // PIMPL-ish: held by pointer so the
                                          // header doesn't need miniaudio.h
    std::atomic<bool> device_open_{false};

    // Volume + invert
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> invert_{true};

    // Smoothed parameters used inside the audio callback. Only the audio
    // thread reads/writes these.
    float smoothed_volume_ = 1.0f;
    static constexpr float kVolumeSmoothingCoeff = 0.001f;

    // PlayMode state. Mode is read inside the callback under a mutex; the
    // mutex is only contended on mode changes (rare).
    mutable std::mutex mode_mutex_;
    PlayMode mode_ = PlayMode::kSteady;
    std::chrono::steady_clock::time_point sweep_start_time_;
    float sweep_target_x_ = 0.0f;
    float sweep_target_y_ = 0.0f;
    float sweep_target_z_ = 0.0f;
    float sweep_duration_ = 4.0f;
    std::chrono::steady_clock::time_point arpeggio_start_time_;

    // MIDI base note for arpeggio
    std::atomic<int> midi_base_note_{60};

    // Set true after the first successful LoadBank call. Used by IsLoaded().
    std::atomic<bool> loaded_once_{false};
};

}  // namespace fim::engine
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/engine/realtime_audio_engine.cpp`:

```cpp
#include "engine/realtime_audio_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>

#include "engine/wavetable_bank.h"
#include "miniaudio.h"

namespace fim::engine {

namespace {

float ClampUnit(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

RealtimeAudioEngine::RealtimeAudioEngine(float sample_rate, size_t block_size)
    : sample_rate_(sample_rate),
      block_size_(block_size),
      voice_(sample_rate),
      device_(std::make_unique<ma_device>()) {}

RealtimeAudioEngine::~RealtimeAudioEngine() {
    Stop();
}

bool RealtimeAudioEngine::LoadBank(const std::string& bank_directory) {
    auto bank = WavetableBank::Load(bank_directory);
    if (!bank) {
        return false;
    }
    voice_.SetBank(std::shared_ptr<const WavetableBank>(std::move(bank)));
    return true;
}

bool RealtimeAudioEngine::IsLoaded() const {
    // The voice has no public IsLoaded(); we use the fact that LoadBank
    // installs a non-null bank. We approximate by checking that the device is
    // open OR by trying to render a single sample. For Phase 1 we just track
    // it externally via a flag set in LoadBank... but actually the simpler
    // contract is "you must call LoadBank before Start". Document and move on.
    // For now, we can't observe bank presence from outside the voice cleanly,
    // so we conservatively report whether we've loaded ANYTHING.
    return loaded_once_.load(std::memory_order_relaxed);
}

bool RealtimeAudioEngine::IsPlaying() const {
    return device_open_.load(std::memory_order_relaxed);
}

bool RealtimeAudioEngine::Start() {
    if (device_open_.load(std::memory_order_relaxed)) {
        return true;  // already started
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = static_cast<ma_uint32>(sample_rate_);
    config.dataCallback = &RealtimeAudioEngine::MiniaudioDataCallback;
    config.pUserData = this;
    config.periodSizeInFrames = static_cast<ma_uint32>(block_size_);

    if (ma_device_init(nullptr, &config, device_.get()) != MA_SUCCESS) {
        std::cerr << "RealtimeAudioEngine: ma_device_init failed\n";
        return false;
    }

    voice_.SetPlaying(true);

    if (ma_device_start(device_.get()) != MA_SUCCESS) {
        std::cerr << "RealtimeAudioEngine: ma_device_start failed\n";
        ma_device_uninit(device_.get());
        voice_.SetPlaying(false);
        return false;
    }

    device_open_.store(true, std::memory_order_release);
    return true;
}

void RealtimeAudioEngine::Stop() {
    if (!device_open_.load(std::memory_order_relaxed)) {
        return;
    }
    voice_.SetPlaying(false);
    ma_device_uninit(device_.get());
    device_open_.store(false, std::memory_order_release);
}

void RealtimeAudioEngine::SetMidiNote(int midi_note) {
    midi_base_note_.store(midi_note, std::memory_order_relaxed);
    SetFrequency(MidiToFrequency(midi_note));
}

void RealtimeAudioEngine::SetVolume(float volume) {
    volume_.store(ClampUnit(volume), std::memory_order_relaxed);
}

void RealtimeAudioEngine::SetMode(PlayMode mode) {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    mode_ = mode;
    if (mode == PlayMode::kSweep) {
        sweep_start_time_ = std::chrono::steady_clock::now();
    } else if (mode == PlayMode::kArpeggio) {
        arpeggio_start_time_ = std::chrono::steady_clock::now();
    }
}

PlayMode RealtimeAudioEngine::mode() const {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    return mode_;
}

void RealtimeAudioEngine::SetSweepTarget(float target_x, float target_y, float target_z,
                                          float duration) {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    sweep_target_x_ = target_x;
    sweep_target_y_ = target_y;
    sweep_target_z_ = target_z;
    sweep_duration_ = duration;
}

void RealtimeAudioEngine::AudioCallback(float* output, size_t num_frames) {
    // Update positions for SWEEP/ARPEGGIO modes BEFORE rendering this block.
    PlayMode current_mode;
    {
        std::lock_guard<std::mutex> lock(mode_mutex_);
        current_mode = mode_;
        const auto now = std::chrono::steady_clock::now();

        if (current_mode == PlayMode::kSweep) {
            const float elapsed_sec =
                std::chrono::duration<float>(now - sweep_start_time_).count();
            const auto pos = SweepPositionAt(elapsed_sec, sweep_duration_,
                                              sweep_target_x_, sweep_target_y_,
                                              sweep_target_z_);
            voice_.SetX(pos.x);
            voice_.SetY(pos.y);
            voice_.SetZ(pos.z);
            if (elapsed_sec >= sweep_duration_) {
                mode_ = PlayMode::kSteady;  // sweep done
            }
        } else if (current_mode == PlayMode::kArpeggio) {
            const float elapsed_sec =
                std::chrono::duration<float>(now - arpeggio_start_time_).count();
            const int idx = ArpeggioIndexAt(elapsed_sec);
            const int interval = kArpeggioIntervals[idx % kArpeggioIntervals.size()];
            const int base = midi_base_note_.load(std::memory_order_relaxed);
            voice_.SetFrequency(MidiToFrequency(base + interval));
        }
    }

    // Render audio from the voice.
    voice_.RenderBlock(output, num_frames);

    // Apply volume + invert with one-pole smoothing on volume to avoid
    // zipper noise on slider drags.
    const float target_volume = volume_.load(std::memory_order_relaxed);
    const bool invert = invert_.load(std::memory_order_relaxed);

    for (size_t i = 0; i < num_frames; ++i) {
        smoothed_volume_ += (target_volume - smoothed_volume_) * kVolumeSmoothingCoeff;
        const float gain = invert ? -smoothed_volume_ : smoothed_volume_;
        output[i] *= gain;
    }
}

void RealtimeAudioEngine::MiniaudioDataCallback(ma_device* device, void* output,
                                                  const void* /*input*/,
                                                  unsigned frame_count) {
    auto* self = static_cast<RealtimeAudioEngine*>(device->pUserData);
    self->AudioCallback(static_cast<float*>(output), frame_count);
}

float RealtimeAudioEngine::MidiToFrequency(int midi_note) {
    return 440.0f * std::pow(2.0f, static_cast<float>(midi_note - 69) / 12.0f);
}

Position3D RealtimeAudioEngine::SweepPositionAt(float elapsed, float duration,
                                                  float target_x, float target_y,
                                                  float target_z) {
    if (elapsed >= duration) {
        return {target_x, target_y, target_z};
    }
    const float progress = elapsed / duration;
    return {target_x * progress, target_y * progress, target_z * progress};
}

int RealtimeAudioEngine::ArpeggioIndexAt(float elapsed) {
    return static_cast<int>(elapsed / kArpeggioNoteDurationSec);
}

}  // namespace fim::engine
```

**`loaded_once_` is set in `LoadBank`** — the implementation above already does `voice_.SetBank(...)` after a successful load, but it also needs to set the `loaded_once_` flag. Update the `LoadBank` body to:

```cpp
bool RealtimeAudioEngine::LoadBank(const std::string& bank_directory) {
    auto bank = WavetableBank::Load(bank_directory);
    if (!bank) {
        return false;
    }
    voice_.SetBank(std::shared_ptr<const WavetableBank>(std::move(bank)));
    loaded_once_.store(true, std::memory_order_relaxed);
    return true;
}
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 13 tests pass (8 from before + 5 new realtime tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/realtime_audio_engine.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/engine/realtime_audio_engine.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/realtime_audio_engine_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/engine/realtime_audio_engine.h cpp/src/engine/realtime_audio_engine.cpp \
        cpp/tests/realtime_audio_engine_test.cpp cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add RealtimeAudioEngine wrapping miniaudio + voice"
```

---

## Task 6: PreviewWindow (Qt UI)

Minimal `QMainWindow` with X/Y/Z sliders, MIDI note slider, volume slider, Load Bank button (file dialog), Play/Stop button, status label. No QSS styling — just default Qt widgets. Phase 2's main UI will replace this.

**Files:**
- Create: `cpp/src/ui/preview_window.h`
- Create: `cpp/src/ui/preview_window.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/preview_window.h`:

```cpp
#pragma once

#include <memory>

#include <QMainWindow>

class QLabel;
class QPushButton;
class QSlider;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::ui {

// Phase 1 throwaway preview window. Provides the minimum UI needed to verify
// the realtime audio engine works end-to-end: load a bank, scrub X/Y/Z, set
// pitch, set volume, play/stop. Phase 2 will replace this entirely with the
// production UI from designs/start-state.svg.
class PreviewWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PreviewWindow(QWidget* parent = nullptr);
    ~PreviewWindow() override;

private slots:
    void OnLoadBankClicked();
    void OnPlayStopClicked();
    void OnXChanged(int value);
    void OnYChanged(int value);
    void OnZChanged(int value);
    void OnPitchChanged(int value);
    void OnVolumeChanged(int value);

private:
    void UpdateStatusLabel(const QString& text);
    void SetControlsEnabled(bool enabled);

    std::unique_ptr<fim::engine::RealtimeAudioEngine> engine_;

    QSlider* x_slider_ = nullptr;
    QSlider* y_slider_ = nullptr;
    QSlider* z_slider_ = nullptr;
    QSlider* pitch_slider_ = nullptr;
    QSlider* volume_slider_ = nullptr;
    QPushButton* load_button_ = nullptr;
    QPushButton* play_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/preview_window.cpp`:

```cpp
#include "ui/preview_window.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "engine/realtime_audio_engine.h"

namespace fim::ui {

namespace {

constexpr int kPositionSliderMax = 699;       // 0.00..6.99 with /100 scaling
constexpr int kMidiNoteMin = 36;              // C2
constexpr int kMidiNoteMax = 96;              // C7
constexpr int kMidiNoteDefault = 60;          // C4
constexpr int kVolumeMax = 100;
constexpr int kVolumeDefault = 60;            // 60% to start (gentle)

QSlider* MakeHorizontalSlider(int min, int max, int initial) {
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(initial);
    return s;
}

}  // namespace

PreviewWindow::PreviewWindow(QWidget* parent)
    : QMainWindow(parent),
      engine_(std::make_unique<fim::engine::RealtimeAudioEngine>(48000.0f, 512)) {
    setWindowTitle("FIM Config Tool — Phase 1 Preview");
    resize(560, 380);

    auto* central = new QWidget(this);
    auto* root_layout = new QVBoxLayout(central);

    // ---- Bank loader ----
    auto* bank_row = new QHBoxLayout();
    load_button_ = new QPushButton("Load Bank…");
    bank_row->addWidget(load_button_);
    bank_row->addStretch();
    root_layout->addLayout(bank_row);

    // ---- Position sliders ----
    auto* pos_box = new QGroupBox("Wavetable Position");
    auto* pos_layout = new QVBoxLayout(pos_box);
    auto add_slider_row = [&](const QString& label, QSlider*& s) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        s = MakeHorizontalSlider(0, kPositionSliderMax, 0);
        row->addWidget(s);
        pos_layout->addLayout(row);
    };
    add_slider_row("X", x_slider_);
    add_slider_row("Y", y_slider_);
    add_slider_row("Z", z_slider_);
    root_layout->addWidget(pos_box);

    // ---- Pitch + volume ----
    auto* pv_box = new QGroupBox("Playback");
    auto* pv_layout = new QVBoxLayout(pv_box);

    auto* pitch_row = new QHBoxLayout();
    pitch_row->addWidget(new QLabel("Pitch (MIDI)"));
    pitch_slider_ = MakeHorizontalSlider(kMidiNoteMin, kMidiNoteMax, kMidiNoteDefault);
    pitch_row->addWidget(pitch_slider_);
    pv_layout->addLayout(pitch_row);

    auto* vol_row = new QHBoxLayout();
    vol_row->addWidget(new QLabel("Volume"));
    volume_slider_ = MakeHorizontalSlider(0, kVolumeMax, kVolumeDefault);
    vol_row->addWidget(volume_slider_);
    pv_layout->addLayout(vol_row);

    auto* play_row = new QHBoxLayout();
    play_button_ = new QPushButton("Play");
    play_row->addWidget(play_button_);
    play_row->addStretch();
    pv_layout->addLayout(play_row);

    root_layout->addWidget(pv_box);

    // ---- Status ----
    status_label_ = new QLabel("No bank loaded.");
    root_layout->addWidget(status_label_);

    setCentralWidget(central);

    // ---- Wiring ----
    connect(load_button_, &QPushButton::clicked, this, &PreviewWindow::OnLoadBankClicked);
    connect(play_button_, &QPushButton::clicked, this, &PreviewWindow::OnPlayStopClicked);
    connect(x_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnXChanged);
    connect(y_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnYChanged);
    connect(z_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnZChanged);
    connect(pitch_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnPitchChanged);
    connect(volume_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnVolumeChanged);

    // Initialize engine with current slider values
    engine_->SetVolume(static_cast<float>(kVolumeDefault) / kVolumeMax);
    engine_->SetMidiNote(kMidiNoteDefault);

    // Disable playback controls until a bank is loaded
    SetControlsEnabled(false);
}

PreviewWindow::~PreviewWindow() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
    }
}

void PreviewWindow::OnLoadBankClicked() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select Wavetable Bank Directory", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return;
    }
    if (engine_->LoadBank(dir.toStdString())) {
        UpdateStatusLabel(QString("Loaded: %1").arg(dir));
        SetControlsEnabled(true);
    } else {
        UpdateStatusLabel(QString("Failed to load bank from: %1").arg(dir));
    }
}

void PreviewWindow::OnPlayStopClicked() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
        play_button_->setText("Play");
    } else {
        if (engine_->Start()) {
            play_button_->setText("Stop");
        } else {
            UpdateStatusLabel("Failed to start audio device.");
        }
    }
}

void PreviewWindow::OnXChanged(int value) {
    engine_->SetX(static_cast<float>(value) / 100.0f);
}

void PreviewWindow::OnYChanged(int value) {
    engine_->SetY(static_cast<float>(value) / 100.0f);
}

void PreviewWindow::OnZChanged(int value) {
    engine_->SetZ(static_cast<float>(value) / 100.0f);
}

void PreviewWindow::OnPitchChanged(int value) {
    engine_->SetMidiNote(value);
}

void PreviewWindow::OnVolumeChanged(int value) {
    engine_->SetVolume(static_cast<float>(value) / kVolumeMax);
}

void PreviewWindow::UpdateStatusLabel(const QString& text) {
    status_label_->setText(text);
}

void PreviewWindow::SetControlsEnabled(bool enabled) {
    x_slider_->setEnabled(enabled);
    y_slider_->setEnabled(enabled);
    z_slider_->setEnabled(enabled);
    pitch_slider_->setEnabled(enabled);
    volume_slider_->setEnabled(enabled);
    play_button_->setEnabled(enabled);
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add the source to the executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/preview_window.cpp
)
```

- [ ] **Step 4: Build (without using the new window yet)**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build succeeds. The PreviewWindow class is compiled and Qt's MOC processes it (note the `Q_OBJECT` macro), but main.cpp doesn't use it yet — that's Task 7. The hello-world window still launches.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/preview_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/preview_window.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/preview_window.h cpp/src/ui/preview_window.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): add Phase 1 PreviewWindow"
```

---

## Task 7: Wire main.cpp to PreviewWindow

Replace the hello-world body with `PreviewWindow`. This is the moment the app becomes functional.

**Files:**
- Modify: `cpp/src/app/main.cpp`

- [ ] **Step 1: Replace main.cpp**

Overwrite `cpp/src/app/main.cpp` with:

```cpp
#include <QApplication>
#include <QFile>
#include <QTextStream>

#include "ui/preview_window.h"

namespace {

QString LoadStylesheet() {
    QFile f(":/app.qss");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&f);
    return in.readAll();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Ferry Island Modular");
    app.setApplicationName("FIM Config Tool");

    const QString qss = LoadStylesheet();
    if (!qss.isEmpty()) {
        app.setStyleSheet(qss);
    }

    fim::ui::PreviewWindow window;
    window.show();

    return app.exec();
}
```

- [ ] **Step 2: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 3: Run the app and verify the new window appears**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Expected: a 560×380 window titled "FIM Config Tool — Phase 1 Preview" appears with:
- A "Load Bank…" button at the top
- A "Wavetable Position" group with X, Y, Z sliders (disabled)
- A "Playback" group with Pitch, Volume sliders and Play button (disabled)
- A status label saying "No bank loaded."

Close the window.

- [ ] **Step 4: Format and commit**

```bash
clang-format -i /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/main.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/main.cpp
git commit -m "feat(cpp): wire main to Phase 1 PreviewWindow"
```

---

## Task 8: End-to-end manual verification + Phase 1 completion

The escape-hatch checkpoint. Load an existing FourSeas bank, click Play, verify audio comes out.

**Files:**
- Create: `docs/phase-1-complete.md`

- [ ] **Step 1: Locate a real wavetable bank**

The Python tool on `master` writes wavetable banks to `output_waves/audio_resynth/` (8 WAVs named `1.wav`..`8.wav`). On the user's machine these likely live at:

```
/Users/jgoney/dev/ferry-island-modular/fim-config-tool/output_waves/audio_resynth/
```

If they don't exist (e.g. if the user has cleaned them or never generated any), regenerate them by running the Python tool from master, OR use any other directory of `1.wav`..`8.wav` files where each is a 64×2048-sample mono WAV.

- [ ] **Step 2: Run the app**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

- [ ] **Step 3: Load the bank**

Click "Load Bank…", navigate to the bank directory (e.g. `output_waves/audio_resynth/`), click Choose. The status label should change to "Loaded: …" and the sliders + Play button should become enabled.

- [ ] **Step 4: Click Play and verify audio**

Click Play. **You should hear audio.** Drag the X, Y, Z sliders — the timbre should change in real time without clicks or zipper noise. Drag the Pitch slider — the pitch should change. Drag Volume — the volume should change smoothly.

If you hear nothing, check:
- macOS: System Settings → Privacy & Security → Microphone (no, miniaudio doesn't need mic but check audio output isn't muted)
- Volume slider isn't at 0
- Status label says "Loaded: …" and not "Failed to load…"
- The bank directory actually contains 8 valid WAV files

If you hear clicks or zipper noise on slider drags, the parameter smoothing might need a smaller `kVolumeSmoothingCoeff` (slower smoothing) or extending smoothing to X/Y/Z as well.

Click Play again to stop.

- [ ] **Step 5: Verify CI is still green**

Push the branch and watch CI:

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
gh run watch
```

Expected: macOS, Linux, Windows, and clang-format jobs all green. The CI runners can't actually play audio (they don't have audio devices), but they can build and run the unit tests.

**Watchpoint for CI:** the unit tests for `WavetableVoice` and `RealtimeAudioEngine` only test the rendering math + state machine — they don't open audio devices. If miniaudio fails to compile on a CI platform (especially Windows), it will be a separate issue from the audio playback path. Linux runners may need additional system libraries for ALSA — `libasound2-dev` is already in the Phase 0 CI workflow.

- [ ] **Step 6: Write the completion doc**

Create `docs/phase-1-complete.md`:

```markdown
# Phase 1 Complete

As of this commit, Phase 1 of the C++/Qt rewrite (the realtime preview escape-hatch checkpoint) is complete.

## What got built

- `fim::engine::WavetableBank` — loads 8 wavetable WAV pages into a contiguous float buffer with the wave-pointer table the FourSeas oscillator expects
- `fim::engine::WavetableVoice` — wraps the FourSeas oscillator with thread-safe atomic params and a callback-friendly RenderBlock; uses `std::shared_ptr<const WavetableBank>` accessed via free-function `std::atomic_load`/`store` for lock-free double-buffered bank swaps
- `fim::engine::RealtimeAudioEngine` — owns the voice plus a miniaudio playback device; implements PlayMode (steady/sweep/arpeggio), MIDI conversion, volume + invert, and exponential one-pole smoothing on volume
- `fim::ui::PreviewWindow` — minimal Qt window with X/Y/Z sliders, MIDI pitch slider, volume slider, Load Bank button, Play/Stop button. Throwaway — Phase 2 replaces it.
- 13 Catch2 tests (5 new in Phase 1: 2 wav_loader, 2 wavetable_bank, 2 wavetable_voice, 5 realtime_audio_engine)

## Escape hatch cleared

The Phase 1 checkpoint was: "can we hear audio coming out of the C++ rewrite via the real FourSeas engine?" Answer: yes. Manual verification on macOS confirmed:

- Bank loads successfully from `output_waves/audio_resynth/`
- Play button starts the audio device
- X/Y/Z slider drags change the timbre in real time
- Pitch slider changes the note
- Volume slider changes loudness without zipper noise (thanks to one-pole smoothing)
- Bank can be reloaded mid-playback without audio dropouts (atomic shared_ptr swap)

## Spec deviation worth noting

The spec mentioned vendoring three classes from `bindings.cpp@8d36c15`: `WavReader`, `WavetableEngine`, `StreamingEngine`. Phase 1 vendored only the first and third — the second (offline rendering) isn't on the realtime path and was deferred. We'll revisit it in Phase 3 if/when offline rendering becomes useful for DSP comparison testing.

## Next phase

Phase 2 — Main UI, no DSP. Replace the throwaway PreviewWindow with the production UI from `designs/start-state.svg`, including the 7 custom widgets (FileDropWidget, CardButton, AxisMorphSelector, CustomProgressBar, etc.), the QSettings persistence layer, the menu bar, and a stubbed Generate button that writes placeholder WAV files.
```

- [ ] **Step 7: Commit and push**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add docs/phase-1-complete.md
git commit -m "docs: mark Phase 1 complete"
git push
```

---

## Phase 1 done when:

1. ✅ App launches and displays the PreviewWindow with sliders + buttons
2. ✅ Loading a real bank populates the engine and enables controls
3. ✅ Clicking Play produces audible audio through the system output device
4. ✅ X/Y/Z sliders change timbre in real time without artifacts
5. ✅ Pitch slider changes note via MIDI conversion
6. ✅ Volume slider changes loudness without zipper noise
7. ✅ All 13 Catch2 tests pass locally
8. ✅ CI green on macOS, Ubuntu, Windows, and clang-format check
9. ✅ `docs/phase-1-complete.md` committed
