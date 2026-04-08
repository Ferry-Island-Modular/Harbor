# Phase 0 Implementation Plan — Scaffold + Cross-Platform CI Matrix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a CMake-based C++/Qt 6 project that builds a styled hello-world window on four CI runners (macOS arm64, macOS x86_64, Ubuntu, Windows), with the FourSeas engine submoduled in, all third-party dependencies wired up, and the SCSS→QSS build step working end-to-end.

**Architecture:** All C++ work lives under `cpp/` during the rewrite (staging subdirectory). The FourSeas engine comes in as a git submodule at `cpp/third_party/Four-Seas/`. Single-header libraries (miniaudio, dr_wav, PFFFT) are vendored into `cpp/third_party/` as checked-in files. CMake-native libraries (libsamplerate, Catch2, spdlog) come via `FetchContent`. Styles transpile from `cpp/styles/input.scss` → `generated/app.qss` via a CMake `add_custom_command` invoking `npx sass`, and the result is embedded via a Qt `.qrc` resource. At Phase 6 cutover, `cpp/*` gets flattened to the repo root.

**Tech Stack:** CMake 3.25+, Qt 6.7+, C++17, dart-sass via npx, GitHub Actions, clang-format, aqtinstall (for CI Qt), Catch2 v3, spdlog, libsamplerate, miniaudio, dr_wav, PFFFT

**Work branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## Prerequisites (before Task 1)

These are one-time installs the human needs to do before the plan can start executing. They are NOT tasks because they involve OS-level installations that shouldn't be automated by a plan.

1. **Install Qt 6.7+** on the dev machine. Simplest path on macOS:
   ```bash
   brew install qt
   ```
   Verify: `brew --prefix qt` should print a path, and `$(brew --prefix qt)/bin/qmake6 --version` should print `QMake version ... Using Qt version 6.7+`.

2. **Verify CMake ≥ 3.25**:
   ```bash
   cmake --version
   ```
   Expected: `cmake version 3.25.0` or higher. (This machine already has 3.28.1 from STM32CubeCLT.)

3. **Verify Node / npx**:
   ```bash
   npx --version
   ```
   Expected: any 8.0+. (This machine has 10.9.2.)

4. **Install clang-format**:
   ```bash
   brew install clang-format
   clang-format --version
   ```
   Expected: `clang-format version 17.0+` (any recent version is fine).

Once all four are verified, execute the tasks in order.

---

## Task 1: Create `cpp/` skeleton directory structure

**Files:**
- Create: `cpp/.gitignore`
- Create: `cpp/README.md`

- [ ] **Step 1: Create the directory tree**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
mkdir -p cpp/src/app cpp/src/engine cpp/src/dsp cpp/src/ui
mkdir -p cpp/third_party cpp/styles cpp/resources cpp/tests/dsp cpp/tests/data
mkdir -p cpp/cmake
```

- [ ] **Step 2: Write `cpp/.gitignore`**

```
# Build outputs
build/
cmake-build-*/
generated/

# IDE
.idea/
.vscode/
*.user
CMakeLists.txt.user*
compile_commands.json

# OS
.DS_Store
Thumbs.db
```

- [ ] **Step 3: Write `cpp/README.md`**

```markdown
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
```

- [ ] **Step 4: Commit**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/.gitignore cpp/README.md
git commit -m "feat(cpp): scaffold cpp/ staging directory"
```

---

## Task 2: Minimum viable CMakeLists.txt that finds Qt 6

**Files:**
- Create: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the minimum CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.25)
project(fim-config-tool
    VERSION 0.1.0
    DESCRIPTION "Configuration tool for Ferry Island Modular products"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Export compile commands for clangd/LSP
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Widgets Gui)

# Main executable — empty for now, sources added in later tasks
qt_add_executable(fim-config-tool
    src/app/main.cpp
)

target_link_libraries(fim-config-tool PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
)

# Deployment finalization (on macOS this builds a .app bundle)
if(APPLE)
    set_target_properties(fim-config-tool PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "FIM Config Tool"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ferryislandmodular.fim-config-tool"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# Qt 6.3+ qt_add_executable auto-finalizes; no explicit qt_finalize_executable needed.
```

- [ ] **Step 2: Commit** (CMakeLists without the main.cpp it references will fail to configure — that's fine, we fix it in the next task)

```bash
git add cpp/CMakeLists.txt
git commit -m "feat(cpp): add minimal CMakeLists.txt with Qt 6 target"
```

---

## Task 3: Write `main.cpp` with empty QMainWindow

**Files:**
- Create: `cpp/src/app/main.cpp`

- [ ] **Step 1: Write `cpp/src/app/main.cpp`**

```cpp
#include <QApplication>
#include <QLabel>
#include <QMainWindow>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Ferry Island Modular");
    app.setApplicationName("FIM Config Tool");

    QMainWindow window;
    window.setWindowTitle("FIM Config Tool");
    window.resize(1000, 378);

    auto* label = new QLabel("Hello, FourSeas", &window);
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);

    window.show();
    return app.exec();
}
```

- [ ] **Step 2: Configure the build**

```bash
cd cpp
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
```

Expected output ends with:
```
-- Configuring done
-- Generating done
-- Build files have been written to: .../cpp/build
```

If Ninja is not installed, use `-G "Unix Makefiles"` instead.

- [ ] **Step 3: Build**

```bash
cmake --build build
```

Expected: compiles `main.cpp` and links `fim-config-tool.app` (macOS) or `fim-config-tool` (Linux/Windows). No warnings.

- [ ] **Step 4: Run and visually verify**

```bash
open build/fim-config-tool.app
```

(On Linux: `./build/fim-config-tool`; on Windows: `build\fim-config-tool.exe`.)

Expected: a blank 1000×378 window appears with "Hello, FourSeas" centered. Close it.

- [ ] **Step 5: Commit**

```bash
git add cpp/src/app/main.cpp
git commit -m "feat(cpp): add minimal hello-world Qt main window"
```

---

## Task 4: Add the FourSeas git submodule

**Files:**
- Create: `.gitmodules` (repo root)
- Create: `cpp/third_party/Four-Seas/` (submodule)

- [ ] **Step 1: Add the submodule, recursive to pull stmlib**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git submodule add https://github.com/Ferry-Island-Modular/Four-Seas.git cpp/third_party/Four-Seas
git submodule update --init --recursive cpp/third_party/Four-Seas
```

Expected: clones the repo and its stmlib/DaisySP/libDaisy submodules.

- [ ] **Step 2: Verify the engine header is present**

```bash
test -f cpp/third_party/Four-Seas/wavetable_oscillator.h && echo "OK: engine header present"
test -d cpp/third_party/Four-Seas/stmlib && echo "OK: stmlib present"
```

Both lines should print `OK: ...`.

- [ ] **Step 3: Pin to current HEAD (document the SHA)**

```bash
(cd cpp/third_party/Four-Seas && git rev-parse HEAD > /tmp/foursea_sha.txt && cat /tmp/foursea_sha.txt)
```

Save this SHA — reference it in the commit message below.

- [ ] **Step 4: Commit**

```bash
SHA=$(cd cpp/third_party/Four-Seas && git rev-parse --short HEAD)
git add .gitmodules cpp/third_party/Four-Seas
git commit -m "feat(cpp): add Four-Seas submodule at ${SHA}"
```

---

## Task 5: Wire FourSeas include paths into CMake and prove template instantiation

**Files:**
- Modify: `cpp/CMakeLists.txt`
- Create: `cpp/src/engine/engine_smoke.cpp`

- [ ] **Step 1: Add the FourSeas include paths to CMakeLists.txt**

Add the following after `find_package(Qt6 ...)` but before `qt_add_executable`:

```cmake
# FourSeas engine (via submodule).
#
# wavetable_oscillator.h is logically header-only, but it transitively pulls
# in src/params.h -> daisysp.h -> the entire DaisySP/libDaisy header tree.
# To compile the engine for a desktop host we need 10 include paths and 3
# preprocessor defines. We do NOT compile any DaisySP/libDaisy translation
# units — WavetableOscillator only references inline header-only utilities,
# so the resulting binary contains zero embedded baggage.
#
# This recipe is adapted from the existing pybind11 binding's CMakeLists
# (private FourSeas monorepo, python-preview branch, commit 8d36c15).
set(FOURSEAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Four-Seas")
set(DAISYSP_DIR   "${FOURSEAS_ROOT}/DaisySP")
set(LIBDAISY_DIR  "${FOURSEAS_ROOT}/libDaisy")
set(STMLIB_DIR    "${FOURSEAS_ROOT}/stmlib")

if(NOT EXISTS "${FOURSEAS_ROOT}/wavetable_oscillator.h")
    message(FATAL_ERROR
        "FourSeas submodule not initialized. Run: "
        "git submodule update --init --recursive cpp/third_party/Four-Seas")
endif()
if(NOT EXISTS "${DAISYSP_DIR}/Source/daisysp.h")
    message(FATAL_ERROR
        "DaisySP nested submodule missing under Four-Seas/DaisySP. "
        "Run: git submodule update --init --recursive cpp/third_party/Four-Seas")
endif()

add_library(fourseas_engine INTERFACE)
target_include_directories(fourseas_engine SYSTEM INTERFACE
    "${FOURSEAS_ROOT}"
    "${FOURSEAS_ROOT}/src"
    "${DAISYSP_DIR}/Source"
    "${DAISYSP_DIR}/Source/Synthesis"
    "${DAISYSP_DIR}/Source/Utility"
    "${STMLIB_DIR}"
    "${LIBDAISY_DIR}"
    "${LIBDAISY_DIR}/src"
    "${LIBDAISY_DIR}/src/sys"
    "${LIBDAISY_DIR}/Middlewares/Third_Party/FatFs/src"
)
target_compile_definitions(fourseas_engine INTERFACE
    UNIT_TEST=1
    CURRENT_WAVE_SAMPLES=2048
    IWDG_HandleTypeDef=int
)
# Include directories above are marked SYSTEM so warnings from vendored
# embedded code (e.g. ARM __asm constraints in stmlib firing on host
# targets) don't pollute our build output.
```

Then update the executable block to add the smoke source and link the engine:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/engine/engine_smoke.cpp
)

target_link_libraries(fim-config-tool PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    fourseas_engine
)
```

- [ ] **Step 2: Write the smoke-test compilation unit**

`cpp/src/engine/engine_smoke.cpp`:

```cpp
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
```

- [ ] **Step 3: Reconfigure and rebuild**

```bash
cd cpp
cmake --build build
```

Expected: compiles without errors. If you get `wavetable_oscillator.h: No such file or directory`, the include path is wrong — verify the submodule paths.

**Note on the include chain:** `wavetable_oscillator.h` `#includes` `"src/params.h"` which itself includes `"daisysp.h"`. DaisySP transitively pulls in `daisy.h`, which pulls in libDaisy headers and FatFS. The 10-path recipe above satisfies the entire chain. The `IWDG_HandleTypeDef=int` define mocks a hardware watchdog type that isn't defined when not cross-compiling for Cortex-M4. None of the libDaisy/DaisySP/FatFS code gets compiled into the binary — these are header-only references for the smoke test.

- [ ] **Step 4: Run the app to verify runtime still works**

```bash
open build/fim-config-tool.app
```

Expected: same blank window as before (the smoke test has no runtime effect).

- [ ] **Step 5: Commit**

```bash
git add cpp/CMakeLists.txt cpp/src/engine/engine_smoke.cpp
git commit -m "feat(cpp): wire FourSeas engine headers + smoke instantiation"
```

---

## Task 6: Vendor single-header libraries (miniaudio, dr_wav, PFFFT)

**Files:**
- Create: `cpp/third_party/miniaudio/miniaudio.h`
- Create: `cpp/third_party/dr_libs/dr_wav.h`
- Create: `cpp/third_party/pffft/pffft.c`
- Create: `cpp/third_party/pffft/pffft.h`
- Create: `cpp/third_party/README.md`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Download miniaudio.h at a pinned release**

Use release 0.11.22 (pinned tag, not `master`, for reproducibility):

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
mkdir -p cpp/third_party/miniaudio
curl -L -o cpp/third_party/miniaudio/miniaudio.h \
    https://raw.githubusercontent.com/mackron/miniaudio/0.11.22/miniaudio.h
test -f cpp/third_party/miniaudio/miniaudio.h && \
    head -5 cpp/third_party/miniaudio/miniaudio.h
```

Expected: the first 5 lines print and include a version banner mentioning "miniaudio".

- [ ] **Step 2: Download dr_wav.h at a pinned release**

```bash
mkdir -p cpp/third_party/dr_libs
curl -L -o cpp/third_party/dr_libs/dr_wav.h \
    https://raw.githubusercontent.com/mackron/dr_libs/0.13.16/dr_wav.h
test -f cpp/third_party/dr_libs/dr_wav.h && head -5 cpp/third_party/dr_libs/dr_wav.h
```

Expected: first 5 lines print, mention "dr_wav".

If tag `0.13.16` doesn't exist by the time this plan runs, browse https://github.com/mackron/dr_libs/tags and pick the latest release tag; update this step to use that tag.

- [ ] **Step 3: Download PFFFT at a pinned commit**

PFFFT does not publish tagged releases; use a known-good commit SHA:

```bash
mkdir -p cpp/third_party/pffft
PFFFT_SHA=e0bf595c98ded55cc457a371c1b29c8cab552628
curl -L -o cpp/third_party/pffft/pffft.c \
    https://raw.githubusercontent.com/marton78/pffft/${PFFFT_SHA}/pffft.c
curl -L -o cpp/third_party/pffft/pffft.h \
    https://raw.githubusercontent.com/marton78/pffft/${PFFFT_SHA}/pffft.h
test -f cpp/third_party/pffft/pffft.c && test -f cpp/third_party/pffft/pffft.h && echo "OK"
```

Expected: prints `OK`.

- [ ] **Step 4: Write `cpp/third_party/README.md` documenting provenance**

```markdown
# Vendored third-party single-header libraries

| Library | Version/SHA | License | Source |
|---|---|---|---|
| miniaudio | 0.11.22 | public domain / MIT-0 | https://github.com/mackron/miniaudio |
| dr_wav | 0.13.16 | public domain / MIT-0 | https://github.com/mackron/dr_libs |
| PFFFT | e0bf595c | BSD-like | https://github.com/marton78/pffft |

To update any of these, edit the version in the relevant curl command in
`docs/superpowers/plans/2026-04-07-cpp-qt-rewrite-phase-0.md` (Task 6) and
re-run the downloads. Keep the version column above in sync.

Other dependencies (libsamplerate, Catch2, spdlog) come via CMake FetchContent
and are not checked in.
```

- [ ] **Step 5: Add PFFFT as a CMake target**

Add this block to `cpp/CMakeLists.txt` after the `fourseas_engine` block:

```cmake
# PFFFT — vendored single-translation-unit library
add_library(pffft STATIC
    third_party/pffft/pffft.c
)
target_include_directories(pffft PUBLIC
    third_party/pffft
)
# PFFFT needs no special defines; it auto-detects SSE/NEON.
set_target_properties(pffft PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)

# Single-header libs (miniaudio, dr_wav) — interface-only targets for include paths
add_library(miniaudio_headers INTERFACE)
target_include_directories(miniaudio_headers INTERFACE third_party/miniaudio)

add_library(dr_wav_headers INTERFACE)
target_include_directories(dr_wav_headers INTERFACE third_party/dr_libs)
```

Then update `target_link_libraries` for the main executable:

```cmake
target_link_libraries(fim-config-tool PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
)
```

- [ ] **Step 6: Rebuild to verify PFFFT compiles cleanly**

```bash
cd cpp
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

Expected: PFFFT compiles without errors. You may see benign `-Wunused-function` or similar warnings inside `pffft.c` — acceptable for a vendored library. If warnings fail the build because of `-Werror`, you can silence them by adding `target_compile_options(pffft PRIVATE -w)` to the PFFFT target block.

- [ ] **Step 7: Commit**

```bash
git add cpp/third_party/miniaudio cpp/third_party/dr_libs cpp/third_party/pffft \
        cpp/third_party/README.md cpp/CMakeLists.txt
git commit -m "feat(cpp): vendor miniaudio, dr_wav, PFFFT single-header libraries"
```

---

## Task 7: Add libsamplerate, Catch2, spdlog via FetchContent

**Files:**
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Add FetchContent declarations**

Add this block near the top of `cpp/CMakeLists.txt`, after `find_package(Qt6 ...)`:

```cmake
include(FetchContent)

# libsamplerate — BSD-2, CMake-native
FetchContent_Declare(
    libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG        0.2.2
    GIT_SHALLOW    TRUE
)
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libsamplerate)

# Catch2 v3 — test framework
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# spdlog — logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)
```

- [ ] **Step 2: Link libsamplerate and spdlog to the main target**

Update `target_link_libraries`:

```cmake
target_link_libraries(fim-config-tool PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
    SampleRate::samplerate
    spdlog::spdlog
)
```

Catch2 is linked only from the test executable, added in Task 8.

- [ ] **Step 3: Reconfigure — first configure will be slow (downloading deps)**

```bash
cd cpp
rm -rf build
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
```

Expected: CMake prints `-- Populating libsamplerate`, `-- Populating Catch2`, `-- Populating spdlog` (these each take 10–60 seconds on first run; cached after). Final output ends with `-- Build files have been written to: .../cpp/build`.

- [ ] **Step 4: Build**

```bash
cmake --build build
```

Expected: compiles cleanly. Build time may be several minutes on first run because libsamplerate, Catch2, and spdlog each compile themselves.

- [ ] **Step 5: Commit**

```bash
git add cpp/CMakeLists.txt
git commit -m "feat(cpp): fetch libsamplerate, Catch2, spdlog via FetchContent"
```

---

## Task 8: Add Catch2 test scaffold + a trivial passing test

**Files:**
- Create: `cpp/tests/CMakeLists.txt`
- Create: `cpp/tests/smoke_test.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write `cpp/tests/CMakeLists.txt`**

```cmake
add_executable(fim-tests
    smoke_test.cpp
)

target_link_libraries(fim-tests PRIVATE
    Catch2::Catch2WithMain
    fourseas_engine
    pffft
    SampleRate::samplerate
    spdlog::spdlog
)

include(Catch)
catch_discover_tests(fim-tests)
```

- [ ] **Step 2: Write `cpp/tests/smoke_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "wavetable_oscillator.h"

TEST_CASE("FourSeas WavetableOscillator template instantiates", "[smoke]") {
    using Osc = fourseas::WavetableOscillator<2048, false, false>;
    Osc osc;
    REQUIRE(sizeof(Osc) > 0);
}

TEST_CASE("Basic arithmetic sanity check", "[smoke]") {
    REQUIRE(2 + 2 == 4);
}
```

- [ ] **Step 3: Enable testing and add the tests subdirectory in the top-level CMakeLists.txt**

Add near the top of `cpp/CMakeLists.txt`, after the `project(...)` call:

```cmake
enable_testing()
```

Add at the bottom of `cpp/CMakeLists.txt`:

```cmake
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

Note: `BUILD_TESTING` is set to `ON` by default when `enable_testing()` is called, unless explicitly overridden.

- [ ] **Step 4: Reconfigure, build, and run tests**

```bash
cd cpp
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `2 tests from 1 test suite ran. (X ms total)` with 0 failures.

- [ ] **Step 5: Commit**

```bash
git add cpp/tests/CMakeLists.txt cpp/tests/smoke_test.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): add Catch2 test scaffold and smoke tests"
```

---

## Task 9: Copy input.scss and wire the SCSS→QSS CMake rule

**Files:**
- Create: `cpp/styles/input.scss` (copied from current Python tree)
- Create: `cpp/resources/app.qrc`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/src/app/main.cpp`

- [ ] **Step 1: Copy the SCSS from the existing Python tree**

Since the Python tree isn't checked out in this worktree (we're on the `cpp-qt-rewrite` branch which has no Python files yet), copy from the master branch via `git show`:

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git show master:src/config_tool/input.scss > cpp/styles/input.scss
wc -l cpp/styles/input.scss
```

Expected: line count > 100 (the Python version has variables, mixins, and component rules).

- [ ] **Step 2: Add the dart-sass transpile rule to CMakeLists.txt**

Add this block after the Qt `find_package`:

```cmake
# Transpile SCSS -> QSS at build time via dart-sass (npx)
find_program(NPX_EXECUTABLE npx REQUIRED)

set(SCSS_INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/styles/input.scss")
set(QSS_OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/app.qss")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT  "${QSS_OUTPUT}"
    COMMAND "${NPX_EXECUTABLE}" --yes sass
            "${SCSS_INPUT}"
            "${QSS_OUTPUT}"
            --no-source-map
            --style=expanded
    DEPENDS "${SCSS_INPUT}"
    COMMENT "Transpiling input.scss -> app.qss via dart-sass"
    VERBATIM
)

add_custom_target(transpile_qss ALL DEPENDS "${QSS_OUTPUT}")
```

- [ ] **Step 3: Write `cpp/resources/app.qrc`**

```xml
<!DOCTYPE RCC><RCC version="1.0">
<qresource prefix="/">
    <file alias="app.qss">../build/generated/app.qss</file>
</qresource>
</RCC>
```

**Important:** Qt's `rcc` resolves relative paths from the `.qrc` file's own location. The path above assumes a single `build/` directory co-located with `resources/` at the `cpp/` level. If you use a different build directory name or location, update the path.

- [ ] **Step 4: Add the resource file to the executable target in CMakeLists.txt**

Modify the `qt_add_executable` call:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/engine/engine_smoke.cpp
    resources/app.qrc
)

add_dependencies(fim-config-tool transpile_qss)
```

- [ ] **Step 5: Update `main.cpp` to load the stylesheet and display a styled label**

Replace `cpp/src/app/main.cpp` with:

```cpp
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMainWindow>
#include <QTextStream>

namespace {
QString LoadStylesheet() {
    QFile f(":/app.qss");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&f);
    return in.readAll();
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Ferry Island Modular");
    app.setApplicationName("FIM Config Tool");

    const QString qss = LoadStylesheet();
    if (!qss.isEmpty()) {
        app.setStyleSheet(qss);
    }

    QMainWindow window;
    window.setWindowTitle("FIM Config Tool");
    window.resize(1000, 378);

    auto* label = new QLabel("Hello, FourSeas", &window);
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);

    window.show();
    return app.exec();
}
```

- [ ] **Step 6: Reconfigure and build**

```bash
cd cpp
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

Expected: CMake prints `Transpiling input.scss -> app.qss via dart-sass`, the generated `build/generated/app.qss` file exists, and the executable links successfully.

- [ ] **Step 7: Run and verify the dark theme applies**

```bash
open build/fim-config-tool.app
```

Expected: the window background is dark (bg `#222222` from `input.scss`), and the label text is white/light, in the Pangram font family *if* Pangram is installed at the OS level — otherwise it falls back to the default system font. Either way, verify the background color is dark, which confirms the QSS is loading.

- [ ] **Step 8: Commit**

```bash
git add cpp/styles/input.scss cpp/resources/app.qrc cpp/CMakeLists.txt cpp/src/app/main.cpp
git commit -m "feat(cpp): wire SCSS->QSS build step and apply stylesheet"
```

---

## Task 10: Add clang-format config

**Files:**
- Create: `cpp/.clang-format`

- [ ] **Step 1: Write `cpp/.clang-format`**

Match the style of the FourSeas firmware's clang-format (the firmware has `.clang-format` at `cpp/third_party/Four-Seas/firmware/.clang-format` — check its contents and mirror them; if unavailable, use this sensible default):

```yaml
---
Language: Cpp
BasedOnStyle: Google
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
BreakBeforeBraces: Attach
AccessModifierOffset: -4
NamespaceIndentation: None
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
PointerAlignment: Left
SpaceAfterCStyleCast: false
Standard: c++17
---
```

- [ ] **Step 2: Format existing source files and verify idempotency**

```bash
cd cpp
clang-format -i src/app/main.cpp src/engine/engine_smoke.cpp tests/smoke_test.cpp
clang-format -i src/app/main.cpp src/engine/engine_smoke.cpp tests/smoke_test.cpp  # second run should be a no-op
git diff --stat cpp/src cpp/tests
```

Expected: After the first run, there may be small formatting changes. After the second run, `git diff` shows nothing further. Review the diff with `git diff` to make sure nothing looks wrong; if you disagree with a style choice, tweak `.clang-format` and re-run.

- [ ] **Step 3: Commit the clang-format config and any reformatted sources**

```bash
git add cpp/.clang-format cpp/src cpp/tests
git commit -m "feat(cpp): add clang-format config and format sources"
```

---

## Task 11: GitHub Actions workflow — `macos-latest` runner first

**Files:**
- Create: `.github/workflows/ci.yml` (repo root, not `cpp/`)

- [ ] **Step 1: Write initial `.github/workflows/ci.yml` with only `macos-latest`**

Start narrow so we can debug one platform before adding the rest. Create `.github/workflows/ci.yml`:

```yaml
name: CI (cpp)

on:
  push:
    branches: [cpp-qt-rewrite, master]
  pull_request:
    branches: [cpp-qt-rewrite, master]

jobs:
  build:
    name: Build - ${{ matrix.os }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: macos-latest
            qt_arch: clang_64
            qt_target: desktop

    steps:
      - name: Checkout (with submodules)
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: '6.7.3'
          host: mac
          target: ${{ matrix.qt_target }}
          arch: ${{ matrix.qt_arch }}
          cache: true

      - name: Install Ninja
        run: brew install ninja

      - name: Install Node (for dart-sass)
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Configure
        working-directory: cpp
        run: cmake -B build -G Ninja

      - name: Build
        working-directory: cpp
        run: cmake --build build --parallel

      - name: Test
        working-directory: cpp
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 2: Commit and push**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add .github/workflows/ci.yml
git commit -m "ci: add macos-latest runner for cpp build"
git push -u origin cpp-qt-rewrite
```

- [ ] **Step 3: Watch the workflow run**

```bash
gh run watch
```

Expected: first run may fail (especially because of Qt install quirks or platform-specific issues). **This task is "done" when the `macos-latest` job succeeds.**

- [ ] **Step 4: Fix any failures, iterating as needed**

Common failures and fixes:
- **`install-qt-action` fails to find version 6.7.3:** check the [Qt online repo](https://github.com/miurahr/aqtinstall#install-qt) for the available versions; update to the latest available 6.7.x.
- **CMake can't find Qt:** `install-qt-action` sets `Qt6_DIR` automatically; if the configure step can't find Qt6 anyway, add `-DCMAKE_PREFIX_PATH=${{ env.Qt6_DIR }}` to the configure command.
- **`npx sass` hangs because of an interactive prompt:** `--yes` is already in the command but if it's still prompting, replace with `npm install -g sass && sass ...` in both the CI step and the CMakeLists (only as a fallback).
- **Build fails with a FourSeas header include error:** double-check the submodule was recursively cloned (`submodules: recursive` in the checkout action).

Iterate: commit fixes, push, watch, repeat until green.

- [ ] **Step 5: Once green, commit any follow-up fixes**

No additional action needed — commits from Step 4 already cover this. Verify with:

```bash
gh run list --limit 5
```

Expected: the most recent run is green.

---

## Task 12: Extend CI matrix with `macos-13` (x86_64)

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add a matrix entry for `macos-13`**

In `.github/workflows/ci.yml`, add to the `matrix.include` list:

```yaml
          - os: macos-13
            qt_arch: clang_64
            qt_target: desktop
```

- [ ] **Step 2: Commit and push**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add macos-13 (x86_64) runner"
git push
gh run watch
```

- [ ] **Step 3: Fix failures and iterate**

`macos-13` is x86_64 and has a slightly different Homebrew prefix (`/usr/local/opt` instead of `/opt/homebrew/opt`). If the build fails because of a hard-coded path, fix by relying on `install-qt-action`'s env vars rather than `brew --prefix qt`.

**Task is done when both `macos-latest` and `macos-13` jobs are green.**

- [ ] **Step 4: Confirm green**

```bash
gh run list --limit 3
```

Expected: latest run shows both macOS jobs passing.

---

## Task 13: Extend CI matrix with `ubuntu-latest`

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add Ubuntu entry to the matrix**

```yaml
          - os: ubuntu-latest
            qt_arch: linux_gcc_64
            qt_target: desktop
```

- [ ] **Step 2: Add Ubuntu-specific system deps**

Qt on Linux needs additional system packages. Add this step to the workflow, conditional on Linux:

```yaml
      - name: Install Linux system deps
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            ninja-build \
            libgl1-mesa-dev \
            libxkbcommon-dev \
            libegl1-mesa-dev \
            libfontconfig1 \
            libxcb-xinerama0 \
            libxcb-cursor0 \
            libpulse-dev \
            libasound2-dev
```

Place this step **after** "Checkout" and **before** "Install Qt". Also make the existing "Install Ninja" step conditional on macOS:

```yaml
      - name: Install Ninja (macOS)
        if: runner.os == 'macOS'
        run: brew install ninja
```

- [ ] **Step 3: Commit, push, iterate**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add ubuntu-latest runner"
git push
gh run watch
```

- [ ] **Step 4: Fix Linux-specific failures**

Common issues:
- **Missing X11/Wayland libs:** add more packages to the system deps step. Qt 6's exact system library list varies; the `install-qt-action` docs have a recommended list.
- **`libsamplerate` build fails:** check that gcc is being used and not some stale toolchain.

**Task is done when all three jobs (macos-latest, macos-13, ubuntu-latest) are green.**

---

## Task 14: Extend CI matrix with `windows-latest`

**Files:**
- Modify: `.github/workflows/ci.yml`

This is the highest-risk runner — MSVC + Qt + CMake interactions are the usual cause of Phase 0 escape-hatch bailouts.

- [ ] **Step 1: Add Windows entry to the matrix**

```yaml
          - os: windows-latest
            qt_arch: win64_msvc2022_64
            qt_target: desktop
```

- [ ] **Step 2: Windows-specific build step adjustments**

MSVC on Windows uses a different generator. The simplest fix is to let CMake pick the default Visual Studio generator by omitting `-G Ninja` on Windows. Update the Configure step:

```yaml
      - name: Configure (Windows)
        if: runner.os == 'Windows'
        working-directory: cpp
        run: cmake -B build -A x64

      - name: Configure (Unix)
        if: runner.os != 'Windows'
        working-directory: cpp
        run: cmake -B build -G Ninja
```

And update the Build step to pass `--config Release` on Windows:

```yaml
      - name: Build (Windows)
        if: runner.os == 'Windows'
        working-directory: cpp
        run: cmake --build build --parallel --config Release

      - name: Build (Unix)
        if: runner.os != 'Windows'
        working-directory: cpp
        run: cmake --build build --parallel
```

Similarly for the test step:

```yaml
      - name: Test (Windows)
        if: runner.os == 'Windows'
        working-directory: cpp
        run: ctest --test-dir build --output-on-failure -C Release

      - name: Test (Unix)
        if: runner.os != 'Windows'
        working-directory: cpp
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 3: Commit, push, watch, iterate**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add windows-latest runner"
git push
gh run watch
```

- [ ] **Step 4: Fix Windows-specific failures**

Common issues and mitigations:
- **PFFFT `pffft.c` fails with MSVC syntax errors:** PFFFT has historically had MSVC issues with SSE intrinsics. If this happens, add a compile flag to disable SSE: `target_compile_definitions(pffft PRIVATE PFFFT_SIMD_DISABLE)`.
- **`target_link_libraries` can't find `SampleRate::samplerate`:** the target name may differ; check `libsamplerate-config.cmake` or use `samplerate` unqualified.
- **`dr_wav.h` MSVC warnings treated as errors:** the vendored single-header libs often have platform-specific warning sources. Suppress via `target_compile_options(miniaudio_headers INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/wd4100 /wd4244>)` and similar.
- **`npx sass` not found in PATH:** Windows-specific PATH issue. Ensure `actions/setup-node@v4` has run before the build step.
- **Qt deployment: executable can't find Qt DLLs at runtime:** add a `windeployqt` step post-build to bundle the DLLs alongside the executable. Not needed for CI "does it build" test but will be needed for Phase 5.

**🚦 This is the Phase 0 escape hatch decision point.** If Windows refuses to cooperate after a reasonable effort (say, 2 days of iteration), you have three options:

1. **Drop Windows from the rewrite.** The rewrite ships Mac + Linux only, and Windows stays on the Python branch. Unsatisfying but honest.
2. **Abort the rewrite entirely.** Go back to Python and fix the signing pipeline.
3. **Push through.** If it's close but flaky, power through.

Make this decision explicitly. Document it in `docs/issues.md` before proceeding past this task.

**Task is done when all four jobs are green OR a documented decision has been made about what to drop.**

---

## Task 15: Add clang-format check to CI

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add a separate `lint` job that runs clang-format in check mode**

Add at the top of `jobs:` (before `build:`):

```yaml
  lint:
    name: clang-format check
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install clang-format
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-format

      - name: Check formatting
        working-directory: cpp
        run: |
          find src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
            | xargs clang-format --dry-run --Werror
```

- [ ] **Step 2: Commit, push, verify green**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add clang-format check job"
git push
gh run watch
```

Expected: the `lint` job passes because Task 10 already formatted the sources.

---

## Task 16: Phase 0 checkpoint — verify all four runners green for 3 consecutive pushes

**Files:** none

This is the explicit GO/NO-GO gate that the spec mandates.

- [ ] **Step 1: Verify the most recent run is fully green**

```bash
gh run list --limit 1
```

Expected output includes: `✓ completed success` for the most recent run, and the run name is the latest commit message.

- [ ] **Step 2: Trigger two more successful runs**

Make two more trivial commits (e.g., tweak a comment in `main.cpp` and then revert) and push each one. The goal is to verify stability, not just a single passing run.

Alternative: use `gh workflow run` to re-run the existing workflow twice:

```bash
gh workflow run "CI (cpp)" --ref cpp-qt-rewrite
# wait for completion
gh run watch
gh workflow run "CI (cpp)" --ref cpp-qt-rewrite
gh run watch
```

- [ ] **Step 3: Verify 3 consecutive green runs**

```bash
gh run list --limit 5
```

Expected: the three most recent runs on `cpp-qt-rewrite` branch are all `✓ completed success`.

- [ ] **Step 4: Write Phase 0 completion note**

```bash
mkdir -p docs
cat > docs/phase-0-complete.md <<'EOF'
# Phase 0 Complete

As of this commit, Phase 0 of the C++/Qt rewrite is complete:

- CMake project builds on all four CI runners (macos-latest, macos-13,
  ubuntu-latest, windows-latest)
- FourSeas engine submodule is wired in and the WavetableOscillator template
  instantiates on clang, gcc, and MSVC
- All third-party deps (miniaudio, dr_wav, PFFFT, libsamplerate, Catch2,
  spdlog) are available and linked
- SCSS -> QSS build step works via npx sass
- A styled hello-world Qt window launches on macOS (dark theme visible)
- Catch2 test scaffold runs a smoke test

Next phase: see docs/superpowers/plans/... (phase-1 plan, written at the
start of Phase 1).
EOF
git add docs/phase-0-complete.md
git commit -m "docs: mark Phase 0 complete"
git push
```

- [ ] **Step 5: Update the implementation tracking memory**

The writing-plans skill leaves this open-ended. At minimum, note in the session handoff (or a new memory entry) that Phase 0 is complete and Phase 1 plan-writing is the next session's first task.

---

## Phase 0 done when:

1. ✅ Hello-world Qt window launches locally with dark QSS theme
2. ✅ `wavetable_oscillator.h` template instantiates in both the main executable and the Catch2 test suite
3. ✅ All four CI runners are green across 3+ consecutive pushes
4. ✅ clang-format CI check passes
5. ✅ `docs/phase-0-complete.md` committed

**Phase 1 plan gets written in a new session, starting from the post-Phase-0 state.**
