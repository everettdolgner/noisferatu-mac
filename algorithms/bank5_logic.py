"""
bank5_logic.py — Bank 5: Logic Disorder (9 algorithms)

Bitwise/logic combinations of two (or three) oscillators. Faithful to algos.h:
several "square" oscillators are actually saws taken from (phase>>22)-512, and the
NOR/NAND/XNOR results are masked with & 0x3FF (so they read 0..1023, not signed) —
the offset/clipping that follows is part of the gnarly character, so it's preserved.

All phase increments are fixed per-block, so every algorithm here vectorises cleanly.
"""

from __future__ import annotations

import numpy as np

from dsp_core import Algo, freq_to_inc, phase_block, tri10_vec, saw10_vec


def _lerp(lo: float, hi: float, t: float) -> float:
    return lo + (hi - lo) * t


class ThreeCascadedSquares(Algo):
    name = "3-Cascade"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = self.p2 = self.p3 = 0
        self.inc1 = freq_to_inc(9.96)      # osc1 fixed
        self.inc2 = self.inc3 = 0

    def set_params(self, p1, p2):
        self.inc1 = freq_to_inc(9.96)
        self.inc2 = freq_to_inc(_lerp(0.6, 1024.0, p1))
        self.inc3 = freq_to_inc(_lerp(1.0, 1024.0, p2))

    def render(self, n):
        ph1, self.p1 = phase_block(self.p1, self.inc1, n)
        ph2, self.p2 = phase_block(self.p2, self.inc2, n)
        ph3, self.p3 = phase_block(self.p3, self.inc3, n)
        bit = np.uint32(0x80000000)
        sq1 = np.where(ph1 & bit, 511, -512).astype(np.int32)
        sq2 = np.where(ph2 & bit, 511, -512).astype(np.int32)
        sq3 = np.where(ph3 & bit, 511, -512).astype(np.int32)
        sq1_am = (sq1 + 512) >> 1
        sq2_mod = (sq2 * sq1_am) >> 9
        sq2_mod_am = (sq2_mod + 512) >> 1
        return (sq3 * sq2_mod_am) >> 9


class _TwoOsc(Algo):
    """Shared scaffold: two phase accumulators with pot-mapped frequencies."""
    f1 = (0.0, 0.0)
    f2 = (0.0, 0.0)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = self.p2 = 0
        self.inc1 = self.inc2 = 0

    def set_params(self, p1, p2):
        self.inc1 = freq_to_inc(_lerp(self.f1[0], self.f1[1], p1))
        self.inc2 = freq_to_inc(_lerp(self.f2[0], self.f2[1], p2))

    def _phases(self, n):
        ph1, self.p1 = phase_block(self.p1, self.inc1, n)
        ph2, self.p2 = phase_block(self.p2, self.inc2, n)
        return ph1, ph2


class NorSquare(_TwoOsc):
    name = "NOR Square"
    f1 = (0.8, 200.0)
    f2 = (0.73, 215.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        sq1, sq2 = saw10_vec(ph1), saw10_vec(ph2)
        return (~(sq1 | sq2)) & 0x3FF


class TriOrSaw(_TwoOsc):
    name = "Tri OR Saw"
    f1 = (4.5, 1024.0)
    f2 = (2.0, 1024.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        return tri10_vec(ph1) | saw10_vec(ph2)


class TriNorTri(_TwoOsc):
    name = "Tri NOR Tri"
    f1 = (4.0, 880.0)
    f2 = (15.0, 900.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        return (~(tri10_vec(ph1) | tri10_vec(ph2))) & 0x3FF


class TriXorTri(_TwoOsc):
    name = "Tri XOR Tri"
    f1 = (0.7, 220.0)
    f2 = (0.6, 440.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        return tri10_vec(ph1) ^ tri10_vec(ph2)


class SquareXnorSquare(_TwoOsc):
    name = "Sq XNOR Sq"
    f1 = (0.5, 440.0)
    f2 = (0.6, 150.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        sq1, sq2 = saw10_vec(ph1), saw10_vec(ph2)
        return (~(sq1 ^ sq2)) & 0x3FF


class SquareNandSquare(_TwoOsc):
    name = "Sq NAND Sq"
    f1 = (0.1, 50.0)
    f2 = (0.08, 45.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        sq1, sq2 = saw10_vec(ph1), saw10_vec(ph2)
        return (~(sq1 & sq2)) & 0x3FF


class TwoSaws(_TwoOsc):
    name = "Two Saws"
    f1 = (15.0, 850.0)
    f2 = (15.0, 850.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        return (saw10_vec(ph1) + saw10_vec(ph2)) >> 1


class SquareOrSquare(_TwoOsc):
    name = "Sq OR Sq"
    f1 = (15.0, 850.0)
    f2 = (15.0, 850.0)

    def render(self, n):
        ph1, ph2 = self._phases(n)
        return saw10_vec(ph1) | saw10_vec(ph2)


def build(ctx) -> list[Algo]:
    return [
        ThreeCascadedSquares(ctx),
        NorSquare(ctx),
        TriOrSaw(ctx),
        TriNorTri(ctx),
        TriXorTri(ctx),
        SquareXnorSquare(ctx),
        SquareNandSquare(ctx),
        TwoSaws(ctx),
        SquareOrSquare(ctx),
    ]
