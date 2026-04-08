#include "app/services/stub_bank_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "dr_wav.h"

namespace fim::app {

namespace {

constexpr size_t kWavetableSize = 2048;
constexpr size_t kWavesPerPage = 64;
constexpr size_t kPagesPerBank = 8;
constexpr uint32_t kSampleRate = 44100;

// Generate `kWavesPerPage * kWavetableSize` samples. Each wavetable cycle is
// `freq_multiplier` periods of a sine wave across its 2048 samples — so
// page 1 is one period per cycle (the fundamental), page 2 is two periods
// (the first harmonic), etc. The FourSeas oscillator scans through the cycle
// at runtime to produce audible pitch changes when scrubbing the Z slider.
std::vector<float> SinePage(float freq_multiplier) {
    std::vector<float> samples(kWavesPerPage * kWavetableSize);
    for (size_t wave = 0; wave < kWavesPerPage; ++wave) {
        for (size_t i = 0; i < kWavetableSize; ++i) {
            const float phase = 2.0f * 3.14159265358979f * static_cast<float>(i) / kWavetableSize;
            samples[wave * kWavetableSize + i] = std::sin(phase * freq_multiplier);
        }
    }
    return samples;
}

bool WritePageToWav(const std::filesystem::path& path, const std::vector<float>& samples) {
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

    // dr_wav can convert float→int16 itself, but doing it explicitly here
    // keeps the clamping behavior obvious and matches our 16-bit PCM target.
    std::vector<int16_t> int_samples(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, samples[i]));
        int_samples[i] = static_cast<int16_t>(clamped * 32767.0f);
    }

    const drwav_uint64 frames_written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);

    return frames_written == int_samples.size();
}

}  // namespace

bool StubBankWriter::WriteSineBank(const std::filesystem::path& output_directory) {
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    for (size_t page = 0; page < kPagesPerBank; ++page) {
        // Each page gets a different "frequency multiplier" so the pages
        // sound different when scrubbed via the Z slider.
        const float multiplier = 1.0f + static_cast<float>(page);
        const auto samples = SinePage(multiplier);
        const auto path = output_directory / (std::to_string(page + 1) + ".wav");
        if (!WritePageToWav(path, samples)) {
            return false;
        }
    }
    return true;
}

}  // namespace fim::app
