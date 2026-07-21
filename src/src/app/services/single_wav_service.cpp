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

GenerateServiceBase::GenerationTask SingleWavService::CreateGenerationTask() const {
    const fim::dsp::GenerateOptions opts = options_;
    return [opts](const std::filesystem::path& input, const std::filesystem::path& output,
                  const ProgressCallback& progress_cb) {
        fim::dsp::SingleWavGenerator generator;
        return generator.Generate(input, output, opts, progress_cb);
    };
}

}  // namespace fim::app
