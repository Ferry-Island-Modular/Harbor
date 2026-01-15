from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QWidget


class ButtonRow(QWidget):
    def __init__(self) -> None:
        super().__init__()

        self.create_wavetable_button: QPushButton = QPushButton("Create wavetable")
        self.create_wavetable_button.setEnabled(False)

        self.export_wavetable_button: QPushButton = QPushButton("Export wavetable")
        self.export_wavetable_button.setEnabled(False)
        # Temporarily hide
        self.export_wavetable_button.hide()

        self.label: QLabel = QLabel(
            "Drop file and choose Y/Z actions to generate and export wavetables."
        )
        self.label.setObjectName("buttonRowLabel")

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 12, 0, 12)
        layout.addWidget(self.create_wavetable_button)
        layout.addWidget(self.export_wavetable_button)
        layout.addWidget(self.label)

        self.setLayout(layout)
