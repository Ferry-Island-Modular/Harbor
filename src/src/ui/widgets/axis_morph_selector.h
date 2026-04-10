#pragma once

#include <QFrame>
#include <QString>
#include <QStringList>

class QButtonGroup;

namespace fim::ui {

// A title label + a row of mutually-exclusive radio-button-style buttons.
// Used by AnyWavScreen for the Y and Z axis selectors.
class AxisMorphSelector : public QFrame {
    Q_OBJECT

public:
    AxisMorphSelector(const QString& title, const QStringList& options, QWidget* parent = nullptr);

    int currentIndex() const;
    void SetCurrentIndex(int index);

signals:
    void currentIndexChanged(int index);

private:
    QButtonGroup* button_group_ = nullptr;
};

}  // namespace fim::ui
