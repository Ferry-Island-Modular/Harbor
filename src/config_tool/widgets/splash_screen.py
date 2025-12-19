from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import QSplashScreen, QLabel, QVBoxLayout, QWidget


class SplashScreen(QSplashScreen):
    def __init__(self):
        # Create a simple splash with the app icon
        pixmap = QPixmap(":/assets/app-icon.png").scaled(
            300, 300,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation
        )
        super().__init__(pixmap, Qt.WindowType.WindowStaysOnTopHint)
        
        # Set message style
        self.setStyleSheet("""
            QSplashScreen {
                background-color: #000000;
            }
        """)
        
    def show_message(self, message: str):
        """Show a loading message on the splash screen"""
        self.showMessage(
            message,
            Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignCenter,
            Qt.GlobalColor.white
        )
