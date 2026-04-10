#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fim::engine {

// Loads a WAV file and returns its samples as float in [-1.0, 1.0]. If the
// file has multiple channels, only the first channel is returned. Supports
// whatever bit depths dr_wav supports (8-bit, 16-bit, 24-bit PCM, 32-bit
// float). Returns std::nullopt if the file cannot be opened, parsed, or
// fully read.
std::optional<std::vector<float>> LoadWavMono(const std::string& path);

}  // namespace fim::engine
