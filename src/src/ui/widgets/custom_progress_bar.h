#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QProgressBar;

namespace fim::ui {

// A label-over-progressbar composite. The label text and progress value are
// independently settable. Used by AnyWavScreen during the kGenerating state.
class CustomProgressBar : public QFrame {
    Q_OBJECT

public:
    explicit CustomProgressBar(QWidget* parent = nullptr);

    void SetLabel(const QString& text);
    void SetProgress(int percent);  // 0..100

private:
    QLabel* label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
};

}  // namespace fim::ui
