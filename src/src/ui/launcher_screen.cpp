#include "ui/launcher_screen.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "ui/widgets/card_button.h"

namespace fim::ui {

LauncherScreen::LauncherScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("launcherScreen");

    // Design rhythm: 8px title→subtitle, 32px subtitle→cards (base spacing
    // plus the explicit addSpacing below).
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(32, 32, 32, 32);
    root_layout->setSpacing(8);

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

    root_layout->addSpacing(24);

    auto* cards_row = new QHBoxLayout();
    cards_row->setSpacing(16);

    any_wav_card_ = new CardButton("Use any .wav file to create your wavetable bank",
                                   CardButton::State::kDefault, this);
    serum_card_ = new CardButton("Use a Serum .wav file to create your wavetable bank",
                                 CardButton::State::kDefault, this);
    three_wavs_card_ = new CardButton("Use three .wav files to create your wavetable bank",
                                      CardButton::State::kDefault, this);

    cards_row->addWidget(any_wav_card_, /*stretch=*/1);
    cards_row->addWidget(serum_card_, /*stretch=*/1);
    cards_row->addWidget(three_wavs_card_, /*stretch=*/1);
    root_layout->addLayout(cards_row);
    root_layout->addStretch();

    connect(any_wav_card_, &CardButton::chosen, this, [this]() { emit modeChosen(Mode::kAnyWav); });
    connect(serum_card_, &CardButton::chosen, this, [this]() { emit modeChosen(Mode::kSerum); });
    connect(three_wavs_card_, &CardButton::chosen, this,
            [this]() { emit modeChosen(Mode::kThreeWavs); });
}

}  // namespace fim::ui
