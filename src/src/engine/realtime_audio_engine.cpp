#include "engine/realtime_audio_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>

#include "engine/wavetable_bank.h"
#include "miniaudio.h"

namespace fim::engine {

namespace {

float ClampUnit(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

std::int64_t SteadyNowNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

RealtimeAudioEngine::RealtimeAudioEngine(float sample_rate, size_t block_size)
    : sample_rate_(sample_rate),
      block_size_(block_size),
      voice_(sample_rate),
      device_(std::make_unique<ma_device>()) {}

RealtimeAudioEngine::~RealtimeAudioEngine() {
    Stop();
}

bool RealtimeAudioEngine::LoadBank(const std::string& bank_directory) {
    auto bank = WavetableBank::Load(bank_directory);
    if (!bank) {
        return false;
    }
    voice_.SetBank(std::shared_ptr<const WavetableBank>(std::move(bank)));
    loaded_once_.store(true, std::memory_order_relaxed);
    return true;
}

bool RealtimeAudioEngine::IsLoaded() const {
    return loaded_once_.load(std::memory_order_relaxed);
}

bool RealtimeAudioEngine::IsPlaying() const {
    return device_open_.load(std::memory_order_relaxed);
}

bool RealtimeAudioEngine::Start() {
    if (device_open_.load(std::memory_order_relaxed)) {
        return true;  // already started
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = static_cast<ma_uint32>(sample_rate_);
    config.dataCallback = &RealtimeAudioEngine::MiniaudioDataCallback;
    config.pUserData = this;
    config.periodSizeInFrames = static_cast<ma_uint32>(block_size_);

    if (ma_device_init(nullptr, &config, device_.get()) != MA_SUCCESS) {
        std::cerr << "RealtimeAudioEngine: ma_device_init failed\n";
        return false;
    }

    voice_.SetPlaying(true);

    if (ma_device_start(device_.get()) != MA_SUCCESS) {
        std::cerr << "RealtimeAudioEngine: ma_device_start failed\n";
        ma_device_uninit(device_.get());
        voice_.SetPlaying(false);
        return false;
    }

    device_open_.store(true, std::memory_order_release);
    return true;
}

void RealtimeAudioEngine::Stop() {
    if (!device_open_.load(std::memory_order_relaxed)) {
        return;
    }
    voice_.SetPlaying(false);
    ma_device_uninit(device_.get());
    device_open_.store(false, std::memory_order_release);
}

void RealtimeAudioEngine::SetMidiNote(int midi_note) {
    midi_base_note_.store(midi_note, std::memory_order_relaxed);
    SetFrequency(MidiToFrequency(midi_note));
}

void RealtimeAudioEngine::SetVolume(float volume) {
    volume_.store(ClampUnit(volume), std::memory_order_relaxed);
}

void RealtimeAudioEngine::SetMode(PlayMode mode) {
    if (mode == PlayMode::kSweep) {
        sweep_start_ns_.store(SteadyNowNanoseconds(), std::memory_order_relaxed);
    } else if (mode == PlayMode::kArpeggio) {
        arpeggio_start_ns_.store(SteadyNowNanoseconds(), std::memory_order_relaxed);
    }
    mode_.store(mode, std::memory_order_release);
}

PlayMode RealtimeAudioEngine::mode() const {
    return mode_.load(std::memory_order_acquire);
}

void RealtimeAudioEngine::SetSweepTarget(float target_x, float target_y, float target_z,
                                         float duration) {
    sweep_target_x_.store(target_x, std::memory_order_relaxed);
    sweep_target_y_.store(target_y, std::memory_order_relaxed);
    sweep_target_z_.store(target_z, std::memory_order_relaxed);
    sweep_duration_.store(std::max(duration, 0.0f), std::memory_order_relaxed);
}

void RealtimeAudioEngine::AudioCallback(float* output, size_t num_frames) {
    // Update positions for SWEEP/ARPEGGIO modes BEFORE rendering this block.
    const PlayMode current_mode = mode_.load(std::memory_order_acquire);
    const std::int64_t now_ns = SteadyNowNanoseconds();

    if (current_mode == PlayMode::kSweep) {
        const float elapsed_sec =
            static_cast<float>(now_ns - sweep_start_ns_.load(std::memory_order_relaxed)) /
            1'000'000'000.0f;
        const float duration = sweep_duration_.load(std::memory_order_relaxed);
        const auto pos =
            SweepPositionAt(elapsed_sec, duration, sweep_target_x_.load(std::memory_order_relaxed),
                            sweep_target_y_.load(std::memory_order_relaxed),
                            sweep_target_z_.load(std::memory_order_relaxed));
        voice_.SetX(pos.x);
        voice_.SetY(pos.y);
        voice_.SetZ(pos.z);
        if (elapsed_sec >= duration) {
            PlayMode expected = PlayMode::kSweep;
            mode_.compare_exchange_strong(expected, PlayMode::kSteady, std::memory_order_release,
                                          std::memory_order_relaxed);
        }
    } else if (current_mode == PlayMode::kArpeggio) {
        const float elapsed_sec =
            static_cast<float>(now_ns - arpeggio_start_ns_.load(std::memory_order_relaxed)) /
            1'000'000'000.0f;
        const int idx = ArpeggioIndexAt(elapsed_sec);
        const int interval = kArpeggioIntervals[idx % kArpeggioIntervals.size()];
        const int base = midi_base_note_.load(std::memory_order_relaxed);
        voice_.SetFrequency(MidiToFrequency(base + interval));
    }

    // Render audio from the voice.
    voice_.RenderBlock(output, num_frames);

    // Apply volume + invert with one-pole smoothing on volume to avoid zipper
    // noise on slider drags.
    const float target_volume = volume_.load(std::memory_order_relaxed);
    const bool invert = invert_.load(std::memory_order_relaxed);

    for (size_t i = 0; i < num_frames; ++i) {
        smoothed_volume_ += (target_volume - smoothed_volume_) * kVolumeSmoothingCoeff;
        const float gain = invert ? -smoothed_volume_ : smoothed_volume_;
        output[i] *= gain;
    }
}

void RealtimeAudioEngine::MiniaudioDataCallback(ma_device* device, void* output,
                                                const void* /*input*/, unsigned frame_count) {
    auto* self = static_cast<RealtimeAudioEngine*>(device->pUserData);
    self->AudioCallback(static_cast<float*>(output), frame_count);
}

float RealtimeAudioEngine::MidiToFrequency(int midi_note) {
    return 440.0f * std::pow(2.0f, static_cast<float>(midi_note - 69) / 12.0f);
}

Position3D RealtimeAudioEngine::SweepPositionAt(float elapsed, float duration, float target_x,
                                                float target_y, float target_z) {
    if (elapsed >= duration) {
        return {target_x, target_y, target_z};
    }
    const float progress = elapsed / duration;
    return {target_x * progress, target_y * progress, target_z * progress};
}

int RealtimeAudioEngine::ArpeggioIndexAt(float elapsed) {
    return static_cast<int>(elapsed / kArpeggioNoteDurationSec);
}

}  // namespace fim::engine
