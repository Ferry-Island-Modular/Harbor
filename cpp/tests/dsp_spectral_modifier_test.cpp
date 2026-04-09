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

TEST_CASE("SpectralModifier formant mode moves the peak bin", "[dsp][spectral_modifier]") {
    // Build a spectrum with a clear peak at bin 5.
    const std::size_t num_bins = 16;
    const std::size_t num_frames = 2;
    std::vector<std::vector<float>> magnitude(num_bins, std::vector<float>(num_frames, 0.1f));
    std::vector<std::vector<float>> phase(num_bins, std::vector<float>(num_frames, 0.0f));
    magnitude[5][0] = magnitude[5][1] = 1.0f;

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/7, /*z=*/0, fim::dsp::YMode::kFormant,
                   fim::dsp::ZMode::kRandom);

    // The peak should have moved away from its original bin (5).
    float peak_mag = 0.0f;
    std::size_t peak_bin = 0;
    for (std::size_t k = 0; k < num_bins; ++k) {
        if (magnitude[k][0] > peak_mag) {
            peak_mag = magnitude[k][0];
            peak_bin = k;
        }
    }
    REQUIRE(peak_bin != 5);
}

TEST_CASE("SpectralModifier harmonic stretch stays finite and non-silent",
          "[dsp][spectral_modifier]") {
    auto magnitude = std::vector<std::vector<float>>(16, std::vector<float>(2, 1.0f));
    auto phase = std::vector<std::vector<float>>(16, std::vector<float>(2, 0.0f));
    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/4, /*z=*/0, fim::dsp::YMode::kStretch,
                   fim::dsp::ZMode::kRandom);

    for (const auto& row : magnitude) {
        for (float m : row) {
            REQUIRE(std::isfinite(m));
        }
    }
    bool has_content = false;
    for (const auto& row : magnitude) {
        for (float m : row) {
            if (m > 0.01f) {
                has_content = true;
                break;
            }
        }
    }
    REQUIRE(has_content);
}

TEST_CASE("SpectralModifier phase disperse changes phase but leaves magnitude alone",
          "[dsp][spectral_modifier]") {
    auto magnitude = std::vector<std::vector<float>>(16, std::vector<float>(2, 1.0f));
    auto phase = std::vector<std::vector<float>>(16, std::vector<float>(2, 0.0f));

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/3, /*z=*/5, fim::dsp::YMode::kTilt,
                   fim::dsp::ZMode::kDisperse);

    bool any_phase_changed = false;
    for (std::size_t k = 0; k < 16; ++k) {
        for (std::size_t f = 0; f < 2; ++f) {
            if (std::abs(phase[k][f]) > 1e-4f) {
                any_phase_changed = true;
                break;
            }
        }
    }
    REQUIRE(any_phase_changed);
}

TEST_CASE("SpectralModifier smear mode reduces adjacent-bin magnitude variance",
          "[dsp][spectral_modifier]") {
    // Build a spiky spectrum with every other bin at mag=1 and the rest at 0.
    const std::size_t num_bins = 16;
    const std::size_t num_frames = 2;
    std::vector<std::vector<float>> magnitude(num_bins, std::vector<float>(num_frames, 0.0f));
    std::vector<std::vector<float>> phase(num_bins, std::vector<float>(num_frames, 0.0f));
    for (std::size_t k = 0; k < num_bins; ++k) {
        if (k % 2 == 0) {
            magnitude[k][0] = magnitude[k][1] = 1.0f;
        }
    }

    // Compute variance before.
    auto variance = [](const std::vector<std::vector<float>>& mag) {
        float sum = 0.0f;
        float count = 0.0f;
        for (const auto& row : mag) {
            for (float v : row) {
                sum += v;
                count += 1.0f;
            }
        }
        const float mean = sum / count;
        float var = 0.0f;
        for (const auto& row : mag) {
            for (float v : row) {
                var += (v - mean) * (v - mean);
            }
        }
        return var / count;
    };
    const float variance_before = variance(magnitude);

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/7, /*z=*/0, fim::dsp::YMode::kSmear,
                   fim::dsp::ZMode::kRandom);

    const float variance_after = variance(magnitude);
    // Smear should significantly reduce the bin-to-bin variance.
    REQUIRE(variance_after < variance_before * 0.5f);
}

TEST_CASE("SpectralModifier ZMode::kCrush is a no-op for phase", "[dsp][spectral_modifier]") {
    auto magnitude = std::vector<std::vector<float>>(8, std::vector<float>(2, 1.0f));
    auto phase = std::vector<std::vector<float>>(8, std::vector<float>(2, 0.5f));
    const auto phase_before = phase;

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/3, /*z=*/7, fim::dsp::YMode::kTilt,
                   fim::dsp::ZMode::kCrush);

    // Phase must be bit-identical — kCrush should not touch it.
    for (std::size_t k = 0; k < 8; ++k) {
        for (std::size_t f = 0; f < 2; ++f) {
            REQUIRE(phase[k][f] == phase_before[k][f]);
        }
    }
}
