#include "dsp/single_wav_generator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dr_wav.h"
#include "dsp/cycle_extractor.h"
#include "dsp/fft_resampler.h"
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
      num_pages_(num_pages) {}

bool SingleWavGenerator::Generate(const std::filesystem::path& input_audio_path,
                                  const std::filesystem::path& output_directory,
                                  const ProgressCallback& on_progress) const {
    // Step 1: load and normalize the input audio.
    auto loaded = LoadWav(input_audio_path, /*normalize=*/true);
    if (!loaded.has_value() || loaded->samples.empty()) {
        return false;
    }

    // Step 2: ensure the output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    // Step 3: STFT analysis. Cached across pages — same input audio for all
    // 8 z values.
    Stft stft(kStftFftSize, kStftHopSize);
    const auto bins = stft.Analyze(loaded->samples);
    if (bins.empty()) {
        // Audio is shorter than fft_size. Cannot resynthesize.
        return false;
    }
    const auto magnitude = Magnitude(bins);
    const auto phase = Phase(bins);
    const std::size_t num_frames = bins.size();

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

    // Step 4: prepare DSP objects (constructed once, reused across cells).
    CycleExtractor extractor(kStftFftSize, n_samples_);
    FftResampler downsampler(n_samples_, samples_);

    // Step 5: for each Z page, generate 64 cells, downsample, and write.
    for (std::size_t z = 0; z < num_pages_; ++z) {
        // Use a fresh SpectralModifier per page so phase randomization is
        // varied between pages but reproducible within a single Generate
        // call. Phase 3c may make the seed configurable.
        SpectralModifier modifier;

        // Each cell's downsampled samples are appended sequentially into
        // the page buffer: total size = samples_ * 64.
        std::vector<float> page_downsampled;
        page_downsampled.reserve(samples_ * kCellsPerPage);

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                auto mag_copy = magnitude_t;
                auto phase_copy = phase_t;
                modifier.Apply(mag_copy, phase_copy, static_cast<int>(x), static_cast<int>(y),
                               static_cast<int>(z));

                const std::size_t frame_selection = static_cast<std::size_t>(
                    static_cast<float>(x) / 7.0f * static_cast<float>(num_frames - 1));

                const auto cell_oversampled =
                    extractor.Extract(mag_copy, phase_copy, frame_selection);

                const auto cell_downsampled = downsampler.Resample(cell_oversampled);
                page_downsampled.insert(page_downsampled.end(), cell_downsampled.begin(),
                                        cell_downsampled.end());
            }
        }

        // Step 6: globally normalize the page so the max abs value across
        // all 131072 samples is 1.0. Matches Python's save_wavetables which
        // divides by the global max before int16 quantization.
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
