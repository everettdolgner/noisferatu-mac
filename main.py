"""
main.py — NOISFERATU Python port entry point.

Usage:
    python main.py

A macOS/Python recreation of the rob-scape NOISFERATU generative texture synth
(Arduino original: https://github.com/rob-scape/noisferatu). 45 algorithms across
5 banks, running at the original 16 kHz for authentic lo-fi character.

Controls:
    POT1 / POT2  — per-algorithm parameters (drag vertically, or scroll)
    BITCRUSH     — 10-bit down to 1-bit
    RATE         — sample-rate reduction (1x .. 40x decimation)
    VOL          — master volume
    PREV / ALGO  — step through algorithms in the current bank
    BANK         — cycle through the 5 banks
"""

from __future__ import annotations

import sys

from PyQt6.QtWidgets import QApplication

from engine import Engine
from ui.main_window import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    engine = Engine()
    window = MainWindow(engine)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
