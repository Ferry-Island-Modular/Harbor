# Phase 3b Implementation Plan — Single-WAV DSP core (real audio)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Phase 2 `StubBankWriter` (placeholder sine waves) with a real single-WAV spectral resynthesis pipeline. After Phase 3b ships, dropping a `.wav` file into the app and clicking Generate produces eight playable wavetable WAV files containing actual cross-synthesis output, not stubs.

**Architecture:** Four new pure-C++ DSP units in `cpp/src/dsp/` built on the Phase 3a primitives (`WavLoader`, `Stft`, `RealFft`, `HannWindow`): `FftResampler` (class wrapping two `RealFft` instances for arbitrary length conversion via frequency-domain zero-pad/truncate), `SpectralModifier` (applies the Python tilt + stretch + phase-randomize transforms with a seedable RNG), `CycleExtractor` (one STFT frame → IFFT → resample → window → normalize → one wavetable cell), and `SingleWavGenerator` (top-level orchestration: load audio, run STFT, loop 8 pages × 64 cells, downsample, write WAVs). The Phase 2 `StubBankWriter` is deleted at the end and `SingleWavService` is rewired to call `SingleWavGenerator` instead.

**Tech Stack:** C++20, the Phase 3a DSP layer (`fim::dsp::WavLoader`, `Stft`, `RealFft`, `HannWindow`), `dr_wav` for output writing (already vendored), Catch2 v3 for tests. **No new dependencies.** libsamplerate is in the build but Phase 3b deliberately does not use it — FFT-based resampling via the existing `RealFft` primitives is mathematically equivalent to ideal sinc resampling for our integer ratios and produces a smaller, simpler implementation.

**Spec deviations from Python reference:**

- **Inverse FFT length is 2048, not 2049.** Python's `_extract_single_cycle` constructs a length-`2*N_bins - 1 = 2049` complex spectrum and does an odd-length IFFT. PFFFT only supports power-of-2 (×{1,3,5}) sizes ≥ 32, so we use a standard length-2048 inverse FFT on the rfft-format bins directly. This produces a real signal that's mathematically the correct inverse of the STFT frame, but is one sample shorter and slightly different in phase from Python's odd-length output. Acceptable per the "perceptual parity, not bit-exact" decision from Phase 3 brainstorming. Phase 3c oracle testing will quantify the divergence.
- **Zero-crossing search is removed.** In Python, `_extract_single_cycle` searches for a zero crossing where `zc + cycle_length < len(time_signal)`. Since `cycle_length = n_samples = 8192` is always larger than `len(time_signal) ≈ 2048`, the loop never finds a match and falls through to `start_idx = 0`. The C++ port skips the dead-code search entirely and uses `start_idx = 0` directly.
- **Resampling is FFT-based, not polyphase Kaiser.** Python's `baseclass.save_wavetables` uses `scipy.signal.resample_poly` with a Kaiser-β=5.0 window for the final 4× downsample (8192 → 2048 per cell). We use frequency-domain truncation via `RealFft`. For periodic signals at integer ratios this is equivalent to ideal sinc resampling (no anti-aliasing artifacts) and avoids both the libsamplerate dependency and the question of bit-exact matching scipy's polyphase filter design. The same `FftResampler` handles the upsample inside `CycleExtractor` (2048 → 8192) and the downsample in `SingleWavGenerator` (8192 → 2048).
- **Single morph mode only (Y0 + Z0).** The Phase 3b `SpectralModifier` implements ONLY the original Python behaviors: Y = spectral tilt, Z = phase randomization (plus the X-driven spectral stretch which Python applies as a side effect). The `AxisMorphSelector` UI buttons remain placeholder labels ("First option / Second option / Third option"); they don't yet switch behavior. Additional morph modes (Y1 formant, Y2 harmonic stretch, Z1 phase disperse, Z3 detune) land in Phase 3d.
- **X has two effects: frame selection AND spectral stretch.** This matches the Python behavior — `_generate_wavetable_from_audio` derives `frame_selection = int(x / 7.0 * (num_frames - 1))` AND passes the same `x` to `_spectral_modifications` which uses it for stretching. The UI labels X as "Scans the wave" which only describes the frame-selection effect; the stretching is invisible to the user but materially shapes the timbre. Faithful port; Phase 3d may revisit.
- **Phase randomization uses an unseeded RNG by default.** Python uses `np.random.uniform()` with no explicit seed, so each generate run produces different output. C++ uses `std::mt19937` seeded from `std::random_device` by default. Phase 3c will add a `Seed()` method on `SpectralModifier` so the oracle harness can produce reproducible runs for parity testing.
- **`SingleWavService::Generate()` no longer simulates progress.** Phase 2's stub generation took ~1 second, so the service ran a fake `sleep(125ms) × 8` loop to give the progress bar something to animate. Phase 3b's real DSP generation actually takes some time, and the `SingleWavGenerator::Generate()` invokes its progress callback after each of the 8 pages completes. The fake sleep loop is removed.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files:**

- `cpp/src/dsp/fft_resampler.h` / `fft_resampler.cpp` — `fim::dsp::FftResampler` class. Constructor takes `(input_length, output_length)`, both PFFFT-compatible. Owns two `RealFft` instances (one per length). `Resample(const std::vector<float>& input) -> std::vector<float>` does forward FFT → bin copy/truncate/zero-pad → inverse FFT → 1/N scaling.
- `cpp/src/dsp/spectral_modifier.h` / `spectral_modifier.cpp` — `fim::dsp::SpectralModifier` class. Holds a `std::mt19937` for phase randomization. `Apply(magnitude, phase, x, y, z)` modifies the 2D arrays in place: spectral tilt (Y), spectral stretch (X), phase randomization + smoothing (Z). Direct port of Python's `_spectral_modifications`.
- `cpp/src/dsp/cycle_extractor.h` / `cycle_extractor.cpp` — `fim::dsp::CycleExtractor` class. Owns a `RealFft` for the inverse transform, an `FftResampler` for the upsample step, and a precomputed `HannWindow` of `target_length`. `Extract(magnitude, phase, frame_index)` returns one `target_length`-sized oversampled cycle.
- `cpp/src/dsp/single_wav_generator.h` / `single_wav_generator.cpp` — `fim::dsp::SingleWavGenerator` class. Top-level orchestration. Constructor takes `(samples, oversample_factor, num_pages)` defaulting to `(2048, 4, 8)`. `Generate(input_path, output_dir, progress_cb)` does the full pipeline and writes `1.wav`..`8.wav`. Has an internal `WritePageToWav` helper using dr_wav (replaces the same logic in `StubBankWriter`).
- `cpp/tests/dsp_fft_resampler_test.cpp` — 5 tests: identity (same length), 2× upsample of a sine, 2× downsample of a sine, DC preservation, length correctness.
- `cpp/tests/dsp_spectral_modifier_test.cpp` — 4 tests: tilt at y=3 leaves bins unchanged (the neutral midpoint), tilt at y=7 brightens (high bins amplified), stretch identity at x where stretch_amount=1.0 (x=2.33 isn't representable; use a numerical near-identity check), seeded RNG produces reproducible phase output.
- `cpp/tests/dsp_cycle_extractor_test.cpp` — 4 tests: a single-bin spectrum (DC + one cosine bin) produces a clean cosine cycle; output peak is exactly 1.0 after normalization; output length matches `target_length`; an empty/zero spectrum produces zeros (no NaN from divide-by-zero in normalize step).

**Modified files:**

- `cpp/src/app/services/single_wav_service.h` — no changes (the public API stays the same).
- `cpp/src/app/services/single_wav_service.cpp` — replace the `StubBankWriter::WriteSineBank` call with `SingleWavGenerator::Generate`. Remove the `sleep(125ms) × 8` fake progress loop. The service still runs the work on `QThreadPool` and emits `progressChanged` / `generationFinished` / `generationFailed` signals, but progress is now driven by the generator's callback.
- `cpp/CMakeLists.txt` — add the four new DSP source files; remove `src/app/services/stub_bank_writer.cpp`.
- `cpp/tests/CMakeLists.txt` — add the three new test files and their implementation source dependencies; remove `stub_bank_writer_test.cpp` and `../src/app/services/stub_bank_writer.cpp`.

**Deleted files:**

- `cpp/src/app/services/stub_bank_writer.h`
- `cpp/src/app/services/stub_bank_writer.cpp`
- `cpp/tests/stub_bank_writer_test.cpp`

---

## Task 1: FftResampler

A stateful resampler that uses `RealFft` for both forward and inverse transforms, doing arbitrary length conversion via frequency-domain zero-pad (upsample) or truncation (downsample). Both lengths must be PFFFT-compatible (multiples of 32, factors {2,3,5}). Constructed once per `(input_length, output_length)` pair so the PFFFT setup tables are reused across many calls.

**Files:**
- Create: `cpp/src/dsp/fft_resampler.h`
- Create: `cpp/src/dsp/fft_resampler.cpp`
- Create: `cpp/tests/dsp_fft_resampler_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_fft_resampler_test.cpp`:

```cpp
#include "dsp/fft_resampler.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
}

TEST_CASE("FftResampler same-length is approximately identity",
          "[dsp][fft_resampler]") {
    constexpr std::size_t kN = 64;
    fim::dsp::FftResampler resampler(kN, kN);

    std::vector<float> input(kN);
    for (std::size_t n = 0; n < kN; ++n) {
        input[n] = std::sin(2.0f * kPi * 4.0f * n / kN);
    }

    const auto output = resampler.Resample(input);
    REQUIRE(output.size() == kN);
    for (std::size_t n = 0; n < kN; ++n) {
        REQUIRE_THAT(output[n], WithinAbs(input[n], 1e-4));
    }
}

TEST_CASE("FftResampler 2x upsample preserves a sine's frequency",
          "[dsp][fft_resampler]") {
    constexpr std::size_t kIn = 64;
    constexpr std::size_t kOut = 128;
    fim::dsp::FftResampler resampler(kIn, kOut);

    // Pure sine at bin 4 of the 64-sample input. After 2x upsample to 128
    // samples, the same physical frequency is at bin 4 of the 128-sample
    // output (its "bin" is the same in absolute frequency terms).
    std::vector<float> input(kIn);
    for (std::size_t n = 0; n < kIn; ++n) {
        input[n] = std::sin(2.0f * kPi * 4.0f * n / kIn);
    }

    const auto output = resampler.Resample(input);
    REQUIRE(output.size() == kOut);

    // Sample the upsampled output at the same time positions as the input.
    // For a 2x upsample, output index 2*n should equal input index n.
    for (std::size_t n = 0; n < kIn; ++n) {
        REQUIRE_THAT(output[2 * n], WithinAbs(input[n], 1e-3));
    }
}

TEST_CASE("FftResampler 2x downsample preserves a sine at the new rate",
          "[dsp][fft_resampler]") {
    constexpr std::size_t kIn = 128;
    constexpr std::size_t kOut = 64;
    fim::dsp::FftResampler resampler(kIn, kOut);

    // Pure sine at bin 4 of the 128-sample input.
    std::vector<float> input(kIn);
    for (std::size_t n = 0; n < kIn; ++n) {
        input[n] = std::sin(2.0f * kPi * 4.0f * n / kIn);
    }

    const auto output = resampler.Resample(input);
    REQUIRE(output.size() == kOut);

    // After 2x downsample, the same sine evaluated at the half-rate samples.
    // output[n] should equal input[2*n].
    for (std::size_t n = 0; n < kOut; ++n) {
        REQUIRE_THAT(output[n], WithinAbs(input[2 * n], 1e-3));
    }
}

TEST_CASE("FftResampler preserves DC", "[dsp][fft_resampler]") {
    constexpr std::size_t kIn = 64;
    constexpr std::size_t kOut = 256;
    fim::dsp::FftResampler resampler(kIn, kOut);

    std::vector<float> input(kIn, 0.5f);
    const auto output = resampler.Resample(input);
    REQUIRE(output.size() == kOut);

    for (float s : output) {
        REQUIRE_THAT(s, WithinAbs(0.5f, 1e-4));
    }
}

TEST_CASE("FftResampler 4x upsample (the wavetable use case)",
          "[dsp][fft_resampler]") {
    // This is the actual ratio used by CycleExtractor in Phase 3b:
    // a 2048-sample IFFT output gets upsampled to 8192. We use smaller
    // sizes here for test speed but the same 4x ratio.
    constexpr std::size_t kIn = 64;
    constexpr std::size_t kOut = 256;
    fim::dsp::FftResampler resampler(kIn, kOut);

    std::vector<float> input(kIn);
    for (std::size_t n = 0; n < kIn; ++n) {
        input[n] = std::cos(2.0f * kPi * 2.0f * n / kIn);
    }

    const auto output = resampler.Resample(input);
    REQUIRE(output.size() == kOut);
    // output[4*n] should equal input[n].
    for (std::size_t n = 0; n < kIn; ++n) {
        REQUIRE_THAT(output[4 * n], WithinAbs(input[n], 1e-3));
    }
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`. Add `dsp_fft_resampler_test.cpp` and `../src/dsp/fft_resampler.cpp`:

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
    dsp_fft_resampler_test.cpp
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
    ../src/dsp/fft_resampler.cpp
)
```

(`stub_bank_writer.cpp` and `stub_bank_writer_test.cpp` are still in the list — they get removed in Task 5.)

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/fft_resampler.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/fft_resampler.h`:

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include "dsp/real_fft.h"

namespace fim::dsp {

// FFT-based length resampler. Converts a real-valued signal of one length
// to another by going through the frequency domain: forward FFT → zero-pad
// (upsample) or truncate (downsample) the spectrum → inverse FFT → scale.
//
// For periodic signals at integer ratios, this is equivalent to ideal sinc
// resampling — no anti-aliasing artifacts, no filter design choices, no
// dependency on a separate resampling library.
//
// Constraints:
// - Both `input_length` and `output_length` must be supported by PFFFT
//   (multiples of 32 with prime factors only in {2, 3, 5}).
// - The resampler is stateful and reusable: construct once per
//   (input_length, output_length) pair and call Resample many times to
//   amortize the PFFFT setup cost.
//
// Limitations:
// - When downsampling, the new Nyquist bin (input bin index output_length/2)
//   is generally complex but the inverse FFT treats it as real. The
//   imaginary part is discarded. For typical wavetable content where the
//   cutoff bin has small magnitude this is negligible. Document the
//   deviation if it ever matters in practice.
class FftResampler {
public:
    FftResampler(std::size_t input_length, std::size_t output_length);

    std::size_t input_length() const { return input_length_; }
    std::size_t output_length() const { return output_length_; }

    // Resample the input to `output_length`. The input must have exactly
    // `input_length()` samples. Returns a fresh vector of size
    // `output_length()`.
    std::vector<float> Resample(const std::vector<float>& input) const;

private:
    std::size_t input_length_;
    std::size_t output_length_;
    RealFft input_fft_;
    RealFft output_fft_;
};

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/fft_resampler.cpp`:

```cpp
#include "dsp/fft_resampler.h"

#include <algorithm>
#include <cassert>
#include <complex>

namespace fim::dsp {

FftResampler::FftResampler(std::size_t input_length, std::size_t output_length)
    : input_length_(input_length),
      output_length_(output_length),
      input_fft_(input_length),
      output_fft_(output_length) {}

std::vector<float> FftResampler::Resample(const std::vector<float>& input) const {
    assert(input.size() == input_length_);

    // Step 1: forward FFT of the input. Bins are in standard rfft order.
    std::vector<std::complex<float>> input_bins(input_fft_.num_bins());
    input_fft_.Forward(input.data(), input_bins.data());

    // Step 2: build the output bins by truncating or zero-padding.
    std::vector<std::complex<float>> output_bins(output_fft_.num_bins(),
                                                 std::complex<float>(0.0f, 0.0f));
    const std::size_t copy_count = std::min(input_bins.size(), output_bins.size());
    for (std::size_t k = 0; k < copy_count; ++k) {
        output_bins[k] = input_bins[k];
    }

    // Step 3: inverse FFT into the output buffer.
    std::vector<float> output(output_length_);
    output_fft_.Inverse(output_bins.data(), output.data());

    // Step 4: PFFFT inverse is unnormalized. To preserve amplitude across
    // the resample, scale by 1/input_length. Derivation: backward(forward(x))
    // = N*x for unnormalized FFTs of length N. Selecting bins doesn't change
    // the magnitude of each retained bin, so backward_M(X') has effective
    // amplitude N*x at the M output samples. Dividing by N recovers x at the
    // resampled rate.
    const float scale = 1.0f / static_cast<float>(input_length_);
    for (float& s : output) {
        s *= scale;
    }
    return output;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/dsp/fft_resampler.cpp` to the `qt_add_executable(fim-config-tool ...)` block right after `src/dsp/stft.cpp`:

```cmake
    src/dsp/window.cpp
    src/dsp/wav_loader.cpp
    src/dsp/real_fft.cpp
    src/dsp/stft.cpp
    src/dsp/fft_resampler.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 45 tests pass (40 from Phase 3a + 5 new FftResampler tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/fft_resampler.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/fft_resampler.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_fft_resampler_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/fft_resampler.h cpp/src/dsp/fft_resampler.cpp \
        cpp/tests/dsp_fft_resampler_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add FftResampler for length conversion via PFFFT"
```

---

## Task 2: SpectralModifier

Direct port of Python's `_spectral_modifications` function. Applies three modifications in sequence to the magnitude/phase arrays of a STFT result: spectral tilt (driven by Y), spectral stretch (driven by X), phase randomization with bin-to-bin coherence smoothing (driven by Z). Holds an `std::mt19937` so phase randomization can be made reproducible by seeding.

**Files:**
- Create: `cpp/src/dsp/spectral_modifier.h`
- Create: `cpp/src/dsp/spectral_modifier.cpp`
- Create: `cpp/tests/dsp_spectral_modifier_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_spectral_modifier_test.cpp`:

```cpp
#include "dsp/spectral_modifier.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

// Build a synthetic 2D magnitude/phase array shaped (num_bins x num_frames)
// with known content for tests. All bins start at magnitude 1.0 and phase 0.
struct SyntheticSpectrum {
    std::vector<std::vector<float>> magnitude;
    std::vector<std::vector<float>> phase;
};

SyntheticSpectrum MakeFlat(std::size_t num_bins, std::size_t num_frames,
                           float mag_value = 1.0f) {
    SyntheticSpectrum s;
    s.magnitude.assign(num_bins, std::vector<float>(num_frames, mag_value));
    s.phase.assign(num_bins, std::vector<float>(num_frames, 0.0f));
    return s;
}

}  // namespace

TEST_CASE("SpectralModifier tilt at y=3 leaves magnitudes nearly unchanged",
          "[dsp][spectral_modifier]") {
    // y_norm = 3/7, tilt exponent = (2*3/7 - 1) * freq_idx/num_bins * 5
    //        = (-1/7) * freq_idx/num_bins * 5
    // Not exactly zero — it dims the high bins slightly. We allow a generous
    // tolerance (factor of e^(-5/7) ~= 0.49 at the top bin).
    auto spectrum = MakeFlat(/*num_bins=*/8, /*num_frames=*/4);
    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    // Use x=4 (stretch_amount = 0.5 + 4/7 * 1.5 ~= 1.357 — nontrivial but
    // shouldn't blow up bin 0). z=0 disables phase modification entirely.
    modifier.Apply(spectrum.magnitude, spectrum.phase, /*x=*/4, /*y=*/3, /*z=*/0);

    // Bin 0 (DC) should be approximately preserved by tilt.
    for (std::size_t f = 0; f < 4; ++f) {
        REQUIRE_THAT(spectrum.magnitude[0][f], WithinAbs(1.0f, 0.5f));
    }
}

TEST_CASE("SpectralModifier tilt at y=7 brightens (high bins amplified relative to low)",
          "[dsp][spectral_modifier]") {
    auto spectrum = MakeFlat(/*num_bins=*/16, /*num_frames=*/2);
    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(spectrum.magnitude, spectrum.phase, /*x=*/0, /*y=*/7, /*z=*/0);

    // After tilt at y=7, bin 15 should have a much larger magnitude than
    // bin 0. The tilt formula at y_norm=1 is exp(freq_idx / num_bins * 5),
    // so bin 15/16 has roughly exp(15/16 * 5) ~= 113x bin 0.
    REQUIRE(spectrum.magnitude[15][0] > 5.0f * spectrum.magnitude[0][0]);
}

TEST_CASE("SpectralModifier with z=0 leaves the phase array unchanged",
          "[dsp][spectral_modifier]") {
    auto spectrum = MakeFlat(/*num_bins=*/8, /*num_frames=*/4);
    // Set non-zero starting phases.
    for (auto& row : spectrum.phase) {
        for (auto& p : row) {
            p = 0.5f;
        }
    }
    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(spectrum.magnitude, spectrum.phase, /*x=*/3, /*y=*/3, /*z=*/0);

    for (const auto& row : spectrum.phase) {
        for (float p : row) {
            REQUIRE_THAT(p, WithinAbs(0.5f, 1e-6));
        }
    }
}

TEST_CASE("SpectralModifier with z>0 produces deterministic output for a fixed seed",
          "[dsp][spectral_modifier]") {
    // Apply with the same seed twice; outputs must be bit-identical.
    auto spec_a = MakeFlat(/*num_bins=*/16, /*num_frames=*/4);
    auto spec_b = MakeFlat(/*num_bins=*/16, /*num_frames=*/4);

    fim::dsp::SpectralModifier mod_a(/*seed=*/12345);
    fim::dsp::SpectralModifier mod_b(/*seed=*/12345);

    mod_a.Apply(spec_a.magnitude, spec_a.phase, /*x=*/3, /*y=*/3, /*z=*/5);
    mod_b.Apply(spec_b.magnitude, spec_b.phase, /*x=*/3, /*y=*/3, /*z=*/5);

    for (std::size_t k = 0; k < 16; ++k) {
        for (std::size_t f = 0; f < 4; ++f) {
            REQUIRE(spec_a.magnitude[k][f] == spec_b.magnitude[k][f]);
            REQUIRE(spec_a.phase[k][f] == spec_b.phase[k][f]);
        }
    }
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`:

```cmake
    dsp_fft_resampler_test.cpp
    dsp_spectral_modifier_test.cpp
    ../src/engine/single_header_impls.cpp
```

And in the source list:

```cmake
    ../src/dsp/fft_resampler.cpp
    ../src/dsp/spectral_modifier.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/spectral_modifier.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/spectral_modifier.h`:

```cpp
#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace fim::dsp {

// Applies the Phase 3b "original" spectral modifications (tilt + stretch +
// phase randomize) to a 2D magnitude/phase array in place. Direct port of
// Python's AudioResynthWavetableGenerator._spectral_modifications.
//
// Parameter semantics:
// - x in [0, 7]: spectral envelope stretch. stretch_amount in [0.5, 2.0].
//   Values < 1 compress the envelope toward DC; values > 1 spread it
//   toward the Nyquist.
// - y in [0, 7]: spectral tilt. y=0 darkens (low bins amplified, highs
//   attenuated). y=7 brightens (highs amplified). y=3 or 4 is approximately
//   neutral.
// - z in [0, 7]: phase randomization strength. z=0 leaves phase untouched.
//   Higher z blends the original phase with random uniform phase, then
//   smooths adjacent bins for a formant-like effect.
//
// The 2D arrays are indexed as magnitude[bin][frame] (matches Python's
// numpy convention from np.abs(stft_result)).
class SpectralModifier {
public:
    // Default-constructed: seeds the RNG from std::random_device.
    SpectralModifier();

    // Seeded: produces reproducible phase output. Used by tests and by the
    // Phase 3c oracle harness.
    explicit SpectralModifier(std::uint32_t seed);

    // Reseed the internal RNG. Useful for resetting between generate runs.
    void Seed(std::uint32_t seed);

    // Apply tilt + stretch + phase modifications in place. Both arrays
    // must have the same outer dimension (num_bins) and the same inner
    // dimension (num_frames) per row.
    void Apply(std::vector<std::vector<float>>& magnitude,
               std::vector<std::vector<float>>& phase, int x, int y, int z);

private:
    std::mt19937 rng_;
};

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/spectral_modifier.cpp`. This is the longest implementation in the plan because it ports three sequential transforms with their interpolation, RNG, and edge cases.

```cpp
#include "dsp/spectral_modifier.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fim::dsp {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// Compute the time-averaged spectral envelope: for each bin, the mean
// magnitude across all frames.
std::vector<float> SpectralEnvelope(
    const std::vector<std::vector<float>>& magnitude) {
    const std::size_t num_bins = magnitude.size();
    std::vector<float> env(num_bins, 0.0f);
    if (num_bins == 0 || magnitude[0].empty()) {
        return env;
    }
    const std::size_t num_frames = magnitude[0].size();
    for (std::size_t k = 0; k < num_bins; ++k) {
        float sum = 0.0f;
        for (std::size_t f = 0; f < num_frames; ++f) {
            sum += magnitude[k][f];
        }
        env[k] = sum / static_cast<float>(num_frames);
    }
    return env;
}

}  // namespace

SpectralModifier::SpectralModifier() : rng_(std::random_device{}()) {}

SpectralModifier::SpectralModifier(std::uint32_t seed) : rng_(seed) {}

void SpectralModifier::Seed(std::uint32_t seed) { rng_.seed(seed); }

void SpectralModifier::Apply(std::vector<std::vector<float>>& magnitude,
                             std::vector<std::vector<float>>& phase, int x, int y,
                             int z) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    const float x_norm = static_cast<float>(x) / 7.0f;
    const float y_norm = static_cast<float>(y) / 7.0f;
    const float z_norm = static_cast<float>(z) / 7.0f;

    // ---- 1. Spectral tilt (y) ----
    // Python: tilt_factor = exp(((y_norm * 2) - 1) * freq_idx / len(freq_idx) * 5)
    {
        const float exponent_scale = ((y_norm * 2.0f) - 1.0f) * 5.0f;
        const float inv_num_bins = 1.0f / static_cast<float>(num_bins);
        for (std::size_t k = 0; k < num_bins; ++k) {
            const float tilt = std::exp(exponent_scale * static_cast<float>(k) * inv_num_bins);
            for (std::size_t f = 0; f < num_frames; ++f) {
                magnitude[k][f] *= tilt;
            }
        }
    }

    // ---- 2. Spectral stretch (x) ----
    // Python: stretch_amount = 0.5 + x_norm * 1.5  in [0.5, 2.0]
    //         env = mean(magnitude_modified, axis=time)
    //         stretched_env[i] = lerp(env, i / stretch_amount)
    //         magnitude *= (stretched_env / (mean(env) + 1e-10))[:, np.newaxis]
    {
        const float stretch_amount = 0.5f + x_norm * 1.5f;
        const auto env = SpectralEnvelope(magnitude);

        // Linear-interpolate the envelope at non-integer indices.
        std::vector<float> stretched_env(num_bins, 0.0f);
        for (std::size_t i = 0; i < num_bins; ++i) {
            const float src_idx = static_cast<float>(i) / stretch_amount;
            if (src_idx < static_cast<float>(num_bins) - 1.0f) {
                const std::size_t idx_floor = static_cast<std::size_t>(std::floor(src_idx));
                const std::size_t idx_ceil = idx_floor + 1;
                const float fraction = src_idx - static_cast<float>(idx_floor);
                stretched_env[i] = env[idx_floor] * (1.0f - fraction) + env[idx_ceil] * fraction;
            }
            // else: stretched_env[i] stays 0, matching Python's `np.zeros_like(env)` init.
        }

        // Compute mean(env) for the per-bin scaling factor.
        float env_sum = 0.0f;
        for (float v : env) {
            env_sum += v;
        }
        const float env_mean = env_sum / static_cast<float>(num_bins) + 1e-10f;

        for (std::size_t k = 0; k < num_bins; ++k) {
            const float scale = stretched_env[k] / env_mean;
            for (std::size_t f = 0; f < num_frames; ++f) {
                magnitude[k][f] *= scale;
            }
        }
    }

    // ---- 3. Phase manipulation (z) ----
    // Python: only runs if z_norm > 0.
    //   random_phase = uniform(0, 2*pi, shape)
    //   phase_modified = (1 - z_norm) * phase_modified + z_norm * random_phase
    //   coherence = z_norm * 0.5
    //   smoothed_phase[0] = phase_modified[0]
    //   for i in 1..num_bins:
    //     smoothed_phase[i] = phase_modified[i] * (1 - coherence) +
    //                         (smoothed_phase[i-1] + uniform(-0.1, 0.1)) * coherence
    //   phase_modified = smoothed_phase
    if (z_norm > 0.0f) {
        std::uniform_real_distribution<float> uniform_full(0.0f, kTwoPi);
        std::uniform_real_distribution<float> uniform_jitter(-0.1f, 0.1f);

        // First pass: blend with random phase. Note that we draw the random
        // values in (bin, frame) order matching Python's numpy.random.uniform
        // which fills row-major. To stay deterministic on a given seed, we
        // iterate bins outermost.
        for (std::size_t k = 0; k < num_bins; ++k) {
            for (std::size_t f = 0; f < num_frames; ++f) {
                const float random_phase = uniform_full(rng_);
                phase[k][f] = (1.0f - z_norm) * phase[k][f] + z_norm * random_phase;
            }
        }

        // Second pass: bin-wise smoothing. The smoothing accumulates from
        // bin 0 upward independently for each frame.
        const float coherence = z_norm * 0.5f;
        const float one_minus_coherence = 1.0f - coherence;
        for (std::size_t f = 0; f < num_frames; ++f) {
            float prev = phase[0][f];
            for (std::size_t k = 1; k < num_bins; ++k) {
                const float jitter = uniform_jitter(rng_);
                const float smoothed = phase[k][f] * one_minus_coherence +
                                       (prev + jitter) * coherence;
                phase[k][f] = smoothed;
                prev = smoothed;
            }
        }
    }
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/dsp/spectral_modifier.cpp`:

```cmake
    src/dsp/fft_resampler.cpp
    src/dsp/spectral_modifier.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 49 tests pass (45 from after Task 1 + 4 new SpectralModifier tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/spectral_modifier.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/spectral_modifier.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_spectral_modifier_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/spectral_modifier.h cpp/src/dsp/spectral_modifier.cpp \
        cpp/tests/dsp_spectral_modifier_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add SpectralModifier porting Python tilt/stretch/phase"
```

---

## Task 3: CycleExtractor

Takes one STFT frame's magnitude + phase and produces a single oversampled wavetable cycle. Pipeline: reconstruct complex bins from mag/phase → inverse FFT (length `fft_size`) → upsample to `target_length` via `FftResampler` → apply Hann window → normalize to peak 1.0.

**Files:**
- Create: `cpp/src/dsp/cycle_extractor.h`
- Create: `cpp/src/dsp/cycle_extractor.cpp`
- Create: `cpp/tests/dsp_cycle_extractor_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_cycle_extractor_test.cpp`:

```cpp
#include "dsp/cycle_extractor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

// Build a synthetic mag/phase array shaped (num_bins x num_frames) where
// only the specified bin has nonzero magnitude in the chosen frame, with
// phase 0. This produces a clean cosine when inverted.
struct OneBinSpectrum {
    std::vector<std::vector<float>> magnitude;
    std::vector<std::vector<float>> phase;
};

OneBinSpectrum MakeOneBin(std::size_t num_bins, std::size_t num_frames,
                          std::size_t bin, std::size_t frame, float mag) {
    OneBinSpectrum s;
    s.magnitude.assign(num_bins, std::vector<float>(num_frames, 0.0f));
    s.phase.assign(num_bins, std::vector<float>(num_frames, 0.0f));
    s.magnitude[bin][frame] = mag;
    return s;
}

}  // namespace

TEST_CASE("CycleExtractor produces output of the requested length",
          "[dsp][cycle_extractor]") {
    constexpr std::size_t kFft = 64;
    constexpr std::size_t kTarget = 256;
    fim::dsp::CycleExtractor extractor(kFft, kTarget);

    auto spectrum = MakeOneBin(kFft / 2 + 1, /*num_frames=*/2, /*bin=*/4,
                               /*frame=*/0, /*mag=*/1.0f);
    const auto cycle = extractor.Extract(spectrum.magnitude, spectrum.phase, 0);
    REQUIRE(cycle.size() == kTarget);
}

TEST_CASE("CycleExtractor normalizes the output to peak 1.0",
          "[dsp][cycle_extractor]") {
    constexpr std::size_t kFft = 64;
    constexpr std::size_t kTarget = 256;
    fim::dsp::CycleExtractor extractor(kFft, kTarget);

    auto spectrum = MakeOneBin(kFft / 2 + 1, /*num_frames=*/2, /*bin=*/4,
                               /*frame=*/0, /*mag=*/1.0f);
    const auto cycle = extractor.Extract(spectrum.magnitude, spectrum.phase, 0);

    float peak = 0.0f;
    for (float s : cycle) {
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE_THAT(peak, WithinAbs(1.0f, 1e-4));
}

TEST_CASE("CycleExtractor on a zero spectrum returns zeros without NaN",
          "[dsp][cycle_extractor]") {
    constexpr std::size_t kFft = 64;
    constexpr std::size_t kTarget = 256;
    fim::dsp::CycleExtractor extractor(kFft, kTarget);

    std::vector<std::vector<float>> magnitude(kFft / 2 + 1, std::vector<float>(2, 0.0f));
    std::vector<std::vector<float>> phase(kFft / 2 + 1, std::vector<float>(2, 0.0f));
    const auto cycle = extractor.Extract(magnitude, phase, 0);
    REQUIRE(cycle.size() == kTarget);
    for (float s : cycle) {
        REQUIRE(std::isfinite(s));
        REQUIRE_THAT(s, WithinAbs(0.0f, 1e-6));
    }
}

TEST_CASE("CycleExtractor produces a non-trivial waveform for a multi-bin spectrum",
          "[dsp][cycle_extractor]") {
    constexpr std::size_t kFft = 64;
    constexpr std::size_t kTarget = 256;
    fim::dsp::CycleExtractor extractor(kFft, kTarget);

    // Two nonzero bins → an output with at least two distinct extrema.
    std::vector<std::vector<float>> magnitude(kFft / 2 + 1, std::vector<float>(1, 0.0f));
    std::vector<std::vector<float>> phase(kFft / 2 + 1, std::vector<float>(1, 0.0f));
    magnitude[2][0] = 1.0f;
    magnitude[5][0] = 0.5f;

    const auto cycle = extractor.Extract(magnitude, phase, 0);

    // The Hann window forces edges to ~0; somewhere in the middle there
    // should be a sample with abs > 0.5.
    bool has_significant_value = false;
    for (float s : cycle) {
        if (std::abs(s) > 0.5f) {
            has_significant_value = true;
            break;
        }
    }
    REQUIRE(has_significant_value);
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`:

```cmake
    dsp_spectral_modifier_test.cpp
    dsp_cycle_extractor_test.cpp
    ../src/engine/single_header_impls.cpp
```

And in the source list:

```cmake
    ../src/dsp/spectral_modifier.cpp
    ../src/dsp/cycle_extractor.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/cycle_extractor.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/cycle_extractor.h`:

```cpp
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/fft_resampler.h"
#include "dsp/real_fft.h"

namespace fim::dsp {

// Extracts a single oversampled wavetable cycle from one STFT frame's
// magnitude + phase arrays. Direct port of the meaningful parts of Python's
// AudioResynthWavetableGenerator._extract_single_cycle, with the dead-code
// zero-crossing search removed and the length-2049 IFFT replaced by a
// standard length-fft_size IFFT.
//
// Pipeline:
//   1. Reconstruct complex bins from magnitude * exp(i * phase) for the
//      chosen frame.
//   2. Inverse FFT (length fft_size) → real signal of fft_size samples.
//   3. FftResample fft_size → target_length (typically 2048 → 8192 in the
//      Phase 3b wavetable use case).
//   4. Apply a precomputed Hann window of target_length to taper the edges.
//   5. Normalize to peak 1.0 if there's any non-zero content; otherwise
//      return zeros.
//
// Constructor parameters fix the inverse FFT and resample sizes; reuse a
// single CycleExtractor across many Extract calls to amortize setup cost.
class CycleExtractor {
public:
    CycleExtractor(std::size_t fft_size, std::size_t target_length);

    std::size_t fft_size() const { return fft_size_; }
    std::size_t target_length() const { return target_length_; }

    // Extract a cycle from the spectrum. magnitude and phase must be 2D
    // arrays shaped (num_bins x num_frames) with num_bins == fft_size/2 + 1.
    // frame_index selects which time slice of the STFT to use.
    std::vector<float> Extract(const std::vector<std::vector<float>>& magnitude,
                               const std::vector<std::vector<float>>& phase,
                               std::size_t frame_index) const;

private:
    std::size_t fft_size_;
    std::size_t target_length_;
    RealFft inverse_fft_;
    FftResampler resampler_;
    std::vector<float> window_;
};

}  // namespace fim::dsp
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/cycle_extractor.cpp`:

```cpp
#include "dsp/cycle_extractor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>

#include "dsp/window.h"

namespace fim::dsp {

CycleExtractor::CycleExtractor(std::size_t fft_size, std::size_t target_length)
    : fft_size_(fft_size),
      target_length_(target_length),
      inverse_fft_(fft_size),
      resampler_(fft_size, target_length),
      window_(HannWindow(target_length)) {}

std::vector<float> CycleExtractor::Extract(
    const std::vector<std::vector<float>>& magnitude,
    const std::vector<std::vector<float>>& phase, std::size_t frame_index) const {
    const std::size_t num_bins = inverse_fft_.num_bins();
    assert(magnitude.size() == num_bins);
    assert(phase.size() == num_bins);

    // Step 1: reconstruct complex bins from mag/phase for the chosen frame.
    std::vector<std::complex<float>> bins(num_bins);
    for (std::size_t k = 0; k < num_bins; ++k) {
        const float mag = magnitude[k][frame_index];
        const float ph = phase[k][frame_index];
        bins[k] = std::complex<float>(mag * std::cos(ph), mag * std::sin(ph));
    }

    // Step 2: inverse FFT to time domain. Output length = fft_size.
    std::vector<float> time_signal(fft_size_);
    inverse_fft_.Inverse(bins.data(), time_signal.data());

    // Step 3: resample fft_size → target_length.
    std::vector<float> cycle = resampler_.Resample(time_signal);

    // Step 4: apply the Hann window to taper edges.
    for (std::size_t i = 0; i < target_length_; ++i) {
        cycle[i] *= window_[i];
    }

    // Step 5: normalize to peak 1.0 (or leave as zeros if silent).
    float peak = 0.0f;
    for (float s : cycle) {
        peak = std::max(peak, std::abs(s));
    }
    if (peak > 0.0f) {
        const float inv_peak = 1.0f / peak;
        for (float& s : cycle) {
            s *= inv_peak;
        }
    }

    return cycle;
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`:

```cmake
    src/dsp/spectral_modifier.cpp
    src/dsp/cycle_extractor.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 53 tests pass (49 from after Task 2 + 4 new CycleExtractor tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/cycle_extractor.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/cycle_extractor.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_cycle_extractor_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/cycle_extractor.h cpp/src/dsp/cycle_extractor.cpp \
        cpp/tests/dsp_cycle_extractor_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add CycleExtractor for single wavetable cells"
```

---

## Task 4: SingleWavGenerator

The top-level orchestration class. Loads the input WAV, runs STFT, loops over 8 Z values × 64 (X, Y) cells per page, downsamples each cell from 8192 to 2048 samples, and writes 8 WAV files. No Qt dependency — pure C++ so it can be unit-tested in isolation later if needed (though Phase 3b doesn't add unit tests for it; manual end-to-end verification via the UI is sufficient).

**Files:**
- Create: `cpp/src/dsp/single_wav_generator.h`
- Create: `cpp/src/dsp/single_wav_generator.cpp`
- Modify: `cpp/CMakeLists.txt`

This task does NOT include unit tests because the meaningful behavior is end-to-end (file I/O, multi-page coordination, dr_wav output) and would essentially duplicate the manual verification step. The DSP pieces it composes (`FftResampler`, `SpectralModifier`, `CycleExtractor`) are all unit-tested individually.

- [ ] **Step 1: Write the header**

Create `cpp/src/dsp/single_wav_generator.h`:

```cpp
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>

namespace fim::dsp {

// Top-level single-WAV wavetable generator. Loads an input audio file,
// runs STFT analysis, loops over 8 Z pages × 64 (X, Y) cells per page
// (the FourSeas wavetable grid), downsamples each oversampled cell to the
// final wavetable length, and writes 8 WAV files (1.wav .. 8.wav) into
// the output directory.
//
// This class replaces the Phase 2 fim::app::StubBankWriter as the work
// performed by fim::app::SingleWavService::Generate().
//
// Defaults match the Python AudioResynthWavetableGenerator:
//   samples            = 2048   (per-cycle final length)
//   oversample_factor  = 4      (internal oversampling ratio)
//   num_pages          = 8      (Z dimension, fixed by hardware)
class SingleWavGenerator {
public:
    explicit SingleWavGenerator(std::size_t samples = 2048,
                                std::size_t oversample_factor = 4,
                                std::size_t num_pages = 8);

    // Progress callback type. Called from the same thread that invoked
    // Generate(); the caller is responsible for marshalling to a UI thread
    // if needed.
    using ProgressCallback = std::function<void(int percent)>;

    // Run the full pipeline. Returns false on any I/O or DSP error.
    // The output directory is created if it doesn't exist.
    bool Generate(const std::filesystem::path& input_audio_path,
                  const std::filesystem::path& output_directory,
                  const ProgressCallback& on_progress = {}) const;

    std::size_t samples() const { return samples_; }
    std::size_t oversample_factor() const { return oversample_factor_; }
    std::size_t n_samples() const { return n_samples_; }
    std::size_t num_pages() const { return num_pages_; }

private:
    bool WritePageToWav(const std::filesystem::path& path,
                        const std::vector<float>& downsampled_page) const;

    std::size_t samples_;
    std::size_t oversample_factor_;
    std::size_t n_samples_;  // = samples * oversample_factor
    std::size_t num_pages_;
};

}  // namespace fim::dsp
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/dsp/single_wav_generator.cpp`. This is the largest single file in the plan (~200 LOC). It composes everything we've built.

```cpp
#include "dsp/single_wav_generator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dr_wav.h"
#include "dsp/cycle_extractor.h"
#include "dsp/fft_resampler.h"
#include "dsp/spectral_modifier.h"
#include "dsp/stft.h"
#include "dsp/wav_loader.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kStftFftSize = 2048;
constexpr std::size_t kStftHopSize = 1024;
constexpr std::size_t kCellsPerSide = 8;
constexpr std::size_t kCellsPerPage = kCellsPerSide * kCellsPerSide;  // 64
constexpr std::uint32_t kOutputSampleRate = 44100;

}  // namespace

SingleWavGenerator::SingleWavGenerator(std::size_t samples,
                                       std::size_t oversample_factor,
                                       std::size_t num_pages)
    : samples_(samples),
      oversample_factor_(oversample_factor),
      n_samples_(samples * oversample_factor),
      num_pages_(num_pages) {}

bool SingleWavGenerator::Generate(const std::filesystem::path& input_audio_path,
                                  const std::filesystem::path& output_directory,
                                  const ProgressCallback& on_progress) const {
    // Step 1: load and normalize the input audio.
    auto loaded = LoadWav(input_audio_path, /*normalize=*/true);
    if (!loaded.has_value() || loaded->samples.empty()) {
        return false;
    }

    // Step 2: ensure the output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    // Step 3: STFT analysis. Cached across pages — same input audio for all 8 z values.
    Stft stft(kStftFftSize, kStftHopSize);
    const auto bins = stft.Analyze(loaded->samples);
    if (bins.empty()) {
        // Audio is shorter than fft_size. Cannot resynthesize.
        return false;
    }
    const auto magnitude = Magnitude(bins);
    const auto phase = Phase(bins);
    const std::size_t num_frames = bins.size();

    // Stft::Analyze and the Magnitude/Phase helpers return arrays indexed
    // [frame][bin] (matching the Stft::Analyze internal frame loop). But
    // SpectralModifier and CycleExtractor expect [bin][frame] (matching
    // Python's numpy convention from scipy.signal.stft, which produces a
    // (num_bins, num_frames) shape). Transpose once here so the inner loop
    // can pass the same arrays to both.
    const std::size_t num_bins = stft.num_bins();
    std::vector<std::vector<float>> magnitude_t(num_bins,
                                                std::vector<float>(num_frames, 0.0f));
    std::vector<std::vector<float>> phase_t(num_bins,
                                            std::vector<float>(num_frames, 0.0f));
    for (std::size_t f = 0; f < num_frames; ++f) {
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude_t[k][f] = magnitude[f][k];
            phase_t[k][f] = phase[f][k];
        }
    }

    // Step 4: prepare DSP objects (constructed once, reused across cells).
    CycleExtractor extractor(kStftFftSize, n_samples_);
    FftResampler downsampler(n_samples_, samples_);

    // Step 5: for each Z page, generate 64 cells, downsample, and write.
    for (std::size_t z = 0; z < num_pages_; ++z) {
        // Use a fresh SpectralModifier per page so phase randomization is
        // varied between pages but reproducible within a single Generate
        // call. Phase 3c may make the seed configurable.
        SpectralModifier modifier;

        // Each cell needs a fresh copy of the magnitude/phase to modify.
        // Cells in the page are stored sequentially as samples_ * 64 floats
        // after downsampling.
        std::vector<float> page_downsampled;
        page_downsampled.reserve(samples_ * kCellsPerPage);

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                auto mag_copy = magnitude_t;
                auto phase_copy = phase_t;
                modifier.Apply(mag_copy, phase_copy, static_cast<int>(x),
                               static_cast<int>(y), static_cast<int>(z));

                const std::size_t frame_selection = static_cast<std::size_t>(
                    static_cast<float>(x) / 7.0f * static_cast<float>(num_frames - 1));

                const auto cell_oversampled =
                    extractor.Extract(mag_copy, phase_copy, frame_selection);

                const auto cell_downsampled = downsampler.Resample(cell_oversampled);
                page_downsampled.insert(page_downsampled.end(), cell_downsampled.begin(),
                                        cell_downsampled.end());
            }
        }

        // Step 6: globally normalize the page so the max abs value across
        // all 131072 samples is 1.0. Matches Python's save_wavetables which
        // divides by the global max before int16 quantization.
        float page_peak = 0.0f;
        for (float s : page_downsampled) {
            page_peak = std::max(page_peak, std::abs(s));
        }
        if (page_peak > 0.0f) {
            const float inv = 1.0f / page_peak;
            for (float& s : page_downsampled) {
                s *= inv;
            }
        }

        const auto path = output_directory / (std::to_string(z + 1) + ".wav");
        if (!WritePageToWav(path, page_downsampled)) {
            return false;
        }

        if (on_progress) {
            const int percent = static_cast<int>((z + 1) * 100 / num_pages_);
            on_progress(percent);
        }
    }

    return true;
}

bool SingleWavGenerator::WritePageToWav(const std::filesystem::path& path,
                                        const std::vector<float>& downsampled_page) const {
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

    std::vector<std::int16_t> int_samples(downsampled_page.size());
    for (std::size_t i = 0; i < downsampled_page.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, downsampled_page[i]));
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
    src/dsp/cycle_extractor.cpp
    src/dsp/single_wav_generator.cpp
```

- [ ] **Step 4: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. The new generator is now compiled but not yet wired into `SingleWavService`.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/single_wav_generator.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/single_wav_generator.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/single_wav_generator.h cpp/src/dsp/single_wav_generator.cpp \
        cpp/CMakeLists.txt
git commit -m "feat(cpp): add SingleWavGenerator orchestrating the DSP pipeline"
```

---

## Task 5: Wire into SingleWavService and delete StubBankWriter

Replace the Phase 2 stub. `SingleWavService::Generate()` switches from calling `StubBankWriter::WriteSineBank` (with the fake 1-second sleep loop) to calling `SingleWavGenerator::Generate` (with real progress reported via the callback). The `StubBankWriter` files and its tests are deleted.

**Files:**
- Modify: `cpp/src/app/services/single_wav_service.cpp`
- Delete: `cpp/src/app/services/stub_bank_writer.h`
- Delete: `cpp/src/app/services/stub_bank_writer.cpp`
- Delete: `cpp/tests/stub_bank_writer_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Rewrite `single_wav_service.cpp`**

Overwrite `cpp/src/app/services/single_wav_service.cpp` with:

```cpp
#include "app/services/single_wav_service.h"

#include <QThreadPool>

#include <filesystem>

#include "dsp/single_wav_generator.h"

namespace fim::app {

SingleWavService::SingleWavService(QObject* parent) : QObject(parent) {}

void SingleWavService::SetInputFile(const QString& path) {
    input_file_ = path;
}
QString SingleWavService::InputFile() const {
    return input_file_;
}

void SingleWavService::SetOutputDirectory(const QString& path) {
    output_directory_ = path;
}
QString SingleWavService::OutputDirectory() const {
    return output_directory_;
}

bool SingleWavService::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void SingleWavService::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    const QString in_file = input_file_;
    const QString out_dir = output_directory_;

    QThreadPool::globalInstance()->start([this, in_file, out_dir]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path output_path(out_dir.toStdString());

        fim::dsp::SingleWavGenerator generator;
        const bool ok = generator.Generate(input_path, output_path,
                                           [this](int percent) {
                                               // Cross-thread signal — Qt
                                               // auto-queues this onto the
                                               // GUI thread.
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

- [ ] **Step 2: Delete the obsolete StubBankWriter files**

```bash
rm /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/stub_bank_writer.h
rm /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/stub_bank_writer.cpp
rm /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/stub_bank_writer_test.cpp
```

- [ ] **Step 3: Drop StubBankWriter from CMakeLists.txt**

Modify `cpp/CMakeLists.txt`. Remove the `src/app/services/stub_bank_writer.cpp` line from the `qt_add_executable(fim-config-tool ...)` block. The final block should look like:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/app/services/single_wav_service.cpp
    src/dsp/window.cpp
    src/dsp/wav_loader.cpp
    src/dsp/real_fft.cpp
    src/dsp/stft.cpp
    src/dsp/fft_resampler.cpp
    src/dsp/spectral_modifier.cpp
    src/dsp/cycle_extractor.cpp
    src/dsp/single_wav_generator.cpp
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

- [ ] **Step 4: Drop StubBankWriter from tests/CMakeLists.txt**

Modify `cpp/tests/CMakeLists.txt`. Remove `stub_bank_writer_test.cpp` from the test sources and `../src/app/services/stub_bank_writer.cpp` from the implementation sources. The final block should look like:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    dsp_window_test.cpp
    dsp_wav_loader_test.cpp
    dsp_real_fft_test.cpp
    dsp_stft_test.cpp
    dsp_fft_resampler_test.cpp
    dsp_spectral_modifier_test.cpp
    dsp_cycle_extractor_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
    ../src/dsp/window.cpp
    ../src/dsp/wav_loader.cpp
    ../src/dsp/real_fft.cpp
    ../src/dsp/stft.cpp
    ../src/dsp/fft_resampler.cpp
    ../src/dsp/spectral_modifier.cpp
    ../src/dsp/cycle_extractor.cpp
)
```

(Note: `single_wav_generator.cpp` is NOT in the test target — it pulls in dr_wav and isn't unit-tested. Its components are tested individually.)

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. `SingleWavService` now calls `SingleWavGenerator`.

- [ ] **Step 6: Run the app and manually verify end-to-end**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Walk through the flow:

1. Launcher → click "Choose" on the "Any wav" card
2. Drop or browse to a real `.wav` file (any audio — speech, music, drum loop, etc.)
3. Click "Generate wavetable bank"
4. Watch the progress bar fill (now driven by the real generator's per-page callbacks)
5. After generation, click "Play steady tone" in the embedded preview
6. **Listen carefully:** the output should NOT be the placeholder sine waves anymore. It should sound like a recognizable spectral resynthesis of the input file — pitched at the preview's MIDI note, but with timbral character that varies as you scrub the X/Y/Z sliders.
7. Drag Z slider through its full range — different pages should sound different
8. Click "Export wavetable bank" → pick a directory → verify 8 WAV files appear

If the audio sounds wrong (silent, clipped, NaN garbage, etc.) STOP and investigate. The most likely culprits in order:
- `SpectralModifier` math error (tilt or stretch producing NaN or infinite values)
- `CycleExtractor` normalization bug (peak=0 case not handled, or normalization scaling wrong)
- `FftResampler` scaling factor wrong (output too quiet or too loud)
- `SingleWavGenerator` page-normalization bug

- [ ] **Step 7: Run unit tests**

```bash
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 50 tests pass (53 from after Task 3 minus 3 deleted StubBankWriter tests = 50).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/single_wav_service.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/single_wav_service.cpp \
        cpp/src/app/services/stub_bank_writer.h \
        cpp/src/app/services/stub_bank_writer.cpp \
        cpp/tests/stub_bank_writer_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): wire SingleWavService to real DSP and delete StubBankWriter"
```

(`git add` of the deleted files records their removal — the `rm` from Step 2 already deleted them from the working tree.)

---

## Task 6: Push and verify CI

Push the branch and verify CI is green on all platforms. Per the lessons from Phase 3a, do NOT trust the background watch's exit code — verify explicitly with `gh run view` after the watch completes.

- [ ] **Step 1: Push the branch**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
```

- [ ] **Step 2: Get the latest run id and watch**

```bash
sleep 5
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId,status --jq '.[0]'
# Note the databaseId, then:
gh run watch <id> --exit-status
```

- [ ] **Step 3: VERIFY CI status explicitly**

The background watch exit code is unreliable. Always verify:

```bash
gh run view <id>
```

Expected: all four jobs ✓ — `clang-format check`, `Build - macos-latest`, `Build - ubuntu-latest`, `Build - windows-latest`. Investigate any failure before claiming success. The most likely platform-specific failure is MSVC complaining about something the macOS clang accepts (e.g. missing standard-library headers, narrowing conversions).

---

## Phase 3b done when:

1. ✅ `fim::dsp::FftResampler` correctly resamples between PFFFT-compatible lengths
2. ✅ `fim::dsp::SpectralModifier` ports Python's tilt + stretch + phase mods with seedable RNG
3. ✅ `fim::dsp::CycleExtractor` produces normalized oversampled cycles from STFT frames
4. ✅ `fim::dsp::SingleWavGenerator` runs the full pipeline end-to-end
5. ✅ `SingleWavService` calls the real generator instead of `StubBankWriter`
6. ✅ Phase 2 `StubBankWriter` is deleted
7. ✅ Manual verification: the app produces audibly resynthesis output, not placeholder sines, when dropping a real WAV
8. ✅ All 50 Catch2 tests pass locally
9. ✅ CI green on macOS, Ubuntu, Windows, and clang-format

After Phase 3b ships, the app does what the project name says: takes a `.wav` file in, produces a wavetable bank out. Phase 3c adds the Python oracle harness for parity testing; Phase 3d adds the additional Y/Z morph modes.
