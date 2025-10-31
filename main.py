from pathlib import Path

from PySide6.QtCore import QSize, Qt
from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QPushButton,
    QVBoxLayout,
    QLabel,
    QWidget,
    QProgressBar,
)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.i = 0

        self.setWindowTitle("My App")

        self.label = QLabel("Asswipe")

        self.button = QPushButton("Press Me!")
        self.button.clicked.connect(self.onClick)

        self.progress = QProgressBar()
        self.progress.setMaximum(100)
        self.progress.setMinimum(0)

        layout = QVBoxLayout()
        layout.addWidget(self.label)
        layout.addWidget(self.button)
        layout.addWidget(self.progress)

        container = QWidget()
        container.setLayout(layout)

        # Set the central widget of the Window.
        self.setCentralWidget(container)

    def onClick(self):
        self.i += 1
        self.label.setText(f"Clicked {self.i} times")
        self.progress.setValue(self.progress.value() + 10)


if __name__ == "__main__":
    app = QApplication()

    app.setStyleSheet(Path("login.qss").read_text())

    window = MainWindow()
    window.show()

    app.exec()
