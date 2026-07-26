cmake_minimum_required(VERSION 3.25)
project(fim-config-tool
    VERSION 0.1.0
    DESCRIPTION "Configuration tool for Ferry Island Modular products"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FIM_BUILD_TESTS "Build fim-config-tool unit tests" ON)
option(FIM_BUILD_EVAL_TOOLS "Build deterministic wavetable evaluation tools" OFF)
if(FIM_BUILD_TESTS)
    enable_testing()
endif()

# Explicit default build type. FetchContent dependencies can
# silently set this if we don't, leading to non-deterministic optimization.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Gui)

# ----------------------------------------------------------------------------
# FetchContent dependencies
# ----------------------------------------------------------------------------
include(FetchContent)

# CMake 4.x removed compatibility with cmake_minimum_required(VERSION < 3.5).
# Some of our dependencies (notably libsamplerate 0.2.2) still declare an
# older minimum and fail to configure on CMake 4.x without this override.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")

# libsamplerate — BSD-2, CMake-native sample-rate converter
FetchContent_Declare(
    libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG        0.2.2
    GIT_SHALLOW    TRUE
)
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL  OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
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

# FourSeas engine (via submodule).
#
# wavetable_oscillator.h is logically header-only, but it transitively pulls
# in src/params.h -> daisysp.h -> the entire DaisySP/libDaisy header tree.
# To compile the engine for a desktop host we need:
#   - the FourSeas src/ directory (params.h, app_state.h, constants.h)
#   - DaisySP headers (Source + Synthesis + Utility)
#   - stmlib (header-only DSP utilities)
#   - libDaisy headers (DaisySP transitively pulls in daisy.h)
#   - FatFS headers (libDaisy includes ff.h)
#   - UNIT_TEST=1 to opt into host-friendly code paths in the firmware
#   - CURRENT_WAVE_SAMPLES to pin the wavetable size at compile time
#   - IWDG_HandleTypeDef mocked to int (the watchdog hardware type isn't
#     defined on host)
#
# We DO NOT compile any DaisySP/libDaisy translation units. Headers are
# parsed at compile time but produce no binary output unless something in
# our own code references them. WavetableOscillator only uses inline
# header-only utilities (daisysp::fclamp, stmlib DSP), so the resulting
# binary contains zero embedded baggage despite the long include path list.
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
        "git submodule update --init --recursive src/third_party/Four-Seas")
endif()
if(NOT EXISTS "${DAISYSP_DIR}/Source/daisysp.h")
    message(FATAL_ERROR
        "DaisySP nested submodule missing under Four-Seas/DaisySP. "
        "Run: git submodule update --init --recursive src/third_party/Four-Seas")
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
    # stmlib's dsp.h gates ARM Cortex-M4 inline assembly behind `#ifdef TEST`,
    # falling back to portable C when defined. Without this, MSVC chokes on
    # GNU `__asm` syntax (clang/gcc just emit warnings).
    TEST=1
    CURRENT_WAVE_SAMPLES=2048
    IWDG_HandleTypeDef=int
)
# Include directories above are marked SYSTEM so warnings from vendored
# embedded code (e.g. ARM __asm constraints in stmlib firing on host
# targets) don't pollute our build output.

# ----------------------------------------------------------------------------
# SCSS -> QSS transpilation via dart-sass (npx)
# ----------------------------------------------------------------------------
# input.scss is the single source of truth for styling. dart-sass produces
# generated/app.qss at build time, which is then embedded via Qt resources.
find_program(NPX_EXECUTABLE npx REQUIRED)
set(DART_SASS_VERSION "1.101.3")

set(SCSS_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/styles/input.scss")
set(QSS_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/app.qss")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT  "${QSS_OUTPUT}"
    COMMAND "${NPX_EXECUTABLE}" --yes "sass@${DART_SASS_VERSION}"
            "${SCSS_INPUT}"
            "${QSS_OUTPUT}"
            --no-source-map
            --style=expanded
    DEPENDS "${SCSS_INPUT}"
    COMMENT "Transpiling input.scss -> app.qss via dart-sass"
    VERBATIM
)

add_custom_target(transpile_qss ALL DEPENDS "${QSS_OUTPUT}")

# ----------------------------------------------------------------------------
# Vendored single-header / single-translation-unit libraries
# ----------------------------------------------------------------------------
# See third_party/README.md for provenance and licenses.

# PFFFT — single-translation-unit FFT library (BSD-like).
add_library(pffft STATIC
    third_party/pffft/pffft.c
)
target_include_directories(pffft SYSTEM PUBLIC
    third_party/pffft
)
set_target_properties(pffft PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)
# pffft.c is vendored embedded-style code; suppress its warnings.
if(MSVC)
    target_compile_options(pffft PRIVATE /w)
else()
    target_compile_options(pffft PRIVATE -w)
endif()

# miniaudio — single-header audio I/O library (public domain).
add_library(miniaudio_headers INTERFACE)
target_include_directories(miniaudio_headers SYSTEM INTERFACE
    third_party/miniaudio
)

# dr_wav — single-header WAV reader/writer (public domain).
add_library(dr_wav_headers INTERFACE)
target_include_directories(dr_wav_headers SYSTEM INTERFACE
    third_party/dr_libs
)

# Main executable
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/app/services/generate_service_base.cpp
    src/app/services/serum_wav_service.cpp
    src/app/services/three_wav_service.cpp
    src/app/services/export_writer.cpp
    src/app/services/single_wav_service.cpp
    src/dsp/window.cpp
    src/dsp/wav_loader.cpp
    src/dsp/resample.cpp
    src/dsp/real_fft.cpp
    src/dsp/stft.cpp
    src/dsp/fft_resampler.cpp
    src/dsp/spectral_modifier.cpp
    src/dsp/source_frame_selector.cpp
    src/dsp/cycle_extractor.cpp
    src/dsp/post_effects.cpp
    src/dsp/serum_loader.cpp
    src/dsp/serum_morpher.cpp
    src/dsp/serum_generator.cpp
    src/dsp/three_wav_generator.cpp
    src/dsp/single_wav_generator.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/main_window.cpp
    src/ui/launcher_screen.cpp
    src/ui/mode_screen_base.cpp
    src/ui/any_wav_screen.cpp
    src/ui/serum_wav_screen.cpp
    src/ui/three_wav_screen.cpp
    src/ui/dialogs/about_dialog.cpp
    src/ui/dialogs/help_dialog.cpp
    src/ui/widgets/card_button.cpp
    src/ui/widgets/file_drop_widget.cpp
    src/ui/widgets/axis_morph_selector.cpp
    src/ui/widgets/custom_progress_bar.cpp
    src/ui/widgets/preview_controls_widget.cpp
)

target_include_directories(fim-config-tool PRIVATE src)

# Pinned version string used in the window title and About dialog. Pulled
# from project(VERSION ...) at the top of this file. The string is exposed
# to the C++ source as a HARBOR_VERSION macro so the title can be a
# compile-time constant.
target_compile_definitions(fim-config-tool PRIVATE
    HARBOR_VERSION="${PROJECT_VERSION}"
)

# Embed the transpiled QSS as a Qt resource. We use qt_add_resources rather
# than a static .qrc file because the source path is in the build directory
# and qt_add_resources handles that without relative-path gymnastics.
qt_add_resources(fim-config-tool "app_styles"
    PREFIX "/"
    BASE   "${CMAKE_CURRENT_BINARY_DIR}/generated"
    FILES  "${QSS_OUTPUT}"
)
add_dependencies(fim-config-tool transpile_qss)

# Embed the bundled Inter font so the stylesheet's font-family: "Inter"
# lookup resolves identically on every machine, regardless of what's
# installed system-wide. SIL OFL licensed — see resources/fonts/LICENSE-Inter.txt.
qt_add_resources(fim-config-tool "app_fonts"
    PREFIX "/fonts"
    BASE   "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
    FILES  "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/InterVariable.ttf"
)

# Embed the help HTML so HelpDialog can load it from :/help/help.html.
qt_add_resources(fim-config-tool "app_help"
    PREFIX "/help"
    BASE   "${CMAKE_CURRENT_SOURCE_DIR}/resources/help"
    FILES  "${CMAKE_CURRENT_SOURCE_DIR}/resources/help/help.html"
)

target_link_libraries(fim-config-tool PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
    SampleRate::samplerate
)

# Output binary name. The CMake target stays "fim-config-tool" (an
# internal stable identifier), but the produced executable / .app bundle
# is named after the user-facing product. macOS uses the spaceless
# "Harbor" because that's also the bundle display name; Linux/Windows
# use the lowercase form for shell-friendliness.
set_target_properties(fim-config-tool PROPERTIES OUTPUT_NAME "Harbor")
if(NOT APPLE AND NOT WIN32)
    set_target_properties(fim-config-tool PROPERTIES OUTPUT_NAME "harbor")
endif()

# Deployment finalization (on macOS this builds a .app bundle)
if(APPLE)
    # Bundle the app icon. The .icns file is generated from
    # resources/icon/harbor-source.svg via scripts/generate-icon.sh —
    # rerun that script after editing the source SVG.
    set(APP_ICON_MACOSX "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/Harbor.icns")
    set_source_files_properties("${APP_ICON_MACOSX}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(fim-config-tool PRIVATE "${APP_ICON_MACOSX}")

    set_target_properties(fim-config-tool PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "Harbor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ferryislandmodular.harbor"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "Harbor.icns"
    )
endif()

# Qt 6.3+ qt_add_executable auto-finalizes; no explicit qt_finalize_executable needed.

if(FIM_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(FIM_BUILD_EVAL_TOOLS)
    add_executable(fim-wavetable-eval
        tools/wavetable_eval.cpp
        src/engine/single_header_impls.cpp
        src/engine/wav_loader.cpp
        src/engine/wavetable_bank.cpp
        src/engine/wavetable_voice.cpp
        src/dsp/window.cpp
        src/dsp/wav_loader.cpp
        src/dsp/real_fft.cpp
        src/dsp/stft.cpp
        src/dsp/fft_resampler.cpp
        src/dsp/spectral_modifier.cpp
        src/dsp/source_frame_selector.cpp
        src/dsp/cycle_extractor.cpp
        src/dsp/post_effects.cpp
        src/dsp/single_wav_generator.cpp
    )
    target_include_directories(fim-wavetable-eval PRIVATE src)
    target_link_libraries(fim-wavetable-eval PRIVATE
        fourseas_engine
        pffft
        miniaudio_headers
        dr_wav_headers
    )
    set_target_properties(fim-wavetable-eval PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
endif()
