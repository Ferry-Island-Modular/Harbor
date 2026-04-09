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
    void Apply(std::vector<std::vector<float>>& magnitude, std::vector<std::vector<float>>& phase,
               int x, int y, int z);

private:
    std::mt19937 rng_;
};

}  // namespace fim::dsp
