#include "ui/any_wav_screen.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "app/services/single_wav_service.h"
#include "app/settings.h"
#include "dsp/spectral_modifier.h"
#include "engine/realtime_audio_engine.h"
#include "ui/widgets/axis_morph_selector.h"
#include "ui/widgets/custom_progress_bar.h"
#include "ui/widgets/file_drop_widget.h"
#include "ui/widgets/preview_controls_widget.h"

namespace fim::ui {

namespace {

constexpr int kDoneMessageHoldMs = 800;

// Phase 2 stub bank lives in the per-user app data directory so it works
// regardless of how the app was launched (cwd is unreliable on macOS .app
// bundles, where it defaults to "/"). Phase 3 will plumb this through
// Settings::OutputDir() and let the user choose.
QString StubOutputDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("audio_resynth");
}

// Map a selector button index (0..2) to the corresponding enum value.
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

QPushButton* MakeBackButton(QWidget* parent) {
    auto* button = new QPushButton("← Back", parent);
    button->setObjectName("backButton");
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLabel* MakeTitle(QWidget* parent) {
    auto* label = new QLabel("Use any .wav file to create your wavetable bank", parent);
    label->setObjectName("anyWavTitle");
    return label;
}

}  // namespace

AnyWavScreen::AnyWavScreen(fim::engine::RealtimeAudioEngine* engine, fim::app::Settings* settings,
                           QWidget* parent)
    : QWidget(parent), engine_(engine), settings_(settings) {
    setObjectName("anyWavScreen");
    service_ = new fim::app::SingleWavService(this);
    service_->SetOutputDirectory(StubOutputDir());

    // Initialize the service with the last-used modes from settings.
    service_->SetYMode(YModeFromIndex(settings_->YMorph()));
    service_->SetZMode(ZModeFromIndex(settings_->ZMorph()));

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(32, 32, 32, 32);
    root_layout->setSpacing(16);

    stack_ = new QStackedWidget(this);
    empty_page_index_ = stack_->addWidget(BuildEmptyPage());
    file_set_page_index_ = stack_->addWidget(BuildFileSetPage());
    generating_page_index_ = stack_->addWidget(BuildGeneratingPage());
    done_message_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/false));
    done_preview_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/true));
    root_layout->addWidget(stack_);

    connect(service_, &fim::app::SingleWavService::progressChanged, this,
            &AnyWavScreen::OnProgressChanged);
    connect(service_, &fim::app::SingleWavService::generationFinished, this,
            &AnyWavScreen::OnGenerationFinished);
    connect(service_, &fim::app::SingleWavService::generationFailed, this,
            [this](const QString& error) {
                QMessageBox::warning(this, "Generation failed", error);
                SetState(current_file_.isEmpty() ? State::kEmpty : State::kFileSet);
            });

    SetState(State::kEmpty);
}

void AnyWavScreen::Reset() {
    current_file_.clear();
    // Re-sync the selectors from settings (in case they changed elsewhere).
    if (y_selector_ != nullptr) {
        y_selector_->SetCurrentIndex(settings_->YMorph());
    }
    if (z_selector_ != nullptr) {
        z_selector_->SetCurrentIndex(settings_->ZMorph());
    }
    SetState(State::kEmpty);
}

void AnyWavScreen::SetState(State state) {
    switch (state) {
        case State::kEmpty:
            stack_->setCurrentIndex(empty_page_index_);
            break;
        case State::kFileSet:
            stack_->setCurrentIndex(file_set_page_index_);
            break;
        case State::kGenerating:
            stack_->setCurrentIndex(generating_page_index_);
            progress_bar_->SetProgress(0);
            break;
        case State::kDoneMessage:
            stack_->setCurrentIndex(done_message_page_index_);
            // Hold the "Done!" message briefly, then reveal the preview area.
            QTimer::singleShot(kDoneMessageHoldMs, this,
                               [this]() { SetState(State::kDonePreviewAvailable); });
            break;
        case State::kDonePreviewAvailable:
            stack_->setCurrentIndex(done_preview_page_index_);
            // Auto-load the just-generated bank into the engine so the
            // preview widget can play it.
            engine_->LoadBank(StubOutputDir().toStdString());
            break;
    }
}

QWidget* AnyWavScreen::BuildEmptyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    auto* card = new QFrame(page);
    card->setObjectName("anyWavInnerCard");
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(32, 32, 32, 32);

    auto* drop = new FileDropWidget(card);
    connect(drop, &FileDropWidget::fileDropped, this, &AnyWavScreen::OnFileDropped);
    card_layout->addWidget(drop);

    layout->addWidget(card);
    layout->addStretch();
    return page;
}

QWidget* AnyWavScreen::BuildFileSetPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    // Filename row
    auto* file_row = new QHBoxLayout();
    filename_label_ = new QLabel("(no file)", page);
    filename_label_->setObjectName("anyWavFilename");
    auto* clear_button = new QPushButton("Clear", page);
    clear_button->setObjectName("clearButton");
    file_row->addWidget(filename_label_);
    file_row->addStretch();
    file_row->addWidget(clear_button);
    layout->addLayout(file_row);
    connect(clear_button, &QPushButton::clicked, this, &AnyWavScreen::OnClearClicked);

    // X axis (fixed descriptor)
    auto* x_label = new QLabel("X axis", page);
    x_label->setObjectName("anyWavAxisLabel");
    layout->addWidget(x_label);
    auto* x_descriptor = new QLabel("Scans the wave", page);
    x_descriptor->setObjectName("anyWavAxisDescriptor");
    layout->addWidget(x_descriptor);

    // Y axis selector with real morph mode labels.
    const QStringList y_options{"Tilt", "Formant", "Stretch", "Smear"};
    y_selector_ = new AxisMorphSelector("Y axis", y_options, page);
    y_selector_->SetCurrentIndex(settings_->YMorph());
    layout->addWidget(y_selector_);
    connect(y_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnYModeChanged);

    // Z axis selector with real morph mode labels.
    const QStringList z_options{"Random", "Disperse", "Crush"};
    z_selector_ = new AxisMorphSelector("Z axis", z_options, page);
    z_selector_->SetCurrentIndex(settings_->ZMorph());
    layout->addWidget(z_selector_);
    connect(z_selector_, &AxisMorphSelector::currentIndexChanged, this,
            &AnyWavScreen::OnZModeChanged);

    auto* generate_row = new QHBoxLayout();
    auto* generate_button = new QPushButton("Generate wavetable bank", page);
    generate_button->setObjectName("generateButton");
    generate_row->addStretch();
    generate_row->addWidget(generate_button);
    layout->addLayout(generate_row);
    connect(generate_button, &QPushButton::clicked, this, &AnyWavScreen::OnGenerateClicked);

    layout->addStretch();
    return page;
}

QWidget* AnyWavScreen::BuildGeneratingPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    progress_bar_ = new CustomProgressBar(page);
    layout->addWidget(progress_bar_);

    layout->addStretch();
    return page;
}

QWidget* AnyWavScreen::BuildDonePage(bool with_preview) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &AnyWavScreen::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    layout->addWidget(MakeTitle(page));

    if (!with_preview) {
        auto* done_label = new QLabel("Done!", page);
        done_label->setObjectName("anyWavDoneMessage");
        layout->addWidget(done_label);
    } else {
        auto* button_row = new QHBoxLayout();
        auto* generate_button = new QPushButton("Generate wavetable bank", page);
        generate_button->setObjectName("generateButton");
        auto* export_button = new QPushButton("Export wavetable bank", page);
        export_button->setObjectName("exportButton");
        button_row->addWidget(generate_button);
        button_row->addWidget(export_button);
        button_row->addStretch();
        layout->addLayout(button_row);
        connect(generate_button, &QPushButton::clicked, this, &AnyWavScreen::OnGenerateClicked);
        connect(export_button, &QPushButton::clicked, this, &AnyWavScreen::OnExportClicked);

        preview_controls_ = new PreviewControlsWidget(engine_, page);
        layout->addWidget(preview_controls_);
    }

    layout->addStretch();
    return page;
}

void AnyWavScreen::OnFileDropped(const QString& path) {
    current_file_ = path;
    if (filename_label_) {
        filename_label_->setText(QFileInfo(path).fileName());
    }
    service_->SetInputFile(path);
    SetState(State::kFileSet);
}

void AnyWavScreen::OnClearClicked() {
    current_file_.clear();
    SetState(State::kEmpty);
}

void AnyWavScreen::OnGenerateClicked() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
        if (preview_controls_) {
            preview_controls_->RefreshPlayButton();
        }
    }
    SetState(State::kGenerating);
    service_->Generate();
}

void AnyWavScreen::OnExportClicked() {
    const QString dest = QFileDialog::getExistingDirectory(
        this, "Export bank to…", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dest.isEmpty()) {
        return;
    }
    // Copy the 8 files from the stub output dir to `dest`.
    const QString src_dir = StubOutputDir();
    for (int i = 1; i <= 8; ++i) {
        const QString src = QString("%1/%2.wav").arg(src_dir).arg(i);
        const QString dst = QString("%1/%2.wav").arg(dest).arg(i);
        QFile::copy(src, dst);
    }
}

void AnyWavScreen::OnProgressChanged(int percent) {
    if (progress_bar_) {
        progress_bar_->SetProgress(percent);
    }
}

void AnyWavScreen::OnGenerationFinished() {
    SetState(State::kDoneMessage);
}

void AnyWavScreen::OnYModeChanged(int index) {
    service_->SetYMode(YModeFromIndex(index));
    settings_->SetYMorph(index);
}

void AnyWavScreen::OnZModeChanged(int index) {
    service_->SetZMode(ZModeFromIndex(index));
    settings_->SetZMorph(index);
}

}  // namespace fim::ui
