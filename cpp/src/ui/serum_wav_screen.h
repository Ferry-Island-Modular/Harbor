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
    SerumWavScreen(fim::engine::RealtimeAudioEngine* engine, fim::app::Settings* settings,
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
    fim::app::SerumWavService* service_ = nullptr;
    AxisMorphSelector* y_selector_ = nullptr;
    AxisMorphSelector* z_selector_ = nullptr;
};

}  // namespace fim::ui
