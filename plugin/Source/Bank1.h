// Bank1.h — Bank 1: Wavetables / Generative Waveforms (9 algorithms).
// C++ port of bank1_wavetables.py. Each algorithm scrubs the shared 4000-sample
// waveform buffer (ctx.wave), regenerated on select and on a periodic regen timer.
// Playback uses a fractional phase so pot1 acts as a playback-speed / pitch control.
// The buffer cursor (wave.playbackPhase) is shared with Bank 3, as on the hardware.
#pragma once

#include <memory>
#include <vector>
#include "DspCore.h"
#include "Wavegen.h"

namespace noisferatu {

inline constexpr int   BANK_N = kWaveformSize;     // 4000
inline constexpr int   S_RATE = kSampleRate;       // samples per second for regen intervals

// Base: owns the shared buffer/cursor and an optional periodic regen timer.
class WaveAlgo : public Algo
{
public:
    explicit WaveAlgo (Ctx& c) : Algo (c), wave (c.wave) {}

    void onSelect() override { generate(); regenCounter = 0; }

protected:
    virtual void generate() {}

    std::int16_t* buf() { return wave.buf; }

    void maybeRegen (int n)
    {
        if (regenInterval <= 0) return;            // <=0 means "no periodic regen"
        regenCounter += n;
        if (regenCounter >= regenInterval)
        {
            regenCounter = 0;
            generate();
        }
    }

    WaveBuffer& wave;
    double      frac = 0.0;
    int         regenCounter = 0;
    int         regenInterval = 0;
};

// ====================================================== GW1/2/3: chunked silence
class ChunkedSilence : public WaveAlgo
{
public:
    explicit ChunkedSilence (Ctx& c) : WaveAlgo (c) {}

    void render (std::int32_t* out, int n) override
    {
        maybeRegen (n);
        std::int16_t* b = buf();
        int rem = remaining, phase = wave.playbackPhase;
        double sp = speed, frac_ = frac, prob = silenceProb;
        for (int i = 0; i < n; ++i)
        {
            if (rem == 0)
            {
                int size = sizeAt (rng.rand12() % 3);
                if ((rng.rand12() % 1000) / 1000.0 < prob) rem = size;
            }
            if (rem > 0)
            {
                --rem;
                frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
                while (phase >= BANK_N) phase -= BANK_N;
                out[i] = 0;
                continue;
            }
            out[i] = b[phase];
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            while (phase >= BANK_N) phase -= BANK_N;
        }
        remaining = rem; frac = frac_; wave.playbackPhase = phase;
    }

protected:
    double speedFromP1 (double p1) const { return lerp (0.01, 2.0, p1); }
    virtual int sizeAt (int idx) const = 0;

    double speed = 1.0, silenceProb = 0.0;
    int    remaining = 0;
};

class GW1 : public ChunkedSilence
{
public:
    explicit GW1 (Ctx& c) : ChunkedSilence (c) { regenInterval = 2 * S_RATE; }
    const char* name() const override { return "Sparse Glitch"; }
    void setParams (double p1, double p2) override { speed = speedFromP1 (p1); silenceProb = 0.2 - p2 * 0.2; }
protected:
    int  sizeAt (int idx) const override { static const int s[3] = {32, 8, 64}; return s[idx]; }
    void generate() override { wavegen::gen1 (buf(), rng); }
};

class GW2 : public ChunkedSilence
{
public:
    explicit GW2 (Ctx& c) : ChunkedSilence (c) { regenInterval = 5 * S_RATE; }
    const char* name() const override { return "Dense Microglitch"; }
    void setParams (double p1, double p2) override { speed = speedFromP1 (p1); silenceProb = 1.0 - p2; }
    void onSelect() override { blipFreqSet = false; WaveAlgo::onSelect(); }
protected:
    int  sizeAt (int idx) const override { static const int s[3] = {5, 2, 21}; return s[idx]; }
    void generate() override { wavegen::gen2 (buf(), rng, blipFreqSet, blipFreq); }
private:
    bool   blipFreqSet = false;
    double blipFreq = 2100.0;
};

class GW3 : public ChunkedSilence
{
public:
    explicit GW3 (Ctx& c) : ChunkedSilence (c) { regenInterval = 2 * S_RATE; }
    const char* name() const override { return "Spacey Pulses"; }
    void setParams (double p1, double p2) override { speed = speedFromP1 (p1); silenceProb = 0.1 - p2 * 0.1; }
protected:
    int  sizeAt (int idx) const override { static const int s[3] = {64, 128, 256}; return s[idx]; }
    void generate() override { wavegen::gen3 (buf(), rng); }
};

// ====================================================== GW4: random jump glitch
class GW4 : public WaveAlgo
{
public:
    explicit GW4 (Ctx& c) : WaveAlgo (c) { regenInterval = 0; }
    const char* name() const override { return "Random Jump"; }

    void setParams (double p1, double p2) override
    {
        speed = lerp (0.01, 2.0, p1);
        silenceProb = 1.0 - p2;
        jumpPeriod = S_RATE / 3;
    }

    void render (std::int32_t* out, int n) override
    {
        std::int16_t* b = buf();
        int rem = remaining, jc = jumpCounter, jp = jumpPeriod, phase = wave.playbackPhase;
        double sp = speed, frac_ = frac, prob = silenceProb;
        for (int i = 0; i < n; ++i)
        {
            if (++jc >= jp) { jc = 0; phase = rng.rand12() % BANK_N; }
            if (rem == 0)
            {
                static const int s[3] = {16, 4, 32};
                int size = s[rng.rand12() % 3];
                rem = ((rng.rand12() % 1000) / 1000.0 < prob) ? size : 0;
            }
            if (rem > 0) { --rem; out[i] = 0; continue; }   // silence: no phase advance
            out[i] = b[phase];
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            while (phase >= BANK_N) phase -= BANK_N;
        }
        remaining = rem; frac = frac_; jumpCounter = jc; wave.playbackPhase = phase;
    }

protected:
    void generate() override { wavegen::gen4 (buf(), rng); }
private:
    double speed = 1.0, silenceProb = 0.0;
    int    remaining = 0, jumpCounter = 0, jumpPeriod = S_RATE / 3;
};

// ====================================================== GW5/GW7: wandering window
class WanderWindow : public WaveAlgo
{
public:
    explicit WanderWindow (Ctx& c) : WaveAlgo (c) {}

    void render (std::int32_t* out, int n) override
    {
        maybeRegen (n);
        std::int16_t* b = buf();
        int wstart = windowStart, wc = walkCounter, wp = walkPeriod, phase = wave.playbackPhase;
        double sp = speed, frac_ = frac;
        for (int i = 0; i < n; ++i)
        {
            if (++wc >= wp)
            {
                wc = 0;
                // RNG order matters: Python evaluates the sign condition before the
                // magnitude in `(1+rand%20) if (rand&1) else -(1+rand%20)`.
                int sign = (rng.rand12() & 1) ? 1 : -1;
                int mag  = 1 + rng.rand12() % 20;
                int step = sign * mag;
                int ns = wstart + step;
                if (ns < 0) ns += BANK_N;
                if (ns >= BANK_N) ns -= BANK_N;
                wstart = ns;
            }
            int wend = wstart + WINDOW;
            if (wend > BANK_N) wend = BANK_N;
            if (phase < wstart || phase >= wend) phase = wstart;
            out[i] = b[phase];
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= wend) phase = wstart;
        }
        windowStart = wstart; walkCounter = wc; frac = frac_; wave.playbackPhase = phase;
    }

protected:
    static constexpr int WINDOW = kWaveformSize / 50;   // 80
    double speed = 1.0;
    int    windowStart = 0, walkCounter = 0, walkPeriod = kSampleRate / 5;
};

class GW5 : public WanderWindow
{
public:
    explicit GW5 (Ctx& c) : WanderWindow (c) { regenInterval = 2 * S_RATE; }
    const char* name() const override { return "Wander Window"; }
    void setParams (double p1, double p2) override
    {
        speed = lerp (0.01, 2.0, p1);
        double walk = lerp (0.01, 50.0, p2);
        walkPeriod = std::max (1, static_cast<int> (S_RATE / walk));
    }
protected:
    void generate() override { wavegen::gen5 (buf(), rng); }
};

class GW7 : public WanderWindow
{
public:
    explicit GW7 (Ctx& c) : WanderWindow (c) { regenInterval = 0; }   // on select only
    const char* name() const override { return "Noise/Saw Win"; }
    void setParams (double p1, double p2) override
    {
        speed = lerp (0.1, 4.0, p1);
        double walk = lerp (0.5, 20.0, p2);
        walkPeriod = std::max (1, static_cast<int> (S_RATE / walk));
    }
protected:
    void generate() override { wavegen::gen7 (buf(), rng); }
};

// ====================================================== GW6: manual window + spray
class GW6 : public WaveAlgo
{
public:
    explicit GW6 (Ctx& c) : WaveAlgo (c) { regenInterval = 4 * S_RATE; }
    const char* name() const override { return "Manual Window"; }

    void setParams (double p1, double p2) override
    {
        windowStart = static_cast<int> (p1 * (BANK_N - 1));
        windowSize  = 1 + static_cast<int> (p2 * (BANK_N - 1));
        if (windowStart + windowSize > BANK_N) windowSize = BANK_N - windowStart;
    }

    void render (std::int32_t* out, int n) override
    {
        maybeRegen (n);
        std::int16_t* b = buf();
        int wstart = windowStart, wsize = windowSize, sc = sprayCounter, soff = sprayOffset;
        int phase = wave.playbackPhase;
        double frac_ = frac;
        int sprayPeriod = (wsize >= 100) ? wsize : 100;
        for (int i = 0; i < n; ++i)
        {
            if (++sc >= sprayPeriod)
            {
                sc = 0;
                soff = (rng.rand12() % (SPRAY * 2 + 1)) - SPRAY;
            }
            int actual = wstart + soff;
            while (actual < 0) actual += BANK_N;
            while (actual >= BANK_N) actual -= BANK_N;
            int wend = actual + wsize;
            if (wend > BANK_N) wend = BANK_N;
            if (phase < actual || phase >= wend) phase = actual;
            out[i] = b[phase];
            frac_ += 0.5;                              // fixed slow playback
            int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= wend) phase = actual;
        }
        sprayCounter = sc; sprayOffset = soff; frac = frac_; wave.playbackPhase = phase;
    }

protected:
    void generate() override { wavegen::gen6 (buf(), rng); }
private:
    static constexpr int SPRAY = 30;
    int windowStart = 0, windowSize = BANK_N, sprayCounter = 0, sprayOffset = 0;
};

// ====================================================== GW17: harmonic drone builder
class GW17 : public WaveAlgo
{
public:
    explicit GW17 (Ctx& c) : WaveAlgo (c) {}
    const char* name() const override { return "Harmonic Drone"; }

    void setParams (double p1, double p2) override
    {
        if      (p1 < 0.2) octave = 0.5;
        else if (p1 < 0.4) octave = 1.0;
        else if (p1 < 0.6) octave = 2.0;
        else if (p1 < 0.8) octave = 4.0;
        else               octave = 8.0;
        stutterProb = STUTTER_MAX * (1.0 - p2);
    }

    void render (std::int32_t* out, int n) override
    {
        std::int16_t* b = buf();
        int rem = stutterRemaining, ic = internalCounter, phase = wave.playbackPhase;
        double frac_ = frac;
        for (int i = 0; i < n; ++i)
        {
            if (++ic >= REGEN) { ic = 0; wavegen::gen17Partial (b, rng, root, octave); }
            if (rem == 0)
            {
                static const int s[3] = {666, 1000, 1533};
                int size = s[rng.rand12() % 3];
                if ((rng.rand12() % 1000) / 1000.0 < stutterProb) rem = size;
            }
            if (rem > 0) { --rem; out[i] = b[phase]; continue; }   // freeze (stutter)
            frac_ += SPEED; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= BANK_N) phase = 0;
            out[i] = b[phase];
        }
        stutterRemaining = rem; internalCounter = ic; frac = frac_; wave.playbackPhase = phase;
    }

protected:
    void generate() override { root = wavegen::gen17Full (buf(), rng); }
private:
    static constexpr double SPEED = 0.3;
    static constexpr int    REGEN = 3 * kSampleRate;
    static constexpr double STUTTER_MAX = 0.005;
    double root = 150.0, octave = 1.0, stutterProb = 0.0;
    int    stutterRemaining = 0, internalCounter = 0;
};

// ====================================================== GW18: BitBend Quad
class GW18 : public WaveAlgo
{
public:
    explicit GW18 (Ctx& c) : WaveAlgo (c) { regenInterval = 0; }   // on select only
    const char* name() const override { return "BitBend Quad"; }

    void setParams (double p1, double p2) override
    {
        speed   = lerp (0.1, 4.0, p1);
        clockInc = freqToInc (lerp (0.5, 20.0, p2));
    }

    void render (std::int32_t* out, int n) override
    {
        std::int16_t* b = buf();
        std::uint32_t cph = clockPhase, cinc = clockInc, last = lastClock;
        int bp1_ = bp1, bp2_ = bp2, bp3_ = bp3, bp4_ = bp4, held_ = held;
        int phase = wave.playbackPhase;
        double sp = speed, frac_ = frac;
        for (int i = 0; i < n; ++i)
        {
            cph = (cph + cinc) & kU32;
            std::uint32_t state = cph & 0x80000000u;
            if (state && ! last)
            {
                if (rng.rand12() & 1)
                {
                    bp1_ = pymod (bp1_ + ((rng.rand12() & 1) ? 1 : -1), 2);
                    bp2_ = 2 + pymod (bp2_ - 2 + ((rng.rand12() & 1) ? 1 : -1), 2);
                    bp3_ = 4 + pymod (bp3_ - 4 + ((rng.rand12() & 1) ? 1 : -1), 3);
                    bp4_ = 7 + pymod (bp4_ - 7 + ((rng.rand12() & 1) ? 1 : -1), 3);
                    int holdMask = ((1 << (bp2_ + 1)) - 1) & ~((1 << 2) - 1);
                    held_ = phase & holdMask;
                }
            }
            last = state;
            frac_ += sp; int inc = static_cast<int> (frac_); frac_ -= inc; phase += inc;
            if (phase >= BANK_N) phase = 0;
            int addr = phase;
            addr ^= (1 << (bp1_ + 1)) - 1;                              // XOR bits 0-1
            int holdMask = ((1 << (bp2_ + 1)) - 1) & ~((1 << 2) - 1);   // HOLD bits 2-3
            addr = (addr & ~holdMask) | (held_ & holdMask);
            addr &= ~(((1 << (bp3_ + 1)) - 1) & ~((1 << 4) - 1));       // SET_0 bits 4-6
            addr |= ((1 << (bp4_ + 1)) - 1) & ~((1 << 7) - 1);          // SET_1 bits 7-9
            addr = pymod (addr, BANK_N);
            out[i] = b[addr];
        }
        clockPhase = cph; lastClock = last;
        bp1 = bp1_; bp2 = bp2_; bp3 = bp3_; bp4 = bp4_; held = held_;
        frac = frac_; wave.playbackPhase = phase;
    }

protected:
    void generate() override { wavegen::gen18 (buf(), rng); }
private:
    double        speed = 1.0;
    std::uint32_t clockPhase = 0, clockInc = 0, lastClock = 0;
    int           bp1 = 0, bp2 = 2, bp3 = 5, bp4 = 8, held = 0;
};

inline std::vector<std::unique_ptr<Algo>> buildBank1 (Ctx& c)
{
    std::vector<std::unique_ptr<Algo>> v;
    v.emplace_back (std::make_unique<GW1>  (c));
    v.emplace_back (std::make_unique<GW2>  (c));
    v.emplace_back (std::make_unique<GW3>  (c));
    v.emplace_back (std::make_unique<GW4>  (c));
    v.emplace_back (std::make_unique<GW5>  (c));
    v.emplace_back (std::make_unique<GW6>  (c));
    v.emplace_back (std::make_unique<GW7>  (c));
    v.emplace_back (std::make_unique<GW17> (c));
    v.emplace_back (std::make_unique<GW18> (c));
    return v;
}

} // namespace noisferatu
