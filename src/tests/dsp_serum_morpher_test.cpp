#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "dsp/serum_morpher.h"

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

std::vector<float> MakePhaseShiftedCosineFrame(std::size_t bin, float phase) {
    std::vector<float> frame(kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        frame[i] = std::cos(2.0f * kPi * static_cast<float>(bin) * i / kFftSize + phase);
    }
    return frame;
}

}  // namespace

TEST_CASE("SerumMorpher cache contains correct amplitudes for a cosine", "[dsp][serum_morpher]") {
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

TEST_CASE("SerumMorpher aligns arbitrary cycle starts before interpolation",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    std::vector<fim::dsp::SerumFftCache> sources;
    sources.push_back(morpher.ComputeCache(MakePhaseShiftedCosineFrame(7, 0.0f)));
    sources.push_back(morpher.ComputeCache(MakePhaseShiftedCosineFrame(7, 1.7f)));

    morpher.AlignSourcePhases(sources);

    const float phase_delta =
        std::remainder(sources[1].phases[7] - sources[0].phases[7], 2.0f * kPi);
    // Integer circular shifts cannot generally correct a phase offset to
    // better than half a sample at the tested harmonic.
    REQUIRE_THAT(phase_delta, WithinAbs(0.0f, kPi * 7.0f / kFftSize + 1e-4f));
}

TEST_CASE("SerumMorpher circularly interpolates phase instead of snapping",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    std::vector<fim::dsp::SerumFftCache> sources;
    sources.push_back(morpher.ComputeCache(MakePhaseShiftedCosineFrame(5, 0.0f)));
    sources.push_back(morpher.ComputeCache(MakePhaseShiftedCosineFrame(5, kPi / 2.0f)));

    const auto interpolated = morpher.InterpolateCaches(sources, 3);
    const float first = interpolated[0].phases[5];
    const float middle = interpolated[1].phases[5];
    const float last = interpolated[2].phases[5];
    REQUIRE(std::abs(std::remainder(middle - first, 2.0f * kPi)) > 0.2f);
    REQUIRE(std::abs(std::remainder(last - middle, 2.0f * kPi)) > 0.2f);
}

TEST_CASE("SerumMorpher formant scale at amount=0.5 keeps peak roughly in place",
          "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    const auto frame = MakeCosineFrame(16);
    const auto cache = morpher.ComputeCache(frame);
    const auto result = morpher.Apply(cache, fim::dsp::SerumMode::kFormant, /*amount=*/0.5f);

    REQUIRE(result.size() == kNumBins);
    // Peak should still be at bin 16 (amount=0.5 → scale=1.0 → identity).
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

TEST_CASE("SerumMorpher formant scale at amount=1 shifts peak up", "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    const auto frame = MakeCosineFrame(16);
    const auto cache = morpher.ComputeCache(frame);
    const auto result = morpher.Apply(cache, fim::dsp::SerumMode::kFormant, /*amount=*/1.0f);

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

    const auto result = morpher.Apply(cache, fim::dsp::SerumMode::kPhase, /*amount=*/1.0f);

    REQUIRE_THAT(std::abs(result[0]), WithinAbs(dc_before, 1e-4));
    REQUIRE_THAT(std::abs(result[kNumBins - 1]), WithinAbs(nyq_before, 1e-4));
}

TEST_CASE("SerumMorpher smear reduces the peak magnitude", "[dsp][serum_morpher]") {
    fim::dsp::SerumMorpher morpher;
    const auto frame = MakeCosineFrame(16);
    const auto cache = morpher.ComputeCache(frame);

    const auto result = morpher.Apply(cache, fim::dsp::SerumMode::kSmear, /*amount=*/1.0f);

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
