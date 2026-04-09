#pragma once

#include <QObject>
#include <QString>
#include <atomic>

#include "dsp/generate_options.h"

namespace fim::app {

// Phase 2/3 orchestration of single-wav generation. Holds the chosen
// input file and the selected morph options, queues the generation work
// on QThreadPool, and emits Qt signals as the work progresses. Phase 3b
// swapped in real DSP via fim::dsp::SingleWavGenerator; Phase 3d added
// Y/Z mode selection.
class SingleWavService : public QObject {
    Q_OBJECT

public:
    explicit SingleWavService(QObject* parent = nullptr);
    ~SingleWavService() override = default;

    void SetInputFile(const QString& path);
    QString InputFile() const;

    void SetOutputDirectory(const QString& path);
    QString OutputDirectory() const;

    // Configure the morph modes used by the next Generate() call. These
    // persist across Generate() calls until explicitly changed.
    void SetYMode(fim::dsp::YMode mode);
    void SetZMode(fim::dsp::ZMode mode);

    bool IsGenerating() const;

public slots:
    void Generate();

signals:
    void progressChanged(int percent);
    void generationFinished();
    void generationFailed(const QString& error);

private:
    QString input_file_;
    QString output_directory_;
    fim::dsp::GenerateOptions options_;
    std::atomic<bool> generating_{false};
};

}  // namespace fim::app
