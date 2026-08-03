#include "ui/widgets/custom_progress_bar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>

namespace fim::ui {

CustomProgressBar::CustomProgressBar(QWidget* parent) : QFrame(parent) {
    setObjectName("customProgressBar");

    // Design: label and bar share one row, bar filling the remaining width.
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    label_ = new QLabel("Generating wavetable bank…", this);
    label_->setObjectName("customProgressLabel");
    layout->addWidget(label_);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setObjectName("customProgressBarInner");
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    progress_bar_->setTextVisible(false);
    layout->addWidget(progress_bar_, /*stretch=*/1);
}

void CustomProgressBar::SetLabel(const QString& text) {
    label_->setText(text);
}

void CustomProgressBar::SetProgress(int percent) {
    progress_bar_->setValue(percent);
}

}  // namespace fim::ui
