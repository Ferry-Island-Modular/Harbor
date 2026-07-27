#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "dsp/source_frame_selector.h"

TEST_CASE("Uniform source-frame selection preserves the legacy X mapping",
          "[dsp][source_frame_selector]") {
    const std::vector<std::vector<float>> magnitude(15, std::vector<float>(4, 1.0f));
    const auto selected =
        fim::dsp::SelectSourceFrames(magnitude, fim::dsp::FrameSelectionMode::kUniform, 8);

    REQUIRE(selected == std::vector<std::size_t>{0, 2, 4, 6, 8, 10, 12, 14});
}

TEST_CASE("Salient-window selection excludes silent frames around an active region",
          "[dsp][source_frame_selector]") {
    std::vector<std::vector<float>> magnitude(256, std::vector<float>(16, 0.0f));
    for (std::size_t frame = 100; frame < 120; ++frame) {
        for (std::size_t bin = 1; bin < 16; ++bin) {
            magnitude[frame][bin] = 0.2f + static_cast<float>((frame + bin) % 5) * 0.1f;
        }
    }

    const auto selected =
        fim::dsp::SelectSourceFrames(magnitude, fim::dsp::FrameSelectionMode::kSalientWindow, 8);

    REQUIRE(selected.size() == 8);
    for (std::size_t frame : selected) {
        REQUIRE(frame >= 100);
        REQUIRE(frame < 120);
    }
    REQUIRE(std::is_sorted(selected.begin(), selected.end()));
}

TEST_CASE("Salient-window selection falls back to uniform for silence",
          "[dsp][source_frame_selector]") {
    const std::vector<std::vector<float>> magnitude(15, std::vector<float>(4, 0.0f));
    const auto selected =
        fim::dsp::SelectSourceFrames(magnitude, fim::dsp::FrameSelectionMode::kSalientWindow, 8);

    REQUIRE(selected == std::vector<std::size_t>{0, 2, 4, 6, 8, 10, 12, 14});
}

TEST_CASE("Salient-window selection concentrates frames around spectral change",
          "[dsp][source_frame_selector]") {
    std::vector<std::vector<float>> magnitude(16, std::vector<float>(8, 0.0f));
    for (std::size_t frame = 0; frame < magnitude.size(); ++frame) {
        const std::size_t active_bin = frame < 12 ? 1 : frame - 10;
        magnitude[frame][active_bin] = 1.0f;
    }

    const auto selected =
        fim::dsp::SelectSourceFrames(magnitude, fim::dsp::FrameSelectionMode::kSalientWindow, 8);

    REQUIRE(selected.front() == 0);
    REQUIRE(selected.back() == 15);
    REQUIRE(std::is_sorted(selected.begin(), selected.end()));
    REQUIRE(std::adjacent_find(selected.begin(), selected.end()) == selected.end());
    REQUIRE(std::count_if(selected.begin(), selected.end(),
                          [](std::size_t frame) { return frame >= 11; }) >= 5);
}
