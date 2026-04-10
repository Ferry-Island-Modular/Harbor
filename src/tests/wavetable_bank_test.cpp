#include "engine/wavetable_bank.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

constexpr size_t kPageSamples = 64 * 2048;
constexpr uint32_t kSampleRate = 44100;

void WriteFakePage(const std::filesystem::path& path) {
    std::vector<int16_t> samples(kPageSamples);
    for (size_t i = 0; i < kPageSamples; ++i) {
        const float t = static_cast<float>(i) / 2048.0f;
        samples[i] = static_cast<int16_t>(std::sin(t * 2.0f * 3.14159f) * 16384.0f);
    }

    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t fmt_size = 16;
    const uint32_t riff_size = 36 + data_bytes;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riff_size), 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    f.write(reinterpret_cast<const char*>(&fmt_size), 4);
    const uint16_t audio_format = 1;
    const uint16_t channels = 1;
    const uint32_t byte_rate = kSampleRate * 2;
    const uint16_t block_align = 2;
    const uint16_t bits = 16;
    f.write(reinterpret_cast<const char*>(&audio_format), 2);
    f.write(reinterpret_cast<const char*>(&channels), 2);
    f.write(reinterpret_cast<const char*>(&kSampleRate), 4);
    f.write(reinterpret_cast<const char*>(&byte_rate), 4);
    f.write(reinterpret_cast<const char*>(&block_align), 2);
    f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&data_bytes), 4);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}

std::filesystem::path MakeFakeBank() {
    auto dir = std::filesystem::temp_directory_path() / "fim_test_bank";
    std::filesystem::create_directories(dir);
    for (size_t z = 1; z <= 8; ++z) {
        WriteFakePage(dir / (std::to_string(z) + ".wav"));
    }
    return dir;
}

}  // namespace

TEST_CASE("WavetableBank::Load reads 8 pages and exposes wave pointers", "[bank]") {
    auto dir = MakeFakeBank();

    auto bank = fim::engine::WavetableBank::Load(dir.string());
    REQUIRE(bank != nullptr);

    REQUIRE(bank->total_waves() == 512);
    REQUIRE(bank->wavetable_size() == 2048);

    float** waves = bank->wavetable_pointers();
    REQUIRE(waves != nullptr);
    REQUIRE(waves[0] != nullptr);

    // Wraparound sample requirement: wave[N] == wave[0].
    REQUIRE(waves[0][2048] == waves[0][0]);

    std::filesystem::remove_all(dir);
}

TEST_CASE("WavetableBank::Load returns nullptr for missing directory", "[bank]") {
    auto bank = fim::engine::WavetableBank::Load("/tmp/this_dir_does_not_exist_xyz");
    REQUIRE(bank == nullptr);
}
