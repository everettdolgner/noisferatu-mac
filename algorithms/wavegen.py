"""
wavegen.py — Buffer generators for the shared 4000-sample waveform buffer.

These mirror the firmware's generateWaveformN() routines exactly: chunked
noise/silence/saw/triangle layout plus scattered triangle "blips". They are used
by both Bank 1 (wavetables) and Bank 3 (bitbend), which scrub the same buffer.

Generation is *not* in the audio hot path — it runs on algo-select and on the
periodic regen timer — so straightforward per-sample fills are fine.
"""

from __future__ import annotations

import numpy as np

from dsp_core import WAVEFORM_SIZE as N, SAMPLE_RATE as SR

_SPIN_GUARD = 200_000  # safety bound for the rare all-zero-chunk spin (matches HW intent)


# ---------------------------------------------------------------- low-level fills
def _noise_chunk(buf, rng, start, size, thresh=3):
    """Latched noise: re-roll the held value with probability thresh/10."""
    crushed = rng.noise1()
    for i in range(size):
        if rng.rand12() % 10 < thresh:
            crushed = rng.noise1()
        buf[start + i] = crushed


def _saw_chunk(buf, rng, start, size, freq, sweep_step=0.0):
    ph = 0.0
    f = freq
    for i in range(size):
        buf[start + i] = int((ph * 2.0 - 1.0) * 511.0)
        ph += f / SR
        if ph >= 1.0:
            ph -= 1.0
        f += sweep_step


def _tri_chunk(buf, rng, start, size, freq):
    ph = 0.0
    inc = freq / SR
    for i in range(size):
        tri = ph * 2.0 if ph < 0.5 else 2.0 - ph * 2.0
        buf[start + i] = int((tri * 2.0 - 1.0) * 511.0)
        ph += inc
        if ph >= 1.0:
            ph -= 1.0


def _blip(buf, rng, size, fmin, fmax, count=1, freq=None):
    """Scatter `count` triangle blips of length `size`. If `freq` is given it is
    reused (GW2's cached blip); otherwise a random freq in [fmin, fmax]."""
    if not (0 < size < N):
        return
    for _ in range(count):
        pos = rng.rand12() % (N - size)
        f = freq if freq is not None else fmin + (rng.rand12() % 1000) / 1000.0 * (fmax - fmin)
        ph = 0.0
        inc = f / SR
        for i in range(size):
            tri = ph * 2.0 if ph < 0.5 else 2.0 - ph * 2.0
            buf[pos + i] = int((tri * 2.0 - 1.0) * 511.0)
            ph += inc
            if ph >= 1.0:
                ph -= 1.0


def _chunk_size(rng, sizes):
    return sizes[rng.rand12() % 3]


# ---------------------------------------------------------------- chunk/noise+silence layout
def _gen_chunked(buf, rng, sizes, noise_prob, noise_thresh):
    """Generic 'chunk = noise or silence' layout (GW1/2/3/4/5/6)."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        size = _chunk_size(rng, sizes)
        if pos + size > N:
            size = N - pos
        if (rng.rand12() % 100) < noise_prob:
            _noise_chunk(buf, rng, pos, size, noise_thresh)
        else:
            buf[pos:pos + size] = 0
        pos += size


# ---------------------------------------------------------------- Bank 1 generators
def gen1(buf, rng):
    _gen_chunked(buf, rng, (32, 8, 64), 1, 3)
    _blip(buf, rng, 50, 220.0, 7000.0)


def gen2(buf, rng, blip_freq_set, blip_freq):
    _gen_chunked(buf, rng, (5, 2, 21), 3, 1)
    # GW2 caches its blip frequency until the algorithm is re-selected
    if not blip_freq_set:
        blip_freq = 400.0 + (rng.rand12() % 1000) / 1000.0 * (2503.0 - 400.0)
        blip_freq_set = True
    _blip(buf, rng, 77, 0, 0, freq=blip_freq)
    return blip_freq_set, blip_freq


def gen3(buf, rng):
    _gen_chunked(buf, rng, (0, 33, 1), 4, 2)
    # no blip for Spacey Pulses


def gen4(buf, rng):
    _gen_chunked(buf, rng, (16, 4, 32), 25, 3)
    _blip(buf, rng, 30, 500.0, 3000.0)


def gen5(buf, rng):
    _gen_chunked(buf, rng, (0, 6, 48), 22, 3)
    _blip(buf, rng, 12, 600.0, 2500.0)


def gen6(buf, rng):
    _gen_chunked(buf, rng, (20, 5, 40), 35, 3)
    _blip(buf, rng, 15, 700.0, 3500.0)


def _gen_noise_or_saw(buf, rng, sizes, noise_prob, saw_prob, saw_min, saw_max):
    """GW7/GW8 layout: chunk = (saw or noise) or silence."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        size = _chunk_size(rng, sizes)
        if pos + size > N:
            size = N - pos
        if (rng.rand12() % 100) < noise_prob:
            if (rng.rand12() % 100) < saw_prob:
                freq = saw_min + (rng.rand12() % 1000) / 1000.0 * (saw_max - saw_min)
                _saw_chunk(buf, rng, pos, size, freq)
            else:
                _noise_chunk(buf, rng, pos, size, 3)
        else:
            buf[pos:pos + size] = 0
        pos += size


def gen7(buf, rng):
    _gen_noise_or_saw(buf, rng, (0, 6, 48), 22, 50, 30.0, 1000.0)
    _blip(buf, rng, 12, 600.0, 2500.0)


# ---- GW17 Harmonic Drone Builder (noise / harmonic-pair / silence) ----
def _harmonic_chunk(buf, rng, start, size, harmonics):
    h1 = rng.rand12() % 4
    h2 = rng.rand12() % 4
    while h2 == h1:
        h2 = rng.rand12() % 4
    f1, f2 = harmonics[h1], harmonics[h2]
    ph1 = ph2 = 0.0
    i1, i2 = f1 / SR, f2 / SR
    for i in range(size):
        t1 = ph1 * 2.0 if ph1 < 0.5 else 2.0 - ph1 * 2.0
        t2 = ph2 * 2.0 if ph2 < 0.5 else 2.0 - ph2 * 2.0
        s1 = int((t1 * 2.0 - 1.0) * 255.0)
        s2 = int((t2 * 2.0 - 1.0) * 255.0)
        buf[start + i] = s1 + s2
        ph1 += i1
        if ph1 >= 1.0:
            ph1 -= 1.0
        ph2 += i2
        if ph2 >= 1.0:
            ph2 -= 1.0


def gen17_full(buf, rng):
    """Returns the chosen root frequency (kept for partial regens)."""
    root = 80.0 + (rng.rand12() % 1000) / 1000.0 * (240.0 - 80.0)
    harmonics = (root, root * 2.0, root * 3.0, root * 4.0)
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        roll = rng.rand12() % 100
        if roll < 30:                                    # NOISE
            size = min(2, N - pos)
            _noise_chunk(buf, rng, pos, size, 3)
            pos += size
        elif roll < 80:                                  # HARMONIC
            size = min(324, N - pos)
            _harmonic_chunk(buf, rng, pos, size, harmonics)
            pos += size
        else:                                            # SILENCE
            size = (31, 127, 251)[rng.rand12() % 3]
            if pos + size > N:
                size = N - pos
            buf[pos:pos + size] = 0
            pos += size
    return root


def gen17_partial(buf, rng, root, octave_mult):
    harmonics = (root * octave_mult, root * octave_mult * 2.0,
                 root * octave_mult * 3.0, root * octave_mult * 4.0)
    for _ in range(5):
        start = rng.rand12() % (N - 324)
        if rng.rand12() % 100 < 40:
            _noise_chunk(buf, rng, start, 2, 3)
        else:
            _harmonic_chunk(buf, rng, start, 324, harmonics)


def gen18(buf, rng):
    """BitBend Quad source: sparse super-latched noise + big silence + 3 big blips."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        if (rng.rand12() % 100) < 12:
            size = min(57, N - pos)
            _noise_chunk(buf, rng, pos, size, 1)         # super-latched (10% update)
            pos += size
        else:
            size = (32, 146, 299)[rng.rand12() % 3]
            if pos + size > N:
                size = N - pos
            buf[pos:pos + size] = 0
            pos += size
    _blip(buf, rng, 99, 600.0, 900.0, count=3)


# ---------------------------------------------------------------- Bank 3 generators
def gen8(buf, rng):
    _gen_noise_or_saw(buf, rng, (0, 8, 64), 55, 40, 40.0, 800.0)
    _blip(buf, rng, 16, 800.0, 3000.0)


def gen12(buf, rng):
    """BitBend Ping source: high-freq triangles or noise, mostly silence."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        size = _chunk_size(rng, (0, 7, 31))
        if pos + size > N:
            size = N - pos
        if (rng.rand12() % 100) < 17:
            if (rng.rand12() % 100) < 37:
                freq = 3000.0 + (rng.rand12() % 1000) / 1000.0 * (4000.0 - 3000.0)
                _tri_chunk(buf, rng, pos, size, freq)
            else:
                _noise_chunk(buf, rng, pos, size, 3)
        else:
            buf[pos:pos + size] = 0
        pos += size
    _blip(buf, rng, 12, 600.0, 2500.0)


def gen13(buf, rng):
    """BitBend Mirror source: swept saws (mostly) or noise."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        size = _chunk_size(rng, (0, 7, 31))
        if pos + size > N:
            size = N - pos
        if (rng.rand12() % 100) < 17:
            if (rng.rand12() % 100) < 79:
                _saw_chunk(buf, rng, pos, size, 30.0, sweep_step=1.0)
            else:
                _noise_chunk(buf, rng, pos, size, 3)
        else:
            buf[pos:pos + size] = 0
        pos += size
    _blip(buf, rng, 12, 600.0, 2500.0)


def gen14(buf, rng):
    """BitBend Triple source: noise(33) or big silence, 3 low blips."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        if (rng.rand12() % 100) < 21:
            rng.rand12()                                  # HW rolls a coin then uses 33
            size = min(33, N - pos)
            _noise_chunk(buf, rng, pos, size, 3)
            pos += size
        else:
            size = (63, 127, 251)[rng.rand12() % 3]
            if pos + size > N:
                size = N - pos
            buf[pos:pos + size] = 0
            pos += size
    _blip(buf, rng, 31, 100.0, 300.0, count=3)


def gen15(buf, rng):
    """BitBend Sweep source: Dense Microglitch style (noise or silence)."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        size = _chunk_size(rng, (5, 0, 47))
        if pos + size > N:
            size = N - pos
        if (rng.rand12() % 100) < 7:
            _noise_chunk(buf, rng, pos, size, 3)
        else:
            buf[pos:pos + size] = 0
        pos += size
    _blip(buf, rng, 66, 2000.0, 4200.0)


def gen16(buf, rng):
    """BitBend Triple B source: noise(47) or big silence, 3 high blips."""
    pos = 0
    guard = 0
    while pos < N and guard < _SPIN_GUARD:
        guard += 1
        if (rng.rand12() % 100) < 17:
            size = min(47, N - pos)
            _noise_chunk(buf, rng, pos, size, 3)
            pos += size
        else:
            size = (41, 111, 253)[rng.rand12() % 3]
            if pos + size > N:
                size = N - pos
            buf[pos:pos + size] = 0
            pos += size
    _blip(buf, rng, 67, 4000.0, 6000.0, count=3)
