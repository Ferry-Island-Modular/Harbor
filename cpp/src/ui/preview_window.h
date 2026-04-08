#pragma once

#include <QMainWindow>
#include <memory>

class QLabel;
class QPushButton;
class QSlider;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::ui {

// Phase 1 throwaway preview window. Provides the minimum UI needed to verify
// the realtime audio engine works end-to-end: load a bank, scrub X/Y/Z, set
// pitch, set volume, play/stop. Phase 2 will replace this entirely with the
// production UI from designs/start-state.svg.
class PreviewWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PreviewWindow(QWidget* parent = nullptr);
    ~PreviewWindow() override;

private slots:
    void OnLoadBankClicked();
    void OnPlayStopClicked();
    void OnXChanged(int value);
    void OnYChanged(int value);
    void OnZChanged(int value);
    void OnPitchChanged(int value);
    void OnVolumeChanged(int value);

private:
    void UpdateStatusLabel(const QString& text);
    void SetControlsEnabled(bool enabled);

    std::unique_ptr<fim::engine::RealtimeAudioEngine> engine_;

    QSlider* x_slider_ = nullptr;
    QSlider* y_slider_ = nullptr;
    QSlider* z_slider_ = nullptr;
    QSlider* pitch_slider_ = nullptr;
    QSlider* volume_slider_ = nullptr;
    QPushButton* load_button_ = nullptr;
    QPushButton* play_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

}  // namespace fim::ui
