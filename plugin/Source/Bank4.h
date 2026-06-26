// Bank4.h — Bank 4: Blips & Tones (9 algorithms).
// C++ port of bank4_blips.py. Triangle-based tones, ring mod, and Bernoulli-gated
// note generators. The Python vectorises the purely-periodic algos; here everything
// is a per-sample loop (cheap at 16 kHz) producing identical results.
#pragma once

#include <algorithm>
#include <memory>
#include <vector>
#include "DspCore.h"

namespace noisferatu {

// Firmware's exp-free decay approximation, clamped to (0.9, 0.9999).
inline double decayCoeff (double decayTime, int sr)
{
    double c = 1.0 - (3.0 / (decayTime * sr));
    return std::min (std::max (c, 0.9), 0.9999);
}

// ---------------------------------------------------------------- Random Triangle
class RandomTriangle : public Algo
{
public:
    explicit RandomTriangle (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Random Tri"; }
    void setParams (double p1, double p2) override
    {
        double trig = lerp (0.05, 0.5, 1.0 - p1);          // pot1 reversed
        period = std::max (1, static_cast<int> (trig * sr));
        decay = decayCoeff (lerp (0.005, 0.6, p2), sr);
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph = phase, in = inc;
        double env_ = env, dec = decay;
        int cnt = counter, per = period;
        for (int i = 0; i < n; ++i)
        {
            if (++cnt >= per)
            {
                cnt = 0;
                if (! (rng.rand12() & 1))
                {
                    env_ = 1.0;
                    double freq = 250.0 + (rng.rand12() % 1000) / 1000.0 * (2780.0 - 250.0);
                    in = freqToInc (freq);
                }
            }
            env_ *= dec;
            ph = (ph + in) & kU32;
            out[i] = static_cast<int> (tri10 (ph) * env_);
        }
        phase = ph; inc = in; env = env_; counter = cnt;
    }
private:
    std::uint32_t phase = 0, inc = 0;
    double        env = 0.0, decay = 0.9995;
    int           counter = 0, period = kSampleRate / 2;
};

// ---------------------------------------------------------------- Harmonic Tris
class HarmonicTris : public Algo
{
public:
    explicit HarmonicTris (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Harmonic Tri"; }
    void setParams (double p1, double p2) override
    {
        double base = lerp (50.0, 400.0, p1);
        inc[0] = freqToInc (base); inc[1] = freqToInc (base * 3.0); inc[2] = freqToInc (base * 4.0);
        double e = p2 * p2 * p2;                            // cubic curve
        double lfo = lerp (0.001, 10.0, e);
        linc[0] = freqToInc (lfo); linc[1] = freqToInc (lfo * 1.618); linc[2] = freqToInc (lfo * 1.732);
    }
    void render (std::int32_t* out, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            int s = 0;
            for (int k = 0; k < 3; ++k)
            {
                ph[k]  = (ph[k]  + inc[k])  & kU32;
                lph[k] = (lph[k] + linc[k]) & kU32;
                s += (tri10 (ph[k]) * lfo10 (lph[k])) >> 10;
            }
            out[i] = static_cast<int> (s / 3.0);
        }
    }
private:
    std::uint32_t ph[3] = {0, 0, 0}, lph[3] = {0, 0, 0}, inc[3] = {0, 0, 0}, linc[3] = {0, 0, 0};
};

// ---------------------------------------------------------------- Fast Triangle
class FastTriangle : public Algo
{
public:
    explicit FastTriangle (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Fast Tri"; }
    void setParams (double p1, double p2) override
    {
        inc = freqToInc (lerp (11.0, 6000.0, p1));
        decay = decayCoeff (lerp (0.0074, 0.7, p2), sr);
        period = sr / 16;
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph = phase, in = inc;
        double env_ = env, dec = decay;
        int crush_ = crush, cnt = counter, per = period;
        for (int i = 0; i < n; ++i)
        {
            if (++cnt >= per)
            {
                cnt = 0;
                if (! ((rng.rand12() & 3) > 0))             // 75% skip
                {
                    env_ = 1.0;
                    crush_ = 3 + (rng.rand12() % 10);
                }
            }
            env_ *= dec;
            ph = (ph + in) & kU32;
            int enveloped = static_cast<int> (tri10 (ph) * env_);
            int bits = 10 - crush_;                         // crush 3..12; >10 bits => clean
            int mask = (bits > 0) ? ~((1 << bits) - 1) : -1;
            out[i] = enveloped & mask;
        }
        phase = ph; inc = in; env = env_; crush = crush_; counter = cnt;
    }
private:
    std::uint32_t phase = 0, inc = 0;
    double        env = 0.0, decay = 0.9995;
    int           crush = 10, counter = 0, period = kSampleRate / 16;
};

// ---------------------------------------------------------------- Phrygian Tri
class PhrygianTri : public Algo
{
public:
    explicit PhrygianTri (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Phrygian Tri"; }
    void setParams (double p1, double p2) override
    {
        double burstFreq = lerp (0.5, 8.0, p1);             // pots swapped on this algo
        burstInc = freqToInc (burstFreq);
        root = lerp (50.0, 200.0, p2);
        trigPeriod = std::max (1, sr / std::max (1, static_cast<int> (burstFreq * 2.0)));
        inc = freqToInc (root * RATIOS[scalePos]);
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph = phase, in = inc, burst_ = burst, binc = burstInc;
        double env_ = env;
        int envc = envCounter, tc = trigCounter, basePeriod = trigPeriod, pos = scalePos;
        double rt = root;
        for (int i = 0; i < n; ++i)
        {
            burst_ = (burst_ + binc) & kU32;
            int curPeriod = basePeriod + (static_cast<int> (burst_ >> 22) << 4);
            if (++tc >= curPeriod)
            {
                tc = 0;
                pos += (rng.rand12() & 1) ? 1 : -1;
                if (pos < 0) pos = 7;
                if (pos > 7) pos = 0;
                in = freqToInc (rt * RATIOS[pos]);
                env_ = 1.0;
                envc = 0;
            }
            if (envc < 800) { ++envc; env_ *= 0.9963; }
            else            { env_ = 0.0; }
            ph = (ph + in) & kU32;
            out[i] = static_cast<int> (tri10 (ph) * env_);
        }
        phase = ph; inc = in; env = env_; envCounter = envc;
        trigCounter = tc; burst = burst_; scalePos = pos;
    }
private:
    static constexpr double RATIOS[8] = {1.0, 1.0595, 1.1892, 1.3348, 1.4983, 1.5874, 1.7818, 2.0};
    std::uint32_t phase = 0, inc = 0, burst = 0, burstInc = 0;
    double        env = 0.0, root = 100.0;
    int           envCounter = 0, trigCounter = 0, trigPeriod = kSampleRate / 4, scalePos = 0;
};

// ---------------------------------------------------------------- Ring Mod
class RingMod : public Algo
{
public:
    explicit RingMod (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Ring Mod"; }
    void setParams (double pp1, double pp2) override
    {
        inc1 = freqToInc (lerp (15.0, 2000.0, pp1));
        inc2 = freqToInc (lerp (15.0, 2000.0, pp2));
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph1 = p1, ph2 = p2;
        for (int i = 0; i < n; ++i)
        {
            ph1 = (ph1 + inc1) & kU32;
            ph2 = (ph2 + inc2) & kU32;
            out[i] = (tri10 (ph1) * tri10 (ph2)) >> 9;
        }
        p1 = ph1; p2 = ph2;
    }
private:
    std::uint32_t p1 = 0, p2 = 0, inc1 = 0, inc2 = 0;
};

// ---------------------------------------------------------------- Noise OR Square
class NoiseOrSquare : public Algo
{
public:
    explicit NoiseOrSquare (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Noise|Square"; }
    void setParams (double p1, double p2) override
    {
        double shFreq = lerp (15.0, 116.0, p1);
        shPeriod = std::max (1, static_cast<int> (sr / shFreq));
        inc = freqToInc (lerp (35.0, 5000.0, p2));
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph = phase, in = inc;
        int cnt = shCounter, per = shPeriod, held_ = held;
        for (int i = 0; i < n; ++i)
        {
            if (++cnt >= per) { cnt = 0; held_ = rng.noise1(); }
            ph = (ph + in) & kU32;
            out[i] = held_ | saw10 (ph);
        }
        phase = ph; shCounter = cnt; held = held_;
    }
private:
    std::uint32_t phase = 0, inc = 0;
    int           shCounter = 0, shPeriod = 1, held = 0;
};

// ---------------------------------------------------------------- Major Tris
class MajorTris : public Algo
{
public:
    explicit MajorTris (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Major Tris"; }
    void setParams (double p1, double p2) override
    {
        linc1 = freqToInc (lerp (0.08, 2.0, p1));
        linc2 = freqToInc (lerp (0.08, 2.0, p2));
    }
    void render (std::int32_t* out, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            p1 = (p1 + F1) & kU32;   p2 = (p2 + F2) & kU32;   p3 = (p3 + F3) & kU32;
            l1 = (l1 + linc1) & kU32; l2 = (l2 + linc2) & kU32;
            l3 = (l3 + LFO3) & kU32;  l3slow = (l3slow + LFO3_SLOW) & kU32;
            int lfo1 = lfo10 (l1), lfo2 = lfo10 (l2);
            int lfo3 = (lfo10 (l3) * lfo10 (l3slow)) >> 10;     // nested AM
            int mod1 = (tri10 (p1) * lfo1) >> 10;
            int mod2 = (tri10 (p2) * lfo2) >> 10;
            int mod3 = (tri10 (p3) * lfo3) >> 10;
            out[i] = static_cast<int> ((mod1 + mod2 + mod3) / 3.0);
        }
    }
private:
    static constexpr std::uint32_t F1 = freqToInc (220.0);
    static constexpr std::uint32_t F2 = freqToInc (220.0 * 1.25992);
    static constexpr std::uint32_t F3 = freqToInc (220.0 * 1.5);
    static constexpr std::uint32_t LFO3 = freqToInc (0.2);
    static constexpr std::uint32_t LFO3_SLOW = freqToInc (0.035);
    std::uint32_t p1 = 0, p2 = 0, p3 = 0, l1 = 0, l2 = 0, l3 = 0, l3slow = 0;
    std::uint32_t linc1 = 0, linc2 = 0;
};

// ---------------------------------------------------------------- Bernoulli Minor7
class BernoulliTris : public Algo
{
public:
    explicit BernoulliTris (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Bernoulli 7"; }
    void setParams (double p1, double p2) override { prob1 = p1; prob2 = p2; }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t clk_ = clk, last = lastState, ph1 = p1, ph2 = p2, i1 = inc1, i2 = inc2;
        double e1_ = e1, e2_ = e2;
        for (int i = 0; i < n; ++i)
        {
            clk_ = (clk_ + CLOCK) & kU32;
            std::uint32_t state = clk_ & 0x80000000u;
            if (state && ! last)
            {
                if (rng.rand12() & 1)
                {
                    double r1 = (rng.rand12() % 1000) / 1000.0;
                    i1 = freqToInc (r1 < prob1 ? 220.0 : 330.0);
                    e1_ = 1.0;
                    double r2 = (rng.rand12() % 1000) / 1000.0;
                    i2 = freqToInc (r2 < prob2 ? 264.0 : 396.0);
                    e2_ = 1.0;
                }
            }
            last = state;
            e1_ *= DECAY; e2_ *= DECAY;
            ph1 = (ph1 + i1) & kU32;
            ph2 = (ph2 + i2) & kU32;
            out[i] = (static_cast<int> (tri10 (ph1) * e1_) + static_cast<int> (tri10 (ph2) * e2_)) >> 1;
        }
        clk = clk_; lastState = last; p1 = ph1; p2 = ph2; inc1 = i1; inc2 = i2; e1 = e1_; e2 = e2_;
    }
private:
    static constexpr std::uint32_t CLOCK = freqToInc (5.0);
    static constexpr double DECAY = 0.9985;
    std::uint32_t clk = 0, lastState = 0, p1 = 0, p2 = 0, inc1 = 0, inc2 = 0;
    double        e1 = 0.0, e2 = 0.0, prob1 = 0.5, prob2 = 0.5;
};

// ---------------------------------------------------------------- Pentatonic Blips
class PentatonicBlips : public Algo
{
public:
    explicit PentatonicBlips (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Penta Blips"; }
    void setParams (double p1, double p2) override
    {
        clkInc = freqToInc (lerp (3.0, 11.5, p1));
        decay = decayCoeff (lerp (0.001, 0.5, p2), sr);
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t clk_ = clk, last = lastState, cinc = clkInc, ph = phase, in = inc;
        double env_ = env, dec = decay;
        for (int i = 0; i < n; ++i)
        {
            clk_ = (clk_ + cinc) & kU32;
            std::uint32_t state = clk_ & 0x80000000u;
            if (state && ! last)
            {
                if (rng.rand12() & 1) { in = NOTES[rng.rand12() % 5]; env_ = 1.0; }
            }
            last = state;
            env_ *= dec;
            ph = (ph + in) & kU32;
            out[i] = static_cast<int> (tri10 (ph) * env_);
        }
        clk = clk_; lastState = last; phase = ph; inc = in; env = env_;
    }
private:
    static constexpr std::uint32_t NOTES[5] = {
        freqToInc (220.0), freqToInc (220.0 * 1.125), freqToInc (220.0 * 1.25),
        freqToInc (220.0 * 1.5), freqToInc (220.0 * 1.6875) };
    std::uint32_t clk = 0, lastState = 0, clkInc = 0, phase = 0, inc = 0;
    double        env = 0.0, decay = 0.999;
};

inline std::vector<std::unique_ptr<Algo>> buildBank4 (Ctx& c)
{
    std::vector<std::unique_ptr<Algo>> v;
    v.emplace_back (std::make_unique<RandomTriangle>  (c));
    v.emplace_back (std::make_unique<HarmonicTris>    (c));
    v.emplace_back (std::make_unique<FastTriangle>    (c));
    v.emplace_back (std::make_unique<PhrygianTri>     (c));
    v.emplace_back (std::make_unique<RingMod>         (c));
    v.emplace_back (std::make_unique<NoiseOrSquare>   (c));
    v.emplace_back (std::make_unique<MajorTris>       (c));
    v.emplace_back (std::make_unique<BernoulliTris>   (c));
    v.emplace_back (std::make_unique<PentatonicBlips> (c));
    return v;
}

} // namespace noisferatu
