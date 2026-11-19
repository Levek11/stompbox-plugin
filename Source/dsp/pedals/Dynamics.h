#pragma once

#include <atomic>
#include "Pedal.h"

// The compressors: Pacific 76 (Cali76), Silver 76 (UAFX 1176) and
// Fresh Squeeze (Pulp 'N' Peel V4).

namespace stompbox::dsp
{

//==============================================================================
/** 1176-style FET compression: the input level drives the signal into a fixed
    threshold, a very fast peak detector, a soft knee, program-dependent
    release (the longer it's been compressing, the slower it lets go) and a
    touch of FET colour. Shared by Pacific 76 and Silver 76. */
struct FetCompressor
{
    struct Settings
    {
        double inputGain = 1.0;      // linear
        double ratio = 4.0;
        double attackMs = 0.2, releaseMs = 300.0;
        double kneeDb = 6.0;
        bool allButtons = false;     // "all buttons in": harder, grittier, overshooting
    };

    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset() { env = 0.0; grDb = 0.0; held = 0.0; }

    void set (const Settings& s) noexcept
    {
        cfg = s;
        atk = 1.0 - std::exp (-1.0 / (std::max (0.005, cfg.attackMs) * 0.001 * fs));
        relBase = cfg.releaseMs;
    }

    inline double process (double x) noexcept
    {
        constexpr double thresholdDb = -18.0;   // in volts re 1 V, as on the hardware: Input drives into it
        const double xi = x * cfg.inputGain;

        // Program-dependent release: up to 2x slower after sustained compression.
        const double rel = 1.0 - std::exp (-1.0 / (relBase * (1.0 + std::min (1.0, held / 12.0)) * 0.001 * fs));
        const double level = std::abs (xi);
        env += (level > env ? atk : rel) * (level - env);

        const double over = 20.0 * std::log10 (env + 1.0e-9) - thresholdDb;
        const double slope = 1.0 - 1.0 / (cfg.allButtons ? 20.0 : cfg.ratio);
        const double knee = cfg.allButtons ? 1.0 : cfg.kneeDb;
        double gr = 0.0;
        if (over >= knee * 0.5)        gr = slope * over;
        else if (over > -knee * 0.5)   gr = slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
        if (cfg.allButtons) gr *= 1.25;   // the all-buttons-in overshoot

        grDb = gr;
        held += ((gr > 3.0 ? gr : 0.0) - held) * 0.00005;

        double y = xi * std::pow (10.0, -gr / 20.0);
        // FET colour: gentle, slightly asymmetric; much more with all buttons in.
        y = cfg.allButtons ? diodeClip (y * 2.0, 0.9, 0.8, 2.2) * 0.55 : diodeClip (y, 2.6, 2.3, 2.5);
        return y;
    }

    Settings cfg;
    double fs = 48000.0, env = 0.0, grDb = 0.0, held = 0.0, atk = 0.5, relBase = 300.0;
};

/** Peak gain reduction over the last block, decaying like a meter. */
struct GrMeter
{
    void update (double peakDb) noexcept
    {
        const float v = (float) std::max (peakDb, (double) value.load() * 0.85);
        value.store (v);
    }
    std::atomic<float> value { 0.0f };
};

//==============================================================================
/** Pacific 76: the Cali76 Compact Deluxe, an 1176-style FET compressor for
    guitar. IN drives the compressor, RATIO sweeps 4:1 to 20:1, ATTACK and
    RELEASE set the timing, OUT the level, and DRY blends the uncompressed
    signal back in (parallel compression). The row of dots is a live
    gain-reduction meter. */
class Pacific76 final : public CleanPedal
{
public:
    Pacific76()
        : iIn (control ("pacific76", "in")), iOut (control ("pacific76", "out")), iDry (control ("pacific76", "dry")),
          iAttack (control ("pacific76", "attack")), iRelease (control ("pacific76", "release")), iRatio (control ("pacific76", "ratio"))
    {
    }

    int telemetry (float* out) const noexcept override { out[0] = meter.value.load(); return 1; }

protected:
    void setRate (double r) override { for (auto& c : comp) c.prepare (r); for (auto& h : dcHp) h.setCutoff (8.0, r); }
    void resetModel() override { for (auto& c : comp) c.reset(); for (auto& h : dcHp) h.reset(); }
    void idle() noexcept override { meter.update (0.0); }

    void setControls (const float* v) noexcept override
    {
        FetCompressor::Settings s;
        s.inputGain = std::pow (10.0, (-10.0 + 40.0 * v[iIn]) / 20.0);
        s.ratio = 4.0 + 16.0 * v[iRatio];
        s.attackMs = 0.05 * std::pow (200.0, (double) v[iAttack]);       // 0.05 ms .. 10 ms
        s.releaseMs = 50.0 * std::pow (22.0, (double) v[iRelease]);      // 50 ms .. 1.1 s
        for (auto& c : comp) c.set (s);
        // Makeup: OUT at noon roughly matches the bypassed level at IN noon.
        out = levelKnob (v[iOut]) * 1.9;
        dry = (double) v[iDry];
        blockPeak = 0.0;
    }

    double tick (double x, int c) noexcept override
    {
        auto& k = comp[(size_t) c];
        const double y = k.process (x) * out + x * dry;
        blockPeak = std::max (blockPeak, k.grDb);
        if (c == 0) { if (++meterCount >= 256) { meter.update (blockPeak); blockPeak = 0.0; meterCount = 0; } }
        return dcHp[(size_t) c].highpass (y);
    }

private:
    std::array<FetCompressor, 2> comp;
    std::array<OnePole, 2> dcHp;
    GrMeter meter;
    const int iIn, iOut, iDry, iAttack, iRelease, iRatio;
    double out = 1.0, dry = 0.0, blockPeak = 0.0;
    int meterCount = 0;
};

//==============================================================================
/** Silver 76: the UAFX 1176 Studio Compressor. INPUT drives it, OUTPUT sets the
    level; ATTACK and RELEASE get faster as they turn up (as on the hardware);
    RATIO steps 4, 8, 12, 20 and ALL ("all buttons in"). MODE: SINGLE is one
    1176; DUAL runs two in series (a light one feeding the main one) for
    smoother, deeper compression; SUSTAIN pushes harder with a slower release
    for long, singing notes. MIX blends the dry signal back in (the
    hardware's parallel mode, made continuous). */
class Silver76 final : public CleanPedal
{
public:
    Silver76()
        : iInput (control ("silver76", "input")), iOutput (control ("silver76", "output")), iMode (control ("silver76", "mode")),
          iMix (control ("silver76", "mix")), iAttack (control ("silver76", "attack")),
          iRelease (control ("silver76", "release")), iRatio (control ("silver76", "ratio"))
    {
    }

    int telemetry (float* out) const noexcept override { out[0] = meter.value.load(); return 1; }

protected:
    void setRate (double r) override
    {
        for (auto& c : main) c.prepare (r);
        for (auto& c : first) c.prepare (r);
        for (auto& h : dcHp) h.setCutoff (8.0, r);
    }
    void resetModel() override { for (auto& c : main) c.reset(); for (auto& c : first) c.reset(); for (auto& h : dcHp) h.reset(); }
    void idle() noexcept override { meter.update (0.0); }

    void setControls (const float* v) noexcept override
    {
        mode = juce::roundToInt (v[iMode] * 2.0f);        // 0 SINGLE, 1 DUAL, 2 SUSTAIN
        mix = (double) v[iMix];                          // parallel compression: dry blended back in
        const int step = juce::jlimit (0, 4, juce::roundToInt (v[iRatio] * 4.0f));
        static constexpr double ratios[5] = { 4.0, 8.0, 12.0, 20.0, 20.0 };

        FetCompressor::Settings s;
        s.inputGain = std::pow (10.0, (-10.0 + 40.0 * v[iInput] + (mode == 2 ? 8.0 : 0.0)) / 20.0);
        s.ratio = ratios[step];
        s.allButtons = step == 4;
        s.attackMs = 0.8 * std::pow (0.025, (double) v[iAttack]);     // CW = faster: 800 us .. 20 us
        s.releaseMs = 1100.0 * std::pow (50.0 / 1100.0, (double) v[iRelease]) * (mode == 2 ? 2.0 : 1.0);
        for (auto& c : main) c.set (s);

        FetCompressor::Settings f = s;    // DUAL: a gentle 4:1 stage ahead of the main one
        f.ratio = 4.0; f.allButtons = false; f.inputGain = 1.0; f.attackMs = 0.5; f.releaseMs = 400.0;
        for (auto& c : first) c.set (f);

        out = levelKnob (v[iOutput]) * (mode == 2 ? 1.2 : 2.0);
        blockPeak = 0.0;
    }

    double tick (double x, int c) noexcept override
    {
        auto& k = main[(size_t) c];
        double y = x;
        if (mode == 1) y = first[(size_t) c].process (y) * 2.2;
        y = k.process (y) * out;
        y = mix * y + (1.0 - mix) * x;
        blockPeak = std::max (blockPeak, k.grDb + (mode == 1 ? first[(size_t) c].grDb : 0.0));
        if (c == 0) { if (++meterCount >= 256) { meter.update (blockPeak); blockPeak = 0.0; meterCount = 0; } }
        return dcHp[(size_t) c].highpass (y);
    }

private:
    std::array<FetCompressor, 2> main, first;
    std::array<OnePole, 2> dcHp;
    GrMeter meter;
    const int iInput, iOutput, iMode, iMix, iAttack, iRelease, iRatio;
    int mode = 0;
    double mix = 1.0, out = 1.0, blockPeak = 0.0;
    int meterCount = 0;
};

//==============================================================================
/** Fresh Squeeze: the Pulp 'N' Peel V4, a smooth, transparent compressor in the
    classic OTA family (slower, rounder than a FET). COMP sets how hard it
    squeezes, with automatic makeup; BLEND runs from all-dry (left) to fully
    compressed (right); EQ is flat at noon, darker left, brighter right; DIRT
    adds the warm parallel overdrive to the compressed side; VOLUME has lots of
    clean headroom, so it doubles as a boost. */
class FreshSqueeze final : public CleanPedal
{
public:
    FreshSqueeze()
        : iVolume (control ("freshSqueeze", "volume")), iComp (control ("freshSqueeze", "comp")),
          iEq (control ("freshSqueeze", "eq")), iBlend (control ("freshSqueeze", "blend")), iDirt (control ("freshSqueeze", "dirt"))
    {
    }

protected:
    void setRate (double r) override
    {
        rate = r;
        atk = 1.0 - std::exp (-1.0 / (0.004 * r));
        rel = 1.0 - std::exp (-1.0 / (0.25 * r));
        for (auto& s : st) { s.dcHp.setCutoff (8.0, r); s.dirtLp.setCutoff (6000.0, r); }
        lastEq = -1.0f;
    }
    void resetModel() override { for (auto& s : st) { s.env = 0.0; s.eq.reset(); s.dcHp.reset(); s.dirtLp.reset(); } lastEq = -1.0f; }

    void setControls (const float* v) noexcept override
    {
        const double comp = v[iComp];
        thresholdDb = -12.0 - 26.0 * comp;
        ratio = 2.0 + 10.0 * comp;
        makeup = std::pow (10.0, (-thresholdDb - 12.0) * (1.0 - 1.0 / ratio) * 0.45 / 20.0) * 1.6;
        blend = (double) v[iBlend];
        dirt = v[iDirt] > 0.5f;   // OFF|ON
        volume = levelKnob (v[iVolume]);
        if (v[iEq] != lastEq)
        {
            lastEq = v[iEq];
            const double db = (lastEq - 0.5) * 24.0;   // noon = flat
            for (auto& s : st) s.eq.setHighShelf (1800.0, db, rate);
        }
    }

    double tick (double x, int c) noexcept override
    {
        auto& s = st[(size_t) c];
        const double level = std::abs (x);
        s.env += (level > s.env ? atk : rel) * (level - s.env);
        const double over = 20.0 * std::log10 (s.env + 1.0e-9) - thresholdDb;
        const double gr = over > 0.0 ? over * (1.0 - 1.0 / ratio) : 0.0;
        double wet = x * std::pow (10.0, -gr / 20.0) * makeup;
        if (dirt)
            wet = s.dirtLp.lowpass (diodeClip (wet * 4.0, 0.6, 0.5, 2.2) * 0.45 + wet * 0.3);
        const double y = x * (1.0 - blend) + wet * blend;
        return s.dcHp.highpass (s.eq.process (y)) * volume;
    }

private:
    struct State { double env = 0.0; Biquad eq; OnePole dcHp, dirtLp; };
    std::array<State, 2> st;
    const int iVolume, iComp, iEq, iBlend, iDirt;
    double rate = 48000.0, atk = 0.01, rel = 0.0001, thresholdDb = -20.0, ratio = 4.0, makeup = 1.0, blend = 0.7, volume = 1.0;
    bool dirt = false;
    float lastEq = -1.0f;
};

} // namespace stompbox::dsp
