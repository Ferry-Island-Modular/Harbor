"""
Wavetable Preview Widget

Provides real-time audio preview of generated wavetables using the fourseas_preview module.
"""

from pathlib import Path

from config_tool.settings import settings
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QSlider,
    QLabel,
    QFrame,
)
from PySide6.QtCore import Qt, Signal, Slot

try:
    from fourseas_preview import RealtimeAudioEngine, PlayMode

    PREVIEW_AVAILABLE = True
except ImportError:
    PREVIEW_AVAILABLE = False
    RealtimeAudioEngine = None  # type: ignore


class WavetablePreviewWidget(QWidget):
    """
    Widget for real-time wavetable preview.

    Provides:
    - X, Y, Z position sliders with real-time parameter updates
    - Pitch slider (MIDI notes 36-96)
    - Steady tone, sweep, and arpeggio playback modes
    """

    # Signals
    preview_started = Signal()
    preview_stopped = Signal()

    # MIDI note range
    MIDI_MIN = 36  # C2
    MIDI_MAX = 96  # C7
    MIDI_DEFAULT = 60  # C4 (Middle C)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("previewWidget")

        if not PREVIEW_AVAILABLE:
            self._init_unavailable_ui()
            return

        self.engine = RealtimeAudioEngine(sample_rate=48000.0)
        self.bank_loaded = False
        self._active_button: QPushButton | None = None

        self._init_ui()

    def _init_unavailable_ui(self):
        """Show message when preview module is not available."""
        layout = QVBoxLayout(self)
        label = QLabel(
            "Preview unavailable\n\nInstall fourseas-preview and sounddevice"
        )
        label.setAlignment(Qt.AlignCenter)
        layout.addWidget(label)

    def _init_ui(self):
        """Initialize the UI components."""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 16, 0, 0)

        # Section title
        title = QLabel("Preview")
        title.setObjectName("previewTitle")
        layout.addWidget(title)

        # Container for controls
        controls_frame = QFrame()
        controls_frame.setObjectName("previewControls")
        controls_layout = QVBoxLayout(controls_frame)
        controls_layout.setContentsMargins(16, 16, 16, 16)
        controls_layout.setSpacing(12)

        # Position sliders section
        positions_layout = QVBoxLayout()
        positions_layout.setSpacing(8)

        # X slider
        x_row = QHBoxLayout()
        x_label = QLabel("X")
        x_label.setObjectName("sliderLabel")
        x_label.setFixedWidth(20)
        self.x_slider = QSlider(Qt.Horizontal)
        self.x_slider.setObjectName("previewSlider")
        self.x_slider.setMinimum(0)
        self.x_slider.setMaximum(699)
        self.x_slider.setValue(0)
        self.x_value = QLabel("0.00")
        self.x_value.setObjectName("sliderValue")
        self.x_value.setFixedWidth(40)
        self.x_slider.valueChanged.connect(self._on_x_changed)
        x_row.addWidget(x_label)
        x_row.addWidget(self.x_slider)
        x_row.addWidget(self.x_value)
        positions_layout.addLayout(x_row)

        # Y slider
        y_row = QHBoxLayout()
        y_label = QLabel("Y")
        y_label.setObjectName("sliderLabel")
        y_label.setFixedWidth(20)
        self.y_slider = QSlider(Qt.Horizontal)
        self.y_slider.setObjectName("previewSlider")
        self.y_slider.setMinimum(0)
        self.y_slider.setMaximum(699)
        self.y_slider.setValue(0)
        self.y_value = QLabel("0.00")
        self.y_value.setObjectName("sliderValue")
        self.y_value.setFixedWidth(40)
        self.y_slider.valueChanged.connect(self._on_y_changed)
        y_row.addWidget(y_label)
        y_row.addWidget(self.y_slider)
        y_row.addWidget(self.y_value)
        positions_layout.addLayout(y_row)

        # Z slider
        z_row = QHBoxLayout()
        z_label = QLabel("Z")
        z_label.setObjectName("sliderLabel")
        z_label.setFixedWidth(20)
        self.z_slider = QSlider(Qt.Horizontal)
        self.z_slider.setObjectName("previewSlider")
        self.z_slider.setMinimum(0)
        self.z_slider.setMaximum(699)
        self.z_slider.setValue(0)
        self.z_value = QLabel("0.00")
        self.z_value.setObjectName("sliderValue")
        self.z_value.setFixedWidth(40)
        self.z_slider.valueChanged.connect(self._on_z_changed)
        z_row.addWidget(z_label)
        z_row.addWidget(self.z_slider)
        z_row.addWidget(self.z_value)
        positions_layout.addLayout(z_row)

        controls_layout.addLayout(positions_layout)

        # Pitch slider
        pitch_row = QHBoxLayout()
        pitch_label = QLabel("Pitch")
        pitch_label.setObjectName("sliderLabel")
        pitch_label.setFixedWidth(40)
        self.pitch_slider = QSlider(Qt.Horizontal)
        self.pitch_slider.setObjectName("previewSlider")
        self.pitch_slider.setMinimum(self.MIDI_MIN)
        self.pitch_slider.setMaximum(self.MIDI_MAX)
        self.pitch_slider.setValue(self.MIDI_DEFAULT)
        self.pitch_value = QLabel(self._midi_to_note_name(self.MIDI_DEFAULT))
        self.pitch_value.setObjectName("sliderValue")
        self.pitch_value.setFixedWidth(40)
        self.pitch_slider.valueChanged.connect(self._on_pitch_changed)
        pitch_row.addWidget(pitch_label)
        pitch_row.addWidget(self.pitch_slider)
        pitch_row.addWidget(self.pitch_value)
        controls_layout.addLayout(pitch_row)

        # Volume slider
        volume_row = QHBoxLayout()
        volume_label = QLabel("Vol")
        volume_label.setObjectName("sliderLabel")
        volume_label.setFixedWidth(40)
        self.volume_slider = QSlider(Qt.Horizontal)
        self.volume_slider.setObjectName("previewSlider")
        self.volume_slider.setMinimum(0)
        self.volume_slider.setMaximum(100)
        self.volume_slider.setValue(100)
        self.volume_value = QLabel("100%")
        self.volume_value.setObjectName("sliderValue")
        self.volume_value.setFixedWidth(40)
        self.volume_slider.valueChanged.connect(self._on_volume_changed)
        volume_row.addWidget(volume_label)
        volume_row.addWidget(self.volume_slider)
        volume_row.addWidget(self.volume_value)
        controls_layout.addLayout(volume_row)

        # Playback buttons
        buttons_layout = QHBoxLayout()
        buttons_layout.setSpacing(8)

        self.steady_btn = QPushButton("Steady Tone")
        self.steady_btn.setObjectName("previewButton")
        self.steady_btn.setCheckable(True)
        self.steady_btn.setEnabled(False)
        self.steady_btn.clicked.connect(self._on_steady_clicked)
        buttons_layout.addWidget(self.steady_btn)

        self.sweep_btn = QPushButton("Sweep")
        self.sweep_btn.setObjectName("previewButton")
        self.sweep_btn.setCheckable(True)
        self.sweep_btn.setEnabled(False)
        self.sweep_btn.clicked.connect(self._on_sweep_clicked)
        buttons_layout.addWidget(self.sweep_btn)

        self.arpeggio_btn = QPushButton("Arpeggio")
        self.arpeggio_btn.setObjectName("previewButton")
        self.arpeggio_btn.setCheckable(True)
        self.arpeggio_btn.setEnabled(False)
        self.arpeggio_btn.clicked.connect(self._on_arpeggio_clicked)
        buttons_layout.addWidget(self.arpeggio_btn)

        controls_layout.addLayout(buttons_layout)

        # Status label
        self.status_label = QLabel("No wavetable bank loaded")
        self.status_label.setObjectName("previewStatus")
        self.status_label.setAlignment(Qt.AlignCenter)
        controls_layout.addWidget(self.status_label)

        layout.addWidget(controls_frame)

        # Restore saved settings
        saved_volume = settings.preview_volume
        self.volume_slider.setValue(saved_volume)
        self.engine.set_volume(saved_volume / 100.0)

        saved_device = settings.audio_device
        if saved_device is not None:
            self.engine.set_device(saved_device)

    def _midi_to_note_name(self, midi_note: int) -> str:
        """Convert MIDI note number to note name (e.g., C4)."""
        note_names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        octave = (midi_note // 12) - 1
        note = note_names[midi_note % 12]
        return f"{note}{octave}"

    @Slot(int)
    def _on_x_changed(self, value: int):
        """Handle X slider change."""
        x = value / 100.0
        self.x_value.setText(f"{x:.2f}")
        if self.bank_loaded:
            self.engine.set_x(x)

    @Slot(int)
    def _on_y_changed(self, value: int):
        """Handle Y slider change."""
        y = value / 100.0
        self.y_value.setText(f"{y:.2f}")
        if self.bank_loaded:
            self.engine.set_y(y)

    @Slot(int)
    def _on_z_changed(self, value: int):
        """Handle Z slider change."""
        z = value / 100.0
        self.z_value.setText(f"{z:.2f}")
        if self.bank_loaded:
            self.engine.set_z(z)

    @Slot(int)
    def _on_pitch_changed(self, value: int):
        """Handle pitch slider change."""
        self.pitch_value.setText(self._midi_to_note_name(value))
        if self.bank_loaded:
            self.engine.set_midi_note(value)

    @Slot(int)
    def _on_volume_changed(self, value: int):
        """Handle volume slider change."""
        self.volume_value.setText(f"{value}%")
        settings.preview_volume = value
        if PREVIEW_AVAILABLE:
            self.engine.set_volume(value / 100.0)

    def _get_current_position(self) -> tuple[float, float, float]:
        """Get current X, Y, Z position from sliders."""
        x = self.x_slider.value() / 100.0
        y = self.y_slider.value() / 100.0
        z = self.z_slider.value() / 100.0
        return x, y, z

    def _stop_all_playback(self):
        """Stop playback and uncheck all buttons."""
        if self.engine.is_playing():
            self.engine.stop()

        self.steady_btn.setChecked(False)
        self.sweep_btn.setChecked(False)
        self.arpeggio_btn.setChecked(False)
        self._active_button = None
        self.preview_stopped.emit()

    def _start_playback(self, mode: PlayMode, button: QPushButton):
        """Start playback with the specified mode."""
        # Stop any current playback
        if self.engine.is_playing():
            self.engine.stop()

        # Uncheck other buttons
        for btn in [self.steady_btn, self.sweep_btn, self.arpeggio_btn]:
            if btn != button:
                btn.setChecked(False)

        # Configure engine
        self.engine.set_mode(mode)

        if mode == PlayMode.SWEEP:
            x, y, z = self._get_current_position()
            self.engine.set_sweep_target(x, y, z, duration=4.0)
            self.engine.set_on_sweep_complete(self._on_sweep_complete)

        # Start playback
        self._active_button = button
        self.engine.start()
        self.preview_started.emit()

    @Slot()
    def _on_steady_clicked(self):
        """Handle steady tone button click."""
        if self.steady_btn.isChecked():
            self._start_playback(PlayMode.STEADY, self.steady_btn)
        else:
            self._stop_all_playback()

    @Slot()
    def _on_sweep_clicked(self):
        """Handle sweep button click."""
        if self.sweep_btn.isChecked():
            self._start_playback(PlayMode.SWEEP, self.sweep_btn)
        else:
            self._stop_all_playback()

    @Slot()
    def _on_arpeggio_clicked(self):
        """Handle arpeggio button click."""
        if self.arpeggio_btn.isChecked():
            self._start_playback(PlayMode.ARPEGGIO, self.arpeggio_btn)
        else:
            self._stop_all_playback()

    def _on_sweep_complete(self):
        """Called when sweep completes - switch to steady mode."""
        # This is called from a background thread, so we just update state
        # The button will stay checked but mode switches to steady
        pass

    def set_bank_path(self, path: str | Path):
        """Set the wavetable bank path and load it."""
        if not PREVIEW_AVAILABLE:
            return

        path = Path(path)
        if self.engine.load_bank(str(path)):
            self.bank_loaded = True
            self.steady_btn.setEnabled(True)
            self.sweep_btn.setEnabled(True)
            self.arpeggio_btn.setEnabled(True)
            self.status_label.setText(f"Loaded: {path.name}")

            # Initialize engine with current slider values
            x, y, z = self._get_current_position()
            self.engine.set_position(x, y, z)
            self.engine.set_midi_note(self.pitch_slider.value())
        else:
            self.bank_loaded = False
            self.steady_btn.setEnabled(False)
            self.sweep_btn.setEnabled(False)
            self.arpeggio_btn.setEnabled(False)
            self.status_label.setText("Failed to load bank")

    def stop_preview(self):
        """Stop any active preview playback."""
        if PREVIEW_AVAILABLE:
            self._stop_all_playback()

    def set_audio_device(self, device_index: int | None):
        """
        Set the audio output device.

        Args:
            device_index: Device index or None for system default
        """
        if not PREVIEW_AVAILABLE:
            return

        was_playing = self.engine.is_playing()
        if was_playing:
            self.engine.stop()

        self.engine.set_device(device_index)

        if was_playing:
            self.engine.start()

    def set_volume(self, volume: int):
        """Set preview volume (0-100)."""
        if PREVIEW_AVAILABLE:
            self.volume_slider.setValue(volume)
            self.engine.set_volume(volume / 100.0)

    def get_volume(self) -> int:
        """Get current volume (0-100)."""
        return self.volume_slider.value() if PREVIEW_AVAILABLE else 100

    @staticmethod
    def get_audio_devices() -> list[dict]:
        """Get list of available audio output devices."""
        if not PREVIEW_AVAILABLE or RealtimeAudioEngine is None:
            return []
        return RealtimeAudioEngine.query_devices()
