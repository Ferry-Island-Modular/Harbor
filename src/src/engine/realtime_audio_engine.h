#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "engine/wavetable_voice.h"

struct ma_device;  // forward decl from miniaudio.h

namespace fim::engine {

class WavetableBank;

enum class PlayMode {
    kSteady,
    kSweep,
    kArpeggio,
};

struct Position3D {
    float x;
    float y;
    float z;
};

// Real-time audio engine. Owns a WavetableVoice plus a miniaudio playback
// device. Wires the device's audio callback into the voice's RenderBlock,
// applies volume + invert, and runs the PlayMode state machine (steady tone,
// sweep, arpeggio) plus exponential one-pole parameter smoothing on volume to
// avoid zipper noise on slider drags.
//
// Lifetime: construct, optionally LoadBank, Start (opens device), eventually
// Stop (closes device), destruct.
class RealtimeAudioEngine {
public:
    // Major-triad up-and-down arpeggio pattern, in semitones from the base
    // note. Matches RealtimeAudioEngine in streaming.py.
    static constexpr std::array<int, 6> kArpeggioIntervals{0, 4, 7, 12, 7, 4};
    static constexpr float kArpeggioNoteDurationSec = 0.25f;

    explicit RealtimeAudioEngine(float sample_rate = 48000.0f, size_t block_size = 512);
    ~RealtimeAudioEngine();

    RealtimeAudioEngine(const RealtimeAudioEngine&) = delete;
    RealtimeAudioEngine& operator=(const RealtimeAudioEngine&) = delete;

    // Loads a wavetable bank from a directory of 1.wav..8.wav files. Returns
    // false on any I/O or format error. Safe to call while playing — the new
    // bank atomically replaces the old one without dropouts.
    bool LoadBank(const std::string& bank_directory);

    // Opens the audio device and begins playback. Returns false if no bank
    // is loaded or the device cannot be opened.
    bool Start();

    // Stops playback and closes the audio device.
    void Stop();

    bool IsPlaying() const;
    bool IsLoaded() const;

    // Position controls (0.0..6.9999, clamped internally).
    void SetX(float x) { voice_.SetX(x); }
    void SetY(float y) { voice_.SetY(y); }
    void SetZ(float z) { voice_.SetZ(z); }
    void SetPosition(float x, float y, float z) {
        voice_.SetX(x);
        voice_.SetY(y);
        voice_.SetZ(z);
    }

    // Pitch controls.
    void SetFrequency(float hz) { voice_.SetFrequency(hz); }
    void SetMidiNote(int midi_note);
    int midi_note() const { return midi_base_note_.load(std::memory_order_relaxed); }

    // Volume in [0.0, 1.0]; clamped internally.
    void SetVolume(float volume);
    float volume() const { return volume_.load(std::memory_order_relaxed); }

    // Whether to invert the output (matches the FourSeas hardware's inverting
    // op-amp). Defaults to true.
    void SetInvert(bool invert) { invert_.store(invert, std::memory_order_relaxed); }
    bool invert() const { return invert_.load(std::memory_order_relaxed); }

    // Playback mode.
    void SetMode(PlayMode mode);
    PlayMode mode() const;

    // Sweep target. The sweep starts at (0,0,0) and progresses linearly to
    // (target_x, target_y, target_z) over `duration` seconds.
    void SetSweepTarget(float target_x, float target_y, float target_z, float duration);

    float sample_rate() const { return sample_rate_; }

    // ---- Pure-math helpers (testable without opening audio devices) ----

    static float MidiToFrequency(int midi_note);

    // Returns position at time `elapsed` into a sweep of total `duration`,
    // starting from (0,0,0) and ending at (target_x, target_y, target_z).
    // Clamps to the target if elapsed > duration.
    static Position3D SweepPositionAt(float elapsed, float duration, float target_x, float target_y,
                                      float target_z);

    // Returns the arpeggio index for time `elapsed` since arpeggio start.
    // Wraps around the kArpeggioIntervals array.
    static int ArpeggioIndexAt(float elapsed);

private:
    void AudioCallback(float* output, size_t num_frames);
    static void MiniaudioDataCallback(ma_device* device, void* output, const void* input,
                                      unsigned frame_count);

    float sample_rate_;
    size_t block_size_;

    WavetableVoice voice_;
    std::unique_ptr<ma_device> device_;  // PIMPL-ish: held by pointer so the
                                         // header doesn't need miniaudio.h
    std::atomic<bool> device_open_{false};

    // Volume + invert
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> invert_{true};

    // Smoothed parameters used inside the audio callback. Audio thread only.
    float smoothed_volume_ = 1.0f;
    static constexpr float kVolumeSmoothingCoeff = 0.001f;

    // PlayMode state is lock-free from the audio callback's perspective. Time
    // points are stored as steady-clock nanoseconds so GUI updates cannot ever
    // block realtime rendering.
    std::atomic<PlayMode> mode_{PlayMode::kSteady};
    std::atomic<std::int64_t> sweep_start_ns_{0};
    std::atomic<float> sweep_target_x_{0.0f};
    std::atomic<float> sweep_target_y_{0.0f};
    std::atomic<float> sweep_target_z_{0.0f};
    std::atomic<float> sweep_duration_{4.0f};
    std::atomic<std::int64_t> arpeggio_start_ns_{0};

    // MIDI base note for arpeggio
    std::atomic<int> midi_base_note_{60};

    // Set true after the first successful LoadBank call. Used by IsLoaded().
    std::atomic<bool> loaded_once_{false};
};

}  // namespace fim::engine
