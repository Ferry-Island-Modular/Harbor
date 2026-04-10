#include "dsp/real_fft.h"

#include <cassert>
#include <cstring>

#include "pffft.h"

namespace fim::dsp {

// PFFFT's real FFT output (via pffft_transform_ordered) is packed as:
//   [dc_real, nyquist_real, real_1, imag_1, real_2, imag_2, ...,
//    real_{N/2-1}, imag_{N/2-1}]
// for a total of N floats (where N = fft_size). DC and Nyquist have no
// imaginary component so they share the first two slots. We translate this
// to a standard std::complex<float> array of length N/2+1 for callers.

struct RealFft::Impl {
    PFFFT_Setup* setup = nullptr;
    float* pack_buffer = nullptr;  // N floats, packed PFFFT format
    float* work_buffer = nullptr;  // N floats, PFFFT work area
    std::size_t n;

    explicit Impl(std::size_t fft_size) : n(fft_size) {
        setup = pffft_new_setup(static_cast<int>(n), PFFFT_REAL);
        assert(setup != nullptr && "PFFFT does not support this fft_size");
        pack_buffer = static_cast<float*>(pffft_aligned_malloc(n * sizeof(float)));
        work_buffer = static_cast<float*>(pffft_aligned_malloc(n * sizeof(float)));
    }

    ~Impl() {
        if (work_buffer) {
            pffft_aligned_free(work_buffer);
        }
        if (pack_buffer) {
            pffft_aligned_free(pack_buffer);
        }
        if (setup) {
            pffft_destroy_setup(setup);
        }
    }
};

RealFft::RealFft(std::size_t fft_size)
    : fft_size_(fft_size), impl_(std::make_unique<Impl>(fft_size)) {}

RealFft::~RealFft() = default;

void RealFft::Forward(const float* input, std::complex<float>* output) const {
    // Step 1: copy caller's input into aligned scratch (the caller's buffer
    // may be unaligned).
    std::memcpy(impl_->pack_buffer, input, fft_size_ * sizeof(float));

    // Step 2: transform in place on pack_buffer.
    pffft_transform_ordered(impl_->setup, impl_->pack_buffer, impl_->pack_buffer,
                            impl_->work_buffer, PFFFT_FORWARD);

    // Step 3: unpack PFFFT's format into standard std::complex bins.
    const std::size_t half = fft_size_ / 2;
    output[0] = std::complex<float>(impl_->pack_buffer[0], 0.0f);     // DC
    output[half] = std::complex<float>(impl_->pack_buffer[1], 0.0f);  // Nyquist
    for (std::size_t k = 1; k < half; ++k) {
        output[k] = std::complex<float>(impl_->pack_buffer[2 * k], impl_->pack_buffer[2 * k + 1]);
    }
}

void RealFft::Inverse(const std::complex<float>* input, float* output) const {
    // Step 1: pack standard complex bins into PFFFT's packed format.
    const std::size_t half = fft_size_ / 2;
    impl_->pack_buffer[0] = input[0].real();     // DC
    impl_->pack_buffer[1] = input[half].real();  // Nyquist
    for (std::size_t k = 1; k < half; ++k) {
        impl_->pack_buffer[2 * k] = input[k].real();
        impl_->pack_buffer[2 * k + 1] = input[k].imag();
    }

    // Step 2: inverse transform in place on pack_buffer.
    pffft_transform_ordered(impl_->setup, impl_->pack_buffer, impl_->pack_buffer,
                            impl_->work_buffer, PFFFT_BACKWARD);

    // Step 3: copy aligned scratch out to caller's (possibly unaligned) buffer.
    std::memcpy(output, impl_->pack_buffer, fft_size_ * sizeof(float));
}

}  // namespace fim::dsp
