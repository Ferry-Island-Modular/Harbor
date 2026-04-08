#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;

namespace fim::ui {

// One of the three mode chooser cards on the launcher screen. A QFrame with
// a description label and a button. Construct with the description text and
// the initial button state. Emits `chosen()` when the user clicks the button.
class CardButton : public QFrame {
    Q_OBJECT

public:
    enum class State {
        kDefault,     // "Choose" — clickable
        kChosen,      // "Chosen" — clickable, visually selected
        kComingSoon,  // "Coming soon!" — disabled
    };

    CardButton(const QString& description, State initial_state, QWidget* parent = nullptr);

    void SetState(State state);
    State state() const { return state_; }

signals:
    void chosen();

private:
    void UpdateButtonText();

    QLabel* description_label_ = nullptr;
    QPushButton* action_button_ = nullptr;
    State state_;
};

}  // namespace fim::ui
