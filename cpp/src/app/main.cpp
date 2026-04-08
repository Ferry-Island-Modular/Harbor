#include <QApplication>
#include <QFile>
#include <QTextStream>

#include "ui/preview_window.h"

namespace {

QString LoadStylesheet() {
    QFile f(":/app.qss");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&f);
    return in.readAll();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Ferry Island Modular");
    app.setApplicationName("FIM Config Tool");

    const QString qss = LoadStylesheet();
    if (!qss.isEmpty()) {
        app.setStyleSheet(qss);
    }

    fim::ui::PreviewWindow window;
    window.show();

    return app.exec();
}
