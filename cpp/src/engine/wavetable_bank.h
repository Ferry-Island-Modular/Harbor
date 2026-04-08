#pragma once

#include <memory>
#include <string>
#include <vector>

namespace fim::engine {

// A loaded FourSeas wavetable bank: 8 pages × 64 waves per page × 2048 samples
// per wave = 131072 samples per page = 1048576 samples total. Each wave has an
// extra wraparound sample (2049 floats per wave) so the FourSeas oscillator's
// linear interpolation can read past the end without a modulo. Storage is one
// contiguous std::vector with a parallel std::vector<float*> of pointers into
// it (matching the float** API the oscillator expects).
//
// Immutable after construction. Loading a new bank produces a new instance;
// the old one is released only when the audio thread is no longer using it.
class WavetableBank {
public:
    static constexpr size_t kWavetableSize = 2048;
    static constexpr size_t kNumWavesX = 8;
    static constexpr size_t kNumWavesY = 8;
    static constexpr size_t kNumWavesZ = 8;
    static constexpr size_t kTotalWaves = kNumWavesX * kNumWavesY * kNumWavesZ;
    static constexpr size_t kWaveStride = kWavetableSize + 1;  // +1 for wraparound

    // Loads `<bank_directory>/{1..8}.wav` and returns a fully-populated bank.
    // Returns nullptr if the directory doesn't exist, any page is missing, or
    // any page has an unexpected sample count.
    static std::unique_ptr<WavetableBank> Load(const std::string& bank_directory);

    // Pointer table for fourseas::WavetableOscillator::Init(float**). Stable
    // for the lifetime of this bank instance.
    float** wavetable_pointers() { return wave_ptrs_.data(); }
    const float* const* wavetable_pointers() const { return wave_ptrs_.data(); }

    static constexpr size_t total_waves() { return kTotalWaves; }
    static constexpr size_t wavetable_size() { return kWavetableSize; }

private:
    WavetableBank() = default;

    std::vector<float> samples_;     // size: kTotalWaves * kWaveStride
    std::vector<float*> wave_ptrs_;  // size: kTotalWaves; each points into samples_
};

}  // namespace fim::engine
