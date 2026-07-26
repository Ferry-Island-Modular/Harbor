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

    std::vector<std::size_t> selected;
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

}  // namespace fim::dsp
