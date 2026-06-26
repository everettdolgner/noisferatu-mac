"""
bank4_blips.py — Bank 4: Blips & Tones (9 algorithms)

Triangle-based tones, ring mod, and Bernoulli-gated note generators. The purely
periodic algorithms (harmonic/major tris, ring mod) are vectorised; the triggered
ones (random/fast triangle, phrygian, bernoulli, pentatonic, noise-or-square) keep
their per-sample state machines, which is cheap at 16 kHz.
"""

from __future__ import annotations

import numpy as np

from dsp_core import (
    Algo, freq_to_inc, phase_block, tri10, tri10_vec, lfo10_vec, saw10,
)

M = 0xFFFFFFFF


def _lerp(lo: float, hi: float, t: float) -> float:
    return lo + (hi - lo) * t


def _decay_coeff(decay_time: float, sr: int) -> float:
    """Firmware's exp-free decay approximation, clamped to (0.9, 0.9999)."""
    c = 1.0 - (3.0 / (decay_time * sr))
    return min(max(c, 0.9), 0.9999)


# ---------------------------------------------------------------- Random Triangle
class RandomTriangle(Algo):
    name = "Random Tri"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.phase = 0
        self.inc = 0
        self.env = 0.0
        self.decay = 0.9995
        self.counter = 0
        self.period = self.sr // 2

    def set_params(self, p1, p2):
        trig = _lerp(0.05, 0.5, 1.0 - p1)          # pot1 reversed
        self.period = max(1, int(trig * self.sr))
        self.decay = _decay_coeff(_lerp(0.005, 0.6, p2), self.sr)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        phase, inc, env = self.phase, self.inc, self.env
        cnt, period, decay = self.counter, self.period, self.decay
        for i in range(n):
            cnt += 1
            if cnt >= period:
                cnt = 0
                if not (rng.rand12() & 1):
                    env = 1.0
                    freq = 250.0 + (rng.rand12() % 1000) / 1000.0 * (2780.0 - 250.0)
                    inc = freq_to_inc(freq)
            env *= decay
            phase = (phase + inc) & M
            out[i] = int(tri10(phase) * env)
        self.phase, self.inc, self.env, self.counter = phase, inc, env, cnt
        return out


# ---------------------------------------------------------------- Harmonic Tris
class HarmonicTris(Algo):
    name = "Harmonic Tri"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.ph = [0, 0, 0]
        self.lph = [0, 0, 0]
        self.inc = [0, 0, 0]
        self.linc = [0, 0, 0]

    def set_params(self, p1, p2):
        base = _lerp(50.0, 400.0, p1)
        self.inc = [freq_to_inc(base), freq_to_inc(base * 3.0), freq_to_inc(base * 4.0)]
        exp = p2 ** 3                                  # cubic curve
        lfo = _lerp(0.001, 10.0, exp)
        self.linc = [freq_to_inc(lfo), freq_to_inc(lfo * 1.618), freq_to_inc(lfo * 1.732)]

    def render(self, n):
        mods = []
        for k in range(3):
            ph, self.ph[k] = phase_block(self.ph[k], self.inc[k], n)
            lph, self.lph[k] = phase_block(self.lph[k], self.linc[k], n)
            mods.append((tri10_vec(ph) * lfo10_vec(lph)) >> 10)
        s = mods[0] + mods[1] + mods[2]
        return (s / 3.0).astype(np.int32)             # C integer divide (trunc toward 0)


# ---------------------------------------------------------------- Fast Triangle
class FastTriangle(Algo):
    name = "Fast Tri"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.phase = 0
        self.inc = 0
        self.env = 0.0
        self.decay = 0.9995
        self.crush = 10
        self.counter = 0
        self.period = self.sr // 16

    def set_params(self, p1, p2):
        self.inc = freq_to_inc(_lerp(11.0, 6000.0, p1))
        self.decay = _decay_coeff(_lerp(0.0074, 0.7, p2), self.sr)
        self.period = self.sr // 16

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        phase, inc, env, decay = self.phase, self.inc, self.env, self.decay
        crush, cnt, period = self.crush, self.counter, self.period
        for i in range(n):
            cnt += 1
            if cnt >= period:
                cnt = 0
                if not ((rng.rand12() & 3) > 0):       # 75% skip
                    env = 1.0
                    crush = 3 + (rng.rand12() % 10)
            env *= decay
            phase = (phase + inc) & M
            enveloped = int(tri10(phase) * env)
            bits = 10 - crush                       # crush is 3..12; >10 bits => clean
            mask = ~((1 << bits) - 1) if bits > 0 else -1
            out[i] = enveloped & mask
        self.phase, self.inc, self.env, self.crush, self.counter = phase, inc, env, crush, cnt
        return out


# ---------------------------------------------------------------- Phrygian Tri
class PhrygianTri(Algo):
    name = "Phrygian Tri"
    RATIOS = (1.0, 1.0595, 1.1892, 1.3348, 1.4983, 1.5874, 1.7818, 2.0)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.phase = 0
        self.inc = 0
        self.env = 0.0
        self.env_counter = 0
        self.trig_counter = 0
        self.trig_period = self.sr // 4
        self.burst = 0
        self.burst_inc = 0
        self.scale_pos = 0
        self.root = 100.0

    def set_params(self, p1, p2):
        burst_freq = _lerp(0.5, 8.0, p1)               # pots swapped on this algo
        self.burst_inc = freq_to_inc(burst_freq)
        self.root = _lerp(50.0, 200.0, p2)
        self.trig_period = max(1, self.sr // max(1, int(burst_freq * 2.0)))
        self.inc = freq_to_inc(self.root * self.RATIOS[self.scale_pos])

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        phase, inc, env, envc = self.phase, self.inc, self.env, self.env_counter
        tc, base_period = self.trig_counter, self.trig_period
        burst, binc, pos, root = self.burst, self.burst_inc, self.scale_pos, self.root
        for i in range(n):
            burst = (burst + binc) & M
            cur_period = base_period + ((burst >> 22) << 4)
            tc += 1
            if tc >= cur_period:
                tc = 0
                pos += 1 if (rng.rand12() & 1) else -1
                if pos < 0:
                    pos = 7
                if pos > 7:
                    pos = 0
                inc = freq_to_inc(root * self.RATIOS[pos])
                env = 1.0
                envc = 0
            if envc < 800:
                envc += 1
                env *= 0.9963
            else:
                env = 0.0
            phase = (phase + inc) & M
            out[i] = int(tri10(phase) * env)
        self.phase, self.inc, self.env, self.env_counter = phase, inc, env, envc
        self.trig_counter, self.burst, self.scale_pos = tc, burst, pos
        return out


# ---------------------------------------------------------------- Ring Mod
class RingMod(Algo):
    name = "Ring Mod"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = self.p2 = 0
        self.inc1 = self.inc2 = 0

    def set_params(self, p1, p2):
        self.inc1 = freq_to_inc(_lerp(15.0, 2000.0, p1))
        self.inc2 = freq_to_inc(_lerp(15.0, 2000.0, p2))

    def render(self, n):
        ph1, self.p1 = phase_block(self.p1, self.inc1, n)
        ph2, self.p2 = phase_block(self.p2, self.inc2, n)
        return (tri10_vec(ph1) * tri10_vec(ph2)) >> 9


# ---------------------------------------------------------------- Noise OR Square
class NoiseOrSquare(Algo):
    name = "Noise|Square"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.phase = 0
        self.inc = 0
        self.sh_counter = 0
        self.sh_period = 1
        self.held = 0

    def set_params(self, p1, p2):
        sh_freq = _lerp(15.0, 116.0, p1)
        self.sh_period = max(1, int(self.sr / sh_freq))
        self.inc = freq_to_inc(_lerp(35.0, 5000.0, p2))

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        phase, inc = self.phase, self.inc
        cnt, period, held = self.sh_counter, self.sh_period, self.held
        for i in range(n):
            cnt += 1
            if cnt >= period:
                cnt = 0
                held = rng.noise1()
            phase = (phase + inc) & M
            out[i] = held | saw10(phase)
        self.phase, self.sh_counter, self.held = phase, cnt, held
        return out


# ---------------------------------------------------------------- Major Tris
class MajorTris(Algo):
    name = "Major Tris"
    F1 = freq_to_inc(220.0)
    F2 = freq_to_inc(220.0 * 1.25992)
    F3 = freq_to_inc(220.0 * 1.5)
    LFO3 = freq_to_inc(0.2)
    LFO3_SLOW = freq_to_inc(0.035)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = self.p2 = self.p3 = 0
        self.l1 = self.l2 = self.l3 = 0
        self.l3slow = 0
        self.linc1 = self.linc2 = 0

    def set_params(self, p1, p2):
        self.linc1 = freq_to_inc(_lerp(0.08, 2.0, p1))
        self.linc2 = freq_to_inc(_lerp(0.08, 2.0, p2))

    def render(self, n):
        ph1, self.p1 = phase_block(self.p1, self.F1, n)
        ph2, self.p2 = phase_block(self.p2, self.F2, n)
        ph3, self.p3 = phase_block(self.p3, self.F3, n)
        lph1, self.l1 = phase_block(self.l1, self.linc1, n)
        lph2, self.l2 = phase_block(self.l2, self.linc2, n)
        lph3, self.l3 = phase_block(self.l3, self.LFO3, n)
        lph3s, self.l3slow = phase_block(self.l3slow, self.LFO3_SLOW, n)
        lfo1, lfo2 = lfo10_vec(lph1), lfo10_vec(lph2)
        lfo3 = (lfo10_vec(lph3) * lfo10_vec(lph3s)) >> 10        # nested AM
        mod1 = (tri10_vec(ph1) * lfo1) >> 10
        mod2 = (tri10_vec(ph2) * lfo2) >> 10
        mod3 = (tri10_vec(ph3) * lfo3) >> 10
        return ((mod1 + mod2 + mod3) / 3.0).astype(np.int32)


# ---------------------------------------------------------------- Bernoulli Minor7
class BernoulliTris(Algo):
    name = "Bernoulli 7"
    CLOCK = freq_to_inc(5.0)
    DECAY = 0.9985

    def __init__(self, ctx):
        super().__init__(ctx)
        self.clk = 0
        self.last = 0
        self.p1 = self.p2 = 0
        self.inc1 = self.inc2 = 0
        self.e1 = self.e2 = 0.0
        self.prob1 = self.prob2 = 0.5

    def set_params(self, p1, p2):
        self.prob1, self.prob2 = p1, p2

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        clk, last = self.clk, self.last
        p1, p2, inc1, inc2 = self.p1, self.p2, self.inc1, self.inc2
        e1, e2 = self.e1, self.e2
        for i in range(n):
            clk = (clk + self.CLOCK) & M
            state = clk & 0x80000000
            if state and not last:
                if rng.rand12() & 1:
                    r1 = (rng.rand12() % 1000) / 1000.0
                    inc1 = freq_to_inc(220.0 if r1 < self.prob1 else 330.0)
                    e1 = 1.0
                    r2 = (rng.rand12() % 1000) / 1000.0
                    inc2 = freq_to_inc(264.0 if r2 < self.prob2 else 396.0)
                    e2 = 1.0
            last = state
            e1 *= self.DECAY
            e2 *= self.DECAY
            p1 = (p1 + inc1) & M
            p2 = (p2 + inc2) & M
            out[i] = (int(tri10(p1) * e1) + int(tri10(p2) * e2)) >> 1
        self.clk, self.last = clk, last
        self.p1, self.p2, self.inc1, self.inc2 = p1, p2, inc1, inc2
        self.e1, self.e2 = e1, e2
        return out


# ---------------------------------------------------------------- Pentatonic Blips
class PentatonicBlips(Algo):
    name = "Penta Blips"
    NOTES = (
        freq_to_inc(220.0),
        freq_to_inc(220.0 * 1.125),
        freq_to_inc(220.0 * 1.25),
        freq_to_inc(220.0 * 1.5),
        freq_to_inc(220.0 * 1.6875),
    )

    def __init__(self, ctx):
        super().__init__(ctx)
        self.clk = 0
        self.last = 0
        self.clk_inc = 0
        self.phase = 0
        self.inc = 0
        self.env = 0.0
        self.decay = 0.999

    def set_params(self, p1, p2):
        self.clk_inc = freq_to_inc(_lerp(3.0, 11.5, p1))
        self.decay = _decay_coeff(_lerp(0.001, 0.5, p2), self.sr)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        clk, last, cinc = self.clk, self.last, self.clk_inc
        phase, inc, env, decay = self.phase, self.inc, self.env, self.decay
        for i in range(n):
            clk = (clk + cinc) & M
            state = clk & 0x80000000
            if state and not last:
                if rng.rand12() & 1:
                    inc = self.NOTES[rng.rand12() % 5]
                    env = 1.0
            last = state
            env *= decay
            phase = (phase + inc) & M
            out[i] = int(tri10(phase) * env)
        self.clk, self.last, self.phase, self.inc, self.env = clk, last, phase, inc, env
        return out


def build(ctx) -> list[Algo]:
    return [
        RandomTriangle(ctx),
        HarmonicTris(ctx),
        FastTriangle(ctx),
        PhrygianTri(ctx),
        RingMod(ctx),
        NoiseOrSquare(ctx),
        MajorTris(ctx),
        BernoulliTris(ctx),
        PentatonicBlips(ctx),
    ]
