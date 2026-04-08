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
    std::lock_guard<std::mutex> lock(mode_mutex_);
    mode_ = mode;
    if (mode == PlayMode::kSweep) {
        sweep_start_time_ = std::chrono::steady_clock::now();
    } else if (mode == PlayMode::kArpeggio) {
        arpeggio_start_time_ = std::chrono::steady_clock::now();
    }
}

PlayMode RealtimeAudioEngine::mode() const {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    return mode_;
}

void RealtimeAudioEngine::SetSweepTarget(float target_x, float target_y, float target_z,
                                         float duration) {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    sweep_target_x_ = target_x;
    sweep_target_y_ = target_y;
    sweep_target_z_ = target_z;
    sweep_duration_ = duration;
}

void RealtimeAudioEngine::AudioCallback(float* output, size_t num_frames) {
    // Update positions for SWEEP/ARPEGGIO modes BEFORE rendering this block.
    {
        std::lock_guard<std::mutex> lock(mode_mutex_);
        const auto now = std::chrono::steady_clock::now();

        if (mode_ == PlayMode::kSweep) {
            const float elapsed_sec = std::chrono::duration<float>(now - sweep_start_time_).count();
            const auto pos = SweepPositionAt(elapsed_sec, sweep_duration_, sweep_target_x_,
                                             sweep_target_y_, sweep_target_z_);
            voice_.SetX(pos.x);
            voice_.SetY(pos.y);
            voice_.SetZ(pos.z);
            if (elapsed_sec >= sweep_duration_) {
                mode_ = PlayMode::kSteady;  // sweep done
            }
        } else if (mode_ == PlayMode::kArpeggio) {
            const float elapsed_sec =
                std::chrono::duration<float>(now - arpeggio_start_time_).count();
            const int idx = ArpeggioIndexAt(elapsed_sec);
            const int interval = kArpeggioIntervals[idx % kArpeggioIntervals.size()];
            const int base = midi_base_note_.load(std::memory_order_relaxed);
            voice_.SetFrequency(MidiToFrequency(base + interval));
        }
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
