import sys
import traceback
from pathlib import Path

from PySide6.QtCore import QFile, QIODevice, QObject, QRunnable, QThreadPool, Signal, Slot
from PySide6.QtWidgets import QApplication

import config_tool.assets_rc  # noqa: F401 - Import registers Qt resources
from config_tool.service import WavetableServiceBase, WavetableServiceFactory
from config_tool.ui import MainWindow
from config_tool.widgets.splash_screen import SplashScreen


class WorkerSignals(QObject):
    finished = Signal()
    error = Signal(tuple)
    result = Signal(object)
    progress = Signal(float)


class Worker(QRunnable):
    def __init__(self, fn, *args, **kwargs):
        super().__init__()
        self.fn = fn
        self.args = args
        self.kwargs = kwargs
        self.signals = WorkerSignals()

        # Provide a callback function that emits our signal
        def progress_callback(value):
            self.signals.progress.emit(value)

        self.kwargs["progress_callback"] = progress_callback

    @Slot()
    def run(self):
        try:
            self.fn(*self.args, **self.kwargs)
        except Exception:
            traceback.print_exc()
            exctype, value = sys.exc_info()[:2]
            self.signals.error.emit((exctype, value, traceback.format_exc()))
        finally:
            self.signals.finished.emit()


class UIControlSignals(QObject):
    """Signals for controlling UI state"""

    set_create_button_enabled = Signal(bool)
    set_export_button_enabled = Signal(bool)
    set_progress = Signal(int)
    show_progress_bar = Signal(bool)
    show_file_drop = Signal(bool)


class ConfigApp:
    def __init__(self):
        self.ui = MainWindow()
        self.ui_signals = UIControlSignals()

        self.mode: str | None = None
        self.service: WavetableServiceBase | None = None

        # Connect UI control signals (Controller → View)
        self.ui_signals.set_create_button_enabled.connect(
            self.ui.set_create_button_enabled
        )
        self.ui_signals.set_export_button_enabled.connect(
            self.ui.set_export_button_enabled
        )
        self.ui_signals.show_progress_bar.connect(self.ui.show_progress_bar)
        self.ui_signals.set_progress.connect(self.ui.set_progress)
        self.ui_signals.show_file_drop.connect(self.ui.show_file_drop)

        # Connect user action signals (View → Controller)
        self.ui.files_dropped.connect(self.handle_files_dropped)
        self.ui.file_cleared.connect(self.handle_file_cleared)
        self.ui.button_clicked.connect(self.handle_generate_waves)
        self.ui.mode_changed.connect(self.handle_mode_changed)

        # Set initial UI state
        self.ui_signals.set_create_button_enabled.emit(False)

        self.threadpool = QThreadPool()
        thread_count = self.threadpool.maxThreadCount()
        print(f"Multithreading with maximum {thread_count} threads")

    def handle_files_dropped(self, file_list):
        if self.service:
            self.service.add_file(file_list["id"], file_list["paths"][0])
            self.ui_signals.set_create_button_enabled.emit(
                self.service.are_files_loaded()
            )

    def handle_file_cleared(self):
        """Handle when user clears a file"""
        # Clear all files (since we only have one drop zone currently)
        if self.service:
            self.service.clear_files()

        # Disable button since files are gone
        self.ui_signals.set_create_button_enabled.emit(False)

    def handle_mode_changed(self, mode):
        self.mode = mode
        if self.mode:
            self.service = WavetableServiceFactory.create(mode)
            self.ui_signals.show_file_drop.emit(True)

    def on_generate_waves_done(self):
        # Update UI via signals
        self.ui_signals.show_progress_bar.emit(False)
        self.ui_signals.set_progress.emit(0)
        self.ui_signals.set_create_button_enabled.emit(True)
        self.ui_signals.set_export_button_enabled.emit(True)  # Can now export

    def progress_fn(self, progress_value):
        # Update UI via signals
        self.ui_signals.set_progress.emit(progress_value)

    def handle_generate_waves(self):
        """Delegate to service"""
        if not self.service or not self.service.are_files_loaded():
            return

        self.ui_signals.set_create_button_enabled.emit(False)
        self.ui_signals.show_progress_bar.emit(True)

        worker = Worker(self.service.generate)
        worker.signals.finished.connect(self.on_generate_waves_done)
        worker.signals.progress.connect(self.progress_fn)
        self.threadpool.start(worker)

    def run(self):
        self.ui.show()


def app():
    qt_app = QApplication(sys.argv)
    qt_app.setApplicationName("Ferry Island Modular Config Tool")

    # Show splash screen
    splash = SplashScreen()
    splash.show()
    splash.show_message("Loading...")
    qt_app.processEvents()  # Ensure splash is rendered

    # Load stylesheet from Qt resources
    splash.show_message("Loading stylesheet...")
    qt_app.processEvents()
    qss_file = QFile(":/app.qss")
    if qss_file.open(QIODevice.ReadOnly | QIODevice.Text):
        qt_app.setStyleSheet(str(qss_file.readAll(), encoding='utf-8'))
        qss_file.close()

    # Initialize application
    splash.show_message("Initializing...")
    qt_app.processEvents()
    app = ConfigApp()

    # Close splash and show main window
    splash.finish(app.ui)
    app.run()

    sys.exit(qt_app.exec())


if __name__ == "__main__":
    app()
