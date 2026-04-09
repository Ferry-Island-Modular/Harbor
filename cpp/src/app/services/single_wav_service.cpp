#include "app/services/single_wav_service.h"

#include <QThreadPool>
#include <filesystem>

#include "dsp/single_wav_generator.h"

namespace fim::app {

SingleWavService::SingleWavService(QObject* parent) : QObject(parent) {}

void SingleWavService::SetInputFile(const QString& path) {
    input_file_ = path;
}
QString SingleWavService::InputFile() const {
    return input_file_;
}

void SingleWavService::SetOutputDirectory(const QString& path) {
    output_directory_ = path;
}
QString SingleWavService::OutputDirectory() const {
    return output_directory_;
}

void SingleWavService::SetYMode(fim::dsp::YMode mode) {
    options_.y_mode = mode;
}
void SingleWavService::SetZMode(fim::dsp::ZMode mode) {
    options_.z_mode = mode;
}

bool SingleWavService::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void SingleWavService::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    const QString in_file = input_file_;
    const QString out_dir = output_directory_;
    const fim::dsp::GenerateOptions opts = options_;

    QThreadPool::globalInstance()->start([this, in_file, out_dir, opts]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path output_path(out_dir.toStdString());

        fim::dsp::SingleWavGenerator generator;
        // Qt auto-queues cross-thread signal emits onto the GUI thread.
        const bool ok = generator.Generate(input_path, output_path, opts,
                                           [this](int percent) { emit progressChanged(percent); });

        generating_.store(false, std::memory_order_release);

        if (ok) {
            emit generationFinished();
        } else {
            emit generationFailed(
                QString("Failed to generate wavetable bank from %1").arg(in_file));
        }
    });
}

}  // namespace fim::app
