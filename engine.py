"""
engine.py — NOISFERATU audio engine.

Owns the shared DSP context, the 45 algorithm instances (5 banks x 9), bank/algo
routing, and the global effects chain that follows every algorithm — exactly as in
the firmware's TC5_Handler ISR:

    algorithm  ->  sample-rate reduction (decimate + hold)
               ->  bitcrush (10-bit down to 1-bit)
               ->  symmetric +/-1 dither
               ->  master volume (quadratic curve) + clamp to +/-512

Audio is generated block-by-block in the sounddevice callback at 16 kHz.
"""

from __future__ import annotations

import numpy as np
import sounddevice as sd

from dsp_core import Rng, WaveBuffer, SAMPLE_RATE
from algorithms import (
    bank1_wavetables, bank2_noisy, bank3_bitbend, bank4_blips, bank5_logic,
)

BLOCK_SIZE = 512

BANK_NAMES = [
    "Wavetables",
    "Noisy Textures",
    "BitBend",
    "Blips & Tones",
    "Logic Disorder",
]


class _Ctx:
    """Shared context handed to every algorithm."""

    def __init__(self):
        self.rng = Rng()
        self.sample_rate = SAMPLE_RATE
        self.wave = WaveBuffer()


class Engine:
    def __init__(self):
        self.ctx = _Ctx()
        self.banks = [
            bank1_wavetables.build(self.ctx),
            bank2_noisy.build(self.ctx),
            bank3_bitbend.build(self.ctx),
            bank4_blips.build(self.ctx),
            bank5_logic.build(self.ctx),
        ]

        self.bank = 0
        self.algo = 0
        self._pending = None            # (bank, algo) switch request from UI thread

        # Normalised control values (0..1), written by the UI thread.
        self.pot1 = 0.5
        self.pot2 = 0.5
        self.bitcrush_pot = 1.0         # CW = clean (10 bits)
        self.rate_pot = 1.0             # CW = full rate
        self.volume_pot = 0.8

        # Effects-chain persistent state.
        self._decim_counter = 0
        self._held = 0

        # Scope tap: most recent block of post-effects float samples (for the UI).
        self.scope = np.zeros(BLOCK_SIZE, dtype=np.float32)

        self._stream: sd.OutputStream | None = None
        self._current().on_select()     # generate initial buffer (Bank 1, Algo 0)

    # ----------------------------------------------------------------- selection
    def _current(self):
        return self.banks[self.bank][self.algo]

    @property
    def algo_name(self) -> str:
        return self._current().name

    @property
    def bank_name(self) -> str:
        return BANK_NAMES[self.bank]

    @property
    def display_text(self) -> str:
        """e.g. '1.03' (1-indexed bank.algo), matching the hardware's 7-seg."""
        return f"{self.bank + 1}.{self.algo + 1:02d}"

    def next_bank(self):
        self._pending = ((self.bank + 1) % 5, 0)

    def next_algo(self):
        self._pending = (self.bank, (self.algo + 1) % 9)

    def prev_algo(self):
        self._pending = (self.bank, (self.algo - 1) % 9)

    def _apply_pending(self):
        if self._pending is None:
            return
        self.bank, self.algo = self._pending
        self._pending = None
        self._current().on_select()

    # ----------------------------------------------------------------- effects map
    def _decimation(self) -> int:
        n = self.rate_pot
        if n > 0.9:
            return 1
        return 40 - int((n / 0.9) * 39.0)

    def _bitcrush_mask(self) -> int:
        bc = 1 + int(self.bitcrush_pot * 9.0)          # 1..10 bits
        bc = min(max(bc, 1), 10)
        return ~((1 << (10 - bc)) - 1)

    def _master_volume(self) -> int:
        vol_temp = int(self.volume_pot * 4095.0) >> 4   # 0..255
        return (vol_temp * vol_temp) >> 8               # quadratic curve

    # ----------------------------------------------------------------- audio callback
    def _callback(self, outdata, frames, time_info, status):  # noqa: ARG002
        self._apply_pending()

        algo = self._current()
        algo.set_params(self.pot1, self.pot2)
        raw = algo.render(frames)                       # int32, ~ -512..1023

        decim = self._decimation()
        mask = self._bitcrush_mask()
        master = self._master_volume()
        rng = self.ctx.rng
        counter, held = self._decim_counter, self._held

        out = np.empty(frames, dtype=np.float32)
        for i in range(frames):
            counter += 1
            if counter >= decim:
                counter = 0
                held = int(raw[i])
            crushed = held & mask
            dithered = crushed + ((rng.rand12() & 1) - (rng.rand12() & 1))
            v = (dithered * master) >> 8
            if v > 511:
                v = 511
            elif v < -512:
                v = -512
            out[i] = v / 512.0

        self._decim_counter, self._held = counter, held
        self.scope = out
        outdata[:, 0] = out

    # ----------------------------------------------------------------- lifecycle
    def start(self):
        if self._stream is not None:
            return
        self._stream = sd.OutputStream(
            samplerate=SAMPLE_RATE,
            blocksize=BLOCK_SIZE,
            channels=1,
            dtype="float32",
            callback=self._callback,
        )
        self._stream.start()

    def stop(self):
        if self._stream is not None:
            self._stream.stop()
            self._stream.close()
            self._stream = None
