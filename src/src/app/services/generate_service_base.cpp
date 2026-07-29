#include "app/services/generate_service_base.h"

#include <QThreadPool>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "app/services/export_writer.h"

namespace fim::app {

namespace {

std::filesystem::path UniqueSibling(const std::filesystem::path& destination, const char* label) {
    static std::atomic<std::uint64_t> counter{0};
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    return destination.parent_path() /
           (destination.filename().string() + "." + label + "-" + std::to_string(id));
}

}  // namespace

GenerateServiceBase::GenerateServiceBase(QObject* parent) : QObject(parent) {}

GenerateServiceBase::~GenerateServiceBase() {
    // The worker only touches GenerateServiceBase after dispatch; all derived
    // state was copied into GenerationTask first. Waiting here therefore keeps
    // the base object and its signals alive until the final worker access.
    std::unique_lock lock(worker_mutex_);
    worker_finished_.wait(lock, [this]() { return !worker_active_; });
}

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
    GenerationTask task;
    try {
        task = CreateGenerationTask();
    } catch (...) {
        generating_.store(false, std::memory_order_release);
        emit generationFailed("Unable to prepare wavetable generation");
        return;
    }

    if (!task || cache_dir.isEmpty()) {
        generating_.store(false, std::memory_order_release);
        emit generationFailed("Unable to prepare wavetable generation");
        return;
    }

    {
        std::lock_guard lock(worker_mutex_);
        worker_active_ = true;
    }

    QThreadPool::globalInstance()->start(
        [this, task = std::move(task), in_file, cache_dir, user_export_dir, samples_per_frame]() {
            const std::filesystem::path input_path(in_file.toStdString());
            const std::filesystem::path cache_path(cache_dir.toStdString());
            const std::filesystem::path staging_path = UniqueSibling(cache_path, "staging");

            bool dsp_ok = false;
            bool publish_ok = false;
            bool export_ok = true;

            try {
                std::error_code cleanup_ec;
                std::filesystem::remove_all(staging_path, cleanup_ec);

                dsp_ok = task(input_path, staging_path,
                              [this](int percent) { emit progressChanged(percent); });

                if (dsp_ok) {
                    publish_ok = PublishStagedBank(staging_path, cache_path);
                }

                // After successful publication, copy (and optionally
                // downsample) the bank to the user-facing export directory.
                if (dsp_ok && publish_ok && !user_export_dir.isEmpty() &&
                    user_export_dir != cache_dir) {
                    export_ok = WriteBankToExportDir(cache_dir, user_export_dir, samples_per_frame);
                }

                if (!publish_ok) {
                    std::filesystem::remove_all(staging_path, cleanup_ec);
                }
            } catch (...) {
                std::error_code cleanup_ec;
                std::filesystem::remove_all(staging_path, cleanup_ec);
                dsp_ok = false;
            }

            if (dsp_ok && publish_ok && export_ok) {
                emit generationFinished();
            } else if (!dsp_ok) {
                emit generationFailed(
                    QString("Failed to generate wavetable bank from %1").arg(in_file));
            } else if (!publish_ok) {
                emit generationFailed(
                    QString("Generated bank, but failed to publish preview cache at %1")
                        .arg(cache_dir));
            } else {
                emit generationFailed(
                    QString("Generated bank, but failed to write to %1").arg(user_export_dir));
            }

            {
                std::lock_guard lock(worker_mutex_);
                // Publish availability and retire the lifetime guard together.
                // A new Generate() may win the atomic immediately afterward,
                // but it must take this mutex before marking its own worker active.
                generating_.store(false, std::memory_order_release);
                worker_active_ = false;
                worker_finished_.notify_all();
            }
        });
}

}  // namespace fim::app
