#include "engine/wavetable_voice.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "engine/wavetable_bank.h"

namespace {

constexpr size_t kPageSamples = 64 * 2048;
constexpr uint32_t kSampleRate = 44100;

void WritePage(const std::filesystem::path& path) {
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

std::filesystem::path MakeBank() {
    auto dir = std::filesystem::temp_directory_path() / "fim_voice_test_bank";
    std::filesystem::create_directories(dir);
    for (size_t z = 1; z <= 8; ++z) {
        WritePage(dir / (std::to_string(z) + ".wav"));
    }
    return dir;
}

}  // namespace

TEST_CASE("WavetableVoice renders silence when not playing", "[voice]") {
    auto dir = MakeBank();
    auto bank = fim::engine::WavetableBank::Load(dir.string());
    REQUIRE(bank != nullptr);

    fim::engine::WavetableVoice voice(48000.0f);
    voice.SetBank(std::shared_ptr<const fim::engine::WavetableBank>(std::move(bank)));
    // playing_ defaults to false; do not call SetPlaying(true).

    std::vector<float> out(256, 1.234f);  // pre-filled with non-zero
    voice.RenderBlock(out.data(), out.size());

    for (float s : out) {
        REQUIRE(s == 0.0f);
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("WavetableVoice produces nonzero audio when playing", "[voice]") {
    auto dir = MakeBank();
    auto bank = fim::engine::WavetableBank::Load(dir.string());
    REQUIRE(bank != nullptr);

    fim::engine::WavetableVoice voice(48000.0f);
    voice.SetBank(std::shared_ptr<const fim::engine::WavetableBank>(std::move(bank)));
    voice.SetX(0.0f);
    voice.SetY(0.0f);
    voice.SetZ(0.0f);
    voice.SetFrequency(440.0f);
    voice.SetPlaying(true);

    std::vector<float> out(2048, 0.0f);
    voice.RenderBlock(out.data(), out.size());

    bool any_nonzero = false;
    for (float s : out) {
        if (std::abs(s) > 0.001f) {
            any_nonzero = true;
            break;
        }
    }
    REQUIRE(any_nonzero);

    std::filesystem::remove_all(dir);
}
