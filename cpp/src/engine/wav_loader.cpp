#include "engine/wav_loader.h"

#include "dr_wav.h"

namespace fim::engine {

std::optional<std::vector<float>> LoadWavMono(const std::string& path) {
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), nullptr)) {
        return std::nullopt;
    }

    const drwav_uint64 total_frames = wav.totalPCMFrameCount;
    const unsigned channels = wav.channels;

    // Read all frames as interleaved float, then deinterleave to mono.
    std::vector<float> interleaved(total_frames * channels);
    const drwav_uint64 frames_read =
        drwav_read_pcm_frames_f32(&wav, total_frames, interleaved.data());
    drwav_uninit(&wav);

    if (frames_read != total_frames) {
        return std::nullopt;
    }

    if (channels == 1) {
        return interleaved;
    }

    // Multi-channel: take the first channel only.
    std::vector<float> mono(total_frames);
    for (drwav_uint64 i = 0; i < total_frames; ++i) {
        mono[i] = interleaved[i * channels];
    }
    return mono;
}

}  // namespace fim::engine
