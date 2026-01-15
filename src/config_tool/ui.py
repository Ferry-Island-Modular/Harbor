from PySide6.QtCore import Signal
from PySide6.QtGui import QAction, QActionGroup
from PySide6.QtWidgets import (
    QButtonGroup,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMenu,
    QVBoxLayout,
    QWidget,
)

from config_tool.lib.serum_converter import MorphType
from config_tool.settings import AppSettings, settings
from config_tool.widgets.axis_morph_selector import AxisMorphSelectorWrapper
from config_tool.widgets.button_row import ButtonRow
from config_tool.widgets.card_with_button import CardButton
from config_tool.widgets.custom_progress import CustomProgressBar
from config_tool.widgets.file_drop_widget import FileDropWrapper
from config_tool.widgets.preview_widget import WavetablePreviewWidget


class MainWindow(QMainWindow):
    files_dropped: Signal = Signal(dict)
    file_cleared: Signal = Signal()
    button_clicked: Signal = Signal()
    mode_changed = Signal(str)
    y_morph_changed = Signal(MorphType)
    z_morph_changed = Signal(MorphType)
    output_dir_changed = Signal(str)
    samples_per_frame_changed = Signal(int)

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

        self.file_drop = FileDropWrapper()

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

        self.y_morph_selector = AxisMorphSelectorWrapper("Y", morph_options)
        self.y_morph_selector.set_selected_morph(
            settings.y_morph
        )  # Restore from settings
        self.y_morph_selector.morph_changed.connect(self.onYMorphChanged)
        self.y_morph_selector.hide()

        self.z_morph_selector = AxisMorphSelectorWrapper("Z", morph_options)
        self.z_morph_selector.set_selected_morph(
            settings.z_morph
        )  # Restore from settings
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
        layout.setSpacing(0)

        self.button_row = ButtonRow()
        self.button_row.create_wavetable_button.clicked.connect(self.onSubmit)
        layout.addWidget(self.button_row)

        layout.addWidget(self.progress)
        layout.addWidget(self.preview_widget)

        container = QWidget()
        container.setLayout(layout)

        # Create menu bar
        self._create_menu_bar()

        self.setCentralWidget(container)

    def _create_menu_bar(self):
        """Create the application menu bar."""
        menu_bar = self.menuBar()

        # File menu
        file_menu: QMenu = menu_bar.addMenu("&File")

        set_output_dir_action = QAction("Set Output Directory...", self)
        set_output_dir_action.triggered.connect(self._on_set_output_dir)
        file_menu.addAction(set_output_dir_action)

        # Audio menu
        self.audio_menu: QMenu = menu_bar.addMenu("&Audio")
        self.audio_menu.aboutToShow.connect(self._populate_audio_menu)
        self._populate_audio_menu()  # Populate initially so menu shows on macOS

        # Settings menu
        settings_menu: QMenu = menu_bar.addMenu("&Settings")

        # Samples per frame submenu
        samples_menu: QMenu = settings_menu.addMenu("Samples per Frame")
        self.samples_action_group = QActionGroup(self)
        self.samples_action_group.setExclusive(True)

        current_samples = settings.samples_per_frame

        for samples, label in AppSettings.SAMPLE_PRESETS.items():
            action = QAction(label, self)
            action.setCheckable(True)
            action.setChecked(samples == current_samples)
            action.setData(samples)
            action.triggered.connect(
                lambda checked, s=samples: self._on_samples_changed(s)
            )
            self.samples_action_group.addAction(action)
            samples_menu.addAction(action)

    def _on_set_output_dir(self):
        """Handle output directory selection."""
        current_dir = settings.output_dir
        directory = QFileDialog.getExistingDirectory(
            self,
            "Select Output Directory",
            current_dir,
            QFileDialog.Option.ShowDirsOnly | QFileDialog.Option.DontResolveSymlinks,
        )
        if directory:
            settings.output_dir = directory
            self.output_dir_changed.emit(directory)

    def _on_samples_changed(self, samples: int):
        """Handle samples per frame selection."""
        settings.samples_per_frame = samples
        self.samples_per_frame_changed.emit(samples)

    def _populate_audio_menu(self):
        """Populate the audio menu with available devices."""
        self.audio_menu.clear()

        devices = self.preview_widget.get_audio_devices()
        current_device = settings.audio_device

        if not devices:
            no_devices_action = QAction("No audio devices available", self)
            no_devices_action.setEnabled(False)
            self.audio_menu.addAction(no_devices_action)
            return

        audio_action_group = QActionGroup(self)
        audio_action_group.setExclusive(True)

        # System default option
        default_action = QAction("System Default", self)
        default_action.setCheckable(True)
        default_action.setChecked(current_device is None)
        default_action.triggered.connect(lambda: self._on_audio_device_changed(None))
        audio_action_group.addAction(default_action)
        self.audio_menu.addAction(default_action)

        self.audio_menu.addSeparator()

        # Individual devices
        for device in devices:
            label = device["name"]
            if device["is_default"]:
                label += " (Default)"

            action = QAction(label, self)
            action.setCheckable(True)
            action.setChecked(current_device == device["index"])
            action.triggered.connect(
                lambda checked, idx=device["index"]: self._on_audio_device_changed(idx)
            )
            audio_action_group.addAction(action)
            self.audio_menu.addAction(action)

    def _on_audio_device_changed(self, device_index: int | None):
        """Handle audio device selection."""
        settings.audio_device = device_index
        self.preview_widget.set_audio_device(device_index)

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
        for card in [self.single_wav, self.serum_wav, self.three_wavs]:
            card.setProperty("tableOpen", visible)
            card.style().unpolish(card)
            card.style().polish(card)
            card.update()

    def show_progress_bar(self, visible: bool):
        """Show/hide progress bar widget"""
        self.progress.setVisible(visible)

    def get_y_morph(self) -> MorphType:
        """Get the selected Y axis morph type"""
        return self.y_morph_selector.get_selected_morph()

    def get_z_morph(self) -> MorphType:
        """Get the selected Z axis morph type"""
        return self.z_morph_selector.get_selected_morph()
