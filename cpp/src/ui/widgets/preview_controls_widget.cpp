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

    auto add_slider_row = [&](const QString& label, QSlider*& s, int min, int max, int initial) {
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
    connect(play_button_, &QPushButton::clicked, this, &PreviewControlsWidget::OnPlayStopClicked);
    connect(x_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnXChanged);
    connect(y_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnYChanged);
    connect(z_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnZChanged);
    connect(pitch_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnPitchChanged);
    connect(volume_slider_, &QSlider::valueChanged, this, &PreviewControlsWidget::OnVolumeChanged);

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
