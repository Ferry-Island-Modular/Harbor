# Phase 5 Implementation Plan — Feature parity, beta polish, Python sunset

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring Harbor to feature parity with the Python tool, polish the rough edges that block beta distribution, ship a tester-ready `.dmg`, then delete the Python project and merge `cpp-qt-rewrite` to `master`.

**Architecture:** The biggest structural change is splitting the **preview cache** (always 2048-sample, hidden, fixed location) from the **user export directory** (configurable, may be 2048 or 256 samples depending on the selected hardware target). This is what unlocks both the output-directory picker AND the Waveedit-target feature without touching DSP cores or breaking the preview engine. Generation always produces a 2048 bank into the cache; a new shared **export writer** then writes a second copy to the user's chosen export dir, optionally downsampling 2048→256 via libsamplerate. All three services (`SingleWavService`, `SerumWavService`, `ThreeWavService`) call into the same export writer so the dual-write logic lives in one place.

Other changes are smaller: a real menu bar with File / Export / Help submenus, an About dialog, an HTML-rendered Help dialog, equal-width launcher cards, a sane minimum window size, a `Read Me First.rtf` shipped in the `.dmg`, and finally the deletion of the Python source tree + a top-level README rewrite for the C++ project.

**Tech Stack:** Existing C++20 / Qt 6.8 stack; libsamplerate (already vendored, used here for the 2048→256 decimation step); QTextBrowser for HTML help; macOS `.app` packaging via the existing `cpp/scripts/package-macos.sh`.

**Spec deviations from Python reference:**

- **Preview always works in Waveedit mode.** Python's tool generated whatever sample size you asked for and that's what got previewed (Python-side it didn't really matter — preview was via `fourseas-preview` which is still 2048-locked, so Python had the same problem and ignored it). Harbor sidesteps the issue by always producing a 2048-sample preview-cache copy regardless of export target, so the user can audition any bank.
- **Output directory is per-product, not per-mode.** Python's tool wrote everything to one `output_waves/` dir. Harbor's user-facing output dir is one global setting; the on-disk filenames don't collide because the three modes already write to subdirectories internally.
- **Help dialog ships with the binary, not a separate HTML file.** Python's Help opened a system browser to a local HTML file that was bundled with the PyInstaller spec. Harbor embeds the help HTML as a Qt resource and renders it in a `QTextBrowser` inside a modal dialog — no external browser, no installation footprint.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files:**

- `cpp/src/app/services/export_writer.h` / `export_writer.cpp` — `fim::app::WriteBankToExportDir(const QString& src_cache_dir, const QString& dest_dir, int target_samples_per_cycle)`. Reads the 8 `*.wav` files from the preview cache, optionally decimates 2048→target via libsamplerate, writes them to the user's destination. Shared by all three services.
- `cpp/resources/help/help.html` — embedded help content. Rendered by `HelpDialog` via `QTextBrowser`. Plain HTML4-ish (Qt's rich text engine isn't a real browser).
- `cpp/src/ui/dialogs/help_dialog.h` / `help_dialog.cpp` — `fim::ui::HelpDialog`. Modal `QDialog` containing a `QTextBrowser` that loads the embedded help resource. Closeable with Esc / OK button.
- `cpp/src/ui/dialogs/about_dialog.h` / `about_dialog.cpp` — `fim::ui::AboutDialog`. Modal `QDialog` showing app name, version, license, attribution to FourSeas / DaisySP / PFFFT / libsamplerate / etc. (Could be a single `QMessageBox::about()` call, but a real dialog gives us room for the icon and a clickable link to the project URL.)
- `cpp/resources/dist/Read Me First.rtf` — RTF shipped in the `.dmg` alongside the `.app`. Gives testers the install steps + xattr workaround + a sentence on what Harbor is.

**Modified files:**

- `cpp/src/app/settings.h` / `settings.cpp` — wire up `OutputDir()` (already declared, currently returns `"output_waves"` as a placeholder default; needs a sensible default like `~/Documents/Harbor` and to actually be read by the services). Wire up `SamplesPerFrame()` similarly (currently defaulted to 2048 but unread).
- `cpp/src/app/services/generate_service_base.h` / `generate_service_base.cpp` — accept a *preview cache directory* parameter alongside the user output directory. The service writes the cache always; the export writer step writes to the user dir. Pass `samples_per_frame` through to `DoGenerate` so subclasses know what to ask the export writer for.
- `cpp/src/app/services/single_wav_service.cpp` — call the new export writer at the end of `DoGenerate`.
- `cpp/src/app/services/serum_wav_service.cpp` — same.
- `cpp/src/app/services/three_wav_service.cpp` — same.
- `cpp/src/ui/any_wav_screen.cpp` / `serum_wav_screen.cpp` / `three_wav_screen.cpp` — switch from hardcoded `SingleWavOutputDir()` (etc.) to reading from `Settings::OutputDir()`. Change `OutputDirForPreview()` to point at the per-mode preview cache dir, NOT at the user export dir. Subscribe to settings changes so the user can change the output dir mid-session and the next generation respects it.
- `cpp/src/ui/main_window.h` / `main_window.cpp` — flesh out the menu bar:
  - **File** → "Choose output directory…" (with current path shown), "Quit"
  - **Export** → "Target hardware ▶ Four Seas (2048 samples) / Waveedit (256 samples)" (radio group, persisted to `Settings::SamplesPerFrame`)
  - **Help** → "Harbor Help…", "About Harbor…"
- `cpp/src/ui/launcher_screen.cpp` — equal-width cards: `cards_row->addWidget(card, /*stretch=*/1)` for all three so each gets ⅓ of the row regardless of label length.
- `cpp/src/ui/main_window.cpp` — set a sensible `setMinimumSize()` so the launcher descriptors don't clip. Need to determine the "natural" minimum from the longest descriptor — probably ~960×540.
- `cpp/CMakeLists.txt` — add the new sources, add a Qt resource block for the help HTML.
- `cpp/scripts/package-macos.sh` — copy `Read Me First.rtf` into the staging dir before `hdiutil create`, so it ends up in the mounted `.dmg` next to the `.app`.
- `docs/followups.md` — remove resolved items: `SingleWavService does not validate the input file` (deferred again, still NOT done in this phase but flagging), the output-directory followup (resolved), the styling followup (resolved minimally for beta).

**Deleted files (Phase 5 cleanup task):**

Everything Python-side at the repo root + `src/`:

- `src/` (entire Python source tree)
- `pyproject.toml`
- `uv.lock`
- `.python-version`
- `__init__.py` (root-level Python marker)
- `BUILD.md`
- `build.sh`
- `create_dmg.sh`
- `FIM Config Tool.spec` (PyInstaller spec)
- `TODO.md` (Python-era TODOs, all stale)
- Top-level `README.md` (will be replaced with a C++-focused one)
- Top-level `CLAUDE.md` (will be replaced — currently describes the Python project)

**Kept:** `cpp/`, `docs/`, `designs/`, `LICENSE`, `.gitignore`, `.github/` (CI), `.gitmodules`.

---

## Task 1: Equal-width launcher cards + minimum window size

The simplest visible polish item. Two trivial layout fixes that fix the user's biggest visual complaint about the home screen.

**Files:**
- Modify: `cpp/src/ui/launcher_screen.cpp`
- Modify: `cpp/src/ui/main_window.cpp`

- [ ] **Step 1: Equal-width cards**

In `cpp/src/ui/launcher_screen.cpp`, change the three `addWidget` calls to specify a stretch factor of 1:

```cpp
    cards_row->addWidget(any_wav_card_, /*stretch=*/1);
    cards_row->addWidget(serum_card_, /*stretch=*/1);
    cards_row->addWidget(three_wavs_card_, /*stretch=*/1);
```

This forces `QHBoxLayout` to give each card an equal share of the row width regardless of internal label length.

- [ ] **Step 2: Minimum window size**

In `cpp/src/ui/main_window.cpp`, replace the existing `resize(1000, 480);` line with:

```cpp
    resize(1100, 560);
    setMinimumSize(960, 520);
```

The minimum was picked so that the launcher's three card descriptors (the longest is ~50 chars: "Use three .wav files to create your wavetable bank") render on at most two lines with a comfortable padding.

- [ ] **Step 3: Build, launch, verify**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/Harbor.app
```

Manual checks:
- Three launcher cards are visibly the same width.
- Try to drag the window narrower than 960 — it should refuse.
- At minimum width, all three card descriptors are still readable (no clipping, no text overflow).

- [ ] **Step 4: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/launcher_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/launcher_screen.cpp cpp/src/ui/main_window.cpp
git commit -m "fix(cpp): equal-width launcher cards and sane minimum window size"
```

---

## Task 2: Preview cache vs user output directory split

The architectural cornerstone of Phase 5. Introduce two distinct directories per mode: a **preview cache** (always written, always 2048-sample, hidden under `AppLocalDataLocation`) and a **user export directory** (read from `Settings::OutputDir()`, may be anywhere the user picks). Wire generation to write the preview cache; the user export step is added in Task 3.

This task is purely infrastructural — when it lands, everything still functions as before because the user export dir is initially set equal to the preview cache for the migration. Task 3 introduces the actual split behavior.

**Files:**
- Modify: `cpp/src/app/services/generate_service_base.h`
- Modify: `cpp/src/app/services/generate_service_base.cpp`
- Modify: `cpp/src/ui/any_wav_screen.cpp`
- Modify: `cpp/src/ui/serum_wav_screen.cpp`
- Modify: `cpp/src/ui/three_wav_screen.cpp`

- [ ] **Step 1: Add a preview cache directory accessor to GenerateServiceBase**

In `cpp/src/app/services/generate_service_base.h`, add a new field and getters/setters parallel to the existing output directory:

```cpp
public:
    // The preview cache is where the always-2048-sample bank is written.
    // The audio engine loads from here. This is distinct from the user-
    // facing export directory (SetOutputDirectory) which may live anywhere
    // and may receive a downsampled copy of the bank.
    void SetPreviewCacheDirectory(const QString& path);
    QString PreviewCacheDirectory() const;
```

And in the private section:

```cpp
private:
    QString preview_cache_directory_;
```

- [ ] **Step 2: Implement the accessors**

In `cpp/src/app/services/generate_service_base.cpp`:

```cpp
void GenerateServiceBase::SetPreviewCacheDirectory(const QString& path) {
    preview_cache_directory_ = path;
}

QString GenerateServiceBase::PreviewCacheDirectory() const {
    return preview_cache_directory_;
}
```

- [ ] **Step 3: Route Generate() through the cache, not the user dir**

Still in `generate_service_base.cpp`, change `Generate()` to pass the preview cache directory to `DoGenerate` (because that's where the DSP cores write their output). The user output directory becomes the *destination for the export writer step* in Task 3 — for now just snapshot it but don't use it inside `DoGenerate`.

```cpp
void GenerateServiceBase::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    const QString in_file = input_file_;
    const QString cache_dir = preview_cache_directory_;
    // user_export_dir is captured for Task 3 — currently unused inside the
    // worker.
    const QString user_export_dir = output_directory_;

    QThreadPool::globalInstance()->start([this, in_file, cache_dir, user_export_dir]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path cache_path(cache_dir.toStdString());

        const bool ok = DoGenerate(input_path, cache_path, [this](int percent) {
            emit progressChanged(percent);
        });

        // (Task 3 will add: if ok, also call the export writer to copy the
        // cache to user_export_dir, optionally downsampled.)
        (void)user_export_dir;

        generating_.store(false, std::memory_order_release);

        if (ok) {
            emit generationFinished();
        } else {
            emit generationFailed(
                QString("Failed to generate wavetable bank from %1").arg(in_file));
        }
    });
}
```

- [ ] **Step 4: Update each mode screen to set both directories**

In `cpp/src/ui/any_wav_screen.cpp`, find the constructor and add a call to set the preview cache dir alongside the existing output dir call. Keep the current behavior for now by setting the user output dir equal to the cache dir — Task 3 / 4 will introduce the real user-output dir from Settings.

Find this block:

```cpp
    service_->SetOutputDirectory(SingleWavOutputDir());
```

And replace with:

```cpp
    service_->SetPreviewCacheDirectory(SingleWavOutputDir());
    service_->SetOutputDirectory(SingleWavOutputDir());  // temporary —
                                                        // Task 4 sources
                                                        // this from
                                                        // Settings::OutputDir()
```

Apply the equivalent change in `cpp/src/ui/serum_wav_screen.cpp` (using `SerumWavOutputDir()`) and `cpp/src/ui/three_wav_screen.cpp` (using `ThreeWavOutputDir()`).

- [ ] **Step 5: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 77 tests still pass. Pure plumbing change — no behavior difference yet.

- [ ] **Step 6: Manual regression check**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/Harbor.app
```

Generate in any mode and verify the preview still plays. Behavior should be identical to before.

- [ ] **Step 7: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/generate_service_base.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/generate_service_base.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/serum_wav_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/three_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/generate_service_base.h \
        cpp/src/app/services/generate_service_base.cpp \
        cpp/src/ui/any_wav_screen.cpp \
        cpp/src/ui/serum_wav_screen.cpp \
        cpp/src/ui/three_wav_screen.cpp
git commit -m "refactor(cpp): split preview cache directory from user export directory"
```

---

## Task 3: ExportWriter (writes user-export bank, optionally decimated)

A free function that copies an 8-page bank from the preview cache to the user's export directory, optionally downsampling each page from 2048 to a target sample count. Uses libsamplerate's `src_simple` (already vendored, already wrapped in `fim::dsp::ResampleTo` from Phase 4c).

The signature is intentionally simple: src dir, dest dir, target sample count. The caller decides what target to pass — Task 5 wires it to `Settings::SamplesPerFrame()`.

**Files:**
- Create: `cpp/src/app/services/export_writer.h`
- Create: `cpp/src/app/services/export_writer.cpp`
- Modify: `cpp/src/app/services/generate_service_base.cpp`
- Modify: `cpp/CMakeLists.txt`
- Create: `cpp/tests/app_export_writer_test.cpp`
- Modify: `cpp/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `cpp/tests/app_export_writer_test.cpp`:

```cpp
#include "app/services/export_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "dr_wav.h"

namespace {

// Write a 2048-cycle bank to `dir` containing `num_pages` files named
// 1.wav .. N.wav, each of which is 64 cycles of 2048 samples (one sine
// per cell at unit amplitude).
void WriteFakeBank(const std::filesystem::path& dir, int num_pages = 8) {
    std::filesystem::create_directories(dir);
    constexpr std::size_t kCycleSamples = 2048;
    constexpr std::size_t kCellsPerPage = 64;
    constexpr std::uint32_t kSampleRate = 44100;

    for (int p = 1; p <= num_pages; ++p) {
        const auto path = dir / (std::to_string(p) + ".wav");
        drwav_data_format format = {};
        format.container = drwav_container_riff;
        format.format = DR_WAVE_FORMAT_PCM;
        format.channels = 1;
        format.sampleRate = kSampleRate;
        format.bitsPerSample = 16;
        drwav wav;
        REQUIRE(drwav_init_file_write(&wav, path.string().c_str(), &format, nullptr));
        std::vector<std::int16_t> samples(kCycleSamples * kCellsPerPage, 1234);
        drwav_write_pcm_frames(&wav, samples.size(), samples.data());
        drwav_uninit(&wav);
    }
}

drwav_uint64 GetFrameCount(const std::filesystem::path& path) {
    drwav wav;
    REQUIRE(drwav_init_file(&wav, path.string().c_str(), nullptr));
    const drwav_uint64 frames = wav.totalPCMFrameCount;
    drwav_uninit(&wav);
    return frames;
}

}  // namespace

TEST_CASE("WriteBankToExportDir copies 2048-sample bank unchanged when target is 2048",
          "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::filesystem::path src_dir =
        std::filesystem::path(tmp.path().toStdString()) / "src";
    const std::filesystem::path dest_dir =
        std::filesystem::path(tmp.path().toStdString()) / "dest";
    WriteFakeBank(src_dir);

    const bool ok = fim::app::WriteBankToExportDir(
        QString::fromStdString(src_dir.string()),
        QString::fromStdString(dest_dir.string()),
        /*target_samples_per_cycle=*/2048);
    REQUIRE(ok);

    for (int p = 1; p <= 8; ++p) {
        const auto path = dest_dir / (std::to_string(p) + ".wav");
        REQUIRE(std::filesystem::exists(path));
        REQUIRE(GetFrameCount(path) == 2048u * 64u);
    }
}

TEST_CASE("WriteBankToExportDir downsamples to 256 sample cycles when target is 256",
          "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const std::filesystem::path src_dir =
        std::filesystem::path(tmp.path().toStdString()) / "src";
    const std::filesystem::path dest_dir =
        std::filesystem::path(tmp.path().toStdString()) / "dest";
    WriteFakeBank(src_dir);

    const bool ok = fim::app::WriteBankToExportDir(
        QString::fromStdString(src_dir.string()),
        QString::fromStdString(dest_dir.string()),
        /*target_samples_per_cycle=*/256);
    REQUIRE(ok);

    for (int p = 1; p <= 8; ++p) {
        const auto path = dest_dir / (std::to_string(p) + ".wav");
        REQUIRE(std::filesystem::exists(path));
        // 64 cells * 256 samples = 16384.
        REQUIRE(GetFrameCount(path) == 256u * 64u);
    }
}

TEST_CASE("WriteBankToExportDir returns false when source dir is missing",
          "[app][export_writer]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString missing = tmp.filePath("does_not_exist");
    const QString dest = tmp.filePath("dest");
    const bool ok = fim::app::WriteBankToExportDir(missing, dest, 2048);
    REQUIRE_FALSE(ok);
}
```

- [ ] **Step 2: Add the test to the test target**

Modify `cpp/tests/CMakeLists.txt`. Add the test source and the implementation source. Find the `dsp_resample_test.cpp` line and add right after:

```cmake
    dsp_resample_test.cpp
    app_export_writer_test.cpp
```

And in the implementation source list, after `../src/dsp/resample.cpp`:

```cmake
    ../src/dsp/resample.cpp
    ../src/app/services/export_writer.cpp
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `app/services/export_writer.h` does not exist.

- [ ] **Step 4: Write the header**

Create `cpp/src/app/services/export_writer.h`:

```cpp
#pragma once

#include <QString>

namespace fim::app {

// Copies an 8-page wavetable bank from a source directory to a destination
// directory, optionally downsampling each cycle from 2048 samples to
// target_samples_per_cycle (e.g. 256 for Waveedit-target banks).
//
// The source directory is expected to contain 1.wav .. 8.wav, each of
// which is a sequence of 64 single-cycle waveforms concatenated. Each
// cycle is 2048 samples; the function decimates per-cycle so the total
// frame count of each output file is `64 * target_samples_per_cycle`.
//
// If target_samples_per_cycle == 2048, the source files are copied
// verbatim (no resample, no quality loss).
//
// Resampling uses libsamplerate's SRC_SINC_MEDIUM_QUALITY converter via
// fim::dsp::ResampleTo.
//
// Returns false on any I/O or DSP error. Creates the destination directory
// if it does not exist.
bool WriteBankToExportDir(const QString& source_dir, const QString& dest_dir,
                          int target_samples_per_cycle);

}  // namespace fim::app
```

- [ ] **Step 5: Write the implementation**

Create `cpp/src/app/services/export_writer.cpp`:

```cpp
#include "app/services/export_writer.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "dr_wav.h"
#include "dsp/resample.h"

namespace fim::app {

namespace {

constexpr int kNumPages = 8;
constexpr int kCellsPerPage = 64;
constexpr int kSourceCycleSamples = 2048;
constexpr std::uint32_t kSampleRate = 44100;

bool ReadWavMono16(const std::filesystem::path& path, std::vector<float>& out) {
    drwav wav;
    if (!drwav_init_file(&wav, path.string().c_str(), nullptr)) {
        return false;
    }
    out.resize(wav.totalPCMFrameCount * wav.channels);
    const drwav_uint64 read =
        drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, out.data());
    drwav_uninit(&wav);
    if (read != wav.totalPCMFrameCount) {
        return false;
    }
    if (wav.channels > 1) {
        // Average down to mono.
        std::vector<float> mono(read);
        for (drwav_uint64 i = 0; i < read; ++i) {
            float sum = 0.0f;
            for (drwav_uint16 c = 0; c < wav.channels; ++c) {
                sum += out[i * wav.channels + c];
            }
            mono[i] = sum / static_cast<float>(wav.channels);
        }
        out = std::move(mono);
    }
    return true;
}

bool WriteWavMono16(const std::filesystem::path& path, const std::vector<float>& samples) {
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
    std::vector<std::int16_t> int_samples(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, samples[i]));
        int_samples[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }
    const drwav_uint64 written =
        drwav_write_pcm_frames(&wav, int_samples.size(), int_samples.data());
    drwav_uninit(&wav);
    return written == int_samples.size();
}

}  // namespace

bool WriteBankToExportDir(const QString& source_dir, const QString& dest_dir,
                          int target_samples_per_cycle) {
    if (target_samples_per_cycle <= 0 || target_samples_per_cycle > kSourceCycleSamples) {
        return false;
    }
    const std::filesystem::path src(source_dir.toStdString());
    const std::filesystem::path dest(dest_dir.toStdString());
    if (!std::filesystem::exists(src)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        return false;
    }

    for (int p = 1; p <= kNumPages; ++p) {
        const auto src_path = src / (std::to_string(p) + ".wav");
        const auto dest_path = dest / (std::to_string(p) + ".wav");
        if (!std::filesystem::exists(src_path)) {
            return false;
        }

        if (target_samples_per_cycle == kSourceCycleSamples) {
            // Identity copy — no resample, no quality loss.
            std::error_code copy_ec;
            std::filesystem::copy_file(
                src_path, dest_path,
                std::filesystem::copy_options::overwrite_existing, copy_ec);
            if (copy_ec) {
                return false;
            }
            continue;
        }

        // Read source bank, decimate per-cycle, write destination.
        std::vector<float> source_samples;
        if (!ReadWavMono16(src_path, source_samples)) {
            return false;
        }
        const std::size_t expected_source = kCellsPerPage * kSourceCycleSamples;
        if (source_samples.size() != expected_source) {
            return false;
        }

        std::vector<float> dest_samples;
        dest_samples.reserve(kCellsPerPage * target_samples_per_cycle);
        for (int cell = 0; cell < kCellsPerPage; ++cell) {
            std::vector<float> cycle(
                source_samples.begin() + cell * kSourceCycleSamples,
                source_samples.begin() + (cell + 1) * kSourceCycleSamples);
            // Use ResampleTo with synthetic rates that produce the desired
            // ratio. e.g. 2048 -> 256 is an 8:1 downsample.
            const std::uint32_t in_rate = kSourceCycleSamples;
            const std::uint32_t out_rate =
                static_cast<std::uint32_t>(target_samples_per_cycle);
            auto resampled = fim::dsp::ResampleTo(cycle, in_rate, out_rate);
            // libsamplerate may produce off-by-one frames; trim or pad to
            // exactly target_samples_per_cycle.
            resampled.resize(target_samples_per_cycle, 0.0f);
            dest_samples.insert(dest_samples.end(), resampled.begin(), resampled.end());
        }

        if (!WriteWavMono16(dest_path, dest_samples)) {
            return false;
        }
    }
    return true;
}

}  // namespace fim::app
```

- [ ] **Step 6: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/app/services/export_writer.cpp` right after `src/app/services/three_wav_service.cpp`:

```cmake
    src/app/services/three_wav_service.cpp
    src/app/services/export_writer.cpp
    src/app/services/single_wav_service.cpp
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 80 tests pass (77 from Phase 4c + 3 new export writer tests).

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/export_writer.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/export_writer.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/app_export_writer_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/services/export_writer.h \
        cpp/src/app/services/export_writer.cpp \
        cpp/tests/app_export_writer_test.cpp \
        cpp/CMakeLists.txt cpp/tests/CMakeLists.txt
git commit -m "feat(cpp): add ExportWriter for dual-write + Waveedit-size decimation"
```

---

## Task 4: Wire ExportWriter into the service base + Settings::OutputDir

Connect the dots: each successful generation now writes to the preview cache (already wired) AND to the user-chosen export directory via `WriteBankToExportDir`. The user output dir is read from `Settings::OutputDir()`. Default is `~/Documents/Harbor`.

The samples-per-frame parameter is read from `Settings::SamplesPerFrame()`, which still defaults to 2048 in this task — Task 5 adds the menu UI to change it.

**Files:**
- Modify: `cpp/src/app/settings.cpp`
- Modify: `cpp/src/app/services/generate_service_base.h`
- Modify: `cpp/src/app/services/generate_service_base.cpp`
- Modify: `cpp/src/ui/any_wav_screen.cpp`
- Modify: `cpp/src/ui/serum_wav_screen.cpp`
- Modify: `cpp/src/ui/three_wav_screen.cpp`

- [ ] **Step 1: Make Settings::OutputDir default to ~/Documents/Harbor**

In `cpp/src/app/settings.cpp`, add the include:

```cpp
#include <QDir>
#include <QStandardPaths>
```

And change the OutputDir default:

```cpp
std::string Settings::OutputDir() const {
    const QString default_dir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath("Harbor");
    return backing_.value(kKeyOutputDir, default_dir).toString().toStdString();
}
```

- [ ] **Step 2: Add a samples-per-frame field to GenerateServiceBase**

In `cpp/src/app/services/generate_service_base.h`:

```cpp
public:
    void SetSamplesPerFrame(int samples);
    int SamplesPerFrame() const;

private:
    int samples_per_frame_ = 2048;
```

- [ ] **Step 3: Implement the accessors**

In `cpp/src/app/services/generate_service_base.cpp`:

```cpp
void GenerateServiceBase::SetSamplesPerFrame(int samples) {
    samples_per_frame_ = samples;
}

int GenerateServiceBase::SamplesPerFrame() const {
    return samples_per_frame_;
}
```

- [ ] **Step 4: Call ExportWriter from Generate()**

In `cpp/src/app/services/generate_service_base.cpp`, add the include:

```cpp
#include "app/services/export_writer.h"
```

And update `Generate()` to call `WriteBankToExportDir` after `DoGenerate` succeeds:

```cpp
void GenerateServiceBase::Generate() {
    if (generating_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    const QString in_file = input_file_;
    const QString cache_dir = preview_cache_directory_;
    const QString user_export_dir = output_directory_;
    const int samples_per_frame = samples_per_frame_;

    QThreadPool::globalInstance()->start([this, in_file, cache_dir, user_export_dir,
                                          samples_per_frame]() {
        const std::filesystem::path input_path(in_file.toStdString());
        const std::filesystem::path cache_path(cache_dir.toStdString());

        const bool dsp_ok = DoGenerate(input_path, cache_path, [this](int percent) {
            emit progressChanged(percent);
        });

        bool export_ok = true;
        if (dsp_ok && !user_export_dir.isEmpty() && user_export_dir != cache_dir) {
            export_ok = WriteBankToExportDir(cache_dir, user_export_dir, samples_per_frame);
        }

        generating_.store(false, std::memory_order_release);

        if (dsp_ok && export_ok) {
            emit generationFinished();
        } else if (!dsp_ok) {
            emit generationFailed(
                QString("Failed to generate wavetable bank from %1").arg(in_file));
        } else {
            emit generationFailed(
                QString("Generated bank, but failed to write to %1").arg(user_export_dir));
        }
    });
}
```

Note the guard `user_export_dir != cache_dir`: if the two are the same (legacy default), skip the export write — the cache *is* the user dir. This matches the behavior we have today before the user picks a custom output dir.

- [ ] **Step 5: Wire each screen to read from Settings::OutputDir**

In `cpp/src/ui/any_wav_screen.cpp`, change the constructor block from:

```cpp
    service_->SetPreviewCacheDirectory(SingleWavOutputDir());
    service_->SetOutputDirectory(SingleWavOutputDir());
```

to:

```cpp
    service_->SetPreviewCacheDirectory(SingleWavOutputDir());
    const QString user_dir = QDir(QString::fromStdString(settings->OutputDir()))
                                 .filePath("any_wav");
    service_->SetOutputDirectory(user_dir);
    service_->SetSamplesPerFrame(settings->SamplesPerFrame());
```

You'll need to add `#include <QDir>` at the top if it's not already there.

Apply the equivalent change in `serum_wav_screen.cpp` (subdir `serum_wav`) and `three_wav_screen.cpp` (subdir `three_wav`).

- [ ] **Step 6: Build, test, and manually verify**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/Harbor.app
```

Manual checks:
- Generate in any-wav mode. After "Done!", check `~/Documents/Harbor/any_wav/` — should contain 1.wav .. 8.wav.
- Preview should still play (loads from the preview cache, not from `~/Documents/Harbor`).
- Same for serum and three-wav modes (subdirs `serum_wav` and `three_wav`).

- [ ] **Step 7: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/settings.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/generate_service_base.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/services/generate_service_base.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/serum_wav_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/three_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/settings.cpp \
        cpp/src/app/services/generate_service_base.h \
        cpp/src/app/services/generate_service_base.cpp \
        cpp/src/ui/any_wav_screen.cpp \
        cpp/src/ui/serum_wav_screen.cpp \
        cpp/src/ui/three_wav_screen.cpp
git commit -m "feat(cpp): write export bank to user-configurable Documents/Harbor"
```

---

## Task 5: File menu — Choose output directory

A real File menu with a "Choose output directory…" entry that opens a `QFileDialog::getExistingDirectory`, persists the result via `Settings::SetOutputDir`, and pushes the new value to the live screens so the next generation uses it.

**Files:**
- Modify: `cpp/src/ui/main_window.h`
- Modify: `cpp/src/ui/main_window.cpp`
- Modify: `cpp/src/ui/any_wav_screen.h` / `cpp/src/ui/any_wav_screen.cpp` (add a `RefreshOutputDirFromSettings()` slot)
- Modify: `cpp/src/ui/serum_wav_screen.h` / `cpp/src/ui/serum_wav_screen.cpp` (same)
- Modify: `cpp/src/ui/three_wav_screen.h` / `cpp/src/ui/three_wav_screen.cpp` (same)

- [ ] **Step 1: Add a public RefreshOutputDirFromSettings slot to each mode screen**

In `cpp/src/ui/any_wav_screen.h`, add to the public section:

```cpp
public slots:
    // Re-read Settings::OutputDir() and update the service so the next
    // generation writes to the new location. Called by MainWindow when
    // the user picks a new output directory from the File menu.
    void RefreshOutputDirFromSettings();
```

And in `cpp/src/ui/any_wav_screen.cpp`, implement it. Extract the per-mode dir-mapping into a helper to avoid duplicating the `QDir(...).filePath("any_wav")` logic between the constructor and the refresh slot:

```cpp
void AnyWavScreen::RefreshOutputDirFromSettings() {
    const QString user_dir = QDir(QString::fromStdString(settings()->OutputDir()))
                                 .filePath("any_wav");
    service_->SetOutputDirectory(user_dir);
    service_->SetSamplesPerFrame(settings()->SamplesPerFrame());
}
```

You may need to add a `settings()` accessor to `ModeScreenBase` (it already has `settings_` as a field but the getter may not be public — check `mode_screen_base.h` and add `Settings* settings() const { return settings_; }` to the protected section if not already there). Note: the existing `protected accessors` block has `fim::app::Settings* settings() const { return settings_; }` — already exists.

Apply equivalent additions to `serum_wav_screen.h/cpp` (subdir `serum_wav`) and `three_wav_screen.h/cpp` (subdir `three_wav`).

- [ ] **Step 2: Build the File menu in MainWindow**

In `cpp/src/ui/main_window.cpp`, replace the existing `BuildMenuBar()` body. Add includes at the top:

```cpp
#include <QAction>
#include <QFileDialog>
#include <QStandardPaths>
```

Replace `BuildMenuBar()`:

```cpp
void MainWindow::BuildMenuBar() {
    auto* file_menu = menuBar()->addMenu("&File");
    auto* choose_dir_action =
        file_menu->addAction("Choose output directory…", this,
                             &MainWindow::OnChooseOutputDirectory);
    choose_dir_action->setStatusTip("Pick where Harbor saves your generated wavetable banks");
    file_menu->addSeparator();
    file_menu->addAction("Quit", QKeySequence::Quit, this, &QWidget::close);

    // Export menu (Task 6 fills this in)
    menuBar()->addMenu("&Export");

    // Help menu (Task 7 fills this in)
    menuBar()->addMenu("&Help");
}

void MainWindow::OnChooseOutputDirectory() {
    const QString current = QString::fromStdString(settings_.OutputDir());
    const QString picked = QFileDialog::getExistingDirectory(
        this, "Choose Harbor output directory", current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (picked.isEmpty()) {
        return;  // user cancelled
    }
    settings_.SetOutputDir(picked.toStdString());
    // Push the new value to all three screens.
    any_wav_screen_->RefreshOutputDirFromSettings();
    serum_wav_screen_->RefreshOutputDirFromSettings();
    three_wav_screen_->RefreshOutputDirFromSettings();
}
```

- [ ] **Step 3: Declare the new slot in MainWindow's header**

In `cpp/src/ui/main_window.h`, add to the private slots section:

```cpp
private slots:
    void OnModeChosen(int mode);
    void OnBackToLauncher();
    void OnChooseOutputDirectory();
```

- [ ] **Step 4: Build and manually verify**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/Harbor.app
```

Manual checks:
- File menu → "Choose output directory…" opens a directory picker.
- Pick a different directory (e.g. ~/Desktop/harbor-test).
- Generate in any mode → output appears in the new dir (e.g. `~/Desktop/harbor-test/any_wav/`).
- Quit and relaunch → File menu still uses the new dir (persisted via QSettings).

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/any_wav_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/serum_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/serum_wav_screen.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/three_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/three_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/main_window.h cpp/src/ui/main_window.cpp \
        cpp/src/ui/any_wav_screen.h cpp/src/ui/any_wav_screen.cpp \
        cpp/src/ui/serum_wav_screen.h cpp/src/ui/serum_wav_screen.cpp \
        cpp/src/ui/three_wav_screen.h cpp/src/ui/three_wav_screen.cpp
git commit -m "feat(cpp): add File > Choose output directory menu"
```

---

## Task 6: Export menu — Target hardware (Four Seas / Waveedit)

A radio-style submenu under the Export menu that lets the user choose between Four Seas (2048 samples per cycle) and Waveedit (256 samples per cycle). The selection is persisted via `Settings::SamplesPerFrame()` and pushed to the live screens via the same `RefreshOutputDirFromSettings()` slot from Task 5 (which already calls `SetSamplesPerFrame`).

**Files:**
- Modify: `cpp/src/ui/main_window.h`
- Modify: `cpp/src/ui/main_window.cpp`

- [ ] **Step 1: Add a private member for the action group**

In `cpp/src/ui/main_window.h`, forward-declare `QActionGroup` and add a member:

```cpp
class QActionGroup;
```

```cpp
private:
    void BuildMenuBar();

    // Owned by the menu — kept as a member so we can query selection.
    QActionGroup* target_hardware_group_ = nullptr;
```

And declare the slot:

```cpp
private slots:
    void OnTargetHardwareChanged();
```

- [ ] **Step 2: Build the Export submenu**

In `cpp/src/ui/main_window.cpp`, add the include:

```cpp
#include <QActionGroup>
```

Replace the placeholder `menuBar()->addMenu("&Export");` with a real menu:

```cpp
    auto* export_menu = menuBar()->addMenu("&Export");
    auto* target_menu = export_menu->addMenu("Target hardware");
    target_hardware_group_ = new QActionGroup(this);
    target_hardware_group_->setExclusive(true);

    auto* four_seas_action = target_menu->addAction("Four Seas (2048 samples)");
    four_seas_action->setCheckable(true);
    four_seas_action->setData(2048);
    target_hardware_group_->addAction(four_seas_action);

    auto* waveedit_action = target_menu->addAction("Waveedit (256 samples)");
    waveedit_action->setCheckable(true);
    waveedit_action->setData(256);
    target_hardware_group_->addAction(waveedit_action);

    // Restore the persisted selection.
    const int current = settings_.SamplesPerFrame();
    if (current == 256) {
        waveedit_action->setChecked(true);
    } else {
        four_seas_action->setChecked(true);
    }

    connect(target_hardware_group_, &QActionGroup::triggered, this,
            &MainWindow::OnTargetHardwareChanged);
```

And implement the slot:

```cpp
void MainWindow::OnTargetHardwareChanged() {
    auto* checked = target_hardware_group_->checkedAction();
    if (!checked) {
        return;
    }
    const int samples = checked->data().toInt();
    settings_.SetSamplesPerFrame(samples);
    any_wav_screen_->RefreshOutputDirFromSettings();
    serum_wav_screen_->RefreshOutputDirFromSettings();
    three_wav_screen_->RefreshOutputDirFromSettings();
}
```

- [ ] **Step 3: Build and manually verify**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/Harbor.app
```

Manual checks:
- Export → Target hardware shows two radio items, Four Seas selected by default.
- Switch to Waveedit → generate in any mode → check the resulting `.wav` file in the user output dir. Use `afinfo` or `magick identify` to confirm 256 samples per cycle (16384 total frames per page = 64 cells × 256).
- Preview should still play normally (the cache copy is always 2048).
- Switch back to Four Seas → generate again → 2048 samples per cycle (131072 total per page = 64 × 2048).
- Quit and relaunch → the radio selection persists.

- [ ] **Step 4: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/main_window.h cpp/src/ui/main_window.cpp
git commit -m "feat(cpp): add Export > Target hardware menu (Four Seas / Waveedit)"
```

---

## Task 7: Help menu — About + Help dialogs

Two modal dialogs: a small About panel with the app name + version + license + attributions, and a Help dialog containing rendered HTML loaded from a Qt resource. Both wired into the Help menu.

**Files:**
- Create: `cpp/resources/help/help.html`
- Create: `cpp/src/ui/dialogs/about_dialog.h`
- Create: `cpp/src/ui/dialogs/about_dialog.cpp`
- Create: `cpp/src/ui/dialogs/help_dialog.h`
- Create: `cpp/src/ui/dialogs/help_dialog.cpp`
- Modify: `cpp/src/ui/main_window.h`
- Modify: `cpp/src/ui/main_window.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the help HTML**

Create `cpp/resources/help/help.html` with content suitable for a first-time user. Plain HTML4-style — Qt's rich text engine doesn't do CSS3:

```html
<!DOCTYPE html>
<html>
<head><title>Harbor Help</title></head>
<body>
<h1>Harbor</h1>
<p><b>Harbor</b> generates wavetable banks for Eurorack hardware (Ferry Island Modular's Four Seas) and other 256-sample wavetable hosts (e.g. Waveedit / Synthesis Technology).</p>

<h2>Three modes</h2>
<dl>
<dt><b>Use any .wav file</b></dt>
<dd>Drop in a single audio file. Harbor analyzes it and creates an 8&nbsp;page &times; 64&nbsp;cell wavetable bank by spectrally morphing the source across the X, Y, and Z axes. The Y and Z morph mode selectors control how the spectrum is transformed.</dd>

<dt><b>Use a Serum .wav file</b></dt>
<dd>Drop in a Serum-format wavetable (256 frames of 2048 samples each, 32-bit float). Harbor resamples the frame timeline to 8 pages and applies the same Y/Z morph modes as Any-WAV.</dd>

<dt><b>Use three .wav files</b></dt>
<dd>Drop in three audio files, one per axis. Each cell in the 8&times;8&times;8 grid blends the three files' spectra weighted by the cell's position. No morph modes — variety comes from the three source files you pick.</dd>
</dl>

<h2>Output directory</h2>
<p>Use <b>File &gt; Choose output directory&hellip;</b> to pick where Harbor saves the generated banks. By default they go to <code>~/Documents/Harbor/</code>, organized into per-mode subdirectories.</p>

<h2>Target hardware</h2>
<p>Use <b>Export &gt; Target hardware</b> to choose between:</p>
<ul>
<li><b>Four Seas (2048 samples per cycle)</b> &mdash; the default. Compatible with Ferry Island Modular's Four Seas oscillator.</li>
<li><b>Waveedit (256 samples per cycle)</b> &mdash; compatible with Synthesis Technology's Waveedit and other 256-sample wavetable hosts.</li>
</ul>
<p>The preview always plays the 2048-sample version regardless of which target you've selected.</p>

<h2>Preview</h2>
<p>After Generate finishes, the preview panel appears with X, Y, Z, Pitch, and Volume sliders. Click <b>Play steady tone</b> to audition the bank. Drag the sliders to scan through the wavetable grid.</p>

<h2>Reporting bugs</h2>
<p>Harbor is in beta. Please report issues at <a href="https://github.com/jgoney/fim-config-tool/issues">github.com/jgoney/fim-config-tool/issues</a>.</p>

</body>
</html>
```

- [ ] **Step 2: Add the help HTML to the Qt resource bundle**

In `cpp/CMakeLists.txt`, after the existing `qt_add_resources` blocks for `app_styles` and `app_fonts`, add:

```cmake
qt_add_resources(fim-config-tool "app_help"
    PREFIX "/help"
    BASE   "${CMAKE_CURRENT_SOURCE_DIR}/resources/help"
    FILES  "${CMAKE_CURRENT_SOURCE_DIR}/resources/help/help.html"
)
```

- [ ] **Step 3: Write the AboutDialog class**

Create `cpp/src/ui/dialogs/about_dialog.h`:

```cpp
#pragma once

#include <QDialog>

namespace fim::ui {

// Modal "About Harbor" dialog. Single fixed-content panel with app name,
// version, license, and library attributions.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace fim::ui
```

Create `cpp/src/ui/dialogs/about_dialog.cpp`:

```cpp
#include "ui/dialogs/about_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace fim::ui {

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("About Harbor");
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto* title = new QLabel("<h2>Harbor</h2>", this);
    layout->addWidget(title);

    auto* body = new QLabel(this);
    body->setText(QString(
                      "<p>Version: %1 (beta)</p>"
                      "<p>Wavetable bank generator for Ferry Island Modular hardware "
                      "and other wavetable synth hosts.</p>"
                      "<p>License: MIT</p>"
                      "<p>Built with Qt, libsamplerate, dr_wav, miniaudio, PFFFT, "
                      "spdlog, Catch2, and the FourSeas firmware engine.</p>"
                      "<p><a href='https://github.com/jgoney/fim-config-tool'>"
                      "github.com/jgoney/fim-config-tool</a></p>")
                      .arg(HARBOR_VERSION));
    body->setOpenExternalLinks(true);
    body->setWordWrap(true);
    layout->addWidget(body);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    setFixedSize(420, sizeHint().height());
}

}  // namespace fim::ui
```

- [ ] **Step 4: Write the HelpDialog class**

Create `cpp/src/ui/dialogs/help_dialog.h`:

```cpp
#pragma once

#include <QDialog>

namespace fim::ui {

// Modal Help dialog. Renders the embedded help.html via QTextBrowser.
// Closes on Esc or by clicking the OK button.
class HelpDialog : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(QWidget* parent = nullptr);
};

}  // namespace fim::ui
```

Create `cpp/src/ui/dialogs/help_dialog.cpp`:

```cpp
#include "ui/dialogs/help_dialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fim::ui {

HelpDialog::HelpDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Harbor Help");
    setModal(true);
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);

    QFile help_file(":/help/help.html");
    if (help_file.open(QIODevice::ReadOnly)) {
        browser->setHtml(QString::fromUtf8(help_file.readAll()));
        help_file.close();
    } else {
        browser->setHtml("<p>Help content failed to load.</p>");
    }
    layout->addWidget(browser);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

}  // namespace fim::ui
```

- [ ] **Step 5: Wire the dialogs into the Help menu**

In `cpp/src/ui/main_window.h`, add the slot:

```cpp
private slots:
    void OnShowHelp();
    void OnShowAbout();
```

In `cpp/src/ui/main_window.cpp`, add includes:

```cpp
#include "ui/dialogs/about_dialog.h"
#include "ui/dialogs/help_dialog.h"
```

Replace the `menuBar()->addMenu("&Help");` placeholder with:

```cpp
    auto* help_menu = menuBar()->addMenu("&Help");
    help_menu->addAction("Harbor Help…", this, &MainWindow::OnShowHelp);
    help_menu->addSeparator();
    help_menu->addAction("About Harbor…", this, &MainWindow::OnShowAbout);
```

And implement the slots:

```cpp
void MainWindow::OnShowHelp() {
    HelpDialog dialog(this);
    dialog.exec();
}

void MainWindow::OnShowAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}
```

- [ ] **Step 6: Add new sources to the executable target**

In `cpp/CMakeLists.txt`, add the dialog sources to the `qt_add_executable(fim-config-tool ...)` block, right after the existing `src/ui/three_wav_screen.cpp`:

```cmake
    src/ui/three_wav_screen.cpp
    src/ui/dialogs/about_dialog.cpp
    src/ui/dialogs/help_dialog.cpp
```

- [ ] **Step 7: Build and verify**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/Harbor.app
```

Manual checks:
- Help → Harbor Help… → modal dialog with rendered HTML, scrollable, links open in browser, OK closes.
- Help → About Harbor… → smaller modal with version string from HARBOR_VERSION, link to GitHub repo, OK closes.

- [ ] **Step 8: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/dialogs/about_dialog.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/dialogs/about_dialog.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/dialogs/help_dialog.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/dialogs/help_dialog.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/resources/help/help.html \
        cpp/src/ui/dialogs/about_dialog.h cpp/src/ui/dialogs/about_dialog.cpp \
        cpp/src/ui/dialogs/help_dialog.h cpp/src/ui/dialogs/help_dialog.cpp \
        cpp/src/ui/main_window.h cpp/src/ui/main_window.cpp \
        cpp/CMakeLists.txt
git commit -m "feat(cpp): add Help > Harbor Help and About Harbor dialogs"
```

---

## Task 8: Read Me First.rtf in the .dmg

Add a small RTF document next to the `.app` in the staged `.dmg`. Tester sees it the moment they mount the disk image.

**Files:**
- Create: `cpp/resources/dist/Read Me First.rtf`
- Modify: `cpp/scripts/package-macos.sh`

- [ ] **Step 1: Write the RTF**

Create `cpp/resources/dist/Read Me First.rtf`. RTF is plaintext with control codes; keep it simple:

```rtf
{\rtf1\ansi\ansicpg1252\cocoartf2761
\fonttbl\f0\fswiss\fcharset0 Helvetica;\f1\fswiss\fcharset0 Helvetica-Bold;
\colortbl;\red0\green0\blue0;
\paperw11900\paperh16840\margl1440\margr1440\vieww14000\viewh8400\viewkind0
\pard\sa200\sl276\slmult1

\f1\b Welcome to Harbor (beta)\f0\b0\par
\par
Harbor is a desktop tool for generating wavetable banks for Ferry Island Modular hardware and other wavetable synth hosts.\par
\par
\f1\b Installing\f0\b0\par
\par
1. Drag {\f1\b Harbor\f0\b0} to the Applications folder.\par
\par
2. Open the Terminal app and run this command (copy/paste the whole line):\par
\par
\f1\b xattr -dr com.apple.quarantine "/Applications/Harbor.app"\f0\b0\par
\par
This removes macOS's "downloaded from internet" flag so the app will open. (Harbor is not yet notarized by Apple, so without this step you'll see an "app is damaged" warning. We're working on a notarized release.)\par
\par
3. Launch Harbor from Applications normally.\par
\par
\f1\b Alternative: no Terminal\f0\b0\par
\par
Right-click {\f1\b Harbor\f0\b0} in Applications, choose Open, then click Open in the warning dialog. macOS remembers this and won't ask again.\par
\par
\f1\b Reporting bugs\f0\b0\par
\par
Please file issues at github.com/jgoney/fim-config-tool/issues.\par
\par
Thanks for testing!\par
}
```

- [ ] **Step 2: Update the package script to include the RTF**

In `cpp/scripts/package-macos.sh`, find the `STAGE_DIR` block where the `.app` is copied in. Add a copy of the RTF before `hdiutil create`:

```bash
STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT
cp -R "$APP_PATH" "$STAGE_DIR/"
ln -s /Applications "$STAGE_DIR/Applications"
cp "${REPO_ROOT}/cpp/resources/dist/Read Me First.rtf" "$STAGE_DIR/"
```

- [ ] **Step 3: Repackage and verify**

```bash
cpp/scripts/package-macos.sh
```

Mount the resulting `.dmg` (double-click in Finder) and confirm:
- The `.app` is there
- The Applications symlink is there
- `Read Me First.rtf` is there and opens in TextEdit with formatted content

- [ ] **Step 4: Commit**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add "cpp/resources/dist/Read Me First.rtf" cpp/scripts/package-macos.sh
git commit -m "build(cpp): include Read Me First.rtf in macOS .dmg"
```

---

## Task 9: Followups cleanup

Remove resolved entries from `docs/followups.md` and add a couple of small new ones discovered during Phase 5.

**Files:**
- Modify: `docs/followups.md`

- [ ] **Step 1: Remove resolved items**

Open `docs/followups.md` and remove these now-fixed items:

- "**Stub bank output dir is hardcoded to `QStandardPaths::AppLocalDataLocation/audio_resynth`.**" — resolved by Tasks 4 + 5.
- "**Styling needs manual tuning.**" — keep, but rephrase to acknowledge that the absolute minimum (equal columns + min size) is done and the rest is deferred to a post-beta polish phase.

- [ ] **Step 2: Add new items**

Append to the Phase 5 / Distribution section:

```markdown
- **Help dialog content needs review.** `cpp/resources/help/help.html` was written from a developer's mental model of the tool. After the first round of beta testers, rewrite based on the questions they actually ask. May want to add screenshots once the styling is finalized.
- **AboutDialog version string is hardcoded to "(beta)".** Once we cut a non-beta release, drop the suffix and pull the channel from a CMake option (e.g. `-DHARBOR_RELEASE_CHANNEL=stable`).
- **`SingleWavService` (and the other two services) still don't validate the input file** before passing it to the DSP layer. A non-WAV path produces a generic "Failed to generate" error. Add explicit `LoadWav` probe + user-friendly error message in a follow-up phase. (Originally noted in Phase 2.)
```

- [ ] **Step 3: Commit**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add docs/followups.md
git commit -m "docs: update followups for Phase 5 (output dir + Waveedit target landed)"
```

---

## Task 10: Push and verify CI

After the code-side work in Tasks 1–9 is committed, push to origin and verify all four CI jobs are green before proceeding to the Python-deletion task.

- [ ] **Step 1: Push**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
```

- [ ] **Step 2: Watch CI**

```bash
sleep 5
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId,status --jq '.[0]'
gh run watch <id>
```

- [ ] **Step 3: VERIFY explicitly**

```bash
gh run view <id>
```

Expected: 4 green jobs (clang-format, macos-latest, ubuntu-latest, windows-latest).

If anything fails, fix it in a new commit and re-push. Do NOT proceed to Task 11 until CI is green — the Python deletion is irreversible enough that we want a known-good state on the C++ side first.

---

## Task 11: Delete the Python project

The big cleanup. Removes the entire Python source tree, project metadata, build scripts, and the legacy top-level `README.md` / `CLAUDE.md`. Replaces the top-level docs with C++/Harbor-focused versions.

This is a single big commit so the history is bisectable: before this commit, the repo has both Python and C++; after this commit, it's C++-only.

**Files:**
- Delete: `src/` (entire directory)
- Delete: `pyproject.toml`
- Delete: `uv.lock`
- Delete: `.python-version`
- Delete: `__init__.py` (root-level)
- Delete: `BUILD.md`
- Delete: `build.sh`
- Delete: `create_dmg.sh`
- Delete: `FIM Config Tool.spec`
- Delete: `TODO.md`
- Replace: `README.md` (top-level)
- Replace: `CLAUDE.md` (top-level)

**Files NOT touched:** `cpp/`, `docs/`, `designs/`, `LICENSE`, `.gitignore`, `.github/`, `.gitmodules`, `.git/`.

- [ ] **Step 1: Sanity check the staged deletions**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
ls src/ pyproject.toml uv.lock .python-version __init__.py BUILD.md build.sh create_dmg.sh "FIM Config Tool.spec" TODO.md README.md CLAUDE.md 2>&1 | head -30
```

Confirm all the listed files exist before deleting them. If any are missing, the rm commands below will fail loudly — that's fine, it just means someone already cleaned them up.

- [ ] **Step 2: Delete the Python files**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git rm -rf src/
git rm pyproject.toml uv.lock .python-version __init__.py
git rm BUILD.md build.sh create_dmg.sh "FIM Config Tool.spec" TODO.md
```

- [ ] **Step 3: Write the new top-level README.md**

Create `README.md` (overwriting the old one):

```markdown
# Harbor

A desktop tool for generating wavetable banks for Ferry Island Modular hardware (Four Seas) and other wavetable synth hosts (Waveedit / Synthesis Technology).

Three input modes:

- **Single .wav** — analyze any audio file and morph its spectrum across the X / Y / Z axes
- **Serum .wav** — convert a Serum 256-frame wavetable into an 8 × 64 cell bank
- **Three .wavs** — blend three audio files, one per axis, via cross-synthesis

The output is 8 WAV files (one per Z page), each containing 64 single-cycle waveforms ready to load into a wavetable oscillator.

## Status

Beta. macOS Apple Silicon only for distribution today; the source builds on Linux and Windows via CI.

## Building

See [`cpp/README.md`](cpp/README.md) for build instructions.

```bash
git submodule update --init --recursive
cd cpp
cmake -G Ninja -B build
cmake --build build
open build/Harbor.app
```

## Packaging for distribution

```bash
cpp/scripts/package-macos.sh
# Output: cpp/build/dist/Harbor-<version>.dmg
```

## License

MIT — see [LICENSE](LICENSE).
```

- [ ] **Step 4: Write the new top-level CLAUDE.md**

Create `CLAUDE.md` (overwriting the old Python-focused one):

```markdown
# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Harbor is a C++/Qt desktop application that generates wavetable banks for Ferry Island Modular hardware and other wavetable synth hosts. It started as a Python project (PySide6 + librosa) and was rewritten in C++ for native performance, distribution simplicity, and feature parity across macOS / Windows / Linux. The Python source was removed when the C++ rewrite reached feature parity in Phase 5.

## Tech Stack

- C++20, Qt 6.8 (Widgets, Core, Gui)
- CMake + Ninja
- libsamplerate, dr_wav, miniaudio, PFFFT (vendored or via FetchContent)
- Catch2 v3 for tests
- spdlog for logging
- FourSeas firmware engine vendored as a git submodule under `cpp/third_party/Four-Seas`

## Repository Layout

- `cpp/` — the application
  - `cpp/src/app/` — application/services layer (settings, generate services)
  - `cpp/src/dsp/` — DSP cores (STFT, FFT, generators, resamplers)
  - `cpp/src/engine/` — realtime audio engine (preview playback)
  - `cpp/src/ui/` — Qt Widgets UI (screens, dialogs, custom widgets)
  - `cpp/tests/` — Catch2 tests
  - `cpp/scripts/` — packaging and icon generation scripts
  - `cpp/resources/` — fonts, icons, help HTML, dist artifacts
  - `cpp/third_party/` — vendored single-header libs and FourSeas submodule
- `docs/` — design notes, implementation plans, followups
- `designs/` — Penpot SVG exports of the UI mockups
- `.github/workflows/` — CI configuration

## Build

```bash
git submodule update --init --recursive
cd cpp
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The macOS bundle ends up at `cpp/build/Harbor.app`.

## Distribution

```bash
cpp/scripts/package-macos.sh
```

Produces `cpp/build/dist/Harbor-<git-version>.dmg` — ad-hoc signed (not notarized). See `cpp/scripts/README.md` for details.
```

- [ ] **Step 5: Stage the new docs and verify the deletion is complete**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add README.md CLAUDE.md
git status
```

`git status` should show:
- Deleted: all the Python files listed above
- Modified: `README.md`, `CLAUDE.md` (or "new file" if `git rm` removed them first)
- Untracked: `designs/` (still untracked, that's fine — separate concern)

Confirm the `cpp/` tree is unchanged.

- [ ] **Step 6: Build and test from the slimmed-down repo**

```bash
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure
```

Expected: 80 tests pass. The deletion shouldn't have touched anything the C++ build cares about.

- [ ] **Step 7: Commit**

```bash
git commit -m "chore: remove Python project (superseded by C++/Qt Harbor)"
```

- [ ] **Step 8: Push and verify CI again**

```bash
git push
sleep 5
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId
gh run watch <id>
gh run view <id>
```

Expected: all 4 jobs green. If anything depends on the old Python files (shouldn't, but…), fix and re-push.

---

## Task 12: Merge cpp-qt-rewrite to master

The final step. The branch has been a long-running rewrite — merging it is a substantial diff but the history is clean and the CI is green.

Approach: a regular `git merge` (no squash, no rebase) so the entire Phase 1–5 history is preserved on master and bisectable. The rewrite branch becomes part of master's history, and master picks up the deletion of the old Python tree as one of its commits.

**Pre-merge sanity:**

- [ ] **Step 1: Confirm branch state**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git status                # working tree clean
git log --oneline master..cpp-qt-rewrite | wc -l   # how many commits we're merging
```

- [ ] **Step 2: Switch to master and pull**

This part is done from the main checkout, NOT the worktree:

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool
git checkout master
git pull origin master
```

- [ ] **Step 3: Merge with --no-ff so the merge commit is explicit**

```bash
git merge --no-ff cpp-qt-rewrite -m "Merge cpp-qt-rewrite: replace Python with C++/Qt Harbor

This merges the Phase 1-5 C++/Qt rewrite of the FIM Config Tool, now
named Harbor. The Python project under src/ has been removed; the new
application lives entirely under cpp/.

Beta-quality, macOS distribution via cpp/scripts/package-macos.sh."
```

- [ ] **Step 4: Push master**

```bash
git push origin master
```

- [ ] **Step 5: Verify CI on master**

```bash
sleep 5
gh run list --branch master --limit 1 --json databaseId
gh run watch <id>
gh run view <id>
```

Expected: green on master.

- [ ] **Step 6: Tag the beta**

```bash
git tag -a v0.1.0-beta -m "Harbor v0.1.0 beta — first feature-complete C++/Qt release"
git push origin v0.1.0-beta
```

- [ ] **Step 7: Cleanup the worktree**

The `cpp-qt-rewrite` branch has served its purpose. From the main checkout:

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool
git worktree remove .worktrees/cpp-qt-rewrite
git branch -d cpp-qt-rewrite             # local
git push origin --delete cpp-qt-rewrite  # remote — only if you're sure
```

Note: the `--delete` push of `cpp-qt-rewrite` is destructive. Do it only after confirming the merge is fully visible on origin/master and you're not surprised by anything in the history.

---

## Phase 5 done when:

1. ✅ Launcher home screen has equal-width columns and the window has a sane minimum size
2. ✅ Generation writes to a hidden preview cache (always 2048-sample) AND to the user-chosen export directory
3. ✅ Export directory is configurable via File → Choose output directory…, persisted across launches
4. ✅ Target hardware is selectable via Export → Target hardware (Four Seas / Waveedit), persisted across launches
5. ✅ Waveedit target writes 256-sample-per-cycle banks via libsamplerate decimation; preview still works because it loads from the always-2048 cache
6. ✅ Help → Harbor Help… renders embedded HTML in a QTextBrowser dialog
7. ✅ Help → About Harbor… shows version, license, attributions
8. ✅ macOS `.dmg` includes a "Read Me First.rtf" with install + xattr instructions
9. ✅ All 80 Catch2 tests pass locally and on CI
10. ✅ Python project deleted; top-level README.md and CLAUDE.md replaced with C++-focused content
11. ✅ `cpp-qt-rewrite` merged into `master` with full history preserved
12. ✅ Tagged `v0.1.0-beta` and pushed to origin

After Phase 5 ships, Harbor is feature-complete vs the Python tool, distributed as a tester-ready `.dmg`, and the repository is C++-only on `master`.
