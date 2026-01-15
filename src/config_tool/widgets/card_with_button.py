from typing import override

from PySide6.QtCore import QSize, Signal
from PySide6.QtGui import QIcon, QPainter
from PySide6.QtWidgets import (
    QLabel,
    QPushButton,
    QStyle,
    QStyleOption,
    QVBoxLayout,
    QWidget,
)


class CardButton(QWidget):
    button_clicked: Signal = Signal(str)

    def __init__(self, label_text: str, id: str) -> None:
        super().__init__()
        self.setObjectName(f"card-button-{id}")

        self.id: str = id
        self.label: QLabel = QLabel(label_text)

        self.button: QPushButton = QPushButton("Choose")
        self.button.setCheckable(True)
        self.button.setIcon(QIcon())
        self.button.setIconSize(QSize(24, 24))
        self.button.clicked.connect(self.onClick)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(24, 24, 24, 24)
        layout.addWidget(self.label)
        layout.addWidget(self.button)
        self.setLayout(layout)

    def onClick(self):
        self.button_clicked.emit(self.id)

    def setEnabled(self, isEnabled: bool) -> None:
        self.button.setEnabled(isEnabled)

    def setLabelText(self, text: str) -> None:
        self.label.setText(text)

    def setButtonText(self, text: str) -> None:
        self.button.setText(text)

    def updateButton(self, button: QPushButton, checked: bool):
        if button == self.button:
            if checked:
                self.button.setText("Chosen")
                self.button.setIcon(QIcon(":/assets/check.svg"))
            else:
                self.button.setText("Choose")
                self.button.setIcon(QIcon())

    @override
    def paintEvent(self, event):
        """Required for QSS styling to work on custom QWidget subclasses."""
        opt = QStyleOption()
        opt.initFrom(self)
        p = QPainter(self)
        self.style().drawPrimitive(QStyle.PrimitiveElement.PE_Widget, opt, p, self)
