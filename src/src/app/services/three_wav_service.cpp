#include "app/services/three_wav_service.h"

#include <array>
#include <filesystem>

#include "dsp/three_wav_generator.h"

namespace fim::app {

ThreeWavService::ThreeWavService(QObject* parent) : GenerateServiceBase(parent) {}

void ThreeWavService::SetInputFileAt(int slot, const QString& path) {
    if (slot < 0 || slot >= 3) {
        return;
    }
    files_[slot] = path;
}

QString ThreeWavService::InputFileAt(int slot) const {
    if (slot < 0 || slot >= 3) {
        return QString();
    }
    return files_[slot];
}

bool ThreeWavService::AllFilesSet() const {
    for (const auto& f : files_) {
        if (f.isEmpty()) {
            return false;
        }
    }
    return true;
}

bool ThreeWavService::DoGenerate(const std::filesystem::path& /*input*/,
                                 const std::filesystem::path& output,
                                 const ProgressCallback& progress_cb) {
    // Ignore the base-class input parameter — three-wav uses its own
    // 3-slot array instead. Snapshot it for thread safety.
    std::array<std::filesystem::path, 3> input_paths;
    for (std::size_t i = 0; i < 3; ++i) {
        input_paths[i] = std::filesystem::path(files_[i].toStdString());
    }

    fim::dsp::ThreeWavGenerator generator;
    return generator.Generate(input_paths, output, progress_cb);
}

}  // namespace fim::app
