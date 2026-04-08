#pragma once

#include <QMainWindow>
#include <memory>

class QStackedWidget;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::ui {

class AnyWavScreen;
class LauncherScreen;

// Top-level QMainWindow. Owns the RealtimeAudioEngine and the QStackedWidget
// router that swaps between LauncherScreen and AnyWavScreen.
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
    QStackedWidget* stack_ = nullptr;
    LauncherScreen* launcher_screen_ = nullptr;
    AnyWavScreen* any_wav_screen_ = nullptr;
    int launcher_index_ = -1;
    int any_wav_index_ = -1;
};

}  // namespace fim::ui
