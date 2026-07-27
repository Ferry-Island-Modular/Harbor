# fim-config-tool — C++/Qt rewrite (Phase 0+)

This directory is a staging area for the C++/Qt rewrite. At Phase 6 cutover it
will be flattened to the repo root and the current Python tree will move to
`legacy/python/`.

## Prerequisites
- Qt 6.7+ (`brew install qt` on macOS)
- CMake 3.25+
- Node / npx (the build fetches a pinned dart-sass version)
- clang-format

## Build
```bash
git submodule update --init --recursive
cmake -S src -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
./build/Harbor.app/Contents/MacOS/Harbor  # macOS
# ./build/harbor                         # Linux
# ./build/Harbor.exe                     # Windows
```

See `docs/superpowers/plans/` (repo root, not this directory) for the phase plans.

## Wavetable evaluation corpus

The optional evaluation tool creates five deterministic synthetic inputs,
generates a bank from each one, and renders standard X, Y, and Z audition
sweeps. This makes DSP changes directly comparable without checking generated
audio into git.

```bash
cmake -S src -B build -G Ninja -DFIM_BUILD_EVAL_TOOLS=ON
cmake --build build --target fim-wavetable-eval
./build/fim-wavetable-eval evaluation-output
```

The output directory contains `sources/`, `banks/`, `previews/`, and a
`manifest.csv` recording fixture origin, the fixed generation seed, and paths.
The corpus is a stress set for relative listening comparisons; it is not
intended to model real instruments faithfully.

For a small corpus of real public-domain recordings, install `ffmpeg` and run:

```bash
python3 src/tools/fetch_real_eval_corpus.py evaluation-corpus
```

This downloads voice, flute, percussion-loop, rain, and dense-mix recordings
from Wikimedia Commons, converts the first 20 seconds of each to mono 44.1 kHz
PCM WAV, and records URLs, authors, licenses, and SHA-256 hashes in
`evaluation-corpus/manifest.json`. The downloaded and converted audio is
ignored by git.

Pass fetched WAVs after the output directory to include them in a self-contained
baseline run alongside the synthetic fixtures:

```bash
./build/fim-wavetable-eval evaluation-output/baseline evaluation-corpus/real/*.wav
```

The default preview oscillator frequency is 110 Hz. Use `--frequency` to keep
the generated banks identical while rendering a separate lower-pitched
listening set, for example:

```bash
./build/fim-wavetable-eval --frequency 55 evaluation-output/baseline-bass \
    evaluation-corpus/real/*.wav
```

The Any WAV screen defaults to **Focused** source treatment. It selects a
salient, non-silent source window, spaces frames with a blend of accumulated
spectral change and chronological progress, and dedicates X exclusively to
progression through those frames. Its random-phase Z treatment uses a stable
target throughout the bank, making Y neighbours phase-aligned and Z a
progressive move toward one texture. **Legacy stretch** remains available in
the screen's source-treatment selector and retains its independent phase RNG.

Use `--candidate` to reproduce Focused mode in the evaluation harness:

```bash
./build/fim-wavetable-eval --candidate --frequency 55 \
    evaluation-output/candidate-source-selection-bass evaluation-corpus/real/*.wav
```

The same tool can render complete bass-preview matrices for the Serum and
three-wave axis designs:

```bash
./build/fim-wavetable-eval --serum --frequency 55 \
  evaluation-output/serum-axis-candidate input-serum-table.wav

./build/fim-wavetable-eval --three-wave --frequency 55 \
  evaluation-output/three-wave-axis-candidate source-a.wav source-b.wav source-c.wav
```

Serum evaluation renders every spectral-color × texture combination.
Three-wave evaluation renders Phase Motion, Odd/Even, and Crush variants.

For controlled comparisons, the parts of candidate mode can also be
selected independently:

```bash
# Change frame selection only; retain the legacy X spectral stretch.
./build/fim-wavetable-eval --frame-selection salient --x-stretch enabled \
    --frequency 55 evaluation-output/salient-with-stretch-bass

# Change X behavior only; retain uniform source-frame selection.
./build/fim-wavetable-eval --frame-selection uniform --x-stretch disabled \
    --frequency 55 evaluation-output/uniform-without-stretch-bass

# Compare only the phase treatment while retaining focused frame/X behavior.
./build/fim-wavetable-eval --frame-selection salient --x-stretch disabled \
    --phase-randomization independent --frequency 55 \
    evaluation-output/focused-independent-phase-bass
```

The generated banks can be compared with the audit and spectral modules from
the Four-Seas resources repository:

```bash
cd /path/to/Four-Seas/resources/generators
uv run python /path/to/fim-config-tool/src/scripts/analyze-wavetable-evaluations.py \
    --analysis-root "$PWD" \
    --variant legacy=/path/to/baseline/banks \
    --variant candidate=/path/to/candidate/banks \
    --output /path/to/variant-analysis.json
```

Pitch-dependent aliasing can be measured against the Four Seas firmware's
48 kHz, linearly interpolated oscillator model:

```bash
uv run python /path/to/fim-config-tool/src/scripts/analyze_wavetable_aliasing.py \
    --analysis-root "$PWD" \
    --variant legacy=/path/to/baseline/banks \
    --variant candidate=/path/to/candidate/banks \
    --output /path/to/alias-analysis.json \
    --preview-output /path/to/alias-previews
```

The report expresses interpolation-weighted harmonic power above Nyquist
relative to valid harmonic power in dB. Preview groups contain candidate raw,
pitch-bandlimited reference, matching legacy raw, and a normalized isolated
alias signal.
