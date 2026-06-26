// Engine.h — NOISFERATU audio engine (C++ port of engine.py).
//
// Owns the shared DSP context, the 45 algorithm instances (5 banks x 9), bank/algo
// routing, and the global effects chain that follows every algorithm — exactly as in
// the firmware's TC5_Handler ISR:
//
//     algorithm  ->  sample-rate reduction (decimate + hold)
//                ->  bitcrush (10-bit down to 1-bit)
//                ->  symmetric +/-1 dither
//                ->  master volume (quadratic curve) + clamp to +/-512
//
// The core generates block-by-block at the original 16 kHz; nextHostSample() linearly
// upsamples that 16 kHz stream to the host sample rate. The upsampling preserves (and
// slightly softens) the lo-fi character while avoiding the harshest imaging artifacts.
//
// This file is intentionally free of any JUCE dependency so the DSP can be compiled and
// golden-tested against the Python reference in isolation.
#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include "DspCore.h"
#include "Bank1.h"
#include "Bank2.h"
#include "Bank3.h"
#include "Bank4.h"
#include "Bank5.h"

namespace noisferatu {

inline constexpr int kEngineBlock = 512;     // 16 kHz render block (matches engine.py)

inline const char* bankName (int b)
{
    static const char* names[5] = { "Wavetables", "Noisy Textures", "BitBend",
                                    "Blips & Tones", "Logic Disorder" };
    return (b >= 0 && b < 5) ? names[b] : "?";
}

class Engine
{
public:
    Engine()
    {
        banks_.push_back (buildBank1 (ctx_));
        banks_.push_back (buildBank2 (ctx_));
        banks_.push_back (buildBank3 (ctx_));
        banks_.push_back (buildBank4 (ctx_));
        banks_.push_back (buildBank5 (ctx_));
        current()->onSelect();                 // generate initial buffer (Bank 1, Algo 0)
    }

    // ----------------------------------------------------------------- configuration
    void prepare (double hostSampleRate)
    {
        hostSR_ = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
        ratio_  = static_cast<double> (kSampleRate) / hostSR_;   // 16k samples per host sample
        upPos_  = 0.0;
        s0_ = s1_ = 0.0f;
        fifoPos_ = fifoFill_ = 0;
        s1_ = pull16k();                       // prime the interpolator
    }

    // Control values (0..1), pushed from the processor each block.
    void setControls (double pot1, double pot2, double bitcrush, double rate, double volume)
    {
        pot1_ = pot1; pot2_ = pot2; bitcrushPot_ = bitcrush; ratePot_ = rate; volumePot_ = volume;
    }

    // Request a bank/algorithm switch (applied at the next 16 kHz block boundary).
    void setBankAlgo (int bank, int algo)
    {
        bank = std::min (std::max (bank, 0), 4);
        algo = std::min (std::max (algo, 0), 8);
        if (bank != bank_ || algo != algo_)
            pending_ = (bank << 8) | algo;     // encode; -1 means none
    }

    // ----------------------------------------------------------------- audio
    inline float nextHostSample()
    {
        float out = static_cast<float> (s0_ + (s1_ - s0_) * upPos_);   // linear interpolate
        upPos_ += ratio_;
        while (upPos_ >= 1.0)
        {
            upPos_ -= 1.0;
            s0_ = s1_;
            s1_ = pull16k();
        }
        return out;
    }

    // ----------------------------------------------------------------- display / scope
    int  bank() const { return bank_; }
    int  algo() const { return algo_; }
    const char* bankNameStr() const { return bankName (bank_); }
    const char* algoNameStr() const { return current()->name(); }

    // e.g. "1.03" (1-indexed bank.algo), matching the hardware's 7-seg.
    std::array<char, 8> displayText() const
    {
        std::array<char, 8> s {};
        int a = algo_ + 1;
        s[0] = static_cast<char> ('0' + (bank_ + 1));
        s[1] = '.';
        s[2] = static_cast<char> ('0' + (a / 10));
        s[3] = static_cast<char> ('0' + (a % 10));
        s[4] = '\0';
        return s;
    }

    // Copy the most recent 16 kHz post-effects block for the editor's scope (best-effort).
    void copyScope (float* dst, int n) const
    {
        for (int i = 0; i < n; ++i) dst[i] = (i < kEngineBlock) ? scope_[i] : 0.0f;
    }

    // Test hook: render `n` raw 16 kHz post-effects samples (no upsampling), processed in
    // kEngineBlock-sized blocks exactly like the Python engine.py callback. Used by the
    // golden test that diffs this port against the verified Python reference.
    void debugRender16k (float* dst, int n)
    {
        for (int i = 0; i < n; ++i) dst[i] = pull16k();
    }

private:
    Algo* current() const { return banks_[bank_][algo_].get(); }

    void applyPending()
    {
        if (pending_ < 0) return;
        bank_ = (pending_ >> 8) & 0xFF;
        algo_ = pending_ & 0xFF;
        pending_ = -1;
        current()->onSelect();
    }

    // ----- effects-chain parameter maps (engine.py) -----
    int decimation() const
    {
        double n = ratePot_;
        if (n > 0.9) return 1;
        return 40 - static_cast<int> ((n / 0.9) * 39.0);
    }
    int bitcrushMask() const
    {
        int bc = 1 + static_cast<int> (bitcrushPot_ * 9.0);   // 1..10 bits
        bc = std::min (std::max (bc, 1), 10);
        return ~((1 << (10 - bc)) - 1);
    }
    int masterVolume() const
    {
        int volTemp = static_cast<int> (volumePot_ * 4095.0) >> 4;   // 0..255
        return (volTemp * volTemp) >> 8;                            // quadratic curve
    }

    // Render one 16 kHz block (raw algorithm -> effects chain -> float) into the FIFO.
    void refill()
    {
        applyPending();
        Algo* a = current();
        a->setParams (pot1_, pot2_);
        a->render (raw_.data(), kEngineBlock);

        int decim = decimation();
        int mask  = bitcrushMask();
        int master = masterVolume();
        int counter = decimCounter_, held = held_;
        Rng& rng = ctx_.rng;

        for (int i = 0; i < kEngineBlock; ++i)
        {
            if (++counter >= decim) { counter = 0; held = raw_[i]; }
            int crushed  = held & mask;
            int dithered = crushed + ((rng.rand12() & 1) - (rng.rand12() & 1));
            int v = (dithered * master) >> 8;
            if (v > 511)       v = 511;
            else if (v < -512) v = -512;
            float f = v / 512.0f;
            fifo_[i]  = f;
            scope_[i] = f;
        }
        decimCounter_ = counter; held_ = held;
        fifoFill_ = kEngineBlock;
        fifoPos_  = 0;
    }

    inline float pull16k()
    {
        if (fifoPos_ >= fifoFill_) refill();
        return fifo_[fifoPos_++];
    }

    Ctx ctx_;
    std::vector<std::vector<std::unique_ptr<Algo>>> banks_;

    int    bank_ = 0, algo_ = 0;
    int    pending_ = -1;

    double pot1_ = 0.5, pot2_ = 0.5, bitcrushPot_ = 1.0, ratePot_ = 1.0, volumePot_ = 0.8;

    // effects-chain persistent state
    int decimCounter_ = 0, held_ = 0;

    // 16 kHz FIFO
    std::array<std::int32_t, kEngineBlock> raw_  {};
    std::array<float,        kEngineBlock> fifo_ {};
    std::array<float,        kEngineBlock> scope_ {};
    int fifoPos_ = 0, fifoFill_ = 0;

    // upsampler state
    double hostSR_ = 48000.0, ratio_ = static_cast<double> (kSampleRate) / 48000.0, upPos_ = 0.0;
    float  s0_ = 0.0f, s1_ = 0.0f;
};

} // namespace noisferatu
