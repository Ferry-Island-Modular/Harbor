#include "ui/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMenuBar>
#include <QStackedWidget>

#include "engine/realtime_audio_engine.h"
#include "ui/any_wav_screen.h"
#include "ui/dialogs/about_dialog.h"
#include "ui/dialogs/help_dialog.h"
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

    auto* export_menu = menuBar()->addMenu("&Export");
    auto* target_menu = export_menu->addMenu("Target hardware");
    target_hardware_group_ = new QActionGroup(this);
    target_hardware_group_->setExclusive(true);

    auto* four_seas_action = target_menu->addAction("Four Seas (2048 samples)");
    four_seas_action->setCheckable(true);
    four_seas_action->setData(2048);
    target_hardware_group_->addAction(four_seas_action);

    auto* waveedit_action = target_menu->addAction("Waveedit (256 samples)");
    waveedit_action->setCheckable(true);
    waveedit_action->setData(256);
    target_hardware_group_->addAction(waveedit_action);

    // Restore the persisted selection.
    const int current_spf = settings_.SamplesPerFrame();
    if (current_spf == 256) {
        waveedit_action->setChecked(true);
    } else {
        four_seas_action->setChecked(true);
    }

    connect(target_hardware_group_, &QActionGroup::triggered, this,
            &MainWindow::OnTargetHardwareChanged);

    auto* help_menu = menuBar()->addMenu("&Help");
    help_menu->addAction("Harbor Help…", this, &MainWindow::OnShowHelp);
    help_menu->addSeparator();
    help_menu->addAction("About Harbor…", this, &MainWindow::OnShowAbout);
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

void MainWindow::OnTargetHardwareChanged() {
    auto* checked = target_hardware_group_->checkedAction();
    if (!checked) {
        return;
    }
    const int samples = checked->data().toInt();
    settings_.SetSamplesPerFrame(samples);
    any_wav_screen_->RefreshOutputDirFromSettings();
    serum_wav_screen_->RefreshOutputDirFromSettings();
    three_wav_screen_->RefreshOutputDirFromSettings();
}

void MainWindow::OnShowHelp() {
    HelpDialog dialog(this);
    dialog.exec();
}

void MainWindow::OnShowAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

}  // namespace fim::ui
