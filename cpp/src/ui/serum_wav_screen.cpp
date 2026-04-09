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
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
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

    // Y and Z axis selectors share the same 4-option list. Order must
    // match SerumModeFromIndex() above.
    const QStringList mode_options{"Formant", "Phase", "Smear", "Stretch"};

    y_selector_ = new AxisMorphSelector("Y axis", mode_options, content);
    y_selector_->SetCurrentIndex(settings()->SerumYMorph());
    layout->addWidget(y_selector_);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &SerumWavScreen::OnYModeChanged);

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
    connect(generate_button, &QPushButton::clicked, this, [this]() { OnGenerateClicked(); });

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
