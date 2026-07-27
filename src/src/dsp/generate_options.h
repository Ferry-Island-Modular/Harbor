#pragma once

#include <cstdint>
#include <optional>

#include "dsp/source_frame_selector.h"
#include "dsp/spectral_modifier.h"

namespace fim::dsp {

enum class SourceMode {
    kFocused,
    kLegacyStretch,
};

// Options bundle passed from the UI/service layer to SingleWavGenerator.
// Default-constructed values use focused source selection: a salient source
// window with X dedicated to progression through spectrally spaced frames.
struct GenerateOptions {
    YMode y_mode = YMode::kTilt;
    ZMode z_mode = ZMode::kRandom;
    FrameSelectionMode frame_selection = FrameSelectionMode::kSalientWindow;
    bool apply_x_spectral_stretch = false;

    // Leave unset for fresh variations in the interactive app. Evaluation
    // tools set this so identical inputs and options produce identical banks.
    std::optional<std::uint32_t> random_seed;

    void SetSourceMode(SourceMode mode) {
        const bool legacy = mode == SourceMode::kLegacyStretch;
        frame_selection =
            legacy ? FrameSelectionMode::kUniform : FrameSelectionMode::kSalientWindow;
        apply_x_spectral_stretch = legacy;
    }
};

}  // namespace fim::dsp
