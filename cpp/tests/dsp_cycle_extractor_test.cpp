#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "dsp/cycle_extractor.h"

using Catch::Matchers::WithinAbs;

namespace {

// Build a synthetic mag/phase array shaped (num_bins x num_frames) where
// only the specified bin has nonzero magnitude in the chosen frame, with
// phase 0. This produces a clean cosine when inverted.
struct OneBinSpectrum {
    std::vector<std::vector<float>> magnitude;
    std::vector<std::vector<float>> phase;
};

OneBinSpectrum MakeOneBin(std::size_t num_bins, std::size_t num_frames, std::size_t bin,
                          std::size_t frame, float mag) {
    OneBinSpectrum s;
    s.magnitude.assign(num_bins, std::vector<float>(num_frames, 0.0f));
    s.phase.assign(num_bins, std::vector<float>(num_frames, 0.0f));
    s.magnitude[bin][frame] = mag;
    return s;
}

}  // namespace

TEST_CASE("CycleExtractor produces output of the requested length", "[dsp][cycle_extractor]") {
    constexpr std::size_t kFft = 64;
    constexpr std::size_t kTarget = 256;
    fim::dsp::CycleExtractor extractor(kFft, kTarget);

    auto spectrum = MakeOneBin(kFft / 2 + 1, /*num_frames=*/2, /*bin=*/4,
                               /*frame=*/0, /*mag=*/1.0f);
    const auto cycle = extractor.Extract(spectrum.magnitude, spectrum.phase, 0);
    REQUIRE(cycle.size() == kTarget);
}

TEST_CASE("CycleExtractor normalizes the output to peak 1.0", "[dsp][cycle_extractor]") {
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

TEST_CASE("CycleExtractor on a zero spectrum returns zeros without NaN", "[dsp][cycle_extractor]") {
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

TEST_CASE("CycleExtractor returns silence for a near-zero spectrum (below threshold)",
          "[dsp][cycle_extractor]") {
    constexpr std::size_t kFft = 64;
    constexpr std::size_t kTarget = 256;
    fim::dsp::CycleExtractor extractor(kFft, kTarget);

    // Build a spectrum with a single bin at magnitude 1e-7 — well below
    // the 1e-4 threshold. The pre-Phase-3d behavior would normalize this
    // up to peak=1.0 and produce amplified noise; the new behavior
    // returns zeros.
    std::vector<std::vector<float>> magnitude(kFft / 2 + 1, std::vector<float>(1, 0.0f));
    std::vector<std::vector<float>> phase(kFft / 2 + 1, std::vector<float>(1, 0.0f));
    magnitude[4][0] = 1e-7f;

    const auto cycle = extractor.Extract(magnitude, phase, 0);
    REQUIRE(cycle.size() == kTarget);
    for (float s : cycle) {
        REQUIRE(std::abs(s) < 1e-6f);
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
