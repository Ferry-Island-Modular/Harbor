#include "ui/preview_window.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "engine/realtime_audio_engine.h"

namespace fim::ui {

namespace {

constexpr int kPositionSliderMax = 699;  // 0.00..6.99 with /100 scaling
constexpr int kMidiNoteMin = 36;         // C2
constexpr int kMidiNoteMax = 96;         // C7
constexpr int kMidiNoteDefault = 60;     // C4
constexpr int kVolumeMax = 100;
constexpr int kVolumeDefault = 60;  // 60% to start (gentle)

QSlider* MakeHorizontalSlider(int min, int max, int initial) {
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(initial);
    return s;
}

}  // namespace

PreviewWindow::PreviewWindow(QWidget* parent)
    : QMainWindow(parent),
      engine_(std::make_unique<fim::engine::RealtimeAudioEngine>(48000.0f, 512)) {
    setWindowTitle("FIM Config Tool — Phase 1 Preview");
    resize(560, 380);

    auto* central = new QWidget(this);
    auto* root_layout = new QVBoxLayout(central);

    // ---- Bank loader ----
    auto* bank_row = new QHBoxLayout();
    load_button_ = new QPushButton("Load Bank…");
    bank_row->addWidget(load_button_);
    bank_row->addStretch();
    root_layout->addLayout(bank_row);

    // ---- Position sliders ----
    auto* pos_box = new QGroupBox("Wavetable Position");
    auto* pos_layout = new QVBoxLayout(pos_box);
    auto add_slider_row = [&](const QString& label, QSlider*& s) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        s = MakeHorizontalSlider(0, kPositionSliderMax, 0);
        row->addWidget(s);
        pos_layout->addLayout(row);
    };
    add_slider_row("X", x_slider_);
    add_slider_row("Y", y_slider_);
    add_slider_row("Z", z_slider_);
    root_layout->addWidget(pos_box);

    // ---- Pitch + volume ----
    auto* pv_box = new QGroupBox("Playback");
    auto* pv_layout = new QVBoxLayout(pv_box);

    auto* pitch_row = new QHBoxLayout();
    pitch_row->addWidget(new QLabel("Pitch (MIDI)"));
    pitch_slider_ = MakeHorizontalSlider(kMidiNoteMin, kMidiNoteMax, kMidiNoteDefault);
    pitch_row->addWidget(pitch_slider_);
    pv_layout->addLayout(pitch_row);

    auto* vol_row = new QHBoxLayout();
    vol_row->addWidget(new QLabel("Volume"));
    volume_slider_ = MakeHorizontalSlider(0, kVolumeMax, kVolumeDefault);
    vol_row->addWidget(volume_slider_);
    pv_layout->addLayout(vol_row);

    auto* play_row = new QHBoxLayout();
    play_button_ = new QPushButton("Play");
    play_row->addWidget(play_button_);
    play_row->addStretch();
    pv_layout->addLayout(play_row);

    root_layout->addWidget(pv_box);

    // ---- Status ----
    status_label_ = new QLabel("No bank loaded.");
    root_layout->addWidget(status_label_);

    setCentralWidget(central);

    // ---- Wiring ----
    connect(load_button_, &QPushButton::clicked, this, &PreviewWindow::OnLoadBankClicked);
    connect(play_button_, &QPushButton::clicked, this, &PreviewWindow::OnPlayStopClicked);
    connect(x_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnXChanged);
    connect(y_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnYChanged);
    connect(z_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnZChanged);
    connect(pitch_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnPitchChanged);
    connect(volume_slider_, &QSlider::valueChanged, this, &PreviewWindow::OnVolumeChanged);

    // Initialize engine with current slider values
    engine_->SetVolume(static_cast<float>(kVolumeDefault) / kVolumeMax);
    engine_->SetMidiNote(kMidiNoteDefault);

    // Disable playback controls until a bank is loaded
    SetControlsEnabled(false);
}

PreviewWindow::~PreviewWindow() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
    }
}

void PreviewWindow::OnLoadBankClicked() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select Wavetable Bank Directory", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return;
    }
    if (engine_->LoadBank(dir.toStdString())) {
        UpdateStatusLabel(QString("Loaded: %1").arg(dir));
        SetControlsEnabled(true);
    } else {
        UpdateStatusLabel(QString("Failed to load bank from: %1").arg(dir));
    }
}

void PreviewWindow::OnPlayStopClicked() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
        play_button_->setText("Play");
    } else {
        if (engine_->Start()) {
            play_button_->setText("Stop");
        } else {
            UpdateStatusLabel("Failed to start audio device.");
        }
    }
}

void PreviewWindow::OnXChanged(int value) {
    engine_->SetX(static_cast<float>(value) / 100.0f);
}

void PreviewWindow::OnYChanged(int value) {
    engine_->SetY(static_cast<float>(value) / 100.0f);
}

void PreviewWindow::OnZChanged(int value) {
    engine_->SetZ(static_cast<float>(value) / 100.0f);
}

void PreviewWindow::OnPitchChanged(int value) {
    engine_->SetMidiNote(value);
}

void PreviewWindow::OnVolumeChanged(int value) {
    engine_->SetVolume(static_cast<float>(value) / kVolumeMax);
}

void PreviewWindow::UpdateStatusLabel(const QString& text) {
    status_label_->setText(text);
}

void PreviewWindow::SetControlsEnabled(bool enabled) {
    x_slider_->setEnabled(enabled);
    y_slider_->setEnabled(enabled);
    z_slider_->setEnabled(enabled);
    pitch_slider_->setEnabled(enabled);
    volume_slider_->setEnabled(enabled);
    play_button_->setEnabled(enabled);
}

}  // namespace fim::ui
