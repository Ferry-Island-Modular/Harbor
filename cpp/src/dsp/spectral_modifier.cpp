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
std::vector<float> SpectralEnvelope(const std::vector<std::vector<float>>& magnitude) {
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

void SpectralModifier::Seed(std::uint32_t seed) {
    rng_.seed(seed);
}

void SpectralModifier::Apply(std::vector<std::vector<float>>& magnitude,
                             std::vector<std::vector<float>>& phase, int x, int y, int z) {
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
            // else: stretched_env[i] stays 0, matching Python's init.
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

        // First pass: blend with random phase. Iterate bins outermost to
        // match Python's numpy row-major random fill and keep the seed→
        // output mapping deterministic per (bin, frame) order.
        for (std::size_t k = 0; k < num_bins; ++k) {
            for (std::size_t f = 0; f < num_frames; ++f) {
                const float random_phase = uniform_full(rng_);
                phase[k][f] = (1.0f - z_norm) * phase[k][f] + z_norm * random_phase;
            }
        }

        // Second pass: bin-wise smoothing accumulating from bin 0 upward,
        // independently per frame.
        const float coherence = z_norm * 0.5f;
        const float one_minus_coherence = 1.0f - coherence;
        for (std::size_t f = 0; f < num_frames; ++f) {
            float prev = phase[0][f];
            for (std::size_t k = 1; k < num_bins; ++k) {
                const float jitter = uniform_jitter(rng_);
                const float smoothed =
                    phase[k][f] * one_minus_coherence + (prev + jitter) * coherence;
                phase[k][f] = smoothed;
                prev = smoothed;
            }
        }
    }
}

}  // namespace fim::dsp
