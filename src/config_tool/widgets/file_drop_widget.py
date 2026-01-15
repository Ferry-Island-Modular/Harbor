from pathlib import Path
from typing import override

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QIcon, QPainter, QPixmap
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QPushButton,
    QStyle,
    QStyleOption,
    QWidget,
)


class FileDropWrapper(QWidget):
    file_dropped = Signal(dict)
    file_cleared = Signal()

    def __init__(self):
        super().__init__()

        self.drop = FileDropWidget("Drop or browse your audio file", id="x_drop")

        layout = QHBoxLayout(self)
        layout.setContentsMargins(24, 24, 24, 24)
        layout.setSpacing(0)
        layout.addWidget(self.drop)

        # Forward signals
        self.drop.file_dropped.connect(self.file_dropped)
        self.drop.file_cleared.connect(self.file_cleared)

    @override
    def paintEvent(self, event):
        """Required for QSS styling to work on custom QWidget subclasses."""
        opt = QStyleOption()
        opt.initFrom(self)
        p = QPainter(self)
        self.style().drawPrimitive(QStyle.PrimitiveElement.PE_Widget, opt, p, self)


class FileDropWidget(QWidget):
    file_dropped = Signal(dict)
    file_cleared = Signal()

    def __init__(self, label_text: str, id: str, icon_path: str = ""):
        super().__init__()
        self.id: str = id
        self.setAcceptDrops(True)
        self.filePathSet = ""
        self.filePath = None

        # Create layout
        self.boxLayout = QHBoxLayout(self)
        self.boxLayout.setContentsMargins(24, 24, 24, 24)
        self.boxLayout.setAlignment(Qt.AlignmentFlag.AlignCenter)

        # Create icon label (optional)
        self.icon_label: QLabel = QLabel()

        self.boxLayout.addWidget(self.icon_label)

        # Create text label
        self.text_label: QLabel = QLabel(
            """
            Drop or
            <span style="color: #d6a62c; text-decoration: underline;">browse</span>
            for your audio file
            """
        )
        self.text_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.text_label.setObjectName("fileDropLabel")
        self.text_label.setTextFormat(Qt.TextFormat.RichText)

        # TODO: this is an ugly hack
        self.clear_button = QPushButton("\u2003Clear")
        self.clear_button.setObjectName("clearButton")
        self.clear_button.hide()
        self.clear_button.setIcon(QIcon(":/assets/x-circle.svg"))
        self.clear_button.clicked.connect(self.onClearForm)

        self.boxLayout.addWidget(self.text_label)
        self.boxLayout.addWidget(self.clear_button)

        self.setLayout(self.boxLayout)

    def setIcon(self):
        pixmap = QPixmap(":/assets/audio-waveform.svg")
        self.icon_label.setPixmap(
            pixmap.scaled(
                24,
                24,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )

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
                # Update own UI first
                self._update_ui_for_file(file_path)
                # Then emit signal for parent
                self.file_dropped.emit({"id": self.id, "paths": [file_path]})
            else:
                self.text_label.setText("Invalid file! Please drop a .wav file")

    def _update_ui_for_file(self, file_path: str):
        """Update widget UI when file is loaded"""
        self.filePath = Path(file_path).name
        self.text_label.setText(self.filePath)
        self.spacer_item = self.boxLayout.insertStretch(2, 0)
        self.clear_button.show()

    def onClearForm(self):
        self.file_cleared.emit()
        self.filePath = None
        self.text_label.setText("""
        Drop or
        <span style="color: #d6a62c; text-decoration: underline;">browse</span>
        for your audio file
        """)
        self.clear_button.hide()
        for i in reversed(range(self.boxLayout.count())):
            item = self.boxLayout.itemAt(i)
            if item.spacerItem():
                self.boxLayout.removeItem(item)

    @override
    def paintEvent(self, event):
        """Required for QSS styling to work on custom QWidget subclasses."""
        opt = QStyleOption()
        opt.initFrom(self)
        p = QPainter(self)
        self.style().drawPrimitive(QStyle.PrimitiveElement.PE_Widget, opt, p, self)
