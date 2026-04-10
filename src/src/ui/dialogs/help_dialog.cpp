#include "ui/dialogs/help_dialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fim::ui {

HelpDialog::HelpDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Harbor Help");
    setModal(true);
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);

    QFile help_file(":/help/help.html");
    if (help_file.open(QIODevice::ReadOnly)) {
        browser->setHtml(QString::fromUtf8(help_file.readAll()));
        help_file.close();
    } else {
        browser->setHtml("<p>Help content failed to load.</p>");
    }
    layout->addWidget(browser);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

}  // namespace fim::ui
