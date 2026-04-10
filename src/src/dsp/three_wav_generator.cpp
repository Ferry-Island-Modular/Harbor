#include "dsp/three_wav_generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

#include "dr_wav.h"
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
constexpr float kWeightEpsilon = 0.1f;

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

// Random-phase inverse FFT of a magnitude spectrum. Each non-DC, non-
// Nyquist bin gets a uniform random phase in [0, 2*pi); DC and Nyquist
// stay real (their imaginary parts must be zero for the inverse to
// produce a real signal). Seeded deterministically per cell so output
// is reproducible across runs. This diffuses the energy across the
// cycle (instead of clumping at t=0 like zero-phase) and produces a
// much less buzzy, more organic sound.
std::vector<float> RandomPhaseIfft(const std::vector<float>& magnitude, RealFft& fft,
                                   std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> phase_dist(0.0f, 2.0f * std::numbers::pi_v<float>);

    std::vector<std::complex<float>> bins(kNumBins);
    bins[0] = std::complex<float>(magnitude[0], 0.0f);  // DC stays real
    for (std::size_t k = 1; k < kNumBins - 1; ++k) {
        const float phase = phase_dist(rng);
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

ThreeWavGenerator::ThreeWavGenerator(std::size_t samples, std::size_t num_pages)
    : samples_(samples), num_pages_(num_pages) {}

bool ThreeWavGenerator::Generate(const std::array<std::filesystem::path, 3>& input_paths,
                                 const std::filesystem::path& output_directory,
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

    // Step 5: generate 8 pages. Each cell is a weighted sum of the 3
    // per-file magnitude spectra, weights = axis positions + epsilon.
    for (std::size_t z = 0; z < num_pages_; ++z) {
        std::vector<float> page_samples;
        page_samples.reserve(samples_ * kCellsPerPage);

        const float z_weight = static_cast<float>(z) / 7.0f + kWeightEpsilon;

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            const float y_weight = static_cast<float>(y) / 7.0f + kWeightEpsilon;
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                const float x_weight = static_cast<float>(x) / 7.0f + kWeightEpsilon;

                // Weighted sum of the 3 per-file magnitude spectra.
                std::vector<float> combined_mag(kNumBins, 0.0f);
                for (std::size_t k = 0; k < kNumBins; ++k) {
                    combined_mag[k] = x_weight * file_avg_magnitudes[0][k] +
                                      y_weight * file_avg_magnitudes[1][k] +
                                      z_weight * file_avg_magnitudes[2][k];
                }

                // Random-phase iFFT -> time-domain cell. Seed is the
                // linear cell index so output is deterministic.
                const std::uint32_t cell_seed =
                    static_cast<std::uint32_t>((z * kCellsPerSide + y) * kCellsPerSide + x);
                auto cell = RandomPhaseIfft(combined_mag, ifft, cell_seed);

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
