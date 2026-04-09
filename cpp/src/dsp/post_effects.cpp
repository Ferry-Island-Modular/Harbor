#include "dsp/post_effects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fim::dsp {

void ZCrush(std::vector<float>& cycle, int bit_depth, int sample_hold) {
    if (cycle.empty()) {
        return;
    }

    // Step 1: bit-depth quantization. A signed fixed-point with
    // bit_depth bits represents 2^(bit_depth-1) positive levels and
    // 2^(bit_depth-1) negative levels. We round to the nearest level.
    if (bit_depth < 16) {
        const int levels = 1 << (bit_depth - 1);  // e.g. bit_depth=3 -> 4
        const float step = 1.0f / static_cast<float>(levels);
        for (float& s : cycle) {
            const float clamped = std::max(-1.0f, std::min(1.0f, s));
            s = std::round(clamped / step) * step;
        }
    }

    // Step 2: sample-rate reduction via sample-and-hold. Every group of
    // sample_hold samples takes the value of the first sample in the
    // group.
    if (sample_hold > 1) {
        for (std::size_t i = 0; i < cycle.size(); i += sample_hold) {
            const float held = cycle[i];
            const std::size_t end =
                std::min(i + static_cast<std::size_t>(sample_hold), cycle.size());
            for (std::size_t j = i + 1; j < end; ++j) {
                cycle[j] = held;
            }
        }
    }
}

ZCrushParams ZCrushAmount(int z) {
    // z=0: bit_depth=16, hold=1 (passthrough)
    // z=7: bit_depth=3, hold=8 (maximal crush)
    const int clamped = std::max(0, std::min(7, z));
    const float t = static_cast<float>(clamped) / 7.0f;
    const int bit_depth = static_cast<int>(std::round(16.0f - t * 13.0f));
    const int sample_hold = 1 + static_cast<int>(std::round(t * 7.0f));
    return ZCrushParams{bit_depth, sample_hold};
}

}  // namespace fim::dsp
