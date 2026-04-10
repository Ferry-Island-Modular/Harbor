#include "ui/widgets/card_button.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace fim::ui {

CardButton::CardButton(const QString& description, State initial_state, QWidget* parent)
    : QFrame(parent), state_(initial_state) {
    setObjectName("cardButton");
    setFrameShape(QFrame::StyledPanel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    description_label_ = new QLabel(description, this);
    description_label_->setObjectName("cardButtonDescription");
    description_label_->setWordWrap(true);
    description_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(description_label_, /*stretch=*/1);

    auto* button_row = new QHBoxLayout();
    action_button_ = new QPushButton(this);
    action_button_->setObjectName("cardButtonAction");
    button_row->addWidget(action_button_);
    button_row->addStretch();
    layout->addLayout(button_row);

    UpdateButtonText();

    connect(action_button_, &QPushButton::clicked, this, &CardButton::chosen);
}

void CardButton::SetState(State state) {
    state_ = state;
    UpdateButtonText();
}

void CardButton::UpdateButtonText() {
    switch (state_) {
        case State::kDefault:
            action_button_->setText("Choose");
            action_button_->setEnabled(true);
            action_button_->setProperty("variant", "default");
            break;
        case State::kChosen:
            action_button_->setText("Chosen");
            action_button_->setEnabled(true);
            action_button_->setProperty("variant", "chosen");
            break;
        case State::kComingSoon:
            action_button_->setText("Coming soon!");
            action_button_->setEnabled(false);
            action_button_->setProperty("variant", "comingSoon");
            break;
    }
    // Force a re-style after a property change.
    action_button_->style()->unpolish(action_button_);
    action_button_->style()->polish(action_button_);
}

}  // namespace fim::ui
