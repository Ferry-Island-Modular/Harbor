#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>

namespace fim::app {

// Abstract base for wavetable generation services. Each mode (single-wav,
// Serum, three-wav) has its own concrete subclass that plugs in a specific
// DSP generator via an immutable GenerationTask. The base class owns the
// common scaffolding: input/output path storage, the "generating" atomic,
// the QThreadPool dispatch, and the three Qt signals that the UI connects
// to.
//
// Lifecycle:
//   1. Construct (owned by a QObject parent, typically the mode screen).
//   2. SetInputFile / SetOutputDirectory as the user provides values.
//   3. Subclass configures its own mode-specific options (e.g. Y/Z morph
//      selections via its own setters).
//   4. Generate() snapshots a GenerationTask on the GUI thread, then queues
//      that self-contained task on the global thread pool.
//   5. Signals fire for progress and completion on the GUI thread via
//      Qt's auto-queued cross-thread signals.
class GenerateServiceBase : public QObject {
    Q_OBJECT

public:
    explicit GenerateServiceBase(QObject* parent = nullptr);
    ~GenerateServiceBase() override;

    void SetInputFile(const QString& path);
    QString InputFile() const;

    void SetOutputDirectory(const QString& path);
    QString OutputDirectory() const;

    // The preview cache is where the always-2048-sample bank is written.
    // The audio engine loads from here. This is distinct from the user-
    // facing export directory (SetOutputDirectory) which may live anywhere
    // and may receive a downsampled copy of the bank.
    void SetPreviewCacheDirectory(const QString& path);
    QString PreviewCacheDirectory() const;

    // Target samples-per-cycle for the user-export bank. Defaults to 2048
    // (Four Seas). When set to 256 (Waveedit), the ExportWriter
    // downsamples each cycle from the always-2048 preview cache before
    // writing to the user export directory. The preview cache is
    // unaffected; the audio engine always loads 2048-sample cycles.
    void SetSamplesPerFrame(int samples);
    int SamplesPerFrame() const;

    bool IsGenerating() const;

public slots:
    // Queue the generation work on QThreadPool::globalInstance(). Returns
    // immediately. Calling Generate() while already generating is a no-op.
    void Generate();

signals:
    // Emitted from the worker thread. Qt auto-queues cross-thread signal
    // emits onto the GUI thread when the receiver lives there.
    void progressChanged(int percent);
    void generationFinished();
    void generationFailed(const QString& error);

protected:
    // Type of the progress callback passed to GenerationTask. Tasks call
    // this from inside their DSP work to report progress.
    using ProgressCallback = std::function<void(int percent)>;

    // A complete, immutable generation job. CreateGenerationTask() is called
    // on the service's owning (GUI) thread before work is dispatched. Concrete
    // services must capture every mode-specific input by value so the returned
    // callable never needs to dereference the service from the worker thread.
    using GenerationTask =
        std::function<bool(const std::filesystem::path& input, const std::filesystem::path& output,
                           const ProgressCallback& progress_cb)>;

    // Build the worker callable. Called synchronously from Generate(), before
    // QThreadPool dispatch, so derived state can be copied without a race.
    virtual GenerationTask CreateGenerationTask() const = 0;

private:
    QString input_file_;
    QString output_directory_;
    QString preview_cache_directory_;
    int samples_per_frame_ = 2048;
    std::atomic<bool> generating_{false};
    std::mutex worker_mutex_;
    std::condition_variable worker_finished_;
    bool worker_active_ = false;
};

}  // namespace fim::app
