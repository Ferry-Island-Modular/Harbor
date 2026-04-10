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
