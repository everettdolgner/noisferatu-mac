"""
bank2_noisy.py — Bank 2: Noisy Textures (9 algorithms)

Sample-and-hold noise, dust, FM/inharmonic chaos, gated noise, vinyl crackle, etc.
Most of these have per-sample feedback (1-pole filters, S&H, random walks,
coincidence clocks), so they run as tight per-sample loops. Saw Clicks vectorises.

The Vinyl Crackle algorithm replays the *actual* 32000-sample crackle recording
extracted from the firmware's sample.h (vinyl_crackle.npy).
"""

from __future__ import annotations

import math
import os

import numpy as np

from dsp_core import Algo, freq_to_inc, phase_block, saw10_vec, tri10, lfo10

M = 0xFFFFFFFF


def _lerp(lo: float, hi: float, t: float) -> float:
    return lo + (hi - lo) * t


# ---------------------------------------------------------------- Latched Noise
class LatchedNoise(Algo):
    name = "Latched Noise"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.counter = 0
        self.period = self.sr
        self.value = 0
        self.threshold = 4095

    def set_params(self, p1, p2):
        freq = _lerp(9.0, 8000.0, p2 * p2)
        self.period = max(1, int(self.sr / freq))
        p = _lerp(0.05, 1.0, p1 * p1)
        self.threshold = int(p * 4095.0)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        cnt, period, val, thr = self.counter, self.period, self.value, self.threshold
        for i in range(n):
            cnt += 1
            if cnt >= period:
                cnt = 0
                if rng.rand12() < thr:
                    val = rng.noise1()
            out[i] = val
        self.counter, self.value = cnt, val
        return out


# ---------------------------------------------------------------- Dust
class DustNoise(Algo):
    name = "Dust"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.alpha = 1.0
        self.prob = 0.0
        self.state = 0.0

    def set_params(self, p1, p2):
        cutoff = _lerp(90.0, 8000.0, p1)
        self.alpha = min(max(1.0 - math.exp(-2.0 * math.pi * cutoff / self.sr), 0.0), 1.0)
        self.prob = _lerp(1.0, 2000.0, p2) / self.sr

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        alpha, prob, state = self.alpha, self.prob, self.state
        for i in range(n):
            raw = rng.noise1() if rng.randf() < prob else 0
            state += alpha * (raw - state)
            out[i] = int(state)
        self.state = state
        return out


# ---------------------------------------------------------------- FM Noise
class FMNoise(Algo):
    name = "FM Noise"
    RATIOS = (1.41, 1.618, 1.73, 2.11, 2.37, 2.81, 3.14, 4.0)
    CLOCK_RATIO = 2.37

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = self.p2 = 0
        self.c1 = self.c2 = 0
        self.inc1 = self.inc2 = 0
        self.cinc1 = self.cinc2 = 0
        self.last_c1 = 0
        self.idx = 0
        self.base = 2.0

    def set_params(self, p1, p2):
        self.base = _lerp(2.0, 2000.0, p1)
        self.inc1 = freq_to_inc(self.base)
        self.inc2 = freq_to_inc(self.base * self.RATIOS[self.idx])
        clock = _lerp(0.5, 20.0, p2)
        self.cinc1 = freq_to_inc(clock)
        self.cinc2 = freq_to_inc(clock * self.CLOCK_RATIO)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        p1, p2, c1, c2 = self.p1, self.p2, self.c1, self.c2
        inc1, inc2 = self.inc1, self.inc2
        last = self.last_c1
        for i in range(n):
            p1 = (p1 + inc1) & M
            p2 = (p2 + inc2) & M
            c1 = (c1 + self.cinc1) & M
            c2 = (c2 + self.cinc2) & M
            s1 = c1 & 0x80000000
            s2 = c2 & 0x80000000
            if (s1 and not last) and s2:
                self.idx = rng.rand12() % 8
                inc2 = freq_to_inc(self.base * self.RATIOS[self.idx])
            last = s1
            out[i] = tri10(p1) ^ tri10(p2)
        self.p1, self.p2, self.c1, self.c2 = p1, p2, c1, c2
        self.inc2, self.last_c1 = inc2, last
        return out


# ---------------------------------------------------------------- Noise Gates
class NoiseGates(Algo):
    name = "Noise Gates"
    WALK_RATE = 8.0

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = self.p2 = 0
        self.inc1 = 0
        self.base2 = 0
        self.walk = 0
        self.step = 0
        self.wcounter = 0
        self.wperiod = self.sr // 8

    def set_params(self, p1, p2):
        freq = _lerp(0.01, 5.0, p1)
        self.inc1 = freq_to_inc(freq)
        self.base2 = self.inc1
        self.step = int(_lerp(1000.0, 300000.0, p2))
        self.wperiod = int(self.sr / self.WALK_RATE)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        p1, p2, walk = self.p1, self.p2, self.walk
        wc, wp, step = self.wcounter, self.wperiod, self.step
        max_walk = step * 500
        for i in range(n):
            wc += 1
            if wc >= wp:
                wc = 0
                walk += step if (rng.rand12() & 1) else -step
                if walk < -max_walk:
                    walk = -max_walk
                if walk > max_walk:
                    walk = max_walk
            p1 = (p1 + self.inc1) & M
            tri1 = tri10(p1)
            inc2 = self.base2 + walk
            if inc2 < 0:
                inc2 = 0
            p2 = (p2 + inc2) & M
            tri2 = tri10(p2)
            nz = rng.noise1()
            g1 = nz if tri1 > 486 else 0
            g2 = nz if tri2 > 486 else 0
            out[i] = (g1 + g2) >> 1
        self.p1, self.p2, self.walk, self.wcounter = p1, p2, walk, wc
        return out


# ---------------------------------------------------------------- Saw Clicks
class SawClicks(Algo):
    name = "Saw Clicks"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = self.p2 = 0
        self.inc1 = self.inc2 = 65536

    def set_params(self, p1, p2):
        self.inc1 = freq_to_inc(_lerp(0.001, 20.0, p1))
        self.inc2 = freq_to_inc(_lerp(0.001, 20.0, p2))

    def render(self, n):
        ph1, self.p1 = phase_block(self.p1, self.inc1, n)
        ph2, self.p2 = phase_block(self.p2, self.inc2, n)
        return (saw10_vec(ph1) + saw10_vec(ph2)) >> 1


# ---------------------------------------------------------------- Noise NOR Noise
class NoiseNorNoise(Algo):
    name = "Noise NOR Noise"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.c1 = self.c2 = 0
        self.period1 = self.period2 = 1
        self.held1 = self.held2 = 0

    def set_params(self, p1, p2):
        f1 = _lerp(5.0, 120.0, p1)
        f2 = _lerp(5.0, 120.0, p2)
        self.period1 = max(1, int(self.sr / f1))
        self.period2 = max(1, int(self.sr / f2))

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        c1, c2, h1, h2 = self.c1, self.c2, self.held1, self.held2
        p1, p2 = self.period1, self.period2
        for i in range(n):
            c1 += 1
            if c1 >= p1:
                c1 = 0
                h1 = rng.noise1()
            c2 += 1
            if c2 >= p2:
                c2 = 0
                h2 = rng.noise1()
            out[i] = (~(h1 | h2)) & 0x3FF
        self.c1, self.c2, self.held1, self.held2 = c1, c2, h1, h2
        return out


# ---------------------------------------------------------------- Dust Burst
class DustBurst(Algo):
    name = "Dust Burst"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.prob = 0.0
        self.walk_speed = 0.001
        self.step = 0.1
        self.alpha = 1.0
        self.state = 0.0

    def set_params(self, p1, p2):
        self.walk_speed = _lerp(0.0001, 0.02, p1 * p1)
        self.step = _lerp(0.05, 0.8, p2)
        cutoff = 2000.0
        self.alpha = min(max(1.0 - math.exp(-2.0 * math.pi * cutoff / self.sr), 0.0), 1.0)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        prob, step, alpha, state = self.prob, self.step, self.alpha, self.state
        walk_thr = int(self.walk_speed * 4095.0)
        for i in range(n):
            if rng.rand12() < walk_thr:
                prob += step if (rng.rand12() & 1) else -step
                if prob < 0.0:
                    prob = 0.0
                if prob > 1.0:
                    prob = 1.0
            raw = rng.noise1() if rng.rand12() < int(prob * 4095.0) else 0
            state += alpha * (raw - state)
            out[i] = int(state)
        self.prob, self.state = prob, state
        return out


# ---------------------------------------------------------------- Highpass Noise
class HighpassNoise(Algo):
    name = "Highpass Noise"
    MASK = 0x380
    RATE_TABLE = (1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
                  1.33, 0.5, 0.75, 0.89, 0.6, 0.9, 0.43, 0.71)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.am_phase = 0
        self.am_inc = 0
        self.base_inc = 0
        self.depth = 1.0

    def set_params(self, p1, p2):
        self.depth = 1.0 - p1
        self.base_inc = freq_to_inc(_lerp(0.3, 20.0, p2))
        if self.am_inc == 0:
            self.am_inc = self.base_inc

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        phase, inc, base, depth = self.am_phase, self.am_inc, self.base_inc, self.depth
        for i in range(n):
            nz = rng.noise1() & self.MASK
            prev = phase
            phase = (phase + inc) & M
            if phase < prev:                       # LFO wrapped
                inc = int(base * self.RATE_TABLE[rng.rand12() % 16])
            lfo = lfo10(phase)
            blended = int(lfo * depth + 1023 * (1.0 - depth))
            out[i] = (nz * blended) >> 10
        self.am_phase, self.am_inc = phase, inc
        return out


# ---------------------------------------------------------------- Vinyl Crackle
_CRACKLE = np.load(os.path.join(os.path.dirname(__file__), "..", "vinyl_crackle.npy"))
_CRACKLE_LEN = len(_CRACKLE)


class VinylCrackle(Algo):
    name = "Vinyl Crackle"
    SILENCE = (57, 113, 331)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 0.5
        self.window = 1000
        self.frac = 0.0
        self.pos = 0
        self.remaining = 0
        self.in_silence = False

    def set_params(self, p1, p2):
        self.speed = _lerp(0.04, 2.0, p1)
        self.window = 200 + int((4000 - 200) * p2)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        speed, window = self.speed, self.window
        frac, pos, rem, silent = self.frac, self.pos, self.remaining, self.in_silence
        span = max(1, _CRACKLE_LEN - window)
        for i in range(n):
            if rem == 0:
                if silent:
                    silent = False
                    pos = rng.rand12() % span
                    rem = window
                else:
                    if rng.rand12() % 4 > 0:          # 75% silence
                        silent = True
                        rem = self.SILENCE[rng.rand12() % 3]
                    else:
                        pos = rng.rand12() % span
                        rem = window
            rem -= 1
            if silent:
                out[i] = 0
                continue
            out[i] = int(_CRACKLE[pos % _CRACKLE_LEN]) >> 6
            frac += speed
            inc = int(frac)
            frac -= inc
            pos += inc
        self.frac, self.pos, self.remaining, self.in_silence = frac, pos, rem, silent
        return out


def build(ctx) -> list[Algo]:
    return [
        LatchedNoise(ctx),
        DustNoise(ctx),
        FMNoise(ctx),
        NoiseGates(ctx),
        SawClicks(ctx),
        NoiseNorNoise(ctx),
        DustBurst(ctx),
        HighpassNoise(ctx),
        VinylCrackle(ctx),
    ]
