// Wavegen.h — Buffer generators for the shared 4000-sample waveform buffer.
// C++ port of wavegen.py; mirrors the firmware's generateWaveformN() routines exactly:
// chunked noise/silence/saw/triangle layouts plus scattered triangle "blips".
// Used by Bank 1 (wavetables) and Bank 3 (bitbend), which scrub the same buffer.
//
// Generation is not in the audio hot path (runs on algo-select and the periodic regen
// timer), so straightforward per-sample fills mirror the Python directly.
#pragma once

#include <algorithm>
#include "DspCore.h"

namespace noisferatu::wavegen {

inline constexpr int   N    = kWaveformSize;
inline constexpr int   SR   = kSampleRate;
inline constexpr int   SPIN_GUARD = 200000;   // safety bound for rare all-zero-chunk spin

// ---------------------------------------------------------------- low-level fills
inline void noiseChunk (std::int16_t* buf, Rng& rng, int start, int size, int thresh = 3)
{
    int crushed = rng.noise1();
    for (int i = 0; i < size; ++i)
    {
        if (rng.rand12() % 10 < thresh)
            crushed = rng.noise1();
        buf[start + i] = static_cast<std::int16_t> (crushed);
    }
}

inline void sawChunk (std::int16_t* buf, int start, int size, double freq, double sweepStep = 0.0)
{
    double ph = 0.0, f = freq;
    for (int i = 0; i < size; ++i)
    {
        buf[start + i] = static_cast<std::int16_t> (static_cast<int> ((ph * 2.0 - 1.0) * 511.0));
        ph += f / SR;
        if (ph >= 1.0) ph -= 1.0;
        f += sweepStep;
    }
}

inline void triChunk (std::int16_t* buf, int start, int size, double freq)
{
    double ph = 0.0, inc = freq / SR;
    for (int i = 0; i < size; ++i)
    {
        double tri = (ph < 0.5) ? ph * 2.0 : 2.0 - ph * 2.0;
        buf[start + i] = static_cast<std::int16_t> (static_cast<int> ((tri * 2.0 - 1.0) * 511.0));
        ph += inc;
        if (ph >= 1.0) ph -= 1.0;
    }
}

// Scatter `count` triangle blips of length `size`. If hasFreq, `freq` is reused
// (GW2's cached blip); otherwise a random freq in [fmin, fmax].
inline void blip (std::int16_t* buf, Rng& rng, int size, double fmin, double fmax,
                  int count = 1, bool hasFreq = false, double freq = 0.0)
{
    if (! (size > 0 && size < N))
        return;
    for (int c = 0; c < count; ++c)
    {
        int pos = rng.rand12() % (N - size);
        double f = hasFreq ? freq
                           : fmin + (rng.rand12() % 1000) / 1000.0 * (fmax - fmin);
        double ph = 0.0, inc = f / SR;
        for (int i = 0; i < size; ++i)
        {
            double tri = (ph < 0.5) ? ph * 2.0 : 2.0 - ph * 2.0;
            buf[pos + i] = static_cast<std::int16_t> (static_cast<int> ((tri * 2.0 - 1.0) * 511.0));
            ph += inc;
            if (ph >= 1.0) ph -= 1.0;
        }
    }
}

inline int chunkSize (Rng& rng, int s0, int s1, int s2)
{
    int idx = rng.rand12() % 3;
    return (idx == 0) ? s0 : (idx == 1) ? s1 : s2;
}

inline void zeroFill (std::int16_t* buf, int start, int size)
{
    for (int i = 0; i < size; ++i) buf[start + i] = 0;
}

// ---------------------------------------------------------------- chunked layouts
inline void genChunked (std::int16_t* buf, Rng& rng, int s0, int s1, int s2,
                        int noiseProb, int noiseThresh)
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        int size = chunkSize (rng, s0, s1, s2);
        if (pos + size > N) size = N - pos;
        if (rng.rand12() % 100 < noiseProb) noiseChunk (buf, rng, pos, size, noiseThresh);
        else                                zeroFill   (buf, pos, size);
        pos += size;
    }
}

inline void genNoiseOrSaw (std::int16_t* buf, Rng& rng, int s0, int s1, int s2,
                           int noiseProb, int sawProb, double sawMin, double sawMax)
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        int size = chunkSize (rng, s0, s1, s2);
        if (pos + size > N) size = N - pos;
        if (rng.rand12() % 100 < noiseProb)
        {
            if (rng.rand12() % 100 < sawProb)
            {
                double freq = sawMin + (rng.rand12() % 1000) / 1000.0 * (sawMax - sawMin);
                sawChunk (buf, pos, size, freq);
            }
            else
                noiseChunk (buf, rng, pos, size, 3);
        }
        else
            zeroFill (buf, pos, size);
        pos += size;
    }
}

// ---------------------------------------------------------------- Bank 1 generators
inline void gen1 (std::int16_t* buf, Rng& rng) { genChunked (buf, rng, 32, 8, 64, 1, 3);  blip (buf, rng, 50, 220.0, 7000.0); }
inline void gen3 (std::int16_t* buf, Rng& rng) { genChunked (buf, rng, 0, 33, 1, 4, 2); } // no blip
inline void gen4 (std::int16_t* buf, Rng& rng) { genChunked (buf, rng, 16, 4, 32, 25, 3); blip (buf, rng, 30, 500.0, 3000.0); }
inline void gen5 (std::int16_t* buf, Rng& rng) { genChunked (buf, rng, 0, 6, 48, 22, 3);  blip (buf, rng, 12, 600.0, 2500.0); }
inline void gen6 (std::int16_t* buf, Rng& rng) { genChunked (buf, rng, 20, 5, 40, 35, 3); blip (buf, rng, 15, 700.0, 3500.0); }
inline void gen7 (std::int16_t* buf, Rng& rng) { genNoiseOrSaw (buf, rng, 0, 6, 48, 22, 50, 30.0, 1000.0); blip (buf, rng, 12, 600.0, 2500.0); }

// GW2 caches its blip frequency until the algorithm is re-selected.
inline void gen2 (std::int16_t* buf, Rng& rng, bool& blipFreqSet, double& blipFreq)
{
    genChunked (buf, rng, 5, 2, 21, 3, 1);
    if (! blipFreqSet)
    {
        blipFreq = 400.0 + (rng.rand12() % 1000) / 1000.0 * (2503.0 - 400.0);
        blipFreqSet = true;
    }
    blip (buf, rng, 77, 0.0, 0.0, 1, true, blipFreq);
}

// GW17 Harmonic Drone Builder (noise / harmonic-pair / silence)
inline void harmonicChunk (std::int16_t* buf, Rng& rng, int start, int size, const double harmonics[4])
{
    int h1 = rng.rand12() % 4;
    int h2 = rng.rand12() % 4;
    while (h2 == h1) h2 = rng.rand12() % 4;
    double f1 = harmonics[h1], f2 = harmonics[h2];
    double ph1 = 0.0, ph2 = 0.0, i1 = f1 / SR, i2 = f2 / SR;
    for (int i = 0; i < size; ++i)
    {
        double t1 = (ph1 < 0.5) ? ph1 * 2.0 : 2.0 - ph1 * 2.0;
        double t2 = (ph2 < 0.5) ? ph2 * 2.0 : 2.0 - ph2 * 2.0;
        int s1 = static_cast<int> ((t1 * 2.0 - 1.0) * 255.0);
        int s2 = static_cast<int> ((t2 * 2.0 - 1.0) * 255.0);
        buf[start + i] = static_cast<std::int16_t> (s1 + s2);
        ph1 += i1; if (ph1 >= 1.0) ph1 -= 1.0;
        ph2 += i2; if (ph2 >= 1.0) ph2 -= 1.0;
    }
}

inline double gen17Full (std::int16_t* buf, Rng& rng)   // returns chosen root frequency
{
    double root = 80.0 + (rng.rand12() % 1000) / 1000.0 * (240.0 - 80.0);
    double harmonics[4] = { root, root * 2.0, root * 3.0, root * 4.0 };
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        int roll = rng.rand12() % 100;
        if (roll < 30)                              // NOISE
        {
            int size = std::min (2, N - pos);
            noiseChunk (buf, rng, pos, size, 3);
            pos += size;
        }
        else if (roll < 80)                         // HARMONIC
        {
            int size = std::min (324, N - pos);
            harmonicChunk (buf, rng, pos, size, harmonics);
            pos += size;
        }
        else                                        // SILENCE
        {
            int size = chunkSize (rng, 31, 127, 251);
            if (pos + size > N) size = N - pos;
            zeroFill (buf, pos, size);
            pos += size;
        }
    }
    return root;
}

inline void gen17Partial (std::int16_t* buf, Rng& rng, double root, double octaveMult)
{
    double harmonics[4] = { root * octaveMult, root * octaveMult * 2.0,
                            root * octaveMult * 3.0, root * octaveMult * 4.0 };
    for (int k = 0; k < 5; ++k)
    {
        int start = rng.rand12() % (N - 324);
        if (rng.rand12() % 100 < 40) noiseChunk    (buf, rng, start, 2, 3);
        else                         harmonicChunk (buf, rng, start, 324, harmonics);
    }
}

inline void gen18 (std::int16_t* buf, Rng& rng)   // BitBend Quad source
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        if (rng.rand12() % 100 < 12)
        {
            int size = std::min (57, N - pos);
            noiseChunk (buf, rng, pos, size, 1);    // super-latched (10% update)
            pos += size;
        }
        else
        {
            int size = chunkSize (rng, 32, 146, 299);
            if (pos + size > N) size = N - pos;
            zeroFill (buf, pos, size);
            pos += size;
        }
    }
    blip (buf, rng, 99, 600.0, 900.0, 3);
}

// ---------------------------------------------------------------- Bank 3 generators
inline void gen8 (std::int16_t* buf, Rng& rng)
{
    genNoiseOrSaw (buf, rng, 0, 8, 64, 55, 40, 40.0, 800.0);
    blip (buf, rng, 16, 800.0, 3000.0);
}

inline void gen12 (std::int16_t* buf, Rng& rng)   // BitBend Ping source
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        int size = chunkSize (rng, 0, 7, 31);
        if (pos + size > N) size = N - pos;
        if (rng.rand12() % 100 < 17)
        {
            if (rng.rand12() % 100 < 37)
            {
                double freq = 3000.0 + (rng.rand12() % 1000) / 1000.0 * (4000.0 - 3000.0);
                triChunk (buf, pos, size, freq);
            }
            else
                noiseChunk (buf, rng, pos, size, 3);
        }
        else
            zeroFill (buf, pos, size);
        pos += size;
    }
    blip (buf, rng, 12, 600.0, 2500.0);
}

inline void gen13 (std::int16_t* buf, Rng& rng)   // BitBend Mirror source: swept saws
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        int size = chunkSize (rng, 0, 7, 31);
        if (pos + size > N) size = N - pos;
        if (rng.rand12() % 100 < 17)
        {
            if (rng.rand12() % 100 < 79) sawChunk   (buf, pos, size, 30.0, 1.0);
            else                         noiseChunk (buf, rng, pos, size, 3);
        }
        else
            zeroFill (buf, pos, size);
        pos += size;
    }
    blip (buf, rng, 12, 600.0, 2500.0);
}

inline void gen14 (std::int16_t* buf, Rng& rng)   // BitBend Triple source
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        if (rng.rand12() % 100 < 21)
        {
            rng.rand12();                           // HW rolls a coin then uses 33
            int size = std::min (33, N - pos);
            noiseChunk (buf, rng, pos, size, 3);
            pos += size;
        }
        else
        {
            int size = chunkSize (rng, 63, 127, 251);
            if (pos + size > N) size = N - pos;
            zeroFill (buf, pos, size);
            pos += size;
        }
    }
    blip (buf, rng, 31, 100.0, 300.0, 3);
}

inline void gen15 (std::int16_t* buf, Rng& rng)   // BitBend Sweep source
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        int size = chunkSize (rng, 5, 0, 47);
        if (pos + size > N) size = N - pos;
        if (rng.rand12() % 100 < 7) noiseChunk (buf, rng, pos, size, 3);
        else                        zeroFill   (buf, pos, size);
        pos += size;
    }
    blip (buf, rng, 66, 2000.0, 4200.0);
}

inline void gen16 (std::int16_t* buf, Rng& rng)   // BitBend Triple B source
{
    int pos = 0, guard = 0;
    while (pos < N && guard < SPIN_GUARD)
    {
        ++guard;
        if (rng.rand12() % 100 < 17)
        {
            int size = std::min (47, N - pos);
            noiseChunk (buf, rng, pos, size, 3);
            pos += size;
        }
        else
        {
            int size = chunkSize (rng, 41, 111, 253);
            if (pos + size > N) size = N - pos;
            zeroFill (buf, pos, size);
            pos += size;
        }
    }
    blip (buf, rng, 67, 4000.0, 6000.0, 3);
}

} // namespace noisferatu::wavegen
