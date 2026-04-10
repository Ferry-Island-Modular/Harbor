#include "dsp/resample.h"

#include <samplerate.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace fim::dsp {

std::vector<float> ResampleTo(const std::vector<float>& input, std::uint32_t input_rate,
                              std::uint32_t target_rate) {
    if (input.empty() || input_rate == 0 || target_rate == 0) {
        return {};
    }

    const double ratio = static_cast<double>(target_rate) / static_cast<double>(input_rate);

    // Allocate output with a small headroom beyond ceil(input_length *
    // ratio) — libsamplerate can produce one or two extra frames at the
    // transient.
    const std::size_t expected =
        static_cast<std::size_t>(std::ceil(static_cast<double>(input.size()) * ratio));
    std::vector<float> output(expected + 8, 0.0f);

    SRC_DATA data = {};
    data.data_in = input.data();
    data.input_frames = static_cast<long>(input.size());
    data.data_out = output.data();
    data.output_frames = static_cast<long>(output.size());
    data.src_ratio = ratio;
    data.end_of_input = 1;

    const int err = src_simple(&data, SRC_SINC_MEDIUM_QUALITY, /*channels=*/1);
    if (err != 0) {
        return {};
    }

    // Trim to the actual number of frames libsamplerate produced.
    output.resize(static_cast<std::size_t>(data.output_frames_gen));
    return output;
}

}  // namespace fim::dsp
