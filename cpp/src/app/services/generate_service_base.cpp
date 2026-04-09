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

void GenerateServiceBase::SetPreviewCacheDirectory(const QString& path) {
    preview_cache_directory_ = path;
}

QString GenerateServiceBase::PreviewCacheDirectory() const {
    return preview_cache_directory_;
}

bool GenerateServiceBase::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void GenerateServiceBase::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    const QString in_file = input_file_;
    const QString cache_dir = preview_cache_directory_;
    // user_export_dir is captured for Task 3/4 — currently unused inside
    // the worker. The DSP cores write to the preview cache; a follow-up
    // step will copy/decimate from cache to user_export_dir.
    const QString user_export_dir = output_directory_;

    QThreadPool::globalInstance()->start([this, in_file, cache_dir, user_export_dir]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path cache_path(cache_dir.toStdString());

        const bool ok = DoGenerate(input_path, cache_path, [this](int percent) {
            // Qt auto-queues cross-thread signal emits onto the GUI thread.
            emit progressChanged(percent);
        });

        (void)user_export_dir;

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
