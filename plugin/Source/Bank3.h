// Bank3.h — Bank 3: BitBend Wavetables (9 algorithms).
// C++ port of bank3_bitbend.py. SoundScaper-style address manipulation: a bit-clock
// periodically random-walks one or more "bit position" pointers, then each output
// sample is read from the shared buffer at an address whose bits have been XOR'd /
// forced-0 / forced-1 / frozen (HOLD). The buffer is shared with Bank 1.
//
// The Python bit-position walks use Python's floor `%` on operands that can go
// negative; pymod() reproduces that exactly (see DspCore.h).
#pragma once

#include <memory>
#include <vector>
#include "DspCore.h"
#include "Wavegen.h"
#include "Bank1.h"     // WaveAlgo

namespace noisferatu {

inline int coin (Rng& rng) { return (rng.rand12() & 1) ? 1 : -1; }
inline int modN (int a)    { return pymod (a, BANK_N); }

// ====================================================== GW8: Chaos (generative)
class GW8 : public WaveAlgo
{
public:
    explicit GW8 (Ctx& c) : WaveAlgo (c) {}
    const char* name() const override { return "BitBend Chaos"; }
    void setParams (double p1, double p2) override { speed = lerp (0.1, 4.0, p1); cinc = freqToInc (lerp (0.5, 20.0, p2)); }

    void render (std::int32_t* out, int n) override
    {
        std::int16_t* b = buf();
        std::uint32_t cph_ = cph, last = lastClock;
        int pos_ = pos, mode_ = mode, held_ = held, phase = wave.playbackPhase;
        double sp = speed, frac_ = frac;
        for (int i = 0; i < n; ++i)
        {
            cph_ = (cph_ + cinc) & kU32;
            std::uint32_t state = cph_ & 0x80000000u;
            if (state && ! last)
            {
                if (rng.rand12() & 1) mode_ = rng.rand12() % 4;
                pos_ = pymod (pos_ + coin (rng), 12);
                if (mode_ == 3) held_ = phase & ((1 << pos_) - 1);
            }
            last = state;
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= BANK_N) phase = 0;
            int addr = phase, mask = (1 << pos_) - 1;
            if      (mode_ == 0) addr &= ~mask;
            else if (mode_ == 1) addr |= mask;
            else if (mode_ == 2) addr ^= mask;
            else                 addr = (addr & ~mask) | (held_ & mask);
            out[i] = b[modN (addr)];
        }
        cph = cph_; lastClock = last; pos = pos_; mode = mode_; held = held_;
        frac = frac_; wave.playbackPhase = phase;
    }

protected:
    void generate() override { wavegen::gen8 (buf(), rng); }
private:
    double        speed = 1.0;
    std::uint32_t cph = 0, cinc = 0, lastClock = 0;
    int           pos = 5, mode = 0, held = 0;
};

// ====================================================== GW9: Sparse (mode = pot2)
class GW9 : public WaveAlgo
{
public:
    explicit GW9 (Ctx& c) : WaveAlgo (c) { clock = freqToInc (8.0); }
    const char* name() const override { return "BitBend Sparse"; }
    void setParams (double p1, double p2) override { speed = lerp (0.1, 4.0, p1); mode = std::min (3, static_cast<int> (p2 * 3.999)); }

    void render (std::int32_t* out, int n) override
    {
        std::int16_t* b = buf();
        std::uint32_t cph_ = cph, last = lastClock;
        int pos_ = pos, held_ = held, phase = wave.playbackPhase;
        double sp = speed, frac_ = frac;
        for (int i = 0; i < n; ++i)
        {
            cph_ = (cph_ + clock) & kU32;
            std::uint32_t state = cph_ & 0x80000000u;
            if (state && ! last)
            {
                pos_ = pymod (pos_ + coin (rng), 12);
                if (mode == 3) held_ = phase & ((1 << pos_) - 1);
            }
            last = state;
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= BANK_N) phase = 0;
            int addr = phase, mask = (1 << pos_) - 1;
            if      (mode == 0) addr &= ~mask;
            else if (mode == 1) addr |= mask;
            else if (mode == 2) addr ^= mask;
            else                addr = (addr & ~mask) | (held_ & mask);
            out[i] = b[modN (addr)];
        }
        cph = cph_; lastClock = last; pos = pos_; held = held_;
        frac = frac_; wave.playbackPhase = phase;
    }

protected:
    void generate() override { wavegen::gen1 (buf(), rng); }   // Sparse Glitchy layout
private:
    double        speed = 1.0;
    std::uint32_t cph = 0, clock = 0, lastClock = 0;
    int           pos = 5, mode = 0, held = 0;
};

// ====================================================== Dual/triple combos
class BitBendCombo : public WaveAlgo
{
public:
    explicit BitBendCombo (Ctx& c) : WaveAlgo (c) {}
    void setParams (double p1, double p2) override { speed = lerp (0.1, 4.0, p1); cinc = freqToInc (lerp (0.5, 20.0, p2)); }

    void render (std::int32_t* out, int n) override
    {
        std::int16_t* b = buf();
        std::uint32_t cph_ = cph, last = lastClock;
        int phase = wave.playbackPhase;
        double sp = speed, frac_ = frac;
        for (int i = 0; i < n; ++i)
        {
            cph_ = (cph_ + cinc) & kU32;
            std::uint32_t state = cph_ & 0x80000000u;
            if (state && ! last)
            {
                if ((! bernoulli) || (rng.rand12() & 1)) walk (phase);
            }
            last = state;
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= BANK_N) phase = 0;
            out[i] = b[modN (manip (phase))];
        }
        cph = cph_; lastClock = last; frac = frac_; wave.playbackPhase = phase;
    }

protected:
    virtual void walk (int /*phase*/) {}
    virtual int  manip (int phase) { return phase; }

    bool          bernoulli = true;
    double        speed = 1.0;
    std::uint32_t cph = 0, cinc = 0, lastClock = 0;
};

class GW10 : public BitBendCombo   // BitBend Dual
{
public:
    explicit GW10 (Ctx& c) : BitBendCombo (c) {}
    const char* name() const override { return "BitBend Dual"; }
protected:
    void generate() override { wavegen::gen3 (buf(), rng); }   // Spacey Pulses
    void walk (int phase) override
    {
        posXor  = pymod (posXor + coin (rng), 4);
        posHold = 4 + pymod (posHold - 4 + coin (rng), 5);
        int hm = ((1 << (posHold + 1)) - 1) & ~((1 << 4) - 1);
        held = phase & hm;
    }
    int manip (int phase) override
    {
        int addr = phase ^ ((1 << (posXor + 1)) - 1);
        int hm = ((1 << (posHold + 1)) - 1) & ~((1 << 4) - 1);
        return (addr & ~hm) | (held & hm);
    }
private:
    int posXor = 3, posHold = 8, held = 0;
};

class GW11 : public BitBendCombo   // BitBend Freeze (fixed-rate clock, no gate)
{
public:
    explicit GW11 (Ctx& c) : BitBendCombo (c) { bernoulli = false; }
    const char* name() const override { return "BitBend Freeze"; }
protected:
    void generate() override { wavegen::gen5 (buf(), rng); }   // Wandering Window layout
    void walk (int phase) override
    {
        posLow  = pymod (posLow + coin (rng), 3);
        posHigh = 3 + pymod (posHigh - 3 + coin (rng), 4);
        int hm = ((1 << (posHigh + 1)) - 1) & ~((1 << 3) - 1);
        held = phase & hm;
    }
    int manip (int phase) override
    {
        int addr = phase & ~((1 << (posLow + 1)) - 1);
        int hm = ((1 << (posHigh + 1)) - 1) & ~((1 << 3) - 1);
        return (addr & ~hm) | (held & hm);
    }
private:
    int posLow = 2, posHigh = 6, held = 0;
};

class GW12 : public BitBendCombo   // BitBend Ping
{
public:
    explicit GW12 (Ctx& c) : BitBendCombo (c) {}
    const char* name() const override { return "BitBend Ping"; }
protected:
    void generate() override { wavegen::gen12 (buf(), rng); }
    void walk (int /*phase*/) override
    {
        posLow  = pymod (posLow + coin (rng), 3);
        posHigh = 3 + pymod (posHigh - 3 + coin (rng), 5);
    }
    int manip (int phase) override
    {
        int addr = phase ^ ((1 << (posLow + 1)) - 1);
        return addr | (((1 << (posHigh + 1)) - 1) & ~((1 << 3) - 1));
    }
private:
    int posLow = 2, posHigh = 7;
};

class GW13 : public BitBendCombo   // BitBend Mirror
{
public:
    explicit GW13 (Ctx& c) : BitBendCombo (c) {}
    const char* name() const override { return "BitBend Mirror"; }
protected:
    void generate() override { wavegen::gen13 (buf(), rng); }
    void walk (int /*phase*/) override
    {
        posLow  = pymod (posLow + coin (rng), 3);
        posHigh = 3 + pymod (posHigh - 3 + coin (rng), 5);
    }
    int manip (int phase) override
    {
        int addr = phase & ~((1 << (posLow + 1)) - 1);
        return addr | (((1 << (posHigh + 1)) - 1) & ~((1 << 3) - 1));
    }
private:
    int posLow = 2, posHigh = 7;
};

class GW14 : public BitBendCombo   // BitBend Triple
{
public:
    explicit GW14 (Ctx& c) : BitBendCombo (c) {}
    const char* name() const override { return "BitBend Triple"; }
protected:
    void generate() override { wavegen::gen14 (buf(), rng); }
    void walk (int phase) override
    {
        p1 = pymod (p1 + coin (rng), 2);
        p2 = 2 + pymod (p2 - 2 + coin (rng), 3);
        p3 = 5 + pymod (p3 - 5 + coin (rng), 5);
        int hm = ((1 << (p2 + 1)) - 1) & ~((1 << 2) - 1);
        held = phase & hm;
    }
    int manip (int phase) override
    {
        int addr = phase ^ ((1 << (p1 + 1)) - 1);
        int hm = ((1 << (p2 + 1)) - 1) & ~((1 << 2) - 1);
        addr = (addr & ~hm) | (held & hm);
        return addr & ~(((1 << (p3 + 1)) - 1) & ~((1 << 5) - 1));
    }
private:
    int p1 = 1, p2 = 4, p3 = 9, held = 0;
};

class GW16 : public BitBendCombo   // BitBend Triple B
{
public:
    explicit GW16 (Ctx& c) : BitBendCombo (c) {}
    const char* name() const override { return "BitBend Triple B"; }
protected:
    void generate() override { wavegen::gen16 (buf(), rng); }
    void walk (int phase) override
    {
        p1 = pymod (p1 + coin (rng), 2);
        p2 = 2 + pymod (p2 - 2 + coin (rng), 3);
        p3 = 5 + pymod (p3 - 5 + coin (rng), 5);
        int hm = ((1 << (p2 + 1)) - 1) & ~((1 << 2) - 1);
        held = phase & hm;
    }
    int manip (int phase) override
    {
        int addr = phase & ~((1 << (p1 + 1)) - 1);
        int hm = ((1 << (p2 + 1)) - 1) & ~((1 << 2) - 1);
        addr = (addr & ~hm) | (held & hm);
        return addr | (((1 << (p3 + 1)) - 1) & ~((1 << 5) - 1));
    }
private:
    int p1 = 1, p2 = 4, p3 = 9, held = 0;
};

// ====================================================== GW15: Sweep (two clocks)
class GW15 : public WaveAlgo
{
public:
    explicit GW15 (Ctx& c) : WaveAlgo (c) {}
    const char* name() const override { return "BitBend Sweep"; }
    void setParams (double p1, double p2) override { speed = lerp (0.1, 4.0, p1); cinc = freqToInc (lerp (0.5, 20.0, p2)); }

    void render (std::int32_t* out, int n) override
    {
        std::int16_t* b = buf();
        std::uint32_t c1_ = c1, c2_ = c2, l1 = last1, l2 = last2;
        int pl = posLow, ph_ = posHigh, hl = heldLow, hh = heldHigh, phase = wave.playbackPhase;
        std::uint32_t slow = cinc / 3;
        double sp = speed, frac_ = frac;
        for (int i = 0; i < n; ++i)
        {
            c1_ = (c1_ + slow) & kU32;
            std::uint32_t s1 = c1_ & 0x80000000u;
            if (s1 && ! l1)
            {
                pl = pymod (pl + coin (rng), 3);
                hl = phase & ((1 << (pl + 1)) - 1);
            }
            l1 = s1;
            c2_ = (c2_ + cinc) & kU32;
            std::uint32_t s2 = c2_ & 0x80000000u;
            if (s2 && ! l2)
            {
                ph_ = 3 + pymod (ph_ - 3 + coin (rng), 4);
                int hmh = ((1 << (ph_ + 1)) - 1) & ~((1 << 3) - 1);
                hh = phase & hmh;
            }
            l2 = s2;
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= BANK_N) phase = 0;
            int addr = phase;
            int hml = (1 << (pl + 1)) - 1;
            addr = (addr & ~hml) | (hl & hml);
            int hmh = ((1 << (ph_ + 1)) - 1) & ~((1 << 3) - 1);
            addr = (addr & ~hmh) | (hh & hmh);
            out[i] = b[modN (addr)];
        }
        c1 = c1_; c2 = c2_; last1 = l1; last2 = l2;
        posLow = pl; posHigh = ph_; heldLow = hl; heldHigh = hh;
        frac = frac_; wave.playbackPhase = phase;
    }

protected:
    void generate() override { wavegen::gen15 (buf(), rng); }
private:
    double        speed = 1.0;
    std::uint32_t cinc = 0, c1 = 0, c2 = 0, last1 = 0, last2 = 0;
    int           posLow = 2, posHigh = 6, heldLow = 0, heldHigh = 0;
};

inline std::vector<std::unique_ptr<Algo>> buildBank3 (Ctx& c)
{
    std::vector<std::unique_ptr<Algo>> v;
    v.emplace_back (std::make_unique<GW8>  (c));
    v.emplace_back (std::make_unique<GW9>  (c));
    v.emplace_back (std::make_unique<GW10> (c));
    v.emplace_back (std::make_unique<GW11> (c));
    v.emplace_back (std::make_unique<GW12> (c));
    v.emplace_back (std::make_unique<GW13> (c));
    v.emplace_back (std::make_unique<GW14> (c));
    v.emplace_back (std::make_unique<GW15> (c));
    v.emplace_back (std::make_unique<GW16> (c));
    return v;
}

} // namespace noisferatu
