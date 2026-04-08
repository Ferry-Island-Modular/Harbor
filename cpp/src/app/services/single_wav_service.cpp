#include "app/services/single_wav_service.h"

#include <QThreadPool>
#include <chrono>
#include <filesystem>
#include <thread>

#include "app/services/stub_bank_writer.h"

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

bool SingleWavService::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void SingleWavService::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    // Capture the output directory by value into the lambda; the QObject's
    // signals are emitted via Qt's queued-connection machinery so it's safe
    // to call them from the worker thread.
    const QString out_dir = output_directory_;

    QThreadPool::globalInstance()->start([this, out_dir]() {
        constexpr int kSteps = 8;
        for (int i = 0; i < kSteps; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(125));
            const int pct = static_cast<int>((i + 1) * 100.0f / kSteps);
            emit progressChanged(pct);
        }

        const std::filesystem::path dir(out_dir.toStdString());
        const bool ok = StubBankWriter::WriteSineBank(dir);

        generating_.store(false, std::memory_order_release);

        if (ok) {
            emit generationFinished();
        } else {
            emit generationFailed(QString("Failed to write placeholder bank to %1").arg(out_dir));
        }
    });
}

}  // namespace fim::app
