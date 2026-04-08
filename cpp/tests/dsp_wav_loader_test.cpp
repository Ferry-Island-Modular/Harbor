#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "dr_wav.h"
#include "dsp/wav_loader.h"

using Catch::Matchers::WithinAbs;

namespace {

// Writes a small test WAV file using dr_wav directly. Returns the path.
// Used by fixtures to produce known inputs for the loader tests.
std::filesystem::path WriteTestWavMono(const std::string& basename,
                                       const std::vector<float>& samples,
                                       std::uint32_t sample_rate) {
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    return path;
}

std::filesystem::path WriteTestWavStereo(const std::string& basename,
                                         const std::vector<float>& left,
                                         const std::vector<float>& right,
                                         std::uint32_t sample_rate) {
    REQUIRE(left.size() == right.size());
    const auto path = std::filesystem::temp_directory_path() / (basename + ".wav");
    std::filesystem::remove(path);

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 2;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 32;

    drwav wav;
    REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
    std::vector<float> interleaved(left.size() * 2);
    for (std::size_t i = 0; i < left.size(); ++i) {
        interleaved[2 * i] = left[i];
        interleaved[2 * i + 1] = right[i];
    }
    drwav_write_pcm_frames(&wav, left.size(), interleaved.data());
    drwav_uninit(&wav);
    return path;
}

class WavLoaderFixture {
public:
    ~WavLoaderFixture() {
        for (const auto& p : to_cleanup_) {
            std::filesystem::remove(p);
        }
    }
    void Track(const std::filesystem::path& p) { to_cleanup_.push_back(p); }

private:
    std::vector<std::filesystem::path> to_cleanup_;
};

}  // namespace

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav round-trips a mono file", "[dsp][wav_loader]") {
    const std::vector<float> input{0.25f, 0.5f, 0.75f, -0.5f, -0.25f};
    const auto path = WriteTestWavMono("fim_wav_loader_mono", input, 48000);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/false);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->sample_rate == 48000);
    REQUIRE(loaded->original_channels == 1);
    REQUIRE(loaded->samples.size() == input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE_THAT(loaded->samples[i], WithinAbs(input[i], 1e-6));
    }
}

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav mixes stereo down to mono", "[dsp][wav_loader]") {
    const std::vector<float> left{1.0f, 0.5f, 0.0f, -0.5f};
    const std::vector<float> right{0.0f, 0.5f, 1.0f, 0.5f};
    const auto path = WriteTestWavStereo("fim_wav_loader_stereo", left, right, 44100);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/false);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->sample_rate == 44100);
    REQUIRE(loaded->original_channels == 2);
    REQUIRE(loaded->samples.size() == left.size());
    // Mono = average of left and right.
    REQUIRE_THAT(loaded->samples[0], WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(loaded->samples[1], WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(loaded->samples[2], WithinAbs(0.5f, 1e-6));
    REQUIRE_THAT(loaded->samples[3], WithinAbs(0.0f, 1e-6));
}

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav normalizes to peak 1.0", "[dsp][wav_loader]") {
    const std::vector<float> input{0.1f, -0.2f, 0.4f, -0.05f};
    const auto path = WriteTestWavMono("fim_wav_loader_norm", input, 44100);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/true);
    REQUIRE(loaded.has_value());

    float peak = 0.0f;
    for (float s : loaded->samples) {
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE_THAT(peak, WithinAbs(1.0f, 1e-6));

    // Samples should be scaled proportionally: input peak is 0.4, so the
    // output at the peak index should be 1.0 (or -1.0) and others scaled.
    REQUIRE_THAT(loaded->samples[2], WithinAbs(1.0f, 1e-6));
    REQUIRE_THAT(loaded->samples[0], WithinAbs(0.25f, 1e-6));
}

TEST_CASE_METHOD(WavLoaderFixture, "LoadWav preserves exact values when not normalizing",
                 "[dsp][wav_loader]") {
    const std::vector<float> input{0.1f, 0.2f, 0.3f};
    const auto path = WriteTestWavMono("fim_wav_loader_exact", input, 22050);
    Track(path);

    const auto loaded = fim::dsp::LoadWav(path, /*normalize=*/false);
    REQUIRE(loaded.has_value());
    REQUIRE_THAT(loaded->samples[0], WithinAbs(0.1f, 1e-6));
    REQUIRE_THAT(loaded->samples[1], WithinAbs(0.2f, 1e-6));
    REQUIRE_THAT(loaded->samples[2], WithinAbs(0.3f, 1e-6));
}

TEST_CASE("LoadWav returns nullopt for a nonexistent file", "[dsp][wav_loader]") {
    const auto result = fim::dsp::LoadWav("/tmp/this_path_definitely_does_not_exist_xyz_12345.wav");
    REQUIRE_FALSE(result.has_value());
}
