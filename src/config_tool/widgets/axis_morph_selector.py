from typing import override

from PySide6.QtCore import QSize, Signal
from PySide6.QtGui import QIcon, QPainter
from PySide6.QtWidgets import (
    QButtonGroup,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QStyle,
    QStyleOption,
    QVBoxLayout,
    QWidget,
)

from config_tool.lib.serum_converter import MORPH_TYPE_LABELS, MorphType


class AxisMorphSelectorWrapper(QWidget):
    morph_changed: Signal = Signal(MorphType)

    def __init__(self, axis_label: str, morph_options: list[MorphType], parent=None):
        super().__init__(parent)
        self.setObjectName(f"axis-morph-wrapper-{axis_label.lower()}")

        self.selector = AxisMorphSelector(axis_label, morph_options, self)
        self.selector.morph_changed.connect(self.morph_changed)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)
        layout.setSpacing(0)
        layout.addWidget(self.selector)

    def get_selected_morph(self) -> MorphType:
        return self.selector.get_selected_morph()

    def set_selected_morph(self, morph_type: MorphType):
        self.selector.set_selected_morph(morph_type)

    @override
    def paintEvent(self, event):
        """Required for QSS styling to work on custom QWidget subclasses."""
        opt = QStyleOption()
        opt.initFrom(self)
        p = QPainter(self)
        self.style().drawPrimitive(QStyle.PrimitiveElement.PE_Widget, opt, p, self)


class AxisMorphSelector(QWidget):
    """
    Widget for selecting spectral morph type for an axis (Y or Z).

    Displays a label and a row of toggle buttons for morph selection.
    """

    morph_changed: Signal = Signal(MorphType)

    def __init__(self, axis_label: str, morph_options: list[MorphType], parent=None):
        """
        Initialize the axis morph selector.

        Args:
            axis_label: Label for the axis (e.g., "Y" or "Z")
            morph_options: List of MorphType options to show
            parent: Parent widget
        """
        super().__init__(parent)
        self.setObjectName(f"axis-morph-selector-{axis_label.lower()}")

        self.morph_options = morph_options
        self.buttons: dict[MorphType, QPushButton] = {}

        # Label
        self.label = QLabel(f"Choose {axis_label} axis action")
        self.label.setObjectName("axisMorphLabel")

        # Button group for exclusive selection
        self.button_group = QButtonGroup(self)
        self.button_group.setExclusive(True)

        # Create buttons container
        button_container = QHBoxLayout()

        for i, morph_type in enumerate(morph_options):
            button = QPushButton(MORPH_TYPE_LABELS[morph_type])
            button.setCheckable(True)
            button.setIconSize(QSize(16, 16))

            button.setObjectName("morphButton")

            self.button_group.addButton(button)
            button_container.addWidget(button)
            self.buttons[morph_type] = button

            # Connect to update handler
            button.toggled.connect(
                lambda checked, mt=morph_type: self._on_button_toggled(mt, checked)
            )

        # Select first option by default
        if morph_options:
            first_button = self.buttons[morph_options[0]]
            first_button.setChecked(True)
            self._update_button_appearance(first_button, True)

        # Layout
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)
        layout.addWidget(self.label)
        layout.addLayout(button_container)

    def _on_button_toggled(self, morph_type: MorphType, checked: bool):
        """Handle button toggle."""
        button = self.buttons[morph_type]
        self._update_button_appearance(button, checked)

        if checked:
            self.morph_changed.emit(morph_type)

    def _update_button_appearance(self, button: QPushButton, checked: bool):
        """Update button text and icon based on checked state."""
        morph_type = None
        for mt, btn in self.buttons.items():
            if btn == button:
                morph_type = mt
                break

        if morph_type is None:
            return

        label = MORPH_TYPE_LABELS[morph_type]
        if checked:
            button.setText(label)
            button.setIcon(QIcon(":/assets/check.svg"))
        else:
            button.setText(label)
            button.setIcon(QIcon())

    def get_selected_morph(self) -> MorphType:
        """Get the currently selected morph type."""
        for morph_type, button in self.buttons.items():
            if button.isChecked():
                return morph_type
        return self.morph_options[0] if self.morph_options else None

    def set_selected_morph(self, morph_type: MorphType):
        """Set the selected morph type."""
        if morph_type in self.buttons:
            self.buttons[morph_type].setChecked(True)

    @override
    def paintEvent(self, event):
        """Required for QSS styling to work on custom QWidget subclasses."""
        opt = QStyleOption()
        opt.initFrom(self)
        p = QPainter(self)
        self.style().drawPrimitive(QStyle.PrimitiveElement.PE_Widget, opt, p, self)
