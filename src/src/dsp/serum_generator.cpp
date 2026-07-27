#include "dsp/serum_generator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "dr_wav.h"
#include "dsp/post_effects.h"
#include "dsp/serum_loader.h"
#include "dsp/serum_morpher.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kCellsPerSide = 8;
constexpr std::size_t kCellsPerPage = kCellsPerSide * kCellsPerSide;  // 64
constexpr std::uint32_t kOutputSampleRate = 44100;

float BipolarFormantAmount(std::size_t position) {
    constexpr float kAmounts[8] = {
        0.0f, 1.0f / 6.0f, 1.0f / 3.0f, 0.5f, 0.5f, 2.0f / 3.0f, 5.0f / 6.0f, 1.0f,
    };
    return kAmounts[std::min(position, std::size_t{7})];
}

}  // namespace

SerumGenerator::SerumGenerator(std::size_t samples, std::size_t num_pages)
    : samples_(samples), num_pages_(num_pages) {
    if (samples != 2048 || num_pages != 8) {
        throw std::invalid_argument(
            "SerumGenerator currently supports only 2048 samples and 8 pages");
    }
}

bool SerumGenerator::Generate(const std::filesystem::path& input_audio_path,
                              const std::filesystem::path& output_directory,
                              const SerumGenerateOptions& options,
                              const ProgressCallback& on_progress) const {
    // Step 1: load and split the Serum WAV into N × 2048 frames.
    auto loaded = SerumLoader::Load(input_audio_path);
    if (!loaded.has_value() || loaded->frames.empty()) {
        return false;
    }

    // Step 2: ensure output directory exists.
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    // Step 3: compute source FFT caches, one per loaded frame.
    SerumMorpher morpher;
    std::vector<SerumFftCache> source_caches;
    source_caches.reserve(loaded->frames.size());
    for (const auto& frame : loaded->frames) {
        source_caches.push_back(morpher.ComputeCache(frame));
    }
    morpher.AlignSourcePhases(source_caches);

    // Step 4: frequency-domain interpolate to exactly 8 caches (one per
    // X position in the output wavetable grid).
    const auto x_caches = morpher.InterpolateCaches(source_caches, kCellsPerSide);

    // Step 5: generate 8 pages.
    for (std::size_t z = 0; z < num_pages_; ++z) {
        std::vector<float> page_samples;
        page_samples.reserve(samples_ * kCellsPerPage);

        const float z_amount = static_cast<float>(z) / 7.0f;

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            const float y_amount = options.y_mode == SerumMode::kFormant
                                       ? BipolarFormantAmount(y)
                                       : static_cast<float>(y) / 7.0f;
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                // Apply Y morph.
                const auto y_morphed = morpher.Apply(x_caches[x], options.y_mode, y_amount);

                // Re-derive the cache from the Y-morphed spectrum so we
                // can chain the Z morph.
                SerumFftCache mid_cache;
                mid_cache.amplitudes.resize(y_morphed.size());
                mid_cache.phases.resize(y_morphed.size());
                mid_cache.normalized_real.resize(y_morphed.size());
                mid_cache.normalized_imag.resize(y_morphed.size());
                for (std::size_t k = 0; k < y_morphed.size(); ++k) {
                    mid_cache.amplitudes[k] = std::abs(y_morphed[k]);
                    mid_cache.phases[k] = std::arg(y_morphed[k]);
                    mid_cache.normalized_real[k] = std::cos(mid_cache.phases[k]);
                    mid_cache.normalized_imag[k] = std::sin(mid_cache.phases[k]);
                }

                // Apply Z morph.
                const auto z_morphed = morpher.Apply(mid_cache, options.z_mode, z_amount);

                // Inverse FFT back to time domain.
                auto cell = morpher.InverseFft(z_morphed);
                if (options.z_mode == SerumMode::kCrush) {
                    const auto crush = ZCrushAmount(static_cast<int>(z));
                    ZCrush(cell, crush.bit_depth, crush.sample_hold);
                }

                // Remove DC from the cell.
                float dc_sum = 0.0f;
                for (float s : cell) {
                    dc_sum += s;
                }
                const float dc = dc_sum / static_cast<float>(cell.size());
                for (float& s : cell) {
                    s -= dc;
                }

                // Normalize the cell to peak 1.0.
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

                // Serum cells are already at samples_ length (2048) since
                // we ifft'd at 2048. Append directly — no resampling
                // needed.
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

bool SerumGenerator::WritePageToWav(const std::filesystem::path& path,
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
