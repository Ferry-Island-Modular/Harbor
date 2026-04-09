#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace fim::dsp {

// Y-axis morph modes. The original Phase 3b behavior is kTilt.
//
// - kTilt:    exponential brightness shift (darkens or brightens uniformly)
// - kFormant: shifts the spectral envelope up or down in frequency while
//             preserving its shape — sounds like a formant shift on vowels
// - kStretch: non-linear log-frequency remapping that stretches or
//             compresses the harmonic spacing — inharmonic/bell-like character
enum class YMode {
    kTilt,
    kFormant,
    kStretch,
};

// Z-axis morph modes. The original Phase 3b behavior is kRandom.
//
// - kRandom:   blend the original phase with uniform random phase + bin-wise
//              smoothing (noisy/diffuse character)
// - kDisperse: frequency-dependent phase shift centered at a chosen bin —
//              comb-filter-like character
// - kCrush:    HANDLED OUTSIDE THIS CLASS. ZCrush is a time-domain post-effect
//              applied in SingleWavGenerator after cycle extraction. When
//              Apply() receives kCrush it leaves the phase array untouched
//              (the Y mode still runs normally).
enum class ZMode {
    kRandom,
    kDisperse,
    kCrush,
};

// Applies the chosen Y and Z morph modes to a 2D magnitude/phase array in
// place. Y runs first (matching Phase 3b and Python), then the X-driven
// spectral envelope stretch operates on the Y-modified envelope, then the
// Z mode applies phase-domain transformations.
//
// Parameter semantics:
// - x in [0, 7]: spectral envelope stretch (0.5x..2.0x). Always applies.
// - y in [0, 7]: strength of the Y morph mode. y=3 or 4 is roughly neutral.
// - z in [0, 7]: strength of the Z morph mode. z=0 leaves phase untouched.
// - y_mode: which Y transformation to apply.
// - z_mode: which Z transformation to apply. kCrush is a no-op here;
//           handle it in the caller as a post-effect.
//
// The 2D arrays are indexed as magnitude[bin][frame] (Python numpy
// convention from scipy.signal.stft).
class SpectralModifier {
public:
    // Default-constructed: seeds the RNG from std::random_device.
    SpectralModifier();

    // Seeded: produces reproducible phase output. Used by tests and by the
    // Phase 3c oracle harness.
    explicit SpectralModifier(std::uint32_t seed);

    // Reseed the internal RNG. Useful for resetting between generate runs.
    void Seed(std::uint32_t seed);

    // Default arguments preserve Phase 3b behavior (tilt + phase randomize).
    void Apply(std::vector<std::vector<float>>& magnitude, std::vector<std::vector<float>>& phase,
               int x, int y, int z, YMode y_mode = YMode::kTilt, ZMode z_mode = ZMode::kRandom);

private:
    std::mt19937 rng_;
};

}  // namespace fim::dsp
