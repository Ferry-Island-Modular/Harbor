#pragma once

#include <cstddef>
#include <vector>

#include "dsp/real_fft.h"

namespace fim::dsp {

// FFT-based length resampler. Converts a real-valued signal of one length
// to another by going through the frequency domain: forward FFT → zero-pad
// (upsample) or truncate (downsample) the spectrum → inverse FFT → scale.
//
// For periodic signals at integer ratios, this is equivalent to ideal sinc
// resampling — no anti-aliasing artifacts, no filter design choices, no
// dependency on a separate resampling library.
//
// Constraints:
// - Both `input_length` and `output_length` must be supported by PFFFT
//   (multiples of 32 with prime factors only in {2, 3, 5}).
// - The resampler is stateful and reusable: construct once per
//   (input_length, output_length) pair and call Resample many times to
//   amortize the PFFFT setup cost.
//
// Limitations:
// - When downsampling, the new Nyquist bin (input bin index output_length/2)
//   is generally complex but the inverse FFT treats it as real. The
//   imaginary part is discarded. For typical wavetable content where the
//   cutoff bin has small magnitude this is negligible. Document the
//   deviation if it ever matters in practice.
class FftResampler {
public:
    FftResampler(std::size_t input_length, std::size_t output_length);

    std::size_t input_length() const { return input_length_; }
    std::size_t output_length() const { return output_length_; }

    // Resample the input to `output_length`. The input must have exactly
    // `input_length()` samples. Returns a fresh vector of size
    // `output_length()`.
    std::vector<float> Resample(const std::vector<float>& input) const;

private:
    std::size_t input_length_;
    std::size_t output_length_;
    RealFft input_fft_;
    RealFft output_fft_;
};

}  // namespace fim::dsp
