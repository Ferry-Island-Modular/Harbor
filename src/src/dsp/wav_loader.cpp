#include "dsp/wav_loader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "dr_wav.h"

namespace fim::dsp {

std::optional<LoadedAudio> LoadWav(const std::filesystem::path& path, bool normalize) {
    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drwav_uint64 total_frames = 0;
    float* raw = drwav_open_file_and_read_pcm_frames_f32(path.string().c_str(), &channels,
                                                         &sample_rate, &total_frames, nullptr);
    if (raw == nullptr) {
        return std::nullopt;
    }

    LoadedAudio result;
    result.sample_rate = sample_rate;
    result.original_channels = channels;
    result.samples.resize(static_cast<std::size_t>(total_frames));

    // Mix down to mono by averaging all channels.
    for (drwav_uint64 frame = 0; frame < total_frames; ++frame) {
        float sum = 0.0f;
        for (unsigned int ch = 0; ch < channels; ++ch) {
            sum += raw[frame * channels + ch];
        }
        result.samples[static_cast<std::size_t>(frame)] = sum / static_cast<float>(channels);
    }

    drwav_free(raw, nullptr);

    if (normalize && !result.samples.empty()) {
        float peak = 0.0f;
        for (float s : result.samples) {
            peak = std::max(peak, std::abs(s));
        }
        if (peak > 0.0f) {
            const float scale = 1.0f / peak;
            for (float& s : result.samples) {
                s *= scale;
            }
        }
    }

    return result;
}

}  // namespace fim::dsp
