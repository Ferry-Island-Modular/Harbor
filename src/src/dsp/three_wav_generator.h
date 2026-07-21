#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

namespace fim::dsp {

// Three-wav mode wavetable generator. Takes 3 arbitrary .wav files, one
// per axis, and builds an 8x8x8 wavetable grid where each cell's
// spectrum is a weighted sum of the 3 files' time-averaged magnitudes,
// with weights proportional to the cell's (X, Y, Z) position. Zero-
// phase inverse FFT produces the output waveform.
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

    std::size_t samples() const { return samples_; }
    std::size_t num_pages() const { return num_pages_; }

private:
    bool WritePageToWav(const std::filesystem::path& path,
                        const std::vector<float>& page_samples) const;

    std::size_t samples_;
    std::size_t num_pages_;
};

}  // namespace fim::dsp
