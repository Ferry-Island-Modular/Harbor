#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace fim::engine {
class RealtimeAudioEngine;
}

namespace fim::app {
class GenerateServiceBase;
class Settings;
}  // namespace fim::app

namespace fim::ui {

class CustomProgressBar;
class PreviewControlsWidget;

// Abstract base for mode screens (AnyWavScreen, SerumWavScreen, etc.).
// Holds the 5-state QStackedWidget machine, the back-button header, the
// progress bar used during generation, and the embedded preview controls
// that play the generated output. Subclasses provide the mode-specific
// content for the "empty" and "file-set" pages, plus a reference to a
// concrete GenerateServiceBase subclass.
//
// State transitions (same for all modes):
//   kEmpty           ──── file dropped ────► kFileSet
//   kFileSet         ──── Generate clicked ─► kGenerating
//   kGenerating      ──── service done ─────► kDoneMessage
//   kDoneMessage     ──── after ~800ms ─────► kDonePreviewAvailable
//   kDoneMessage,
//   kDonePreviewAvailable
//                    ──── Generate clicked ─► kGenerating
class ModeScreenBase : public QWidget {
    Q_OBJECT

public:
    enum class State {
        kEmpty,
        kFileSet,
        kGenerating,
        kDoneMessage,
        kDonePreviewAvailable,
    };

    ModeScreenBase(fim::engine::RealtimeAudioEngine* engine, fim::app::Settings* settings,
                   QWidget* parent = nullptr);
    ~ModeScreenBase() override = default;

    // Resets the screen back to the empty state. Called when the user
    // navigates away and returns. Also calls the subclass OnResetHook so
    // mode-specific state (file path, selector positions) can reset too.
    //
    // Virtual so multi-file modes can override to handle their own
    // state (e.g. ThreeWavScreen clears 3 slots and stays in kFileSet).
    virtual void Reset();

signals:
    void backRequested();

protected:
    // ---- Hooks subclasses must implement ----

    // The title shown at the top of every state page (e.g. "Use any .wav
    // file to create your wavetable bank").
    virtual QString ModeTitle() const = 0;

    // Builds the mode-specific content for the empty page (below the
    // title). Typically a file drop widget.
    virtual QWidget* BuildEmptyPageContent(QWidget* parent) = 0;

    // Builds the mode-specific content for the file-set page (below the
    // title and filename row). Typically the axis selectors and the
    // Generate button.
    virtual QWidget* BuildFileSetPageContent(QWidget* parent) = 0;

    // Returns a pointer to the subclass's service instance. The base
    // class wires the service's signals (progressChanged /
    // generationFinished / generationFailed) to its own slots.
    virtual fim::app::GenerateServiceBase* Service() = 0;

    // Called from OnClearClicked when the user clicks the Clear button
    // in the file-set state. Subclasses override to clear their cached
    // input file path and reset the filename label to "(no file)".
    virtual void OnClearHook() = 0;

    // Called from Reset(). Subclasses override to re-sync mode-specific
    // UI state (e.g. axis selector positions) from Settings.
    virtual void OnResetHook() = 0;

    // Returns the directory where generated output lives. Used by the
    // base class to call engine_->LoadBank() after generation completes.
    virtual QString OutputDirForPreview() const = 0;

    // Subclass MUST call this from its own constructor body (not
    // initializer list) after its members — especially its service
    // instance — are fully constructed. Builds the state pages using
    // the subclass's virtual content hooks, wires the service signals,
    // and transitions to the initial kEmpty state.
    void FinishInit();

    // ---- Helpers exposed to subclasses ----

    // Subclasses call this when their mode-specific file drop widget
    // emits its "file chosen" signal. Updates the filename label and
    // transitions the state machine to kFileSet.
    void OnFileChosen(const QString& path);

    // Subclasses call this when their Generate button is clicked.
    // Transitions to kGenerating and calls service->Generate().
    void OnGenerateClicked();

    // Transition the state machine. Subclasses occasionally need this
    // (e.g. ThreeWavScreen jumping to kFileSet from its constructor
    // since it doesn't use the kEmpty state at all).
    void SetState(State state);

    // Wraps `content` in a rounded #111 card frame (the anyWavInnerCard
    // style) with the design's 32px padding. The design places every
    // content block on the mode pages inside one of these sections.
    static QWidget* MakeSectionCard(QWidget* content, QWidget* parent);

    // Whether the file-set page should show the default "single filename
    // label + clear button" row above the subclass content. Single-file
    // modes (any-wav, Serum) return true (default). Multi-file modes
    // (three-wav) return false to suppress the row and manage filenames
    // inside their own content.
    virtual bool ShowDefaultFilenameRow() const { return true; }

    // Protected accessors for subclass use.
    fim::engine::RealtimeAudioEngine* engine() const { return engine_; }
    fim::app::Settings* settings() const { return settings_; }
    QLabel* filename_label() const { return filename_label_; }
    void set_current_file(const QString& path) { current_file_ = path; }
    const QString& current_file() const { return current_file_; }

private slots:
    void OnClearClicked();
    void OnExportClicked();
    void OnProgressChanged(int percent);
    void OnGenerationFinished();

private:
    QWidget* BuildEmptyPage();
    QWidget* BuildFileSetPage();
    QWidget* BuildGeneratingPage();
    QWidget* BuildDonePage(bool with_preview);
    QPushButton* MakeBackButton(QWidget* parent);

    fim::engine::RealtimeAudioEngine* engine_;  // non-owning
    fim::app::Settings* settings_;              // non-owning
    QString current_file_;

    QStackedWidget* stack_ = nullptr;
    int empty_page_index_ = -1;
    int file_set_page_index_ = -1;
    int generating_page_index_ = -1;
    int done_message_page_index_ = -1;
    int done_preview_page_index_ = -1;

    QLabel* filename_label_ = nullptr;
    CustomProgressBar* progress_bar_ = nullptr;
    PreviewControlsWidget* preview_controls_ = nullptr;
};

}  // namespace fim::ui
