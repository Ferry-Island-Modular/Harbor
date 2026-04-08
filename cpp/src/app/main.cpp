#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMainWindow>
#include <QTextStream>

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

    QMainWindow window;
    window.setWindowTitle("FIM Config Tool");
    window.resize(1000, 378);

    auto* label = new QLabel("Hello, FourSeas", &window);
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);

    window.show();
    return app.exec();
}
