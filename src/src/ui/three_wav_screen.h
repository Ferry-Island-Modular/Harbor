#pragma once

#include <QString>
#include <array>

#include "ui/mode_screen_base.h"

class QLabel;
class QPushButton;

namespace fim::app {
class ThreeWavService;
}

namespace fim::ui {

class FileDropWidget;

// The three-wav mode screen. Takes 3 audio files (one per slot, mapped
// to X / Y / Z axes) and blends their time-averaged magnitude spectra
// via ThreeWavGenerator's axis-weighted cross-synthesis algorithm.
//
// Differs from AnyWavScreen / SerumWavScreen in several ways:
//   - No morph mode selectors (three-wav has no morph modes)
//   - 3 file drop slots instead of 1
//   - ShowDefaultFilenameRow() returns false (per-slot filenames inside
//     the content instead of the base's single-file row)
//   - Reset() override clears slots and stays in kFileSet
//   - Constructor jumps straight to kFileSet (no kEmpty state used)
class ThreeWavScreen : public ModeScreenBase {
    Q_OBJECT

public:
    ThreeWavScreen(fim::engine::RealtimeAudioEngine* engine, fim::app::Settings* settings,
                   QWidget* parent = nullptr);

    // Override Reset to clear all 3 slots and stay in kFileSet.
    void Reset() override;

public slots:
    void RefreshOutputDirFromSettings();

protected:
    QString ModeTitle() const override;
    QWidget* BuildEmptyPageContent(QWidget* parent) override;
    QWidget* BuildFileSetPageContent(QWidget* parent) override;
    fim::app::GenerateServiceBase* Service() override;
    void OnClearHook() override;
    void OnResetHook() override;
    QString OutputDirForPreview() const override;
    bool ShowDefaultFilenameRow() const override { return false; }

private:
    void OnSlotFileDropped(int slot_index, const QString& path);
    void OnSlotClearClicked(int slot_index);
    void RefreshGenerateEnabled();

    fim::app::ThreeWavService* service_ = nullptr;

    std::array<FileDropWidget*, 3> drop_widgets_{nullptr, nullptr, nullptr};
    std::array<QLabel*, 3> filename_labels_{nullptr, nullptr, nullptr};
    QPushButton* generate_button_ = nullptr;
};

}  // namespace fim::ui
