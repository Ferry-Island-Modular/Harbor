#pragma once

#include <cstdint>
#include <optional>

#include "dsp/source_frame_selector.h"
#include "dsp/spectral_modifier.h"

namespace fim::dsp {

// Options bundle passed from the UI/service layer to SingleWavGenerator.
// Default-constructed values reproduce Phase 3b behavior: tilt + phase
// randomize, no post-effects.
struct GenerateOptions {
    YMode y_mode = YMode::kTilt;
    ZMode z_mode = ZMode::kRandom;
    FrameSelectionMode frame_selection = FrameSelectionMode::kUniform;
    bool apply_x_spectral_stretch = true;

    // Leave unset for fresh variations in the interactive app. Evaluation
    // tools set this so identical inputs and options produce identical banks.
    std::optional<std::uint32_t> random_seed;
};

}  // namespace fim::dsp
