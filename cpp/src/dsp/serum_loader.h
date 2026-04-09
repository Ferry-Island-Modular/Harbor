#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace fim::dsp {

// Result of loading a Serum-format wavetable file. Each frame is exactly
// 2048 samples (Serum's fixed frame size), mono, float in [-1, 1] with
// per-frame DC removed.
struct SerumFrames {
    std::vector<std::vector<float>> frames;  // [frame_index][sample_index]
    std::uint32_t source_sample_rate = 0;
};

// Loader for Serum-format wavetable files. Serum stores a wavetable as
// a sequence of `N × 2048` single-cycle waveforms concatenated in a WAV
// file. There's no explicit frame count marker — the loader infers it
// from the total sample count.
class SerumLoader {
public:
    // Load and split a Serum WAV file into 2048-sample frames. Returns
    // std::nullopt if the file can't be loaded or its sample count isn't
    // a multiple of 2048. Stereo files are mixed to mono before framing.
    // DC offset is removed from each frame individually.
    static std::optional<SerumFrames> Load(const std::filesystem::path& path);
};

}  // namespace fim::dsp
