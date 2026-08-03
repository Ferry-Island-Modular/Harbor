#include "ui/widgets/axis_morph_selector.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace fim::ui {

AxisMorphSelector::AxisMorphSelector(const QString& title, const QStringList& options,
                                     QWidget* parent)
    : QFrame(parent) {
    setObjectName("axisMorphSelector");

    // No internal margins — the selector sits inside a 32px-padded card
    // section, and its title must left-align with sibling labels.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* title_label = new QLabel(title, this);
    title_label->setObjectName("axisMorphTitle");
    layout->addWidget(title_label);

    auto* buttons_row = new QHBoxLayout();
    buttons_row->setSpacing(8);
    button_group_ = new QButtonGroup(this);
    button_group_->setExclusive(true);

    // A style sheet min-height does not reliably feed back into a widget's
    // sizeHint, so the layout allocated the smaller hinted height and Windows
    // clipped the bottom of each pill — rounded corners and all. Setting the
    // minimum here is what the layout actually honours.
    //
    // Keep in step with #axisMorphOption in styles/input.scss: 18px content
    // plus 6px padding top and bottom.
    constexpr int kOptionMinHeight = 30;

    for (int i = 0; i < options.size(); ++i) {
        auto* button = new QPushButton(options[i], this);
        button->setObjectName("axisMorphOption");
        button->setCheckable(true);
        button->setMinimumHeight(kOptionMinHeight);
        if (i == 0) {
            button->setChecked(true);
        }
        button_group_->addButton(button, i);
        buttons_row->addWidget(button);
    }
    buttons_row->addStretch();
    layout->addLayout(buttons_row);

    connect(button_group_, &QButtonGroup::idClicked, this, &AxisMorphSelector::currentIndexChanged);
}

int AxisMorphSelector::currentIndex() const {
    return button_group_->checkedId();
}

void AxisMorphSelector::SetCurrentIndex(int index) {
    auto* button = button_group_->button(index);
    if (button) {
        button->setChecked(true);
    }
}

}  // namespace fim::ui
