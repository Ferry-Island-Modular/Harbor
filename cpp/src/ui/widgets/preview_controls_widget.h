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
    PreviewControlsWidget(fim::engine::RealtimeAudioEngine* engine, QWidget* parent = nullptr);

    // Re-sync the Play/Stop button label from the engine's current state.
    // Use after stopping the engine externally (e.g. when navigating away or
    // starting a new generation) so the button reflects reality.
    void RefreshPlayButton();

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
