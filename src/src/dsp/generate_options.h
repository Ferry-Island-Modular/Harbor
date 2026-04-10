#pragma once

#include "dsp/spectral_modifier.h"

namespace fim::dsp {

// Options bundle passed from the UI/service layer to SingleWavGenerator.
// Default-constructed values reproduce Phase 3b behavior: tilt + phase
// randomize, no post-effects.
struct GenerateOptions {
    YMode y_mode = YMode::kTilt;
    ZMode z_mode = ZMode::kRandom;
};

}  // namespace fim::dsp
