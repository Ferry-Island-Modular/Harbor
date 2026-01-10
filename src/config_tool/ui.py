from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QButtonGroup,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QVBoxLayout,
    QWidget,
)

from config_tool.lib.serum_converter import MorphType
from config_tool.widgets.axis_morph_selector import AxisMorphSelector
from config_tool.widgets.button_row import ButtonRow
from config_tool.widgets.card_with_button import CardButton
from config_tool.widgets.custom_progress import CustomProgressBar
from config_tool.widgets.file_drop_widget import FileDropWidget
from config_tool.widgets.preview_widget import WavetablePreviewWidget


class MainWindow(QMainWindow):
    files_dropped: Signal = Signal(dict)
    file_cleared: Signal = Signal()
    button_clicked: Signal = Signal()
    mode_changed = Signal(str)
    y_morph_changed = Signal(MorphType)
    z_morph_changed = Signal(MorphType)

    def __init__(self):
        super().__init__()

        self.setWindowTitle("Ferry Island Modular Config Tool")

        title: QLabel = QLabel("Create your own wavetables for Four Seas")
        title.setObjectName("titleLabel")

        subtitle: QLabel = QLabel(
            "You can use your own sound files in .wav format or Serum 32-bit .wav files. The tool will take care of the rest."
        )
        subtitle.setObjectName("subtitleLabel")

        title_container = QVBoxLayout()
        title_container.addWidget(title)
        title_container.addWidget(subtitle)

        title_container.setContentsMargins(0, 0, 0, 32)

        self.preview_widget = WavetablePreviewWidget()

        self.file_drop = FileDropWidget(
            label_text="Drop or browse your audio file", id="x_drop"
        )
        self.file_drop.file_dropped.connect(self.onZoneDrop)
        self.file_drop.file_cleared.connect(self.onZoneClear)
        self.file_drop.hide()
        # self.y_drop = FileDropWidget(label_text="Drop Y file here", id="y_drop")
        # self.z_drop = FileDropWidget(label_text="Drop Z file here", id="z_drop")

        # # Subscribe to each child once, re-emit unified signal
        # for zone in [self.x_drop, self.y_drop, self.z_drop]:
        #     zone.file_dropped.connect(self.onZoneDrop)

        self.progress = CustomProgressBar()
        self.progress.hide()

        drop_container = QHBoxLayout()
        drop_container.addWidget(self.file_drop)
        # drop_container.addWidget(self.y_drop)
        # drop_container.addWidget(self.z_drop)

        # Axis morph selectors (for Serum mode)
        morph_options = [
            MorphType.FORMANT_SCALE,
            MorphType.PHASE_DISPERSE,
            MorphType.SMEAR,
            MorphType.HARMONIC_STRETCH,
        ]

        self.y_morph_selector = AxisMorphSelector("Y", morph_options)
        self.y_morph_selector.morph_changed.connect(self.onYMorphChanged)
        self.y_morph_selector.hide()

        self.z_morph_selector = AxisMorphSelector("Z", morph_options)
        self.z_morph_selector.morph_changed.connect(self.onZMorphChanged)
        self.z_morph_selector.hide()

        morph_selector_container = QHBoxLayout()
        morph_selector_container.addWidget(self.y_morph_selector)
        morph_selector_container.addWidget(self.z_morph_selector)

        mode_chooser_container = QHBoxLayout()
        mode_chooser_container.setSpacing(0)  # Remove gaps between CardButton widgets
        mode_chooser_container.setContentsMargins(0, 0, 0, 0)

        self.single_wav: CardButton = CardButton(
            "Any *.wav file to wavetable", "single-wav"
        )
        self.serum_wav: CardButton = CardButton(
            "Serum *.wav file to wavetable", "serum-wav"
        )
        self.three_wavs: CardButton = CardButton(
            "Three *.wav files to wavetable", "three-wavs"
        )

        # Temp, until implemented
        self.three_wavs.setEnabled(False)
        self.three_wavs.setButtonText("Coming soon!")

        self.button_group: QButtonGroup = QButtonGroup()
        self.button_group.setExclusive(True)

        for card in [self.single_wav, self.serum_wav, self.three_wavs]:
            mode_chooser_container.addWidget(card)
            self.button_group.addButton(card.button)
            self.button_group.buttonToggled.connect(card.updateButton)
            card.button_clicked.connect(self.setMode)

        layout = QVBoxLayout()
        layout.addLayout(title_container)
        layout.addLayout(mode_chooser_container)
        layout.addLayout(drop_container)
        layout.addLayout(morph_selector_container)
        layout.setContentsMargins(32, 32, 32, 32)

        self.button_row = ButtonRow()
        self.button_row.create_wavetable_button.clicked.connect(self.onSubmit)
        layout.addWidget(self.button_row)

        layout.addWidget(self.progress)
        layout.addWidget(self.preview_widget)

        container = QWidget()
        container.setLayout(layout)

        # toolbar = QToolBar("My main toolbar")
        # self.addToolBar(toolbar)

        # menu = self.menuBar()
        # file_menu = menu.addMenu("&File")
        # edit_menu = menu.addMenu("&Edit")
        # help_menu = menu.addMenu("&Help")

        self.setCentralWidget(container)

    def onSubmit(self):
        self.button_clicked.emit()

    def onZoneDrop(self, file_path):
        self.files_dropped.emit(file_path)

    def onZoneClear(self):
        self.file_cleared.emit()

    def setMode(self, mode: str):
        self.mode_changed.emit(mode)
        # Show morph selectors only for Serum mode
        is_serum = mode == "serum-wav"
        self.y_morph_selector.setVisible(is_serum)
        self.z_morph_selector.setVisible(is_serum)

    def onYMorphChanged(self, morph_type: MorphType):
        self.y_morph_changed.emit(morph_type)

    def onZMorphChanged(self, morph_type: MorphType):
        self.z_morph_changed.emit(morph_type)

    # Public API for ConfigApp to control UI state
    def set_create_button_enabled(self, enabled: bool):
        """Enable/disable the create wavetable button"""
        self.button_row.create_wavetable_button.setEnabled(enabled)

    def set_export_button_enabled(self, enabled: bool):
        """Enable/disable the export button"""
        self.button_row.export_wavetable_button.setEnabled(enabled)

    def set_progress(self, value: int):
        """Update progress bar value"""
        self.progress.setValue(value)

    def show_file_drop(self, visible: bool):
        """Show/hide file drop widget"""
        self.file_drop.setVisible(visible)

    def show_progress_bar(self, visible: bool):
        """Show/hide progress bar widget"""
        self.progress.setVisible(visible)

    def get_y_morph(self) -> MorphType:
        """Get the selected Y axis morph type"""
        return self.y_morph_selector.get_selected_morph()

    def get_z_morph(self) -> MorphType:
        """Get the selected Z axis morph type"""
        return self.z_morph_selector.get_selected_morph()
