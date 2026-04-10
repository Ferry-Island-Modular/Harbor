// Single translation unit that instantiates the implementation portions of
// the vendored single-header libraries. Each #define MUST appear exactly
// once in the entire program; this file is the only place they live.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
