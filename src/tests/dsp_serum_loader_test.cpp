#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <vector>

#include "dr_wav.h"
#include "dsp/serum_loader.h"

using Catch::Matchers::WithinAbs;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Write a Serum-like WAV file: `num_frames` frames of 2048 samples each.
// Each frame is a sine wave at a different frequency so we can
// distinguish frames in tests.
std::filesystem::path WriteTestSerumWav(const std::string& basename, std::size_t num_frames) {
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    constexpr std::size_t kFrameSize = 2048;
    std::vector<float> samples(num_frames * kFrameSize);
    for (std::size_t f = 0; f < num_frames; ++f) {
        const float freq_mult = 1.0f + static_cast<float>(f);
        for (std::size_t i = 0; i < kFrameSize; ++i) {
            const float phase = 2.0f * kPi * static_cast<float>(i) / kFrameSize;
            samples[f * kFrameSize + i] = std::sin(phase * freq_mult);
        }
    }

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = 44100;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    return path;
}

// Same as above but with a non-multiple-of-2048 sample count to test
// invalid-length handling.
std::filesystem::path WriteTestInvalidWav(const std::string& basename) {
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    // 3000 samples — not a multiple of 2048.
    std::vector<float> samples(3000, 0.5f);

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = 44100;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    return path;
}

class SerumLoaderFixture {
public:
    ~SerumLoaderFixture() {
        for (const auto& p : to_cleanup_) {
            std::filesystem::remove(p);
        }
    }
    void Track(const std::filesystem::path& p) { to_cleanup_.push_back(p); }

private:
    std::vector<std::filesystem::path> to_cleanup_;
};

}  // namespace

TEST_CASE_METHOD(SerumLoaderFixture, "SerumLoader loads a valid 4-frame file",
                 "[dsp][serum_loader]") {
    const auto path = WriteTestSerumWav("fim_serum_4frames", 4);
    Track(path);

    const auto loaded = fim::dsp::SerumLoader::Load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->frames.size() == 4);
    for (const auto& frame : loaded->frames) {
        REQUIRE(frame.size() == 2048);
    }
    REQUIRE(loaded->source_sample_rate == 44100);
}

TEST_CASE_METHOD(SerumLoaderFixture, "SerumLoader returns nullopt for invalid sample count",
                 "[dsp][serum_loader]") {
    const auto path = WriteTestInvalidWav("fim_serum_invalid");
    Track(path);

    const auto loaded = fim::dsp::SerumLoader::Load(path);
    REQUIRE_FALSE(loaded.has_value());
}

TEST_CASE_METHOD(SerumLoaderFixture, "SerumLoader removes DC per frame", "[dsp][serum_loader]") {
    const auto path = WriteTestSerumWav("fim_serum_dc", 2);
    Track(path);

    const auto loaded = fim::dsp::SerumLoader::Load(path);
    REQUIRE(loaded.has_value());
    for (const auto& frame : loaded->frames) {
        float sum = 0.0f;
        for (float s : frame) {
            sum += s;
        }
        REQUIRE_THAT(sum / static_cast<float>(frame.size()), WithinAbs(0.0f, 1e-5));
    }
}

TEST_CASE("SerumLoader returns nullopt for a nonexistent file", "[dsp][serum_loader]") {
    const auto result = fim::dsp::SerumLoader::Load("/tmp/this_serum_file_does_not_exist_xyz.wav");
    REQUIRE_FALSE(result.has_value());
}
