#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fim::dsp {

// One-shot sample rate conversion using libsamplerate's SRC_SINC_MEDIUM_
// QUALITY converter. Appropriate for file loading (non-streaming,
// non-realtime). For streaming use, wrap libsamplerate's state-based API
// directly.
//
// If input_rate == target_rate, returns a copy of the input (still
// routed through libsamplerate but effectively identity).
//
// Returns an empty vector if libsamplerate fails internally (rare —
// usually only on allocation failure).
std::vector<float> ResampleTo(const std::vector<float>& input, std::uint32_t input_rate,
                              std::uint32_t target_rate);

}  // namespace fim::dsp
