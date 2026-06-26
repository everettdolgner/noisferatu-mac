"""
dsp_core.py — Shared DSP primitives for the NOISFERATU Python port.

Mirrors the building blocks in the original Arduino firmware (algos.h / hardware.h):
  * xorshift32 PRNG driving noise1() / rand12()  (one shared instance, as on hardware)
  * 32-bit phase accumulators with automatic uint32 wrapping
  * triangle/saw extraction from the top 10 bits of a phase accumulator
  * the 4000-sample waveform buffer shared by Bank 1 and Bank 3

Everything runs at the original 16 kHz to preserve the lo-fi character.
"""

from __future__ import annotations

import numpy as np

SAMPLE_RATE: int = 16000
DAC_CENTER: int = 511

# freq -> 32-bit phase increment:  inc = freq * (2**32 / SAMPLE_RATE)
# For 16 kHz this is the firmware's magic constant 268435.456.
PHASE_PER_HZ: float = (2 ** 32) / SAMPLE_RATE          # 268435.456
WAVEFORM_SIZE: int = 4000                              # shared buffer length


# ======================================================================
# PRNG — xorshift32, identical to the firmware's noise1()/rand12()
# ======================================================================
class Rng:
    """xorshift32 PRNG. A single instance is shared by every algorithm,
    exactly like the global `rng` on the hardware."""

    __slots__ = ("state",)

    def __init__(self, seed: int = 0x12345678) -> None:
        self.state = seed & 0xFFFFFFFF

    def _step(self) -> int:
        s = self.state
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self.state = s & 0xFFFFFFFF
        return self.state

    def noise1(self) -> int:
        """Signed 10-bit white noise (-512..+511)."""
        return (self._step() & 0x3FF) - 512

    def rand12(self) -> int:
        """0..4095."""
        return self._step() & 0x0FFF

    def rand16(self) -> int:
        """0..65535 (stands in for Arduino random(0, 65536))."""
        return self._step() & 0xFFFF

    def randf(self) -> float:
        """Uniform 0.0..1.0 (16-bit resolution)."""
        return self.rand16() * (1.0 / 65536.0)


# ======================================================================
# Scalar helpers (used inside per-sample loops)
# ======================================================================
def tri10(phase_u32: int) -> int:
    """Triangle (-512..+511) from the top 10 bits of a uint32 phase.
    Matches the firmware idiom: p = phase>>22; p<512 ? (p<<1)-512 : 1535-(p<<1)."""
    p = (phase_u32 >> 22) & 0x3FF
    return (p << 1) - 512 if p < 512 else 1535 - (p << 1)


def lfo10(phase_u32: int) -> int:
    """Unipolar triangle LFO (0..1023) from the top 10 bits of a uint32 phase.
    Firmware idiom: p<512 ? (p<<1) : 2047-(p<<1)."""
    p = (phase_u32 >> 22) & 0x3FF
    return (p << 1) if p < 512 else 2047 - (p << 1)


def saw10(phase_u32: int) -> int:
    """Signed saw (-512..+511) from the top 10 bits: (phase>>22) - 512."""
    return ((phase_u32 >> 22) & 0x3FF) - 512


def freq_to_inc(freq_hz: float) -> int:
    """Frequency (Hz) -> 32-bit phase increment."""
    return int(freq_hz * PHASE_PER_HZ) & 0xFFFFFFFF


# ======================================================================
# Vector helpers (used by the easily-vectorisable algorithms)
# ======================================================================
def phase_block(start: int, inc: int, n: int) -> tuple[np.ndarray, int]:
    """Return (phase_values, next_start) for `n` samples of a phase accumulator.

    phase_values[i] is the accumulator AFTER adding inc, matching the firmware
    which advances the phase *before* reading it each sample.
    """
    idx = np.arange(1, n + 1, dtype=np.uint64)
    ph = (np.uint64(start) + np.uint64(inc) * idx) & np.uint64(0xFFFFFFFF)
    next_start = int(ph[-1]) if n else start
    return ph.astype(np.uint32), next_start


def tri10_vec(ph: np.ndarray) -> np.ndarray:
    """Vectorised triangle (-512..+511) from uint32 phase array."""
    p = (ph >> np.uint32(22)) & np.uint32(0x3FF)
    p = p.astype(np.int32)
    return np.where(p < 512, (p << 1) - 512, 1535 - (p << 1)).astype(np.int32)


def lfo10_vec(ph: np.ndarray) -> np.ndarray:
    """Vectorised unipolar triangle LFO (0..1023) from uint32 phase array."""
    p = (ph >> np.uint32(22)) & np.uint32(0x3FF)
    p = p.astype(np.int32)
    return np.where(p < 512, p << 1, 2047 - (p << 1)).astype(np.int32)


def saw10_vec(ph: np.ndarray) -> np.ndarray:
    """Vectorised signed saw (-512..+511) from uint32 phase array."""
    return (((ph >> np.uint32(22)) & np.uint32(0x3FF)).astype(np.int32)) - 512


# ======================================================================
# Shared waveform buffer (Bank 1 + Bank 3)
# ======================================================================
class WaveBuffer:
    """The 4000-sample buffer and shared playback cursor.

    On the hardware `waveformBuffer[]` and `playbackPhase` are globals shared by
    the wavetable bank and the bitbend bank — both read/scrub the same buffer."""

    __slots__ = ("buf", "playback_phase")

    def __init__(self) -> None:
        self.buf = np.zeros(WAVEFORM_SIZE, dtype=np.int16)
        self.playback_phase = 0          # shared integer cursor (0..WAVEFORM_SIZE-1)


class Algo:
    """Base class for every algorithm.

    Subclasses implement:
      set_params(p1, p2)  — map normalised pots (0..1) to internal state
      render(n)           — return an int32 ndarray of `n` samples (-512..1023-ish)
    Wavetable/bitbend algos may also implement on_select() to (re)generate the buffer.
    """

    name: str = "?"

    def __init__(self, ctx) -> None:
        self.ctx = ctx
        self.rng: Rng = ctx.rng
        self.sr: int = ctx.sample_rate

    def set_params(self, p1: float, p2: float) -> None:  # pragma: no cover - override
        pass

    def on_select(self) -> None:
        """Called when this algorithm becomes active (used by wavetable banks)."""
        pass

    def render(self, n: int) -> np.ndarray:  # pragma: no cover - override
        return np.zeros(n, dtype=np.int32)
