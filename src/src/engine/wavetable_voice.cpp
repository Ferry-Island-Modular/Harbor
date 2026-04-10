#include "engine/wavetable_voice.h"

#include <algorithm>
#include <cstring>

#include "engine/wavetable_bank.h"
#include "src/params.h"

namespace fim::engine {

namespace {

constexpr float kPositionMin = 0.0f;
constexpr float kPositionMax = 6.9999f;  // matches the original StreamingEngine clamping

float ClampPosition(float v) {
    return std::clamp(v, kPositionMin, kPositionMax);
}

}  // namespace

WavetableVoice::WavetableVoice(float sample_rate) : sample_rate_(sample_rate) {}

void WavetableVoice::SetBank(std::shared_ptr<const WavetableBank> bank) {
    std::atomic_store(&bank_, std::move(bank));
}

void WavetableVoice::SetX(float x) {
    x_.store(ClampPosition(x), std::memory_order_relaxed);
}
void WavetableVoice::SetY(float y) {
    y_.store(ClampPosition(y), std::memory_order_relaxed);
}
void WavetableVoice::SetZ(float z) {
    z_.store(ClampPosition(z), std::memory_order_relaxed);
}
void WavetableVoice::SetFrequency(float hz) {
    frequency_.store(hz, std::memory_order_relaxed);
}
void WavetableVoice::SetPlaying(bool playing) {
    playing_.store(playing, std::memory_order_relaxed);
}

void WavetableVoice::RenderBlock(float* out, size_t num_samples) {
    auto bank = std::atomic_load(&bank_);

    if (!bank || !playing_.load(std::memory_order_relaxed)) {
        std::memset(out, 0, num_samples * sizeof(float));
        return;
    }

    // Re-init the oscillator if the bank pointer changed since last block.
    // Audio-thread only; no synchronization needed for last_inited_bank_.
    if (bank.get() != last_inited_bank_) {
        // Init takes a non-const float** because the firmware oscillator may
        // mutate its bank pointer (SetBank). We never call that, so the
        // const_cast is safe in our usage. Localized to this one line to make
        // the contract explicit.
        osc_.Init(const_cast<float**>(bank->wavetable_pointers()));
        last_inited_bank_ = bank.get();
    }

    // Snapshot the atomic targets once per block; smoothed state chases them
    // per-sample to prevent zipper noise on fast slider drags.
    const float target_x = x_.load(std::memory_order_relaxed);
    const float target_y = y_.load(std::memory_order_relaxed);
    const float target_z = z_.load(std::memory_order_relaxed);
    const float target_freq = frequency_.load(std::memory_order_relaxed);

    for (size_t i = 0; i < num_samples; ++i) {
        smoothed_x_ += (target_x - smoothed_x_) * kParamSmoothingCoeff;
        smoothed_y_ += (target_y - smoothed_y_) * kParamSmoothingCoeff;
        smoothed_z_ += (target_z - smoothed_z_) * kParamSmoothingCoeff;
        smoothed_frequency_ += (target_freq - smoothed_frequency_) * kParamSmoothingCoeff;

        fourseas::Params::Values values = {};
        values.frequency = smoothed_frequency_ / sample_rate_;
        values.x = smoothed_x_;
        values.y = smoothed_y_;
        values.z = smoothed_z_;
        values.osc_mod_amount = 0.0f;

        fourseas::OscillatorParams params = {
            .values = values,
            .interpolate = true,
            .mod_state = 0,
            .mod_input = 0.0f,
            .sync_state = 0,
            .sync_input = false,
        };

        osc_.Render(params, &out[i]);
    }
}

}  // namespace fim::engine
