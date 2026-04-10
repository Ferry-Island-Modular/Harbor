#pragma once

#include <complex>
#include <cstddef>
#include <memory>

namespace fim::dsp {

// Real-input FFT backed by PFFFT. A single instance is tied to one transform
// size; construct once and reuse across many transforms to amortize PFFFT's
// setup cost.
//
// Conventions:
// - `Forward` takes `fft_size()` real samples and produces `num_bins()`
//   complex bins in standard order (DC at index 0, Nyquist at fft_size/2,
//   positive frequencies in between).
// - `Inverse` takes `num_bins()` complex bins in the same standard order and
//   produces `fft_size()` real samples. The result is NOT scaled by 1/N.
//   Callers that want the true mathematical inverse must divide by fft_size.
// - The caller's buffers may be any alignment — internal PFFFT calls go
//   through aligned scratch buffers owned by this class.
//
// PFFFT supports real FFT sizes that are multiples of 32 with factors only
// {2, 3, 5}. 2048 and 64 (used by tests) are both supported.
class RealFft {
public:
    explicit RealFft(std::size_t fft_size);
    ~RealFft();

    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;
    RealFft(RealFft&&) = delete;
    RealFft& operator=(RealFft&&) = delete;

    std::size_t fft_size() const { return fft_size_; }
    std::size_t num_bins() const { return fft_size_ / 2 + 1; }

    // Forward transform. `input` must point to fft_size() real samples.
    // `output` must point to num_bins() std::complex<float> slots.
    void Forward(const float* input, std::complex<float>* output) const;

    // Inverse transform. `input` must point to num_bins() complex bins.
    // `output` must point to fft_size() real samples. The result is
    // UNNORMALIZED — divide by fft_size() if you want the mathematical
    // inverse.
    void Inverse(const std::complex<float>* input, float* output) const;

private:
    struct Impl;
    std::size_t fft_size_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fim::dsp
