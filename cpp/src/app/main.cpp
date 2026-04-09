#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QTextStream>

#include "ui/main_window.h"

namespace {

QString LoadStylesheet() {
    QFile f(":/app.qss");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&f);
    return in.readAll();
}

// Register bundled fonts so the stylesheet's font-family lookups resolve
// identically on every machine, regardless of what's installed system-wide.
// Called once at startup, before any widgets are constructed.
void RegisterBundledFonts() {
    QFontDatabase::addApplicationFont(":/fonts/InterVariable.ttf");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Ferry Island Modular");
    app.setApplicationName("FIM Config Tool");

    RegisterBundledFonts();

    const QString qss = LoadStylesheet();
    if (!qss.isEmpty()) {
        app.setStyleSheet(qss);
    }

    fim::ui::MainWindow window;
    window.show();

    return app.exec();
}
