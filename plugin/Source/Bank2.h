// Bank2.h — Bank 2: Noisy Textures (9 algorithms).
// C++ port of bank2_noisy.py. Sample-and-hold noise, dust, FM/inharmonic chaos,
// gated noise, vinyl crackle, etc. The Vinyl Crackle algorithm replays the actual
// 32000-sample crackle recording extracted from the firmware (VinylCrackleData.h).
#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "DspCore.h"
#include "VinylCrackleData.h"

namespace noisferatu {

inline constexpr double kTwoPi = 6.283185307179586;

// ---------------------------------------------------------------- Latched Noise
class LatchedNoise : public Algo
{
public:
    explicit LatchedNoise (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Latched Noise"; }
    void setParams (double p1, double p2) override
    {
        double freq = lerp (9.0, 8000.0, p2 * p2);
        period = std::max (1, static_cast<int> (sr / freq));
        double p = lerp (0.05, 1.0, p1 * p1);
        threshold = static_cast<int> (p * 4095.0);
    }
    void render (std::int32_t* out, int n) override
    {
        int cnt = counter, per = period, val = value, thr = threshold;
        for (int i = 0; i < n; ++i)
        {
            if (++cnt >= per) { cnt = 0; if (rng.rand12() < thr) val = rng.noise1(); }
            out[i] = val;
        }
        counter = cnt; value = val;
    }
private:
    int counter = 0, period = kSampleRate, value = 0, threshold = 4095;
};

// ---------------------------------------------------------------- Dust
class DustNoise : public Algo
{
public:
    explicit DustNoise (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Dust"; }
    void setParams (double p1, double p2) override
    {
        double cutoff = lerp (90.0, 8000.0, p1);
        alpha = std::min (std::max (1.0 - std::exp (-kTwoPi * cutoff / sr), 0.0), 1.0);
        prob  = lerp (1.0, 2000.0, p2) / sr;
    }
    void render (std::int32_t* out, int n) override
    {
        double a = alpha, pr = prob, st = state;
        for (int i = 0; i < n; ++i)
        {
            int raw = (rng.randf() < pr) ? rng.noise1() : 0;
            st += a * (raw - st);
            out[i] = static_cast<int> (st);
        }
        state = st;
    }
private:
    double alpha = 1.0, prob = 0.0, state = 0.0;
};

// ---------------------------------------------------------------- FM Noise
class FMNoise : public Algo
{
public:
    explicit FMNoise (Ctx& c) : Algo (c) {}
    const char* name() const override { return "FM Noise"; }
    void setParams (double p1, double p2) override
    {
        base = lerp (2.0, 2000.0, p1);
        inc1 = freqToInc (base);
        inc2 = freqToInc (base * RATIOS[idx]);
        double clock = lerp (0.5, 20.0, p2);
        cinc1 = freqToInc (clock);
        cinc2 = freqToInc (clock * CLOCK_RATIO);
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph1 = p1, ph2 = p2, c1 = cc1, c2 = cc2, last = lastC1;
        std::uint32_t i1 = inc1, i2 = inc2;
        for (int i = 0; i < n; ++i)
        {
            ph1 = (ph1 + i1) & kU32;
            ph2 = (ph2 + i2) & kU32;
            c1  = (c1 + cinc1) & kU32;
            c2  = (c2 + cinc2) & kU32;
            std::uint32_t s1 = c1 & 0x80000000u;
            std::uint32_t s2 = c2 & 0x80000000u;
            if ((s1 && ! last) && s2)
            {
                idx = rng.rand12() % 8;
                i2 = freqToInc (base * RATIOS[idx]);
            }
            last = s1;
            out[i] = tri10 (ph1) ^ tri10 (ph2);
        }
        p1 = ph1; p2 = ph2; cc1 = c1; cc2 = c2; inc2 = i2; lastC1 = last;
    }
private:
    static constexpr double RATIOS[8] = {1.41, 1.618, 1.73, 2.11, 2.37, 2.81, 3.14, 4.0};
    static constexpr double CLOCK_RATIO = 2.37;
    std::uint32_t p1 = 0, p2 = 0, cc1 = 0, cc2 = 0;
    std::uint32_t inc1 = 0, inc2 = 0, cinc1 = 0, cinc2 = 0, lastC1 = 0;
    int    idx = 0;
    double base = 2.0;
};

// ---------------------------------------------------------------- Noise Gates
class NoiseGates : public Algo
{
public:
    explicit NoiseGates (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Noise Gates"; }
    void setParams (double p1, double p2) override
    {
        double freq = lerp (0.01, 5.0, p1);
        inc1 = freqToInc (freq);
        base2 = inc1;
        step = static_cast<int> (lerp (1000.0, 300000.0, p2));
        wperiod = static_cast<int> (sr / WALK_RATE);
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph1 = p1, ph2 = p2;
        long long walk_ = walk;
        int wc = wcounter, wp = wperiod, st = step;
        long long maxWalk = static_cast<long long> (st) * 500;
        for (int i = 0; i < n; ++i)
        {
            if (++wc >= wp)
            {
                wc = 0;
                walk_ += (rng.rand12() & 1) ? st : -st;
                if (walk_ < -maxWalk) walk_ = -maxWalk;
                if (walk_ >  maxWalk) walk_ =  maxWalk;
            }
            ph1 = (ph1 + inc1) & kU32;
            int tri1 = tri10 (ph1);
            long long inc2 = static_cast<long long> (base2) + walk_;
            if (inc2 < 0) inc2 = 0;
            ph2 = (ph2 + static_cast<std::uint32_t> (inc2)) & kU32;
            int tri2 = tri10 (ph2);
            int nz = rng.noise1();
            int g1 = (tri1 > 486) ? nz : 0;
            int g2 = (tri2 > 486) ? nz : 0;
            out[i] = (g1 + g2) >> 1;
        }
        p1 = ph1; p2 = ph2; walk = walk_; wcounter = wc;
    }
private:
    static constexpr double WALK_RATE = 8.0;
    std::uint32_t p1 = 0, p2 = 0, inc1 = 0, base2 = 0;
    long long     walk = 0;
    int           step = 0, wcounter = 0, wperiod = kSampleRate / 8;
};

// ---------------------------------------------------------------- Saw Clicks
class SawClicks : public Algo
{
public:
    explicit SawClicks (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Saw Clicks"; }
    void setParams (double pp1, double pp2) override
    {
        inc1 = freqToInc (lerp (0.001, 20.0, pp1));
        inc2 = freqToInc (lerp (0.001, 20.0, pp2));
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph1 = p1, ph2 = p2;
        for (int i = 0; i < n; ++i)
        {
            ph1 = (ph1 + inc1) & kU32;
            ph2 = (ph2 + inc2) & kU32;
            out[i] = (saw10 (ph1) + saw10 (ph2)) >> 1;
        }
        p1 = ph1; p2 = ph2;
    }
private:
    std::uint32_t p1 = 0, p2 = 0, inc1 = 65536, inc2 = 65536;
};

// ---------------------------------------------------------------- Noise NOR Noise
class NoiseNorNoise : public Algo
{
public:
    explicit NoiseNorNoise (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Noise NOR Noise"; }
    void setParams (double pp1, double pp2) override
    {
        double f1 = lerp (5.0, 120.0, pp1), f2 = lerp (5.0, 120.0, pp2);
        period1 = std::max (1, static_cast<int> (sr / f1));
        period2 = std::max (1, static_cast<int> (sr / f2));
    }
    void render (std::int32_t* out, int n) override
    {
        int c1 = cc1, c2 = cc2, h1 = held1, h2 = held2, pp1 = period1, pp2 = period2;
        for (int i = 0; i < n; ++i)
        {
            if (++c1 >= pp1) { c1 = 0; h1 = rng.noise1(); }
            if (++c2 >= pp2) { c2 = 0; h2 = rng.noise1(); }
            out[i] = (~(h1 | h2)) & 0x3FF;
        }
        cc1 = c1; cc2 = c2; held1 = h1; held2 = h2;
    }
private:
    int cc1 = 0, cc2 = 0, period1 = 1, period2 = 1, held1 = 0, held2 = 0;
};

// ---------------------------------------------------------------- Dust Burst
class DustBurst : public Algo
{
public:
    explicit DustBurst (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Dust Burst"; }
    void setParams (double p1, double p2) override
    {
        walkSpeed = lerp (0.0001, 0.02, p1 * p1);
        step = lerp (0.05, 0.8, p2);
        double cutoff = 2000.0;
        alpha = std::min (std::max (1.0 - std::exp (-kTwoPi * cutoff / sr), 0.0), 1.0);
    }
    void render (std::int32_t* out, int n) override
    {
        double pr = prob, st_ = step, a = alpha, state_ = state;
        int walkThr = static_cast<int> (walkSpeed * 4095.0);
        for (int i = 0; i < n; ++i)
        {
            if (rng.rand12() < walkThr)
            {
                pr += (rng.rand12() & 1) ? st_ : -st_;
                if (pr < 0.0) pr = 0.0;
                if (pr > 1.0) pr = 1.0;
            }
            int raw = (rng.rand12() < static_cast<int> (pr * 4095.0)) ? rng.noise1() : 0;
            state_ += a * (raw - state_);
            out[i] = static_cast<int> (state_);
        }
        prob = pr; state = state_;
    }
private:
    double prob = 0.0, walkSpeed = 0.001, step = 0.1, alpha = 1.0, state = 0.0;
};

// ---------------------------------------------------------------- Highpass Noise
class HighpassNoise : public Algo
{
public:
    explicit HighpassNoise (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Highpass Noise"; }
    void setParams (double p1, double p2) override
    {
        depth = 1.0 - p1;
        baseInc = freqToInc (lerp (0.3, 20.0, p2));
        if (amInc == 0) amInc = baseInc;
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t phase = amPhase, inc = amInc, base = baseInc;
        double d = depth;
        for (int i = 0; i < n; ++i)
        {
            int nz = rng.noise1() & MASK;
            std::uint32_t prev = phase;
            phase = (phase + inc) & kU32;
            if (phase < prev)                                  // LFO wrapped
                inc = static_cast<std::uint32_t> (static_cast<std::int64_t> (base * RATE_TABLE[rng.rand12() % 16]));
            int lfo = lfo10 (phase);
            int blended = static_cast<int> (lfo * d + 1023.0 * (1.0 - d));
            out[i] = (nz * blended) >> 10;
        }
        amPhase = phase; amInc = inc;
    }
private:
    static constexpr int    MASK = 0x380;
    static constexpr double RATE_TABLE[16] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
                                              1.33, 0.5, 0.75, 0.89, 0.6, 0.9, 0.43, 0.71};
    std::uint32_t amPhase = 0, amInc = 0, baseInc = 0;
    double        depth = 1.0;
};

// ---------------------------------------------------------------- Vinyl Crackle
class VinylCrackle : public Algo
{
public:
    explicit VinylCrackle (Ctx& c) : Algo (c) {}
    const char* name() const override { return "Vinyl Crackle"; }
    void setParams (double p1, double p2) override
    {
        speed  = lerp (0.04, 2.0, p1);
        window = 200 + static_cast<int> ((4000 - 200) * p2);
    }
    void render (std::int32_t* out, int n) override
    {
        const int len = static_cast<int> (kVinylCrackleLen);
        double sp = speed, frac_ = frac;
        int win = window, pos_ = pos, rem = remaining;
        bool silent = inSilence;
        int span = std::max (1, len - win);
        for (int i = 0; i < n; ++i)
        {
            if (rem == 0)
            {
                if (silent)
                {
                    silent = false;
                    pos_ = rng.rand12() % span;
                    rem = win;
                }
                else
                {
                    if (rng.rand12() % 4 > 0)                  // 75% silence
                    {
                        silent = true;
                        rem = SILENCE[rng.rand12() % 3];
                    }
                    else
                    {
                        pos_ = rng.rand12() % span;
                        rem = win;
                    }
                }
            }
            --rem;
            if (silent) { out[i] = 0; continue; }
            out[i] = static_cast<int> (kVinylCrackle[pos_ % len]) >> 6;
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; pos_ += inc;
        }
        frac = frac_; pos = pos_; remaining = rem; inSilence = silent;
    }
private:
    static constexpr int SILENCE[3] = {57, 113, 331};
    double speed = 0.5, frac = 0.0;
    int    window = 1000, pos = 0, remaining = 0;
    bool   inSilence = false;
};

inline std::vector<std::unique_ptr<Algo>> buildBank2 (Ctx& c)
{
    std::vector<std::unique_ptr<Algo>> v;
    v.emplace_back (std::make_unique<LatchedNoise>  (c));
    v.emplace_back (std::make_unique<DustNoise>     (c));
    v.emplace_back (std::make_unique<FMNoise>       (c));
    v.emplace_back (std::make_unique<NoiseGates>    (c));
    v.emplace_back (std::make_unique<SawClicks>     (c));
    v.emplace_back (std::make_unique<NoiseNorNoise> (c));
    v.emplace_back (std::make_unique<DustBurst>     (c));
    v.emplace_back (std::make_unique<HighpassNoise> (c));
    v.emplace_back (std::make_unique<VinylCrackle>  (c));
    return v;
}

} // namespace noisferatu
