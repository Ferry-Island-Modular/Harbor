from pathlib import Path
from typing import override

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QMainWindow,
    QPushButton,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QWidget,
    QProgressBar,
)


class FileDropWidget(QLabel):
    file_dropped = Signal(dict)

    def __init__(self, label_text: str, id: str):
        super().__init__(label_text)
        self.id = id
        self.setAlignment(Qt.AlignCenter)
        self.setAcceptDrops(True)

    @override
    def dragEnterEvent(self, event):
        # Check if the dragged data contains URLs (like file paths)
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    @override
    def dropEvent(self, event):
        urls = event.mimeData().urls()
        if urls:
            # Only take the first file
            file_path = urls[0].toLocalFile()

            # Check if it's a WAV file
            if Path(file_path).suffix.lower() == ".wav":
                self.setText(Path(file_path).name)
                self.file_dropped.emit({"id": self.id, "paths": [file_path]})
            else:
                self.setText("Invalid file! Please drop a .wav file")


class MainWindow(QMainWindow):
    files_dropped = Signal(dict)
    button_clicked = Signal()

    def __init__(self):
        super().__init__()

        self.setWindowTitle("Ferry Island Modular Config Tool")

        self.x_drop = FileDropWidget(label_text="Drop X file here", id="x_drop")
        self.y_drop = FileDropWidget(label_text="Drop Y file here", id="y_drop")
        self.z_drop = FileDropWidget(label_text="Drop Z file here", id="z_drop")

        # Subscribe to each child once, re-emit unified signal
        for zone in [self.x_drop, self.y_drop, self.z_drop]:
            zone.file_dropped.connect(self.onZoneDrop)

        self.button = QPushButton("Click to generate waves")
        self.button.clicked.connect(self.onSubmit)

        self.progress = QProgressBar()
        self.progress.setMaximum(100)
        self.progress.setMinimum(0)

        drop_container = QHBoxLayout()
        drop_container.addWidget(self.x_drop)
        drop_container.addWidget(self.y_drop)
        drop_container.addWidget(self.z_drop)

        layout = QVBoxLayout()
        layout.addLayout(drop_container)
        layout.addWidget(self.button)
        layout.addWidget(self.progress)

        container = QWidget()
        container.setLayout(layout)

        # Set the central widget of the Window.
        self.setCentralWidget(container)

    def onSubmit(self):
        self.button_clicked.emit()

    def onZoneDrop(self, file_path):
        self.files_dropped.emit(file_path)
