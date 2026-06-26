"""
ui/hardware_panel.py — Virtual recreation of the NOISFERATU front panel.

Five rotary knobs (POT1, POT2, BITCRUSH, RATE, VOL), three buttons (PREV, ALGO,
BANK), a 4-digit red 7-segment display showing bank.algo, and bank/algo name labels.
Knobs drag vertically; the display mirrors the engine's display_text.
"""

from __future__ import annotations

import math

from PyQt6.QtCore import Qt, pyqtSignal, QRectF, QPointF
from PyQt6.QtGui import QColor, QFont, QPainter, QPen, QBrush, QRadialGradient
from PyQt6.QtWidgets import (
    QWidget, QLabel, QPushButton, QVBoxLayout, QHBoxLayout, QGridLayout,
)

ACCENT = "#e8a23d"      # amber knob indicator
LED_ON = QColor("#ff3b2f")
LED_OFF = QColor("#3a1410")
PANEL = "#1a1a1f"


# ====================================================================== Knob
class Knob(QWidget):
    valueChanged = pyqtSignal(float)

    def __init__(self, label: str, value: float = 0.5, parent=None):
        super().__init__(parent)
        self._value = max(0.0, min(1.0, value))
        self._label = label
        self._drag_y = 0
        self._drag_v = 0.0
        self.setFixedSize(86, 104)
        self.setCursor(Qt.CursorShape.SizeVerCursor)

    def value(self) -> float:
        return self._value

    def setValue(self, v: float):
        v = max(0.0, min(1.0, v))
        if v != self._value:
            self._value = v
            self.update()
            self.valueChanged.emit(v)

    def mousePressEvent(self, e):
        self._drag_y = e.position().y()
        self._drag_v = self._value

    def mouseMoveEvent(self, e):
        delta = (self._drag_y - e.position().y()) * 0.006
        self.setValue(self._drag_v + delta)

    def wheelEvent(self, e):
        self.setValue(self._value + e.angleDelta().y() / 120 * 0.03)

    def paintEvent(self, event):  # noqa: ARG002
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        cx, cy, r = self.width() / 2, 44, 30

        # Knob body
        grad = QRadialGradient(cx, cy - 8, r * 1.6)
        grad.setColorAt(0.0, QColor("#4a4a52"))
        grad.setColorAt(1.0, QColor("#202026"))
        p.setBrush(QBrush(grad))
        p.setPen(QPen(QColor("#0c0c0f"), 2))
        p.drawEllipse(QPointF(cx, cy), r, r)

        # Indicator (sweep -135..+135 deg)
        ang = math.radians(-135 + self._value * 270)
        ix = cx + math.sin(ang) * (r - 6)
        iy = cy - math.cos(ang) * (r - 6)
        p.setPen(QPen(QColor(ACCENT), 3, cap=Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(cx, cy), QPointF(ix, iy))

        # Label
        p.setPen(QColor("#c8c8d0"))
        f = QFont("Helvetica", 9, QFont.Weight.Bold)
        p.setFont(f)
        p.drawText(QRectF(0, 86, self.width(), 16),
                   Qt.AlignmentFlag.AlignHCenter, self._label)


# ====================================================== 7-segment display
_SEG_MAP = {
    "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc",
    "5": "afgcd", "6": "afgecd", "7": "abc", "8": "abcdefg", "9": "abcdfg",
    " ": "",
}


class SevenSegmentDisplay(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._text = "1.01"
        self.setFixedSize(150, 80)

    def setText(self, text: str):
        if text != self._text:
            self._text = text
            self.update()

    def _draw_digit(self, p: QPainter, x: float, ch: str):
        on = _SEG_MAP.get(ch, "")
        w, h, t = 30.0, 56.0, 6.0
        y0, ymid, y1 = 12.0, 12.0 + h / 2, 12.0 + h
        x1, x2 = x, x + w
        seg = {
            "a": ((x1 + t, y0), (x2 - t, y0)),
            "b": ((x2, y0 + t), (x2, ymid - t / 2)),
            "c": ((x2, ymid + t / 2), (x2, y1 - t)),
            "d": ((x1 + t, y1), (x2 - t, y1)),
            "e": ((x1, ymid + t / 2), (x1, y1 - t)),
            "f": ((x1, y0 + t), (x1, ymid - t / 2)),
            "g": ((x1 + t, ymid), (x2 - t, ymid)),
        }
        for name, ((ax, ay), (bx, by)) in seg.items():
            p.setPen(QPen(LED_ON if name in on else LED_OFF, t,
                          cap=Qt.PenCapStyle.RoundCap))
            p.drawLine(QPointF(ax, ay), QPointF(bx, by))

    def paintEvent(self, event):  # noqa: ARG002
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor("#120505"))
        # text like "1.01" -> bank digit, dot, two algo digits
        left, _, right = self._text.partition(".")
        right = (right + "  ")[:2]
        x = 10.0
        self._draw_digit(p, x, left[:1] or " ")
        x += 38
        # decimal point
        p.setBrush(LED_ON)
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QPointF(x - 2, 12 + 56), 4, 4)
        x += 8
        self._draw_digit(p, x, right[0])
        x += 38
        self._draw_digit(p, x, right[1])


# ====================================================== Panel
def _make_button(text: str) -> QPushButton:
    b = QPushButton(text)
    b.setFixedSize(78, 40)
    b.setStyleSheet(
        "QPushButton {background:#2a2a33; color:#e0e0e8; border:1px solid #000;"
        " border-radius:5px; font-weight:bold; font-size:11px;}"
        "QPushButton:pressed {background:#e8a23d; color:#101010;}"
    )
    return b


class HardwarePanel(QWidget):
    def __init__(self, engine, parent=None):
        super().__init__(parent)
        self.engine = engine
        self.setStyleSheet(f"background:{PANEL};")

        root = QVBoxLayout(self)
        root.setContentsMargins(20, 16, 20, 16)
        root.setSpacing(14)

        # Title
        title = QLabel("N O I S F E R A T U")
        title.setStyleSheet("color:#e8a23d; font-size:20px; font-weight:bold;"
                            " letter-spacing:3px;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        root.addWidget(title)

        sub = QLabel("generative texture synthesizer  ·  python port")
        sub.setStyleSheet("color:#6a6a72; font-size:10px; letter-spacing:1px;")
        sub.setAlignment(Qt.AlignmentFlag.AlignCenter)
        root.addWidget(sub)

        # Display + names
        disp_row = QHBoxLayout()
        disp_row.addStretch()
        self.display = SevenSegmentDisplay()
        disp_row.addWidget(self.display)
        names = QVBoxLayout()
        self.bank_label = QLabel("Wavetables")
        self.bank_label.setStyleSheet("color:#e8a23d; font-size:15px; font-weight:bold;")
        self.algo_label = QLabel("Sparse Glitch")
        self.algo_label.setStyleSheet("color:#c8c8d0; font-size:13px;")
        names.addStretch()
        names.addWidget(self.bank_label)
        names.addWidget(self.algo_label)
        names.addStretch()
        disp_row.addSpacing(16)
        disp_row.addLayout(names)
        disp_row.addStretch()
        root.addLayout(disp_row)

        # Knobs
        knob_row = QHBoxLayout()
        knob_row.addStretch()
        self.knobs = {}
        specs = [
            ("POT1", "pot1", engine.pot1),
            ("POT2", "pot2", engine.pot2),
            ("BITCRUSH", "bitcrush_pot", engine.bitcrush_pot),
            ("RATE", "rate_pot", engine.rate_pot),
            ("VOL", "volume_pot", engine.volume_pot),
        ]
        for label, attr, val in specs:
            k = Knob(label, val)
            k.valueChanged.connect(lambda v, a=attr: setattr(self.engine, a, v))
            self.knobs[attr] = k
            knob_row.addWidget(k)
        knob_row.addStretch()
        root.addLayout(knob_row)

        # Buttons
        btn_row = QHBoxLayout()
        btn_row.addStretch()
        self.prev_btn = _make_button("◀ PREV")
        self.algo_btn = _make_button("ALGO ▶")
        self.bank_btn = _make_button("BANK")
        self.prev_btn.clicked.connect(self.engine.prev_algo)
        self.algo_btn.clicked.connect(self.engine.next_algo)
        self.bank_btn.clicked.connect(self.engine.next_bank)
        for b in (self.prev_btn, self.algo_btn, self.bank_btn):
            btn_row.addWidget(b)
        btn_row.addStretch()
        root.addLayout(btn_row)

    def refresh(self):
        """Sync display + name labels with the engine (called on a timer)."""
        self.display.setText(self.engine.display_text)
        self.bank_label.setText(self.engine.bank_name)
        self.algo_label.setText(self.engine.algo_name)
