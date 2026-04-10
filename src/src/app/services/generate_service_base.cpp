#include "app/services/generate_service_base.h"

#include <QThreadPool>
#include <filesystem>

#include "app/services/export_writer.h"

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

void GenerateServiceBase::SetSamplesPerFrame(int samples) {
    samples_per_frame_ = samples;
}

int GenerateServiceBase::SamplesPerFrame() const {
    return samples_per_frame_;
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
    const QString user_export_dir = output_directory_;
    const int samples_per_frame = samples_per_frame_;

    QThreadPool::globalInstance()->start(
        [this, in_file, cache_dir, user_export_dir, samples_per_frame]() {
            const std::filesystem::path input_path(in_file.toStdString());
            const std::filesystem::path cache_path(cache_dir.toStdString());

            const bool dsp_ok = DoGenerate(input_path, cache_path, [this](int percent) {
                // Qt auto-queues cross-thread signal emits onto the GUI thread.
                emit progressChanged(percent);
            });

            // After DSP succeeds, copy (and optionally downsample) the bank
            // from the preview cache to the user-chosen export directory.
            // Skip if the two are the same (legacy default — the cache IS
            // the user dir). Also skip if user_export_dir is empty.
            bool export_ok = true;
            if (dsp_ok && !user_export_dir.isEmpty() && user_export_dir != cache_dir) {
                export_ok = WriteBankToExportDir(cache_dir, user_export_dir, samples_per_frame);
            }

            generating_.store(false, std::memory_order_release);

            if (dsp_ok && export_ok) {
                emit generationFinished();
            } else if (!dsp_ok) {
                emit generationFailed(
                    QString("Failed to generate wavetable bank from %1").arg(in_file));
            } else {
                emit generationFailed(
                    QString("Generated bank, but failed to write to %1").arg(user_export_dir));
            }
        });
}

}  // namespace fim::app
