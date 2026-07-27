#include "dsp/source_frame_selector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace fim::dsp {

namespace {

constexpr std::size_t kMaximumWindowFrames = 128;
constexpr float kActiveEnergyRatio = 1.0e-4f;  // -40 dB relative power.

std::vector<std::size_t> UniformFrames(std::size_t num_frames, std::size_t output_count) {
    std::vector<std::size_t> selected;
    if (num_frames == 0 || output_count == 0) {
        return selected;
    }
    selected.reserve(output_count);
    if (output_count == 1) {
        selected.push_back((num_frames - 1) / 2);
        return selected;
    }
    for (std::size_t i = 0; i < output_count; ++i) {
        selected.push_back(
            static_cast<std::size_t>(static_cast<float>(i) / static_cast<float>(output_count - 1) *
                                     static_cast<float>(num_frames - 1)));
    }
    return selected;
}

std::vector<std::size_t> UniformActiveFrames(const std::vector<std::size_t>& active_frames,
                                             std::size_t output_count) {
    std::vector<std::size_t> selected;
    if (active_frames.empty() || output_count == 0) {
        return selected;
    }
    selected.reserve(output_count);
    if (output_count == 1) {
        selected.push_back(active_frames[active_frames.size() / 2]);
        return selected;
    }
    for (std::size_t i = 0; i < output_count; ++i) {
        const std::size_t active_index = i * (active_frames.size() - 1) / (output_count - 1);
        selected.push_back(active_frames[active_index]);
    }
    return selected;
}

float SpectralShapeDistance(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t num_bins = std::min(a.size(), b.size());
    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    for (std::size_t bin = 1; bin < num_bins; ++bin) {
        const double value_a = std::log1p(std::max(a[bin], 0.0f));
        const double value_b = std::log1p(std::max(b[bin], 0.0f));
        dot += value_a * value_b;
        norm_a += value_a * value_a;
        norm_b += value_b * value_b;
    }
    if (norm_a <= std::numeric_limits<double>::epsilon() ||
        norm_b <= std::numeric_limits<double>::epsilon()) {
        return 0.0f;
    }
    const double similarity = dot / std::sqrt(norm_a * norm_b);
    return static_cast<float>(std::clamp(1.0 - similarity, 0.0, 1.0));
}

std::vector<std::size_t> SpectrallySpacedFrames(const std::vector<std::vector<float>>& magnitude,
                                                const std::vector<std::size_t>& active_frames,
                                                std::size_t output_count) {
    if (active_frames.size() < 2 || output_count < 2) {
        return UniformActiveFrames(active_frames, output_count);
    }

    std::vector<double> spectral_steps(active_frames.size() - 1, 0.0);
    for (std::size_t i = 1; i < active_frames.size(); ++i) {
        spectral_steps[i - 1] =
            SpectralShapeDistance(magnitude[active_frames[i - 1]], magnitude[active_frames[i]]);
    }
    const double spectral_total =
        std::accumulate(spectral_steps.begin(), spectral_steps.end(), 0.0);
    if (spectral_total <= std::numeric_limits<double>::epsilon()) {
        return UniformActiveFrames(active_frames, output_count);
    }

    // A pure spectral path over-concentrates choices around abrupt changes.
    // Blend in elapsed-frame progress so X remains exploratory but playable:
    // 65% accumulated spectral change, 35% chronological progression.
    constexpr double kSpectralWeight = 0.65;
    constexpr double kTemporalWeight = 1.0 - kSpectralWeight;
    const double mean_spectral_step = spectral_total / spectral_steps.size();
    std::vector<double> cumulative_distance(active_frames.size(), 0.0);
    for (std::size_t i = 1; i < active_frames.size(); ++i) {
        const double blended_step =
            kSpectralWeight * spectral_steps[i - 1] + kTemporalWeight * mean_spectral_step;
        cumulative_distance[i] = cumulative_distance[i - 1] + blended_step;
    }
    const double total_distance = cumulative_distance.back();

    std::vector<std::size_t> selected;
    selected.reserve(output_count);
    std::size_t previous_index = 0;
    for (std::size_t i = 0; i < output_count; ++i) {
        const double target =
            total_distance * static_cast<double>(i) / static_cast<double>(output_count - 1);
        auto upper =
            std::lower_bound(cumulative_distance.begin(), cumulative_distance.end(), target);
        std::size_t active_index =
            static_cast<std::size_t>(std::distance(cumulative_distance.begin(), upper));
        if (active_index > 0 && active_index < cumulative_distance.size()) {
            const double below = target - cumulative_distance[active_index - 1];
            const double above = cumulative_distance[active_index] - target;
            if (below <= above) {
                --active_index;
            }
        }

        // Keep choices unique when enough active frames exist. Clamping both
        // ends prevents one large transition consuming every quantile.
        if (active_frames.size() >= output_count) {
            const std::size_t minimum_index = i == 0 ? 0 : previous_index + 1;
            const std::size_t maximum_index = active_frames.size() - (output_count - i);
            active_index = std::clamp(active_index, minimum_index, maximum_index);
        }
        selected.push_back(active_frames[active_index]);
        previous_index = active_index;
    }
    return selected;
}

}  // namespace

std::vector<std::size_t> SelectSourceFrames(const std::vector<std::vector<float>>& magnitude,
                                            FrameSelectionMode mode, std::size_t output_count) {
    const std::size_t num_frames = magnitude.size();
    if (mode == FrameSelectionMode::kUniform || num_frames == 0 || output_count == 0) {
        return UniformFrames(num_frames, output_count);
    }

    std::vector<float> energy(num_frames, 0.0f);
    std::vector<float> flux(num_frames, 0.0f);
    std::vector<float> entropy(num_frames, 0.0f);
    float peak_energy = 0.0f;
    float peak_flux = 0.0f;

    for (std::size_t frame = 0; frame < num_frames; ++frame) {
        const auto& bins = magnitude[frame];
        float magnitude_sum = 0.0f;
        for (std::size_t bin = 1; bin < bins.size(); ++bin) {
            const float value = std::max(bins[bin], 0.0f);
            energy[frame] += value * value;
            magnitude_sum += value;
            if (frame > 0 && bin < magnitude[frame - 1].size()) {
                const float delta = value - std::max(magnitude[frame - 1][bin], 0.0f);
                if (delta > 0.0f) {
                    flux[frame] += delta * delta;
                }
            }
        }

        if (magnitude_sum > 0.0f && bins.size() > 2) {
            float accumulated_entropy = 0.0f;
            for (std::size_t bin = 1; bin < bins.size(); ++bin) {
                const float probability = std::max(bins[bin], 0.0f) / magnitude_sum;
                if (probability > 0.0f) {
                    accumulated_entropy -= probability * std::log(probability);
                }
            }
            entropy[frame] = accumulated_entropy / std::log(static_cast<float>(bins.size() - 1));
        }
        peak_energy = std::max(peak_energy, energy[frame]);
        peak_flux = std::max(peak_flux, flux[frame]);
    }

    if (peak_energy <= std::numeric_limits<float>::epsilon()) {
        return UniformFrames(num_frames, output_count);
    }

    std::vector<float> salience(num_frames, 0.0f);
    for (std::size_t frame = 0; frame < num_frames; ++frame) {
        const float energy_ratio = energy[frame] / peak_energy;
        if (energy_ratio < kActiveEnergyRatio) {
            continue;
        }
        const float energy_score = std::clamp((std::log10(energy_ratio) + 4.0f) / 4.0f, 0.0f, 1.0f);
        const float flux_score = peak_flux > 0.0f ? std::sqrt(flux[frame] / peak_flux) : 0.0f;
        salience[frame] = 0.50f * energy_score + 0.35f * flux_score +
                          0.15f * std::clamp(entropy[frame], 0.0f, 1.0f);
    }

    const std::size_t window_size = std::min(num_frames, kMaximumWindowFrames);
    float window_sum = std::accumulate(salience.begin(), salience.begin() + window_size, 0.0f);
    float best_sum = window_sum;
    std::size_t best_start = 0;
    for (std::size_t start = 1; start + window_size <= num_frames; ++start) {
        window_sum += salience[start + window_size - 1] - salience[start - 1];
        if (window_sum > best_sum) {
            best_sum = window_sum;
            best_start = start;
        }
    }

    std::vector<std::size_t> active_frames;
    active_frames.reserve(window_size);
    for (std::size_t frame = best_start; frame < best_start + window_size; ++frame) {
        if (energy[frame] >= peak_energy * kActiveEnergyRatio) {
            active_frames.push_back(frame);
        }
    }
    if (active_frames.empty()) {
        return UniformFrames(num_frames, output_count);
    }

    return SpectrallySpacedFrames(magnitude, active_frames, output_count);
}

}  // namespace fim::dsp
