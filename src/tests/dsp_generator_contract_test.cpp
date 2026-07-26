#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "dsp/generate_options.h"
#include "dsp/serum_generator.h"
#include "dsp/single_wav_generator.h"
#include "dsp/three_wav_generator.h"

TEST_CASE("Wavetable generators accept the fixed Four Seas dimensions", "[dsp][generator]") {
    REQUIRE_NOTHROW(fim::dsp::SingleWavGenerator{});
    REQUIRE_NOTHROW(fim::dsp::SerumGenerator{});
    REQUIRE_NOTHROW(fim::dsp::ThreeWavGenerator{});
}

TEST_CASE("Wavetable generators reject unsupported output dimensions", "[dsp][generator]") {
    REQUIRE_THROWS_AS(fim::dsp::SingleWavGenerator(256, 4, 8), std::invalid_argument);
    REQUIRE_THROWS_AS(fim::dsp::SingleWavGenerator(2048, 2, 8), std::invalid_argument);
    REQUIRE_THROWS_AS(fim::dsp::SingleWavGenerator(2048, 4, 4), std::invalid_argument);
    REQUIRE_THROWS_AS(fim::dsp::SerumGenerator(256, 8), std::invalid_argument);
    REQUIRE_THROWS_AS(fim::dsp::SerumGenerator(2048, 4), std::invalid_argument);
    REQUIRE_THROWS_AS(fim::dsp::ThreeWavGenerator(256, 8), std::invalid_argument);
    REQUIRE_THROWS_AS(fim::dsp::ThreeWavGenerator(2048, 4), std::invalid_argument);
}

TEST_CASE("Default generation options preserve legacy source mapping", "[dsp][generator]") {
    const fim::dsp::GenerateOptions options;

    REQUIRE(options.frame_selection == fim::dsp::FrameSelectionMode::kUniform);
    REQUIRE(options.apply_x_spectral_stretch);
}
