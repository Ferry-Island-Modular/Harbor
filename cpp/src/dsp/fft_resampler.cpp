#include "dsp/fft_resampler.h"

#include <algorithm>
#include <cassert>
#include <complex>

namespace fim::dsp {

FftResampler::FftResampler(std::size_t input_length, std::size_t output_length)
    : input_length_(input_length),
      output_length_(output_length),
      input_fft_(input_length),
      output_fft_(output_length) {}

std::vector<float> FftResampler::Resample(const std::vector<float>& input) const {
    assert(input.size() == input_length_);

    // Step 1: forward FFT of the input. Bins are in standard rfft order.
    std::vector<std::complex<float>> input_bins(input_fft_.num_bins());
    input_fft_.Forward(input.data(), input_bins.data());

    // Step 2: build the output bins by truncating or zero-padding.
    std::vector<std::complex<float>> output_bins(output_fft_.num_bins(),
                                                 std::complex<float>(0.0f, 0.0f));
    const std::size_t copy_count = std::min(input_bins.size(), output_bins.size());
    for (std::size_t k = 0; k < copy_count; ++k) {
        output_bins[k] = input_bins[k];
    }

    // Step 3: inverse FFT into the output buffer.
    std::vector<float> output(output_length_);
    output_fft_.Inverse(output_bins.data(), output.data());

    // Step 4: PFFFT inverse is unnormalized. To preserve amplitude across
    // the resample, scale by 1/input_length. Derivation: backward(forward(x))
    // = N*x for unnormalized FFTs of length N. Selecting bins doesn't change
    // the magnitude of each retained bin, so backward_M(X') has effective
    // amplitude N*x at the M output samples. Dividing by N recovers x at the
    // resampled rate.
    const float scale = 1.0f / static_cast<float>(input_length_);
    for (float& s : output) {
        s *= scale;
    }
    return output;
}

}  // namespace fim::dsp
