#include "engine/wavetable_bank.h"

#include <filesystem>
#include <iostream>

#include "engine/wav_loader.h"

namespace fim::engine {

std::unique_ptr<WavetableBank> WavetableBank::Load(const std::string& bank_directory) {
    namespace fs = std::filesystem;
    fs::path dir(bank_directory);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return nullptr;
    }

    auto bank = std::unique_ptr<WavetableBank>(new WavetableBank());
    bank->samples_.assign(kTotalWaves * kWaveStride, 0.0f);
    bank->wave_ptrs_.resize(kTotalWaves);
    for (size_t i = 0; i < kTotalWaves; ++i) {
        bank->wave_ptrs_[i] = bank->samples_.data() + i * kWaveStride;
    }

    constexpr size_t kSamplesPerPage = kWavetableSize * 64;  // 64 waves per page

    for (size_t z = 0; z < kNumWavesZ; ++z) {
        const fs::path page_path = dir / (std::to_string(z + 1) + ".wav");

        auto page_samples = LoadWavMono(page_path.string());
        if (!page_samples.has_value()) {
            std::cerr << "WavetableBank::Load: failed to read " << page_path << "\n";
            return nullptr;
        }
        if (page_samples->size() != kSamplesPerPage) {
            std::cerr << "WavetableBank::Load: " << page_path << " has " << page_samples->size()
                      << " samples; expected " << kSamplesPerPage << "\n";
            return nullptr;
        }

        for (size_t y = 0; y < kNumWavesY; ++y) {
            for (size_t x = 0; x < kNumWavesX; ++x) {
                const size_t wave_idx = x + y * kNumWavesX + z * (kNumWavesX * kNumWavesY);
                const size_t src_offset = (y * kNumWavesX + x) * kWavetableSize;

                float* dst = bank->wave_ptrs_[wave_idx];
                for (size_t i = 0; i < kWavetableSize; ++i) {
                    dst[i] = (*page_samples)[src_offset + i];
                }
                // Wraparound sample for the FourSeas oscillator's interpolation.
                dst[kWavetableSize] = dst[0];
            }
        }
    }

    return bank;
}

}  // namespace fim::engine
