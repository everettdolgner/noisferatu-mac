"""
bank1_wavetables.py — Bank 1: Wavetables / Generative Waveforms (9 algorithms)

Each algorithm scrubs the shared 4000-sample waveform buffer (ctx.wave) which it
(re)generates on select and on the firmware's periodic regen timer. Playback uses a
fractional phase so pot1 acts as a playback-speed / pitch control. The buffer cursor
(wave.playback_phase) is shared with Bank 3, exactly as on the hardware.
"""

from __future__ import annotations

import numpy as np

from dsp_core import Algo, WAVEFORM_SIZE as N, freq_to_inc
from . import wavegen

M = 0xFFFFFFFF
S = 16000  # samples per second (for regen intervals)


def _lerp(lo, hi, t):
    return lo + (hi - lo) * t


class _WaveAlgo(Algo):
    """Base: owns the shared buffer/cursor and an optional periodic regen timer."""
    regen_interval: int | None = None

    def __init__(self, ctx):
        super().__init__(ctx)
        self.wave = ctx.wave
        self.frac = 0.0
        self.regen_counter = 0

    @property
    def buf(self):
        return self.wave.buf

    def _generate(self):  # override
        pass

    def on_select(self):
        self._generate()
        self.regen_counter = 0

    def _maybe_regen(self, n):
        if self.regen_interval is None:
            return
        self.regen_counter += n
        if self.regen_counter >= self.regen_interval:
            self.regen_counter = 0
            self._generate()


# ====================================================== GW1/2/3: chunked silence
class _ChunkedSilence(_WaveAlgo):
    silence_sizes = (32, 8, 64)

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.silence_prob = 0.0
        self.remaining = 0

    def _speed(self, p1):
        return _lerp(0.01, 2.0, p1)

    def render(self, n):
        self._maybe_regen(n)
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        buf = self.buf
        speed, prob = self.speed, self.silence_prob
        rem, frac = self.remaining, self.frac
        phase = self.wave.playback_phase
        sizes = self.silence_sizes
        for i in range(n):
            if rem == 0:
                size = sizes[rng.rand12() % 3]
                if (rng.rand12() % 1000) / 1000.0 < prob:
                    rem = size
            if rem > 0:
                rem -= 1
                frac += speed
                inc = int(frac)
                frac -= inc
                phase += inc
                while phase >= N:
                    phase -= N
                out[i] = 0
                continue
            out[i] = buf[phase]
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            while phase >= N:
                phase -= N
        self.remaining, self.frac = rem, frac
        self.wave.playback_phase = phase
        return out


class GW1(_ChunkedSilence):
    name = "Sparse Glitch"
    silence_sizes = (32, 8, 64)
    regen_interval = 2 * S

    def set_params(self, p1, p2):
        self.speed = self._speed(p1)
        self.silence_prob = 0.2 - (p2 * 0.2)

    def _generate(self):
        wavegen.gen1(self.buf, self.rng)


class GW2(_ChunkedSilence):
    name = "Dense Microglitch"
    silence_sizes = (5, 2, 21)
    regen_interval = 5 * S

    def __init__(self, ctx):
        super().__init__(ctx)
        self.blip_freq_set = False
        self.blip_freq = 2100.0

    def set_params(self, p1, p2):
        self.speed = self._speed(p1)
        self.silence_prob = 1.0 - p2

    def on_select(self):
        self.blip_freq_set = False          # re-roll cached blip freq on select
        super().on_select()

    def _generate(self):
        self.blip_freq_set, self.blip_freq = wavegen.gen2(
            self.buf, self.rng, self.blip_freq_set, self.blip_freq)


class GW3(_ChunkedSilence):
    name = "Spacey Pulses"
    silence_sizes = (64, 128, 256)
    regen_interval = 2 * S

    def set_params(self, p1, p2):
        self.speed = self._speed(p1)
        self.silence_prob = 0.1 - (p2 * 0.1)

    def _generate(self):
        wavegen.gen3(self.buf, self.rng)


# ====================================================== GW4: random jump glitch
class GW4(_WaveAlgo):
    name = "Random Jump"
    regen_interval = None  # manual only

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.silence_prob = 0.0
        self.remaining = 0
        self.jump_counter = 0
        self.jump_period = S // 3

    def set_params(self, p1, p2):
        self.speed = _lerp(0.01, 2.0, p1)
        self.silence_prob = 1.0 - p2
        self.jump_period = S // 3

    def _generate(self):
        wavegen.gen4(self.buf, self.rng)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        buf = self.buf
        speed, prob = self.speed, self.silence_prob
        rem, frac = self.remaining, self.frac
        jc, jp = self.jump_counter, self.jump_period
        phase = self.wave.playback_phase
        for i in range(n):
            jc += 1
            if jc >= jp:
                jc = 0
                phase = rng.rand12() % N
            if rem == 0:
                size = (16, 4, 32)[rng.rand12() % 3]
                rem = size if (rng.rand12() % 1000) / 1000.0 < prob else 0
            if rem > 0:
                rem -= 1
                out[i] = 0                  # silence: no phase advance (matches HW)
                continue
            out[i] = buf[phase]
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            while phase >= N:
                phase -= N
        self.remaining, self.frac = rem, frac
        self.jump_counter = jc
        self.wave.playback_phase = phase
        return out


# ====================================================== GW5/GW7: wandering window
class _WanderWindow(_WaveAlgo):
    WINDOW = N // 50  # 80

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.window_start = 0
        self.walk_counter = 0
        self.walk_period = S // 5

    def render(self, n):
        self._maybe_regen(n)
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        buf = self.buf
        speed = self.speed
        wstart, wc, wp = self.window_start, self.walk_counter, self.walk_period
        frac = self.frac
        phase = self.wave.playback_phase
        for i in range(n):
            wc += 1
            if wc >= wp:
                wc = 0
                step = (1 + rng.rand12() % 20) if (rng.rand12() & 1) else -(1 + rng.rand12() % 20)
                ns = wstart + step
                if ns < 0:
                    ns += N
                if ns >= N:
                    ns -= N
                wstart = ns
            wend = wstart + self.WINDOW
            if wend > N:
                wend = N
            if phase < wstart or phase >= wend:
                phase = wstart
            out[i] = buf[phase]
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= wend:
                phase = wstart
        self.window_start, self.walk_counter, self.frac = wstart, wc, frac
        self.wave.playback_phase = phase
        return out


class GW5(_WanderWindow):
    name = "Wander Window"
    regen_interval = 2 * S

    def set_params(self, p1, p2):
        self.speed = _lerp(0.01, 2.0, p1)
        walk = _lerp(0.01, 50.0, p2)
        self.walk_period = max(1, int(S / walk))

    def _generate(self):
        wavegen.gen5(self.buf, self.rng)


class GW7(_WanderWindow):
    name = "Noise/Saw Win"
    regen_interval = None  # generated on select only

    def set_params(self, p1, p2):
        self.speed = _lerp(0.1, 4.0, p1)
        walk = _lerp(0.5, 20.0, p2)
        self.walk_period = max(1, int(S / walk))

    def _generate(self):
        wavegen.gen7(self.buf, self.rng)


# ====================================================== GW6: manual window + spray
class GW6(_WaveAlgo):
    name = "Manual Window"
    regen_interval = 4 * S
    SPRAY = 30

    def __init__(self, ctx):
        super().__init__(ctx)
        self.window_start = 0
        self.window_size = N
        self.spray_counter = 0
        self.spray_offset = 0

    def set_params(self, p1, p2):
        self.window_start = int(p1 * (N - 1))
        self.window_size = 1 + int(p2 * (N - 1))
        if self.window_start + self.window_size > N:
            self.window_size = N - self.window_start

    def _generate(self):
        wavegen.gen6(self.buf, self.rng)

    def render(self, n):
        self._maybe_regen(n)
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        buf = self.buf
        wstart, wsize = self.window_start, self.window_size
        sc, soff, frac = self.spray_counter, self.spray_offset, self.frac
        phase = self.wave.playback_phase
        spray_period = wsize if wsize >= 100 else 100
        for i in range(n):
            sc += 1
            if sc >= spray_period:
                sc = 0
                soff = (rng.rand12() % (self.SPRAY * 2 + 1)) - self.SPRAY
            actual = wstart + soff
            while actual < 0:
                actual += N
            while actual >= N:
                actual -= N
            wend = actual + wsize
            if wend > N:
                wend = N
            if phase < actual or phase >= wend:
                phase = actual
            out[i] = buf[phase]
            frac += 0.5                     # fixed slow playback
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= wend:
                phase = actual
        self.spray_counter, self.spray_offset, self.frac = sc, soff, frac
        self.wave.playback_phase = phase
        return out


# ====================================================== GW17: harmonic drone builder
class GW17(_WaveAlgo):
    name = "Harmonic Drone"
    SPEED = 0.3
    REGEN = 3 * S
    STUTTER_MAX = 0.005

    def __init__(self, ctx):
        super().__init__(ctx)
        self.root = 150.0
        self.octave = 1.0
        self.stutter_prob = 0.0
        self.stutter_remaining = 0
        self.internal_counter = 0

    def set_params(self, p1, p2):
        if p1 < 0.2:
            self.octave = 0.5
        elif p1 < 0.4:
            self.octave = 1.0
        elif p1 < 0.6:
            self.octave = 2.0
        elif p1 < 0.8:
            self.octave = 4.0
        else:
            self.octave = 8.0
        self.stutter_prob = self.STUTTER_MAX * (1.0 - p2)

    def _generate(self):
        self.root = wavegen.gen17_full(self.buf, self.rng)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        buf = self.buf
        rem, ic, frac = self.stutter_remaining, self.internal_counter, self.frac
        phase = self.wave.playback_phase
        for i in range(n):
            ic += 1
            if ic >= self.REGEN:
                ic = 0
                wavegen.gen17_partial(buf, rng, self.root, self.octave)
            if rem == 0:
                size = (666, 1000, 1533)[rng.rand12() % 3]
                if (rng.rand12() % 1000) / 1000.0 < self.stutter_prob:
                    rem = size
            if rem > 0:
                rem -= 1
                out[i] = buf[phase]         # freeze (stutter): hold sample, no advance
                continue
            frac += self.SPEED
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= N:
                phase = 0
            out[i] = buf[phase]
        self.stutter_remaining, self.internal_counter, self.frac = rem, ic, frac
        self.wave.playback_phase = phase
        return out


# ====================================================== GW18: BitBend Quad
class GW18(_WaveAlgo):
    name = "BitBend Quad"
    regen_interval = None  # generated on select only

    def __init__(self, ctx):
        super().__init__(ctx)
        self.speed = 1.0
        self.clock_phase = 0
        self.clock_inc = 0
        self.last_clock = 0
        self.bp1, self.bp2, self.bp3, self.bp4 = 0, 2, 5, 8
        self.held = 0

    def set_params(self, p1, p2):
        self.speed = _lerp(0.1, 4.0, p1)
        self.clock_inc = freq_to_inc(_lerp(0.5, 20.0, p2))

    def _generate(self):
        wavegen.gen18(self.buf, self.rng)

    def render(self, n):
        out = np.empty(n, dtype=np.int32)
        rng = self.rng
        buf = self.buf
        cph, cinc, last = self.clock_phase, self.clock_inc, self.last_clock
        bp1, bp2, bp3, bp4, held = self.bp1, self.bp2, self.bp3, self.bp4, self.held
        speed, frac = self.speed, self.frac
        phase = self.wave.playback_phase
        for i in range(n):
            cph = (cph + cinc) & M
            state = cph & 0x80000000
            if state and not last:
                if rng.rand12() & 1:
                    bp1 = (bp1 + (1 if rng.rand12() & 1 else -1)) % 2
                    bp2 = 2 + ((bp2 - 2 + (1 if rng.rand12() & 1 else -1) + 2) % 2)
                    bp3 = 4 + ((bp3 - 4 + (1 if rng.rand12() & 1 else -1) + 3) % 3)
                    bp4 = 7 + ((bp4 - 7 + (1 if rng.rand12() & 1 else -1) + 3) % 3)
                    hold_mask = ((1 << (bp2 + 1)) - 1) & ~((1 << 2) - 1)
                    held = phase & hold_mask
            last = state
            frac += speed
            inc = int(frac)
            frac -= inc
            phase += inc
            if phase >= N:
                phase = 0
            addr = phase
            addr ^= (1 << (bp1 + 1)) - 1                          # XOR bits 0-1
            hold_mask = ((1 << (bp2 + 1)) - 1) & ~((1 << 2) - 1)  # HOLD bits 2-3
            addr = (addr & ~hold_mask) | (held & hold_mask)
            addr &= ~(((1 << (bp3 + 1)) - 1) & ~((1 << 4) - 1))   # SET_0 bits 4-6
            addr |= ((1 << (bp4 + 1)) - 1) & ~((1 << 7) - 1)      # SET_1 bits 7-9
            addr %= N
            out[i] = buf[addr]
        self.clock_phase, self.last_clock = cph, last
        self.bp1, self.bp2, self.bp3, self.bp4, self.held = bp1, bp2, bp3, bp4, held
        self.frac = frac
        self.wave.playback_phase = phase
        return out


def build(ctx) -> list[Algo]:
    return [GW1(ctx), GW2(ctx), GW3(ctx), GW4(ctx), GW5(ctx),
            GW6(ctx), GW7(ctx), GW17(ctx), GW18(ctx)]
