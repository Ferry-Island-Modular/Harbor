#pragma once

#include <atomic>
#include <memory>

#include "wavetable_oscillator.h"

namespace fim::engine {

class WavetableBank;

// A single FourSeas wavetable voice. Wraps fourseas::WavetableOscillator with
// thread-safe atomic parameter setters and a callback-friendly RenderBlock.
//
// Bank ownership uses std::shared_ptr<const WavetableBank> accessed via free-
// function std::atomic_load / std::atomic_store. The audio thread takes a
// local snapshot of the shared_ptr at the start of each block; the loader
// thread atomically installs a new bank. The old bank is destroyed only when
// no audio block is still holding it, eliminating the use-after-free risk
// the original mutex-based StreamingEngine had.
class WavetableVoice {
public:
    explicit WavetableVoice(float sample_rate);

    // Atomically installs a new bank. Safe to call from any thread, including
    // while audio is playing.
    void SetBank(std::shared_ptr<const WavetableBank> bank);

    // Renders `num_samples` mono float samples into `out`. Safe to call from
    // an audio callback. If no bank is loaded or playing_ is false, fills the
    // buffer with zeros.
    void RenderBlock(float* out, size_t num_samples);

    // Thread-safe parameter setters. Range-clamped internally where applicable.
    void SetX(float x);
    void SetY(float y);
    void SetZ(float z);
    void SetFrequency(float hz);
    void SetPlaying(bool playing);

    float x() const { return x_.load(std::memory_order_relaxed); }
    float y() const { return y_.load(std::memory_order_relaxed); }
    float z() const { return z_.load(std::memory_order_relaxed); }
    float frequency() const { return frequency_.load(std::memory_order_relaxed); }
    bool playing() const { return playing_.load(std::memory_order_relaxed); }

    float sample_rate() const { return sample_rate_; }

private:
    static constexpr size_t kWavetableSize = 2048;

    float sample_rate_;
    fourseas::WavetableOscillator<kWavetableSize, false, false> osc_;

    // Bank pointer. Use std::atomic_load / std::atomic_store to access (free-
    // function form for shared_ptr — works in C++17, deprecated in C++20 but
    // still functional).
    std::shared_ptr<const WavetableBank> bank_;

    // Pointer to the bank we last initialized the oscillator with. Only
    // touched by the audio thread; non-atomic.
    const WavetableBank* last_inited_bank_ = nullptr;

    std::atomic<float> x_{0.0f};
    std::atomic<float> y_{0.0f};
    std::atomic<float> z_{0.0f};
    std::atomic<float> frequency_{440.0f};
    std::atomic<bool> playing_{false};
};

}  // namespace fim::engine
