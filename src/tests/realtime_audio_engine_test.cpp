#include "engine/realtime_audio_engine.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using fim::engine::PlayMode;
using fim::engine::RealtimeAudioEngine;

TEST_CASE("MidiToFrequency converts A4 (note 69) to 440 Hz", "[realtime]") {
    REQUIRE(std::abs(RealtimeAudioEngine::MidiToFrequency(69) - 440.0f) < 0.01f);
    REQUIRE(std::abs(RealtimeAudioEngine::MidiToFrequency(60) - 261.626f) < 0.5f);
    REQUIRE(std::abs(RealtimeAudioEngine::MidiToFrequency(81) - 880.0f) < 0.5f);
}

TEST_CASE("Sweep position progresses linearly with time", "[realtime]") {
    auto pos = RealtimeAudioEngine::SweepPositionAt(
        /*elapsed=*/0.5f, /*duration=*/1.0f,
        /*target_x=*/4.0f, /*target_y=*/2.0f, /*target_z=*/6.0f);
    REQUIRE(std::abs(pos.x - 2.0f) < 1e-4f);
    REQUIRE(std::abs(pos.y - 1.0f) < 1e-4f);
    REQUIRE(std::abs(pos.z - 3.0f) < 1e-4f);
}

TEST_CASE("Sweep position clamps to target at end", "[realtime]") {
    auto pos = RealtimeAudioEngine::SweepPositionAt(
        /*elapsed=*/2.0f, /*duration=*/1.0f,
        /*target_x=*/4.0f, /*target_y=*/2.0f, /*target_z=*/6.0f);
    REQUIRE(pos.x == 4.0f);
    REQUIRE(pos.y == 2.0f);
    REQUIRE(pos.z == 6.0f);
}

TEST_CASE("Arpeggio note index advances with elapsed time", "[realtime]") {
    // ARPEGGIO_NOTE_DURATION is 0.25s. After 0.6s elapsed, index should be 2.
    const int idx = RealtimeAudioEngine::ArpeggioIndexAt(/*elapsed=*/0.6f);
    REQUIRE(idx == 2);
}

TEST_CASE("Arpeggio interval table is the major-triad up-down pattern", "[realtime]") {
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[0] == 0);   // root
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[1] == 4);   // major 3rd
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[2] == 7);   // 5th
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[3] == 12);  // octave
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[4] == 7);   // back down
    REQUIRE(RealtimeAudioEngine::kArpeggioIntervals[5] == 4);
}
