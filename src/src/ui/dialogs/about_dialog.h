#pragma once

#include <QDialog>

namespace fim::ui {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace fim::ui
