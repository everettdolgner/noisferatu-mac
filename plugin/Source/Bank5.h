// Bank5.h — Bank 5: Logic Disorder (9 algorithms).
// C++ port of bank5_logic.py. Bitwise/logic combinations of two (or three)
// oscillators. Faithful to algos.h: several "square" oscillators are actually saws
// taken from (phase>>22)-512, and the NOR/NAND/XNOR results are masked with & 0x3FF
// (so they read 0..1023, not signed) — the offset/clipping is part of the character.
#pragma once

#include <memory>
#include <vector>
#include "DspCore.h"

namespace noisferatu {

// ThreeCascadedSquares uses true top-bit squares (not saws).
class ThreeCascadedSquares : public Algo
{
public:
    explicit ThreeCascadedSquares (Ctx& c) : Algo (c) {}
    const char* name() const override { return "3-Cascade"; }
    void setParams (double pp1, double pp2) override
    {
        inc1 = freqToInc (9.96);                            // osc1 fixed
        inc2 = freqToInc (lerp (0.6, 1024.0, pp1));
        inc3 = freqToInc (lerp (1.0, 1024.0, pp2));
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph1 = p1, ph2 = p2, ph3 = p3;
        for (int i = 0; i < n; ++i)
        {
            ph1 = (ph1 + inc1) & kU32;
            ph2 = (ph2 + inc2) & kU32;
            ph3 = (ph3 + inc3) & kU32;
            int sq1 = (ph1 & 0x80000000u) ? 511 : -512;
            int sq2 = (ph2 & 0x80000000u) ? 511 : -512;
            int sq3 = (ph3 & 0x80000000u) ? 511 : -512;
            int sq1am = (sq1 + 512) >> 1;
            int sq2mod = (sq2 * sq1am) >> 9;
            int sq2modam = (sq2mod + 512) >> 1;
            out[i] = (sq3 * sq2modam) >> 9;
        }
        p1 = ph1; p2 = ph2; p3 = ph3;
    }
private:
    std::uint32_t p1 = 0, p2 = 0, p3 = 0;
    std::uint32_t inc1 = freqToInc (9.96), inc2 = 0, inc3 = 0;
};

// Shared scaffold: two phase accumulators with pot-mapped frequencies.
class TwoOsc : public Algo
{
public:
    TwoOsc (Ctx& c, double f1lo, double f1hi, double f2lo, double f2hi)
        : Algo (c), F1lo (f1lo), F1hi (f1hi), F2lo (f2lo), F2hi (f2hi) {}

    void setParams (double pp1, double pp2) override
    {
        inc1 = freqToInc (lerp (F1lo, F1hi, pp1));
        inc2 = freqToInc (lerp (F2lo, F2hi, pp2));
    }
    void render (std::int32_t* out, int n) override
    {
        std::uint32_t ph1 = p1, ph2 = p2;
        for (int i = 0; i < n; ++i)
        {
            ph1 = (ph1 + inc1) & kU32;
            ph2 = (ph2 + inc2) & kU32;
            out[i] = combine (ph1, ph2);
        }
        p1 = ph1; p2 = ph2;
    }

protected:
    virtual int combine (std::uint32_t ph1, std::uint32_t ph2) = 0;

    double        F1lo, F1hi, F2lo, F2hi;
    std::uint32_t p1 = 0, p2 = 0, inc1 = 0, inc2 = 0;
};

class NorSquare : public TwoOsc
{
public:
    explicit NorSquare (Ctx& c) : TwoOsc (c, 0.8, 200.0, 0.73, 215.0) {}
    const char* name() const override { return "NOR Square"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return (~(saw10 (a) | saw10 (b))) & 0x3FF; }
};

class TriOrSaw : public TwoOsc
{
public:
    explicit TriOrSaw (Ctx& c) : TwoOsc (c, 4.5, 1024.0, 2.0, 1024.0) {}
    const char* name() const override { return "Tri OR Saw"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return tri10 (a) | saw10 (b); }
};

class TriNorTri : public TwoOsc
{
public:
    explicit TriNorTri (Ctx& c) : TwoOsc (c, 4.0, 880.0, 15.0, 900.0) {}
    const char* name() const override { return "Tri NOR Tri"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return (~(tri10 (a) | tri10 (b))) & 0x3FF; }
};

class TriXorTri : public TwoOsc
{
public:
    explicit TriXorTri (Ctx& c) : TwoOsc (c, 0.7, 220.0, 0.6, 440.0) {}
    const char* name() const override { return "Tri XOR Tri"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return tri10 (a) ^ tri10 (b); }
};

class SquareXnorSquare : public TwoOsc
{
public:
    explicit SquareXnorSquare (Ctx& c) : TwoOsc (c, 0.5, 440.0, 0.6, 150.0) {}
    const char* name() const override { return "Sq XNOR Sq"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return (~(saw10 (a) ^ saw10 (b))) & 0x3FF; }
};

class SquareNandSquare : public TwoOsc
{
public:
    explicit SquareNandSquare (Ctx& c) : TwoOsc (c, 0.1, 50.0, 0.08, 45.0) {}
    const char* name() const override { return "Sq NAND Sq"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return (~(saw10 (a) & saw10 (b))) & 0x3FF; }
};

class TwoSaws : public TwoOsc
{
public:
    explicit TwoSaws (Ctx& c) : TwoOsc (c, 15.0, 850.0, 15.0, 850.0) {}
    const char* name() const override { return "Two Saws"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return (saw10 (a) + saw10 (b)) >> 1; }
};

class SquareOrSquare : public TwoOsc
{
public:
    explicit SquareOrSquare (Ctx& c) : TwoOsc (c, 15.0, 850.0, 15.0, 850.0) {}
    const char* name() const override { return "Sq OR Sq"; }
protected:
    int combine (std::uint32_t a, std::uint32_t b) override { return saw10 (a) | saw10 (b); }
};

inline std::vector<std::unique_ptr<Algo>> buildBank5 (Ctx& c)
{
    std::vector<std::unique_ptr<Algo>> v;
    v.emplace_back (std::make_unique<ThreeCascadedSquares> (c));
    v.emplace_back (std::make_unique<NorSquare>            (c));
    v.emplace_back (std::make_unique<TriOrSaw>             (c));
    v.emplace_back (std::make_unique<TriNorTri>            (c));
    v.emplace_back (std::make_unique<TriXorTri>            (c));
    v.emplace_back (std::make_unique<SquareXnorSquare>     (c));
    v.emplace_back (std::make_unique<SquareNandSquare>     (c));
    v.emplace_back (std::make_unique<TwoSaws>              (c));
    v.emplace_back (std::make_unique<SquareOrSquare>       (c));
    return v;
}

} // namespace noisferatu
