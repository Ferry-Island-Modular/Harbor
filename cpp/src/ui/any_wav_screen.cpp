#include "ui/any_wav_screen.h"

#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include "app/services/single_wav_service.h"
#include "app/settings.h"
#include "dsp/spectral_modifier.h"
#include "ui/widgets/axis_morph_selector.h"
#include "ui/widgets/file_drop_widget.h"

namespace fim::ui {

namespace {

QString SingleWavOutputDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("audio_resynth");
}

fim::dsp::YMode YModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::YMode::kFormant;
        case 2:
            return fim::dsp::YMode::kStretch;
        case 3:
            return fim::dsp::YMode::kSmear;
        case 0:
        default:
            return fim::dsp::YMode::kTilt;
    }
}

fim::dsp::ZMode ZModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::ZMode::kDisperse;
        case 2:
            return fim::dsp::ZMode::kCrush;
        case 0:
        default:
            return fim::dsp::ZMode::kRandom;
    }
}

}  // namespace

AnyWavScreen::AnyWavScreen(fim::engine::RealtimeAudioEngine* engine, fim::app::Settings* settings,
                           QWidget* parent)
    : ModeScreenBase(engine, settings, parent) {
    setObjectName("anyWavScreen");

    // Construct the service BEFORE calling FinishInit — the base class's
    // FinishInit queries Service() and wires its signals.
    service_ = new fim::app::SingleWavService(this);
    service_->SetOutputDirectory(SingleWavOutputDir());
    service_->SetYMode(YModeFromIndex(settings->YMorph()));
    service_->SetZMode(ZModeFromIndex(settings->ZMorph()));

    FinishInit();
}

QString AnyWavScreen::ModeTitle() const {
    return "Use any .wav file to create your wavetable bank";
}

QWidget* AnyWavScreen::BuildEmptyPageContent(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("anyWavInnerCard");
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(32, 32, 32, 32);

    auto* drop = new FileDropWidget(card);
    connect(drop, &FileDropWidget::fileDropped, this,
            [this](const QString& path) { OnFileChosen(path); });
    card_layout->addWidget(drop);

    return card;
}

QWidget* AnyWavScreen::BuildFileSetPageContent(QWidget* parent) {
    auto* content = new QWidget(parent);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // X axis (fixed descriptor)
    auto* x_label = new QLabel("X axis", content);
    x_label->setObjectName("anyWavAxisLabel");
    layout->addWidget(x_label);
    auto* x_descriptor = new QLabel("Scans the wave", content);
    x_descriptor->setObjectName("anyWavAxisDescriptor");
    layout->addWidget(x_descriptor);

    // Y axis selector with 4 morph modes.
    const QStringList y_options{"Tilt", "Formant", "Stretch", "Smear"};
    y_selector_ = new AxisMorphSelector("Y axis", y_options, content);
    y_selector_->SetCurrentIndex(settings()->YMorph());
    layout->addWidget(y_selector_);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnYModeChanged);

    // Z axis selector with 3 morph modes.
    const QStringList z_options{"Random", "Disperse", "Crush"};
    z_selector_ = new AxisMorphSelector("Z axis", z_options, content);
    z_selector_->SetCurrentIndex(settings()->ZMorph());
    layout->addWidget(z_selector_);
    connect(z_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnZModeChanged);

    auto* generate_row = new QHBoxLayout();
    auto* generate_button = new QPushButton("Generate wavetable bank", content);
    generate_button->setObjectName("generateButton");
    generate_row->addStretch();
    generate_row->addWidget(generate_button);
    layout->addLayout(generate_row);
    connect(generate_button, &QPushButton::clicked, this, [this]() { OnGenerateClicked(); });

    return content;
}

fim::app::GenerateServiceBase* AnyWavScreen::Service() {
    return service_;
}

void AnyWavScreen::OnClearHook() {
    if (filename_label()) {
        filename_label()->setText("(no file)");
    }
}

void AnyWavScreen::OnResetHook() {
    if (y_selector_ != nullptr) {
        y_selector_->SetCurrentIndex(settings()->YMorph());
    }
    if (z_selector_ != nullptr) {
        z_selector_->SetCurrentIndex(settings()->ZMorph());
    }
}

QString AnyWavScreen::OutputDirForPreview() const {
    return SingleWavOutputDir();
}

void AnyWavScreen::OnYModeChanged(int index) {
    service_->SetYMode(YModeFromIndex(index));
    settings()->SetYMorph(index);
}

void AnyWavScreen::OnZModeChanged(int index) {
    service_->SetZMode(ZModeFromIndex(index));
    settings()->SetZMorph(index);
}

}  // namespace fim::ui
