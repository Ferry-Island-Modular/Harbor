#pragma once

#include <QString>

#include "ui/mode_screen_base.h"

namespace fim::app {
class SingleWavService;
}

namespace fim::ui {

class AxisMorphSelector;

// The "any wav" mode screen. Uses a SingleWavService to run the real
// single-wav DSP pipeline. Provides the mode-specific content (file drop
// widget, Y/Z axis selectors) via the ModeScreenBase hooks; the base
// class handles the 5-state machine, back button, progress bar, preview
// controls, and export.
class AnyWavScreen : public ModeScreenBase {
    Q_OBJECT

public:
    AnyWavScreen(fim::engine::RealtimeAudioEngine* engine, fim::app::Settings* settings,
                 QWidget* parent = nullptr);

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
    fim::app::SingleWavService* service_ = nullptr;
    AxisMorphSelector* y_selector_ = nullptr;
    AxisMorphSelector* z_selector_ = nullptr;
};

}  // namespace fim::ui
