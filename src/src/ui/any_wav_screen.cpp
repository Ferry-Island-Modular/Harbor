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

fim::dsp::SourceMode SourceModeFromIndex(int index) {
    return index == 1 ? fim::dsp::SourceMode::kLegacyStretch : fim::dsp::SourceMode::kFocused;
}

QString SourceModeDescription(int index) {
    return index == 1 ? "Scans the full source while stretching its spectrum"
                      : "Scans spectrally distinct frames from a content-rich region";
}

}  // namespace

AnyWavScreen::AnyWavScreen(fim::engine::RealtimeAudioEngine* engine, fim::app::Settings* settings,
                           QWidget* parent)
    : ModeScreenBase(engine, settings, parent) {
    setObjectName("anyWavScreen");

    // Construct the service BEFORE calling FinishInit — the base class's
    // FinishInit queries Service() and wires its signals.
    service_ = new fim::app::SingleWavService(this);
    service_->SetPreviewCacheDirectory(SingleWavOutputDir());
    const QString user_dir =
        QDir(QString::fromStdString(settings->OutputDir())).filePath("any_wav");
    service_->SetOutputDirectory(user_dir);
    service_->SetSamplesPerFrame(settings->SamplesPerFrame());
    service_->SetSourceMode(SourceModeFromIndex(settings->AnyWavSourceMode()));
    service_->SetYMode(YModeFromIndex(settings->YMorph()));
    service_->SetZMode(ZModeFromIndex(settings->ZMorph()));

    FinishInit();
}

void AnyWavScreen::RefreshOutputDirFromSettings() {
    const QString user_dir =
        QDir(QString::fromStdString(settings()->OutputDir())).filePath("any_wav");
    service_->SetOutputDirectory(user_dir);
    service_->SetSamplesPerFrame(settings()->SamplesPerFrame());
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

    const QStringList source_options{"Focused", "Legacy stretch"};
    source_selector_ = new AxisMorphSelector("Source treatment", source_options, content);
    source_selector_->SetCurrentIndex(settings()->AnyWavSourceMode());
    layout->addWidget(source_selector_);
    connect(source_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnSourceModeChanged);

    // X-axis behavior follows the selected source treatment.
    auto* x_label = new QLabel("X axis", content);
    x_label->setObjectName("anyWavAxisLabel");
    layout->addWidget(x_label);
    x_descriptor_ = new QLabel(SourceModeDescription(settings()->AnyWavSourceMode()), content);
    x_descriptor_->setObjectName("anyWavAxisDescriptor");
    x_descriptor_->setWordWrap(true);
    layout->addWidget(x_descriptor_);

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
    if (source_selector_ != nullptr) {
        source_selector_->SetCurrentIndex(settings()->AnyWavSourceMode());
    }
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

void AnyWavScreen::OnSourceModeChanged(int index) {
    service_->SetSourceMode(SourceModeFromIndex(index));
    settings()->SetAnyWavSourceMode(index);
    if (x_descriptor_ != nullptr) {
        x_descriptor_->setText(SourceModeDescription(index));
    }
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
