#include "app/services/serum_wav_service.h"

namespace fim::app {

SerumWavService::SerumWavService(QObject* parent) : GenerateServiceBase(parent) {}

void SerumWavService::SetYMode(fim::dsp::SerumMode mode) {
    options_.y_mode = mode;
}

void SerumWavService::SetZMode(fim::dsp::SerumMode mode) {
    options_.z_mode = mode;
}

bool SerumWavService::DoGenerate(const std::filesystem::path& input,
                                 const std::filesystem::path& output,
                                 const ProgressCallback& progress_cb) {
    const fim::dsp::SerumGenerateOptions opts = options_;
    fim::dsp::SerumGenerator generator;
    return generator.Generate(input, output, opts, progress_cb);
}

}  // namespace fim::app
