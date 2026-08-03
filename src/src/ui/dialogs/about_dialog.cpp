#include "ui/dialogs/about_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace fim::ui {

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("About Harbor");
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto* title = new QLabel("<h2>Harbor</h2>", this);
    layout->addWidget(title);

    auto* body = new QLabel(this);
    body->setText(QString("<p>Version: %1 (beta)</p>"
                          "<p>Wavetable bank generator for Ferry Island Modular hardware "
                          "and other wavetable synth hosts.</p>"
                          "<p>License: MIT</p>"
                          "<p>Built with Qt, libsamplerate, dr_wav, miniaudio, PFFFT, "
                          "Catch2, and the FourSeas firmware engine.</p>"
                          "<p><a href='https://github.com/Ferry-Island-Modular/Harbor'>"
                          "github.com/Ferry-Island-Modular/Harbor</a></p>")
                      .arg(HARBOR_VERSION));
    body->setOpenExternalLinks(true);
    body->setWordWrap(true);
    layout->addWidget(body);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    setFixedSize(420, sizeHint().height());
}

}  // namespace fim::ui
