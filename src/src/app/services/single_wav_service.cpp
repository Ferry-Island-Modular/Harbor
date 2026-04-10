#include "app/services/single_wav_service.h"

#include "dsp/single_wav_generator.h"

namespace fim::app {

SingleWavService::SingleWavService(QObject* parent) : GenerateServiceBase(parent) {}

void SingleWavService::SetYMode(fim::dsp::YMode mode) {
    options_.y_mode = mode;
}

void SingleWavService::SetZMode(fim::dsp::ZMode mode) {
    options_.z_mode = mode;
}

bool SingleWavService::DoGenerate(const std::filesystem::path& input,
                                  const std::filesystem::path& output,
                                  const ProgressCallback& progress_cb) {
    // Snapshot options at the start — the UI thread may change them
    // while the worker runs, but we want the in-flight generation to use
    // a consistent set.
    const fim::dsp::GenerateOptions opts = options_;

    fim::dsp::SingleWavGenerator generator;
    return generator.Generate(input, output, opts, progress_cb);
}

}  // namespace fim::app
