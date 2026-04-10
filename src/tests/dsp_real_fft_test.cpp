#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <vector>

#include "dsp/real_fft.h"

using Catch::Matchers::WithinAbs;

namespace {
constexpr std::size_t kTestN = 64;
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

TEST_CASE("RealFft forward transforms a DC signal to bin 0 only", "[dsp][real_fft]") {
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

TEST_CASE("RealFft forward transforms an impulse to a flat spectrum", "[dsp][real_fft]") {
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

TEST_CASE("RealFft forward concentrates a pure sine at its frequency bin", "[dsp][real_fft]") {
    fim::dsp::RealFft fft(kTestN);
    // Place a sine at exactly bin 4. Period = N/4 = 16 samples.
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
        if (k == kBin) {
            continue;
        }
        REQUIRE_THAT(std::abs(bins[k]), WithinAbs(0.0f, 1e-2));
    }
}

TEST_CASE("RealFft forward then inverse recovers the original (modulo 1/N)", "[dsp][real_fft]") {
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
