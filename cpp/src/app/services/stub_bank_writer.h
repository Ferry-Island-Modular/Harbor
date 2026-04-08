#pragma once

#include <filesystem>

namespace fim::app {

// Phase 2 placeholder bank writer. Writes 8 wavetable WAV files (1.wav..8.wav)
// to `output_directory`. Each file is the standard FourSeas page format:
// 64 waves * 2048 samples per wave = 131072 mono float samples, written as
// 16-bit PCM at 44.1kHz. The samples are sine waves at 8 distinct frequencies
// so the inline preview has something audibly different to play between pages.
//
// Returns false if the directory cannot be created or any file fails to write.
//
// This class exists ONLY for Phase 2 — Phase 3 replaces the call site with
// real DSP. It has no DSP logic of its own beyond generating sine waves.
class StubBankWriter {
public:
    static bool WriteSineBank(const std::filesystem::path& output_directory);
};

}  // namespace fim::app
