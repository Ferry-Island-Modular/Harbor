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

// Y0: spectral tilt. Matches Phase 3b exactly.
void ApplyTilt(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();
    const float exponent_scale = ((y_norm * 2.0f) - 1.0f) * 5.0f;
    const float inv_num_bins = 1.0f / static_cast<float>(num_bins);
    for (std::size_t k = 0; k < num_bins; ++k) {
        const float tilt = std::exp(exponent_scale * static_cast<float>(k) * inv_num_bins);
        for (std::size_t f = 0; f < num_frames; ++f) {
            magnitude[k][f] *= tilt;
        }
    }
}

// Y1: formant scaling. Shifts the spectral envelope up or down in
// frequency while preserving its shape. y_norm=0.5 is neutral; y_norm<0.5
// shifts down, y_norm>0.5 shifts up. Implementation: resample the per-
// frame magnitude curve by linear interpolation at a stretched/compressed
// source index.
void ApplyFormant(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    // formant_shift in [0.5, 2.0]. y_norm=0 gives 0.5 (shift down one
    // octave), y_norm=1 gives 2.0 (shift up one octave).
    const float formant_shift = 0.5f + y_norm * 1.5f;
    const float inv_shift = 1.0f / formant_shift;

    std::vector<float> shifted(num_bins, 0.0f);
    for (std::size_t f = 0; f < num_frames; ++f) {
        std::fill(shifted.begin(), shifted.end(), 0.0f);
        for (std::size_t k = 0; k < num_bins; ++k) {
            const float src_idx = static_cast<float>(k) * inv_shift;
            if (src_idx >= 0.0f && src_idx < static_cast<float>(num_bins) - 1.0f) {
                const std::size_t idx_floor = static_cast<std::size_t>(std::floor(src_idx));
                const std::size_t idx_ceil = idx_floor + 1;
                const float fraction = src_idx - static_cast<float>(idx_floor);
                shifted[k] =
                    magnitude[idx_floor][f] * (1.0f - fraction) + magnitude[idx_ceil][f] * fraction;
            }
        }
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude[k][f] = shifted[k];
        }
    }
}

// Y2: harmonic stretch. Non-linear remapping of bin indices via a power
// curve. y_norm=0.5 gives stretch_power=1.25 (mild stretch); below 0.5
// compresses, above 0.5 stretches harder.
void ApplyHarmonicStretch(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty() || num_bins < 2) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    const float stretch_power = 0.5f + y_norm * 1.5f;
    const float inv_num_bins_m1 = 1.0f / static_cast<float>(num_bins - 1);

    std::vector<float> stretched(num_bins, 0.0f);
    for (std::size_t f = 0; f < num_frames; ++f) {
        std::fill(stretched.begin(), stretched.end(), 0.0f);
        for (std::size_t k = 0; k < num_bins; ++k) {
            // Remap k via remapped_norm = (k/num_bins)^stretch_power.
            const float normalized = static_cast<float>(k) * inv_num_bins_m1;
            const float remapped_norm = std::pow(normalized, stretch_power);
            const float src_idx = remapped_norm * static_cast<float>(num_bins - 1);
            if (src_idx < static_cast<float>(num_bins) - 1.0f) {
                const std::size_t idx_floor = static_cast<std::size_t>(std::floor(src_idx));
                const std::size_t idx_ceil = idx_floor + 1;
                const float fraction = src_idx - static_cast<float>(idx_floor);
                stretched[k] =
                    magnitude[idx_floor][f] * (1.0f - fraction) + magnitude[idx_ceil][f] * fraction;
            }
        }
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude[k][f] = stretched[k];
        }
    }
}

// Y3: smear. Softens spectral peaks by averaging each bin with its
// neighbors, weighted by y_norm. At y_norm=0 this is identity; at y_norm=1
// each bin is fully replaced by the average of its 5-bin neighborhood.
//
// Bespoke implementation — NOT Vital's running-average-with-(i+0.25)/i
// scaling. That version lives in SerumMorpher for Serum mode. Both are
// "smear" but tuned to their data shapes (single-wav's 2D STFT vs Serum's
// 1D single-frame rfft).
void ApplySmear(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins < 3 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    // Per frame: compute a 5-tap moving average of the magnitude column,
    // then lerp from the original into the smoothed version by y_norm.
    std::vector<float> smoothed(num_bins, 0.0f);
    for (std::size_t f = 0; f < num_frames; ++f) {
        // 5-tap moving average with edge clamping.
        for (std::size_t k = 0; k < num_bins; ++k) {
            float sum = 0.0f;
            int count = 0;
            for (int offset = -2; offset <= 2; ++offset) {
                const long idx = static_cast<long>(k) + offset;
                if (idx >= 0 && idx < static_cast<long>(num_bins)) {
                    sum += magnitude[static_cast<std::size_t>(idx)][f];
                    count += 1;
                }
            }
            smoothed[k] = sum / static_cast<float>(count);
        }
        // Lerp original → smoothed by y_norm.
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude[k][f] = magnitude[k][f] * (1.0f - y_norm) + smoothed[k] * y_norm;
        }
    }
}

// X-driven spectral envelope stretch. Runs AFTER the Y mode and reads the
// envelope from the Y-modified magnitude. Matches Phase 3b/Python.
void ApplyXStretch(std::vector<std::vector<float>>& magnitude, float x_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    const float stretch_amount = 0.5f + x_norm * 1.5f;
    const auto env = SpectralEnvelope(magnitude);

    std::vector<float> stretched_env(num_bins, 0.0f);
    for (std::size_t i = 0; i < num_bins; ++i) {
        const float src_idx = static_cast<float>(i) / stretch_amount;
        if (src_idx < static_cast<float>(num_bins) - 1.0f) {
            const std::size_t idx_floor = static_cast<std::size_t>(std::floor(src_idx));
            const std::size_t idx_ceil = idx_floor + 1;
            const float fraction = src_idx - static_cast<float>(idx_floor);
            stretched_env[i] = env[idx_floor] * (1.0f - fraction) + env[idx_ceil] * fraction;
        }
    }

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

// Z0: phase randomization. Matches Phase 3b exactly.
void ApplyPhaseRandom(std::vector<std::vector<float>>& phase, float z_norm, std::mt19937& rng) {
    const std::size_t num_bins = phase.size();
    if (num_bins == 0 || phase[0].empty()) {
        return;
    }
    const std::size_t num_frames = phase[0].size();

    std::uniform_real_distribution<float> uniform_full(0.0f, kTwoPi);
    std::uniform_real_distribution<float> uniform_jitter(-0.1f, 0.1f);

    for (std::size_t k = 0; k < num_bins; ++k) {
        for (std::size_t f = 0; f < num_frames; ++f) {
            const float random_phase = uniform_full(rng);
            phase[k][f] = (1.0f - z_norm) * phase[k][f] + z_norm * random_phase;
        }
    }

    const float coherence = z_norm * 0.5f;
    const float one_minus_coherence = 1.0f - coherence;
    for (std::size_t f = 0; f < num_frames; ++f) {
        float prev = phase[0][f];
        for (std::size_t k = 1; k < num_bins; ++k) {
            const float jitter = uniform_jitter(rng);
            const float smoothed = phase[k][f] * one_minus_coherence + (prev + jitter) * coherence;
            phase[k][f] = smoothed;
            prev = smoothed;
        }
    }
}

// Z1: phase dispersion. Frequency-dependent phase shift with a bell-shaped
// profile centered at 1/4 of the bin range. Simulates a dispersive medium.
// At z_norm=0 this is a no-op.
void ApplyPhaseDisperse(std::vector<std::vector<float>>& phase, float z_norm) {
    const std::size_t num_bins = phase.size();
    if (num_bins == 0 || phase[0].empty() || z_norm == 0.0f) {
        return;
    }
    const std::size_t num_frames = phase[0].size();

    constexpr float kCenterBinFraction = 0.25f;
    const float center_bin = static_cast<float>(num_bins) * kCenterBinFraction;
    const float strength = z_norm * kTwoPi;

    for (std::size_t k = 0; k < num_bins; ++k) {
        // Parabolic phase shift peaking at center_bin: the bell-shape
        // places the strongest phase rotation at low-mid frequencies,
        // gently tapering toward DC and Nyquist.
        const float normalized_offset =
            (static_cast<float>(k) - center_bin) / static_cast<float>(num_bins);
        const float delta_phase = strength * (1.0f - normalized_offset * normalized_offset);
        for (std::size_t f = 0; f < num_frames; ++f) {
            phase[k][f] += delta_phase;
        }
    }
}

}  // namespace

SpectralModifier::SpectralModifier() : rng_(std::random_device{}()) {}

SpectralModifier::SpectralModifier(std::uint32_t seed) : rng_(seed) {}

void SpectralModifier::Seed(std::uint32_t seed) {
    rng_.seed(seed);
}

void SpectralModifier::Apply(std::vector<std::vector<float>>& magnitude,
                             std::vector<std::vector<float>>& phase, int x, int y, int z,
                             YMode y_mode, ZMode z_mode) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }

    const float x_norm = static_cast<float>(x) / 7.0f;
    const float y_norm = static_cast<float>(y) / 7.0f;
    const float z_norm = static_cast<float>(z) / 7.0f;

    // ---- Y: morph mode dispatch (runs FIRST, matches Phase 3b/Python) ----
    switch (y_mode) {
        case YMode::kTilt:
            ApplyTilt(magnitude, y_norm);
            break;
        case YMode::kFormant:
            ApplyFormant(magnitude, y_norm);
            break;
        case YMode::kStretch:
            ApplyHarmonicStretch(magnitude, y_norm);
            break;
        case YMode::kSmear:
            ApplySmear(magnitude, y_norm);
            break;
    }

    // ---- X: spectral envelope stretch (runs AFTER Y, reads Y-modified envelope) ----
    ApplyXStretch(magnitude, x_norm);

    // ---- Z: phase morph mode dispatch ----
    switch (z_mode) {
        case ZMode::kRandom:
            if (z_norm > 0.0f) {
                ApplyPhaseRandom(phase, z_norm, rng_);
            }
            break;
        case ZMode::kDisperse:
            ApplyPhaseDisperse(phase, z_norm);
            break;
        case ZMode::kCrush:
            // Intentional no-op. SingleWavGenerator applies ZCrush to the
            // extracted cycle instead.
            break;
    }
}

}  // namespace fim::dsp
