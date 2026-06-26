"""
bank3_bitbend.py — Bank 3: BitBend Wavetables (9 algorithms)

SoundScaper-style address manipulation: a bit-clock periodically random-walks one or
more "bit position" pointers, then each output sample is read from the shared buffer
at an address whose bits have been XOR'd / forced-0 / forced-1 / frozen (HOLD). The
buffer is shared with Bank 1 and several algos reuse Bank-1 buffer layouts.

(Note: the firmware lets the GW8/GW9 bit pointer underflow a uint8 (UB on the MCU);
here it's kept in 0..11 via Python modulo, which is the musically intended behaviour.)
"""

from __future__ import annotations

import numpy as np

from dsp_core import Algo, WAVEFORM_SIZE as N, freq_to_inc
from . import wavegen
from .bank1_wavetables import _WaveAlgo

M = 0xFFFFFFFF


def _lerp(lo, hi, t):
    return lo + (hi - lo) * t


def _coin(rng):
    return 1 if (rng.rand12() & 1) else -1


# ====================================================== GW8: Chaos (generative)
class GW8(_WaveAlgo):
    name = "BitBend Chaos"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.cph = 0
        self.cinc = 0
        self.last = 0
        self.pos = 5
        self.mode = 0
        self.held = 0

    def set_params(self, p1, p2):
        self.speed = _lerp(0.1, 4.0, p1)
        self.cinc = freq_to_inc(_lerp(0.5, 20.0, p2))

    def _generate(self):
        wavegen.gen8(self.buf, self.rng)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng, buf = self.rng, self.buf
        cph, cinc, last = self.cph, self.cinc, self.last
        pos, mode, held = self.pos, self.mode, self.held
        speed, frac = self.speed, self.frac
        phase = self.wave.playback_phase
        for i in range(n):
            cph = (cph + cinc) & M
            state = cph & 0x80000000
            if state and not last:
                if rng.rand12() & 1:
                    mode = rng.rand12() % 4
                pos = (pos + _coin(rng)) % 12
                if mode == 3:
                    held = phase & ((1 << pos) - 1)
            last = state
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= N:
                phase = 0
            addr = phase
            mask = (1 << pos) - 1
            if mode == 0:
                addr &= ~mask
            elif mode == 1:
                addr |= mask
            elif mode == 2:
                addr ^= mask
            else:
                addr = (addr & ~mask) | (held & mask)
            out[i] = buf[addr % N]
        self.cph, self.last, self.pos, self.mode, self.held = cph, last, pos, mode, held
        self.frac = frac
        self.wave.playback_phase = phase
        return out


# ====================================================== GW9: Sparse (mode = pot2)
class GW9(_WaveAlgo):
    name = "BitBend Sparse"
    CLOCK = freq_to_inc(8.0)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.cph = 0
        self.last = 0
        self.pos = 5
        self.mode = 0
        self.held = 0

    def set_params(self, p1, p2):
        self.speed = _lerp(0.1, 4.0, p1)
        self.mode = min(3, int(p2 * 3.999))

    def _generate(self):
        wavegen.gen1(self.buf, self.rng)        # Sparse Glitchy layout

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng, buf = self.rng, self.buf
        cph, last, pos, mode, held = self.cph, self.last, self.pos, self.mode, self.held
        speed, frac = self.speed, self.frac
        phase = self.wave.playback_phase
        for i in range(n):
            cph = (cph + self.CLOCK) & M
            state = cph & 0x80000000
            if state and not last:
                pos = (pos + _coin(rng)) % 12
                if mode == 3:
                    held = phase & ((1 << pos) - 1)
            last = state
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= N:
                phase = 0
            addr = phase
            mask = (1 << pos) - 1
            if mode == 0:
                addr &= ~mask
            elif mode == 1:
                addr |= mask
            elif mode == 2:
                addr ^= mask
            else:
                addr = (addr & ~mask) | (held & mask)
            out[i] = buf[addr % N]
        self.cph, self.last, self.pos, self.held = cph, last, pos, held
        self.frac = frac
        self.wave.playback_phase = phase
        return out


# ====================================================== Dual/triple combos
class _BitBendCombo(_WaveAlgo):
    """Shared scaffold for the Bernoulli-gated dual/triple manipulators.

    Subclasses set: gen_fn, bernoulli (bool), and implement _walk()/_manip()."""
    gen_fn = None
    bernoulli = True

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.cph = 0
        self.cinc = 0
        self.last = 0

    def set_params(self, p1, p2):
        self.speed = _lerp(0.1, 4.0, p1)
        self.cinc = freq_to_inc(_lerp(0.5, 20.0, p2))

    def _generate(self):
        type(self).gen_fn(self.buf, self.rng)

    def _walk(self, rng, phase):  # update bit positions / held bits
        pass

    def _manip(self, phase):      # return manipulated address
        return phase

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng, buf = self.rng, self.buf
        cph, cinc, last = self.cph, self.cinc, self.last
        speed, frac = self.speed, self.frac
        phase = self.wave.playback_phase
        for i in range(n):
            cph = (cph + cinc) & M
            state = cph & 0x80000000
            if state and not last:
                if (not self.bernoulli) or (rng.rand12() & 1):
                    self._walk(rng, phase)
            last = state
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= N:
                phase = 0
            out[i] = buf[self._manip(phase) % N]
        self.cph, self.last, self.frac = cph, last, frac
        self.wave.playback_phase = phase
        return out


class GW10(_BitBendCombo):
    name = "BitBend Dual"
    gen_fn = staticmethod(wavegen.gen3)         # Spacey Pulses

    def __init__(self, ctx):
        super().__init__(ctx)
        self.pos_xor = 3        # bits 0-3
        self.pos_hold = 8       # bits 4-8
        self.held = 0

    def _walk(self, rng, phase):
        self.pos_xor = (self.pos_xor + _coin(rng)) % 4
        self.pos_hold = 4 + ((self.pos_hold - 4 + _coin(rng) + 5) % 5)
        hm = ((1 << (self.pos_hold + 1)) - 1) & ~((1 << 4) - 1)
        self.held = phase & hm

    def _manip(self, phase):
        addr = phase ^ ((1 << (self.pos_xor + 1)) - 1)
        hm = ((1 << (self.pos_hold + 1)) - 1) & ~((1 << 4) - 1)
        return (addr & ~hm) | (self.held & hm)


class GW11(_BitBendCombo):
    name = "BitBend Freeze"
    gen_fn = staticmethod(wavegen.gen5)         # Wandering Window layout
    bernoulli = False                            # fixed-rate clock, no gate

    def __init__(self, ctx):
        super().__init__(ctx)
        self.pos_low = 2        # SET_0 bits 0-2
        self.pos_high = 6       # HOLD bits 3-6
        self.held = 0

    def _walk(self, rng, phase):
        self.pos_low = (self.pos_low + _coin(rng)) % 3
        self.pos_high = 3 + ((self.pos_high - 3 + _coin(rng) + 4) % 4)
        hm = ((1 << (self.pos_high + 1)) - 1) & ~((1 << 3) - 1)
        self.held = phase & hm

    def _manip(self, phase):
        addr = phase & ~((1 << (self.pos_low + 1)) - 1)
        hm = ((1 << (self.pos_high + 1)) - 1) & ~((1 << 3) - 1)
        return (addr & ~hm) | (self.held & hm)


class GW12(_BitBendCombo):
    name = "BitBend Ping"
    gen_fn = staticmethod(wavegen.gen12)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.pos_low = 2        # XOR bits 0-2
        self.pos_high = 7       # SET_1 bits 3-7

    def _walk(self, rng, phase):
        self.pos_low = (self.pos_low + _coin(rng)) % 3
        self.pos_high = 3 + ((self.pos_high - 3 + _coin(rng) + 5) % 5)

    def _manip(self, phase):
        addr = phase ^ ((1 << (self.pos_low + 1)) - 1)
        return addr | (((1 << (self.pos_high + 1)) - 1) & ~((1 << 3) - 1))


class GW13(_BitBendCombo):
    name = "BitBend Mirror"
    gen_fn = staticmethod(wavegen.gen13)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.pos_low = 2        # SET_0 bits 0-2
        self.pos_high = 7       # SET_1 bits 3-7

    def _walk(self, rng, phase):
        self.pos_low = (self.pos_low + _coin(rng)) % 3
        self.pos_high = 3 + ((self.pos_high - 3 + _coin(rng) + 5) % 5)

    def _manip(self, phase):
        addr = phase & ~((1 << (self.pos_low + 1)) - 1)
        return addr | (((1 << (self.pos_high + 1)) - 1) & ~((1 << 3) - 1))


class GW14(_BitBendCombo):
    name = "BitBend Triple"
    gen_fn = staticmethod(wavegen.gen14)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = 1     # XOR bits 0-1
        self.p2 = 4     # HOLD bits 2-4
        self.p3 = 9     # SET_0 bits 5-9
        self.held = 0

    def _walk(self, rng, phase):
        self.p1 = (self.p1 + _coin(rng)) % 2
        self.p2 = 2 + ((self.p2 - 2 + _coin(rng) + 3) % 3)
        self.p3 = 5 + ((self.p3 - 5 + _coin(rng) + 5) % 5)
        hm = ((1 << (self.p2 + 1)) - 1) & ~((1 << 2) - 1)
        self.held = phase & hm

    def _manip(self, phase):
        addr = phase ^ ((1 << (self.p1 + 1)) - 1)
        hm = ((1 << (self.p2 + 1)) - 1) & ~((1 << 2) - 1)
        addr = (addr & ~hm) | (self.held & hm)
        return addr & ~(((1 << (self.p3 + 1)) - 1) & ~((1 << 5) - 1))


class GW16(_BitBendCombo):
    name = "BitBend Triple B"
    gen_fn = staticmethod(wavegen.gen16)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.p1 = 1     # SET_0 bits 0-1
        self.p2 = 4     # HOLD bits 2-4
        self.p3 = 9     # SET_1 bits 5-9
        self.held = 0

    def _walk(self, rng, phase):
        self.p1 = (self.p1 + _coin(rng)) % 2
        self.p2 = 2 + ((self.p2 - 2 + _coin(rng) + 3) % 3)
        self.p3 = 5 + ((self.p3 - 5 + _coin(rng) + 5) % 5)
        hm = ((1 << (self.p2 + 1)) - 1) & ~((1 << 2) - 1)
        self.held = phase & hm

    def _manip(self, phase):
        addr = phase & ~((1 << (self.p1 + 1)) - 1)
        hm = ((1 << (self.p2 + 1)) - 1) & ~((1 << 2) - 1)
        addr = (addr & ~hm) | (self.held & hm)
        return addr | (((1 << (self.p3 + 1)) - 1) & ~((1 << 5) - 1))


# ====================================================== GW15: Sweep (two clocks)
class GW15(_WaveAlgo):
    name = "BitBend Sweep"

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.cinc = 0
        self.c1 = self.c2 = 0
        self.last1 = self.last2 = 0
        self.pos_low = 2        # HOLD bits 0-2 (slow)
        self.pos_high = 6       # HOLD bits 3-6 (fast)
        self.held_low = 0
        self.held_high = 0

    def set_params(self, p1, p2):
        self.speed = _lerp(0.1, 4.0, p1)
        self.cinc = freq_to_inc(_lerp(0.5, 20.0, p2))

    def _generate(self):
        wavegen.gen15(self.buf, self.rng)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng, buf = self.rng, self.buf
        c1, c2, l1, l2 = self.c1, self.c2, self.last1, self.last2
        pl, ph_, hl, hh = self.pos_low, self.pos_high, self.held_low, self.held_high
        cinc, slow = self.cinc, self.cinc // 3
        speed, frac = self.speed, self.frac
        phase = self.wave.playback_phase
        for i in range(n):
            c1 = (c1 + slow) & M
            s1 = c1 & 0x80000000
            if s1 and not l1:
                pl = (pl + _coin(rng)) % 3
                hl = phase & ((1 << (pl + 1)) - 1)
            l1 = s1
            c2 = (c2 + cinc) & M
            s2 = c2 & 0x80000000
            if s2 and not l2:
                ph_ = 3 + ((ph_ - 3 + _coin(rng) + 4) % 4)
                hmh = ((1 << (ph_ + 1)) - 1) & ~((1 << 3) - 1)
                hh = phase & hmh
            l2 = s2
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= N:
                phase = 0
            addr = phase
            hml = (1 << (pl + 1)) - 1
            addr = (addr & ~hml) | (hl & hml)
            hmh = ((1 << (ph_ + 1)) - 1) & ~((1 << 3) - 1)
            addr = (addr & ~hmh) | (hh & hmh)
            out[i] = buf[addr % N]
        self.c1, self.c2, self.last1, self.last2 = c1, c2, l1, l2
        self.pos_low, self.pos_high, self.held_low, self.held_high = pl, ph_, hl, hh
        self.frac = frac
        self.wave.playback_phase = phase
        return out


def build(ctx) -> list[Algo]:
    # Order matches the firmware: Chaos, Sparse, Dual, Freeze, Ping, Mirror,
    # Triple, Sweep, Triple B
    return [GW8(ctx), GW9(ctx), GW10(ctx), GW11(ctx), GW12(ctx),
            GW13(ctx), GW14(ctx), GW15(ctx), GW16(ctx)]
