#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <stdexcept>

#include "dsp/generate_options.h"
#include "dsp/serum_generator.h"
#include "dsp/single_wav_generator.h"
#include "dsp/three_wav_generator.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("Wavetable generators accept the fixed Four Seas dimensions", "[dsp][generator]") {
    REQUIRE_NOTHROW(fim::dsp::SingleWavGenerator{});
    REQUIRE_NOTHROW(fim::dsp::SerumGenerator{});
    REQUIRE_NOTHROW(fim::dsp::ThreeWavGenerator{});
}

TEST_CASE("Three-wav barycentric mapping reaches A and B and keeps X alive near C",
          "[dsp][three_wav]") {
    const auto source_a = fim::dsp::ThreeWavBarycentricWeights(0.0f, 0.0f);
    REQUIRE_THAT(source_a.a, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(source_a.b, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(source_a.c, WithinAbs(0.0f, 1e-6f));

    const auto source_b = fim::dsp::ThreeWavBarycentricWeights(1.0f, 0.0f);
    REQUIRE_THAT(source_b.a, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(source_b.b, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(source_b.c, WithinAbs(0.0f, 1e-6f));

    const auto near_c_left = fim::dsp::ThreeWavBarycentricWeights(0.0f, 1.0f);
    const auto near_c_right = fim::dsp::ThreeWavBarycentricWeights(1.0f, 1.0f);
    REQUIRE_THAT(near_c_left.c, WithinAbs(0.875f, 1e-6f));
    REQUIRE_THAT(near_c_left.a, WithinAbs(0.125f, 1e-6f));
    REQUIRE_THAT(near_c_right.b, WithinAbs(0.125f, 1e-6f));

    const auto interior = fim::dsp::ThreeWavBarycentricWeights(0.25f, 0.4f);
    REQUIRE_THAT(interior.a + interior.b + interior.c, WithinAbs(1.0f, 1e-6f));
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

TEST_CASE("Default generation options use focused source mapping", "[dsp][generator]") {
    fim::dsp::GenerateOptions options;

    REQUIRE(options.frame_selection == fim::dsp::FrameSelectionMode::kSalientWindow);
    REQUIRE_FALSE(options.apply_x_spectral_stretch);
    REQUIRE(options.coherent_phase_randomization);

    options.SetSourceMode(fim::dsp::SourceMode::kLegacyStretch);
    REQUIRE(options.frame_selection == fim::dsp::FrameSelectionMode::kUniform);
    REQUIRE(options.apply_x_spectral_stretch);
    REQUIRE_FALSE(options.coherent_phase_randomization);

    options.SetSourceMode(fim::dsp::SourceMode::kFocused);
    REQUIRE(options.frame_selection == fim::dsp::FrameSelectionMode::kSalientWindow);
    REQUIRE_FALSE(options.apply_x_spectral_stretch);
    REQUIRE(options.coherent_phase_randomization);
}
