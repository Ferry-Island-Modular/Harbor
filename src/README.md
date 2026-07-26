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

Use `--candidate` to compare the first qualitative strategy against that
baseline. Candidate mode selects a salient, non-silent source window and
dedicates X exclusively to progression through those frames, leaving spectral
transformation to Y and Z:

```bash
./build/fim-wavetable-eval --candidate --frequency 55 \
    evaluation-output/candidate-source-selection-bass evaluation-corpus/real/*.wav
```
