#pragma once

#include <QString>
#include <array>

#include "app/services/generate_service_base.h"

namespace fim::app {

// Three-wav mode wavetable service. Replaces the single input file from
// GenerateServiceBase with an array of 3 paths. The inherited
// SetInputFile(path) from the base class is unused for three-wav —
// subclass just doesn't call it.
class ThreeWavService : public GenerateServiceBase {
    Q_OBJECT

public:
    explicit ThreeWavService(QObject* parent = nullptr);
    ~ThreeWavService() override = default;

    // Set one of the 3 input file slots (0..2). Replaces any previously-
    // set file at that slot.
    void SetInputFileAt(int slot, const QString& path);
    QString InputFileAt(int slot) const;

    // True when all 3 slots are populated with non-empty paths.
    bool AllFilesSet() const;

protected:
    GenerationTask CreateGenerationTask() const override;

private:
    std::array<QString, 3> files_;
};

}  // namespace fim::app
