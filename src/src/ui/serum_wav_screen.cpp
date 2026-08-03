#include "ui/serum_wav_screen.h"

#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>

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
fim::dsp::SerumMode SerumYModeFromIndex(int index) {
    switch (index) {
        case 1:
            return fim::dsp::SerumMode::kSmear;
        case 2:
            return fim::dsp::SerumMode::kStretch;
        case 0:
        default:
            return fim::dsp::SerumMode::kFormant;
    }
}

fim::dsp::SerumMode SerumZModeFromIndex(int index) {
    switch (index) {
        case 0:
            return fim::dsp::SerumMode::kOddEven;
        case 2:
            return fim::dsp::SerumMode::kCrush;
        case 1:
        default:
            return fim::dsp::SerumMode::kPhase;
    }
}

int ValidModeIndex(int index) {
    return std::clamp(index, 0, 2);
}

}  // namespace

SerumWavScreen::SerumWavScreen(fim::engine::RealtimeAudioEngine* engine,
                               fim::app::Settings* settings, QWidget* parent)
    : ModeScreenBase(engine, settings, parent) {
    setObjectName("serumWavScreen");

    // Construct the service BEFORE calling FinishInit — the base class's
    // FinishInit queries Service() and wires its signals.
    service_ = new fim::app::SerumWavService(this);
    service_->SetPreviewCacheDirectory(SerumOutputDir());
    const QString user_dir =
        QDir(QString::fromStdString(settings->OutputDir())).filePath("serum_wav");
    service_->SetOutputDirectory(user_dir);
    service_->SetSamplesPerFrame(settings->SamplesPerFrame());
    service_->SetYMode(SerumYModeFromIndex(ValidModeIndex(settings->SerumYMorph())));
    service_->SetZMode(SerumZModeFromIndex(ValidModeIndex(settings->SerumZMorph())));

    FinishInit();
}

void SerumWavScreen::RefreshOutputDirFromSettings() {
    const QString user_dir =
        QDir(QString::fromStdString(settings()->OutputDir())).filePath("serum_wav");
    service_->SetOutputDirectory(user_dir);
    service_->SetSamplesPerFrame(settings()->SamplesPerFrame());
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

    // Axis section — the design's X | Y | Z columns in one card.
    auto* axes_widget = new QWidget(content);
    auto* columns_row = new QHBoxLayout(axes_widget);
    columns_row->setContentsMargins(0, 0, 0, 0);
    columns_row->setSpacing(24);

    // X axis (fixed descriptor — Serum's X scans the frames, not time).
    auto* x_column = new QWidget(axes_widget);
    auto* x_block = new QVBoxLayout(x_column);
    x_block->setContentsMargins(0, 0, 0, 0);
    x_block->setSpacing(8);
    auto* x_label = new QLabel("X axis", x_column);
    x_label->setObjectName("anyWavAxisLabel");
    x_block->addWidget(x_label);
    auto* x_descriptor = new QLabel("Scans the wavetable frames", x_column);
    x_descriptor->setObjectName("anyWavAxisDescriptor");
    x_descriptor->setWordWrap(true);
    x_block->addWidget(x_descriptor);
    x_block->addStretch();
    columns_row->addWidget(x_column, /*stretch=*/1);

    const QStringList y_options{"Formant", "Smear", "Stretch"};
    const QStringList z_options{"Odd / even", "Phase motion", "Crush"};

    y_selector_ = new AxisMorphSelector("Y axis — spectral color", y_options, axes_widget);
    y_selector_->SetCurrentIndex(ValidModeIndex(settings()->SerumYMorph()));
    columns_row->addWidget(y_selector_, /*stretch=*/1, Qt::AlignTop);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &SerumWavScreen::OnYModeChanged);

    z_selector_ = new AxisMorphSelector("Z axis — texture", z_options, axes_widget);
    z_selector_->SetCurrentIndex(ValidModeIndex(settings()->SerumZMorph()));
    columns_row->addWidget(z_selector_, /*stretch=*/1, Qt::AlignTop);
    connect(z_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &SerumWavScreen::OnZModeChanged);

    layout->addWidget(MakeSectionCard(axes_widget, content));

    // Action section — the design anchors the Generate button left.
    auto* action_widget = new QWidget(content);
    auto* generate_row = new QHBoxLayout(action_widget);
    generate_row->setContentsMargins(0, 0, 0, 0);
    auto* generate_button = new QPushButton("Generate wavetable bank", action_widget);
    generate_button->setObjectName("generateButton");
    generate_row->addWidget(generate_button);
    generate_row->addStretch();
    layout->addWidget(MakeSectionCard(action_widget, content));
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
        y_selector_->SetCurrentIndex(ValidModeIndex(settings()->SerumYMorph()));
    }
    if (z_selector_ != nullptr) {
        z_selector_->SetCurrentIndex(ValidModeIndex(settings()->SerumZMorph()));
    }
}

QString SerumWavScreen::OutputDirForPreview() const {
    return SerumOutputDir();
}

void SerumWavScreen::OnYModeChanged(int index) {
    service_->SetYMode(SerumYModeFromIndex(index));
    settings()->SetSerumYMorph(index);
}

void SerumWavScreen::OnZModeChanged(int index) {
    service_->SetZMode(SerumZModeFromIndex(index));
    settings()->SetSerumZMorph(index);
}

}  // namespace fim::ui
