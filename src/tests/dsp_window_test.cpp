#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

#include "dsp/window.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("HannWindow has the requested length", "[dsp][window]") {
    const auto w = fim::dsp::HannWindow(2048);
    REQUIRE(w.size() == 2048);
}

TEST_CASE("HannWindow endpoints are zero (symmetric definition)", "[dsp][window]") {
    const auto w = fim::dsp::HannWindow(2048);
    REQUIRE_THAT(w.front(), WithinAbs(0.0f, 1e-6));
    REQUIRE_THAT(w.back(), WithinAbs(0.0f, 1e-6));
}

TEST_CASE("HannWindow peak value matches scipy.signal.windows.hann(2048)", "[dsp][window]") {
    // scipy.signal.windows.hann(2048) peak is between index 1023 and 1024.
    // At index 1023 the value is 0.5 * (1 - cos(2*pi*1023/2047)) ≈ 0.9999988...
    const auto w = fim::dsp::HannWindow(2048);
    const float expected_1023 =
        0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * 1023.0f / 2047.0f));
    REQUIRE_THAT(w[1023], WithinAbs(expected_1023, 1e-6));
}

TEST_CASE("HannWindow length 1 is finite", "[dsp][window]") {
    // Degenerate case: N=1. (N-1)=0 in the denominator would divide by zero,
    // so the implementation must special-case it. We return 0.0 (vs scipy's
    // 1.0) — the choice is irrelevant in practice; this test just asserts
    // it doesn't crash or produce NaN.
    const auto w = fim::dsp::HannWindow(1);
    REQUIRE(w.size() == 1);
    REQUIRE(std::isfinite(w[0]));
}
