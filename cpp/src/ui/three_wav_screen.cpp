#include "ui/three_wav_screen.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include "app/services/three_wav_service.h"
#include "app/settings.h"
#include "ui/widgets/file_drop_widget.h"

namespace fim::ui {

namespace {

QString ThreeWavOutputDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("three_wav_resynth");
}

}  // namespace

ThreeWavScreen::ThreeWavScreen(fim::engine::RealtimeAudioEngine* engine,
                               fim::app::Settings* settings, QWidget* parent)
    : ModeScreenBase(engine, settings, parent) {
    setObjectName("threeWavScreen");

    service_ = new fim::app::ThreeWavService(this);
    service_->SetOutputDirectory(ThreeWavOutputDir());

    FinishInit();

    // Three-wav skips kEmpty — the file-set page already shows 3 drop
    // widgets so there's no "drop one file first" state. Jump straight
    // to kFileSet after FinishInit has built the pages.
    SetState(State::kFileSet);
}

void ThreeWavScreen::Reset() {
    for (int i = 0; i < 3; ++i) {
        service_->SetInputFileAt(i, QString());
        if (filename_labels_[i] != nullptr) {
            filename_labels_[i]->setText("(no file)");
        }
    }
    RefreshGenerateEnabled();
    // Stay in kFileSet — don't go back to kEmpty.
    SetState(State::kFileSet);
}

QString ThreeWavScreen::ModeTitle() const {
    return "Use three .wav files to create your wavetable bank";
}

QWidget* ThreeWavScreen::BuildEmptyPageContent(QWidget* parent) {
    // Never shown — the constructor jumps straight to kFileSet.
    return new QWidget(parent);
}

QWidget* ThreeWavScreen::BuildFileSetPageContent(QWidget* parent) {
    auto* content = new QWidget(parent);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // 3 file slots, each mapped to an axis. Each slot: axis title,
    // filename label + clear button row, drop widget.
    static const QStringList kSlotTitles{"X axis — file 1", "Y axis — file 2", "Z axis — file 3"};
    for (int i = 0; i < 3; ++i) {
        auto* slot_title = new QLabel(kSlotTitles[i], content);
        slot_title->setObjectName("anyWavAxisLabel");
        layout->addWidget(slot_title);

        auto* file_row = new QHBoxLayout();
        filename_labels_[i] = new QLabel("(no file)", content);
        filename_labels_[i]->setObjectName("anyWavFilename");
        auto* clear_button = new QPushButton("Clear", content);
        clear_button->setObjectName("clearButton");
        file_row->addWidget(filename_labels_[i]);
        file_row->addStretch();
        file_row->addWidget(clear_button);
        layout->addLayout(file_row);
        connect(clear_button, &QPushButton::clicked, this, [this, i]() { OnSlotClearClicked(i); });

        drop_widgets_[i] = new FileDropWidget(content);
        layout->addWidget(drop_widgets_[i]);
        connect(drop_widgets_[i], &FileDropWidget::fileDropped, this,
                [this, i](const QString& path) { OnSlotFileDropped(i, path); });
    }

    auto* generate_row = new QHBoxLayout();
    generate_button_ = new QPushButton("Generate wavetable bank", content);
    generate_button_->setObjectName("generateButton");
    generate_button_->setEnabled(false);
    generate_row->addStretch();
    generate_row->addWidget(generate_button_);
    layout->addLayout(generate_row);
    connect(generate_button_, &QPushButton::clicked, this, [this]() { OnGenerateClicked(); });

    return content;
}

fim::app::GenerateServiceBase* ThreeWavScreen::Service() {
    return service_;
}

void ThreeWavScreen::OnClearHook() {
    // Unused — three-wav's ShowDefaultFilenameRow=false means the base
    // class's clear button doesn't exist, so this hook is never called.
}

void ThreeWavScreen::OnResetHook() {
    // Unused — three-wav overrides Reset() directly.
}

QString ThreeWavScreen::OutputDirForPreview() const {
    return ThreeWavOutputDir();
}

void ThreeWavScreen::OnSlotFileDropped(int slot_index, const QString& path) {
    service_->SetInputFileAt(slot_index, path);
    if (filename_labels_[slot_index] != nullptr) {
        filename_labels_[slot_index]->setText(QFileInfo(path).fileName());
    }
    RefreshGenerateEnabled();
}

void ThreeWavScreen::OnSlotClearClicked(int slot_index) {
    service_->SetInputFileAt(slot_index, QString());
    if (filename_labels_[slot_index] != nullptr) {
        filename_labels_[slot_index]->setText("(no file)");
    }
    RefreshGenerateEnabled();
}

void ThreeWavScreen::RefreshGenerateEnabled() {
    if (generate_button_ != nullptr) {
        generate_button_->setEnabled(service_->AllFilesSet());
    }
}

}  // namespace fim::ui
