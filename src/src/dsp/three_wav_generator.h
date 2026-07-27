#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace fim::dsp {

enum class ThreeWavZMode {
    kPhase,
    kOddEven,
    kCrush,
};

struct ThreeWavGenerateOptions {
    ThreeWavZMode z_mode = ThreeWavZMode::kPhase;
    std::optional<std::uint32_t> random_seed = 0xF04CEAu;
};

struct ThreeWavWeights {
    float a;
    float b;
    float c;
};

// X crossfades A->B and Y pulls that result strongly toward C. A small A/B
// contribution remains at maximum Y so the top grid row retains X motion.
// Z is left free for an independent texture axis.
ThreeWavWeights ThreeWavBarycentricWeights(float x_amount, float y_amount);

// Three-wav mode wavetable generator. Each file contributes a time-averaged
// magnitude spectrum. X crossfades A->B, Y pulls toward C, and Z applies a
// coherent texture transformation.
//
// Input files are silently resampled to 44.1 kHz if needed — users
// shouldn't have to think about sample rates.
//
// This is Phase 4c's "Option D" — see the project memory note
// reference_three_wav_approaches.md for the enumerated list of
// alternative algorithms we considered.
class ThreeWavGenerator {
public:
    // Values outside the fixed Four Seas contract throw std::invalid_argument.
    explicit ThreeWavGenerator(std::size_t samples = 2048, std::size_t num_pages = 8);

    using ProgressCallback = std::function<void(int percent)>;

    // Run the full pipeline. Returns false on any I/O or DSP error.
    // The 3 input paths are required; all must be non-empty and loadable.
    bool Generate(const std::array<std::filesystem::path, 3>& input_paths,
                  const std::filesystem::path& output_directory,
                  const ProgressCallback& on_progress = {}) const;
    bool Generate(const std::array<std::filesystem::path, 3>& input_paths,
                  const std::filesystem::path& output_directory,
                  const ThreeWavGenerateOptions& options,
                  const ProgressCallback& on_progress = {}) const;

    std::size_t samples() const { return samples_; }
    std::size_t num_pages() const { return num_pages_; }

private:
    bool WritePageToWav(const std::filesystem::path& path,
                        const std::vector<float>& page_samples) const;

    std::size_t samples_;
    std::size_t num_pages_;
};

}  // namespace fim::dsp
