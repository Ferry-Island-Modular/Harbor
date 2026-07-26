#include "dsp/single_wav_generator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "dr_wav.h"
#include "dsp/cycle_extractor.h"
#include "dsp/fft_resampler.h"
#include "dsp/post_effects.h"
#include "dsp/spectral_modifier.h"
#include "dsp/stft.h"
#include "dsp/wav_loader.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kStftFftSize = 2048;
constexpr std::size_t kStftHopSize = 1024;
constexpr std::size_t kCellsPerSide = 8;
constexpr std::size_t kCellsPerPage = kCellsPerSide * kCellsPerSide;  // 64
constexpr std::uint32_t kOutputSampleRate = 44100;

}  // namespace

SingleWavGenerator::SingleWavGenerator(std::size_t samples, std::size_t oversample_factor,
                                       std::size_t num_pages)
    : samples_(samples),
      oversample_factor_(oversample_factor),
      n_samples_(samples * oversample_factor),
      num_pages_(num_pages) {
    if (samples != 2048 || oversample_factor != 4 || num_pages != 8) {
        throw std::invalid_argument(
            "SingleWavGenerator currently supports only 2048 samples, 4x oversampling, and 8 "
            "pages");
    }
}

bool SingleWavGenerator::Generate(const std::filesystem::path& input_audio_path,
                                  const std::filesystem::path& output_directory,
                                  const GenerateOptions& options,
                                  const ProgressCallback& on_progress) const {
    auto loaded = LoadWav(input_audio_path, /*normalize=*/true);
    if (!loaded.has_value() || loaded->samples.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    Stft stft(kStftFftSize, kStftHopSize);
    const auto bins = stft.Analyze(loaded->samples);
    if (bins.empty()) {
        return false;
    }
    const auto magnitude = Magnitude(bins);
    const auto phase = Phase(bins);
    const std::size_t num_frames = bins.size();
    const auto selected_frames =
        SelectSourceFrames(magnitude, options.frame_selection, kCellsPerSide);
    if (selected_frames.size() != kCellsPerSide) {
        return false;
    }

    // Stft::Analyze and the Magnitude/Phase helpers return arrays indexed
    // [frame][bin] (matching the Stft::Analyze internal frame loop). But
    // SpectralModifier and CycleExtractor expect [bin][frame] (matching
    // Python's numpy convention from scipy.signal.stft, which produces a
    // (num_bins, num_frames) shape). Transpose once here so the inner loop
    // can pass the same arrays to both.
    const std::size_t num_bins = stft.num_bins();
    std::vector<std::vector<float>> magnitude_t(num_bins, std::vector<float>(num_frames, 0.0f));
    std::vector<std::vector<float>> phase_t(num_bins, std::vector<float>(num_frames, 0.0f));
    for (std::size_t f = 0; f < num_frames; ++f) {
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude_t[k][f] = magnitude[f][k];
            phase_t[k][f] = phase[f][k];
        }
    }

    CycleExtractor extractor(kStftFftSize, n_samples_);
    FftResampler downsampler(n_samples_, samples_);

    for (std::size_t z = 0; z < num_pages_; ++z) {
        auto modifier = options.random_seed.has_value()
                            ? SpectralModifier(*options.random_seed + static_cast<std::uint32_t>(z))
                            : SpectralModifier();

        // Precompute the Z-crush params for this page if crush mode is
        // selected. We use `z` (the page index) as the crush intensity so
        // later pages are progressively more crushed — mirrors how Z0/Z1
        // modes get progressively stronger with z.
        const ZCrushParams crush_params = (options.z_mode == ZMode::kCrush)
                                              ? ZCrushAmount(static_cast<int>(z))
                                              : ZCrushParams{16, 1};

        std::vector<float> page_downsampled;
        page_downsampled.reserve(samples_ * kCellsPerPage);

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                std::vector<std::vector<float>> mag_copy;
                std::vector<std::vector<float>> phase_copy;
                std::size_t frame_selection = selected_frames[x];

                // The legacy X-stretch reads the time-averaged envelope, so
                // preserving it requires the complete analysis matrices.
                // When X is source progression only, operate on the selected
                // frame directly. This is both clearer and dramatically less
                // expensive for long recordings.
                if (options.apply_x_spectral_stretch) {
                    mag_copy = magnitude_t;
                    phase_copy = phase_t;
                } else {
                    mag_copy.assign(num_bins, std::vector<float>(1, 0.0f));
                    phase_copy.assign(num_bins, std::vector<float>(1, 0.0f));
                    for (std::size_t k = 0; k < num_bins; ++k) {
                        mag_copy[k][0] = magnitude_t[k][frame_selection];
                        phase_copy[k][0] = phase_t[k][frame_selection];
                    }
                    frame_selection = 0;
                }

                modifier.Apply(mag_copy, phase_copy, static_cast<int>(x), static_cast<int>(y),
                               static_cast<int>(z), options.y_mode, options.z_mode,
                               options.apply_x_spectral_stretch);

                auto cell_oversampled = extractor.Extract(mag_copy, phase_copy, frame_selection);

                // Z-crush runs as a post-effect on the oversampled cycle
                // before downsampling, so the quantization levels and
                // sample-hold pattern are preserved through the final
                // rate conversion rather than being smoothed out.
                if (options.z_mode == ZMode::kCrush) {
                    ZCrush(cell_oversampled, crush_params.bit_depth, crush_params.sample_hold);
                }

                const auto cell_downsampled = downsampler.Resample(cell_oversampled);
                page_downsampled.insert(page_downsampled.end(), cell_downsampled.begin(),
                                        cell_downsampled.end());
            }
        }

        float page_peak = 0.0f;
        for (float s : page_downsampled) {
            page_peak = std::max(page_peak, std::abs(s));
        }
        if (page_peak > 0.0f) {
            const float inv = 1.0f / page_peak;
            for (float& s : page_downsampled) {
                s *= inv;
            }
        }

        const auto path = output_directory / (std::to_string(z + 1) + ".wav");
        if (!WritePageToWav(path, page_downsampled)) {
            return false;
        }

        if (on_progress) {
            const int percent = static_cast<int>((z + 1) * 100 / num_pages_);
            on_progress(percent);
        }
    }

    return true;
}

bool SingleWavGenerator::WritePageToWav(const std::filesystem::path& path,
                                        const std::vector<float>& downsampled_page) const {
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

    std::vector<std::int16_t> int_samples(downsampled_page.size());
    for (std::size_t i = 0; i < downsampled_page.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, downsampled_page[i]));
        int_samples[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }

    const drwav_uint64 frames_written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);
    return frames_written == int_samples.size();
}

}  // namespace fim::dsp
