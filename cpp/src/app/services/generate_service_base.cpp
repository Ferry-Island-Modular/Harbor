#include "app/services/generate_service_base.h"

#include <QThreadPool>
#include <filesystem>

namespace fim::app {

GenerateServiceBase::GenerateServiceBase(QObject* parent) : QObject(parent) {}

void GenerateServiceBase::SetInputFile(const QString& path) {
    input_file_ = path;
}

QString GenerateServiceBase::InputFile() const {
    return input_file_;
}

void GenerateServiceBase::SetOutputDirectory(const QString& path) {
    output_directory_ = path;
}

QString GenerateServiceBase::OutputDirectory() const {
    return output_directory_;
}

bool GenerateServiceBase::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void GenerateServiceBase::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    const QString in_file = input_file_;
    const QString out_dir = output_directory_;

    QThreadPool::globalInstance()->start([this, in_file, out_dir]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path output_path(out_dir.toStdString());

        const bool ok = DoGenerate(input_path, output_path, [this](int percent) {
            // Qt auto-queues cross-thread signal emits onto the GUI thread.
            emit progressChanged(percent);
        });

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
