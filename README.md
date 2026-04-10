# Harbor

A desktop tool for generating wavetable banks for Ferry Island Modular hardware (Four Seas) and other wavetable synth hosts (Waveedit / Synthesis Technology).

Three input modes:

- **Single .wav** — analyze any audio file and morph its spectrum across the X / Y / Z axes
- **Serum .wav** — convert a Serum wavetable into an 8 x 64 cell bank
- **Three .wavs** — blend three audio files, one per axis, via cross-synthesis

The output is 8 WAV files (one per Z page), each containing 64 single-cycle waveforms ready to load into a wavetable oscillator.

## Status

Beta. macOS Apple Silicon only for distribution today; the source builds on Linux and Windows via CI.

## Building

```bash
git submodule update --init --recursive
cd src
cmake -G Ninja -B build
cmake --build build
open build/Harbor.app
```

See [`src/README.md`](src/README.md) for more details.

## Packaging for distribution

```bash
src/scripts/package-macos.sh
# Output: src/build/dist/Harbor-<version>.dmg
```

## License

MIT — see [LICENSE](LICENSE).
