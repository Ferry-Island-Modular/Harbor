#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/fft_resampler.h"
#include "dsp/real_fft.h"

namespace fim::dsp {

// Extracts a single oversampled wavetable cycle from one STFT frame's
// magnitude + phase arrays. Direct port of the meaningful parts of Python's
// AudioResynthWavetableGenerator._extract_single_cycle, with the dead-code
// zero-crossing search removed and the length-2049 IFFT replaced by a
// standard length-fft_size IFFT.
//
// Pipeline:
//   1. Reconstruct complex bins from magnitude * exp(i * phase) for the
//      chosen frame.
//   2. Inverse FFT (length fft_size) → real signal of fft_size samples.
//   3. FftResample fft_size → target_length (typically 2048 → 8192 in the
//      Phase 3b wavetable use case).
//   4. Apply a precomputed Hann window of target_length to taper the edges.
//   5. Normalize to peak 1.0 if there's any non-zero content; otherwise
//      return zeros.
//
// Constructor parameters fix the inverse FFT and resample sizes; reuse a
// single CycleExtractor across many Extract calls to amortize setup cost.
class CycleExtractor {
public:
    CycleExtractor(std::size_t fft_size, std::size_t target_length);

    std::size_t fft_size() const { return fft_size_; }
    std::size_t target_length() const { return target_length_; }

    // Extract a cycle from the spectrum. magnitude and phase must be 2D
    // arrays shaped (num_bins x num_frames) with num_bins == fft_size/2 + 1.
    // frame_index selects which time slice of the STFT to use.
    std::vector<float> Extract(const std::vector<std::vector<float>>& magnitude,
                               const std::vector<std::vector<float>>& phase,
                               std::size_t frame_index) const;

private:
    std::size_t fft_size_;
    std::size_t target_length_;
    RealFft inverse_fft_;
    FftResampler resampler_;
    std::vector<float> window_;
};

}  // namespace fim::dsp
