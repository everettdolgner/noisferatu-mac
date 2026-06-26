# NOISFERATU — CLAP / VST3 / AU plugin

A JUCE plugin port of the NOISFERATU generative texture synth, built directly from the
verified Python port in the parent directory (itself a port of
[rob-scape's Arduino original](https://github.com/rob-scape/noisferatu)).

All 45 algorithms across 5 banks, the full effects chain, and the shared xorshift32 RNG
are ported to C++ and **verified bit-for-bit against the Python reference** (see
`tools/golden_test.py`).

## How it works

The DSP core runs at the original **16 kHz**, exactly like the firmware — that fixed rate
*is* the lo-fi character. The engine generates 16 kHz blocks and linearly upsamples them
to the host sample rate (`Engine::nextHostSample`). The plugin is an **instrument**: it
produces a continuous texture (no MIDI required); the mono engine is copied to all output
channels.

### Parameters

| Param      | Range | Notes                                             |
|------------|-------|---------------------------------------------------|
| `bank`     | 0–4   | Wavetables / Noisy / BitBend / Blips / Logic      |
| `algo`     | 0–8   | Algorithm within the bank                         |
| `pot1`     | 0–1   | Per-algorithm parameter 1                         |
| `pot2`     | 0–1   | Per-algorithm parameter 2                         |
| `bitcrush` | 0–1   | 10-bit (CW) down to 1-bit                         |
| `rate`     | 0–1   | Sample-rate reduction (1× … 40× decimation)       |
| `volume`   | 0–1   | Master volume (quadratic)                         |

## Source layout

```
Source/
  DspCore.h            RNG, phase helpers, freqToInc, pymod, Algo base, Ctx
  Wavegen.h            shared-buffer generators (Bank 1 & 3)
  VinylCrackleData.h   generated: 32000-sample crackle embedded from vinyl_crackle.npy
  Bank1.h … Bank5.h    the 45 algorithms (header-only)
  Engine.h             routing + effects chain + 16 kHz FIFO + upsampler (JUCE-free)
  PluginProcessor.*    JUCE AudioProcessor + APVTS
  PluginEditor.*       controls, bank.algo readout, scope
tools/
  gen_crackle_header.py   regenerate VinylCrackleData.h from the .npy
  golden_test.cpp/.py     bit-exact C++↔Python verification harness
```

`DspCore.h` … `Engine.h` have **no JUCE dependency**, so the DSP compiles and is tested
standalone.

## Build

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE 8.0.4 and clap-juce-extensions are
fetched automatically on first configure (network required).

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j           # builds Standalone, VST3, AU, CLAP
```

With `COPY_PLUGIN_AFTER_BUILD`, the VST3/AU/CLAP are copied into your user plugin folders.
To build a single format: `cmake --build build --target Noisferatu_Standalone`.

## Verifying faithfulness

```sh
clang++ -std=c++20 -O2 -I Source tools/golden_test.cpp -o /tmp/golden_test_cpp
PYTHONPATH=.. python tools/golden_test.py /tmp/golden_test_cpp
# -> 45/45 algorithms match exactly.
```

## Notes / future work

- Buffer regeneration for the wavetable/bitbend banks happens on the audio thread on
  algorithm-select and on periodic regen timers, mirroring the firmware. It is bounded
  but not strictly real-time-safe; moving regen onto a background thread is a possible
  refinement.
- The editor is functional rather than a faithful reproduction of the hardware panel.
- Upsampling is linear; a zero-order-hold option could be added for an even grittier feel.
