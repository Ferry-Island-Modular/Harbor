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

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* title_label = new QLabel(title, this);
    title_label->setObjectName("axisMorphTitle");
    layout->addWidget(title_label);

    auto* buttons_row = new QHBoxLayout();
    buttons_row->setSpacing(8);
    button_group_ = new QButtonGroup(this);
    button_group_->setExclusive(true);

    for (int i = 0; i < options.size(); ++i) {
        auto* button = new QPushButton(options[i], this);
        button->setObjectName("axisMorphOption");
        button->setCheckable(true);
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
