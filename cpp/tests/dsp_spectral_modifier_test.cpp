#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "dsp/spectral_modifier.h"

using Catch::Matchers::WithinAbs;

namespace {

// Build a synthetic 2D magnitude/phase array shaped (num_bins x num_frames)
// with known content for tests. All bins start at magnitude 1.0 and phase 0.
struct SyntheticSpectrum {
    std::vector<std::vector<float>> magnitude;
    std::vector<std::vector<float>> phase;
};

SyntheticSpectrum MakeFlat(std::size_t num_bins, std::size_t num_frames, float mag_value = 1.0f) {
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
    // x=4 gives stretch_amount ~= 1.357. The stretch step still modifies
    // magnitudes (it never truly cancels out for any integer x) but at
    // x=4 the top bins stay populated, unlike x=0 which zeroes bins >=
    // num_bins/2. Combined with the y=7 tilt, this gives a clearly
    // brightening result across all bins.
    modifier.Apply(spectrum.magnitude, spectrum.phase, /*x=*/4, /*y=*/7, /*z=*/0);

    // With flat input, y=7 tilt alone multiplies by exp(5*k/num_bins)
    // giving bin 15/bin 0 ~= exp(75/16) ~= 108. The stretch step rescales
    // things but bin 15 still ends up much brighter than bin 0.
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
