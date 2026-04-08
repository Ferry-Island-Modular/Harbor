#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::app {
class SingleWavService;
}

namespace fim::ui {

class AxisMorphSelector;
class CustomProgressBar;
class FileDropWidget;
class PreviewControlsWidget;

// The "any wav" mode screen. Internal state machine implemented as a
// QStackedWidget with one page per state. The "← Back" button at the top is
// always visible and emits backRequested().
//
// State transitions:
//   kEmpty           ──── file dropped ────► kFileSet
//   kFileSet         ──── Generate clicked ─► kGenerating
//   kGenerating      ──── service done ─────► kDoneMessage
//   kDoneMessage     ──── after ~800ms ─────► kDonePreviewAvailable
//   kDoneMessage,
//   kDonePreviewAvailable
//                    ──── Generate clicked ─► kGenerating
class AnyWavScreen : public QWidget {
    Q_OBJECT

public:
    enum class State {
        kEmpty,
        kFileSet,
        kGenerating,
        kDoneMessage,
        kDonePreviewAvailable,
    };

    explicit AnyWavScreen(fim::engine::RealtimeAudioEngine* engine, QWidget* parent = nullptr);

    // Resets the screen back to the empty state. Called when the user
    // navigates away and returns later.
    void Reset();

signals:
    void backRequested();

private slots:
    void OnFileDropped(const QString& path);
    void OnClearClicked();
    void OnGenerateClicked();
    void OnExportClicked();
    void OnProgressChanged(int percent);
    void OnGenerationFinished();

private:
    void SetState(State state);
    QWidget* BuildEmptyPage();
    QWidget* BuildFileSetPage();
    QWidget* BuildGeneratingPage();
    QWidget* BuildDonePage(bool with_preview);

    fim::engine::RealtimeAudioEngine* engine_;
    fim::app::SingleWavService* service_ = nullptr;
    QString current_file_;

    QStackedWidget* stack_ = nullptr;
    int empty_page_index_ = -1;
    int file_set_page_index_ = -1;
    int generating_page_index_ = -1;
    int done_message_page_index_ = -1;
    int done_preview_page_index_ = -1;

    // References into the file_set / done pages so the slots can update
    // them. Owned by their parent QWidgets, not by this class directly.
    QLabel* filename_label_ = nullptr;
    AxisMorphSelector* y_selector_ = nullptr;
    AxisMorphSelector* z_selector_ = nullptr;
    CustomProgressBar* progress_bar_ = nullptr;
    PreviewControlsWidget* preview_controls_ = nullptr;
};

}  // namespace fim::ui
