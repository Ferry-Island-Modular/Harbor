#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "dsp/resample.h"

using Catch::Matchers::WithinAbs;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
}

TEST_CASE("ResampleTo identity (same rate) returns the input", "[dsp][resample]") {
    std::vector<float> input(1024);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 10.0f * i / input.size());
    }

    const auto output = fim::dsp::ResampleTo(input, 44100, 44100);
    REQUIRE(output.size() == input.size());
    // Tolerance is loose because libsamplerate is not strictly an
    // identity for equal-rate conversions (it still runs through the
    // sinc filter).
    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE_THAT(output[i], WithinAbs(input[i], 0.01f));
    }
}

TEST_CASE("ResampleTo 2x upsample doubles the length", "[dsp][resample]") {
    std::vector<float> input(512);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 5.0f * i / input.size());
    }

    const auto output = fim::dsp::ResampleTo(input, 22050, 44100);
    // Allow a small tolerance on length — libsamplerate may produce
    // one or two frames off exactly 2x due to its transient handling.
    REQUIRE(output.size() >= input.size() * 2 - 2);
    REQUIRE(output.size() <= input.size() * 2 + 2);
}

TEST_CASE("ResampleTo 2x downsample halves the length", "[dsp][resample]") {
    std::vector<float> input(1024);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 5.0f * i / input.size());
    }

    const auto output = fim::dsp::ResampleTo(input, 88200, 44100);
    REQUIRE(output.size() >= input.size() / 2 - 2);
    REQUIRE(output.size() <= input.size() / 2 + 2);
}

TEST_CASE("ResampleTo handles non-integer ratio (48k -> 44.1k)", "[dsp][resample]") {
    // 48000 -> 44100 is the most common real-world resample (consumer
    // audio to CD rate). Ratio ~ 0.91875.
    std::vector<float> input(48000);  // 1 second at 48 kHz
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
    }

    const auto output = fim::dsp::ResampleTo(input, 48000, 44100);
    // Expect ~44100 output samples, +/- a few for transient handling.
    REQUIRE(output.size() >= 44090);
    REQUIRE(output.size() <= 44110);
}
