from abc import ABC, abstractmethod

from config_tool.lib.audio_resynthesis import AudioResynthWavetableGenerator
from config_tool.lib.serum_converter import MorphType, SerumWavetableConverter


class WavetableServiceBase(ABC):
    OUTPUT_DIR = "output_waves"
    NUM_SAMPLES = 2048
    NUM_WAVES = 64
    Z_LENGTH = 8

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
        self.converter = SerumWavetableConverter(
            num_waves=self.NUM_WAVES,
            samples=self.NUM_SAMPLES,
            save_path=self.OUTPUT_DIR,
            name="serum_converted",
        )

    def are_files_loaded(self):
        return "x_drop" in self.files

    def set_y_morph(self, morph_type: MorphType):
        """Set the Y axis morph type."""
        self.converter.set_y_morph(morph_type)

    def set_z_morph(self, morph_type: MorphType):
        """Set the Z axis morph type."""
        self.converter.set_z_morph(morph_type)

    def generate(self, progress_callback=None):
        from config_tool.lib.serum_converter import MORPH_TYPE_LABELS
        y_label = MORPH_TYPE_LABELS.get(self.converter.y_morph_type, "Unknown")
        z_label = MORPH_TYPE_LABELS.get(self.converter.z_morph_type, "Unknown")
        print("Generating Serum wavetables with spectral morphing")
        print("  X axis: Source frames")
        print(f"  Y axis: {y_label}")
        print(f"  Z axis: {z_label}")

        serum_file = self.files["x_drop"]
        self.converter.load_wavetable(serum_file)

        for i in range(self.Z_LENGTH):
            page = self.converter.generate_page(i)
            self.converter.save_wavetables(page, f"{i + 1}.wav")
            if progress_callback:
                progress_value = int((100.0 / self.Z_LENGTH) * (i + 1))
                progress_callback(progress_value)


class SingleResynthService(WavetableServiceBase):
    def __init__(self):
        super().__init__()
        self.gen = AudioResynthWavetableGenerator(
            num_waves=self.NUM_WAVES,
            samples=self.NUM_SAMPLES,
            save_path=self.OUTPUT_DIR,
        )

    def are_files_loaded(self):
        return "x_drop" in self.files

    def generate(self, progress_callback=None):
        print("Generating waves")

        for i in range(self.Z_LENGTH):
            wavetables = self.gen.generate_audio_page(i, self.files["x_drop"])

            self.gen.save_wavetables(wavetables, f"{i + 1}.wav")
            if progress_callback:
                progress_value = int((100.0 / self.Z_LENGTH) * (i + 1))
                progress_callback(progress_value)


class TripleResynthService(WavetableServiceBase):
    def __init__(self):
        super().__init__()
        self.gen = AudioResynthWavetableGenerator(
            num_waves=self.NUM_WAVES,
            samples=self.NUM_SAMPLES,
            save_path=self.OUTPUT_DIR,
        )

    def are_files_loaded(self):
        for f in ["x_drop", "y_drop", "z_drop"]:
            if f not in self.files:
                return False
        return True

    def generate(self, progress_callback=None):
        print("Generating waves")

        for i in range(self.Z_LENGTH):
            wavetables = self.gen.generate_multi_audio_page(
                i,
                [
                    self.files["x_drop"],
                    self.files["y_drop"],
                    self.files["z_drop"],
                ],
            )
            self.gen.save_wavetables(wavetables, f"{i + 1}.wav")
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
