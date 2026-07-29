#pragma once

#include <cstddef>
#include <vector>

namespace fim::dsp {

enum class FrameSelectionMode {
    kUniform,
    kSalientWindow,
};

// Select monotonically ordered STFT frame indices for the X axis. Magnitudes
// are frame-major: magnitude[frame][bin]. Uniform preserves the legacy mapping;
// SalientWindow finds an energetic, spectrally active region and distributes
// X positions through its non-silent frames by accumulated spectral change.
std::vector<std::size_t> SelectSourceFrames(const std::vector<std::vector<float>>& magnitude,
                                            FrameSelectionMode mode, std::size_t output_count = 8);

}  // namespace fim::dsp
