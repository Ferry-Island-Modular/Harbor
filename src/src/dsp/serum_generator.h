#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include "dsp/serum_morpher.h"

namespace fim::dsp {

// Options bundle for Serum generation. Default values are placeholders;
// SerumWavService sets them from the UI selections.
struct SerumGenerateOptions {
    SerumMode y_mode = SerumMode::kFormant;
    SerumMode z_mode = SerumMode::kPhase;
};

// Top-level Serum mode wavetable generator. Loads a Serum-format WAV,
// interpolates its frames down to 8 via frequency-domain linear
// interpolation, then runs the 8 pages × 64 cells generation loop
// applying Y then Z morphs to each cell. Writes 8 WAV files (1.wav..8.wav)
// into the output directory.
//
// Defaults match Python:
//   samples     = 2048 (per-cycle final length, fixed by Serum's format)
//   num_pages   = 8    (Z dimension, fixed by FourSeas hardware)
class SerumGenerator {
public:
    // Values outside the fixed Serum/Four Seas contract throw
    // std::invalid_argument.
    explicit SerumGenerator(std::size_t samples = 2048, std::size_t num_pages = 8);

    using ProgressCallback = std::function<void(int percent)>;

    // Run the full pipeline. Returns false on any I/O or DSP error.
    bool Generate(const std::filesystem::path& input_audio_path,
                  const std::filesystem::path& output_directory,
                  const SerumGenerateOptions& options = {},
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
