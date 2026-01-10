"""
Application settings manager using QSettings for persistence.
"""

from pathlib import Path

from PySide6.QtCore import QSettings

from config_tool.lib.serum_converter import MorphType


class AppSettings:
    """
    Manages application settings with automatic persistence.

    Uses QSettings for cross-platform storage (Registry on Windows,
    plist on macOS, config files on Linux).
    """

    # Default values
    DEFAULT_OUTPUT_DIR = "output_waves"
    DEFAULT_SAMPLES_PER_FRAME = 2048
    DEFAULT_MODE = None
    DEFAULT_Y_MORPH = MorphType.FORMANT_SCALE
    DEFAULT_Z_MORPH = MorphType.PHASE_DISPERSE

    # Known sample rates with labels
    SAMPLE_PRESETS = {
        2048: "2048 (Four Seas)",
        256: "256 (Piston Honda)",
    }

    def __init__(self):
        self._settings = QSettings("FerryIslandModular", "FIMConfigTool")

    # Output Directory
    @property
    def output_dir(self) -> str:
        return self._settings.value("output_dir", self.DEFAULT_OUTPUT_DIR)

    @output_dir.setter
    def output_dir(self, value: str):
        self._settings.setValue("output_dir", value)

    # Samples per Frame
    @property
    def samples_per_frame(self) -> int:
        return int(self._settings.value("samples_per_frame", self.DEFAULT_SAMPLES_PER_FRAME))

    @samples_per_frame.setter
    def samples_per_frame(self, value: int):
        self._settings.setValue("samples_per_frame", value)

    # Mode
    @property
    def mode(self) -> str | None:
        value = self._settings.value("mode", self.DEFAULT_MODE)
        return value if value else None

    @mode.setter
    def mode(self, value: str | None):
        self._settings.setValue("mode", value if value else "")

    # Y Morph Type
    @property
    def y_morph(self) -> MorphType:
        value = self._settings.value("y_morph", self.DEFAULT_Y_MORPH.value)
        try:
            return MorphType(value)
        except ValueError:
            return self.DEFAULT_Y_MORPH

    @y_morph.setter
    def y_morph(self, value: MorphType):
        self._settings.setValue("y_morph", value.value)

    # Z Morph Type
    @property
    def z_morph(self) -> MorphType:
        value = self._settings.value("z_morph", self.DEFAULT_Z_MORPH.value)
        try:
            return MorphType(value)
        except ValueError:
            return self.DEFAULT_Z_MORPH

    @z_morph.setter
    def z_morph(self, value: MorphType):
        self._settings.setValue("z_morph", value.value)

    def get_output_path(self) -> Path:
        """Get the output directory as a Path object."""
        return Path(self.output_dir)

    def sync(self):
        """Force sync settings to storage."""
        self._settings.sync()


# Global settings instance
settings = AppSettings()
