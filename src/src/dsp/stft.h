#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/real_fft.h"

namespace fim::dsp {

// Short-Time Fourier Transform of a mono audio buffer using a symmetric
// Hann window.
//
// Matches Python's:
//   freqs, times, Zxx = scipy.signal.stft(
//       audio, nperseg=fft_size, noverlap=fft_size - hop_size,
//       window=scipy.signal.windows.hann(fft_size),
//       boundary=None, padded=False)
//
// Boundary and padding are intentionally disabled here: there is no zero-
// padding at either end of the audio, and the first frame begins at sample 0.
// Frame count formula: 0 if audio has fewer than fft_size samples, otherwise
// floor((n_samples - fft_size) / hop_size) + 1.
//
// Construction allocates a RealFft setup and precomputes the Hann window, so
// reuse a single Stft across many Analyze calls when possible.
class Stft {
public:
    Stft(std::size_t fft_size, std::size_t hop_size);

    std::size_t fft_size() const { return fft_size_; }
    std::size_t hop_size() const { return hop_size_; }
    std::size_t num_bins() const { return fft_size_ / 2 + 1; }

    // Number of frames that will be produced for an input of `n_samples`
    // samples. Useful for pre-allocating.
    std::size_t NumFrames(std::size_t n_samples) const;

    // Perform the STFT. Returns a vector of frames; each frame is a vector
    // of num_bins() complex bins in standard order (DC, positive freqs,
    // Nyquist). Empty if the input is shorter than fft_size.
    std::vector<std::vector<std::complex<float>>> Analyze(const std::vector<float>& audio) const;

private:
    std::size_t fft_size_;
    std::size_t hop_size_;
    std::vector<float> window_;
    RealFft fft_;
};

// Compute the bin-wise magnitudes of a 2D complex bin array. Output shape
// matches the input.
std::vector<std::vector<float>> Magnitude(
    const std::vector<std::vector<std::complex<float>>>& bins);

// Compute the bin-wise phases (atan2) of a 2D complex bin array. Output
// shape matches the input.
std::vector<std::vector<float>> Phase(const std::vector<std::vector<std::complex<float>>>& bins);

}  // namespace fim::dsp
