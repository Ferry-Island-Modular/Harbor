#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/real_fft.h"

namespace fim::dsp {

// Morph types for Serum mode. Mirrored from Python's MorphType enum,
// clean-room implementations of Vital's spectral morph operations.
enum class SerumMode {
    kFormant,  // Bidirectional formant shift (0.25× down to 4× up)
    kPhase,    // Frequency-dependent phase rotation (comb-filter-like)
    kSmear,    // Running-average amplitude smoothing
    kStretch,  // Octave-based inharmonic stretching (1× to 12×)
    kOddEven,  // Progressively emphasizes odd over even harmonics
    kCrush,    // Time-domain texture handled by SerumGenerator
};

// Pre-computed FFT data for a single 2048-sample frame. Holds both the
// amplitude and phase arrays plus the normalized real/imag components
// (cos(phase) / sin(phase)) that several morph ops consume directly.
struct SerumFftCache {
    std::vector<float> amplitudes;       // length 1025
    std::vector<float> phases;           // length 1025
    std::vector<float> normalized_real;  // length 1025, == cos(phases)
    std::vector<float> normalized_imag;  // length 1025, == sin(phases)
};

// Applies Serum-mode spectral morph operations to frame FFT data.
// Holds a RealFft(2048) for efficient cache computation and reuse across
// many frames. Thread-unsafe; each worker thread should own its own
// instance.
class SerumMorpher {
public:
    SerumMorpher();
    ~SerumMorpher();

    SerumMorpher(const SerumMorpher&) = delete;
    SerumMorpher& operator=(const SerumMorpher&) = delete;

    // Compute an FFT cache from a single 2048-sample frame. The frame
    // must have exactly 2048 samples.
    SerumFftCache ComputeCache(const std::vector<float>& frame);

    // Circularly align each source cache to its predecessor using a
    // weighted multi-harmonic correlation search. This removes arbitrary
    // cycle-start offsets before X interpolation.
    void AlignSourcePhases(std::vector<SerumFftCache>& sources) const;

    // Interpolate N source FFT caches to `output_count` caches via
    // frequency-domain linear interpolation. Magnitudes are linearly
    // lerped between the two nearest source caches; phase unit vectors are
    // circularly interpolated so X cannot snap halfway between frames.
    std::vector<SerumFftCache> InterpolateCaches(const std::vector<SerumFftCache>& sources,
                                                 std::size_t output_count);

    // Apply a morph operation to an FFT cache. Returns a complex
    // spectrum (length 1025) that can be fed to irfft to produce the
    // morphed waveform.
    std::vector<std::complex<float>> Apply(const SerumFftCache& cache, SerumMode mode,
                                           float amount);

    // Inverse FFT a complex spectrum back to a time-domain frame of
    // length 2048. Handles the PFFFT 1/N normalization internally.
    std::vector<float> InverseFft(const std::vector<std::complex<float>>& bins);

private:
    RealFft fft_;
};

}  // namespace fim::dsp
