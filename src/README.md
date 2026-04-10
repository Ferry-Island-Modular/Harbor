# fim-config-tool — C++/Qt rewrite (Phase 0+)

This directory is a staging area for the C++/Qt rewrite. At Phase 6 cutover it
will be flattened to the repo root and the current Python tree will move to
`legacy/python/`.

## Prerequisites
- Qt 6.7+ (`brew install qt` on macOS)
- CMake 3.25+
- Node / npx (for dart-sass)
- clang-format

## Build
```bash
cd cpp
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
./build/fim-config-tool
```

See `docs/superpowers/plans/` (repo root, not this directory) for the phase plans.
