#pragma once

#include "app/services/generate_service_base.h"
#include "dsp/serum_generator.h"

namespace fim::app {

// Serum mode wavetable service. Inherits the common scaffolding from
// GenerateServiceBase and adds Y/Z morph mode setters plus a cached
// SerumGenerateOptions for the DSP generator.
class SerumWavService : public GenerateServiceBase {
    Q_OBJECT

public:
    explicit SerumWavService(QObject* parent = nullptr);
    ~SerumWavService() override = default;

    // Configure the Serum morph modes used by the next Generate() call.
    void SetYMode(fim::dsp::SerumMode mode);
    void SetZMode(fim::dsp::SerumMode mode);

protected:
    GenerationTask CreateGenerationTask() const override;

private:
    fim::dsp::SerumGenerateOptions options_;
};

}  // namespace fim::app
