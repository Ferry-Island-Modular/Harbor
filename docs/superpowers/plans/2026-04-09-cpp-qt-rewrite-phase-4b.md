# Phase 4b Implementation Plan — Serum UI + wiring

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose Serum mode to the user. Build `SerumWavScreen` on top of Phase 4a's `ModeScreenBase`, wire `MainWindow` to route the Serum launcher card to it, and remove the "not yet implemented" `QMessageBox`. This is the first real test of the `ModeScreenBase` abstraction — if the base class design is right, the new screen should be almost entirely mode-specific content (drop widget, Y/Z selectors, service instance), with zero duplication of the state machine, back button, progress bar, or preview controls.

**Architecture:** `SerumWavScreen` is a thin subclass of `ModeScreenBase` following the same pattern as `AnyWavScreen`. It owns a `SerumWavService` instance, builds content pages with a file drop widget (empty state) and 4+4 axis selectors (file-set state), and provides the mode-specific hooks (`ModeTitle`, `OutputDirForPreview`, `Service`, `OnClearHook`, `OnResetHook`). The Y axis selector has 4 buttons labeled "Formant / Phase / Smear / Stretch" — same label list as Z since Python's Serum mode lets both axes pick from the same 4-mode set. `Settings` gains two new integer accessors (`SerumYMorph` / `SerumZMorph`) that persist across sessions, parallel to the existing single-wav `YMorph`/`ZMorph`. `MainWindow` constructs a `SerumWavScreen`, adds it to the router's `QStackedWidget`, and updates `OnModeChosen` to route `LauncherScreen::Mode::kSerum` to the new screen.

**Tech Stack:** Phase 4a's `ModeScreenBase` + `SerumWavService` + `SerumGenerator`, existing `AxisMorphSelector` / `FileDropWidget`, `QSettings` wrapper. No new dependencies.

**Spec deviations:**

- **No Serum-specific designs exist yet.** Per earlier brainstorming, `SerumWavScreen` pattern-matches `AnyWavScreen` exactly. Same 5-state machine (inherited from base), same look and feel, different labels and content. When designs arrive, revisit.
- **Both Y and Z use the same 4-mode list.** Python's `SerumWavetableConverter` has `y_morph_type` and `z_morph_type` each picking independently from a 4-value enum. We mirror that — both axes show `{Formant, Phase, Smear, Stretch}` buttons. Per-axis default: Y defaults to `kFormant` (Python default), Z defaults to `kPhase` (Python default).
- **Label overlap between modes is intentional.** Single-wav has a "Formant" option in its Y axis and a "Smear" option too; Serum also has "Formant" and "Smear". The underlying algorithms are different per mode (bespoke vs Vital-derived, operating on 2D STFT vs 1D single-frame rfft), but the user-facing labels are shared. This is intentional — users learn one vocabulary and don't need to remember "single-wav Formant" vs "Serum Formant". The per-mode behavioral difference is subtle enough that it's treated as a flavor of the same operation.
- **Separate output directory for Serum-generated banks.** Single-wav writes to `${AppLocalDataLocation}/audio_resynth`. Serum writes to `${AppLocalDataLocation}/serum_resynth`. This prevents either mode from overwriting the other's output, so users can have both generated banks on disk and switch between them.
- **X axis descriptor text differs slightly.** Single-wav says "Scans the wave". Serum says "Scans the wavetable frames" — because that's literally what X does in Serum mode (picks between the 8 frequency-domain-interpolated source FFT caches). Still no X morph mode selector; X is a static descriptor.
- **The Serum followup in `docs/followups.md` gets removed.** The current entry reads "Serum mode is fully deferred. The launcher's Serum card opens a QMessageBox saying 'not yet implemented.' Designs are pending; revisit when they land." Phase 4b resolves this — once the card routes to a real working screen, the followup is stale.

**Branch:** `cpp-qt-rewrite` (in worktree at `.worktrees/cpp-qt-rewrite/`)

---

## File map

**New files:**

- `cpp/src/ui/serum_wav_screen.h` / `serum_wav_screen.cpp` — `fim::ui::SerumWavScreen` inheriting `ModeScreenBase`. Same pattern as `AnyWavScreen` but with Serum-specific content: 4 Y buttons ("Formant / Phase / Smear / Stretch"), 4 Z buttons (same list), `SerumWavService` instance, different output directory, different X descriptor.

**Modified files:**

- `cpp/src/app/settings.h` / `settings.cpp` — add `SerumYMorph()` / `SetSerumYMorph(int)` and `SerumZMorph()` / `SetSerumZMorph(int)` accessors with default index 0. New QSettings keys: `"serum_y_morph"` and `"serum_z_morph"`.
- `cpp/tests/settings_test.cpp` — add 2 tests: `SerumYMorph` round-trip, `SerumZMorph` round-trip.
- `cpp/src/ui/main_window.h` — add `SerumWavScreen* serum_wav_screen_ = nullptr;` and `int serum_wav_index_ = -1;` members. Forward-declare `SerumWavScreen` in the `fim::ui` namespace.
- `cpp/src/ui/main_window.cpp` — include `serum_wav_screen.h`, construct a `SerumWavScreen` in the constructor and add it to the stack, wire its `backRequested` signal, update `OnModeChosen` to route `Mode::kSerum` to the new screen (and remove the `QMessageBox`).
- `cpp/CMakeLists.txt` — add `src/ui/serum_wav_screen.cpp` to the `fim-config-tool` executable.
- `docs/followups.md` — remove the stale "Serum mode is fully deferred" entry.

**Deleted files:** none.

---

## Task 1: Settings extension for Serum morph modes

Add two new integer accessor pairs to the `Settings` wrapper, mirroring the existing `YMorph`/`ZMorph` API. Used by `SerumWavScreen` to persist the user's Serum morph mode selections across sessions.

**Files:**
- Modify: `cpp/src/app/settings.h`
- Modify: `cpp/src/app/settings.cpp`
- Modify: `cpp/tests/settings_test.cpp`

- [ ] **Step 1: Add the failing tests**

Append to `cpp/tests/settings_test.cpp`:

```cpp
TEST_CASE_METHOD(SettingsFixture, "Settings round-trips serum_y_morph", "[settings]") {
    fim::app::Settings settings;
    settings.SetSerumYMorph(2);
    REQUIRE(settings.SerumYMorph() == 2);
    settings.SetSerumYMorph(0);
    REQUIRE(settings.SerumYMorph() == 0);
}

TEST_CASE_METHOD(SettingsFixture, "Settings round-trips serum_z_morph", "[settings]") {
    fim::app::Settings settings;
    settings.SetSerumZMorph(3);
    REQUIRE(settings.SerumZMorph() == 3);
    settings.SetSerumZMorph(1);
    REQUIRE(settings.SerumZMorph() == 1);
}

TEST_CASE_METHOD(SettingsFixture, "Settings serum morph defaults are 0 and 1", "[settings]") {
    // Defaults match Python's SerumWavetableConverter:
    //   y_morph_type = MorphType.FORMANT_SCALE  (index 0)
    //   z_morph_type = MorphType.PHASE_DISPERSE (index 1)
    fim::app::Settings settings;
    REQUIRE(settings.SerumYMorph() == 0);
    REQUIRE(settings.SerumZMorph() == 1);
}
```

- [ ] **Step 2: Verify the build fails**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: build error — `Settings::SerumYMorph` / `SetSerumYMorph` / `SerumZMorph` / `SetSerumZMorph` are not declared.

- [ ] **Step 3: Add the declarations to the header**

Modify `cpp/src/app/settings.h`. Add the following four methods inside the `Settings` class's public section, right after the existing `YMorph`/`ZMorph` block:

```cpp
    // ---- serum_y_morph / serum_z_morph ----
    // Index into the Serum mode's 4-option AxisMorphSelector. Default
    // matches Python's SerumWavetableConverter: Y = 0 (FORMANT_SCALE),
    // Z = 1 (PHASE_DISPERSE).
    int SerumYMorph() const;
    void SetSerumYMorph(int index);
    int SerumZMorph() const;
    void SetSerumZMorph(int index);
```

- [ ] **Step 4: Add the implementations**

Modify `cpp/src/app/settings.cpp`. Add two new key constants to the anonymous namespace at the top of the file:

```cpp
constexpr const char* kKeySerumYMorph = "serum_y_morph";
constexpr const char* kKeySerumZMorph = "serum_z_morph";
```

And add the four method implementations at the bottom of the file, before the closing namespace brace:

```cpp
int Settings::SerumYMorph() const {
    return backing_.value(kKeySerumYMorph, 0).toInt();
}

void Settings::SetSerumYMorph(int index) {
    backing_.setValue(kKeySerumYMorph, index);
}

int Settings::SerumZMorph() const {
    return backing_.value(kKeySerumZMorph, 1).toInt();
}

void Settings::SetSerumZMorph(int index) {
    backing_.setValue(kKeySerumZMorph, index);
}
```

- [ ] **Step 5: Build and run tests**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
ctest --test-dir /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build --output-on-failure
```

Expected: 73 tests pass (70 from Phase 4a + 3 new Serum morph settings tests).

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/settings.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/app/settings.cpp \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/tests/settings_test.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/app/settings.h cpp/src/app/settings.cpp cpp/tests/settings_test.cpp
git commit -m "feat(cpp): add SerumYMorph/SerumZMorph to Settings"
```

---

## Task 2: SerumWavScreen

The new mode screen. Thin subclass of `ModeScreenBase`. Owns a `SerumWavService`, builds a file drop widget (empty page) and 4+4 axis selectors (file-set page), and provides the 7 required protected virtual hooks. Follows the `AnyWavScreen` pattern closely — this is deliberate and a good test of whether the base class abstraction pays off.

**Files:**
- Create: `cpp/src/ui/serum_wav_screen.h`
- Create: `cpp/src/ui/serum_wav_screen.cpp`
- Modify: `cpp/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `cpp/src/ui/serum_wav_screen.h`:

```cpp
#pragma once

#include <QString>

#include "ui/mode_screen_base.h"

namespace fim::app {
class SerumWavService;
}

namespace fim::ui {

class AxisMorphSelector;

// The Serum mode screen. Uses a SerumWavService to run the Serum DSP
// pipeline (SerumLoader → SerumMorpher → SerumGenerator). Provides the
// mode-specific content (file drop widget, 4+4 axis selectors) via the
// ModeScreenBase hooks; the base class handles the 5-state machine,
// back button, progress bar, preview controls, and export.
class SerumWavScreen : public ModeScreenBase {
    Q_OBJECT

public:
    SerumWavScreen(fim::engine::RealtimeAudioEngine* engine,
                   fim::app::Settings* settings, QWidget* parent = nullptr);

protected:
    QString ModeTitle() const override;
    QWidget* BuildEmptyPageContent(QWidget* parent) override;
    QWidget* BuildFileSetPageContent(QWidget* parent) override;
    fim::app::GenerateServiceBase* Service() override;
    void OnClearHook() override;
    void OnResetHook() override;
    QString OutputDirForPreview() const override;

private slots:
    void OnYModeChanged(int index);
    void OnZModeChanged(int index);

private:
    fim::app::SerumWavService* service_ = nullptr;
    AxisMorphSelector* y_selector_ = nullptr;
    AxisMorphSelector* z_selector_ = nullptr;
};

}  // namespace fim::ui
```

- [ ] **Step 2: Write the implementation**

Create `cpp/src/ui/serum_wav_screen.cpp`:

```cpp
#include "ui/serum_wav_screen.h"

#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include "app/services/serum_wav_service.h"
#include "app/settings.h"
#include "dsp/serum_morpher.h"
#include "ui/widgets/axis_morph_selector.h"
#include "ui/widgets/file_drop_widget.h"

namespace fim::ui {

namespace {

QString SerumOutputDir() {
    // Separate from single-wav's audio_resynth dir so the two modes
    // don't overwrite each other's output. Users can have both banks
    // on disk simultaneously.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("serum_resynth");
}

// Map an AxisMorphSelector button index (0..3) to the corresponding
// SerumMode enum value. The label order in the QStringList below must
// match this mapping.
fim::dsp::SerumMode SerumModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::SerumMode::kPhase;
        case 2:
            return fim::dsp::SerumMode::kSmear;
        case 3:
            return fim::dsp::SerumMode::kStretch;
        case 0:
        default:
            return fim::dsp::SerumMode::kFormant;
    }
}

}  // namespace

SerumWavScreen::SerumWavScreen(fim::engine::RealtimeAudioEngine* engine,
                               fim::app::Settings* settings, QWidget* parent)
    : ModeScreenBase(engine, settings, parent) {
    setObjectName("serumWavScreen");

    // Construct the service BEFORE calling FinishInit — the base class's
    // FinishInit queries Service() and wires its signals.
    service_ = new fim::app::SerumWavService(this);
    service_->SetOutputDirectory(SerumOutputDir());
    service_->SetYMode(SerumModeFromIndex(settings->SerumYMorph()));
    service_->SetZMode(SerumModeFromIndex(settings->SerumZMorph()));

    FinishInit();
}

QString SerumWavScreen::ModeTitle() const {
    return "Use a Serum .wav file to create your wavetable bank";
}

QWidget* SerumWavScreen::BuildEmptyPageContent(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("anyWavInnerCard");  // reuse any-wav card styling
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(32, 32, 32, 32);

    auto* drop = new FileDropWidget(card);
    connect(drop, &FileDropWidget::fileDropped, this,
            [this](const QString& path) { OnFileChosen(path); });
    card_layout->addWidget(drop);

    return card;
}

QWidget* SerumWavScreen::BuildFileSetPageContent(QWidget* parent) {
    auto* content = new QWidget(parent);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // X axis (fixed descriptor — Serum's X scans the frames, not time).
    auto* x_label = new QLabel("X axis", content);
    x_label->setObjectName("anyWavAxisLabel");
    layout->addWidget(x_label);
    auto* x_descriptor = new QLabel("Scans the wavetable frames", content);
    x_descriptor->setObjectName("anyWavAxisDescriptor");
    layout->addWidget(x_descriptor);

    // Y axis selector with 4 Serum morph modes. Order must match
    // SerumModeFromIndex() above.
    const QStringList mode_options{"Formant", "Phase", "Smear", "Stretch"};

    y_selector_ = new AxisMorphSelector("Y axis", mode_options, content);
    y_selector_->SetCurrentIndex(settings()->SerumYMorph());
    layout->addWidget(y_selector_);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &SerumWavScreen::OnYModeChanged);

    // Z axis selector with the same 4 Serum morph modes.
    z_selector_ = new AxisMorphSelector("Z axis", mode_options, content);
    z_selector_->SetCurrentIndex(settings()->SerumZMorph());
    layout->addWidget(z_selector_);
    connect(z_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &SerumWavScreen::OnZModeChanged);

    auto* generate_row = new QHBoxLayout();
    auto* generate_button = new QPushButton("Generate wavetable bank", content);
    generate_button->setObjectName("generateButton");
    generate_row->addStretch();
    generate_row->addWidget(generate_button);
    layout->addLayout(generate_row);
    connect(generate_button, &QPushButton::clicked, this,
            [this]() { OnGenerateClicked(); });

    return content;
}

fim::app::GenerateServiceBase* SerumWavScreen::Service() {
    return service_;
}

void SerumWavScreen::OnClearHook() {
    if (filename_label()) {
        filename_label()->setText("(no file)");
    }
}

void SerumWavScreen::OnResetHook() {
    if (y_selector_ != nullptr) {
        y_selector_->SetCurrentIndex(settings()->SerumYMorph());
    }
    if (z_selector_ != nullptr) {
        z_selector_->SetCurrentIndex(settings()->SerumZMorph());
    }
}

QString SerumWavScreen::OutputDirForPreview() const {
    return SerumOutputDir();
}

void SerumWavScreen::OnYModeChanged(int index) {
    service_->SetYMode(SerumModeFromIndex(index));
    settings()->SetSerumYMorph(index);
}

void SerumWavScreen::OnZModeChanged(int index) {
    service_->SetZMode(SerumModeFromIndex(index));
    settings()->SetSerumZMorph(index);
}

}  // namespace fim::ui
```

- [ ] **Step 3: Add the source to the main executable**

Modify `cpp/CMakeLists.txt`. Add `src/ui/serum_wav_screen.cpp` to the `qt_add_executable(fim-config-tool ...)` block. It should come right after `src/ui/any_wav_screen.cpp`:

```cmake
    src/ui/mode_screen_base.cpp
    src/ui/any_wav_screen.cpp
    src/ui/serum_wav_screen.cpp
```

- [ ] **Step 4: Build to verify it compiles**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. The screen is now compiled but not yet reachable from the UI — that's Task 3.

- [ ] **Step 5: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/serum_wav_screen.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/serum_wav_screen.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/serum_wav_screen.h cpp/src/ui/serum_wav_screen.cpp cpp/CMakeLists.txt
git commit -m "feat(cpp): add SerumWavScreen inheriting ModeScreenBase"
```

---

## Task 3: MainWindow routing + followups cleanup + manual verification

Wire `MainWindow` to route `LauncherScreen::Mode::kSerum` to the new `SerumWavScreen`, add it to the `QStackedWidget`, and remove the "not yet implemented" `QMessageBox`. Also remove the stale followups entry. End with manual end-to-end verification using a real Serum WAV file.

**Files:**
- Modify: `cpp/src/ui/main_window.h`
- Modify: `cpp/src/ui/main_window.cpp`
- Modify: `docs/followups.md`

- [ ] **Step 1: Update MainWindow header**

Modify `cpp/src/ui/main_window.h`. Forward-declare `SerumWavScreen` and add the two new members:

```cpp
namespace fim::ui {

class AnyWavScreen;
class LauncherScreen;
class SerumWavScreen;
```

And inside the `MainWindow` class's private section, add:

```cpp
    SerumWavScreen* serum_wav_screen_ = nullptr;
    int serum_wav_index_ = -1;
```

- [ ] **Step 2: Update MainWindow implementation**

Modify `cpp/src/ui/main_window.cpp`. Add the include at the top:

```cpp
#include "ui/serum_wav_screen.h"
```

In the constructor, after creating the any-wav screen, construct the Serum screen and add it to the stack:

```cpp
    launcher_screen_ = new LauncherScreen(this);
    any_wav_screen_ = new AnyWavScreen(engine_.get(), &settings_, this);
    serum_wav_screen_ = new SerumWavScreen(engine_.get(), &settings_, this);

    launcher_index_ = stack_->addWidget(launcher_screen_);
    any_wav_index_ = stack_->addWidget(any_wav_screen_);
    serum_wav_index_ = stack_->addWidget(serum_wav_screen_);
```

Wire the Serum screen's `backRequested` signal right after the any-wav screen's:

```cpp
    connect(any_wav_screen_, &AnyWavScreen::backRequested, this,
            &MainWindow::OnBackToLauncher);
    connect(serum_wav_screen_, &SerumWavScreen::backRequested, this,
            &MainWindow::OnBackToLauncher);
```

And update `OnModeChosen` to route `kSerum` to the new screen, removing the `QMessageBox`:

```cpp
void MainWindow::OnModeChosen(int mode) {
    const auto m = static_cast<LauncherScreen::Mode>(mode);
    if (m == LauncherScreen::Mode::kAnyWav) {
        any_wav_screen_->Reset();
        stack_->setCurrentIndex(any_wav_index_);
    } else if (m == LauncherScreen::Mode::kSerum) {
        serum_wav_screen_->Reset();
        stack_->setCurrentIndex(serum_wav_index_);
    }
    // kThreeWavs is permanently disabled in the launcher.
}
```

Remove the `#include <QMessageBox>` line at the top of `main_window.cpp` if it's no longer used by anything else in the file. (It was only used for the deprecated "not implemented" dialog.)

- [ ] **Step 3: Remove the stale followups entry**

Modify `docs/followups.md`. Delete the line:

```markdown
- **Serum mode is fully deferred.** The launcher's Serum card opens a `QMessageBox` saying "not yet implemented." Designs are pending; revisit when they land.
```

Keep the line immediately after it:

```markdown
- **Three-wavs mode is permanently `kComingSoon`.** Will be revisited only if there's user demand.
```

- [ ] **Step 4: Build**

```bash
cmake --build /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build
```

Expected: builds clean. If there's a warning about `QMessageBox` being included but unused, remove the include from `main_window.cpp`.

- [ ] **Step 5: Run the app and verify end-to-end**

```bash
open /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/build/fim-config-tool.app
```

Walk through the Serum mode flow:

1. Launcher appears. The Serum card is clickable (not "Coming soon!").
2. Click "Choose" on the Serum card. App transitions to the new `SerumWavScreen` in kEmpty state. Verify:
   - "← Back" button at the top
   - Title: "Use a Serum .wav file to create your wavetable bank"
   - Dashed-border file drop area
3. Drop a real Serum-format WAV file (or click the drop area to browse). State transitions to kFileSet. Verify:
   - Filename displayed
   - "Clear" button
   - X axis label + "Scans the wavetable frames" descriptor
   - Y axis selector with four buttons: Formant / Phase / Smear / Stretch
   - Z axis selector with the same four buttons
   - "Generate wavetable bank" button
4. Pick some non-default combo (e.g. Y=Smear, Z=Stretch).
5. Click "Generate wavetable bank". Progress bar fills. "Done!" appears briefly, then the preview panel.
6. Click "Play steady tone". **Listen for audible Serum resynthesis output** — not placeholder sines, not silence. The sound should reflect the chosen Y/Z morph modes. Drag the Z slider through its range — different pages should sound different.
7. Try a few Y/Z combinations by going back and re-entering:
   - Back to launcher (← Back)
   - Re-enter Serum mode
   - Verify the Y/Z selections are remembered (Settings persistence check)
8. Export: click "Export wavetable bank" → pick a directory → verify 8 WAV files appear.
9. Verify that switching between any-wav and Serum modes does not cross-contaminate output: generate in one, switch to the other, the preview should load the appropriate mode's output when you generate there.

If any step fails, the most likely culprits in order:
- `SerumLoader` rejecting a valid file (e.g. stereo file splitting wrong — check the WavLoader's mono mixdown)
- `SerumMorpher` producing NaN or infinite values (check the `(i+0.25)/i` scaling at i=1 especially)
- Output directory conflict (both modes writing to the same path — check `OutputDirForPreview`)
- Settings persistence not working (keys not being written or read correctly)

- [ ] **Step 6: Format and commit**

```bash
clang-format -i \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.h \
  /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite/cpp/src/ui/main_window.cpp
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git add cpp/src/ui/main_window.h cpp/src/ui/main_window.cpp docs/followups.md
git commit -m "feat(cpp): route Serum launcher card to new SerumWavScreen"
```

---

## Task 4: Push and verify CI

Push the branch and verify explicitly with `gh run view`. Per the Phase 3a/3b/3d/4a lessons, NEVER trust the background `gh run watch` exit code alone.

- [ ] **Step 1: Push the branch**

```bash
cd /Users/jgoney/dev/ferry-island-modular/fim-config-tool/.worktrees/cpp-qt-rewrite
git push
```

- [ ] **Step 2: Watch CI**

```bash
sleep 5
gh run list --branch cpp-qt-rewrite --limit 1 --json databaseId,status --jq '.[0]'
# copy the returned id and pass it to:
gh run watch <id> --exit-status
```

- [ ] **Step 3: VERIFY CI status explicitly**

```bash
gh run view <id>
```

Expected: all four jobs ✓ — `clang-format check`, `Build - macos-latest`, `Build - ubuntu-latest`, `Build - windows-latest`. Phase 4b is done when all green.

---

## Phase 4b done when:

1. ✅ `Settings` has `SerumYMorph` / `SerumZMorph` accessors that round-trip correctly
2. ✅ `fim::ui::SerumWavScreen` exists and inherits `ModeScreenBase`
3. ✅ `MainWindow` routes `LauncherScreen::Mode::kSerum` to the new screen (no more QMessageBox)
4. ✅ Manual verification: dropping a real Serum WAV file, picking Y/Z modes, generating, and playing produces audible Serum resynthesis output
5. ✅ Y/Z mode selections persist across screen exits and app restarts
6. ✅ Single-wav and Serum use separate output directories
7. ✅ `docs/followups.md` no longer mentions "Serum mode is fully deferred"
8. ✅ All 73 Catch2 tests pass locally
9. ✅ CI green on macOS, Ubuntu, Windows, and clang-format

After Phase 4b ships, both major single-file wavetable modes are feature-complete. Three-wav mode remains permanently `kComingSoon` until there's user demand. Natural next phases: styling pass (iterate the QSS against the designs), Phase 3c (Python oracle harness for regression safety), or a break.
