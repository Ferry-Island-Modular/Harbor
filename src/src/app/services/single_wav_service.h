#pragma once

#include "app/services/generate_service_base.h"
#include "dsp/generate_options.h"

namespace fim::app {

// Single-wav wavetable service. Inherits the common scaffolding from
// GenerateServiceBase and adds Y/Z morph mode setters plus a cached
// GenerateOptions for the DSP generator.
class SingleWavService : public GenerateServiceBase {
    Q_OBJECT

public:
    explicit SingleWavService(QObject* parent = nullptr);
    ~SingleWavService() override = default;

    // Configure the morph modes used by the next Generate() call.
    void SetYMode(fim::dsp::YMode mode);
    void SetZMode(fim::dsp::ZMode mode);

protected:
    GenerationTask CreateGenerationTask() const override;

private:
    fim::dsp::GenerateOptions options_;
};

}  // namespace fim::app
