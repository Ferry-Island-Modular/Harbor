#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include "dsp/generate_options.h"

namespace fim::dsp {

// Top-level single-WAV wavetable generator. Loads an input audio file,
// runs STFT analysis, loops over 8 Z pages × 64 (X, Y) cells per page
// (the FourSeas wavetable grid), downsamples each oversampled cell to the
// final wavetable length, and writes 8 WAV files (1.wav .. 8.wav) into
// the output directory.
//
// This class replaces the Phase 2 fim::app::StubBankWriter as the work
// performed by fim::app::SingleWavService::Generate().
//
// Defaults match the Python AudioResynthWavetableGenerator:
//   samples            = 2048   (per-cycle final length)
//   oversample_factor  = 4      (internal oversampling ratio)
//   num_pages          = 8      (Z dimension, fixed by hardware)
class SingleWavGenerator {
public:
    // Values outside the fixed Four Seas contract throw std::invalid_argument.
    explicit SingleWavGenerator(std::size_t samples = 2048, std::size_t oversample_factor = 4,
                                std::size_t num_pages = 8);

    // Progress callback type. Called from the same thread that invoked
    // Generate(); the caller is responsible for marshalling to a UI thread
    // if needed.
    using ProgressCallback = std::function<void(int percent)>;

    // Run the full pipeline. The options parameter selects the Y/Z morph
    // modes; defaults reproduce Phase 3b behavior (tilt + phase randomize).
    // Returns false on any I/O or DSP error. The output directory is
    // created if it doesn't exist.
    bool Generate(const std::filesystem::path& input_audio_path,
                  const std::filesystem::path& output_directory,
                  const GenerateOptions& options = {},
                  const ProgressCallback& on_progress = {}) const;

    std::size_t samples() const { return samples_; }
    std::size_t oversample_factor() const { return oversample_factor_; }
    std::size_t n_samples() const { return n_samples_; }
    std::size_t num_pages() const { return num_pages_; }

private:
    bool WritePageToWav(const std::filesystem::path& path,
                        const std::vector<float>& downsampled_page) const;

    std::size_t samples_;
    std::size_t oversample_factor_;
    std::size_t n_samples_;  // = samples * oversample_factor
    std::size_t num_pages_;
};

}  // namespace fim::dsp
