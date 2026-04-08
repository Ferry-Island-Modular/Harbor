# Phase 2 Implementation Plan — Main UI, no DSP

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Phase 1 throwaway `PreviewWindow` with the production main UI from `designs/start-state.svg` and `designs/any-wav-*.svg` — a launcher screen with three mode cards routing into a full state machine for the "any wav" mode (file drop → axis configuration → stubbed generation → inline preview), with all settings persisted via `QSettings`. No real DSP — the Generate button writes placeholder sine-wave banks so the inline preview can play something.

**Architecture:** A `QMainWindow` hosting a `QStackedWidget` "router" that swaps between top-level screens. Two screens for Phase 2: `LauncherScreen` (the three-card mode picker) and `AnyWavScreen` (the multi-state any-wav flow). Custom widgets — `CardButton`, `FileDropWidget`, `AxisMorphSelector`, `CustomProgressBar`, `PreviewControlsWidget` — are factored into `cpp/src/ui/widgets/` so future screens can reuse them. Generation orchestration lives in `fim::app::SingleWavService`, which uses `fim::app::StubBankWriter` to produce 8 placeholder sine-wave WAV files at `output_waves/audio_resynth/`. The Phase 1 `RealtimeAudioEngine` is reused unchanged, embedded in `PreviewControlsWidget` for the inline preview area. The Phase 1 standalone `PreviewWindow` class is deleted at the end.

**Tech Stack:** C++20, Qt 6.8 Widgets (QMainWindow, QStackedWidget, QFileDialog, QSettings, QThreadPool, QRunnable, drag-and-drop), the Phase 1 engine layer (`fim::engine::*`), `dr_wav` for placeholder bank generation, Catch2 v3 for the testable non-Qt pieces.

**Spec deviations:**

- **⚠️ KNOWN ISSUE — Pangram font is NOT bundled.** The SCSS references `font-family: 'Pangram'` but no font file ships with the app. Qt resolves the family name from the OS font cache at runtime: on machines where Pangram is installed (the developer's Mac, currently), the design renders correctly. **On every other machine — including ALL CI runners and any user who downloads a release — Qt silently falls back to the system default sans, and the app does not look like the design.** This is a real distribution blocker that must be resolved before any release tag. Three possible resolutions: (1) license + bundle Pangram (requires paying Pangram Pangram Foundry), (2) substitute a permissively-licensed lookalike sans (Inter, Public Sans, etc.) and update `input.scss`, (3) accept system fallback as the shipped behavior. Decision is for the user + their designer to make. Tracked loud-and-clear in `docs/followups.md` as the top item.
- **Serum mode is fully deferred.** The launcher's "Serum .wav file" card stays as a clickable card that opens a "Coming soon" placeholder screen. The full Serum flow lands in a later phase once designs exist for it. The "three .wav files" card stays disabled with the "Coming soon!" label, matching `start-state.svg`.
- **`AxisMorphSelector` is implemented in Phase 2 even though the spec assigned it to Serum-only.** The new `any-wav-file-set.svg` design uses Y/Z axis selectors in the "any wav" mode, so the widget needs to exist now. Labels are placeholder strings ("First option", "Second option", "Third option") because the real DSP behaviors are Phase 3 work.
- **Progress bar uses simulated progress.** `SingleWavService` runs a `QRunnable` on `QThreadPool` that sleeps in 8 chunks of ~125ms (one per page), emitting progress at each step. Total ~1 second of fake "generation time" before the placeholder banks land on disk.
- **"X axis" has no UI control.** The new design shows X with the static descriptor "Scans the wave" (no options). Y and Z get `AxisMorphSelector` widgets; X is a styled label only.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files:**

- `cpp/src/app/settings.h` / `settings.cpp` — `fim::app::Settings` thin `QSettings` wrapper with typed accessors for `output_dir`, `samples_per_frame`, `mode`, `audio_device`, `preview_volume`, `y_morph`, `z_morph`. No business logic.
- `cpp/src/app/services/stub_bank_writer.h` / `stub_bank_writer.cpp` — `fim::app::StubBankWriter::WriteSineBank(const std::filesystem::path& dir)`. Writes `1.wav`..`8.wav`, each containing 64 single-cycle sine waves at increasing frequencies, each cycle 2048 samples, 44.1kHz, 16-bit mono. Pure C++, dr_wav-based, no Qt deps.
- `cpp/src/app/services/single_wav_service.h` / `single_wav_service.cpp` — `fim::app::SingleWavService` `QObject` subclass. Owns the input file path, emits `progressChanged(int percent)`, `generationFinished(bool success)`, `generationFailed(QString error)` signals. `Generate()` queues a `QRunnable` on `QThreadPool::globalInstance()` that calls `StubBankWriter::WriteSineBank` after a 1-second simulated progress.
- `cpp/src/ui/widgets/card_button.h` / `card_button.cpp` — `fim::ui::CardButton` `QFrame` subclass. Mode chooser card with a description label and a "Choose" / "Chosen" / "Coming soon" button. Emits `chosen()` signal. Three states: enabled-default, enabled-chosen, disabled-coming-soon.
- `cpp/src/ui/widgets/file_drop_widget.h` / `file_drop_widget.cpp` — `fim::ui::FileDropWidget` `QFrame` subclass. Dashed-bordered drop area with "Drop or browse for your audio file" text. Accepts drag-drop of `.wav` files, has a fallback "browse" link that opens a `QFileDialog`. Emits `fileDropped(QString path)`.
- `cpp/src/ui/widgets/axis_morph_selector.h` / `axis_morph_selector.cpp` — `fim::ui::AxisMorphSelector` `QFrame` subclass. Title label + N radio-button-style buttons in a `QButtonGroup`. Construct with title and option labels; emits `optionChanged(int index)`.
- `cpp/src/ui/widgets/custom_progress_bar.h` / `custom_progress_bar.cpp` — `fim::ui::CustomProgressBar` `QFrame` subclass. `QLabel` + `QProgressBar` composition with the project's QSS styling. `SetProgress(int percent)` method.
- `cpp/src/ui/widgets/preview_controls_widget.h` / `preview_controls_widget.cpp` — `fim::ui::PreviewControlsWidget` `QFrame` subclass. The slider/button panel from Phase 1's `PreviewWindow`, extracted as an embeddable widget. Owns nothing; takes a `RealtimeAudioEngine*` (non-owning) in its constructor.
- `cpp/src/ui/launcher_screen.h` / `launcher_screen.cpp` — `fim::ui::LauncherScreen` `QWidget` subclass. Header label, subtitle, and three `CardButton`s laid out horizontally. Emits `modeChosen(Mode mode)` where `Mode` is an enum (`kAnyWav`, `kSerum`, `kThreeWavs`).
- `cpp/src/ui/any_wav_screen.h` / `any_wav_screen.cpp` — `fim::ui::AnyWavScreen` `QWidget` subclass. Holds a `QStackedWidget` internally with one page per state (`kEmpty`, `kFileSet`, `kGenerating`, `kDoneMessage`, `kDonePreviewAvailable`). Owns a `SingleWavService`, two `AxisMorphSelector`s, a `FileDropWidget`, a `CustomProgressBar`, a `PreviewControlsWidget`, Generate/Export buttons, a "← Back" button. Emits `backRequested()`.
- `cpp/src/ui/main_window.h` / `main_window.cpp` — `fim::ui::MainWindow` `QMainWindow` subclass. Holds a `QStackedWidget` with `LauncherScreen` and `AnyWavScreen`. Owns the `RealtimeAudioEngine` instance and passes it to `AnyWavScreen` which forwards it to `PreviewControlsWidget`. Wires `LauncherScreen::modeChosen` and `AnyWavScreen::backRequested` to the router. Includes a basic `QMenuBar`.
- `cpp/tests/settings_test.cpp` — round-trip read/write tests for the QSettings wrapper.
- `cpp/tests/stub_bank_writer_test.cpp` — verify all 8 files written, correct sample counts, valid WAV format, distinct frequencies.
- `cpp/tests/single_wav_service_test.cpp` — non-Qt logic only: verify the QRunnable invokes the writer with the right path.

**Modified files:**

- `cpp/CMakeLists.txt` — add new sources to `fim-config-tool`, drop `src/ui/preview_window.cpp`. Add new test sources to test target. Add `src/app/main.cpp` MOC processing of the new widgets (handled automatically by `CMAKE_AUTOMOC ON`).
- `cpp/tests/CMakeLists.txt` — add new test sources and engine .cpp dependencies.
- `cpp/styles/input.scss` — add styles for `.cardButton`, `.fileDropWidget`, `.axisMorphSelector`, `.customProgressBar`, `.previewControlsWidget`, `.launcherScreen`, `.anyWavScreen`, `.mainWindow`.
- `cpp/src/app/main.cpp` — replace `PreviewWindow` construction with `MainWindow` construction.

**Deleted files:**

- `cpp/src/ui/preview_window.h`
- `cpp/src/ui/preview_window.cpp`

---

## Task 1: Settings (QSettings wrapper)

A thin typed-accessor wrapper around `QSettings` so the rest of the code never touches raw key strings. TDD with a temporary settings file fixture.

**Files:**
- Create: `cpp/src/app/settings.h`
- Create: `cpp/src/app/settings.cpp`
- Create: `cpp/tests/settings_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/settings_test.cpp`:

```cpp
#include "app/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include <filesystem>

namespace {

// Forces QSettings to use a temporary INI file rather than the user's real
// settings, so tests don't pollute the developer's machine and run
// deterministically.
class TempSettingsScope {
public:
    TempSettingsScope() {
        path_ = std::filesystem::temp_directory_path() / "fim_settings_test.ini";
        std::filesystem::remove(path_);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QString::fromStdString(path_.parent_path().string()));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName("FIM Test");
        QCoreApplication::setApplicationName("fim_settings_test");
    }
    ~TempSettingsScope() { std::filesystem::remove(path_); }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("Settings round-trips output_dir", "[settings]") {
    TempSettingsScope scope;
    fim::app::Settings settings;
    settings.SetOutputDir("/tmp/fim_output");
    REQUIRE(settings.OutputDir() == "/tmp/fim_output");
}

TEST_CASE("Settings round-trips samples_per_frame", "[settings]") {
    TempSettingsScope scope;
    fim::app::Settings settings;
    settings.SetSamplesPerFrame(2048);
    REQUIRE(settings.SamplesPerFrame() == 2048);
    settings.SetSamplesPerFrame(256);
    REQUIRE(settings.SamplesPerFrame() == 256);
}

TEST_CASE("Settings has sensible defaults", "[settings]") {
    TempSettingsScope scope;
    fim::app::Settings settings;
    REQUIRE(settings.SamplesPerFrame() == 2048);
    REQUIRE(settings.PreviewVolume() == 60);
    REQUIRE_FALSE(settings.OutputDir().empty());  // defaults to "output_waves"
}

TEST_CASE("Settings persists across instances", "[settings]") {
    TempSettingsScope scope;
    {
        fim::app::Settings a;
        a.SetSamplesPerFrame(256);
        a.SetPreviewVolume(75);
    }
    {
        fim::app::Settings b;
        REQUIRE(b.SamplesPerFrame() == 256);
        REQUIRE(b.PreviewVolume() == 75);
    }
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`. Update the `add_executable(fim-tests ...)` block:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
)

target_include_directories(fim-tests PRIVATE
    ../src
)

target_link_libraries(fim-tests PRIVATE
    Catch2::Catch2WithMain
    Qt6::Core
    fourseas_engine
    pffft
    miniaudio_headers
    dr_wav_headers
    SampleRate::samplerate
    spdlog::spdlog
)

# Disable Qt AUTOMOC for the test target — it has no Q_OBJECTs.
set_target_properties(fim-tests PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)

include(Catch)
catch_discover_tests(fim-tests)
```

The new bits: `settings_test.cpp` source, `../src/app/settings.cpp` source, and `Qt6::Core` as a link dependency (so the tests can use `QSettings` and `QCoreApplication`).

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `app/settings.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/app/settings.h`:

```cpp
#pragma once

#include <QSettings>
#include <QString>

#include <string>

namespace fim::app {

// Thin typed-accessor wrapper around QSettings. Hides the raw key strings
// from the rest of the code. Uses the application-scoped QSettings (see
// QCoreApplication::setOrganizationName / setApplicationName).
class Settings {
public:
    Settings() = default;

    // ---- output_dir ----
    std::string OutputDir() const;
    void SetOutputDir(const std::string& path);

    // ---- samples_per_frame ----
    int SamplesPerFrame() const;
    void SetSamplesPerFrame(int samples);

    // ---- mode ----
    // The launcher mode the user last selected. One of "any-wav",
    // "serum-wav", "three-wavs". Defaults to "any-wav".
    std::string Mode() const;
    void SetMode(const std::string& mode);

    // ---- audio_device ----
    // miniaudio device index, or -1 for the system default.
    int AudioDevice() const;
    void SetAudioDevice(int device_index);

    // ---- preview_volume ----
    // 0..100 integer.
    int PreviewVolume() const;
    void SetPreviewVolume(int volume);

    // ---- y_morph / z_morph ----
    // Index into the AxisMorphSelector options for the any-wav mode.
    int YMorph() const;
    void SetYMorph(int index);
    int ZMorph() const;
    void SetZMorph(int index);

private:
    QSettings backing_;
};

}  // namespace fim::app
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/app/settings.cpp`:

```cpp
#include "app/settings.h"

namespace fim::app {

namespace {

constexpr const char* kKeyOutputDir = "output_dir";
constexpr const char* kKeySamplesPerFrame = "samples_per_frame";
constexpr const char* kKeyMode = "mode";
constexpr const char* kKeyAudioDevice = "audio_device";
constexpr const char* kKeyPreviewVolume = "preview_volume";
constexpr const char* kKeyYMorph = "y_morph";
constexpr const char* kKeyZMorph = "z_morph";

}  // namespace

std::string Settings::OutputDir() const {
    return backing_.value(kKeyOutputDir, "output_waves").toString().toStdString();
}

void Settings::SetOutputDir(const std::string& path) {
    backing_.setValue(kKeyOutputDir, QString::fromStdString(path));
}

int Settings::SamplesPerFrame() const {
    return backing_.value(kKeySamplesPerFrame, 2048).toInt();
}

void Settings::SetSamplesPerFrame(int samples) {
    backing_.setValue(kKeySamplesPerFrame, samples);
}

std::string Settings::Mode() const {
    return backing_.value(kKeyMode, "any-wav").toString().toStdString();
}

void Settings::SetMode(const std::string& mode) {
    backing_.setValue(kKeyMode, QString::fromStdString(mode));
}

int Settings::AudioDevice() const {
    return backing_.value(kKeyAudioDevice, -1).toInt();
}

void Settings::SetAudioDevice(int device_index) {
    backing_.setValue(kKeyAudioDevice, device_index);
}

int Settings::PreviewVolume() const {
    return backing_.value(kKeyPreviewVolume, 60).toInt();
}

void Settings::SetPreviewVolume(int volume) {
    backing_.setValue(kKeyPreviewVolume, volume);
}

int Settings::YMorph() const {
    return backing_.value(kKeyYMorph, 0).toInt();
}

void Settings::SetYMorph(int index) {
    backing_.setValue(kKeyYMorph, index);
}

int Settings::ZMorph() const {
    return backing_.value(kKeyZMorph, 0).toInt();
}

void Settings::SetZMorph(int index) {
    backing_.setValue(kKeyZMorph, index);
}

}  // namespace fim::app
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block to include the new source:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/preview_window.cpp
)
```

(Note: `src/ui/preview_window.cpp` stays in this list for now — it gets removed in Task 13 when we wire `main.cpp` to `MainWindow`.)

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: all tests pass (13 from before + 4 new settings tests = 17 total).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/settings.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/settings.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/settings_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/settings.h cpp/src/app/settings.cpp \
        cpp/tests/settings_test.cpp cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add Settings QSettings wrapper"
```

---

## Task 2: StubBankWriter

Writes 8 placeholder wavetable WAV files containing sine waves at 8 distinct frequencies. Pure C++, dr_wav-based, no Qt dependency. The output is consumable by `WavetableBank::Load` so the inline preview can play it.

**Files:**
- Create: `cpp/src/app/services/stub_bank_writer.h`
- Create: `cpp/src/app/services/stub_bank_writer.cpp`
- Create: `cpp/tests/stub_bank_writer_test.cpp`
- Modify: `cpp/CMakeLists.txt`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/stub_bank_writer_test.cpp`:

```cpp
#include "app/services/stub_bank_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "engine/wav_loader.h"

TEST_CASE("StubBankWriter writes 8 numbered WAV files", "[stub_bank_writer]") {
    auto dir = std::filesystem::temp_directory_path() / "fim_stub_bank_test";
    std::filesystem::remove_all(dir);

    REQUIRE(fim::app::StubBankWriter::WriteSineBank(dir));

    for (int i = 1; i <= 8; ++i) {
        const auto path = dir / (std::to_string(i) + ".wav");
        REQUIRE(std::filesystem::exists(path));
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("StubBankWriter pages have the expected sample count",
          "[stub_bank_writer]") {
    auto dir = std::filesystem::temp_directory_path() / "fim_stub_bank_test_count";
    std::filesystem::remove_all(dir);

    REQUIRE(fim::app::StubBankWriter::WriteSineBank(dir));

    // Each page is 64 waves * 2048 samples = 131072 samples.
    auto loaded = fim::engine::LoadWavMono((dir / "1.wav").string());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 64 * 2048);

    std::filesystem::remove_all(dir);
}

TEST_CASE("StubBankWriter pages contain non-zero samples", "[stub_bank_writer]") {
    auto dir = std::filesystem::temp_directory_path() / "fim_stub_bank_test_nonzero";
    std::filesystem::remove_all(dir);

    REQUIRE(fim::app::StubBankWriter::WriteSineBank(dir));

    auto loaded = fim::engine::LoadWavMono((dir / "1.wav").string());
    REQUIRE(loaded.has_value());

    bool any_nonzero = false;
    for (float s : *loaded) {
        if (std::abs(s) > 0.01f) {
            any_nonzero = true;
            break;
        }
    }
    REQUIRE(any_nonzero);

    std::filesystem::remove_all(dir);
}
```

- [ ] **Step 2: Add the test and impl source to the test target**

Modify `cpp/tests/CMakeLists.txt`. Update the `add_executable(fim-tests ...)` block:

```cmake
add_executable(fim-tests
    smoke_test.cpp
    wav_loader_test.cpp
    wavetable_bank_test.cpp
    wavetable_voice_test.cpp
    realtime_audio_engine_test.cpp
    settings_test.cpp
    stub_bank_writer_test.cpp
    ../src/engine/single_header_impls.cpp
    ../src/engine/wav_loader.cpp
    ../src/engine/wavetable_bank.cpp
    ../src/engine/wavetable_voice.cpp
    ../src/engine/realtime_audio_engine.cpp
    ../src/app/settings.cpp
    ../src/app/services/stub_bank_writer.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `app/services/stub_bank_writer.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/app/services/stub_bank_writer.h`:

```cpp
#pragma once

#include <filesystem>

namespace fim::app {

// Phase 2 placeholder bank writer. Writes 8 wavetable WAV files (1.wav..8.wav)
// to `output_directory`. Each file is the standard FourSeas page format:
// 64 waves * 2048 samples per wave = 131072 mono float samples, written as
// 16-bit PCM at 44.1kHz. The samples are sine waves at 8 distinct frequencies
// so the inline preview has something audibly different to play between pages.
//
// Returns false if the directory cannot be created or any file fails to write.
//
// This class exists ONLY for Phase 2 — Phase 3 replaces the call site with
// real DSP. It has no DSP logic of its own beyond generating sine waves.
class StubBankWriter {
public:
    static bool WriteSineBank(const std::filesystem::path& output_directory);
};

}  // namespace fim::app
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/app/services/stub_bank_writer.cpp`:

```cpp
#include "app/services/stub_bank_writer.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "dr_wav.h"

namespace fim::app {

namespace {

constexpr size_t kWavetableSize = 2048;
constexpr size_t kWavesPerPage = 64;
constexpr size_t kPagesPerBank = 8;
constexpr uint32_t kSampleRate = 44100;

// Generate `kWavesPerPage * kWavetableSize` samples of a sine wave at `freq_hz`
// (relative to the wavetable cycle, not real time — the FourSeas oscillator
// scans through the cycle at runtime). Each wavetable cycle is one period of
// the requested frequency.
std::vector<float> SinePage(float freq_multiplier) {
    std::vector<float> samples(kWavesPerPage * kWavetableSize);
    for (size_t wave = 0; wave < kWavesPerPage; ++wave) {
        for (size_t i = 0; i < kWavetableSize; ++i) {
            const float phase = 2.0f * 3.14159265358979f *
                                static_cast<float>(i) / kWavetableSize;
            samples[wave * kWavetableSize + i] = std::sin(phase * freq_multiplier);
        }
    }
    return samples;
}

bool WritePageToWav(const std::filesystem::path& path,
                    const std::vector<float>& samples) {
    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = kSampleRate;
    format.bitsPerSample = 16;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr)) {
        return false;
    }

    // dr_wav writes int16 from float via drwav_write_pcm_frames after a
    // float-to-int conversion. Easier path: convert ourselves.
    std::vector<int16_t> int_samples(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, samples[i]));
        int_samples[i] = static_cast<int16_t>(clamped * 32767.0f);
    }

    const drwav_uint64 frames_written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);

    return frames_written == int_samples.size();
}

}  // namespace

bool StubBankWriter::WriteSineBank(const std::filesystem::path& output_directory) {
    std::error_code ec;
    std::filesystem::create_directories(output_directory, ec);
    if (ec) {
        return false;
    }

    for (size_t page = 0; page < kPagesPerBank; ++page) {
        // Each page gets a different "frequency multiplier" so the pages
        // sound different when scrubbed via the Z slider.
        const float multiplier = 1.0f + static_cast<float>(page);
        const auto samples = SinePage(multiplier);
        const auto path = output_directory / (std::to_string(page + 1) + ".wav");
        if (!WritePageToWav(path, samples)) {
            return false;
        }
    }
    return true;
}

}  // namespace fim::app
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/app/services/stub_bank_writer.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/preview_window.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 20 tests pass (17 from before + 3 new stub_bank_writer tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/stub_bank_writer.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/stub_bank_writer.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/stub_bank_writer_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/stub_bank_writer.h cpp/src/app/services/stub_bank_writer.cpp \
        cpp/tests/stub_bank_writer_test.cpp cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add StubBankWriter for placeholder sine-wave banks"
```

---

## Task 3: SingleWavService

The orchestration `QObject` that owns the input file path, queues the stub generation on `QThreadPool`, and emits Qt signals for progress / done / error. Phase 3 replaces the inner work with real DSP; the API surface stays the same.

**Files:**
- Create: `cpp/src/app/services/single_wav_service.h`
- Create: `cpp/src/app/services/single_wav_service.cpp`
- Modify: `cpp/CMakeLists.txt`

This task does NOT include unit tests because the meaningful behavior — running on the thread pool and emitting Qt signals — is hard to test without a Qt event loop. Manual smoke testing happens in Task 11 when `AnyWavScreen` wires the service into the UI.

- [ ] **Step 1: Write the header**

Create `cpp/src/app/services/single_wav_service.h`:

```cpp
#pragma once

#include <QObject>
#include <QString>

#include <atomic>

namespace fim::app {

// Phase 2 orchestration of single-wav generation. Holds the chosen input
// file, exposes a Generate() slot that queues a QRunnable on the global
// thread pool, and emits Qt signals as the work progresses. The actual
// "DSP" in Phase 2 is StubBankWriter::WriteSineBank — Phase 3 swaps in real
// audio resynthesis without changing this class's API.
class SingleWavService : public QObject {
    Q_OBJECT

public:
    explicit SingleWavService(QObject* parent = nullptr);
    ~SingleWavService() override = default;

    // Sets the input audio file path. No validation here — just stores it.
    void SetInputFile(const QString& path);
    QString InputFile() const;

    // Sets the output directory where 1.wav..8.wav will land.
    void SetOutputDirectory(const QString& path);
    QString OutputDirectory() const;

    // True if Generate() has been called and is still running.
    bool IsGenerating() const;

public slots:
    // Queues the generation work on QThreadPool::globalInstance(). Returns
    // immediately. Progress and completion are reported via signals.
    // Calling Generate() while already generating is a no-op.
    void Generate();

signals:
    // Emitted from the worker thread. Connect with Qt::QueuedConnection (or
    // use auto-connect from a QObject living in the GUI thread, which is the
    // default).
    void progressChanged(int percent);
    void generationFinished();
    void generationFailed(const QString& error);

private:
    QString input_file_;
    QString output_directory_;
    std::atomic<bool> generating_{false};
};

}  // namespace fim::app
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/app/services/single_wav_service.cpp`:

```cpp
#include "app/services/single_wav_service.h"

#include <QRunnable>
#include <QThreadPool>

#include <chrono>
#include <filesystem>
#include <thread>

#include "app/services/stub_bank_writer.h"

namespace fim::app {

SingleWavService::SingleWavService(QObject* parent) : QObject(parent) {}

void SingleWavService::SetInputFile(const QString& path) { input_file_ = path; }
QString SingleWavService::InputFile() const { return input_file_; }

void SingleWavService::SetOutputDirectory(const QString& path) {
    output_directory_ = path;
}
QString SingleWavService::OutputDirectory() const { return output_directory_; }

bool SingleWavService::IsGenerating() const {
    return generating_.load(std::memory_order_relaxed);
}

void SingleWavService::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running
    }

    // Capture by value into the lambda; the QObject's signals are emitted
    // via Qt's queued-connection machinery so it's safe to call them from
    // the worker thread.
    const QString out_dir = output_directory_;

    QThreadPool::globalInstance()->start([this, out_dir]() {
        constexpr int kSteps = 8;
        for (int i = 0; i < kSteps; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(125));
            const int pct = static_cast<int>((i + 1) * 100.0f / kSteps);
            emit progressChanged(pct);
        }

        const std::filesystem::path dir(out_dir.toStdString());
        const bool ok = StubBankWriter::WriteSineBank(dir);

        generating_.store(false, std::memory_order_release);

        if (ok) {
            emit generationFinished();
        } else {
            emit generationFailed(
                QString("Failed to write placeholder bank to %1").arg(out_dir));
        }
    });
}

}  // namespace fim::app
```

- [ ] **Step 3: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/app/services/stub_bank_writer.cpp
    src/app/services/single_wav_service.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/preview_window.cpp
)
```

- [ ] **Step 4: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. AutoMOC processes the `Q_OBJECT` macro in `single_wav_service.h`.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/single_wav_service.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/single_wav_service.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/single_wav_service.h cpp/src/app/services/single_wav_service.cpp \
        cpp/CMakeLists.txt
git commit -m "feat(cpp): add SingleWavService orchestration with QThreadPool"
```

---

## Task 4: CardButton widget + SCSS

The mode chooser card on the launcher: a `QFrame` containing a description label and a "Choose" / "Chosen" / "Coming soon!" button. Three states: enabled-default, enabled-chosen, disabled-coming-soon.

**Files:**
- Create: `cpp/src/ui/widgets/card_button.h`
- Create: `cpp/src/ui/widgets/card_button.cpp`
- Modify: `cpp/styles/input.scss`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/widgets/card_button.h`:

```cpp
#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;

namespace fim::ui {

// One of the three mode chooser cards on the launcher screen. A QFrame with
// a description label and a button. Construct with the description text and
// the initial button state. Emits `chosen()` when the user clicks the button.
class CardButton : public QFrame {
    Q_OBJECT

public:
    enum class State {
        kDefault,     // "Choose" — clickable
        kChosen,      // "Chosen" — clickable, visually selected
        kComingSoon,  // "Coming soon!" — disabled
    };

    CardButton(const QString& description, State initial_state, QWidget* parent = nullptr);

    void SetState(State state);
    State state() const { return state_; }

signals:
    void chosen();

private:
    void UpdateButtonText();

    QLabel* description_label_ = nullptr;
    QPushButton* action_button_ = nullptr;
    State state_;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/widgets/card_button.cpp`:

```cpp
#include "ui/widgets/card_button.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace fim::ui {

CardButton::CardButton(const QString& description, State initial_state, QWidget* parent)
    : QFrame(parent), state_(initial_state) {
    setObjectName("cardButton");
    setFrameShape(QFrame::StyledPanel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    description_label_ = new QLabel(description, this);
    description_label_->setObjectName("cardButtonDescription");
    description_label_->setWordWrap(true);
    description_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(description_label_, /*stretch=*/1);

    auto* button_row = new QHBoxLayout();
    action_button_ = new QPushButton(this);
    action_button_->setObjectName("cardButtonAction");
    button_row->addWidget(action_button_);
    button_row->addStretch();
    layout->addLayout(button_row);

    UpdateButtonText();

    connect(action_button_, &QPushButton::clicked, this, &CardButton::chosen);
}

void CardButton::SetState(State state) {
    state_ = state;
    UpdateButtonText();
}

void CardButton::UpdateButtonText() {
    switch (state_) {
        case State::kDefault:
            action_button_->setText("Choose");
            action_button_->setEnabled(true);
            action_button_->setProperty("variant", "default");
            break;
        case State::kChosen:
            action_button_->setText("Chosen");
            action_button_->setEnabled(true);
            action_button_->setProperty("variant", "chosen");
            break;
        case State::kComingSoon:
            action_button_->setText("Coming soon!");
            action_button_->setEnabled(false);
            action_button_->setProperty("variant", "comingSoon");
            break;
    }
    // Force a re-style after a property change.
    action_button_->style()->unpolish(action_button_);
    action_button_->style()->polish(action_button_);
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add SCSS for the card**

Modify `cpp/styles/input.scss`. Append at the end:

```scss
// ==========================================
// CardButton — launcher mode chooser
// ==========================================

#cardButton {
    background-color: $color-black;
    border-radius: $border-radius-base;
    min-height: 172px;
    max-height: 172px;
}

#cardButtonDescription {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
}

#cardButtonAction {
    background-color: $color-accent;
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
    border: none;
    border-radius: 4px;
    padding: 10px 20px;
    min-width: 78px;
}

#cardButtonAction[variant="comingSoon"] {
    background-color: $color-grey;
}

#cardButtonAction:disabled {
    background-color: $color-grey;
    color: $color-light-gray;
}
```

- [ ] **Step 4: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Update the `qt_add_executable(fim-config-tool ...)` block to include `src/ui/widgets/card_button.cpp`:

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/app/services/stub_bank_writer.cpp
    src/app/services/single_wav_service.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/preview_window.cpp
    src/ui/widgets/card_button.cpp
)
```

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. AutoMOC processes the `Q_OBJECT` macro. The widget is compiled but not yet used by anyone.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/card_button.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/card_button.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/widgets/card_button.h cpp/src/ui/widgets/card_button.cpp \
        cpp/styles/input.scss cpp/CMakeLists.txt
git commit -m "feat(cpp): add CardButton widget with QSS styling"
```

---

## Task 5: FileDropWidget + SCSS

A `QFrame` with a dashed border that accepts drag-and-drop of `.wav` files and has a fallback "browse" link that opens a `QFileDialog`. Emits `fileDropped(path)`.

**Files:**
- Create: `cpp/src/ui/widgets/file_drop_widget.h`
- Create: `cpp/src/ui/widgets/file_drop_widget.cpp`
- Modify: `cpp/styles/input.scss`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/widgets/file_drop_widget.h`:

```cpp
#pragma once

#include <QFrame>
#include <QString>

class QLabel;

namespace fim::ui {

// Drag-and-drop area for accepting a single .wav file. Has a fallback
// "browse" link in its label text that opens a QFileDialog. Emits
// `fileDropped(path)` on either drop or browse pick.
class FileDropWidget : public QFrame {
    Q_OBJECT

public:
    explicit FileDropWidget(QWidget* parent = nullptr);

signals:
    void fileDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void OpenBrowseDialog();

    QLabel* label_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/widgets/file_drop_widget.cpp`:

```cpp
#include "ui/widgets/file_drop_widget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>

namespace fim::ui {

FileDropWidget::FileDropWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("fileDropWidget");
    setAcceptDrops(true);
    setMinimumHeight(88);
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    label_ = new QLabel(
        "Drop or <a href=\"#browse\">browse</a> for your audio file", this);
    label_->setObjectName("fileDropLabel");
    label_->setAlignment(Qt::AlignCenter);
    label_->setTextFormat(Qt::RichText);
    label_->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    layout->addWidget(label_);

    connect(label_, &QLabel::linkActivated, this,
            [this](const QString&) { OpenBrowseDialog(); });
}

void FileDropWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && QFileInfo(urls.first().toLocalFile())
                                   .suffix()
                                   .toLower() == "wav") {
            event->acceptProposedAction();
        }
    }
}

void FileDropWidget::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (QFileInfo(path).suffix().toLower() == "wav") {
        emit fileDropped(path);
    }
}

void FileDropWidget::mousePressEvent(QMouseEvent* event) {
    // Click anywhere on the widget (not just the link) opens the browser.
    if (event->button() == Qt::LeftButton) {
        OpenBrowseDialog();
    }
}

void FileDropWidget::OpenBrowseDialog() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Select a WAV file", QString(), "WAV files (*.wav)");
    if (!path.isEmpty()) {
        emit fileDropped(path);
    }
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add SCSS for the file drop widget**

Modify `cpp/styles/input.scss`. Append at the end:

```scss
// ==========================================
// FileDropWidget — dashed-border drop area
// ==========================================

#fileDropWidget {
    background-color: transparent;
    border: 2px dashed $color-grey;
    border-radius: 8px;
    min-height: 88px;
}

#fileDropLabel {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
    background: transparent;
}
```

(Note: the link inside the label is colored via the rich-text `<a>` tag's default color from Qt, which Qt picks from the application palette. We'll tune it via the QPalette in the screen if needed; for Phase 2 the default is acceptable.)

- [ ] **Step 4: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/widgets/file_drop_widget.cpp` to the `qt_add_executable(fim-config-tool ...)` block.

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/file_drop_widget.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/file_drop_widget.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/widgets/file_drop_widget.h cpp/src/ui/widgets/file_drop_widget.cpp \
        cpp/styles/input.scss cpp/CMakeLists.txt
git commit -m "feat(cpp): add FileDropWidget with drag-and-drop and browse"
```

---

## Task 6: AxisMorphSelector + SCSS

A `QFrame` with a title label and a row of N radio-button-style buttons in a `QButtonGroup`. Construct with title and option labels.

**Files:**
- Create: `cpp/src/ui/widgets/axis_morph_selector.h`
- Create: `cpp/src/ui/widgets/axis_morph_selector.cpp`
- Modify: `cpp/styles/input.scss`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/widgets/axis_morph_selector.h`:

```cpp
#pragma once

#include <QFrame>
#include <QString>
#include <QStringList>

class QButtonGroup;

namespace fim::ui {

// A title label + a row of mutually-exclusive radio-button-style buttons.
// Used by AnyWavScreen for the Y and Z axis selectors.
class AxisMorphSelector : public QFrame {
    Q_OBJECT

public:
    AxisMorphSelector(const QString& title, const QStringList& options,
                      QWidget* parent = nullptr);

    int currentIndex() const;
    void SetCurrentIndex(int index);

signals:
    void currentIndexChanged(int index);

private:
    QButtonGroup* button_group_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/widgets/axis_morph_selector.cpp`:

```cpp
#include "ui/widgets/axis_morph_selector.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace fim::ui {

AxisMorphSelector::AxisMorphSelector(const QString& title, const QStringList& options,
                                     QWidget* parent)
    : QFrame(parent) {
    setObjectName("axisMorphSelector");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* title_label = new QLabel(title, this);
    title_label->setObjectName("axisMorphTitle");
    layout->addWidget(title_label);

    auto* buttons_row = new QHBoxLayout();
    buttons_row->setSpacing(8);
    button_group_ = new QButtonGroup(this);
    button_group_->setExclusive(true);

    for (int i = 0; i < options.size(); ++i) {
        auto* button = new QPushButton(options[i], this);
        button->setObjectName("axisMorphOption");
        button->setCheckable(true);
        if (i == 0) {
            button->setChecked(true);
        }
        button_group_->addButton(button, i);
        buttons_row->addWidget(button);
    }
    buttons_row->addStretch();
    layout->addLayout(buttons_row);

    connect(button_group_, &QButtonGroup::idClicked, this,
            &AxisMorphSelector::currentIndexChanged);
}

int AxisMorphSelector::currentIndex() const { return button_group_->checkedId(); }

void AxisMorphSelector::SetCurrentIndex(int index) {
    auto* button = button_group_->button(index);
    if (button) {
        button->setChecked(true);
    }
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add SCSS for the axis selector**

Modify `cpp/styles/input.scss`. Append at the end:

```scss
// ==========================================
// AxisMorphSelector — Y/Z axis option row
// ==========================================

#axisMorphSelector {
    background-color: transparent;
}

#axisMorphTitle {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
}

#axisMorphOption {
    background-color: $color-grey;
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-normal;
    font-size: $font-size-xs;
    border: none;
    border-radius: 4px;
    padding: 6px 14px;
}

#axisMorphOption:checked {
    background-color: $color-accent;
}
```

- [ ] **Step 4: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/widgets/axis_morph_selector.cpp` to the `qt_add_executable(fim-config-tool ...)` block.

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/axis_morph_selector.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/axis_morph_selector.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/widgets/axis_morph_selector.h cpp/src/ui/widgets/axis_morph_selector.cpp \
        cpp/styles/input.scss cpp/CMakeLists.txt
git commit -m "feat(cpp): add AxisMorphSelector radio button row"
```

---

## Task 7: CustomProgressBar + SCSS

A simple `QFrame` that combines a `QLabel` with a `QProgressBar`. Exists as its own widget so AnyWavScreen can show "Generating wavetable bank..." above the bar.

**Files:**
- Create: `cpp/src/ui/widgets/custom_progress_bar.h`
- Create: `cpp/src/ui/widgets/custom_progress_bar.cpp`
- Modify: `cpp/styles/input.scss`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/widgets/custom_progress_bar.h`:

```cpp
#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QProgressBar;

namespace fim::ui {

// A label-over-progressbar composite. The label text and progress value are
// independently settable. Used by AnyWavScreen during the kGenerating state.
class CustomProgressBar : public QFrame {
    Q_OBJECT

public:
    explicit CustomProgressBar(QWidget* parent = nullptr);

    void SetLabel(const QString& text);
    void SetProgress(int percent);  // 0..100

private:
    QLabel* label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/widgets/custom_progress_bar.cpp`:

```cpp
#include "ui/widgets/custom_progress_bar.h"

#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

namespace fim::ui {

CustomProgressBar::CustomProgressBar(QWidget* parent) : QFrame(parent) {
    setObjectName("customProgressBar");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    label_ = new QLabel("Generating wavetable bank…", this);
    label_->setObjectName("customProgressLabel");
    layout->addWidget(label_);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setObjectName("customProgressBarInner");
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    progress_bar_->setTextVisible(false);
    layout->addWidget(progress_bar_);
}

void CustomProgressBar::SetLabel(const QString& text) { label_->setText(text); }

void CustomProgressBar::SetProgress(int percent) { progress_bar_->setValue(percent); }

}  // namespace fim::ui
```

- [ ] **Step 3: Add SCSS for the progress bar**

Modify `cpp/styles/input.scss`. Append at the end:

```scss
// ==========================================
// CustomProgressBar — generation progress
// ==========================================

#customProgressBar {
    background-color: transparent;
}

#customProgressLabel {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
}

#customProgressBarInner {
    background-color: $color-grey;
    border: none;
    border-radius: 4px;
    height: 8px;
}

#customProgressBarInner::chunk {
    background-color: $color-accent;
    border-radius: 4px;
}
```

- [ ] **Step 4: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/widgets/custom_progress_bar.cpp` to the `qt_add_executable(fim-config-tool ...)` block.

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/custom_progress_bar.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/custom_progress_bar.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/widgets/custom_progress_bar.h cpp/src/ui/widgets/custom_progress_bar.cpp \
        cpp/styles/input.scss cpp/CMakeLists.txt
git commit -m "feat(cpp): add CustomProgressBar with label"
```

---

## Task 8: PreviewControlsWidget (extracted from PreviewWindow)

The slider/button panel from Phase 1's `PreviewWindow`, factored out as an embeddable widget. Takes a non-owning `RealtimeAudioEngine*` in its constructor. The Phase 1 standalone `PreviewWindow` stays in place for now — it's deleted in Task 13.

**Files:**
- Create: `cpp/src/ui/widgets/preview_controls_widget.h`
- Create: `cpp/src/ui/widgets/preview_controls_widget.cpp`
- Modify: `cpp/styles/input.scss`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/widgets/preview_controls_widget.h`:

```cpp
#pragma once

#include <QFrame>

class QLabel;
class QPushButton;
class QSlider;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::ui {

// The preview slider/button panel from Phase 1's PreviewWindow, extracted as
// an embeddable QFrame so AnyWavScreen can show it inline below the
// generation results. Owns no engine — takes a non-owning pointer to a
// RealtimeAudioEngine that lives elsewhere (currently MainWindow).
class PreviewControlsWidget : public QFrame {
    Q_OBJECT

public:
    PreviewControlsWidget(fim::engine::RealtimeAudioEngine* engine,
                          QWidget* parent = nullptr);

private slots:
    void OnPlayStopClicked();
    void OnXChanged(int value);
    void OnYChanged(int value);
    void OnZChanged(int value);
    void OnPitchChanged(int value);
    void OnVolumeChanged(int value);

private:
    fim::engine::RealtimeAudioEngine* engine_;  // non-owning

    QSlider* x_slider_ = nullptr;
    QSlider* y_slider_ = nullptr;
    QSlider* z_slider_ = nullptr;
    QSlider* pitch_slider_ = nullptr;
    QSlider* volume_slider_ = nullptr;
    QPushButton* play_button_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/widgets/preview_controls_widget.cpp`:

```cpp
#include "ui/widgets/preview_controls_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "engine/realtime_audio_engine.h"

namespace fim::ui {

namespace {

constexpr int kPositionSliderMax = 699;
constexpr int kMidiNoteMin = 36;
constexpr int kMidiNoteMax = 96;
constexpr int kMidiNoteDefault = 60;
constexpr int kVolumeMax = 100;
constexpr int kVolumeDefault = 60;

QSlider* MakeHorizontalSlider(int min, int max, int initial) {
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(initial);
    return s;
}

}  // namespace

PreviewControlsWidget::PreviewControlsWidget(fim::engine::RealtimeAudioEngine* engine,
                                             QWidget* parent)
    : QFrame(parent), engine_(engine) {
    setObjectName("previewControlsWidget");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* title = new QLabel("Preview your wavetable bank", this);
    title->setObjectName("previewTitle");
    layout->addWidget(title);

    auto add_slider_row = [&](const QString& label, QSlider*& s, int min, int max,
                              int initial) {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(label, this);
        lbl->setObjectName("previewSliderLabel");
        lbl->setFixedWidth(100);
        row->addWidget(lbl);
        s = MakeHorizontalSlider(min, max, initial);
        row->addWidget(s);
        layout->addLayout(row);
    };

    add_slider_row("X position", x_slider_, 0, kPositionSliderMax, 0);
    add_slider_row("Y position", y_slider_, 0, kPositionSliderMax, 0);
    add_slider_row("Z position", z_slider_, 0, kPositionSliderMax, 0);
    add_slider_row("Pitch", pitch_slider_, kMidiNoteMin, kMidiNoteMax, kMidiNoteDefault);
    add_slider_row("Volume", volume_slider_, 0, kVolumeMax, kVolumeDefault);

    auto* play_row = new QHBoxLayout();
    play_button_ = new QPushButton("Play steady tone", this);
    play_button_->setObjectName("previewPlayButton");
    play_row->addWidget(play_button_);
    play_row->addStretch();
    layout->addLayout(play_row);

    // Wiring
    connect(play_button_, &QPushButton::clicked, this,
            &PreviewControlsWidget::OnPlayStopClicked);
    connect(x_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnXChanged);
    connect(y_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnYChanged);
    connect(z_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnZChanged);
    connect(pitch_slider_, &QSlider::valueChanged, this,
            &PreviewControlsWidget::OnPitchChanged);
    connect(volume_slider_, &QSlider::valueChanged, this,
            &PreviewControlsWidget::OnVolumeChanged);

    // Initialize engine state
    engine_->SetVolume(static_cast<float>(kVolumeDefault) / kVolumeMax);
    engine_->SetMidiNote(kMidiNoteDefault);
}

void PreviewControlsWidget::OnPlayStopClicked() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
        play_button_->setText("Play steady tone");
    } else {
        if (engine_->Start()) {
            play_button_->setText("Stop");
        }
    }
}

void PreviewControlsWidget::OnXChanged(int value) {
    engine_->SetX(static_cast<float>(value) / 100.0f);
}

void PreviewControlsWidget::OnYChanged(int value) {
    engine_->SetY(static_cast<float>(value) / 100.0f);
}

void PreviewControlsWidget::OnZChanged(int value) {
    engine_->SetZ(static_cast<float>(value) / 100.0f);
}

void PreviewControlsWidget::OnPitchChanged(int value) {
    engine_->SetMidiNote(value);
}

void PreviewControlsWidget::OnVolumeChanged(int value) {
    engine_->SetVolume(static_cast<float>(value) / kVolumeMax);
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add SCSS for the preview controls**

Modify `cpp/styles/input.scss`. Append at the end:

```scss
// ==========================================
// PreviewControlsWidget — embedded preview
// ==========================================

#previewControlsWidget {
    background-color: $color-black;
    border-radius: $border-radius-base;
}

#previewTitle {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
}

#previewSliderLabel {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-normal;
    font-size: $font-size-xs;
}

#previewPlayButton {
    background-color: $color-accent;
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
    border: none;
    border-radius: 4px;
    padding: 8px 16px;
}
```

- [ ] **Step 4: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/widgets/preview_controls_widget.cpp` to the `qt_add_executable(fim-config-tool ...)` block.

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/preview_controls_widget.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/widgets/preview_controls_widget.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/widgets/preview_controls_widget.h cpp/src/ui/widgets/preview_controls_widget.cpp \
        cpp/styles/input.scss cpp/CMakeLists.txt
git commit -m "feat(cpp): extract PreviewControlsWidget from PreviewWindow"
```

---

## Task 9: LauncherScreen

The launcher screen with header text and three `CardButton`s arranged horizontally. Emits `modeChosen(Mode)` with one of three values when a card is clicked.

**Files:**
- Create: `cpp/src/ui/launcher_screen.h`
- Create: `cpp/src/ui/launcher_screen.cpp`
- Modify: `cpp/styles/input.scss`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/launcher_screen.h`:

```cpp
#pragma once

#include <QWidget>

namespace fim::ui {

class CardButton;

// The launcher screen — header text + three CardButtons. Emits modeChosen
// when the user picks a mode. The "three wavs" card is permanently in the
// kComingSoon state in Phase 2.
class LauncherScreen : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        kAnyWav,
        kSerum,
        kThreeWavs,
    };

    explicit LauncherScreen(QWidget* parent = nullptr);

signals:
    void modeChosen(Mode mode);

private:
    CardButton* any_wav_card_ = nullptr;
    CardButton* serum_card_ = nullptr;
    CardButton* three_wavs_card_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/launcher_screen.cpp`:

```cpp
#include "ui/launcher_screen.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "ui/widgets/card_button.h"

namespace fim::ui {

LauncherScreen::LauncherScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("launcherScreen");

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(32, 32, 32, 32);
    root_layout->setSpacing(16);

    auto* title = new QLabel("Create your own wavetables for Four Seas", this);
    title->setObjectName("launcherTitle");
    root_layout->addWidget(title);

    auto* subtitle = new QLabel(
        "You can use your own .wav files or Serum 32-bit .wav files. The tool "
        "will take care of the rest.",
        this);
    subtitle->setObjectName("launcherSubtitle");
    subtitle->setWordWrap(true);
    root_layout->addWidget(subtitle);

    auto* cards_row = new QHBoxLayout();
    cards_row->setSpacing(16);

    any_wav_card_ = new CardButton(
        "Use any .wav file to create your wavetable bank",
        CardButton::State::kDefault, this);
    serum_card_ = new CardButton(
        "Use a Serum .wav file to create your wavetable bank",
        CardButton::State::kDefault, this);
    three_wavs_card_ = new CardButton(
        "Use three .wav files to create your wavetable bank",
        CardButton::State::kComingSoon, this);

    cards_row->addWidget(any_wav_card_);
    cards_row->addWidget(serum_card_);
    cards_row->addWidget(three_wavs_card_);
    root_layout->addLayout(cards_row);
    root_layout->addStretch();

    connect(any_wav_card_, &CardButton::chosen, this,
            [this]() { emit modeChosen(Mode::kAnyWav); });
    connect(serum_card_, &CardButton::chosen, this,
            [this]() { emit modeChosen(Mode::kSerum); });
    // three_wavs_card_ stays disabled and emits no signal.
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add SCSS for the launcher**

Modify `cpp/styles/input.scss`. Append at the end:

```scss
// ==========================================
// LauncherScreen
// ==========================================

#launcherScreen {
    background-color: $color-dark-bg;
}

#launcherTitle {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-lg;
}

#launcherSubtitle {
    color: $color-light-gray;
    font-family: $font-family;
    font-weight: $font-weight-normal;
    font-size: $font-size-xs;
}
```

- [ ] **Step 4: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/launcher_screen.cpp` to the `qt_add_executable(fim-config-tool ...)` block.

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/launcher_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/launcher_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/launcher_screen.h cpp/src/ui/launcher_screen.cpp \
        cpp/styles/input.scss cpp/CMakeLists.txt
git commit -m "feat(cpp): add LauncherScreen with three CardButtons"
```

---

## Task 10: AnyWavScreen (state machine)

The most complex screen in Phase 2. A `QStackedWidget` internally swapping between five sub-pages — empty, file-set, generating, done-message, done-with-preview. Owns a `SingleWavService`, the Y/Z axis selectors, the file drop widget, the progress bar, and the embedded preview controls.

**Files:**
- Create: `cpp/src/ui/any_wav_screen.h`
- Create: `cpp/src/ui/any_wav_screen.cpp`
- Modify: `cpp/styles/input.scss`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/any_wav_screen.h`:

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
class SingleWavService;
}

namespace fim::ui {

class AxisMorphSelector;
class CustomProgressBar;
class FileDropWidget;
class PreviewControlsWidget;

// The "any wav" mode screen. Internal state machine implemented as a
// QStackedWidget with one page per state. The "← Back" button at the top is
// always visible and emits backRequested().
//
// State transitions:
//   kEmpty           ──── file dropped ────► kFileSet
//   kFileSet         ──── Generate clicked ─► kGenerating
//   kGenerating      ──── service done ─────► kDoneMessage
//   kDoneMessage     ──── after ~800ms ─────► kDonePreviewAvailable
//   kDoneMessage,
//   kDonePreviewAvailable
//                    ──── Generate clicked ─► kGenerating
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

    explicit AnyWavScreen(fim::engine::RealtimeAudioEngine* engine,
                          QWidget* parent = nullptr);

    // Resets the screen back to the empty state. Called when the user
    // navigates away and returns later.
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

private:
    void SetState(State state);
    QWidget* BuildEmptyPage();
    QWidget* BuildFileSetPage();
    QWidget* BuildGeneratingPage();
    QWidget* BuildDonePage(bool with_preview);

    fim::engine::RealtimeAudioEngine* engine_;
    fim::app::SingleWavService* service_ = nullptr;
    QString current_file_;

    QStackedWidget* stack_ = nullptr;
    int empty_page_index_ = -1;
    int file_set_page_index_ = -1;
    int generating_page_index_ = -1;
    int done_message_page_index_ = -1;
    int done_preview_page_index_ = -1;

    // References into the file_set / done pages so the slots can update
    // them. Owned by their parent QWidgets, not by this class directly.
    QLabel* filename_label_ = nullptr;
    AxisMorphSelector* y_selector_ = nullptr;
    AxisMorphSelector* z_selector_ = nullptr;
    CustomProgressBar* progress_bar_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/any_wav_screen.cpp`:

```cpp
#include "ui/any_wav_screen.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "app/services/single_wav_service.h"
#include "engine/realtime_audio_engine.h"
#include "ui/widgets/axis_morph_selector.h"
#include "ui/widgets/custom_progress_bar.h"
#include "ui/widgets/file_drop_widget.h"
#include "ui/widgets/preview_controls_widget.h"

namespace fim::ui {

namespace {

constexpr const char* kStubOutputDir = "output_waves/audio_resynth";
constexpr int kDoneMessageHoldMs = 800;

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

AnyWavScreen::AnyWavScreen(fim::engine::RealtimeAudioEngine* engine, QWidget* parent)
    : QWidget(parent), engine_(engine) {
    setObjectName("anyWavScreen");
    service_ = new fim::app::SingleWavService(this);
    service_->SetOutputDirectory(kStubOutputDir);

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

    SetState(State::kEmpty);
}

void AnyWavScreen::Reset() {
    current_file_.clear();
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
            // Hold the "Done!" message briefly, then reveal the preview area.
            QTimer::singleShot(kDoneMessageHoldMs, this,
                               [this]() { SetState(State::kDonePreviewAvailable); });
            break;
        case State::kDonePreviewAvailable:
            stack_->setCurrentIndex(done_preview_page_index_);
            // Auto-load the just-generated bank into the engine so the preview
            // widget can play it.
            engine_->LoadBank(kStubOutputDir);
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

    // Filename row
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

    // X axis (fixed descriptor)
    auto* x_label = new QLabel("X axis", page);
    x_label->setObjectName("anyWavAxisLabel");
    layout->addWidget(x_label);
    auto* x_descriptor = new QLabel("Scans the wave", page);
    x_descriptor->setObjectName("anyWavAxisDescriptor");
    layout->addWidget(x_descriptor);

    // Y axis (selector)
    const QStringList placeholder_options{"First option", "Second option", "Third option"};
    y_selector_ = new AxisMorphSelector("Y axis", placeholder_options, page);
    layout->addWidget(y_selector_);

    // Z axis (selector)
    z_selector_ = new AxisMorphSelector("Z axis", placeholder_options, page);
    layout->addWidget(z_selector_);

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

        auto* preview = new PreviewControlsWidget(engine_, page);
        layout->addWidget(preview);
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
    // Copy the 8 files from kStubOutputDir to `dest`.
    for (int i = 1; i <= 8; ++i) {
        const QString src = QString("%1/%2.wav").arg(kStubOutputDir).arg(i);
        const QString dst = QString("%1/%2.wav").arg(dest).arg(i);
        QFile::copy(src, dst);
    }
}

void AnyWavScreen::OnProgressChanged(int percent) {
    if (progress_bar_) {
        progress_bar_->SetProgress(percent);
    }
}

void AnyWavScreen::OnGenerationFinished() { SetState(State::kDoneMessage); }

}  // namespace fim::ui
```

- [ ] **Step 3: Add SCSS for the any-wav screen**

Modify `cpp/styles/input.scss`. Append at the end:

```scss
// ==========================================
// AnyWavScreen
// ==========================================

#anyWavScreen {
    background-color: $color-dark-bg;
}

#backButton {
    color: $color-accent;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
    background: transparent;
    border: none;
    padding: 0;
}

#anyWavTitle {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-lg;
}

#anyWavInnerCard {
    background-color: $color-black;
    border-radius: $border-radius-base;
}

#anyWavFilename {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
}

#clearButton {
    background-color: $color-grey;
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-xs;
    border: none;
    border-radius: 4px;
    padding: 6px 14px;
}

#anyWavAxisLabel {
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
}

#anyWavAxisDescriptor {
    color: $color-light-gray;
    font-family: $font-family;
    font-weight: $font-weight-normal;
    font-size: $font-size-xs;
}

#generateButton, #exportButton {
    background-color: $color-accent;
    color: $color-white;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-base;
    border: none;
    border-radius: 4px;
    padding: 10px 20px;
}

#anyWavDoneMessage {
    color: $color-accent;
    font-family: $font-family;
    font-weight: $font-weight-bold;
    font-size: $font-size-lg;
}
```

- [ ] **Step 4: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/any_wav_screen.cpp` to the `qt_add_executable(fim-config-tool ...)` block.

- [ ] **Step 5: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/any_wav_screen.h cpp/src/ui/any_wav_screen.cpp \
        cpp/styles/input.scss cpp/CMakeLists.txt
git commit -m "feat(cpp): add AnyWavScreen state machine with stubbed generation"
```

---

## Task 11: MainWindow (QStackedWidget router)

The top-level `QMainWindow` that holds a `QStackedWidget` with `LauncherScreen` and `AnyWavScreen`. Owns the singleton `RealtimeAudioEngine`. A basic menu bar with placeholder File / Audio / Settings menus.

**Files:**
- Create: `cpp/src/ui/main_window.h`
- Create: `cpp/src/ui/main_window.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/main_window.h`:

```cpp
#pragma once

#include <memory>

#include <QMainWindow>

class QStackedWidget;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::ui {

class AnyWavScreen;
class LauncherScreen;

// Top-level QMainWindow. Owns the RealtimeAudioEngine and the QStackedWidget
// router that swaps between LauncherScreen and AnyWavScreen.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnModeChosen(int mode);  // takes int because of LauncherScreen::Mode enum
    void OnBackToLauncher();

private:
    void BuildMenuBar();

    std::unique_ptr<fim::engine::RealtimeAudioEngine> engine_;
    QStackedWidget* stack_ = nullptr;
    LauncherScreen* launcher_screen_ = nullptr;
    AnyWavScreen* any_wav_screen_ = nullptr;
    int launcher_index_ = -1;
    int any_wav_index_ = -1;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/main_window.cpp`:

```cpp
#include "ui/main_window.h"

#include <QMenuBar>
#include <QMessageBox>
#include <QStackedWidget>

#include "engine/realtime_audio_engine.h"
#include "ui/any_wav_screen.h"
#include "ui/launcher_screen.h"

namespace fim::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      engine_(std::make_unique<fim::engine::RealtimeAudioEngine>(48000.0f, 512)) {
    setWindowTitle("FIM Config Tool");
    resize(1000, 480);

    stack_ = new QStackedWidget(this);

    launcher_screen_ = new LauncherScreen(this);
    any_wav_screen_ = new AnyWavScreen(engine_.get(), this);

    launcher_index_ = stack_->addWidget(launcher_screen_);
    any_wav_index_ = stack_->addWidget(any_wav_screen_);

    setCentralWidget(stack_);

    connect(launcher_screen_, &LauncherScreen::modeChosen, this,
            [this](LauncherScreen::Mode mode) {
                OnModeChosen(static_cast<int>(mode));
            });
    connect(any_wav_screen_, &AnyWavScreen::backRequested, this,
            &MainWindow::OnBackToLauncher);

    BuildMenuBar();

    stack_->setCurrentIndex(launcher_index_);
}

MainWindow::~MainWindow() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
    }
}

void MainWindow::OnModeChosen(int mode) {
    const auto m = static_cast<LauncherScreen::Mode>(mode);
    if (m == LauncherScreen::Mode::kAnyWav) {
        any_wav_screen_->Reset();
        stack_->setCurrentIndex(any_wav_index_);
    } else if (m == LauncherScreen::Mode::kSerum) {
        QMessageBox::information(this, "Serum mode",
                                  "Serum mode UI is not yet implemented in Phase 2.");
    }
    // kThreeWavs is permanently disabled in the launcher.
}

void MainWindow::OnBackToLauncher() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
    }
    stack_->setCurrentIndex(launcher_index_);
}

void MainWindow::BuildMenuBar() {
    auto* file_menu = menuBar()->addMenu("&File");
    file_menu->addAction("Quit", QKeySequence::Quit, this, &QWidget::close);

    menuBar()->addMenu("&Audio");
    menuBar()->addMenu("&Settings");
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/main_window.cpp` to the `qt_add_executable(fim-config-tool ...)` block.

- [ ] **Step 4: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/main_window.h cpp/src/ui/main_window.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): add MainWindow router with QStackedWidget"
```

---

## Task 12: Wire main.cpp + delete PreviewWindow

Replace `main.cpp`'s use of `PreviewWindow` with `MainWindow`. Delete the now-unused `cpp/src/ui/preview_window.{h,cpp}` files.

**Files:**
- Modify: `cpp/src/app/main.cpp`
- Delete: `cpp/src/ui/preview_window.h`
- Delete: `cpp/src/ui/preview_window.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Replace `main.cpp`**

Overwrite `cpp/src/app/main.cpp` with:

```cpp
#include <QApplication>
#include <QFile>
#include <QTextStream>

#include "ui/main_window.h"

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

    fim::ui::MainWindow window;
    window.show();

    return app.exec();
}
```

- [ ] **Step 2: Delete the obsolete PreviewWindow files**

```bash
rm /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/preview_window.h
rm /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/preview_window.cpp
```

- [ ] **Step 3: Drop PreviewWindow from CMakeLists.txt**

Modify `cpp/CMakeLists.txt`. Remove the `src/ui/preview_window.cpp` line from the `qt_add_executable(fim-config-tool ...)` block. The final block should look like (note the removed line):

```cmake
qt_add_executable(fim-config-tool
    src/app/main.cpp
    src/app/settings.cpp
    src/app/services/stub_bank_writer.cpp
    src/app/services/single_wav_service.cpp
    src/engine/engine_smoke.cpp
    src/engine/single_header_impls.cpp
    src/engine/wav_loader.cpp
    src/engine/wavetable_bank.cpp
    src/engine/wavetable_voice.cpp
    src/engine/realtime_audio_engine.cpp
    src/ui/main_window.cpp
    src/ui/launcher_screen.cpp
    src/ui/any_wav_screen.cpp
    src/ui/widgets/card_button.cpp
    src/ui/widgets/file_drop_widget.cpp
    src/ui/widgets/axis_morph_selector.cpp
    src/ui/widgets/custom_progress_bar.cpp
    src/ui/widgets/preview_controls_widget.cpp
)
```

- [ ] **Step 4: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. The new MainWindow is now the entry point.

- [ ] **Step 5: Run the app and visually verify**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Expected: the launcher screen appears with the three mode cards. The "Coming soon!" card is greyed out. The Serum and Any-wav cards are clickable. (Don't test the full flow yet — that's Task 13.)

- [ ] **Step 6: Format and commit**

```bash
clang-format -i /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/main.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/main.cpp cpp/src/ui/preview_window.h cpp/src/ui/preview_window.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): wire MainWindow as entry point and delete PreviewWindow"
```

(`git add` of the deleted files records their removal — the `rm` from Step 2 already deleted them from the working tree.)

---

## Task 13: End-to-end manual verification + Phase 2 complete

Click through the full flow, verify it matches the designs, write `phase-2-complete.md`, push, verify CI green.

**Files:**
- Create: `docs/phase-2-complete.md`
- Modify: `docs/followups.md`

- [ ] **Step 1: Run the app**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

- [ ] **Step 2: Walk through the launcher**

Verify:
- Three cards visible side by side
- Title "Create your own wavetables for Four Seas"
- Subtitle "You can use your own .wav files or Serum 32-bit .wav files. The tool will take care of the rest."
- "Three .wav files" card is greyed out and shows "Coming soon!"
- Clicking "Serum" card opens a `QMessageBox` saying not yet implemented

- [ ] **Step 3: Walk through the any-wav flow**

1. Click "Choose" on the "Use any .wav file" card. App switches to AnyWavScreen kEmpty state.
2. Verify "← Back" button at top, title, and dashed-bordered drop area.
3. Click the drop area. A file dialog opens. Pick any `.wav` file from your filesystem.
4. App switches to kFileSet state. Filename shown next to a "Clear" button. X axis label "X axis" with "Scans the wave" descriptor. Y and Z axis selectors with three radio buttons each. "Generate wavetable bank" button at bottom.
5. Click "Generate wavetable bank". App switches to kGenerating state showing "Generating wavetable bank…" and a progress bar that fills over ~1 second.
6. App switches to kDoneMessage state showing "Done!" briefly, then auto-transitions to kDonePreviewAvailable.
7. kDonePreviewAvailable shows "Generate wavetable bank" + "Export wavetable bank" buttons and the embedded `PreviewControlsWidget` with X/Y/Z/Pitch/Volume sliders and a "Play steady tone" button.
8. Click "Play steady tone". The just-generated placeholder bank starts playing. **You should hear sine waves.** Drag the Z slider — the pitch should change as you move between pages.
9. Click "Stop" (the play button label).
10. Click "Export wavetable bank". A directory picker opens. Pick any directory. Verify 8 wav files appear in it.
11. Click "← Back". App returns to launcher.

If any step fails, debug from there. The build is green if you got to step 1, so failures are runtime / wiring issues.

- [ ] **Step 4: Verify CI is still green**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
gh run watch
```

Expected: macOS, Ubuntu, Windows, and clang-format jobs all green. The C++ tests for `Settings` and `StubBankWriter` run; UI code is compiled but not exercised.

- [ ] **Step 5: Update `docs/followups.md`**

The Pangram font issue needs to be the **first and loudest** item in the followups doc — it's a real distribution blocker. Open `docs/followups.md` and add a new top-level section ABOVE the existing "## Audio engine" section:

```markdown
## ⚠️ KNOWN ISSUES — must address before any release

### Pangram font is NOT bundled with the app

**Severity:** Distribution blocker.

`cpp/styles/input.scss` declares `font-family: 'Pangram'` for every text element in the UI, but **no Pangram font file ships with the app**. Qt resolves the family name from the OS font cache at runtime, which means:

- On the developer's Mac (where Pangram is installed system-wide), the app looks correct.
- On every other machine — CI runners, end-user downloads, fresh installs — Qt silently falls back to the system default sans, and the app does NOT match the designs in `designs/`.

This is a real bug that affects every release. It must be resolved before tagging a public version.

**Possible fixes (decision for the project owner + designer):**

1. **License + bundle Pangram.** The font is commercial (Pangram Pangram Foundry). Buy a license that allows redistribution as part of an application, then drop the `.woff2` files into `cpp/resources/fonts/` and load them via `QFontDatabase::addApplicationFont` from a Qt resource. Cost: licensing fee. Most accurate to the design.
2. **Substitute a permissively-licensed lookalike sans.** Candidates worth considering: Inter (SIL OFL, very common), Public Sans (SIL OFL, US Web Design System), TASA Orbiter Display (similar geometric flavor), or Geist Sans (MIT, Vercel's lookalike of Inter). Pick one, update `cpp/styles/input.scss` to reference the new family name, drop the font file into `cpp/resources/fonts/`, register via `QFontDatabase::addApplicationFont`. Cost: free, requires designer signoff that the substitute is visually acceptable.
3. **Accept system fallback as shipped behavior.** The app looks slightly different on every OS but always renders. Cost: zero, but the design becomes "approximate" and users on Linux/Windows get a different visual than the mockups show.

The Python tool currently has this exact same bug — it also doesn't bundle Pangram and silently uses the system fallback on machines without it. So this isn't a regression introduced by the rewrite; it's a pre-existing issue we now have to acknowledge and fix.

## Phase 2

- **Serum mode is fully deferred.** The launcher's Serum card opens a `QMessageBox` saying "not yet implemented." Designs are pending; revisit when they land.
- **Three-wavs mode is permanently `kComingSoon`.** Will be revisited only if there's user demand.
- **Y/Z axis option labels are placeholders** ("First option / Second option / Third option"). Real labels and behaviors land in Phase 3 with the DSP port.
- **`SingleWavService` does not actually validate the input file.** It accepts any path and feeds it to `StubBankWriter`, which ignores the input entirely (just writes sine waves). Phase 3 wires real DSP and adds input validation.
- **`SingleWavService::Generate()` is not cancellable.** The spec mentioned a "cancellable `QRunnable`" but Phase 2's stub generation only takes ~1 second of simulated work, so cancellation has no practical value. Real cancellation matters in Phase 3 when DSP generation takes 30+ seconds. Add a `Cancel()` slot, an atomic `cancel_requested_` flag checked between progress steps, and a UI button to trigger it. Until then, Phase 2 generates uninterruptibly.
- **`Settings` is wired but not used by the screens yet.** The MainWindow / AnyWavScreen don't read or write any settings. Phase 2's persistence story is incomplete; revisit during Phase 3 when there are real values worth persisting (e.g., last-used output directory, last-used Y/Z morph indices).
```

- [ ] **Step 6: Write `docs/phase-2-complete.md`**

Create `docs/phase-2-complete.md`:

```markdown
# Phase 2 Complete

As of this commit, Phase 2 of the C++/Qt rewrite — the main UI without DSP — is complete. The throwaway Phase 1 `PreviewWindow` has been deleted; the app now boots into a launcher screen and routes through a state-machine UI for the "any wav" mode, including a stubbed Generate flow that produces playable placeholder banks.

## What got built

- **`fim::app::Settings`** — `QSettings` wrapper with typed getters/setters for `output_dir`, `samples_per_frame`, `mode`, `audio_device`, `preview_volume`, `y_morph`, `z_morph`. Sensible defaults. 4 Catch2 tests covering round-trip and persistence.
- **`fim::app::StubBankWriter`** — writes 8 placeholder wavetable WAV files containing sine waves at 8 distinct frequencies. Each file is the standard FourSeas page format (64 waves × 2048 samples). Pure `dr_wav`-based, no Qt deps. 3 Catch2 tests covering file creation, sample counts, and non-zero output.
- **`fim::app::SingleWavService`** — `QObject` orchestration class. Owns the input file path, queues a `QRunnable` on `QThreadPool::globalInstance()` that emits progress over ~1 second of simulated work, then calls `StubBankWriter::WriteSineBank`. Phase 3 swaps in real DSP without changing this API.
- **5 reusable Qt widgets** in `cpp/src/ui/widgets/`:
  - `CardButton` — mode chooser card (3 instances on launcher), three states: default/chosen/coming-soon
  - `FileDropWidget` — dashed-border drop area with `QFileDialog` fallback
  - `AxisMorphSelector` — title + radio button row, used for Y and Z axes
  - `CustomProgressBar` — `QLabel` + `QProgressBar` composite
  - `PreviewControlsWidget` — extracted from Phase 1's `PreviewWindow`, embeddable into any screen
- **`fim::ui::LauncherScreen`** — header + three `CardButton`s. Emits `modeChosen(Mode)`.
- **`fim::ui::AnyWavScreen`** — internal `QStackedWidget` with five sub-pages (`kEmpty`, `kFileSet`, `kGenerating`, `kDoneMessage`, `kDonePreviewAvailable`). Owns a `SingleWavService`, the axis selectors, the progress bar, and the embedded preview controls.
- **`fim::ui::MainWindow`** — top-level `QMainWindow` with a `QStackedWidget` router. Holds the singleton `RealtimeAudioEngine`. Basic menu bar with placeholder File / Audio / Settings menus.
- **SCSS additions** to `cpp/styles/input.scss` for every new widget and screen, transpiled at build time and embedded via `qt_add_resources`.
- **Phase 1's `PreviewWindow` deleted.**

## Catch2 test count

| Phase | Tests |
|---|---|
| Phase 0 (smoke) | 2 |
| Phase 1 (engine layer) | 11 |
| Phase 2 (Settings + StubBankWriter) | 7 |
| **Total** | **20** |

UI code is compiled but not unit-tested — Qt widgets are verified manually.

## Manual verification on macOS

- ✅ App boots into the launcher screen
- ✅ Three cards visible, "Three .wav files" disabled with "Coming soon!"
- ✅ Click "Choose" on "Any wav" card → drills into `AnyWavScreen` empty state
- ✅ "← Back" returns to launcher
- ✅ Click drop area → file picker opens → pick a .wav → state advances to `kFileSet`
- ✅ `kFileSet` shows filename, Clear button, X/Y/Z axis controls, Generate button
- ✅ Click Generate → progress bar fills over ~1 second
- ✅ Auto-transitions through `kDoneMessage` → `kDonePreviewAvailable`
- ✅ Click Play in the embedded preview controls → audible sine wave from the placeholder bank
- ✅ Drag Z slider → pitch changes between pages (different sine frequencies per page)
- ✅ Click Export → directory picker → 8 files copied
- ✅ "Serum" card click → `QMessageBox` saying not implemented (deferred)

## Cross-platform CI

| Job | Status |
|---|---|
| `clang-format check` | ✅ |
| `Build - macos-latest` | ✅ |
| `Build - ubuntu-latest` | ✅ |
| `Build - windows-latest` | ✅ |

## ⚠️ Known issue — Pangram font is NOT bundled

**This is a distribution blocker.** The SCSS references `font-family: 'Pangram'` but no font file ships with the app. The developer's Mac has Pangram installed system-wide, so the app renders correctly there. **On every other machine, Qt silently falls back to the system default sans.** The Python tool has the exact same pre-existing issue. Must be resolved before any release tag — three options documented in `docs/followups.md` (license + bundle, substitute lookalike, accept system fallback). Decision pending designer input.

## Other spec deviations

- **`AxisMorphSelector` implemented in Phase 2** even though the spec assigned it to Serum-only — the new "any wav" designs require Y/Z selectors there too.
- **Serum mode fully deferred** until designs are ready.
- **`Settings` is implemented but not yet used** by the screens. Will be wired in Phase 3.

## Next phase

Phase 3 — **single-wav DSP port**. Replace `StubBankWriter` with a real audio resynthesis port from `lib/audio_resynthesis.py`. Set up the Python oracle test harness. Wire the Y/Z axis option labels to actual DSP behaviors. The `SingleWavService` API stays the same; only its internal worker changes.
```

- [ ] **Step 7: Commit and push the completion docs**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add docs/phase-2-complete.md docs/followups.md
git commit -m "docs: mark Phase 2 complete and add Phase 2 followups"
git push
```

---

## Phase 2 done when:

1. ✅ App launches into the launcher screen with three mode cards
2. ✅ "Choose" on the any-wav card routes to the AnyWavScreen via `QStackedWidget`
3. ✅ File drop / browse advances to `kFileSet`
4. ✅ Generate button runs the stubbed `SingleWavService` with visible progress
5. ✅ Auto-transition through `kDoneMessage` to `kDonePreviewAvailable`
6. ✅ Embedded `PreviewControlsWidget` plays the placeholder banks
7. ✅ Export button copies the 8 files to a chosen directory
8. ✅ "← Back" returns to the launcher
9. ✅ All 20 Catch2 tests pass locally
10. ✅ CI green on macOS, Ubuntu, Windows, and clang-format
11. ✅ `docs/phase-2-complete.md` and `docs/followups.md` committed
