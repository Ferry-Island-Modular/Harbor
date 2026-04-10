#pragma once

#include <cstddef>
#include <vector>

namespace fim::dsp {

// Crush parameters: a target bit depth and a sample-hold factor.
// bit_depth is the number of bits of amplitude quantization (values >= 16
// are effectively full-resolution float passthrough). sample_hold >= 1 is
// the number of consecutive output samples that share the same value —
// sample_hold = 4 holds each sample for 4 output positions (a 4x
// sample-rate reduction).
struct ZCrushParams {
    int bit_depth;
    int sample_hold;
};

// Apply bit-depth quantization and sample-rate reduction to a cycle in
// place. Quantization rounds each sample to the nearest representable
// level in (2^bit_depth) evenly-spaced values across [-1, 1]. Sample hold
// overwrites `sample_hold - 1` out of every `sample_hold` samples with
// the preceding hold-value.
//
// When bit_depth >= 16 and sample_hold == 1, this is a no-op.
void ZCrush(std::vector<float>& cycle, int bit_depth, int sample_hold);

// Map a Z parameter (0..7) to crush parameters for the wavetable use case.
// z=0 is effectively passthrough (bit_depth=16, hold=1). z=7 is maximal
// crush (bit_depth=3, hold=8). Intermediate z values interpolate linearly.
ZCrushParams ZCrushAmount(int z);

}  // namespace fim::dsp
