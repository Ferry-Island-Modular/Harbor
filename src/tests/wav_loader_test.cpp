#include "engine/wav_loader.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

// Writes a minimal 16-bit PCM mono WAV file to `path` containing `samples`.
// Used by tests to avoid depending on dr_wav for fixture creation.
void WriteRawWav16(const std::filesystem::path& path, const std::vector<int16_t>& samples,
                   uint32_t sample_rate) {
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t fmt_chunk_size = 16;
    const uint32_t riff_size = 36 + data_bytes;

    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riff_size), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    f.write(reinterpret_cast<const char*>(&fmt_chunk_size), 4);
    const uint16_t audio_format = 1;  // PCM
    const uint16_t num_channels = 1;
    const uint32_t byte_rate = sample_rate * num_channels * 2;
    const uint16_t block_align = num_channels * 2;
    const uint16_t bits_per_sample = 16;
    f.write(reinterpret_cast<const char*>(&audio_format), 2);
    f.write(reinterpret_cast<const char*>(&num_channels), 2);
    f.write(reinterpret_cast<const char*>(&sample_rate), 4);
    f.write(reinterpret_cast<const char*>(&byte_rate), 4);
    f.write(reinterpret_cast<const char*>(&block_align), 2);
    f.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&data_bytes), 4);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}

}  // namespace

TEST_CASE("LoadWavMono reads a 16-bit PCM mono WAV", "[wav_loader]") {
    auto temp = std::filesystem::temp_directory_path() / "fim_test_simple.wav";

    // Write 100 samples of a square-ish wave at half int16 amplitude.
    std::vector<int16_t> samples(100);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>((i % 50 < 25 ? 16384 : -16384));
    }
    WriteRawWav16(temp, samples, 48000);

    auto loaded = fim::engine::LoadWavMono(temp.string());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 100);

    // 16384 / 32768 = 0.5
    REQUIRE(std::abs((*loaded)[0] - 0.5f) < 0.01f);
    REQUIRE(std::abs((*loaded)[25] - (-0.5f)) < 0.01f);

    std::filesystem::remove(temp);
}

TEST_CASE("LoadWavMono returns nullopt for missing file", "[wav_loader]") {
    auto loaded = fim::engine::LoadWavMono("/tmp/this_file_does_not_exist_12345.wav");
    REQUIRE_FALSE(loaded.has_value());
}
