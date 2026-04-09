#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <filesystem>
#include <functional>

namespace fim::app {

// Abstract base for wavetable generation services. Each mode (single-wav,
// Serum, three-wav) has its own concrete subclass that plugs in a specific
// DSP generator via the DoGenerate virtual. The base class owns the
// common scaffolding: input/output path storage, the "generating" atomic,
// the QThreadPool dispatch, and the three Qt signals that the UI connects
// to.
//
// Lifecycle:
//   1. Construct (owned by a QObject parent, typically the mode screen).
//   2. SetInputFile / SetOutputDirectory as the user provides values.
//   3. Subclass configures its own mode-specific options (e.g. Y/Z morph
//      selections via its own setters).
//   4. Generate() is called — the base class queues work on the global
//      thread pool and calls DoGenerate from the worker thread.
//   5. Signals fire for progress and completion on the GUI thread via
//      Qt's auto-queued cross-thread signals.
class GenerateServiceBase : public QObject {
    Q_OBJECT

public:
    explicit GenerateServiceBase(QObject* parent = nullptr);
    ~GenerateServiceBase() override = default;

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
    // Type of the progress callback passed to DoGenerate. Subclasses call
    // this from inside their DSP work to report progress.
    using ProgressCallback = std::function<void(int percent)>;

    // Subclass implementation of the actual DSP work. Called from the
    // worker thread. Return true on success, false on error. The service
    // base class emits the appropriate finished/failed signals based on
    // the return value. The progress_cb should be invoked at intervals
    // with values in [0, 100].
    //
    // The subclass is responsible for reading any mode-specific state
    // it needs (e.g. cached options) at the start of DoGenerate. The
    // worker thread runs concurrently with the GUI thread so the subclass
    // should snapshot its state atomically if needed.
    virtual bool DoGenerate(const std::filesystem::path& input, const std::filesystem::path& output,
                            const ProgressCallback& progress_cb) = 0;

private:
    QString input_file_;
    QString output_directory_;
    QString preview_cache_directory_;
    int samples_per_frame_ = 2048;
    std::atomic<bool> generating_{false};
};

}  // namespace fim::app
