#include "app/settings.h"

namespace fim::app {

namespace {

constexpr const char* kKeyOutputDir = "output_dir";
constexpr const char* kKeySamplesPerFrame = "samples_per_frame";
constexpr const char* kKeyMode = "mode";
constexpr const char* kKeyAudioDevice = "audio_device";
constexpr const char* kKeyPreviewVolume = "preview_volume";
constexpr const char* kKeyYMorph = "y_morph";
constexpr const char* kKeyZMorph = "z_morph";

}  // namespace

std::string Settings::OutputDir() const {
    return backing_.value(kKeyOutputDir, "output_waves").toString().toStdString();
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

}  // namespace fim::app
