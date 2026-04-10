#pragma once

#include <QDialog>

namespace fim::ui {

class HelpDialog : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(QWidget* parent = nullptr);
};

}  // namespace fim::ui
