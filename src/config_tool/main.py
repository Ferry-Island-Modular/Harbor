import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication

from config_tool.ui import MainWindow
from config_tool.lib.audio_resynthesis import AudioResynthWavetableGenerator


class ConfigApp:
    OUTPUT_DIR = "output_waves"
    NUM_SAMPLES = 2048
    NUM_WAVES = 64
    Z_LENGTH = 8

    def __init__(self):
        self.ui = MainWindow()
        self.gen = AudioResynthWavetableGenerator(
            num_waves=self.NUM_WAVES,
            samples=self.NUM_SAMPLES,
            save_path=self.OUTPUT_DIR,
        )

        self.files = {}
        self.ui.button.setEnabled(self._files_are_loaded())

        # Connect signals
        self.ui.files_dropped.connect(self.handle_files_dropped)
        self.ui.button_clicked.connect(self.handle_generate_wave)

    def _files_are_loaded(self):
        for f in ["x_drop", "y_drop", "z_drop"]:
            if f not in self.files:
                return False
        return True

    def handle_files_dropped(self, file_list):
        self.files[file_list["id"]] = file_list["paths"][0]
        print(self.files)
        self.ui.button.setEnabled(self._files_are_loaded())

    def handle_generate_wave(self):
        print("Generating waves")
        self.ui.progress.setValue(0)
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
            print((100.0 / self.Z_LENGTH) * (i + 1))
            self.ui.progress.setValue((100.0 / self.Z_LENGTH) * (i + 1))

        print("Done!")

    def run(self):
        self.ui.show()


def app():
    qt_app = QApplication(sys.argv)
    qt_app.setApplicationName("Ferry Island Modular Config Tool")

    # Load stylesheet from the same directory as this file
    stylesheet_path = Path(__file__).parent / "app.qss"
    if stylesheet_path.exists():
        qt_app.setStyleSheet(stylesheet_path.read_text())

    app = ConfigApp()
    app.run()

    sys.exit(qt_app.exec())


if __name__ == "__main__":
    app()
