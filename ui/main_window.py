"""
ui/main_window.py — Top-level window tying the hardware panel and scope to the engine.

A single ~30 fps timer refreshes the display labels and redraws the waveform; the
audio runs independently in the sounddevice callback thread.
"""

from __future__ import annotations

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QMainWindow, QWidget, QVBoxLayout

from ui.hardware_panel import HardwarePanel
from ui.waveform import WaveformView


class MainWindow(QMainWindow):
    def __init__(self, engine):
        super().__init__()
        self.engine = engine
        self.setWindowTitle("NOISFERATU — Python Port")
        self.setStyleSheet("background:#141418;")

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self.panel = HardwarePanel(engine)
        self.waveform = WaveformView(engine)
        layout.addWidget(self.panel)
        layout.addWidget(self.waveform)
        self.setCentralWidget(central)
        self.setFixedWidth(540)

        # UI refresh timer (~30 fps). Audio is on its own thread.
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(33)

        engine.start()

    def _tick(self):
        self.panel.refresh()
        self.waveform.update()

    def closeEvent(self, event):
        self.engine.stop()
        super().closeEvent(event)
