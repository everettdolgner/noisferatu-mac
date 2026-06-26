"""
ui/waveform.py — Oscilloscope view of the engine's post-effects output.

Polls engine.scope (the most recent rendered block) and draws it as a glowing
green trace, evoking a small lab scope sitting next to the hardware.
"""

from __future__ import annotations

import numpy as np
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QPainter, QPen, QPolygonF
from PyQt6.QtCore import QPointF
from PyQt6.QtWidgets import QWidget


class WaveformView(QWidget):
    def __init__(self, engine, parent=None):
        super().__init__(parent)
        self.engine = engine
        self.setMinimumHeight(120)

    def paintEvent(self, event):  # noqa: ARG002
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()

        # Bezel / screen
        p.fillRect(self.rect(), QColor("#0a0f0a"))
        p.setPen(QPen(QColor("#1d2a1d"), 1))
        mid = h / 2
        p.drawLine(0, int(mid), w, int(mid))

        data = self.engine.scope
        n = len(data)
        if n < 2:
            return

        # Downsample to the pixel width for a clean trace.
        step = max(1, n // max(1, w))
        ds = data[::step]
        xs = np.linspace(0, w, len(ds))
        ys = mid - ds * (h * 0.45)

        poly = QPolygonF([QPointF(float(x), float(y)) for x, y in zip(xs, ys)])
        pen = QPen(QColor("#39ff7a"), 1.6)
        p.setPen(pen)
        p.drawPolyline(poly)
