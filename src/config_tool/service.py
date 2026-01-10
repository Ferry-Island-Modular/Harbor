from abc import ABC, abstractmethod

from config_tool.lib.audio_resynthesis import AudioResynthWavetableGenerator
from config_tool.lib.serum_converter import MorphType, SerumWavetableConverter
from config_tool.settings import settings


class WavetableServiceBase(ABC):
    NUM_WAVES = 64
    Z_LENGTH = 8

    @property
    def output_dir(self) -> str:
        """Get output directory from settings."""
        return settings.output_dir

    @property
    def num_samples(self) -> int:
        """Get samples per frame from settings."""
        return settings.samples_per_frame

    def __init__(self):
        self.files = {}

    def add_file(self, file_id: str, file_path: str):
        """Add a file to the service"""
        self.files[file_id] = file_path

    def clear_files(self):
        """Clear all loaded files"""
        self.files.clear()

    @abstractmethod
    def are_files_loaded(self) -> bool:
        """Check if all required files are loaded"""
        pass

    @abstractmethod
    def generate(self, progress_callback=None):
        """Generate wavetables - must be implemented by subclasses"""
        pass


class SerumService(WavetableServiceBase):
    def __init__(self):
        super().__init__()
        self._y_morph = MorphType.FORMANT_SCALE
        self._z_morph = MorphType.PHASE_DISPERSE

    def are_files_loaded(self):
        return "x_drop" in self.files

    def set_y_morph(self, morph_type: MorphType):
        """Set the Y axis morph type."""
        self._y_morph = morph_type

    def set_z_morph(self, morph_type: MorphType):
        """Set the Z axis morph type."""
        self._z_morph = morph_type

    def generate(self, progress_callback=None):
        from config_tool.lib.serum_converter import MORPH_TYPE_LABELS

        # Create converter with current settings
        converter = SerumWavetableConverter(
            num_waves=self.NUM_WAVES,
            samples=self.num_samples,
            save_path=self.output_dir,
            name="serum_converted",
        )
        converter.set_y_morph(self._y_morph)
        converter.set_z_morph(self._z_morph)

        y_label = MORPH_TYPE_LABELS.get(self._y_morph, "Unknown")
        z_label = MORPH_TYPE_LABELS.get(self._z_morph, "Unknown")
        print("Generating Serum wavetables with spectral morphing")
        print(f"  Output: {self.output_dir}")
        print(f"  Samples per frame: {self.num_samples}")
        print("  X axis: Source frames")
        print(f"  Y axis: {y_label}")
        print(f"  Z axis: {z_label}")

        serum_file = self.files["x_drop"]
        converter.load_wavetable(serum_file)

        for i in range(self.Z_LENGTH):
            page = converter.generate_page(i)
            converter.save_wavetables(page, f"{i + 1}.wav")
            if progress_callback:
                progress_value = int((100.0 / self.Z_LENGTH) * (i + 1))
                progress_callback(progress_value)


class SingleResynthService(WavetableServiceBase):
    def __init__(self):
        super().__init__()

    def are_files_loaded(self):
        return "x_drop" in self.files

    def generate(self, progress_callback=None):
        # Create generator with current settings
        gen = AudioResynthWavetableGenerator(
            num_waves=self.NUM_WAVES,
            samples=self.num_samples,
            save_path=self.output_dir,
        )

        print("Generating waves")
        print(f"  Output: {self.output_dir}")
        print(f"  Samples per frame: {self.num_samples}")

        for i in range(self.Z_LENGTH):
            wavetables = gen.generate_audio_page(i, self.files["x_drop"])
            gen.save_wavetables(wavetables, f"{i + 1}.wav")
            if progress_callback:
                progress_value = int((100.0 / self.Z_LENGTH) * (i + 1))
                progress_callback(progress_value)


class TripleResynthService(WavetableServiceBase):
    def __init__(self):
        super().__init__()

    def are_files_loaded(self):
        for f in ["x_drop", "y_drop", "z_drop"]:
            if f not in self.files:
                return False
        return True

    def generate(self, progress_callback=None):
        # Create generator with current settings
        gen = AudioResynthWavetableGenerator(
            num_waves=self.NUM_WAVES,
            samples=self.num_samples,
            save_path=self.output_dir,
        )

        print("Generating waves")
        print(f"  Output: {self.output_dir}")
        print(f"  Samples per frame: {self.num_samples}")

        for i in range(self.Z_LENGTH):
            wavetables = gen.generate_multi_audio_page(
                i,
                [
                    self.files["x_drop"],
                    self.files["y_drop"],
                    self.files["z_drop"],
                ],
            )
            gen.save_wavetables(wavetables, f"{i + 1}.wav")
            if progress_callback:
                progress_value = int((100.0 / self.Z_LENGTH) * (i + 1))
                progress_callback(progress_value)


class WavetableServiceFactory:
    """Factory for creating wavetable services"""

    _services = {
        "single-wav": SingleResynthService,
        "serum-wav": SerumService,
        "three-wavs": TripleResynthService,
    }

    @classmethod
    def create(cls, mode: str) -> WavetableServiceBase:
        service_class = cls._services.get(mode)
        if not service_class:
            raise ValueError(f"Unknown mode: {mode}")
        return service_class()
