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
`manifest.csv` recording the fixed generation seed and paths. The corpus is a
stress set for relative listening comparisons; it is not intended to model
real instruments faithfully.
