from typing import override

from PySide6.QtGui import QPainter
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QStyle,
    QStyleOption,
    QWidget,
)


class CustomProgressBar(QWidget):
    def __init__(self):
        super().__init__()

        self.progress = QProgressBar()
        self.progress.setMaximum(100)
        self.progress.setMinimum(0)
        self.progress.setObjectName("progressBar")

        self.title: QLabel = QLabel("Creating wavetable")
        self.title.setObjectName("progressBarLabel")

        layout = QHBoxLayout(self)
        layout.addWidget(self.title)
        layout.addWidget(self.progress)
        layout.setContentsMargins(24, 24, 24, 24)

        self.setLayout(layout)

    def setValue(self, value: int):
        """Set progress bar value"""
        self.progress.setValue(value)

    @override
    def paintEvent(self, event):
        """Required for QSS styling to work on custom QWidget subclasses."""
        opt = QStyleOption()
        opt.initFrom(self)
        p = QPainter(self)
        self.style().drawPrimitive(QStyle.PE_Widget, opt, p, self)
