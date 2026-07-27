#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dr_wav.h"
#include "dsp/generate_options.h"
#include "dsp/single_wav_generator.h"
#include "engine/wavetable_bank.h"
#include "engine/wavetable_voice.h"

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kSampleRate = 44100;
constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
constexpr std::uint32_t kGenerationSeed = 0x46494d31U;  // "FIM1"
constexpr float kFixtureSeconds = 1.5f;
constexpr float kPreviewSeconds = 3.0f;
constexpr float kDefaultPreviewFrequency = 110.0f;
constexpr std::size_t kRenderBlockSize = 64;

struct Fixture {
    std::string name;
    std::vector<float> samples;
};

class DeterministicNoise {
public:
    explicit DeterministicNoise(std::uint32_t seed) : state_(seed) {}

    float Next() {
        state_ = state_ * 1664525U + 1013904223U;
        const float unit = static_cast<float>(state_ >> 8U) / 16777215.0f;
        return unit * 2.0f - 1.0f;
    }

private:
    std::uint32_t state_;
};

void Normalize(std::vector<float>& samples, float peak = 0.9f) {
    float observed_peak = 0.0f;
    for (float sample : samples) {
        observed_peak = std::max(observed_peak, std::abs(sample));
    }
    if (observed_peak <= 0.0f) {
        return;
    }
    const float scale = peak / observed_peak;
    for (float& sample : samples) {
        sample *= scale;
    }
}

std::vector<float> MakeHarmonicSustain() {
    const std::size_t count = static_cast<std::size_t>(kFixtureSeconds * kSampleRate);
    std::vector<float> samples(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float drift = 1.0f + 0.003f * std::sin(kTau * 0.7f * t);
        for (int harmonic = 1; harmonic <= 16; ++harmonic) {
            const float amplitude = 1.0f / static_cast<float>(harmonic);
            samples[i] += amplitude * std::sin(kTau * 110.0f * drift * harmonic * t);
        }
        samples[i] *= 0.8f + 0.2f * std::sin(kTau * 0.35f * t);
    }
    Normalize(samples);
    return samples;
}

float Gaussian(float x, float center, float width) {
    const float normalized = (x - center) / width;
    return std::exp(-0.5f * normalized * normalized);
}

std::vector<float> MakeVowelSweep() {
    const std::size_t count = static_cast<std::size_t>(kFixtureSeconds * kSampleRate);
    std::vector<float> samples(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float morph = t / kFixtureSeconds;
        const float formant1 = 700.0f + morph * 300.0f;
        const float formant2 = 1200.0f + morph * 600.0f;
        const float formant3 = 2500.0f - morph * 300.0f;
        for (int harmonic = 1; harmonic <= 30; ++harmonic) {
            const float frequency = 100.0f * harmonic;
            const float envelope = Gaussian(frequency, formant1, 120.0f) +
                                   0.7f * Gaussian(frequency, formant2, 180.0f) +
                                   0.45f * Gaussian(frequency, formant3, 260.0f);
            samples[i] +=
                envelope / std::sqrt(static_cast<float>(harmonic)) * std::sin(kTau * frequency * t);
        }
    }
    Normalize(samples);
    return samples;
}

std::vector<float> MakePercussion() {
    const std::size_t count = static_cast<std::size_t>(kFixtureSeconds * kSampleRate);
    std::vector<float> samples(count, 0.0f);
    DeterministicNoise noise(0x10203040U);
    constexpr std::array<float, 4> hit_times = {0.0f, 0.38f, 0.76f, 1.14f};
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        for (std::size_t hit = 0; hit < hit_times.size(); ++hit) {
            const float local = t - hit_times[hit];
            if (local < 0.0f || local > 0.3f) {
                continue;
            }
            const float body = std::sin(kTau * (95.0f - 55.0f * local) * local);
            const float transient = noise.Next();
            samples[i] += body * std::exp(-18.0f * local) +
                          transient * std::exp(-55.0f * local) * (hit % 2 == 0 ? 0.8f : 0.5f);
        }
    }
    Normalize(samples);
    return samples;
}

std::vector<float> MakeNoiseTexture() {
    const std::size_t count = static_cast<std::size_t>(kFixtureSeconds * kSampleRate);
    std::vector<float> samples(count, 0.0f);
    DeterministicNoise noise(0xa5a5f00dU);
    float lowpass = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        lowpass += 0.025f * (noise.Next() - lowpass);
        const float tonal =
            0.3f * std::sin(kTau * (180.0f + 40.0f * std::sin(kTau * 0.4f * t)) * t);
        samples[i] = lowpass + tonal + 0.12f * noise.Next();
    }
    Normalize(samples);
    return samples;
}

std::vector<float> MakeDenseMix() {
    const std::size_t count = static_cast<std::size_t>(kFixtureSeconds * kSampleRate);
    std::vector<float> samples(count, 0.0f);
    DeterministicNoise noise(0xc001d00dU);
    constexpr std::array<float, 4> chord = {110.0f, 138.59f, 164.81f, 220.0f};
    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        for (std::size_t note = 0; note < chord.size(); ++note) {
            const float vibrato = 1.0f + 0.002f * std::sin(kTau * (4.0f + note) * t);
            samples[i] += 0.25f * std::sin(kTau * chord[note] * vibrato * t);
            samples[i] += 0.08f * std::sin(kTau * chord[note] * 3.0f * t);
        }
        const float beat_phase = std::fmod(t, 0.3f);
        samples[i] += 0.35f * noise.Next() * std::exp(-65.0f * beat_phase);
    }
    Normalize(samples);
    return samples;
}

bool WriteMonoPcm16(const fs::path& path, const std::vector<float>& samples) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }

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

    std::vector<std::int16_t> pcm(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        pcm[i] = static_cast<std::int16_t>(std::clamp(samples[i], -1.0f, 1.0f) *
                                           static_cast<float>(INT16_MAX));
    }
    const drwav_uint64 written = drwav_write_pcm_frames(&wav, pcm.size(), pcm.data());
    drwav_uninit(&wav);
    return written == pcm.size();
}

enum class SweepAxis { kX, kY, kZ };

std::string_view AxisName(SweepAxis axis) {
    switch (axis) {
        case SweepAxis::kX:
            return "x";
        case SweepAxis::kY:
            return "y";
        case SweepAxis::kZ:
            return "z";
    }
    return "unknown";
}

void SetPosition(fim::engine::WavetableVoice& voice, SweepAxis axis, float value) {
    constexpr float neutral = 3.5f;
    voice.SetX(axis == SweepAxis::kX ? value : neutral);
    voice.SetY(axis == SweepAxis::kY ? value : neutral);
    voice.SetZ(axis == SweepAxis::kZ ? value : neutral);
}

bool RenderSweep(const fs::path& bank_directory, SweepAxis axis, float preview_frequency,
                 const fs::path& output_path) {
    auto loaded = fim::engine::WavetableBank::Load(bank_directory.string());
    if (!loaded) {
        return false;
    }
    std::shared_ptr<const fim::engine::WavetableBank> bank(std::move(loaded));
    fim::engine::WavetableVoice voice(static_cast<float>(kSampleRate));
    voice.SetBank(std::move(bank));
    voice.SetFrequency(preview_frequency);
    voice.SetPlaying(true);
    SetPosition(voice, axis, 0.0f);

    // Discard a short preroll so frequency and fixed axes settle before the
    // comparable portion begins.
    std::vector<float> block(kRenderBlockSize, 0.0f);
    const std::size_t preroll_frames = kSampleRate / 4;
    for (std::size_t rendered = 0; rendered < preroll_frames; rendered += kRenderBlockSize) {
        voice.RenderBlock(block.data(), block.size());
    }

    const std::size_t total_frames = static_cast<std::size_t>(kPreviewSeconds * kSampleRate);
    std::vector<float> preview(total_frames, 0.0f);
    for (std::size_t offset = 0; offset < total_frames; offset += kRenderBlockSize) {
        const std::size_t frames = std::min(kRenderBlockSize, total_frames - offset);
        const float progress = static_cast<float>(offset) / static_cast<float>(total_frames - 1);
        SetPosition(voice, axis, progress * 6.9999f);
        voice.RenderBlock(preview.data() + offset, frames);
    }
    Normalize(preview, 0.8f);
    return WriteMonoPcm16(output_path, preview);
}

std::vector<Fixture> BuildCorpus() {
    std::vector<Fixture> fixtures;
    fixtures.push_back({"harmonic_sustain", MakeHarmonicSustain()});
    fixtures.push_back({"vowel_sweep", MakeVowelSweep()});
    fixtures.push_back({"percussion", MakePercussion()});
    fixtures.push_back({"noise_texture", MakeNoiseTexture()});
    fixtures.push_back({"dense_mix", MakeDenseMix()});
    return fixtures;
}

std::string SafeFixtureName(std::string_view name) {
    std::string safe;
    safe.reserve(name.size());
    bool last_was_separator = false;
    for (unsigned char character : name) {
        if (std::isalnum(character)) {
            safe.push_back(static_cast<char>(std::tolower(character)));
            last_was_separator = false;
        } else if (!safe.empty() && !last_was_separator) {
            safe.push_back('_');
            last_was_separator = true;
        }
    }
    while (!safe.empty() && safe.back() == '_') {
        safe.pop_back();
    }
    return safe;
}

bool GenerateFixture(const std::string& name, std::string_view origin, const fs::path& source_path,
                     const fs::path& output_root, const fs::path& banks_directory,
                     const fs::path& previews_directory,
                     const fim::dsp::SingleWavGenerator& generator,
                     const fim::dsp::GenerateOptions& options, float preview_frequency,
                     std::ofstream& manifest) {
    std::cout << "Generating " << name << "...\n";
    const fs::path bank_path = banks_directory / name;
    if (!generator.Generate(source_path, bank_path, options)) {
        std::cerr << "Failed to generate bank for " << name << '\n';
        return false;
    }

    std::array<fs::path, 3> preview_paths;
    constexpr std::array<SweepAxis, 3> axes = {SweepAxis::kX, SweepAxis::kY, SweepAxis::kZ};
    for (std::size_t i = 0; i < axes.size(); ++i) {
        preview_paths[i] =
            previews_directory / (name + "_" + std::string(AxisName(axes[i])) + ".wav");
        if (!RenderSweep(bank_path, axes[i], preview_frequency, preview_paths[i])) {
            std::cerr << "Failed to render " << AxisName(axes[i]) << " preview for " << name
                      << '\n';
            return false;
        }
    }

    const std::string_view frame_selection =
        options.frame_selection == fim::dsp::FrameSelectionMode::kSalientWindow
            ? "salient_spectral_distance"
            : "uniform";
    manifest << name << ',' << origin << ',' << kGenerationSeed << ',' << preview_frequency
             << ",tilt,random," << frame_selection << ','
             << (options.apply_x_spectral_stretch ? "enabled" : "disabled") << ','
             << fs::relative(source_path, output_root).generic_string() << ','
             << fs::relative(bank_path, output_root).generic_string() << ','
             << fs::relative(preview_paths[0], output_root).generic_string() << ','
             << fs::relative(preview_paths[1], output_root).generic_string() << ','
             << fs::relative(preview_paths[2], output_root).generic_string() << '\n';
    return true;
}

int Run(const fs::path& output_root, const std::vector<fs::path>& external_inputs,
        float preview_frequency, fim::dsp::FrameSelectionMode frame_selection,
        bool apply_x_spectral_stretch) {
    const fs::path sources_directory = output_root / "sources";
    const fs::path banks_directory = output_root / "banks";
    const fs::path previews_directory = output_root / "previews";
    std::error_code ec;
    fs::create_directories(sources_directory, ec);
    fs::create_directories(banks_directory, ec);
    fs::create_directories(previews_directory, ec);
    if (ec) {
        std::cerr << "Unable to create evaluation output: " << ec.message() << '\n';
        return 1;
    }

    std::ofstream manifest(output_root / "manifest.csv", std::ios::trunc);
    if (!manifest) {
        std::cerr << "Unable to create evaluation manifest\n";
        return 1;
    }
    manifest << "fixture,origin,seed,preview_frequency_hz,y_mode,z_mode,frame_selection,"
                "x_spectral_stretch,source,bank,x_preview,y_preview,z_preview\n";

    fim::dsp::GenerateOptions options;
    options.random_seed = kGenerationSeed;
    options.frame_selection = frame_selection;
    options.apply_x_spectral_stretch = apply_x_spectral_stretch;
    fim::dsp::SingleWavGenerator generator;

    for (const auto& fixture : BuildCorpus()) {
        const fs::path source_path = sources_directory / (fixture.name + ".wav");
        if (!WriteMonoPcm16(source_path, fixture.samples) ||
            !GenerateFixture(fixture.name, "synthetic", source_path, output_root, banks_directory,
                             previews_directory, generator, options, preview_frequency, manifest)) {
            return 1;
        }
    }

    std::vector<std::string> external_names;
    for (const auto& input : external_inputs) {
        std::error_code input_ec;
        if (!fs::is_regular_file(input, input_ec) || input_ec || input.extension() != ".wav") {
            std::cerr << "External input is not a readable .wav file: " << input << '\n';
            return 1;
        }

        const std::string name = "real_" + SafeFixtureName(input.stem().string());
        if (name == "real_" ||
            std::find(external_names.begin(), external_names.end(), name) != external_names.end()) {
            std::cerr << "External input has an empty or duplicate fixture name: " << input << '\n';
            return 1;
        }
        external_names.push_back(name);

        const fs::path copied_source = sources_directory / (name + ".wav");
        fs::copy_file(input, copied_source, fs::copy_options::overwrite_existing, input_ec);
        if (input_ec ||
            !GenerateFixture(name, "external", copied_source, output_root, banks_directory,
                             previews_directory, generator, options, preview_frequency, manifest)) {
            if (input_ec) {
                std::cerr << "Failed to copy external input " << input << ": " << input_ec.message()
                          << '\n';
            }
            return 1;
        }
    }

    std::cout << "Evaluation corpus written to " << output_root << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    float preview_frequency = kDefaultPreviewFrequency;
    bool candidate_mode = false;
    std::optional<fim::dsp::FrameSelectionMode> requested_frame_selection;
    std::optional<bool> requested_x_spectral_stretch;
    int next_argument = 1;
    while (next_argument < argc) {
        const std::string_view argument(argv[next_argument]);
        if (argument == "--help") {
            std::cout << "Usage: fim-wavetable-eval [--candidate] "
                         "[--frame-selection uniform|salient] "
                         "[--x-stretch enabled|disabled] [--frequency HZ] "
                         "[output-directory] [input.wav ...]\n";
            return 0;
        }
        if (argument == "--candidate") {
            candidate_mode = true;
            ++next_argument;
            continue;
        }
        if (argument == "--frame-selection") {
            if (next_argument + 1 >= argc) {
                std::cerr << "--frame-selection requires uniform or salient\n";
                return 2;
            }
            const std::string_view value(argv[next_argument + 1]);
            if (value == "uniform") {
                requested_frame_selection = fim::dsp::FrameSelectionMode::kUniform;
            } else if (value == "salient") {
                requested_frame_selection = fim::dsp::FrameSelectionMode::kSalientWindow;
            } else {
                std::cerr << "--frame-selection requires uniform or salient\n";
                return 2;
            }
            next_argument += 2;
            continue;
        }
        if (argument == "--x-stretch") {
            if (next_argument + 1 >= argc) {
                std::cerr << "--x-stretch requires enabled or disabled\n";
                return 2;
            }
            const std::string_view value(argv[next_argument + 1]);
            if (value == "enabled") {
                requested_x_spectral_stretch = true;
            } else if (value == "disabled") {
                requested_x_spectral_stretch = false;
            } else {
                std::cerr << "--x-stretch requires enabled or disabled\n";
                return 2;
            }
            next_argument += 2;
            continue;
        }
        if (argument != "--frequency") {
            if (argument.starts_with("--")) {
                std::cerr << "Unknown option: " << argument << '\n';
                return 2;
            }
            break;
        }
        if (next_argument + 1 >= argc) {
            std::cerr << "--frequency requires a value in Hz\n";
            return 2;
        }
        char* parse_end = nullptr;
        preview_frequency = std::strtof(argv[next_argument + 1], &parse_end);
        if (parse_end == argv[next_argument + 1] || *parse_end != '\0' ||
            !std::isfinite(preview_frequency) || preview_frequency <= 0.0f ||
            preview_frequency > 20000.0f) {
            std::cerr << "Preview frequency must be a number between 0 and 20000 Hz\n";
            return 2;
        }
        next_argument += 2;
    }

    const fs::path output =
        next_argument < argc ? fs::path(argv[next_argument++]) : fs::path("evaluation-output");
    std::vector<fs::path> external_inputs;
    for (int i = next_argument; i < argc; ++i) {
        external_inputs.emplace_back(argv[i]);
    }
    const auto frame_selection = requested_frame_selection.value_or(
        candidate_mode ? fim::dsp::FrameSelectionMode::kSalientWindow
                       : fim::dsp::FrameSelectionMode::kUniform);
    const bool apply_x_spectral_stretch = requested_x_spectral_stretch.value_or(!candidate_mode);
    return Run(output, external_inputs, preview_frequency, frame_selection,
               apply_x_spectral_stretch);
}
