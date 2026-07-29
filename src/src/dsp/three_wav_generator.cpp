#include "dsp/three_wav_generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

#include "dr_wav.h"
#include "dsp/post_effects.h"
#include "dsp/real_fft.h"
#include "dsp/resample.h"
#include "dsp/stft.h"
#include "dsp/wav_loader.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kFftSize = 2048;
constexpr std::size_t kHopSize = 1024;
constexpr std::size_t kNumBins = kFftSize / 2 + 1;  // 1025
constexpr std::size_t kCellsPerSide = 8;
constexpr std::size_t kCellsPerPage = kCellsPerSide * kCellsPerSide;  // 64
constexpr std::uint32_t kOutputSampleRate = 44100;

// Time-average the magnitude spectrum across all STFT frames. Each bin's
// output is the mean of that bin's magnitude across all time frames.
// Returns a vector of length num_bins.
std::vector<float> TimeAverageMagnitude(
    const std::vector<std::vector<std::complex<float>>>& stft_frames) {
    std::vector<float> avg(kNumBins, 0.0f);
    if (stft_frames.empty()) {
        return avg;
    }
    for (const auto& frame : stft_frames) {
        for (std::size_t k = 0; k < kNumBins && k < frame.size(); ++k) {
            avg[k] += std::abs(frame[k]);
        }
    }
    const float inv = 1.0f / static_cast<float>(stft_frames.size());
    for (float& v : avg) {
        v *= inv;
    }
    return avg;
}

std::vector<float> CoherentPhaseTarget(std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> phase_dist(0.0f, 2.0f * std::numbers::pi_v<float>);
    std::vector<float> phases(kNumBins, 0.0f);
    for (std::size_t k = 1; k < kNumBins - 1; ++k) {
        phases[k] = phase_dist(rng);
    }
    return phases;
}

std::vector<float> TextureIfft(std::vector<float> magnitude, const std::vector<float>& phases,
                               ThreeWavZMode mode, float amount, RealFft& fft) {
    // Ease into the texture, then make the upper half of Z substantially more
    // assertive. This preserves useful interpolation near the neutral page
    // without wasting the far end of the hardware control on subtle changes.
    const float strength = amount * amount * (3.0f - 2.0f * amount);
    std::vector<std::complex<float>> bins(kNumBins);
    bins[0] = std::complex<float>(magnitude[0], 0.0f);  // DC stays real
    for (std::size_t k = 1; k < kNumBins - 1; ++k) {
        if (mode == ThreeWavZMode::kOddEven) {
            // At the endpoint, strongly hollow out the even harmonics rather
            // than applying a symmetric tilt that per-cell normalization can
            // partly disguise.
            const float target_gain = (k % 2 == 0) ? 0.04f : 1.5f;
            magnitude[k] *= std::lerp(1.0f, target_gain, strength);
        } else if (mode == ThreeWavZMode::kHarmonicComb) {
            // Keep the fundamental and every fourth harmonic thereafter.
            // The rejected partials are not hard-zeroed, so neighboring Z
            // pages remain smooth and the input's spectral flavor survives.
            const bool retained = ((k - 1) % 4) == 0;
            const float target_gain = retained ? 1.75f : 0.025f;
            magnitude[k] *= std::lerp(1.0f, target_gain, strength);
        }
        float phase = phases[k];
        if (mode == ThreeWavZMode::kPhase) {
            const float normalized = static_cast<float>(k) / static_cast<float>(kNumBins - 1);
            phase += amount * 8.0f * std::numbers::pi_v<float> * normalized * normalized;
        }
        bins[k] = std::polar(magnitude[k], phase);
    }
    bins[kNumBins - 1] = std::complex<float>(magnitude[kNumBins - 1], 0.0f);  // Nyquist real

    std::vector<float> output(kFftSize);
    fft.Inverse(bins.data(), output.data());
    // PFFFT inverse is unnormalized — divide by N.
    const float inv_n = 1.0f / static_cast<float>(kFftSize);
    for (float& s : output) {
        s *= inv_n;
    }
    return output;
}

}  // namespace

ThreeWavWeights ThreeWavBarycentricWeights(float x_amount, float y_amount) {
    const float x = std::clamp(x_amount, 0.0f, 1.0f);
    // Retain one eighth of the A/B crossfade at maximum Y. A mathematically
    // pure C vertex would collapse the entire top grid row to eight identical
    // waves, wasting X resolution on the hardware.
    const float y = std::clamp(y_amount, 0.0f, 1.0f) * 0.875f;
    return {
        .a = (1.0f - y) * (1.0f - x),
        .b = (1.0f - y) * x,
        .c = y,
    };
}

ThreeWavGenerator::ThreeWavGenerator(std::size_t samples, std::size_t num_pages)
    : samples_(samples), num_pages_(num_pages) {
    if (samples != 2048 || num_pages != 8) {
        throw std::invalid_argument(
            "ThreeWavGenerator currently supports only 2048 samples and 8 pages");
    }
}

bool ThreeWavGenerator::Generate(const std::array<std::filesystem::path, 3>& input_paths,
                                 const std::filesystem::path& output_directory,
                                 const ProgressCallback& on_progress) const {
    return Generate(input_paths, output_directory, ThreeWavGenerateOptions{}, on_progress);
}

bool ThreeWavGenerator::Generate(const std::array<std::filesystem::path, 3>& input_paths,
                                 const std::filesystem::path& output_directory,
                                 const ThreeWavGenerateOptions& options,
                                 const ProgressCallback& on_progress) const {
    // Step 1: load and resample each of the 3 files to 44.1 kHz.
    // Normalizing at load means differing file levels don't cause one
    // file to dominate the blend.
    std::array<std::vector<float>, 3> resampled_audio;
    for (std::size_t i = 0; i < 3; ++i) {
        auto loaded = LoadWav(input_paths[i], /*normalize=*/true);
        if (!loaded.has_value() || loaded->samples.empty()) {
            return false;
        }
        if (loaded->sample_rate == kOutputSampleRate) {
            resampled_audio[i] = std::move(loaded->samples);
        } else {
            resampled_audio[i] =
                ResampleTo(loaded->samples, loaded->sample_rate, kOutputSampleRate);
            if (resampled_audio[i].empty()) {
                return false;
            }
        }
    }

    // Step 2: ensure the output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    // Step 3: STFT each file, then time-average the magnitude spectrum.
    // Each file collapses to one 1025-bin magnitude vector — "one color
    // per file", matching Python's cross-synthesis trick.
    Stft stft(kFftSize, kHopSize);
    std::array<std::vector<float>, 3> file_avg_magnitudes;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto frames = stft.Analyze(resampled_audio[i]);
        if (frames.empty()) {
            // File is shorter than one STFT window.
            return false;
        }
        file_avg_magnitudes[i] = TimeAverageMagnitude(frames);
    }

    // Step 4: prepare the iFFT instance (reused across all cells).
    RealFft ifft(kFftSize);
    const std::uint32_t phase_seed =
        options.random_seed.value_or(static_cast<std::uint32_t>(std::random_device{}()));
    const auto coherent_phases = CoherentPhaseTarget(phase_seed);

    // Step 5: X crossfades A->B, Y pulls toward C, and Z independently
    // applies the chosen texture. A single phase target is shared by the
    // entire cube so neighboring cells remain interpolation-compatible.
    for (std::size_t z = 0; z < num_pages_; ++z) {
        std::vector<float> page_samples;
        page_samples.reserve(samples_ * kCellsPerPage);

        const float z_amount = static_cast<float>(z) / 7.0f;

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                const auto weights = ThreeWavBarycentricWeights(static_cast<float>(x) / 7.0f,
                                                                static_cast<float>(y) / 7.0f);

                // Weighted sum of the 3 per-file magnitude spectra.
                std::vector<float> combined_mag(kNumBins, 0.0f);
                for (std::size_t k = 0; k < kNumBins; ++k) {
                    combined_mag[k] = weights.a * file_avg_magnitudes[0][k] +
                                      weights.b * file_avg_magnitudes[1][k] +
                                      weights.c * file_avg_magnitudes[2][k];
                }

                auto cell =
                    TextureIfft(combined_mag, coherent_phases, options.z_mode, z_amount, ifft);
                if (options.z_mode == ThreeWavZMode::kCrush) {
                    const auto crush = ZCrushAmount(static_cast<int>(z));
                    ZCrush(cell, crush.bit_depth, crush.sample_hold);
                }

                // Remove DC.
                float dc_sum = 0.0f;
                for (float s : cell) {
                    dc_sum += s;
                }
                const float dc = dc_sum / static_cast<float>(cell.size());
                for (float& s : cell) {
                    s -= dc;
                }

                // Normalize each cell to peak 1.0 so each cell is
                // individually well-scaled.
                float peak = 0.0f;
                for (float s : cell) {
                    peak = std::max(peak, std::abs(s));
                }
                if (peak > 0.0f) {
                    const float inv = 1.0f / peak;
                    for (float& s : cell) {
                        s *= inv;
                    }
                }

                page_samples.insert(page_samples.end(), cell.begin(), cell.end());
            }
        }

        // Globally normalize the page.
        float page_peak = 0.0f;
        for (float s : page_samples) {
            page_peak = std::max(page_peak, std::abs(s));
        }
        if (page_peak > 0.0f) {
            const float inv = 1.0f / page_peak;
            for (float& s : page_samples) {
                s *= inv;
            }
        }

        const auto path = output_directory / (std::to_string(z + 1) + ".wav");
        if (!WritePageToWav(path, page_samples)) {
            return false;
        }

        if (on_progress) {
            const int percent = static_cast<int>((z + 1) * 100 / num_pages_);
            on_progress(percent);
        }
    }

    return true;
}

bool ThreeWavGenerator::WritePageToWav(const std::filesystem::path& path,
                                       const std::vector<float>& page_samples) const {
    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = kOutputSampleRate;
    format.bitsPerSample = 16;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr)) {
        return false;
    }

    std::vector<std::int16_t> int_samples(page_samples.size());
    for (std::size_t i = 0; i < page_samples.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, page_samples[i]));
        int_samples[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }

    const drwav_uint64 frames_written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);
    return frames_written == int_samples.size();
}

}  // namespace fim::dsp
