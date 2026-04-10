#pragma once

#include <QFrame>
#include <QString>

class QLabel;

namespace fim::ui {

// Drag-and-drop area for accepting a single .wav file. Has a fallback
// "browse" link in its label text that opens a QFileDialog. Emits
// `fileDropped(path)` on either drop or browse pick.
class FileDropWidget : public QFrame {
    Q_OBJECT

public:
    explicit FileDropWidget(QWidget* parent = nullptr);

signals:
    void fileDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void OpenBrowseDialog();

    QLabel* label_ = nullptr;
};

}  // namespace fim::ui
