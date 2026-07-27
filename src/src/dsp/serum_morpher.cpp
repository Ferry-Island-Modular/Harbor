#include "dsp/serum_morpher.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace fim::dsp {

namespace {

constexpr std::size_t kFftSize = 2048;
constexpr std::size_t kNumBins = kFftSize / 2 + 1;  // 1025

// Constants from Python's SerumWavetableConverter (which mirror Vital's).
constexpr float kPhaseDisperseCenter = 24.0f;
constexpr float kPhaseDisperseScale = 0.05f;
constexpr float kMaxHarmonicStretch = 12.0f;
constexpr int kFrequencyBins = 10;

}  // namespace

SerumMorpher::SerumMorpher() : fft_(kFftSize) {}

SerumMorpher::~SerumMorpher() = default;

SerumFftCache SerumMorpher::ComputeCache(const std::vector<float>& frame) {
    assert(frame.size() == kFftSize);

    std::vector<std::complex<float>> bins(kNumBins);
    fft_.Forward(frame.data(), bins.data());

    SerumFftCache cache;
    cache.amplitudes.resize(kNumBins);
    cache.phases.resize(kNumBins);
    cache.normalized_real.resize(kNumBins);
    cache.normalized_imag.resize(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        cache.amplitudes[k] = std::abs(bins[k]);
        cache.phases[k] = std::arg(bins[k]);
        cache.normalized_real[k] = std::cos(cache.phases[k]);
        cache.normalized_imag[k] = std::sin(cache.phases[k]);
    }
    return cache;
}

void SerumMorpher::AlignSourcePhases(std::vector<SerumFftCache>& sources) const {
    if (sources.size() < 2) {
        return;
    }
    constexpr std::size_t kAlignmentBins = 32;
    constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

    for (std::size_t frame = 1; frame < sources.size(); ++frame) {
        const auto& previous = sources[frame - 1];
        auto& current = sources[frame];

        std::vector<std::pair<float, std::size_t>> weighted_bins;
        weighted_bins.reserve(kNumBins - 2);
        for (std::size_t k = 1; k < kNumBins - 1; ++k) {
            weighted_bins.emplace_back(previous.amplitudes[k] * current.amplitudes[k], k);
        }
        const std::size_t keep = std::min(kAlignmentBins, weighted_bins.size());
        std::partial_sort(weighted_bins.begin(), weighted_bins.begin() + keep, weighted_bins.end(),
                          [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

        float best_score = -std::numeric_limits<float>::infinity();
        std::size_t best_shift = 0;
        for (std::size_t shift = 0; shift < kFftSize; ++shift) {
            float score = 0.0f;
            for (std::size_t candidate = 0; candidate < keep; ++candidate) {
                const auto [weight, k] = weighted_bins[candidate];
                const float rotation =
                    kTwoPi * static_cast<float>(k * shift) / static_cast<float>(kFftSize);
                score += weight * std::cos(current.phases[k] - rotation - previous.phases[k]);
            }
            if (score > best_score) {
                best_score = score;
                best_shift = shift;
            }
        }

        for (std::size_t k = 1; k < kNumBins; ++k) {
            const float rotation =
                kTwoPi * static_cast<float>(k * best_shift) / static_cast<float>(kFftSize);
            current.phases[k] = std::remainder(current.phases[k] - rotation, kTwoPi);
            current.normalized_real[k] = std::cos(current.phases[k]);
            current.normalized_imag[k] = std::sin(current.phases[k]);
        }
    }
}

std::vector<SerumFftCache> SerumMorpher::InterpolateCaches(
    const std::vector<SerumFftCache>& sources, std::size_t output_count) {
    std::vector<SerumFftCache> result(output_count);
    if (sources.empty() || output_count == 0) {
        return result;
    }
    if (sources.size() == 1) {
        for (std::size_t i = 0; i < output_count; ++i) {
            result[i] = sources[0];
        }
        return result;
    }

    const float source_last = static_cast<float>(sources.size() - 1);
    const float output_last = static_cast<float>(output_count - 1);

    for (std::size_t out_idx = 0; out_idx < output_count; ++out_idx) {
        // Fractional position in the source range.
        const float src_pos =
            (output_count == 1) ? 0.0f : (static_cast<float>(out_idx) / output_last) * source_last;
        const std::size_t src_floor = static_cast<std::size_t>(std::floor(src_pos));
        const std::size_t src_ceil = std::min(src_floor + 1, sources.size() - 1);
        const float t = src_pos - static_cast<float>(src_floor);

        result[out_idx].amplitudes.resize(kNumBins);
        result[out_idx].phases.resize(kNumBins);
        result[out_idx].normalized_real.resize(kNumBins);
        result[out_idx].normalized_imag.resize(kNumBins);

        for (std::size_t k = 0; k < kNumBins; ++k) {
            // Magnitudes: linear interpolation between the two nearest
            // source caches.
            result[out_idx].amplitudes[k] =
                sources[src_floor].amplitudes[k] * (1.0f - t) + sources[src_ceil].amplitudes[k] * t;
            const float real = sources[src_floor].normalized_real[k] * (1.0f - t) +
                               sources[src_ceil].normalized_real[k] * t;
            const float imag = sources[src_floor].normalized_imag[k] * (1.0f - t) +
                               sources[src_ceil].normalized_imag[k] * t;
            const float norm = std::hypot(real, imag);
            if (norm > 1e-6f) {
                result[out_idx].normalized_real[k] = real / norm;
                result[out_idx].normalized_imag[k] = imag / norm;
            } else {
                // Exactly opposite phasors have no unique midpoint. Retain
                // the lower frame's phase rather than creating NaNs.
                result[out_idx].normalized_real[k] = sources[src_floor].normalized_real[k];
                result[out_idx].normalized_imag[k] = sources[src_floor].normalized_imag[k];
            }
            result[out_idx].phases[k] =
                std::atan2(result[out_idx].normalized_imag[k], result[out_idx].normalized_real[k]);
        }
    }

    return result;
}

namespace {

// FORMANT_SCALE: shift harmonics up or down by a scale factor. Scale in
// [0.25, 4.0] computed from amount in [0, 1]: scale = 2^(4*amount - 2),
// so amount=0 gives 0.25 (shift down 2 octaves), amount=0.5 gives 1.0
// (identity), amount=1 gives 4.0 (shift up 2 octaves).
//
// Bidirectional extension of Python's monotonic-up implementation.
std::vector<std::complex<float>> ApplyFormantScale(const SerumFftCache& cache, float amount) {
    const float scale = std::pow(2.0f, 4.0f * amount - 2.0f);
    const float safe_scale = std::max(scale, 0.001f);

    std::vector<float> new_real(kNumBins, 0.0f);
    std::vector<float> new_imag(kNumBins, 0.0f);

    // DC unchanged.
    new_real[0] = cache.amplitudes[0] * cache.normalized_real[0];
    new_imag[0] = cache.amplitudes[0] * cache.normalized_imag[0];

    const std::size_t max_harmonics = std::min(
        kNumBins, static_cast<std::size_t>(static_cast<float>(kNumBins - 1) / safe_scale + 1.0f));

    for (std::size_t i = 1; i < max_harmonics; ++i) {
        const float shifted_index =
            std::max(1.0f, (static_cast<float>(i) - 1.0f) * safe_scale + 1.0f);
        const std::size_t dest_index = static_cast<std::size_t>(shifted_index);
        if (dest_index >= kNumBins - 1) {
            break;
        }

        const float t = shifted_index - static_cast<float>(dest_index);
        const float amplitude = cache.amplitudes[i];
        const float r = cache.normalized_real[i];
        const float m = cache.normalized_imag[i];

        const float a1 = (1.0f - t) * amplitude;
        const float a2 = t * amplitude;

        new_real[dest_index] += a1 * r;
        new_imag[dest_index] += a1 * m;
        new_real[dest_index + 1] += a2 * r;
        new_imag[dest_index + 1] += a2 * m;
    }

    std::vector<std::complex<float>> result(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        result[k] = std::complex<float>(new_real[k], new_imag[k]);
    }
    return result;
}

// PHASE_DISPERSE: frequency-dependent phase rotation with parabolic
// profile centered at harmonic 24. Matches Python's formula except that
// we explicitly skip DC (bin 0) and Nyquist (bin N/2) since those must
// stay real in a real-signal rfft.
std::vector<std::complex<float>> ApplyPhaseDisperse(const SerumFftCache& cache, float amount) {
    const float center = kPhaseDisperseCenter;
    const float offset = -((center - 1.0f) * (center - 1.0f)) * amount;

    std::vector<std::complex<float>> result(kNumBins);

    // DC and Nyquist stay real, unchanged.
    result[0] = std::complex<float>(cache.amplitudes[0] * cache.normalized_real[0], 0.0f);
    result[kNumBins - 1] = std::complex<float>(
        cache.amplitudes[kNumBins - 1] * cache.normalized_real[kNumBins - 1], 0.0f);

    for (std::size_t i = 1; i < kNumBins - 1; ++i) {
        const float fi = static_cast<float>(i);
        const float delta_phase = (fi - center) * (fi - center) * amount + offset;
        const float phase_shift = delta_phase * kPhaseDisperseScale;

        const float cos_p = std::cos(phase_shift);
        const float sin_p = std::sin(phase_shift);

        const float orig_r = cache.normalized_real[i];
        const float orig_m = cache.normalized_imag[i];

        // Rotate the phasor.
        const float new_r = orig_r * cos_p - orig_m * sin_p;
        const float new_m = orig_r * sin_p + orig_m * cos_p;

        const float amplitude = cache.amplitudes[i];
        result[i] = std::complex<float>(amplitude * new_r, amplitude * new_m);
    }

    return result;
}

// SMEAR: running-average amplitude smoothing with Vital's peculiar
// (i+0.25)/i scaling. Phases unchanged. This is the Vital version;
// single-wav has its own bespoke kernel-widening smoother in
// SpectralModifier.
std::vector<std::complex<float>> ApplySmear(const SerumFftCache& cache, float amount) {
    std::vector<std::complex<float>> result(kNumBins);

    // First harmonic: amplitude scaled by (1 - amount).
    float running_amplitude = cache.amplitudes[0] * (1.0f - amount);
    result[0] = std::complex<float>(running_amplitude * cache.normalized_real[0],
                                    running_amplitude * cache.normalized_imag[0]);

    for (std::size_t i = 1; i < kNumBins; ++i) {
        const float original_amplitude = cache.amplitudes[i];
        running_amplitude = (1.0f - amount) * original_amplitude + amount * running_amplitude;

        result[i] = std::complex<float>(running_amplitude * cache.normalized_real[i],
                                        running_amplitude * cache.normalized_imag[i]);

        // Vital's per-step scaling. Preserved as-is despite being
        // mathematically mysterious — changing it produces a different
        // sound.
        running_amplitude *= (static_cast<float>(i) + 0.25f) / static_cast<float>(i);
    }

    return result;
}

// HARMONIC_STRETCH: octave-based nonlinear stretch. Matches Python's
// formula with MAX_HARMONIC_STRETCH=12 and FREQUENCY_BINS=10.
std::vector<std::complex<float>> ApplyHarmonicStretch(const SerumFftCache& cache, float amount) {
    const float mult = 1.0f + amount * (kMaxHarmonicStretch - 1.0f);

    std::vector<float> new_real(kNumBins, 0.0f);
    std::vector<float> new_imag(kNumBins, 0.0f);

    // DC unchanged.
    new_real[0] = cache.amplitudes[0] * cache.normalized_real[0];
    new_imag[0] = cache.amplitudes[0] * cache.normalized_imag[0];

    for (std::size_t i = 1; i < kNumBins; ++i) {
        const float octave = std::log2(static_cast<float>(i));
        const float power = octave / static_cast<float>(kFrequencyBins - 1);
        const float shift = std::pow(mult, power);
        const float shifted_index = std::max(1.0f, shift * (static_cast<float>(i) - 1.0f) + 1.0f);

        const std::size_t dest_index = static_cast<std::size_t>(shifted_index);
        if (dest_index >= kNumBins - 1) {
            continue;
        }

        const float t = shifted_index - static_cast<float>(dest_index);
        const float amplitude = cache.amplitudes[i];
        const float r = cache.normalized_real[i];
        const float m = cache.normalized_imag[i];

        const float a1 = (1.0f - t) * amplitude;
        const float a2 = t * amplitude;

        new_real[dest_index] += a1 * r;
        new_imag[dest_index] += a1 * m;
        new_real[dest_index + 1] += a2 * r;
        new_imag[dest_index + 1] += a2 * m;
    }

    std::vector<std::complex<float>> result(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        result[k] = std::complex<float>(new_real[k], new_imag[k]);
    }
    return result;
}

std::vector<std::complex<float>> ApplyOddEven(const SerumFftCache& cache, float amount) {
    std::vector<std::complex<float>> result(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        float amplitude = cache.amplitudes[k];
        if (k > 0 && k < kNumBins - 1) {
            const float polarity = (k % 2 == 0) ? -1.0f : 1.0f;
            amplitude *= std::exp(polarity * amount * 1.25f);
        }
        result[k] = std::complex<float>(amplitude * cache.normalized_real[k],
                                        amplitude * cache.normalized_imag[k]);
    }
    return result;
}

std::vector<std::complex<float>> IdentitySpectrum(const SerumFftCache& cache) {
    std::vector<std::complex<float>> result(kNumBins);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        result[k] = std::complex<float>(cache.amplitudes[k] * cache.normalized_real[k],
                                        cache.amplitudes[k] * cache.normalized_imag[k]);
    }
    return result;
}

}  // namespace

std::vector<std::complex<float>> SerumMorpher::Apply(const SerumFftCache& cache, SerumMode mode,
                                                     float amount) {
    switch (mode) {
        case SerumMode::kFormant:
            return ApplyFormantScale(cache, amount);
        case SerumMode::kPhase:
            return ApplyPhaseDisperse(cache, amount);
        case SerumMode::kSmear:
            return ApplySmear(cache, amount);
        case SerumMode::kStretch:
            return ApplyHarmonicStretch(cache, amount);
        case SerumMode::kOddEven:
            return ApplyOddEven(cache, amount);
        case SerumMode::kCrush:
            return IdentitySpectrum(cache);
    }
    // Unreachable but required by some compilers to avoid a warning.
    return std::vector<std::complex<float>>(kNumBins, std::complex<float>(0.0f, 0.0f));
}

std::vector<float> SerumMorpher::InverseFft(const std::vector<std::complex<float>>& bins) {
    assert(bins.size() == kNumBins);
    std::vector<float> output(kFftSize);
    fft_.Inverse(bins.data(), output.data());
    // PFFFT inverse is unnormalized — divide by N to get the mathematical
    // inverse.
    const float inv_n = 1.0f / static_cast<float>(kFftSize);
    for (float& s : output) {
        s *= inv_n;
    }
    return output;
}

}  // namespace fim::dsp
