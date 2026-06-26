# NOISFERATU Python Port — Status

A macOS Python/PyQt6 + sounddevice port of rob-scape's **NOISFERATU** generative
texture synth (Arduino original: https://github.com/rob-scape/noisferatu).
Runs at the original **16 kHz** for authentic lo-fi character.

_Last updated: 2026-06-25_

## ✅ Done (this session)

All **45 algorithms across 5 banks** ported and verified, full audio engine, full UI.

### Project layout
```
Noisferatu/
├── main.py                      entry point (QApplication + window)
├── engine.py                    audio engine: sounddevice callback, routing, effects chain
├── dsp_core.py                  shared primitives: xorshift32 RNG, phase/osc helpers, WaveBuffer
├── vinyl_crackle.npy            REAL 32000-sample crackle, extracted from firmware sample.h
├── requirements.txt
├── algorithms/
│   ├── bank1_wavetables.py      9 generative-waveform scrubbers (GW1-7,17,18)
│   ├── bank2_noisy.py           9 noisy textures (latched/dust/FM/gates/crackle/…)
│   ├── bank3_bitbend.py         9 address-manipulation algos (GW8-16)
│   ├── bank4_blips.py           9 tonal/blip generators
│   ├── bank5_logic.py           9 bitwise logic combinations
│   └── wavegen.py               shared 4000-sample buffer generators (used by banks 1 & 3)
└── ui/
    ├── main_window.py           window + 30 fps refresh timer
    ├── hardware_panel.py        5 knobs, 3 buttons, 4-digit 7-seg display, name labels
    └── waveform.py              oscilloscope view of post-effects output
```

### Verified
- All 45 algorithms render finite output, correct shape, over multiple pot settings & blocks.
- Global effects chain matches firmware exactly: sample-rate decimation (1–40×, A4) →
  bitcrush (10→1 bit, A3) → symmetric ±1 dither → quadratic master volume (A5) → clamp ±512.
- Real CoreAudio output stream opens and the callback runs.
- UI builds, paints all 45 display states, knobs drive engine, buttons cycle bank/algo.
- **Performance: ~0.4 ms/block (~1% of the 32 ms budget)** even on the heaviest
  per-sample algorithms — huge real-time headroom.

### How to run
```bash
cd /Users/machineoil/Documents/Claude/Projects/Noisferatu
source .venv/bin/activate        # venv is Python 3.11 (see note below)
python main.py
```
Controls: knobs drag vertically (or scroll). PREV/ALGO step within a bank; BANK cycles banks.

## ⚠️ Notes & faithful deviations
- **Python 3.11 required.** PyQt6's Qt platform plugins failed to load under Python 3.14
  on this machine; the venv was rebuilt on `python3.11` (3.11.15) and works. Matches the
  spec's target.
- **Vinyl crackle** uses the actual recording from the firmware (`sample.h`), extracted to
  `vinyl_crackle.npy` (32000 × int16, value >> 6 on playback) — not a synthetic stand-in.
- **Logic NOR/NAND/XNOR and Highpass Noise** output 0..1023 (positive, DC-offset) exactly
  as the hardware does (the `& 0x3FF` mask), and clip after the volume stage — that gnarly
  clipped character is intentional and preserved.
- **GW8/GW9 bit pointer**: the firmware lets the `uint8` bit-position underflow (UB on the
  MCU); here it's kept in 0..11 via Python modulo — the musically intended behaviour.
- **Fast Triangle bitcrush**: firmware's per-note crush can exceed 10 bits (negative shift,
  UB); treated as "clean" (no crush) above 10 bits.
- **Phrygian Tri / FM Noise**: note frequency is recomputed at the trigger instant rather
  than at control rate — inaudible difference, slightly cleaner pitch tracking.
- **Single shared xorshift32 RNG** like the hardware; the dither stage draws from the same
  RNG, interleaved with algorithm noise (character matches, exact sample stream won't).
- **Regen timing**: GW1–6 regenerate on the periodic timer (2–5 s); GW7/GW18 on select only;
  GW17 self-regenerates ~30% of its buffer every 3 s (harmonic drift). GW4 = manual only.
- Output is **mono** (the original drives a single 10-bit DAC).
- The hardware's display-timeout long-press toggle is not ported (irrelevant to the GUI).

## ⏭️ Next
1. **Listen / A-B against the original** on speakers — tune pot tapers and any algo that
   feels off vs. the hardware (esp. the BitBend bank addressing and GW17 drone).
2. **Output-device selector** in the UI (sounddevice `query_devices`) — handy on the Mac mini.
3. **Record-to-WAV** button (freeze a take of a texture).
4. Knob value tooltips / double-click-to-default; maybe keyboard shortcuts for bank/algo.
5. Optional: vectorize the remaining per-sample algos with numpy — **not needed** for perf,
   purely cosmetic.
6. **Package as a macOS launcher** (py2app / .app), matching the Wavetable Converter setup.
7. Consider a CV/automation or simple LFO mod source for the pots (the original is
   hands-on; a slow auto-wander on POT1/POT2 could be a fun addition).

## Reference
- Firmware studied: `Noisferatu.ino`, `algos.h` (3636 lines), `params.h`, `hardware.h`,
  `sample.h`. Clone left at `/tmp/noisferatu_src` (re-clone if gone).
- Phase math: `inc = freq * 2**32 / 16000` (firmware constant 268435.456). Triangle/saw
  taken from the top 10 bits of a uint32 accumulator.
