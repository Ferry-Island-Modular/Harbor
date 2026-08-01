#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <QTextStream>

#include "ui/main_window.h"

namespace {

// Colors mirrored from styles/input.scss. Keep the two in sync — the palette
// covers what the style sheet cannot reach, so a drift shows up as one stray
// light-themed control rather than an obvious break.
constexpr QRgb kColorDarkBg = 0xff222222;
constexpr QRgb kColorBase = 0xff111111;
constexpr QRgb kColorAccent = 0xffd6a62c;
constexpr QRgb kColorDisabledText = 0xff888888;

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

// Style sheets only paint widgets they have rules for. Everything else —
// tooltips, scrollbar grooves, disabled text, native dialog chrome — draws
// from the application palette, which defaults to the host theme and is light
// under Ubuntu's stock Yaru. Setting the palette explicitly keeps those
// consistent across platforms without calling setStyle("Fusion"), which would
// also change how the macOS build looks.
void ApplyDarkPalette(QApplication& app) {
    QPalette palette = app.palette();

    palette.setColor(QPalette::Window, QColor(kColorDarkBg));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(kColorBase));
    palette.setColor(QPalette::AlternateBase, QColor(kColorDarkBg));
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(kColorDarkBg));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(kColorDarkBg));
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(kColorAccent));
    palette.setColor(QPalette::HighlightedText, Qt::black);

    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(kColorDisabledText));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(kColorDisabledText));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(kColorDisabledText));

    app.setPalette(palette);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Ferry Island Modular");
    app.setApplicationName("Harbor");
    app.setApplicationVersion(HARBOR_VERSION);

    RegisterBundledFonts();
    ApplyDarkPalette(app);

    const QString qss = LoadStylesheet();
    if (!qss.isEmpty()) {
        app.setStyleSheet(qss);
    }

    fim::ui::MainWindow window;
    window.show();

    return app.exec();
}
