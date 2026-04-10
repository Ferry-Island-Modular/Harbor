#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

#include "dsp/post_effects.h"

using Catch::Matchers::WithinAbs;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;

std::vector<float> MakeSine(std::size_t n, float freq_cycles) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = std::sin(2.0f * kPi * freq_cycles * i / n);
    }
    return v;
}
}  // namespace

TEST_CASE("ZCrush amount=0 passes through unchanged", "[dsp][post_effects]") {
    auto input = MakeSine(256, 4.0f);
    const auto copy = input;
    fim::dsp::ZCrush(input, /*bit_depth=*/16, /*sample_hold=*/1);
    REQUIRE(input.size() == copy.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE_THAT(input[i], WithinAbs(copy[i], 1e-4));
    }
}

TEST_CASE("ZCrush reduces the number of distinct amplitude values", "[dsp][post_effects]") {
    auto input = MakeSine(256, 4.0f);
    fim::dsp::ZCrush(input, /*bit_depth=*/3, /*sample_hold=*/1);
    // 3 bits = 2^3 = 8 possible positive quantization levels + their
    // negatives. The output should have at most ~16 distinct values.
    std::set<float> distinct(input.begin(), input.end());
    REQUIRE(distinct.size() <= 16);
}

TEST_CASE("ZCrush sample-hold repeats each value `hold` times", "[dsp][post_effects]") {
    // Build a signal where every sample is the index — makes the hold
    // pattern easy to see.
    std::vector<float> input(16);
    for (std::size_t i = 0; i < 16; ++i) {
        input[i] = static_cast<float>(i) / 16.0f;
    }
    fim::dsp::ZCrush(input, /*bit_depth=*/16, /*sample_hold=*/4);
    // Samples 0..3 should all equal input[0], 4..7 should all equal
    // input[4], etc.
    REQUIRE_THAT(input[0], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[1], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[2], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[3], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[4], WithinAbs(4.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[8], WithinAbs(8.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[12], WithinAbs(12.0f / 16.0f, 1e-6));
}

TEST_CASE("ZCrushAmount maps z=0 to identity and z>0 to progressively harsher",
          "[dsp][post_effects]") {
    const auto a0 = fim::dsp::ZCrushAmount(0);
    REQUIRE(a0.bit_depth >= 16);  // effectively full-resolution
    REQUIRE(a0.sample_hold == 1);

    const auto a7 = fim::dsp::ZCrushAmount(7);
    REQUIRE(a7.bit_depth < a0.bit_depth);
    REQUIRE(a7.sample_hold > a0.sample_hold);
}
