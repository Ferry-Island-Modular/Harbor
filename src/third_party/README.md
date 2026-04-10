# Vendored third-party single-header libraries

These libraries are checked into the repo as a stable snapshot rather than
fetched at build time. To update one, edit the relevant `curl` invocation in
`docs/superpowers/plans/2026-04-07-cpp-qt-rewrite-phase-0.md` (Task 6) and
re-run the download. Keep the version column below in sync with what's
actually on disk.

| Library    | Version / SHA            | License            | Source                                               |
|------------|--------------------------|--------------------|------------------------------------------------------|
| miniaudio  | 0.11.22                  | public domain / MIT-0 | <https://github.com/mackron/miniaudio>            |
| dr_wav     | wav-0.14.5               | public domain / MIT-0 | <https://github.com/mackron/dr_libs>              |
| PFFFT      | 09796885cd5b (2026-01-05)| BSD-like (FFTPACK)    | <https://bitbucket.org/jpommier/pffft>            |

## Other dependencies

- **Four-Seas** (the wavetable engine) is a git submodule under
  `cpp/third_party/Four-Seas/`, pulled recursively (it brings its own DaisySP,
  libDaisy, and stmlib nested submodules).
- **libsamplerate**, **Catch2**, **spdlog** are not vendored — they come via
  CMake `FetchContent` from GitHub at pinned tags. See `cpp/CMakeLists.txt`.
