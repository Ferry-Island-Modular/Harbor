#include "dsp/cycle_extractor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>

#include "dsp/window.h"

namespace fim::dsp {

CycleExtractor::CycleExtractor(std::size_t fft_size, std::size_t target_length)
    : fft_size_(fft_size),
      target_length_(target_length),
      inverse_fft_(fft_size),
      resampler_(fft_size, target_length),
      window_(HannWindow(target_length)) {}

std::vector<float> CycleExtractor::Extract(const std::vector<std::vector<float>>& magnitude,
                                           const std::vector<std::vector<float>>& phase,
                                           std::size_t frame_index) const {
    const std::size_t num_bins = inverse_fft_.num_bins();
    assert(magnitude.size() == num_bins);
    assert(phase.size() == num_bins);

    // Step 1: reconstruct complex bins from mag/phase for the chosen frame.
    std::vector<std::complex<float>> bins(num_bins);
    for (std::size_t k = 0; k < num_bins; ++k) {
        const float mag = magnitude[k][frame_index];
        const float ph = phase[k][frame_index];
        bins[k] = std::complex<float>(mag * std::cos(ph), mag * std::sin(ph));
    }

    // Step 2: inverse FFT to time domain. Output length = fft_size.
    std::vector<float> time_signal(fft_size_);
    inverse_fft_.Inverse(bins.data(), time_signal.data());

    // Step 3: resample fft_size → target_length.
    std::vector<float> cycle = resampler_.Resample(time_signal);

    // Step 4: apply the Hann window to taper edges.
    for (std::size_t i = 0; i < target_length_; ++i) {
        cycle[i] *= window_[i];
    }

    // Step 5: normalize to peak 1.0 (or leave as zeros if silent).
    float peak = 0.0f;
    for (float s : cycle) {
        peak = std::max(peak, std::abs(s));
    }
    if (peak > 0.0f) {
        const float inv_peak = 1.0f / peak;
        for (float& s : cycle) {
            s *= inv_peak;
        }
    }

    return cycle;
}

}  // namespace fim::dsp
