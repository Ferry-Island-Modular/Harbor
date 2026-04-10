#include "dsp/stft.h"

#include <cmath>

#include "dsp/window.h"

namespace fim::dsp {

Stft::Stft(std::size_t fft_size, std::size_t hop_size)
    : fft_size_(fft_size), hop_size_(hop_size), window_(HannWindow(fft_size)), fft_(fft_size) {}

std::size_t Stft::NumFrames(std::size_t n_samples) const {
    if (n_samples < fft_size_) {
        return 0;
    }
    return (n_samples - fft_size_) / hop_size_ + 1;
}

std::vector<std::vector<std::complex<float>>> Stft::Analyze(const std::vector<float>& audio) const {
    const std::size_t num_frames = NumFrames(audio.size());
    std::vector<std::vector<std::complex<float>>> bins(num_frames);

    std::vector<float> frame(fft_size_);
    for (std::size_t f = 0; f < num_frames; ++f) {
        const std::size_t start = f * hop_size_;
        // Copy the windowed frame into a scratch buffer.
        for (std::size_t i = 0; i < fft_size_; ++i) {
            frame[i] = audio[start + i] * window_[i];
        }
        bins[f].resize(num_bins());
        fft_.Forward(frame.data(), bins[f].data());
    }
    return bins;
}

std::vector<std::vector<float>> Magnitude(
    const std::vector<std::vector<std::complex<float>>>& bins) {
    std::vector<std::vector<float>> result(bins.size());
    for (std::size_t f = 0; f < bins.size(); ++f) {
        result[f].resize(bins[f].size());
        for (std::size_t k = 0; k < bins[f].size(); ++k) {
            result[f][k] = std::abs(bins[f][k]);
        }
    }
    return result;
}

std::vector<std::vector<float>> Phase(const std::vector<std::vector<std::complex<float>>>& bins) {
    std::vector<std::vector<float>> result(bins.size());
    for (std::size_t f = 0; f < bins.size(); ++f) {
        result[f].resize(bins[f].size());
        for (std::size_t k = 0; k < bins[f].size(); ++k) {
            result[f][k] = std::arg(bins[f][k]);
        }
    }
    return result;
}

}  // namespace fim::dsp
