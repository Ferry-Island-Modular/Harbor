// Phase 0 smoke test: prove that wavetable_oscillator.h compiles and the
// WavetableOscillator template instantiates on all three compilers.
// This file contains no runtime code — the static_assert is the whole test.

#include "wavetable_oscillator.h"

namespace {

// Dummy instantiation: 2048-sample wavetable, no sync, no modulation.
// If the template fails to instantiate, the build fails.
using SmokeOscillator = fourseas::WavetableOscillator<2048, false, false>;

static_assert(sizeof(SmokeOscillator) > 0,
              "WavetableOscillator template must instantiate with size=2048");

// Suppress unused-variable warnings while still forcing full instantiation.
[[maybe_unused]] SmokeOscillator g_smoke_oscillator;

}  // namespace
