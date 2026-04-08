#pragma once

#include <QObject>
#include <QString>
#include <atomic>

namespace fim::app {

// Phase 2 orchestration of single-wav generation. Holds the chosen input
// file, exposes a Generate() slot that queues a QRunnable on the global
// thread pool, and emits Qt signals as the work progresses. The actual
// "DSP" in Phase 2 is StubBankWriter::WriteSineBank — Phase 3 swaps in real
// audio resynthesis without changing this class's API.
class SingleWavService : public QObject {
    Q_OBJECT

public:
    explicit SingleWavService(QObject* parent = nullptr);
    ~SingleWavService() override = default;

    // Sets the input audio file path. No validation here — just stores it.
    void SetInputFile(const QString& path);
    QString InputFile() const;

    // Sets the output directory where 1.wav..8.wav will land.
    void SetOutputDirectory(const QString& path);
    QString OutputDirectory() const;

    // True if Generate() has been called and is still running.
    bool IsGenerating() const;

public slots:
    // Queues the generation work on QThreadPool::globalInstance(). Returns
    // immediately. Progress and completion are reported via signals.
    // Calling Generate() while already generating is a no-op.
    void Generate();

signals:
    // Emitted from the worker thread. Cross-thread signal/slot connections
    // become Qt::QueuedConnection automatically when the receiver lives in a
    // different thread (the GUI thread, for AnyWavScreen).
    void progressChanged(int percent);
    void generationFinished();
    void generationFailed(const QString& error);

private:
    QString input_file_;
    QString output_directory_;
    std::atomic<bool> generating_{false};
};

}  // namespace fim::app
