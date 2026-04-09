#include "app/services/export_writer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "dr_wav.h"
#include "dsp/resample.h"

namespace fim::app {

namespace {

constexpr int kNumPages = 8;
constexpr int kCellsPerPage = 64;
constexpr int kSourceCycleSamples = 2048;
constexpr std::uint32_t kSampleRate = 44100;

bool ReadWavMono16(const std::filesystem::path& path, std::vector<float>& out) {
    drwav wav;
    if (!drwav_init_file(&wav, path.string().c_str(), nullptr)) {
        return false;
    }
    const drwav_uint64 total_frames = wav.totalPCMFrameCount;
    const drwav_uint16 channels = wav.channels;
    std::vector<float> interleaved(total_frames * channels);
    const drwav_uint64 read = drwav_read_pcm_frames_f32(&wav, total_frames, interleaved.data());
    drwav_uninit(&wav);
    if (read != total_frames) {
        return false;
    }
    if (channels == 1) {
        out = std::move(interleaved);
    } else {
        // Average down to mono.
        out.resize(read);
        for (drwav_uint64 i = 0; i < read; ++i) {
            float sum = 0.0f;
            for (drwav_uint16 c = 0; c < channels; ++c) {
                sum += interleaved[i * channels + c];
            }
            out[i] = sum / static_cast<float>(channels);
        }
    }
    return true;
}

bool WriteWavMono16(const std::filesystem::path& path, const std::vector<float>& samples) {
    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = kSampleRate;
    format.bitsPerSample = 16;
    drwav wav;
    if (!drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr)) {
        return false;
    }
    std::vector<std::int16_t> int_samples(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, samples[i]));
        int_samples[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }
    const drwav_uint64 written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);
    return written == int_samples.size();
}

}  // namespace

bool WriteBankToExportDir(const QString& source_dir, const QString& dest_dir,
                          int target_samples_per_cycle) {
    if (target_samples_per_cycle <= 0 || target_samples_per_cycle > kSourceCycleSamples) {
        return false;
    }
    const std::filesystem::path src(source_dir.toStdString());
    const std::filesystem::path dest(dest_dir.toStdString());
    if (!std::filesystem::exists(src)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        return false;
    }

    for (int p = 1; p <= kNumPages; ++p) {
        const auto src_path = src / (std::to_string(p) + ".wav");
        const auto dest_path = dest / (std::to_string(p) + ".wav");
        if (!std::filesystem::exists(src_path)) {
            return false;
        }

        if (target_samples_per_cycle == kSourceCycleSamples) {
            // Identity copy — no resample, no quality loss.
            std::error_code copy_ec;
            std::filesystem::copy_file(src_path, dest_path,
                                       std::filesystem::copy_options::overwrite_existing, copy_ec);
            if (copy_ec) {
                return false;
            }
            continue;
        }

        // Read source bank, decimate per-cycle, write destination.
        std::vector<float> source_samples;
        if (!ReadWavMono16(src_path, source_samples)) {
            return false;
        }
        const std::size_t expected_source = kCellsPerPage * kSourceCycleSamples;
        if (source_samples.size() != expected_source) {
            return false;
        }

        std::vector<float> dest_samples;
        dest_samples.reserve(kCellsPerPage * target_samples_per_cycle);
        for (int cell = 0; cell < kCellsPerPage; ++cell) {
            std::vector<float> cycle(source_samples.begin() + cell * kSourceCycleSamples,
                                     source_samples.begin() + (cell + 1) * kSourceCycleSamples);
            // Use ResampleTo with synthetic rates that produce the desired
            // ratio. e.g. 2048 -> 256 is an 8:1 downsample.
            const std::uint32_t in_rate = kSourceCycleSamples;
            const std::uint32_t out_rate = static_cast<std::uint32_t>(target_samples_per_cycle);
            auto resampled = fim::dsp::ResampleTo(cycle, in_rate, out_rate);
            // libsamplerate may produce off-by-one frames; trim or pad to
            // exactly target_samples_per_cycle.
            resampled.resize(target_samples_per_cycle, 0.0f);
            dest_samples.insert(dest_samples.end(), resampled.begin(), resampled.end());
        }

        if (!WriteWavMono16(dest_path, dest_samples)) {
            return false;
        }
    }
    return true;
}

}  // namespace fim::app
