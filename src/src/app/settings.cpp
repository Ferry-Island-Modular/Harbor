#include "app/settings.h"

#include <QDir>
#include <QStandardPaths>

namespace fim::app {

namespace {

constexpr const char* kKeyOutputDir = "output_dir";
constexpr const char* kKeySamplesPerFrame = "samples_per_frame";
constexpr const char* kKeyMode = "mode";
constexpr const char* kKeyAudioDevice = "audio_device";
constexpr const char* kKeyPreviewVolume = "preview_volume";
constexpr const char* kKeyYMorph = "y_morph";
constexpr const char* kKeyZMorph = "z_morph";
constexpr const char* kKeyAnyWavSourceMode = "any_wav_source_mode";
constexpr const char* kKeySerumYMorph = "serum_y_morph";
constexpr const char* kKeySerumZMorph = "serum_z_morph";

}  // namespace

std::string Settings::OutputDir() const {
    const QString default_dir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath("Harbor");
    return backing_.value(kKeyOutputDir, default_dir).toString().toStdString();
}

void Settings::SetOutputDir(const std::string& path) {
    backing_.setValue(kKeyOutputDir, QString::fromStdString(path));
}

int Settings::SamplesPerFrame() const {
    return backing_.value(kKeySamplesPerFrame, 2048).toInt();
}

void Settings::SetSamplesPerFrame(int samples) {
    backing_.setValue(kKeySamplesPerFrame, samples);
}

std::string Settings::Mode() const {
    return backing_.value(kKeyMode, "any-wav").toString().toStdString();
}

void Settings::SetMode(const std::string& mode) {
    backing_.setValue(kKeyMode, QString::fromStdString(mode));
}

int Settings::AudioDevice() const {
    return backing_.value(kKeyAudioDevice, -1).toInt();
}

void Settings::SetAudioDevice(int device_index) {
    backing_.setValue(kKeyAudioDevice, device_index);
}

int Settings::PreviewVolume() const {
    return backing_.value(kKeyPreviewVolume, 60).toInt();
}

void Settings::SetPreviewVolume(int volume) {
    backing_.setValue(kKeyPreviewVolume, volume);
}

int Settings::YMorph() const {
    return backing_.value(kKeyYMorph, 0).toInt();
}

void Settings::SetYMorph(int index) {
    backing_.setValue(kKeyYMorph, index);
}

int Settings::ZMorph() const {
    return backing_.value(kKeyZMorph, 0).toInt();
}

void Settings::SetZMorph(int index) {
    backing_.setValue(kKeyZMorph, index);
}

int Settings::AnyWavSourceMode() const {
    return backing_.value(kKeyAnyWavSourceMode, 0).toInt();
}

void Settings::SetAnyWavSourceMode(int index) {
    backing_.setValue(kKeyAnyWavSourceMode, index);
}

int Settings::SerumYMorph() const {
    return backing_.value(kKeySerumYMorph, 0).toInt();
}

void Settings::SetSerumYMorph(int index) {
    backing_.setValue(kKeySerumYMorph, index);
}

int Settings::SerumZMorph() const {
    return backing_.value(kKeySerumZMorph, 1).toInt();
}

void Settings::SetSerumZMorph(int index) {
    backing_.setValue(kKeySerumZMorph, index);
}

}  // namespace fim::app
