"""
Wavetable Preview Widget

Provides audio preview of generated wavetables using the fourseas_preview module.
"""

from pathlib import Path
import tempfile

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QSlider,
    QLabel,
)
from PySide6.QtCore import Qt, Signal

try:
    from fourseas_preview import WavetablePreview
    import sounddevice as sd

    PREVIEW_AVAILABLE = True
except ImportError:
    PREVIEW_AVAILABLE = False


class WavetablePreviewWidget(QWidget):
    """
    Widget for previewing generated wavetables.

    Allows users to:
    - Load generated wavetable banks
    - Set X, Y, Z positions with sliders
    - Preview audio at current position
    - Play sweeps and LFO modulations
    """

    # Signals
    preview_started = Signal()
    preview_finished = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)

        if not PREVIEW_AVAILABLE:
            self._init_unavailable_ui()
            return

        self.preview = WavetablePreview(sample_rate=48000.0)
        self.bank_loaded = False

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

        # Load bank button
        self.load_btn = QPushButton("Load Wavetable Bank")
        self.load_btn.clicked.connect(self._on_load_bank)
        layout.addWidget(self.load_btn)

        # Position sliders
        positions_layout = QVBoxLayout()

        # X slider
        x_layout = QHBoxLayout()
        x_layout.addWidget(QLabel("X:"))
        self.x_slider = QSlider(Qt.Horizontal)
        self.x_slider.setMinimum(0)
        self.x_slider.setMaximum(699)  # 0-6.99 in 0.01 increments
        self.x_slider.setValue(0)
        self.x_label = QLabel("0.00")
        self.x_slider.valueChanged.connect(
            lambda v: self.x_label.setText(f"{v / 100:.2f}")
        )
        x_layout.addWidget(self.x_slider)
        x_layout.addWidget(self.x_label)
        positions_layout.addLayout(x_layout)

        # Y slider
        y_layout = QHBoxLayout()
        y_layout.addWidget(QLabel("Y:"))
        self.y_slider = QSlider(Qt.Horizontal)
        self.y_slider.setMinimum(0)
        self.y_slider.setMaximum(699)
        self.y_slider.setValue(0)
        self.y_label = QLabel("0.00")
        self.y_slider.valueChanged.connect(
            lambda v: self.y_label.setText(f"{v / 100:.2f}")
        )
        y_layout.addWidget(self.y_slider)
        y_layout.addWidget(self.y_label)
        positions_layout.addLayout(y_layout)

        # Z slider
        z_layout = QHBoxLayout()
        z_layout.addWidget(QLabel("Z:"))
        self.z_slider = QSlider(Qt.Horizontal)
        self.z_slider.setMinimum(0)
        self.z_slider.setMaximum(699)
        self.z_slider.setValue(0)
        self.z_label = QLabel("0.00")
        self.z_slider.valueChanged.connect(
            lambda v: self.z_label.setText(f"{v / 100:.2f}")
        )
        z_layout.addWidget(self.z_slider)
        z_layout.addWidget(self.z_label)
        positions_layout.addLayout(z_layout)

        layout.addLayout(positions_layout)

        # Preview buttons
        buttons_layout = QHBoxLayout()

        self.play_btn = QPushButton("Play Position (A4)")
        self.play_btn.setEnabled(False)
        self.play_btn.clicked.connect(self._on_play_position)
        buttons_layout.addWidget(self.play_btn)

        self.sweep_btn = QPushButton("Play Sweep")
        self.sweep_btn.setEnabled(False)
        self.sweep_btn.clicked.connect(self._on_play_sweep)
        buttons_layout.addWidget(self.sweep_btn)

        layout.addLayout(buttons_layout)

        # Status label
        self.status_label = QLabel("No bank loaded")
        self.status_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.status_label)

    def set_bank_path(self, path: str | Path):
        """Set the wavetable bank path and load it."""
        self.bank_path = Path(path)
        self._load_bank()

    def _on_load_bank(self):
        """Handle load bank button click (uses default output path)."""
        # Default to the audio_resynth output directory
        default_path = Path("output_waves/audio_resynth")
        if default_path.exists():
            self.set_bank_path(default_path)
        else:
            self.status_label.setText(
                "No wavetables found in output_waves/audio_resynth"
            )

    def _load_bank(self):
        """Load the wavetable bank."""
        if self.preview.load_bank(str(self.bank_path)):
            self.bank_loaded = True
            self.play_btn.setEnabled(True)
            self.sweep_btn.setEnabled(True)
            self.status_label.setText(f"Loaded: {self.bank_path.name}")
        else:
            self.bank_loaded = False
            self.play_btn.setEnabled(False)
            self.sweep_btn.setEnabled(False)
            self.status_label.setText("Failed to load bank")

    def _get_current_position(self):
        """Get current X, Y, Z position from sliders."""
        x = self.x_slider.value() / 100.0
        y = self.y_slider.value() / 100.0
        z = self.z_slider.value() / 100.0
        return x, y, z

    def _on_play_position(self):
        """Play audio at current X, Y, Z position."""
        if not self.bank_loaded:
            return

        self.preview_started.emit()
        x, y, z = self._get_current_position()

        # Render 2 seconds at A4 (440 Hz)
        audio = self.preview.render_position(
            x=x, y=y, z=z, frequency=60.0, duration=2.0, interpolate=True
        )

        # Play using sounddevice
        sd.play(audio, self.preview.sample_rate)
        sd.wait()

        self.preview_finished.emit()

    def _on_play_sweep(self):
        """Play a sweep through X, Y, Z positions."""
        if not self.bank_loaded:
            return

        self.preview_started.emit()
        x, y, z = self._get_current_position()

        # Sweep from (0,0,0) to current position over 4 seconds
        audio = self.preview.render_sweep(
            x_start=0.0,
            x_end=x,
            y_start=0.0,
            y_end=y,
            z_start=0.0,
            z_end=z,
            frequency=60.0,
            duration=4.0,
            interpolate=True,
        )

        # Play using sounddevice
        sd.play(audio, self.preview.sample_rate)
        sd.wait()

        self.preview_finished.emit()
