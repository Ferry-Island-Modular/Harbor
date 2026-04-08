#pragma once

#include <cstddef>
#include <vector>

namespace fim::dsp {

// Generates a symmetric Hann window of length `length`. Matches
// scipy.signal.windows.hann(length) with its default sym=True parameter.
//
// Formula: w[n] = 0.5 * (1 - cos(2*pi*n / (length - 1))) for n in [0, length).
//
// Notes:
// - For length == 0, returns an empty vector.
// - For length == 1, returns {0.0f} — the formula would divide by zero, so
//   this edge case is handled explicitly. (scipy returns {1.0f} for this;
//   we choose 0.0f because the window is only ever used with length >> 1
//   and the choice is irrelevant in practice.)
// - The peak value is at the middle of the window. For even lengths, the
//   peak is between indices (length/2 - 1) and (length/2), neither exactly
//   1.0. This is the standard symmetric Hann definition.
std::vector<float> HannWindow(std::size_t length);

}  // namespace fim::dsp
