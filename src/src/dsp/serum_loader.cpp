#include "dsp/serum_loader.h"

#include <cstddef>

#include "dsp/wav_loader.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kSerumFrameSize = 2048;

}  // namespace

std::optional<SerumFrames> SerumLoader::Load(const std::filesystem::path& path) {
    // Use the existing WAV loader to handle dr_wav invocation, stereo
    // mixdown, and float conversion. Don't normalize — Serum files are
    // already normalized by convention, and normalizing across the whole
    // file would be wrong if some frames are quieter than others.
    auto loaded = LoadWav(path, /*normalize=*/false);
    if (!loaded.has_value()) {
        return std::nullopt;
    }
    if (loaded->samples.empty()) {
        return std::nullopt;
    }
    if (loaded->samples.size() % kSerumFrameSize != 0) {
        return std::nullopt;
    }

    const std::size_t num_frames = loaded->samples.size() / kSerumFrameSize;
    if (num_frames == 0) {
        return std::nullopt;
    }

    SerumFrames result;
    result.source_sample_rate = loaded->sample_rate;
    result.frames.resize(num_frames);
    for (std::size_t f = 0; f < num_frames; ++f) {
        result.frames[f].resize(kSerumFrameSize);
        const std::size_t start = f * kSerumFrameSize;
        // Copy.
        for (std::size_t i = 0; i < kSerumFrameSize; ++i) {
            result.frames[f][i] = loaded->samples[start + i];
        }
        // Remove DC per frame.
        float sum = 0.0f;
        for (float s : result.frames[f]) {
            sum += s;
        }
        const float dc = sum / static_cast<float>(kSerumFrameSize);
        for (float& s : result.frames[f]) {
            s -= dc;
        }
    }

    return result;
}

}  // namespace fim::dsp
