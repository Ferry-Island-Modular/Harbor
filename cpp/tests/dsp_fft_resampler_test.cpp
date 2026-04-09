#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "dsp/fft_resampler.h"

using Catch::Matchers::WithinAbs;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
}

TEST_CASE("FftResampler same-length is approximately identity", "[dsp][fft_resampler]") {
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

TEST_CASE("FftResampler 2x upsample preserves a sine's frequency", "[dsp][fft_resampler]") {
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

TEST_CASE("FftResampler 2x downsample preserves a sine at the new rate", "[dsp][fft_resampler]") {
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

TEST_CASE("FftResampler 4x upsample (the wavetable use case)", "[dsp][fft_resampler]") {
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
