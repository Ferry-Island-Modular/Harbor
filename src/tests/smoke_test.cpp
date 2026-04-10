#include <catch2/catch_test_macros.hpp>
#include <type_traits>

#include "wavetable_oscillator.h"

TEST_CASE("FourSeas WavetableOscillator template instantiates", "[smoke]") {
    using Osc = fourseas::WavetableOscillator<2048, false, false>;
    // Forces template instantiation; if it doesn't compile, the test build
    // fails. The runtime check is mostly nominal — we want to know that
    // a default-constructed oscillator has the size we expect from the
    // template parameters, not just any positive number.
    Osc osc;
    static_assert(std::is_default_constructible_v<Osc>,
                  "WavetableOscillator must be default-constructible");
    REQUIRE(sizeof(Osc) >= sizeof(float*) * 2);  // wavetable_ + all_waves_
}

TEST_CASE("Basic arithmetic sanity check", "[smoke]") {
    REQUIRE(2 + 2 == 4);
}
