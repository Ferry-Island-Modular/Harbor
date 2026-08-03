#include "ui/widgets/file_drop_widget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>

namespace fim::ui {

FileDropWidget::FileDropWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("fileDropWidget");
    setAcceptDrops(true);
    setMinimumHeight(88);
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Inline style — the design colors "browse" in the accent gold with no
    // underline, and QSS cannot reach into rich-text links.
    label_ = new QLabel(
        "Drop or <a href=\"#browse\" style=\"color: #d6a62c; text-decoration: none;\">browse</a> "
        "for your audio file",
        this);
    label_->setObjectName("fileDropLabel");
    label_->setAlignment(Qt::AlignCenter);
    label_->setTextFormat(Qt::RichText);
    label_->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    layout->addWidget(label_);

    connect(label_, &QLabel::linkActivated, this, [this](const QString&) { OpenBrowseDialog(); });
}

void FileDropWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && QFileInfo(urls.first().toLocalFile()).suffix().toLower() == "wav") {
            event->acceptProposedAction();
        }
    }
}

void FileDropWidget::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (QFileInfo(path).suffix().toLower() == "wav") {
        emit fileDropped(path);
    }
}

void FileDropWidget::mousePressEvent(QMouseEvent* event) {
    // Click anywhere on the widget (not just the link) opens the browser.
    if (event->button() == Qt::LeftButton) {
        OpenBrowseDialog();
    }
}

void FileDropWidget::OpenBrowseDialog() {
    const QString path =
        QFileDialog::getOpenFileName(this, "Select a WAV file", QString(), "WAV files (*.wav)");
    if (!path.isEmpty()) {
        emit fileDropped(path);
    }
}

}  // namespace fim::ui
