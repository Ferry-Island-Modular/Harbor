#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace fim::dsp {

// A loaded mono audio buffer plus metadata about the source file. Samples
// are always float in [-1, 1] (pre-normalize) or scaled to [-1, 1] peak if
// normalization was requested.
struct LoadedAudio {
    std::vector<float> samples;           // mono
    std::uint32_t sample_rate = 0;        // file's native rate, unresampled
    std::uint32_t original_channels = 0;  // channels in the source file
};

// Load a WAV file from disk via dr_wav. If the source is multichannel, all
// channels are averaged into a single mono channel. If `normalize` is true,
// the samples are scaled so the maximum absolute value is exactly 1.0 (or
// left at zero if the file is silent). The file's native sample rate is
// preserved — no resampling is performed.
//
// Returns std::nullopt if the file cannot be opened or parsed.
//
// This loader is a DSP-layer input loader. It is distinct from the engine
// layer's `fim::engine::LoadWavMono` (which reads previously-generated
// output banks for playback) and returns richer metadata.
std::optional<LoadedAudio> LoadWav(const std::filesystem::path& path, bool normalize = true);

}  // namespace fim::dsp
