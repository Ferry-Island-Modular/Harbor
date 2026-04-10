#include "ui/main_window.h"

#include <QAction>
#include <QFileDialog>
#include <QMenuBar>
#include <QStackedWidget>

#include "engine/realtime_audio_engine.h"
#include "ui/any_wav_screen.h"
#include "ui/launcher_screen.h"
#include "ui/serum_wav_screen.h"
#include "ui/three_wav_screen.h"

namespace fim::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      engine_(std::make_unique<fim::engine::RealtimeAudioEngine>(48000.0f, 512)) {
    setWindowTitle("Harbor (beta v" HARBOR_VERSION ")");
    resize(1100, 560);
    setMinimumSize(960, 520);

    stack_ = new QStackedWidget(this);

    launcher_screen_ = new LauncherScreen(this);
    any_wav_screen_ = new AnyWavScreen(engine_.get(), &settings_, this);
    serum_wav_screen_ = new SerumWavScreen(engine_.get(), &settings_, this);
    three_wav_screen_ = new ThreeWavScreen(engine_.get(), &settings_, this);

    launcher_index_ = stack_->addWidget(launcher_screen_);
    any_wav_index_ = stack_->addWidget(any_wav_screen_);
    serum_wav_index_ = stack_->addWidget(serum_wav_screen_);
    three_wav_index_ = stack_->addWidget(three_wav_screen_);

    setCentralWidget(stack_);

    connect(launcher_screen_, &LauncherScreen::modeChosen, this,
            [this](LauncherScreen::Mode mode) { OnModeChosen(static_cast<int>(mode)); });
    connect(any_wav_screen_, &AnyWavScreen::backRequested, this, &MainWindow::OnBackToLauncher);
    connect(serum_wav_screen_, &SerumWavScreen::backRequested, this, &MainWindow::OnBackToLauncher);
    connect(three_wav_screen_, &ThreeWavScreen::backRequested, this, &MainWindow::OnBackToLauncher);

    BuildMenuBar();

    stack_->setCurrentIndex(launcher_index_);
}

MainWindow::~MainWindow() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
    }
}

void MainWindow::OnModeChosen(int mode) {
    const auto m = static_cast<LauncherScreen::Mode>(mode);
    if (m == LauncherScreen::Mode::kAnyWav) {
        any_wav_screen_->Reset();
        stack_->setCurrentIndex(any_wav_index_);
    } else if (m == LauncherScreen::Mode::kSerum) {
        serum_wav_screen_->Reset();
        stack_->setCurrentIndex(serum_wav_index_);
    } else if (m == LauncherScreen::Mode::kThreeWavs) {
        three_wav_screen_->Reset();
        stack_->setCurrentIndex(three_wav_index_);
    }
}

void MainWindow::OnBackToLauncher() {
    if (engine_->IsPlaying()) {
        engine_->Stop();
    }
    stack_->setCurrentIndex(launcher_index_);
}

void MainWindow::BuildMenuBar() {
    auto* file_menu = menuBar()->addMenu("&File");
    auto* choose_dir_action = file_menu->addAction("Choose output directory…", this,
                                                   &MainWindow::OnChooseOutputDirectory);
    choose_dir_action->setStatusTip("Pick where Harbor saves your generated wavetable banks");
    file_menu->addSeparator();
    file_menu->addAction("Quit", QKeySequence::Quit, this, &QWidget::close);

    // Export menu (Task 6 fills this in)
    menuBar()->addMenu("&Export");

    // Help menu (Task 7 fills this in)
    menuBar()->addMenu("&Help");
}

void MainWindow::OnChooseOutputDirectory() {
    const QString current = QString::fromStdString(settings_.OutputDir());
    const QString picked = QFileDialog::getExistingDirectory(
        this, "Choose Harbor output directory", current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (picked.isEmpty()) {
        return;  // user cancelled
    }
    settings_.SetOutputDir(picked.toStdString());
    any_wav_screen_->RefreshOutputDirFromSettings();
    serum_wav_screen_->RefreshOutputDirFromSettings();
    three_wav_screen_->RefreshOutputDirFromSettings();
}

}  // namespace fim::ui
