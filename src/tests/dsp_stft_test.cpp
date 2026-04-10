#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <vector>

#include "dsp/stft.h"

using Catch::Matchers::WithinAbs;

namespace {
constexpr std::size_t kFft = 64;
constexpr std::size_t kHop = 32;
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

TEST_CASE("Stft NumFrames returns 0 for audio shorter than fft_size", "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    REQUIRE(stft.NumFrames(0) == 0);
    REQUIRE(stft.NumFrames(kFft - 1) == 0);
}

TEST_CASE("Stft NumFrames returns 1 for exactly fft_size samples", "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    REQUIRE(stft.NumFrames(kFft) == 1);
}

TEST_CASE("Stft NumFrames matches floor((n - fft_size) / hop_size) + 1", "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    // n = fft_size + hop_size → 2 frames
    REQUIRE(stft.NumFrames(kFft + kHop) == 2);
    // n = fft_size + 2*hop_size → 3 frames
    REQUIRE(stft.NumFrames(kFft + 2 * kHop) == 3);
    // n = fft_size + hop_size + (hop_size - 1) → still 2 frames
    REQUIRE(stft.NumFrames(kFft + kHop + kHop - 1) == 2);
}

TEST_CASE("Stft Analyze on audio shorter than fft_size returns empty", "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    std::vector<float> audio(kFft - 1, 0.5f);
    const auto bins = stft.Analyze(audio);
    REQUIRE(bins.empty());
}

TEST_CASE("Stft Analyze on exactly fft_size samples produces one frame", "[dsp][stft]") {
    fim::dsp::Stft stft(kFft, kHop);
    std::vector<float> audio(kFft, 0.0f);
    // Pure sine at bin 4. Hann window will spread energy to neighbors but
    // bin 4 should still be the argmax.
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
