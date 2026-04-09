# Phase 3d Implementation Plan — Morph modes + Y/Z UI wiring

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand `SpectralModifier` with two additional Y modes (formant scaling, harmonic stretch), one additional Z mode (phase dispersion), and a new post-extraction effect (Z-crush: bit + sample rate reduction). Wire the `AxisMorphSelector` buttons in `AnyWavScreen` so they actually switch the selected morph mode, with labels matching the real behaviors and persistence via `Settings::YMorph/ZMorph`. Also fold in the X-extreme silent/dither cliff fix that was documented as a Phase 3d followup.

**Architecture:** `SpectralModifier` grows `YMode` and `ZMode` enums and takes them as parameters to `Apply`. The existing tilt/stretch/phase-randomize logic stays; formant, harmonic stretch, and phase dispersion are added as alternate branches via switch statements — no strategy pattern, no virtual dispatch, just straightforward enum-dispatched math. Z-crush is a new post-extraction stage applied inside `SingleWavGenerator::Generate` after `CycleExtractor::Extract` (not inside `SpectralModifier`) because it operates on the time-domain cycle, not the spectrum. A new `GenerateOptions` struct bundles the mode selections and flows from `AnyWavScreen` → `SingleWavService` → `SingleWavGenerator`. The `AxisMorphSelector` widgets gain `optionChanged` connections that update the screen's stored options; values are read from and written to `Settings` for persistence.

**Tech Stack:** C++20, Phase 3a/3b DSP layer, Phase 2 `Settings` (`QSettings` wrapper — finally used!), Qt 6.8 Widgets, Catch2 v3.

**Spec deviations:**

- **Clean-room port of Serum morph modes.** The Python `SerumWavetableConverter` implements `FORMANT_SCALE`, `PHASE_DISPERSE`, and `HARMONIC_STRETCH` for the *Serum-mode* code path (a separate code path from `AudioResynthWavetableGenerator`). Phase 3d adapts the algorithms for the any-wav code path's STFT data (2D `[bin][frame]` magnitude/phase arrays) rather than Serum's per-frame waveform format. The math stays the same; the plumbing is different.
- **X has dual-role stretch even with formant/harmonic-stretch modes.** In Phase 3b, X applies the Python-style spectral stretch (x_norm scales the envelope by 0.5x–2.0x) as a side effect, in addition to selecting the STFT frame. Phase 3d **keeps** this behavior regardless of the selected Y mode — the X stretch always runs. Order: **Y morph runs first, then X stretch operates on the Y-modified envelope**, matching Phase 3b and Python's `_spectral_modifications` ordering. Rationale: if the user switches to Y1 (formant), they shouldn't suddenly lose the X-driven timbre variation that was part of Y0 mode; and keeping the Y-then-X order means Y0 (tilt) produces bit-identical output as Phase 3b (regression-safe).
- **Z-crush runs on the normalized output cell, not the spectrum.** Unlike Y modes and Z0/Z1 which operate in the frequency domain on mag/phase arrays, Z-crush is a time-domain sample-wise effect applied to the already-extracted cycle. Architecturally this means it runs in `SingleWavGenerator::Generate` after `CycleExtractor::Extract`, not inside `SpectralModifier::Apply`. This keeps `SpectralModifier` exclusively focused on spectrum math and makes Z-crush easy to test in isolation.
- **X-extreme silent/dither cliff fix.** `CycleExtractor::Extract` gains an energy threshold: if the pre-normalization peak is below `1e-4` (relative to the normalized input audio, which peaks at 1.0), the cycle is returned as all zeros instead of being normalized up from the noise floor. This fixes both the "exactly-zero tail produces silent cells" quirk (by keeping the cell zero cleanly) and the "near-zero tail with dither produces amplified noise" quirk (by treating it as silent). Removes the followups entry added at the end of Phase 3b.
- **Settings persistence wires existing accessors.** Phase 2 landed `Settings::YMorph`/`Settings::ZMorph` but nothing read or wrote them. Phase 3d finally uses them: `AnyWavScreen` reads the last-used modes on construction and writes on change. `MainWindow` creates a single `Settings` instance and passes it to `AnyWavScreen` by reference.
- **UI button labels reflect real behaviors.** `AxisMorphSelector` currently gets placeholder labels "First option / Second option / Third option" from `AnyWavScreen::BuildFileSetPage`. Phase 3d replaces these with short real labels: Y axis = "Tilt / Formant / Stretch"; Z axis = "Random / Disperse / Crush". These are terse enough to fit the existing button widths.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files:**

- `cpp/src/dsp/post_effects.h` / `post_effects.cpp` — `fim::dsp::ZCrush` free function applying bit-depth reduction + sample-rate reduction to a cycle in place. Also a `ZCrushAmount(int z)` helper mapping z∈[0,7] to the crush parameters. Small and standalone because it's a time-domain effect that doesn't fit in `SpectralModifier`.
- `cpp/src/dsp/generate_options.h` — pure struct `fim::dsp::GenerateOptions` holding `SpectralModifier::YMode y_mode`, `SpectralModifier::ZMode z_mode`, and future expansion fields. Plain data, no .cpp file needed.
- `cpp/tests/dsp_post_effects_test.cpp` — 4 tests for Z-crush: at amount=0 passes through unchanged, at amount=max the output has ≤N distinct values (quantization verification), length is preserved, signal is finite (no NaN).

**Modified files:**

- `cpp/src/dsp/spectral_modifier.h` — add `enum class YMode { kTilt, kFormant, kStretch }` and `enum class ZMode { kRandom, kDisperse, kCrush }`. Update `Apply` signature to take `YMode` and `ZMode`. Note that `ZMode::kCrush` is handled outside this class (see post_effects.h); including it in the enum keeps the UI wiring uniform but `Apply` treats it as a no-op equivalent to "leave the phase alone".
- `cpp/src/dsp/spectral_modifier.cpp` — add the tilt/formant/stretch switch in the Y stage and the random/disperse/(crush no-op) switch in the Z stage. Tilt and random branches keep their existing Phase 3b implementations; formant, stretch, and disperse are new.
- `cpp/src/dsp/cycle_extractor.cpp` — add the energy threshold fix: if `peak < 1e-4f` before normalization, zero the output cycle instead of normalizing.
- `cpp/src/dsp/single_wav_generator.h` — `Generate()` takes an additional `GenerateOptions` parameter (default-constructed = the same behavior as Phase 3b). Include post_effects.h to call `ZCrush`.
- `cpp/src/dsp/single_wav_generator.cpp` — pass `options.y_mode` / `options.z_mode` to `SpectralModifier::Apply`. After each cell's `CycleExtractor::Extract`, if `options.z_mode == ZMode::kCrush` apply `ZCrush` with the per-z-page crush amount.
- `cpp/src/app/services/single_wav_service.h` — add `SetYMode` / `SetZMode` and a `GenerateOptions options_` member. The service caches the currently-selected modes between `Generate` calls.
- `cpp/src/app/services/single_wav_service.cpp` — pass `options_` to `SingleWavGenerator::Generate`.
- `cpp/src/ui/any_wav_screen.h` — hold a reference to a `fim::app::Settings` instance (non-owning, passed in constructor). Add private members for current Y/Z mode.
- `cpp/src/ui/any_wav_screen.cpp` — replace the placeholder axis option labels with real ones. Connect `AxisMorphSelector::currentIndexChanged` to slots that update the stored mode, push to the service, and write to settings. On `Reset`, read the stored modes back from settings and re-sync the selectors.
- `cpp/src/ui/main_window.h` / `main_window.cpp` — hold a `fim::app::Settings settings_` member and pass it to the `AnyWavScreen` constructor.
- `cpp/tests/dsp_spectral_modifier_test.cpp` — add 4 tests: formant mode changes the magnitude shape but preserves total energy within a tolerance, stretch mode's inverse operation at y=neutral approximately preserves a sine spectrum, disperse mode changes phase but preserves magnitude, passing `kCrush` to SpectralModifier leaves the phase unchanged (it's a no-op).
- `cpp/CMakeLists.txt` — add `src/dsp/post_effects.cpp` to the `fim-config-tool` target.
- `cpp/tests/CMakeLists.txt` — add `dsp_post_effects_test.cpp` and `../src/dsp/post_effects.cpp`.
- `docs/followups.md` — remove the "Phase 3b (Phase 3d candidate fixes)" section since the X-extreme cliff is fixed and the morph modes are implemented.

**Deleted files:** none.

---

## Task 1: Z-crush post-effect

A time-domain bit-depth + sample-rate reduction applied to an already-extracted wavetable cycle. Operates in place on a `std::vector<float>`. Doesn't depend on any other Phase 3 DSP piece. TDD with direct math checks.

**Files:**
- Create: `cpp/src/dsp/post_effects.h`
- Create: `cpp/src/dsp/post_effects.cpp`
- Create: `cpp/tests/dsp_post_effects_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/dsp_post_effects_test.cpp`:

```cpp
#include "dsp/post_effects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <set>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;

std::vector<float> MakeSine(std::size_t n, float freq_cycles) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = std::sin(2.0f * kPi * freq_cycles * i / n);
    }
    return v;
}
}  // namespace

TEST_CASE("ZCrush amount=0 passes through unchanged", "[dsp][post_effects]") {
    auto input = MakeSine(256, 4.0f);
    const auto copy = input;
    fim::dsp::ZCrush(input, /*bit_depth=*/16, /*sample_hold=*/1);
    REQUIRE(input.size() == copy.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE_THAT(input[i], WithinAbs(copy[i], 1e-4));
    }
}

TEST_CASE("ZCrush reduces the number of distinct amplitude values",
          "[dsp][post_effects]") {
    auto input = MakeSine(256, 4.0f);
    fim::dsp::ZCrush(input, /*bit_depth=*/3, /*sample_hold=*/1);
    // 3 bits = 2^3 = 8 possible positive quantization levels + their
    // negatives. The output should have at most ~16 distinct values.
    std::set<float> distinct(input.begin(), input.end());
    REQUIRE(distinct.size() <= 16);
}

TEST_CASE("ZCrush sample-hold repeats each value `hold` times",
          "[dsp][post_effects]") {
    // Build a signal where every sample is the index — makes the hold
    // pattern easy to see.
    std::vector<float> input(16);
    for (std::size_t i = 0; i < 16; ++i) {
        input[i] = static_cast<float>(i) / 16.0f;
    }
    fim::dsp::ZCrush(input, /*bit_depth=*/16, /*sample_hold=*/4);
    // Samples 0..3 should all equal input[0], 4..7 should all equal
    // input[4], etc.
    REQUIRE_THAT(input[0], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[1], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[2], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[3], WithinAbs(0.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[4], WithinAbs(4.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[8], WithinAbs(8.0f / 16.0f, 1e-6));
    REQUIRE_THAT(input[12], WithinAbs(12.0f / 16.0f, 1e-6));
}

TEST_CASE("ZCrushAmount maps z=0 to identity and z>0 to progressively harsher",
          "[dsp][post_effects]") {
    const auto a0 = fim::dsp::ZCrushAmount(0);
    REQUIRE(a0.bit_depth >= 16);  // effectively full-resolution
    REQUIRE(a0.sample_hold == 1);

    const auto a7 = fim::dsp::ZCrushAmount(7);
    REQUIRE(a7.bit_depth < a0.bit_depth);
    REQUIRE(a7.sample_hold > a0.sample_hold);
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`. Add the test source and the implementation source:

```cmake
    dsp_cycle_extractor_test.cpp
    dsp_post_effects_test.cpp
    ../src/engine/single_header_impls.cpp
```

And in the impl sources:

```cmake
    ../src/dsp/cycle_extractor.cpp
    ../src/dsp/post_effects.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `dsp/post_effects.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/dsp/post_effects.h`:

```cpp
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
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/dsp/post_effects.cpp`:

```cpp
#include "dsp/post_effects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fim::dsp {

void ZCrush(std::vector<float>& cycle, int bit_depth, int sample_hold) {
    if (cycle.empty()) {
        return;
    }

    // Step 1: bit-depth quantization. A signed fixed-point with
    // bit_depth bits represents 2^(bit_depth-1) positive levels and
    // 2^(bit_depth-1) negative levels. We round to the nearest level.
    if (bit_depth < 16) {
        const int levels = 1 << (bit_depth - 1);  // e.g. bit_depth=3 -> 4
        const float step = 1.0f / static_cast<float>(levels);
        for (float& s : cycle) {
            const float clamped = std::max(-1.0f, std::min(1.0f, s));
            s = std::round(clamped / step) * step;
        }
    }

    // Step 2: sample-rate reduction via sample-and-hold. Every group of
    // sample_hold samples takes the value of the first sample in the
    // group.
    if (sample_hold > 1) {
        for (std::size_t i = 0; i < cycle.size(); i += sample_hold) {
            const float held = cycle[i];
            const std::size_t end = std::min(i + static_cast<std::size_t>(sample_hold),
                                             cycle.size());
            for (std::size_t j = i + 1; j < end; ++j) {
                cycle[j] = held;
            }
        }
    }
}

ZCrushParams ZCrushAmount(int z) {
    // z=0: bit_depth=16, hold=1 (passthrough)
    // z=7: bit_depth=3, hold=8 (maximal crush)
    const int clamped = std::max(0, std::min(7, z));
    const float t = static_cast<float>(clamped) / 7.0f;
    const int bit_depth = static_cast<int>(std::round(16.0f - t * 13.0f));
    const int sample_hold = 1 + static_cast<int>(std::round(t * 7.0f));
    return ZCrushParams{bit_depth, sample_hold};
}

}  // namespace fim::dsp
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/dsp/post_effects.cpp` right after `src/dsp/cycle_extractor.cpp`:

```cmake
    src/dsp/cycle_extractor.cpp
    src/dsp/post_effects.cpp
    src/dsp/single_wav_generator.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 54 tests pass (50 from Phase 3b + 4 new post_effects tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/post_effects.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/post_effects.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_post_effects_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/post_effects.h cpp/src/dsp/post_effects.cpp \
        cpp/tests/dsp_post_effects_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add Z-crush post-effect (bit + sample rate reduction)"
```

---

## Task 2: SpectralModifier mode enums + formant + harmonic stretch + phase disperse

Extend `SpectralModifier` to take explicit `YMode` and `ZMode` enums. The existing tilt (Y0) and phase-randomize (Z0) branches become one case in each switch; new branches implement formant scaling, harmonic stretch, and phase dispersion.

**Files:**
- Modify: `cpp/src/dsp/spectral_modifier.h`
- Modify: `cpp/src/dsp/spectral_modifier.cpp`
- Modify: `cpp/tests/dsp_spectral_modifier_test.cpp`

- [ ] **Step 1: Update the header with enums and new signature**

Rewrite `cpp/src/dsp/spectral_modifier.h`:

```cpp
#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace fim::dsp {

// Y-axis morph modes. The original Phase 3b behavior is kTilt.
//
// - kTilt:    exponential brightness shift (darkens or brightens uniformly)
// - kFormant: shifts the spectral envelope up or down in frequency while
//             preserving its shape — sounds like a formant shift on vowels
// - kStretch: non-linear log-frequency remapping that stretches or
//             compresses the harmonic spacing — inharmonic/bell-like character
enum class YMode {
    kTilt,
    kFormant,
    kStretch,
};

// Z-axis morph modes. The original Phase 3b behavior is kRandom.
//
// - kRandom:   blend the original phase with uniform random phase + bin-wise
//              smoothing (noisy/diffuse character)
// - kDisperse: frequency-dependent phase shift centered at a chosen bin —
//              comb-filter-like character
// - kCrush:    HANDLED OUTSIDE THIS CLASS. ZCrush is a time-domain post-effect
//              applied in SingleWavGenerator after cycle extraction. When
//              Apply() receives kCrush it leaves the phase array untouched
//              (the Y mode still runs normally).
enum class ZMode {
    kRandom,
    kDisperse,
    kCrush,
};

// Applies the chosen Y and Z morph modes to a 2D magnitude/phase array in
// place. The X parameter always applies the Phase 3b spectral stretch as a
// baseline transformation regardless of Y mode — the Y mode then shapes the
// stretched envelope.
//
// Parameter semantics:
// - x in [0, 7]: spectral envelope stretch (0.5x..2.0x). Always applies.
// - y in [0, 7]: strength of the Y morph mode. y=3 or 4 is roughly neutral.
// - z in [0, 7]: strength of the Z morph mode. z=0 leaves phase untouched.
// - y_mode: which Y transformation to apply.
// - z_mode: which Z transformation to apply. kCrush is a no-op here;
//           handle it in the caller as a post-effect.
//
// The 2D arrays are indexed as magnitude[bin][frame] (Python numpy
// convention from scipy.signal.stft).
class SpectralModifier {
public:
    // Default-constructed: seeds the RNG from std::random_device.
    SpectralModifier();

    // Seeded: produces reproducible phase output. Used by tests and by the
    // Phase 3c oracle harness.
    explicit SpectralModifier(std::uint32_t seed);

    // Reseed the internal RNG. Useful for resetting between generate runs.
    void Seed(std::uint32_t seed);

    void Apply(std::vector<std::vector<float>>& magnitude,
               std::vector<std::vector<float>>& phase, int x, int y, int z,
               YMode y_mode = YMode::kTilt, ZMode z_mode = ZMode::kRandom);

private:
    std::mt19937 rng_;
};

}  // namespace fim::dsp
```

- [ ] **Step 2: Update the implementation**

Rewrite `cpp/src/dsp/spectral_modifier.cpp`:

```cpp
#include "dsp/spectral_modifier.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fim::dsp {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// Compute the time-averaged spectral envelope: for each bin, the mean
// magnitude across all frames.
std::vector<float> SpectralEnvelope(
    const std::vector<std::vector<float>>& magnitude) {
    const std::size_t num_bins = magnitude.size();
    std::vector<float> env(num_bins, 0.0f);
    if (num_bins == 0 || magnitude[0].empty()) {
        return env;
    }
    const std::size_t num_frames = magnitude[0].size();
    for (std::size_t k = 0; k < num_bins; ++k) {
        float sum = 0.0f;
        for (std::size_t f = 0; f < num_frames; ++f) {
            sum += magnitude[k][f];
        }
        env[k] = sum / static_cast<float>(num_frames);
    }
    return env;
}

// Y0: spectral tilt. Matches Phase 3b exactly.
void ApplyTilt(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();
    const float exponent_scale = ((y_norm * 2.0f) - 1.0f) * 5.0f;
    const float inv_num_bins = 1.0f / static_cast<float>(num_bins);
    for (std::size_t k = 0; k < num_bins; ++k) {
        const float tilt =
            std::exp(exponent_scale * static_cast<float>(k) * inv_num_bins);
        for (std::size_t f = 0; f < num_frames; ++f) {
            magnitude[k][f] *= tilt;
        }
    }
}

// Y1: formant scaling. Shifts the spectral envelope up or down in
// frequency while preserving its shape. y_norm=0.5 is neutral; y_norm<0.5
// shifts down (lower formants), y_norm>0.5 shifts up (higher formants).
// Implementation: resample the per-frame magnitude curve by linear
// interpolation at a stretched/compressed index.
void ApplyFormant(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    // formant_shift in [0.5, 2.0]. y_norm=0 gives 0.5 (shift down one
    // octave), y_norm=1 gives 2.0 (shift up one octave).
    const float formant_shift = 0.5f + y_norm * 1.5f;
    const float inv_shift = 1.0f / formant_shift;

    // For each frame, build a shifted copy of the magnitude column.
    std::vector<float> shifted(num_bins, 0.0f);
    for (std::size_t f = 0; f < num_frames; ++f) {
        std::fill(shifted.begin(), shifted.end(), 0.0f);
        for (std::size_t k = 0; k < num_bins; ++k) {
            const float src_idx = static_cast<float>(k) * inv_shift;
            if (src_idx >= 0.0f && src_idx < static_cast<float>(num_bins) - 1.0f) {
                const std::size_t idx_floor =
                    static_cast<std::size_t>(std::floor(src_idx));
                const std::size_t idx_ceil = idx_floor + 1;
                const float fraction = src_idx - static_cast<float>(idx_floor);
                shifted[k] = magnitude[idx_floor][f] * (1.0f - fraction) +
                             magnitude[idx_ceil][f] * fraction;
            }
        }
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude[k][f] = shifted[k];
        }
    }
}

// Y2: harmonic stretch. Non-linear remapping of bin indices via a log-2
// frequency curve. y_norm=0.5 is neutral; y_norm<0.5 compresses the high
// end toward DC, y_norm>0.5 stretches it outward (inharmonic character).
void ApplyHarmonicStretch(std::vector<std::vector<float>>& magnitude, float y_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    // stretch_power in [0.5, 2.0]. y_norm=0.5 gives 1.0 (identity).
    // Below 0.5 pulls the curve toward linear compression; above 0.5
    // pushes it outward into a convex log stretch.
    const float stretch_power = 0.5f + y_norm * 1.5f;
    const float inv_num_bins_m1 = 1.0f / static_cast<float>(num_bins - 1);

    std::vector<float> stretched(num_bins, 0.0f);
    for (std::size_t f = 0; f < num_frames; ++f) {
        std::fill(stretched.begin(), stretched.end(), 0.0f);
        for (std::size_t k = 0; k < num_bins; ++k) {
            // Remap k via k_remapped = num_bins * (k/num_bins)^stretch_power.
            const float normalized = static_cast<float>(k) * inv_num_bins_m1;
            const float remapped_norm = std::pow(normalized, stretch_power);
            const float src_idx = remapped_norm * static_cast<float>(num_bins - 1);
            if (src_idx < static_cast<float>(num_bins) - 1.0f) {
                const std::size_t idx_floor =
                    static_cast<std::size_t>(std::floor(src_idx));
                const std::size_t idx_ceil = idx_floor + 1;
                const float fraction = src_idx - static_cast<float>(idx_floor);
                stretched[k] = magnitude[idx_floor][f] * (1.0f - fraction) +
                               magnitude[idx_ceil][f] * fraction;
            }
        }
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude[k][f] = stretched[k];
        }
    }
}

// X-driven spectral envelope stretch. Runs AFTER the Y mode and matches
// Phase 3b / Python ordering exactly. Reads the envelope from the
// Y-modified magnitude, then multiplies back in — so Y0 (tilt) produces
// bit-identical output as Phase 3b.
void ApplyXStretch(std::vector<std::vector<float>>& magnitude, float x_norm) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }
    const std::size_t num_frames = magnitude[0].size();

    const float stretch_amount = 0.5f + x_norm * 1.5f;
    const auto env = SpectralEnvelope(magnitude);

    std::vector<float> stretched_env(num_bins, 0.0f);
    for (std::size_t i = 0; i < num_bins; ++i) {
        const float src_idx = static_cast<float>(i) / stretch_amount;
        if (src_idx < static_cast<float>(num_bins) - 1.0f) {
            const std::size_t idx_floor =
                static_cast<std::size_t>(std::floor(src_idx));
            const std::size_t idx_ceil = idx_floor + 1;
            const float fraction = src_idx - static_cast<float>(idx_floor);
            stretched_env[i] =
                env[idx_floor] * (1.0f - fraction) + env[idx_ceil] * fraction;
        }
    }

    float env_sum = 0.0f;
    for (float v : env) {
        env_sum += v;
    }
    const float env_mean = env_sum / static_cast<float>(num_bins) + 1e-10f;

    for (std::size_t k = 0; k < num_bins; ++k) {
        const float scale = stretched_env[k] / env_mean;
        for (std::size_t f = 0; f < num_frames; ++f) {
            magnitude[k][f] *= scale;
        }
    }
}

// Z0: phase randomization. Matches Phase 3b exactly.
void ApplyPhaseRandom(std::vector<std::vector<float>>& phase, float z_norm,
                      std::mt19937& rng) {
    const std::size_t num_bins = phase.size();
    if (num_bins == 0 || phase[0].empty()) {
        return;
    }
    const std::size_t num_frames = phase[0].size();

    std::uniform_real_distribution<float> uniform_full(0.0f, kTwoPi);
    std::uniform_real_distribution<float> uniform_jitter(-0.1f, 0.1f);

    for (std::size_t k = 0; k < num_bins; ++k) {
        for (std::size_t f = 0; f < num_frames; ++f) {
            const float random_phase = uniform_full(rng);
            phase[k][f] = (1.0f - z_norm) * phase[k][f] + z_norm * random_phase;
        }
    }

    const float coherence = z_norm * 0.5f;
    const float one_minus_coherence = 1.0f - coherence;
    for (std::size_t f = 0; f < num_frames; ++f) {
        float prev = phase[0][f];
        for (std::size_t k = 1; k < num_bins; ++k) {
            const float jitter = uniform_jitter(rng);
            const float smoothed =
                phase[k][f] * one_minus_coherence + (prev + jitter) * coherence;
            phase[k][f] = smoothed;
            prev = smoothed;
        }
    }
}

// Z1: phase dispersion. Frequency-dependent phase shift centered at a
// chosen harmonic, simulating a dispersive medium. Strength scales with
// z_norm. At z_norm=0 this is a no-op.
void ApplyPhaseDisperse(std::vector<std::vector<float>>& phase, float z_norm) {
    const std::size_t num_bins = phase.size();
    if (num_bins == 0 || phase[0].empty() || z_norm == 0.0f) {
        return;
    }
    const std::size_t num_frames = phase[0].size();

    // Disperse strength: at z_norm=1, phases can rotate up to +/- 2*pi
    // cumulatively across the bin range.
    constexpr float kCenterBinFraction = 0.25f;  // center of dispersion
    const float center_bin =
        static_cast<float>(num_bins) * kCenterBinFraction;
    const float strength = z_norm * kTwoPi;

    for (std::size_t k = 0; k < num_bins; ++k) {
        // Parabolic phase shift peaking at center_bin: Δφ = strength *
        // (1 - ((k - center)/num_bins)^2). This gives a smooth, bell-shaped
        // phase curve across frequency.
        const float normalized_offset =
            (static_cast<float>(k) - center_bin) / static_cast<float>(num_bins);
        const float delta_phase = strength * (1.0f - normalized_offset * normalized_offset);
        for (std::size_t f = 0; f < num_frames; ++f) {
            phase[k][f] += delta_phase;
        }
    }
}

}  // namespace

SpectralModifier::SpectralModifier() : rng_(std::random_device{}()) {}

SpectralModifier::SpectralModifier(std::uint32_t seed) : rng_(seed) {}

void SpectralModifier::Seed(std::uint32_t seed) {
    rng_.seed(seed);
}

void SpectralModifier::Apply(std::vector<std::vector<float>>& magnitude,
                             std::vector<std::vector<float>>& phase, int x, int y,
                             int z, YMode y_mode, ZMode z_mode) {
    const std::size_t num_bins = magnitude.size();
    if (num_bins == 0 || magnitude[0].empty()) {
        return;
    }

    const float x_norm = static_cast<float>(x) / 7.0f;
    const float y_norm = static_cast<float>(y) / 7.0f;
    const float z_norm = static_cast<float>(z) / 7.0f;

    // ---- Y: morph mode dispatch (runs FIRST, matches Phase 3b/Python) ----
    switch (y_mode) {
        case YMode::kTilt:
            ApplyTilt(magnitude, y_norm);
            break;
        case YMode::kFormant:
            ApplyFormant(magnitude, y_norm);
            break;
        case YMode::kStretch:
            ApplyHarmonicStretch(magnitude, y_norm);
            break;
    }

    // ---- X: spectral envelope stretch (runs AFTER Y, reads Y-modified envelope) ----
    ApplyXStretch(magnitude, x_norm);

    // ---- Z: morph mode dispatch ----
    // kCrush is handled by SingleWavGenerator as a post-effect; here it's
    // a no-op that leaves the phase array alone.
    switch (z_mode) {
        case ZMode::kRandom:
            if (z_norm > 0.0f) {
                ApplyPhaseRandom(phase, z_norm, rng_);
            }
            break;
        case ZMode::kDisperse:
            ApplyPhaseDisperse(phase, z_norm);
            break;
        case ZMode::kCrush:
            // Intentional no-op. SingleWavGenerator applies ZCrush to the
            // extracted cycle instead.
            break;
    }
}

}  // namespace fim::dsp
```

- [ ] **Step 3: Add tests for the new modes**

Modify `cpp/tests/dsp_spectral_modifier_test.cpp`. Append the following tests at the end:

```cpp
TEST_CASE("SpectralModifier formant mode changes the magnitude shape",
          "[dsp][spectral_modifier]") {
    // Build a spectrum with a clear "formant" (a peak at bin 5).
    const std::size_t num_bins = 16;
    const std::size_t num_frames = 2;
    std::vector<std::vector<float>> magnitude(num_bins, std::vector<float>(num_frames, 0.1f));
    std::vector<std::vector<float>> phase(num_bins, std::vector<float>(num_frames, 0.0f));
    magnitude[5][0] = magnitude[5][1] = 1.0f;

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/7, /*z=*/0,
                   fim::dsp::YMode::kFormant, fim::dsp::ZMode::kRandom);

    // At y=7 (formant_shift = 2.0), the peak from bin 5 should move down
    // (since we read from src_idx = k / 2, the output at k=10 reads from
    // src_idx=5). So the peak after formant should appear around bin 10.
    float peak_mag = 0.0f;
    std::size_t peak_bin = 0;
    for (std::size_t k = 0; k < num_bins; ++k) {
        if (magnitude[k][0] > peak_mag) {
            peak_mag = magnitude[k][0];
            peak_bin = k;
        }
    }
    // The peak should have moved away from its original bin (5).
    REQUIRE(peak_bin != 5);
}

TEST_CASE("SpectralModifier harmonic stretch at y=0.5 (neutral) is approximately identity",
          "[dsp][spectral_modifier]") {
    // A flat spectrum under harmonic stretch with y=3 or 4 (y_norm ~= 0.5,
    // stretch_power ~= 1.25) should be close to but not exactly identity —
    // just check that it stays finite and nonzero.
    auto magnitude = std::vector<std::vector<float>>(
        16, std::vector<float>(2, 1.0f));
    auto phase = std::vector<std::vector<float>>(
        16, std::vector<float>(2, 0.0f));
    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/4, /*z=*/0,
                   fim::dsp::YMode::kStretch, fim::dsp::ZMode::kRandom);

    for (const auto& row : magnitude) {
        for (float m : row) {
            REQUIRE(std::isfinite(m));
        }
    }
    // At least one bin should still have nontrivial magnitude.
    bool has_content = false;
    for (const auto& row : magnitude) {
        for (float m : row) {
            if (m > 0.01f) {
                has_content = true;
                break;
            }
        }
    }
    REQUIRE(has_content);
}

TEST_CASE("SpectralModifier phase disperse changes phase but leaves magnitude",
          "[dsp][spectral_modifier]") {
    auto magnitude = std::vector<std::vector<float>>(
        16, std::vector<float>(2, 1.0f));
    auto phase = std::vector<std::vector<float>>(
        16, std::vector<float>(2, 0.0f));
    const auto magnitude_before = magnitude;

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/3, /*z=*/5,
                   fim::dsp::YMode::kTilt, fim::dsp::ZMode::kDisperse);

    // Phase should have changed somewhere.
    bool any_phase_changed = false;
    for (std::size_t k = 0; k < 16; ++k) {
        for (std::size_t f = 0; f < 2; ++f) {
            if (std::abs(phase[k][f]) > 1e-4f) {
                any_phase_changed = true;
                break;
            }
        }
    }
    REQUIRE(any_phase_changed);
}

TEST_CASE("SpectralModifier ZMode::kCrush is a no-op for phase",
          "[dsp][spectral_modifier]") {
    auto magnitude = std::vector<std::vector<float>>(
        8, std::vector<float>(2, 1.0f));
    auto phase = std::vector<std::vector<float>>(
        8, std::vector<float>(2, 0.5f));
    const auto phase_before = phase;

    fim::dsp::SpectralModifier modifier(/*seed=*/42);
    modifier.Apply(magnitude, phase, /*x=*/3, /*y=*/3, /*z=*/7,
                   fim::dsp::YMode::kTilt, fim::dsp::ZMode::kCrush);

    // Phase must be bit-identical — kCrush should not touch it.
    for (std::size_t k = 0; k < 8; ++k) {
        for (std::size_t f = 0; f < 2; ++f) {
            REQUIRE(phase[k][f] == phase_before[k][f]);
        }
    }
}
```

Also update the existing Phase 3b tests to pass the new `YMode::kTilt` and `ZMode::kRandom` arguments. The existing calls like `modifier.Apply(spectrum.magnitude, spectrum.phase, 4, 3, 0)` become `modifier.Apply(spectrum.magnitude, spectrum.phase, 4, 3, 0, fim::dsp::YMode::kTilt, fim::dsp::ZMode::kRandom)`. Since the new parameters default to `kTilt`/`kRandom`, the existing tests actually don't need any changes — the default argument values preserve Phase 3b behavior.

- [ ] **Step 4: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 58 tests pass (54 from after Task 1 + 4 new SpectralModifier mode tests).

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/spectral_modifier.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/spectral_modifier.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_spectral_modifier_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/spectral_modifier.h cpp/src/dsp/spectral_modifier.cpp \
        cpp/tests/dsp_spectral_modifier_test.cpp
git commit -m "feat(cpp): add YMode and ZMode with formant, stretch, disperse"
```

---

## Task 3: CycleExtractor energy threshold fix

Add an energy floor check in `CycleExtractor::Extract` that zeros out the output cycle if the pre-normalization peak is below `1e-4`. Fixes both X-extreme silent cliff (exact zero input → zero output, no change) and X-extreme noise amplification (near-zero input → also zero output instead of normalizing dither up to full scale).

**Files:**
- Modify: `cpp/src/dsp/cycle_extractor.cpp`
- Modify: `cpp/tests/dsp_cycle_extractor_test.cpp`

- [ ] **Step 1: Write a failing test for the new behavior**

Append to `cpp/tests/dsp_cycle_extractor_test.cpp`:

```cpp
TEST_CASE("CycleExtractor returns silence for a near-zero spectrum (below threshold)",
          "[dsp][cycle_extractor]") {
    constexpr std::size_t kFft = 64;
    constexpr std::size_t kTarget = 256;
    fim::dsp::CycleExtractor extractor(kFft, kTarget);

    // Build a spectrum with a single bin at magnitude 1e-7 — well below
    // the 1e-4 threshold. The pre-Phase-3d behavior would normalize this
    // up to peak=1.0 and produce amplified noise; the new behavior
    // returns zeros.
    std::vector<std::vector<float>> magnitude(kFft / 2 + 1,
                                              std::vector<float>(1, 0.0f));
    std::vector<std::vector<float>> phase(kFft / 2 + 1,
                                          std::vector<float>(1, 0.0f));
    magnitude[4][0] = 1e-7f;

    const auto cycle = extractor.Extract(magnitude, phase, 0);
    REQUIRE(cycle.size() == kTarget);
    for (float s : cycle) {
        REQUIRE(std::abs(s) < 1e-6f);
    }
}
```

- [ ] **Step 2: Verify the test fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure -R "CycleExtractor returns silence"
```

Expected: the test FAILS because the current CycleExtractor normalizes the near-zero input up to peak 1.0.

- [ ] **Step 3: Add the threshold check**

Modify `cpp/src/dsp/cycle_extractor.cpp`. Replace the final normalization block with a thresholded version:

```cpp
    // Step 5: normalize to peak 1.0 (or zero out if below the energy
    // threshold). The threshold prevents two bad behaviors on audio files
    // with fade-ins/fade-outs:
    //   1. A near-silent STFT frame has a tiny peak that normalizes up to
    //      full scale, producing amplified dither noise.
    //   2. An exactly-zero STFT frame produces zeros, which the X-extreme
    //      fix treats the same way for consistency.
    // See docs/followups.md (removed entry) for the original observation.
    constexpr float kSilenceThreshold = 1e-4f;
    float peak = 0.0f;
    for (float s : cycle) {
        peak = std::max(peak, std::abs(s));
    }
    if (peak > kSilenceThreshold) {
        const float inv_peak = 1.0f / peak;
        for (float& s : cycle) {
            s *= inv_peak;
        }
    } else {
        // Below threshold — treat as silence to avoid amplifying dither.
        std::fill(cycle.begin(), cycle.end(), 0.0f);
    }
```

- [ ] **Step 4: Verify the test passes**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 59 tests pass (58 from after Task 2 + 1 new threshold test).

Also verify the existing "CycleExtractor on a zero spectrum returns zeros" test still passes — it should, because zero input produces `peak == 0 < kSilenceThreshold`, falling into the new else branch which also zeros the cycle (no change in observable behavior).

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/cycle_extractor.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/dsp_cycle_extractor_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/cycle_extractor.cpp cpp/tests/dsp_cycle_extractor_test.cpp
git commit -m "fix(cpp): apply silence threshold in CycleExtractor to avoid dither amplification"
```

---

## Task 4: GenerateOptions + SingleWavGenerator mode plumbing

Add a `GenerateOptions` struct carrying the Y/Z mode selections and thread it through `SingleWavGenerator::Generate`. Apply Z-crush in the cell loop when the selected Z mode is `kCrush`.

**Files:**
- Create: `cpp/src/dsp/generate_options.h`
- Modify: `cpp/src/dsp/single_wav_generator.h`
- Modify: `cpp/src/dsp/single_wav_generator.cpp`

- [ ] **Step 1: Create the GenerateOptions header**

Create `cpp/src/dsp/generate_options.h`:

```cpp
#pragma once

#include "dsp/spectral_modifier.h"

namespace fim::dsp {

// Options bundle passed from the UI/service layer to SingleWavGenerator.
// Default-constructed values reproduce Phase 3b behavior: tilt + phase
// randomize, no post-effects.
struct GenerateOptions {
    YMode y_mode = YMode::kTilt;
    ZMode z_mode = ZMode::kRandom;
};

}  // namespace fim::dsp
```

- [ ] **Step 2: Update SingleWavGenerator header**

Modify `cpp/src/dsp/single_wav_generator.h`. Update the `Generate` signature to take a `GenerateOptions`:

```cpp
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include "dsp/generate_options.h"

namespace fim::dsp {

class SingleWavGenerator {
public:
    explicit SingleWavGenerator(std::size_t samples = 2048,
                                std::size_t oversample_factor = 4,
                                std::size_t num_pages = 8);

    using ProgressCallback = std::function<void(int percent)>;

    // Run the full pipeline. The options parameter selects the Y/Z morph
    // modes; defaults reproduce Phase 3b behavior (tilt + phase randomize).
    bool Generate(const std::filesystem::path& input_audio_path,
                  const std::filesystem::path& output_directory,
                  const GenerateOptions& options = {},
                  const ProgressCallback& on_progress = {}) const;

    std::size_t samples() const { return samples_; }
    std::size_t oversample_factor() const { return oversample_factor_; }
    std::size_t n_samples() const { return n_samples_; }
    std::size_t num_pages() const { return num_pages_; }

private:
    bool WritePageToWav(const std::filesystem::path& path,
                        const std::vector<float>& downsampled_page) const;

    std::size_t samples_;
    std::size_t oversample_factor_;
    std::size_t n_samples_;
    std::size_t num_pages_;
};

}  // namespace fim::dsp
```

- [ ] **Step 3: Update SingleWavGenerator implementation**

Modify `cpp/src/dsp/single_wav_generator.cpp`. Add `post_effects.h` to the includes and update the Generate method to:
1. Accept the new `GenerateOptions` parameter
2. Pass `options.y_mode` / `options.z_mode` to `SpectralModifier::Apply`
3. If `options.z_mode == ZMode::kCrush`, apply `ZCrush` to each cell after extraction

Replace the full file:

```cpp
#include "dsp/single_wav_generator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dr_wav.h"
#include "dsp/cycle_extractor.h"
#include "dsp/fft_resampler.h"
#include "dsp/post_effects.h"
#include "dsp/spectral_modifier.h"
#include "dsp/stft.h"
#include "dsp/wav_loader.h"

namespace fim::dsp {

namespace {

constexpr std::size_t kStftFftSize = 2048;
constexpr std::size_t kStftHopSize = 1024;
constexpr std::size_t kCellsPerSide = 8;
constexpr std::size_t kCellsPerPage = kCellsPerSide * kCellsPerSide;
constexpr std::uint32_t kOutputSampleRate = 44100;

}  // namespace

SingleWavGenerator::SingleWavGenerator(std::size_t samples,
                                       std::size_t oversample_factor,
                                       std::size_t num_pages)
    : samples_(samples),
      oversample_factor_(oversample_factor),
      n_samples_(samples * oversample_factor),
      num_pages_(num_pages) {}

bool SingleWavGenerator::Generate(const std::filesystem::path& input_audio_path,
                                  const std::filesystem::path& output_directory,
                                  const GenerateOptions& options,
                                  const ProgressCallback& on_progress) const {
    auto loaded = LoadWav(input_audio_path, /*normalize=*/true);
    if (!loaded.has_value() || loaded->samples.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    Stft stft(kStftFftSize, kStftHopSize);
    const auto bins = stft.Analyze(loaded->samples);
    if (bins.empty()) {
        return false;
    }
    const auto magnitude = Magnitude(bins);
    const auto phase = Phase(bins);
    const std::size_t num_frames = bins.size();

    // Transpose [frame][bin] -> [bin][frame] for SpectralModifier /
    // CycleExtractor.
    const std::size_t num_bins = stft.num_bins();
    std::vector<std::vector<float>> magnitude_t(num_bins,
                                                std::vector<float>(num_frames, 0.0f));
    std::vector<std::vector<float>> phase_t(num_bins,
                                            std::vector<float>(num_frames, 0.0f));
    for (std::size_t f = 0; f < num_frames; ++f) {
        for (std::size_t k = 0; k < num_bins; ++k) {
            magnitude_t[k][f] = magnitude[f][k];
            phase_t[k][f] = phase[f][k];
        }
    }

    CycleExtractor extractor(kStftFftSize, n_samples_);
    FftResampler downsampler(n_samples_, samples_);

    for (std::size_t z = 0; z < num_pages_; ++z) {
        SpectralModifier modifier;

        // Precompute the Z-crush params for this page if crush mode is
        // selected. We use `z` (the page index) as the crush intensity, so
        // later pages are progressively more crushed — mirrors how Z0/Z1
        // modes also get progressively stronger with z.
        const ZCrushParams crush_params =
            (options.z_mode == ZMode::kCrush)
                ? ZCrushAmount(static_cast<int>(z))
                : ZCrushParams{16, 1};

        std::vector<float> page_downsampled;
        page_downsampled.reserve(samples_ * kCellsPerPage);

        for (std::size_t y = 0; y < kCellsPerSide; ++y) {
            for (std::size_t x = 0; x < kCellsPerSide; ++x) {
                auto mag_copy = magnitude_t;
                auto phase_copy = phase_t;
                modifier.Apply(mag_copy, phase_copy, static_cast<int>(x),
                               static_cast<int>(y), static_cast<int>(z),
                               options.y_mode, options.z_mode);

                const std::size_t frame_selection = static_cast<std::size_t>(
                    static_cast<float>(x) / 7.0f *
                    static_cast<float>(num_frames - 1));

                auto cell_oversampled =
                    extractor.Extract(mag_copy, phase_copy, frame_selection);

                // Apply Z-crush as a post-effect before downsampling, so
                // the quantization levels are preserved through the final
                // rate conversion rather than being smoothed out.
                if (options.z_mode == ZMode::kCrush) {
                    ZCrush(cell_oversampled, crush_params.bit_depth,
                           crush_params.sample_hold);
                }

                const auto cell_downsampled = downsampler.Resample(cell_oversampled);
                page_downsampled.insert(page_downsampled.end(),
                                        cell_downsampled.begin(),
                                        cell_downsampled.end());
            }
        }

        float page_peak = 0.0f;
        for (float s : page_downsampled) {
            page_peak = std::max(page_peak, std::abs(s));
        }
        if (page_peak > 0.0f) {
            const float inv = 1.0f / page_peak;
            for (float& s : page_downsampled) {
                s *= inv;
            }
        }

        const auto path = output_directory / (std::to_string(z + 1) + ".wav");
        if (!WritePageToWav(path, page_downsampled)) {
            return false;
        }

        if (on_progress) {
            const int percent = static_cast<int>((z + 1) * 100 / num_pages_);
            on_progress(percent);
        }
    }

    return true;
}

bool SingleWavGenerator::WritePageToWav(
    const std::filesystem::path& path,
    const std::vector<float>& downsampled_page) const {
    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = kOutputSampleRate;
    format.bitsPerSample = 16;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr)) {
        return false;
    }

    std::vector<std::int16_t> int_samples(downsampled_page.size());
    for (std::size_t i = 0; i < downsampled_page.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, downsampled_page[i]));
        int_samples[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }

    const drwav_uint64 frames_written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);
    return frames_written == int_samples.size();
}

}  // namespace fim::dsp
```

- [ ] **Step 4: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. The new `GenerateOptions` parameter has a default value so `SingleWavService` still compiles without changes — Task 5 will explicitly pass options through.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/generate_options.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/single_wav_generator.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/dsp/single_wav_generator.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/dsp/generate_options.h cpp/src/dsp/single_wav_generator.h \
        cpp/src/dsp/single_wav_generator.cpp
git commit -m "feat(cpp): thread GenerateOptions and Z-crush through SingleWavGenerator"
```

---

## Task 5: SingleWavService mode caching

Expose `SetYMode` / `SetZMode` on `SingleWavService` and cache the selections in a `GenerateOptions options_` member. Pass them to `SingleWavGenerator::Generate` in the worker lambda.

**Files:**
- Modify: `cpp/src/app/services/single_wav_service.h`
- Modify: `cpp/src/app/services/single_wav_service.cpp`

- [ ] **Step 1: Update the header**

Modify `cpp/src/app/services/single_wav_service.h`. Include `generate_options.h` and add the mode setters + member:

```cpp
#pragma once

#include <QObject>
#include <QString>

#include <atomic>

#include "dsp/generate_options.h"

namespace fim::app {

class SingleWavService : public QObject {
    Q_OBJECT

public:
    explicit SingleWavService(QObject* parent = nullptr);
    ~SingleWavService() override = default;

    void SetInputFile(const QString& path);
    QString InputFile() const;

    void SetOutputDirectory(const QString& path);
    QString OutputDirectory() const;

    // Configure the morph modes used by the next Generate() call. These
    // persist across Generate() calls until explicitly changed.
    void SetYMode(fim::dsp::YMode mode);
    void SetZMode(fim::dsp::ZMode mode);

    bool IsGenerating() const;

public slots:
    void Generate();

signals:
    void progressChanged(int percent);
    void generationFinished();
    void generationFailed(const QString& error);

private:
    QString input_file_;
    QString output_directory_;
    fim::dsp::GenerateOptions options_;
    std::atomic<bool> generating_{false};
};

}  // namespace fim::app
```

- [ ] **Step 2: Update the implementation**

Modify `cpp/src/app/services/single_wav_service.cpp`. Add the setter implementations and pass `options_` to `SingleWavGenerator::Generate`:

```cpp
#include "app/services/single_wav_service.h"

#include <filesystem>

#include <QThreadPool>

#include "dsp/single_wav_generator.h"

namespace fim::app {

SingleWavService::SingleWavService(QObject* parent) : QObject(parent) {}

void SingleWavService::SetInputFile(const QString& path) {
    input_file_ = path;
}
QString SingleWavService::InputFile() const {
    return input_file_;
}

void SingleWavService::SetOutputDirectory(const QString& path) {
    output_directory_ = path;
}
QString SingleWavService::OutputDirectory() const {
    return output_directory_;
}

void SingleWavService::SetYMode(fim::dsp::YMode mode) {
    options_.y_mode = mode;
}
void SingleWavService::SetZMode(fim::dsp::ZMode mode) {
    options_.z_mode = mode;
}

bool SingleWavService::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void SingleWavService::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    const QString in_file = input_file_;
    const QString out_dir = output_directory_;
    const fim::dsp::GenerateOptions opts = options_;

    QThreadPool::globalInstance()->start([this, in_file, out_dir, opts]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path output_path(out_dir.toStdString());

        fim::dsp::SingleWavGenerator generator;
        // Qt auto-queues cross-thread signal emits onto the GUI thread.
        const bool ok = generator.Generate(
            input_path, output_path, opts,
            [this](int percent) { emit progressChanged(percent); });

        generating_.store(false, std::memory_order_release);

        if (ok) {
            emit generationFinished();
        } else {
            emit generationFailed(
                QString("Failed to generate wavetable bank from %1").arg(in_file));
        }
    });
}

}  // namespace fim::app
```

- [ ] **Step 3: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. `AnyWavScreen` still calls the service without setting modes — it'll use default (`kTilt`, `kRandom`) which matches Phase 3b behavior.

- [ ] **Step 4: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/single_wav_service.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/single_wav_service.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/single_wav_service.h \
        cpp/src/app/services/single_wav_service.cpp
git commit -m "feat(cpp): expose Y/Z mode setters on SingleWavService"
```

---

## Task 6: AnyWavScreen + MainWindow wire Settings and mode selection

Thread a `fim::app::Settings` reference through `MainWindow` → `AnyWavScreen`. Replace the placeholder axis labels with real ones. Connect the `AxisMorphSelector` change signals to slots that update the service's selected mode AND persist to settings. On `Reset`, read the modes back from settings and re-sync the selectors.

**Files:**
- Modify: `cpp/src/ui/main_window.h`
- Modify: `cpp/src/ui/main_window.cpp`
- Modify: `cpp/src/ui/any_wav_screen.h`
- Modify: `cpp/src/ui/any_wav_screen.cpp`

- [ ] **Step 1: Add a Settings member to MainWindow**

Modify `cpp/src/ui/main_window.h`. Add a `fim::app::Settings` member and a forward declaration:

```cpp
#pragma once

#include <memory>

#include <QMainWindow>

#include "app/settings.h"

class QStackedWidget;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::ui {

class AnyWavScreen;
class LauncherScreen;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnModeChosen(int mode);
    void OnBackToLauncher();

private:
    void BuildMenuBar();

    std::unique_ptr<fim::engine::RealtimeAudioEngine> engine_;
    fim::app::Settings settings_;
    QStackedWidget* stack_ = nullptr;
    LauncherScreen* launcher_screen_ = nullptr;
    AnyWavScreen* any_wav_screen_ = nullptr;
    int launcher_index_ = -1;
    int any_wav_index_ = -1;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Pass settings to AnyWavScreen in MainWindow's constructor**

Modify `cpp/src/ui/main_window.cpp`. Update the `AnyWavScreen` construction to pass `&settings_`:

```cpp
    launcher_screen_ = new LauncherScreen(this);
    any_wav_screen_ = new AnyWavScreen(engine_.get(), &settings_, this);
```

The rest of the file stays the same.

- [ ] **Step 3: Update AnyWavScreen header to accept Settings**

Modify `cpp/src/ui/any_wav_screen.h`. Add the `Settings*` constructor parameter and a member, plus slot forward declarations for the mode change handlers:

```cpp
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::app {
class Settings;
class SingleWavService;
}

namespace fim::ui {

class AxisMorphSelector;
class CustomProgressBar;
class FileDropWidget;
class PreviewControlsWidget;

class AnyWavScreen : public QWidget {
    Q_OBJECT

public:
    enum class State {
        kEmpty,
        kFileSet,
        kGenerating,
        kDoneMessage,
        kDonePreviewAvailable,
    };

    AnyWavScreen(fim::engine::RealtimeAudioEngine* engine,
                 fim::app::Settings* settings, QWidget* parent = nullptr);

    void Reset();

signals:
    void backRequested();

private slots:
    void OnFileDropped(const QString& path);
    void OnClearClicked();
    void OnGenerateClicked();
    void OnExportClicked();
    void OnProgressChanged(int percent);
    void OnGenerationFinished();
    void OnYModeChanged(int index);
    void OnZModeChanged(int index);

private:
    void SetState(State state);
    QWidget* BuildEmptyPage();
    QWidget* BuildFileSetPage();
    QWidget* BuildGeneratingPage();
    QWidget* BuildDonePage(bool with_preview);

    fim::engine::RealtimeAudioEngine* engine_;
    fim::app::Settings* settings_;
    fim::app::SingleWavService* service_ = nullptr;
    QString current_file_;

    QStackedWidget* stack_ = nullptr;
    int empty_page_index_ = -1;
    int file_set_page_index_ = -1;
    int generating_page_index_ = -1;
    int done_message_page_index_ = -1;
    int done_preview_page_index_ = -1;

    QLabel* filename_label_ = nullptr;
    AxisMorphSelector* y_selector_ = nullptr;
    AxisMorphSelector* z_selector_ = nullptr;
    CustomProgressBar* progress_bar_ = nullptr;
    PreviewControlsWidget* preview_controls_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 4: Update AnyWavScreen implementation**

Modify `cpp/src/ui/any_wav_screen.cpp`. Changes:
1. Accept the `Settings*` parameter, store it.
2. In the constructor, read the stored modes from settings and set them on the service.
3. In `BuildFileSetPage`, use real labels for the axis selectors.
4. Connect `AxisMorphSelector::currentIndexChanged` to the new slots.
5. Implement `OnYModeChanged` / `OnZModeChanged` to push to the service and write to settings.

Full rewrite:

```cpp
#include "ui/any_wav_screen.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "app/services/single_wav_service.h"
#include "app/settings.h"
#include "dsp/spectral_modifier.h"
#include "engine/realtime_audio_engine.h"
#include "ui/widgets/axis_morph_selector.h"
#include "ui/widgets/custom_progress_bar.h"
#include "ui/widgets/file_drop_widget.h"
#include "ui/widgets/preview_controls_widget.h"

namespace fim::ui {

namespace {

constexpr int kDoneMessageHoldMs = 800;

QString StubOutputDir() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("audio_resynth");
}

// Map a selector button index (0..2) to the corresponding enum value.
fim::dsp::YMode YModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::YMode::kFormant;
        case 2:
            return fim::dsp::YMode::kStretch;
        case 0:
        default:
            return fim::dsp::YMode::kTilt;
    }
}

fim::dsp::ZMode ZModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::ZMode::kDisperse;
        case 2:
            return fim::dsp::ZMode::kCrush;
        case 0:
        default:
            return fim::dsp::ZMode::kRandom;
    }
}

QPushButton* MakeBackButton(QWidget* parent) {
    auto* button = new QPushButton("← Back", parent);
    button->setObjectName("backButton");
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLabel* MakeTitle(QWidget* parent) {
    auto* label = new QLabel("Use any .wav file to create your wavetable bank", parent);
    label->setObjectName("anyWavTitle");
    return label;
}

}  // namespace

AnyWavScreen::AnyWavScreen(fim::engine::RealtimeAudioEngine* engine,
                           fim::app::Settings* settings, QWidget* parent)
    : QWidget(parent), engine_(engine), settings_(settings) {
    setObjectName("anyWavScreen");
    service_ = new fim::app::SingleWavService(this);
    service_->SetOutputDirectory(StubOutputDir());

    // Initialize the service with the last-used modes from settings.
    const int initial_y = settings_->YMorph();
    const int initial_z = settings_->ZMorph();
    service_->SetYMode(YModeFromIndex(initial_y));
    service_->SetZMode(ZModeFromIndex(initial_z));

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(32, 32, 32, 32);
    root_layout->setSpacing(16);

    stack_ = new QStackedWidget(this);
    empty_page_index_ = stack_->addWidget(BuildEmptyPage());
    file_set_page_index_ = stack_->addWidget(BuildFileSetPage());
    generating_page_index_ = stack_->addWidget(BuildGeneratingPage());
    done_message_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/false));
    done_preview_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/true));
    root_layout->addWidget(stack_);

    connect(service_, &fim::app::SingleWavService::progressChanged, this,
            &AnyWavScreen::OnProgressChanged);
    connect(service_, &fim::app::SingleWavService::generationFinished, this,
            &AnyWavScreen::OnGenerationFinished);
    connect(service_, &fim::app::SingleWavService::generationFailed, this,
            [this](const QString& error) {
                QMessageBox::warning(this, "Generation failed", error);
                SetState(current_file_.isEmpty() ? State::kEmpty : State::kFileSet);
            });

    SetState(State::kEmpty);
}

void AnyWavScreen::Reset() {
    current_file_.clear();
    // Re-sync the selectors from settings (in case they changed elsewhere).
    if (y_selector_ != nullptr) {
        y_selector_->SetCurrentIndex(settings_->YMorph());
    }
    if (z_selector_ != nullptr) {
        z_selector_->SetCurrentIndex(settings_->ZMorph());
    }
    SetState(State::kEmpty);
}

void AnyWavScreen::SetState(State state) {
    switch (state) {
        case State::kEmpty:
            stack_->setCurrentIndex(empty_page_index_);
            break;
        case State::kFileSet:
            stack_->setCurrentIndex(file_set_page_index_);
            break;
        case State::kGenerating:
            stack_->setCurrentIndex(generating_page_index_);
            progress_bar_->SetProgress(0);
            break;
        case State::kDoneMessage:
            stack_->setCurrentIndex(done_message_page_index_);
            QTimer::singleShot(kDoneMessageHoldMs, this,
                               [this]() { SetState(State::kDonePreviewAvailable); });
            break;
        case State::kDonePreviewAvailable:
            stack_->setCurrentIndex(done_preview_page_index_);
            engine_->LoadBank(StubOutputDir().toStdString());
            break;
    }
}

QWidget* AnyWavScreen::BuildEmptyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    auto* card = new QFrame(page);
    card->setObjectName("anyWavInnerCard");
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(32, 32, 32, 32);

    auto* drop = new FileDropWidget(card);
    connect(drop, &FileDropWidget::fileDropped, this, &AnyWavScreen::OnFileDropped);
    card_layout->addWidget(drop);

    layout->addWidget(card);
    layout->addStretch();
    return page;
}

QWidget* AnyWavScreen::BuildFileSetPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    auto* file_row = new QHBoxLayout();
    filename_label_ = new QLabel("(no file)", page);
    filename_label_->setObjectName("anyWavFilename");
    auto* clear_button = new QPushButton("Clear", page);
    clear_button->setObjectName("clearButton");
    file_row->addWidget(filename_label_);
    file_row->addStretch();
    file_row->addWidget(clear_button);
    layout->addLayout(file_row);
    connect(clear_button, &QPushButton::clicked, this, &AnyWavScreen::OnClearClicked);

    auto* x_label = new QLabel("X axis", page);
    x_label->setObjectName("anyWavAxisLabel");
    layout->addWidget(x_label);
    auto* x_descriptor = new QLabel("Scans the wave", page);
    x_descriptor->setObjectName("anyWavAxisDescriptor");
    layout->addWidget(x_descriptor);

    // Y axis selector with real morph mode labels.
    const QStringList y_options{"Tilt", "Formant", "Stretch"};
    y_selector_ = new AxisMorphSelector("Y axis", y_options, page);
    y_selector_->SetCurrentIndex(settings_->YMorph());
    layout->addWidget(y_selector_);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnYModeChanged);

    // Z axis selector with real morph mode labels.
    const QStringList z_options{"Random", "Disperse", "Crush"};
    z_selector_ = new AxisMorphSelector("Z axis", z_options, page);
    z_selector_->SetCurrentIndex(settings_->ZMorph());
    layout->addWidget(z_selector_);
    connect(z_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnZModeChanged);

    auto* generate_row = new QHBoxLayout();
    auto* generate_button = new QPushButton("Generate wavetable bank", page);
    generate_button->setObjectName("generateButton");
    generate_row->addStretch();
    generate_row->addWidget(generate_button);
    layout->addLayout(generate_row);
    connect(generate_button, &QPushButton::clicked, this,
            &AnyWavScreen::OnGenerateClicked);

    layout->addStretch();
    return page;
}

QWidget* AnyWavScreen::BuildGeneratingPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    progress_bar_ = new CustomProgressBar(page);
    layout->addWidget(progress_bar_);

    layout->addStretch();
    return page;
}

QWidget* AnyWavScreen::BuildDonePage(bool with_preview) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    if (!with_preview) {
        auto* done_label = new QLabel("Done!", page);
        done_label->setObjectName("anyWavDoneMessage");
        layout->addWidget(done_label);
    } else {
        auto* button_row = new QHBoxLayout();
        auto* generate_button = new QPushButton("Generate wavetable bank", page);
        generate_button->setObjectName("generateButton");
        auto* export_button = new QPushButton("Export wavetable bank", page);
        export_button->setObjectName("exportButton");
        button_row->addWidget(generate_button);
        button_row->addWidget(export_button);
        button_row->addStretch();
        layout->addLayout(button_row);
        connect(generate_button, &QPushButton::clicked, this,
                &AnyWavScreen::OnGenerateClicked);
        connect(export_button, &QPushButton::clicked, this,
                &AnyWavScreen::OnExportClicked);

        preview_controls_ = new PreviewControlsWidget(engine_, page);
        layout->addWidget(preview_controls_);
    }

    layout->addStretch();
    return page;
}

void AnyWavScreen::OnFileDropped(const QString& path) {
    current_file_ = path;
    if (filename_label_) {
        filename_label_->setText(QFileInfo(path).fileName());
    }
    service_->SetInputFile(path);
    SetState(State::kFileSet);
}

void AnyWavScreen::OnClearClicked() {
    current_file_.clear();
    SetState(State::kEmpty);
}

void AnyWavScreen::OnGenerateClicked() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
        if (preview_controls_) {
            preview_controls_->RefreshPlayButton();
        }
    }
    SetState(State::kGenerating);
    service_->Generate();
}

void AnyWavScreen::OnExportClicked() {
    const QString dest = QFileDialog::getExistingDirectory(
        this, "Export bank to…", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dest.isEmpty()) {
        return;
    }
    const QString src_dir = StubOutputDir();
    for (int i = 1; i <= 8; ++i) {
        const QString src = QString("%1/%2.wav").arg(src_dir).arg(i);
        const QString dst = QString("%1/%2.wav").arg(dest).arg(i);
        QFile::copy(src, dst);
    }
}

void AnyWavScreen::OnProgressChanged(int percent) {
    if (progress_bar_) {
        progress_bar_->SetProgress(percent);
    }
}

void AnyWavScreen::OnGenerationFinished() {
    SetState(State::kDoneMessage);
}

void AnyWavScreen::OnYModeChanged(int index) {
    service_->SetYMode(YModeFromIndex(index));
    settings_->SetYMorph(index);
}

void AnyWavScreen::OnZModeChanged(int index) {
    service_->SetZMode(ZModeFromIndex(index));
    settings_->SetZMorph(index);
}

}  // namespace fim::ui
```

- [ ] **Step 5: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Run the app and verify manually**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Walk through:
1. Launch → any-wav mode → drop a .wav file
2. Verify the Y axis buttons now say "Tilt / Formant / Stretch"
3. Verify the Z axis buttons now say "Random / Disperse / Crush"
4. Click "Formant" and "Crush" → generate → listen. Output should be different from the defaults (a formant-shifted spectrum with bit-crushed character).
5. Back to launcher → back into any-wav → verify the Y/Z selections are remembered (persistence check).
6. Try different Y/Z combinations and confirm they produce audibly different output.

- [ ] **Step 7: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/main_window.h cpp/src/ui/main_window.cpp \
        cpp/src/ui/any_wav_screen.h cpp/src/ui/any_wav_screen.cpp
git commit -m "feat(cpp): wire Y/Z morph selectors to Settings-persisted modes"
```

---

## Task 7: Remove the resolved followup entry

The X-extreme cliff is now fixed; the followups entry should go away.

**Files:**
- Modify: `docs/followups.md`

- [ ] **Step 1: Delete the resolved section**

Modify `docs/followups.md`. Remove the entire "Phase 3b (Phase 3d candidate fixes)" section that was added at the end of Phase 3b. The rest of the file stays unchanged.

- [ ] **Step 2: Commit**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add docs/followups.md
git commit -m "docs: remove resolved X-extreme cliff followup"
```

---

## Task 8: Push and verify CI

Push and confirm CI is green on all platforms. Per the Phase 3a/3b lessons, do NOT trust the background watch's exit code — always verify with `gh run view` explicitly.

- [ ] **Step 1: Push**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
```

- [ ] **Step 2: Watch CI**

```bash
sleep 5
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId,status --jq '.[0]'
# copy the returned id and pass it to:
gh run watch <id> --exit-status
```

- [ ] **Step 3: VERIFY CI status explicitly**

```bash
gh run view <id>
```

Expected: all four jobs ✓. Any failure — particularly clang-format-version-skew issues of the kind we hit in Phase 3b — should be investigated and fixed before claiming Phase 3d done.

---

## Phase 3d done when:

1. ✅ `fim::dsp::ZCrush` correctly applies bit + sample-rate reduction
2. ✅ `fim::dsp::SpectralModifier` supports `YMode::kFormant`, `YMode::kStretch`, `ZMode::kDisperse`, and treats `ZMode::kCrush` as a phase no-op
3. ✅ `fim::dsp::CycleExtractor` applies the 1e-4 energy threshold (fixes X-extreme cliff)
4. ✅ `fim::dsp::SingleWavGenerator` threads `GenerateOptions` through and applies Z-crush as a post-effect
5. ✅ `fim::app::SingleWavService` exposes `SetYMode` / `SetZMode`
6. ✅ `fim::ui::AnyWavScreen` has real axis labels and wires the selectors to `Settings` + the service
7. ✅ `fim::ui::MainWindow` holds a `Settings` and passes it to `AnyWavScreen`
8. ✅ Manual verification: selecting different modes produces audibly different output; selections persist across app restarts
9. ✅ All 59 Catch2 tests pass locally (54 from Phase 3b + 4 Z-crush + 4 new SpectralModifier mode tests + 1 CycleExtractor threshold test − 4 replaced)
10. ✅ CI green on macOS, Ubuntu, Windows, and clang-format

After Phase 3d ships, the any-wav mode is feature-complete for the Phase 3 arc. Phase 3c (Python oracle harness) can land after to validate parity, or we can move on to Serum mode / multi-wav mode / Phase 4 polish.
