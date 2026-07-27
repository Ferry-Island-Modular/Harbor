#pragma once

#include <QSettings>
#include <QString>
#include <string>

namespace fim::app {

// Thin typed-accessor wrapper around QSettings. Hides the raw key strings
// from the rest of the code. Uses the application-scoped QSettings (see
// QCoreApplication::setOrganizationName / setApplicationName).
class Settings {
public:
    Settings() = default;

    // ---- output_dir ----
    std::string OutputDir() const;
    void SetOutputDir(const std::string& path);

    // ---- samples_per_frame ----
    int SamplesPerFrame() const;
    void SetSamplesPerFrame(int samples);

    // ---- mode ----
    // The launcher mode the user last selected. One of "any-wav",
    // "serum-wav", "three-wavs". Defaults to "any-wav".
    std::string Mode() const;
    void SetMode(const std::string& mode);

    // ---- audio_device ----
    // miniaudio device index, or -1 for the system default.
    int AudioDevice() const;
    void SetAudioDevice(int device_index);

    // ---- preview_volume ----
    // 0..100 integer.
    int PreviewVolume() const;
    void SetPreviewVolume(int volume);

    // ---- y_morph / z_morph ----
    // Index into the AxisMorphSelector options for the any-wav mode.
    int YMorph() const;
    void SetYMorph(int index);
    int ZMorph() const;
    void SetZMorph(int index);

    // ---- any_wav_source_mode ----
    // 0 = focused salient-window progression (default), 1 = legacy
    // full-source progression plus X spectral stretch.
    int AnyWavSourceMode() const;
    void SetAnyWavSourceMode(int index);

    // ---- serum_y_morph / serum_z_morph ----
    // Index into the Serum mode's 4-option AxisMorphSelector. Default
    // matches Python's SerumWavetableConverter: Y = 0 (FORMANT_SCALE),
    // Z = 1 (PHASE_DISPERSE).
    int SerumYMorph() const;
    void SetSerumYMorph(int index);
    int SerumZMorph() const;
    void SetSerumZMorph(int index);

private:
    QSettings backing_;
};

}  // namespace fim::app
