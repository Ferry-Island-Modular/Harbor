#include "ui/mode_screen_base.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "app/services/generate_service_base.h"
#include "app/settings.h"
#include "engine/realtime_audio_engine.h"
#include "ui/widgets/custom_progress_bar.h"
#include "ui/widgets/preview_controls_widget.h"

namespace fim::ui {

namespace {

constexpr int kDoneMessageHoldMs = 800;
constexpr int kNumPages = 8;

}  // namespace

ModeScreenBase::ModeScreenBase(fim::engine::RealtimeAudioEngine* engine,
                               fim::app::Settings* settings, QWidget* parent)
    : QWidget(parent), engine_(engine), settings_(settings) {
    // Intentionally minimal — can't call virtual hooks (ModeTitle,
    // BuildEmptyPageContent, Service, etc.) from the base constructor
    // because the vtable hasn't been set up to dispatch to the derived
    // class yet. Subclass calls FinishInit() from its own constructor
    // body after its members are fully constructed.
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(32, 32, 32, 32);
    root_layout->setSpacing(16);

    stack_ = new QStackedWidget(this);
    root_layout->addWidget(stack_);
}

void ModeScreenBase::FinishInit() {
    empty_page_index_ = stack_->addWidget(BuildEmptyPage());
    file_set_page_index_ = stack_->addWidget(BuildFileSetPage());
    generating_page_index_ = stack_->addWidget(BuildGeneratingPage());
    done_message_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/false));
    done_preview_page_index_ = stack_->addWidget(BuildDonePage(/*with_preview=*/true));

    auto* service = Service();
    connect(service, &fim::app::GenerateServiceBase::progressChanged, this,
            &ModeScreenBase::OnProgressChanged);
    connect(service, &fim::app::GenerateServiceBase::generationFinished, this,
            &ModeScreenBase::OnGenerationFinished);
    connect(service, &fim::app::GenerateServiceBase::generationFailed, this,
            [this](const QString& error) {
                QMessageBox::warning(this, "Generation failed", error);
                SetState(current_file_.isEmpty() ? State::kEmpty : State::kFileSet);
            });

    SetState(State::kEmpty);
}

void ModeScreenBase::Reset() {
    current_file_.clear();
    OnResetHook();
    SetState(State::kEmpty);
}

void ModeScreenBase::SetState(State state) {
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
            QTimer::singleShot(kDoneMessageHoldMs, this,
                               [this]() { SetState(State::kDonePreviewAvailable); });
            break;
        case State::kDonePreviewAvailable:
            stack_->setCurrentIndex(done_preview_page_index_);
            engine_->LoadBank(OutputDirForPreview().toStdString());
            break;
    }
}

QPushButton* ModeScreenBase::MakeBackButton(QWidget* parent) {
    auto* button = new QPushButton("← Back", parent);
    button->setObjectName("backButton");
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QWidget* ModeScreenBase::BuildEmptyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

    layout->addWidget(BuildEmptyPageContent(page));
    layout->addStretch();
    return page;
}

QWidget* ModeScreenBase::BuildFileSetPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

    // Filename row — shared between all modes.
    auto* file_row = new QHBoxLayout();
    filename_label_ = new QLabel("(no file)", page);
    filename_label_->setObjectName("anyWavFilename");
    auto* clear_button = new QPushButton("Clear", page);
    clear_button->setObjectName("clearButton");
    file_row->addWidget(filename_label_);
    file_row->addStretch();
    file_row->addWidget(clear_button);
    layout->addLayout(file_row);
    connect(clear_button, &QPushButton::clicked, this, &ModeScreenBase::OnClearClicked);

    layout->addWidget(BuildFileSetPageContent(page));
    layout->addStretch();
    return page;
}

QWidget* ModeScreenBase::BuildGeneratingPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

    progress_bar_ = new CustomProgressBar(page);
    layout->addWidget(progress_bar_);

    layout->addStretch();
    return page;
}

QWidget* ModeScreenBase::BuildDonePage(bool with_preview) {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* back = MakeBackButton(page);
    connect(back, &QPushButton::clicked, this, &ModeScreenBase::backRequested);
    layout->addWidget(back, 0, Qt::AlignLeft);

    auto* title = new QLabel(ModeTitle(), page);
    title->setObjectName("anyWavTitle");
    layout->addWidget(title);

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
        connect(generate_button, &QPushButton::clicked, this, &ModeScreenBase::OnGenerateClicked);
        connect(export_button, &QPushButton::clicked, this, &ModeScreenBase::OnExportClicked);

        preview_controls_ = new PreviewControlsWidget(engine_, page);
        layout->addWidget(preview_controls_);
    }

    layout->addStretch();
    return page;
}

void ModeScreenBase::OnFileChosen(const QString& path) {
    current_file_ = path;
    if (filename_label_) {
        filename_label_->setText(QFileInfo(path).fileName());
    }
    Service()->SetInputFile(path);
    SetState(State::kFileSet);
}

void ModeScreenBase::OnClearClicked() {
    current_file_.clear();
    OnClearHook();
    SetState(State::kEmpty);
}

void ModeScreenBase::OnGenerateClicked() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
        if (preview_controls_) {
            preview_controls_->RefreshPlayButton();
        }
    }
    SetState(State::kGenerating);
    Service()->Generate();
}

void ModeScreenBase::OnExportClicked() {
    const QString dest = QFileDialog::getExistingDirectory(
        this, "Export bank to…", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dest.isEmpty()) {
        return;
    }
    const QString src_dir = OutputDirForPreview();
    for (int i = 1; i <= kNumPages; ++i) {
        const QString src = QString("%1/%2.wav").arg(src_dir).arg(i);
        const QString dst = QString("%1/%2.wav").arg(dest).arg(i);
        QFile::copy(src, dst);
    }
}

void ModeScreenBase::OnProgressChanged(int percent) {
    if (progress_bar_) {
        progress_bar_->SetProgress(percent);
    }
}

void ModeScreenBase::OnGenerationFinished() {
    SetState(State::kDoneMessage);
}

}  // namespace fim::ui
