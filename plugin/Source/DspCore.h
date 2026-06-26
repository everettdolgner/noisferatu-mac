// DspCore.h — Shared DSP primitives for the NOISFERATU plugin (C++ port of dsp_core.py).
//
// Mirrors the original Arduino firmware building blocks and the verified Python port:
//   * xorshift32 PRNG driving noise1()/rand12() (one shared instance, as on hardware)
//   * 32-bit phase accumulators with automatic uint32 wrapping
//   * triangle / saw / LFO extraction from the top 10 bits of a phase accumulator
//   * the 4000-sample waveform buffer shared by Bank 1 and Bank 3
//
// Everything runs at the original 16 kHz to preserve the lo-fi character; the plugin
// upsamples the 16 kHz stream to the host sample rate (see Engine).
//
// IMPORTANT — faithfulness notes vs. the Python source:
//   * Python's `>>` on signed ints is an arithmetic (floor) shift. C++20 guarantees
//     the same for signed types, so `a >> b` matches directly.
//   * Python's `%` floors toward negative infinity; C++ truncates toward zero. Where an
//     operand of `%` can be negative (the Bank 3 bit-position walks), use pymod().
//   * Python int(float) truncates toward zero, as does C++ (int)float. Matches directly.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace noisferatu {

inline constexpr int      kSampleRate   = 16000;
inline constexpr int      kDacCenter    = 511;
inline constexpr int      kWaveformSize = 4000;            // shared buffer length

// freq -> 32-bit phase increment: inc = freq * (2^32 / SAMPLE_RATE)
// For 16 kHz this is the firmware's magic constant 268435.456.
inline constexpr double   kPhasePerHz   = 4294967296.0 / kSampleRate;

inline constexpr std::uint32_t kU32 = 0xFFFFFFFFu;

inline double lerp (double lo, double hi, double t) noexcept { return lo + (hi - lo) * t; }

// Python-style floor modulo (result has the sign of the divisor). Used wherever an
// operand of `%` in the Python source can be negative.
inline int pymod(int a, int b) noexcept
{
    int r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}

// ======================================================================
// PRNG — xorshift32, identical to the firmware's noise1()/rand12()
// ======================================================================
class Rng
{
public:
    explicit Rng (std::uint32_t seed = 0x12345678u) : state (seed) {}

    inline std::uint32_t step() noexcept
    {
        std::uint32_t s = state;
        s ^= (s << 13);
        s ^= (s >> 17);
        s ^= (s << 5);
        state = s;
        return state;
    }

    inline int           noise1() noexcept { return static_cast<int> (step() & 0x3FFu) - 512; } // -512..511
    inline int           rand12() noexcept { return static_cast<int> (step() & 0x0FFFu); }      // 0..4095
    inline std::uint32_t rand16() noexcept { return step() & 0xFFFFu; }                          // 0..65535
    inline double        randf()  noexcept { return rand16() * (1.0 / 65536.0); }                // 0..1

    std::uint32_t state;
};

// ======================================================================
// Scalar phase helpers (top 10 bits of a uint32 phase)
// ======================================================================
inline int tri10 (std::uint32_t phase) noexcept   // triangle -512..+511
{
    int p = static_cast<int> ((phase >> 22) & 0x3FFu);
    return (p < 512) ? (p << 1) - 512 : 1535 - (p << 1);
}

inline int lfo10 (std::uint32_t phase) noexcept   // unipolar triangle LFO 0..1023
{
    int p = static_cast<int> ((phase >> 22) & 0x3FFu);
    return (p < 512) ? (p << 1) : 2047 - (p << 1);
}

inline int saw10 (std::uint32_t phase) noexcept   // signed saw -512..+511
{
    return static_cast<int> ((phase >> 22) & 0x3FFu) - 512;
}

constexpr std::uint32_t freqToInc (double freqHz) noexcept
{
    // Python: int(freq * PHASE_PER_HZ) & 0xFFFFFFFF.  freq is non-negative here, so the
    // truncation toward zero matches floor; mask to 32 bits.
    return static_cast<std::uint32_t> (static_cast<std::int64_t> (freqHz * kPhasePerHz));
}

// ======================================================================
// Shared waveform buffer (Bank 1 + Bank 3)
// ======================================================================
struct WaveBuffer
{
    std::int16_t buf[kWaveformSize] = {};
    int          playbackPhase = 0;   // shared integer cursor (0..kWaveformSize-1)
};

// Shared context handed to every algorithm (mirrors engine._Ctx).
struct Ctx
{
    Rng        rng;
    int        sampleRate = kSampleRate;
    WaveBuffer wave;
};

// ======================================================================
// Algorithm base class (mirrors dsp_core.Algo)
// ======================================================================
class Algo
{
public:
    explicit Algo (Ctx& c) : ctx (c), rng (c.rng), sr (c.sampleRate) {}
    virtual ~Algo() = default;

    virtual const char* name() const = 0;
    virtual void setParams (double /*p1*/, double /*p2*/) {}
    virtual void onSelect() {}                 // called when this algo becomes active
    virtual void render (std::int32_t* out, int n) = 0;  // ~ -512..1023

protected:
    Ctx& ctx;
    Rng& rng;
    int  sr;
};

} // namespace noisferatu
