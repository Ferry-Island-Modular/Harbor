#pragma once

#include <QMainWindow>
#include <memory>

#include "app/settings.h"

class QStackedWidget;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::ui {

class AnyWavScreen;
class LauncherScreen;
class SerumWavScreen;

// Top-level QMainWindow. Owns the RealtimeAudioEngine, the persistent
// Settings instance, and the QStackedWidget router that swaps between
// LauncherScreen and AnyWavScreen.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnModeChosen(int mode);  // takes int because of LauncherScreen::Mode enum
    void OnBackToLauncher();

private:
    void BuildMenuBar();

    std::unique_ptr<fim::engine::RealtimeAudioEngine> engine_;
    fim::app::Settings settings_;
    QStackedWidget* stack_ = nullptr;
    LauncherScreen* launcher_screen_ = nullptr;
    AnyWavScreen* any_wav_screen_ = nullptr;
    SerumWavScreen* serum_wav_screen_ = nullptr;
    int launcher_index_ = -1;
    int any_wav_index_ = -1;
    int serum_wav_index_ = -1;
};

}  // namespace fim::ui
