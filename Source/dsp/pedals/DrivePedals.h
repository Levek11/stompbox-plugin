#pragma once

#include "Pedal.h"

namespace stompbox::dsp
{

//==============================================================================
/** Hot Rod: the SD-1 Super OverDrive circuit.

    The heart is a non-inverting op-amp stage: the guitar signal goes straight
    to the output, plus a feedback branch that only sees mids and highs
    (4.7k + 47 nF to ground: high-pass at ~720 Hz). The Drive pot (1M) sets
    that branch's gain, and three silicon diodes across it clip it
    asymmetrically (two one way, one the other) - the SD-1's signature. A
    51 pF cap across the feedback rolls the highs off as Drive goes up.
    Then the active Tone stage (a treble shelf around 1 kHz) and Level. */
class HotRod final : public DrivePedal
{
public:
    HotRod()
        : iLevel (control ("hotRod", "LEVEL")), iDrive (control ("hotRod", "DRIVE")), iTone (control ("hotRod", "TONE"))
    {
    }

protected:
    void setRate (double r) override { rate = r; }

    void resetModel() override
    {
        for (auto& s : st) s = {};
        lastDrive = lastTone = -1.0f;
    }

    void setControls (const float* v) noexcept override
    {
        const float drive = v[iDrive], tone = v[iTone];
        if (drive != lastDrive)
        {
            lastDrive = drive;
            const double rDrive = 15.0e3 + logTaper (drive) * 1.0e6;   // 1M (A) pot + wiring
            gain = rDrive / 4.7e3;
            const double fbLowpass = std::min (16000.0, 1.0 / (2.0 * pi * rDrive * 51.0e-12));
            for (auto& s : st) s.fbLp.setCutoff (fbLowpass, rate);
        }
        if (tone != lastTone)
        {
            lastTone = tone;
            toneGain = 0.12 + 1.5 * std::pow ((double) tone, 1.2);   // -18 dB .. +4 dB above the corner
        }
        for (auto& s : st)
        {
            s.inHp.setCutoff (7.0, rate);
            s.bandHp.setCutoff (1.0 / (2.0 * pi * 4.7e3 * 0.047e-6), rate);   // ~720 Hz
            s.toneLp.setCutoff (1100.0, rate);
            s.outLp.setCutoff (9000.0, rate);
        }
        level = 3.0 * logTaper (v[iLevel]);
    }

    double tick (double x, int c) noexcept override
    {
        auto& s = st[(size_t) c];
        x = s.inHp.highpass (x);
        const double branch = s.fbLp.lowpass (s.bandHp.highpass (x)) * gain;
        double y = railClip (x + diodeClip (branch, 1.15, 0.58, 2.3));
        const double lp = s.toneLp.lowpass (y);
        y = lp + toneGain * (y - lp);
        return s.outLp.lowpass (y) * level;
    }

private:
    struct State { OnePole inHp, bandHp, fbLp, toneLp, outLp; };
    std::array<State, 2> st;
    const int iLevel, iDrive, iTone;
    double rate = 192000.0, gain = 10.0, toneGain = 1.0, level = 1.0;
    float lastDrive = -1.0f, lastTone = -1.0f;
};

//==============================================================================
/** Golden Hour: the Morning Glory V4, a Bluesbreaker-family overdrive.

    An op-amp gain stage (Drive) whose gain is focused above the lows, then
    a pair of series silicon diodes each way to ground: soft, symmetrical
    clipping that keeps the guitar's character ("transparent"). The Gain
    toggle (up = HI) adds gain and lets more low end into the clipper. The
    Tone control is a high-pass blend that brightens clockwise, the Hi-Cut
    switch rounds off the top, and a high-headroom output stage (Volume)
    can push well past unity for a clean boost. */
class GoldenHour final : public DrivePedal
{
public:
    GoldenHour()
        : iVolume (control ("goldenHour", "volume")), iDrive (control ("goldenHour", "drive")),
          iGain (control ("goldenHour", "gain")), iTone (control ("goldenHour", "tone")),
          iHiCut (control ("goldenHour", "hicut"))
    {
    }

protected:
    void setRate (double r) override { rate = r; }

    void resetModel() override
    {
        for (auto& s : st) s = {};
        lastKey = -1.0f;
    }

    void setControls (const float* v) noexcept override
    {
        const bool highGain = v[iGain] < 0.5f;     // toggle up = HI
        hiCut = v[iHiCut] < 0.5f;                  // toggle up = ON
        const float key = v[iDrive] + (highGain ? 10.0f : 0.0f) + (hiCut ? 100.0f : 0.0f) + v[iTone] * 1000.0f;
        if (key != lastKey)
        {
            lastKey = key;
            const double drive = logTaper (v[iDrive], 2.0);
            gain = (2.0 + drive * 80.0) * (highGain ? 2.6 : 1.0);
            toneGain = 0.22 + 1.3 * (double) v[iTone];
            for (auto& s : st)
            {
                s.inHp.setCutoff (15.0, rate);
                s.gainHp.setCutoff (highGain ? 140.0 : 340.0, rate);
                s.gainLp.setCutoff (highGain ? 6000.0 : 7500.0, rate);
                s.toneLp.setCutoff (750.0, rate);
                s.hiCutLp.setCutoff (2800.0, rate);
                s.outLp.setCutoff (12000.0, rate);
            }
        }
        volume = 6.0 * logTaper (v[iVolume]);
    }

    double tick (double x, int c) noexcept override
    {
        auto& s = st[(size_t) c];
        x = s.inHp.highpass (x);
        // Non-inverting gain stage: x plus the focused, amplified part.
        double y = railClip (x + s.gainLp.lowpass (s.gainHp.highpass (x)) * gain);
        // Series diode pairs to ground: soft symmetrical clip at ~1.2 V.
        y = diodeClip (y, 1.2, 1.2, 2.0);
        const double lp = s.toneLp.lowpass (y);
        y = lp + toneGain * (y - lp);
        if (hiCut) y = s.hiCutLp.lowpass (y);
        else       s.hiCutLp.lowpass (y);   // keep the filter state warm for a click-free switch
        return s.outLp.lowpass (y) * volume;
    }

private:
    struct State { OnePole inHp, gainHp, gainLp, toneLp, hiCutLp, outLp; };
    std::array<State, 2> st;
    const int iVolume, iDrive, iGain, iTone, iHiCut;
    double rate = 192000.0, gain = 4.0, toneGain = 1.0, volume = 1.0;
    bool hiCut = false;
    float lastKey = -1.0f;
};

//==============================================================================
/** Blue Highway: the BD-2 Blues Driver.

    Two cascaded discrete op-amp gain stages, both controlled by the GAIN knob
    (a dual-gang pot, so they rise together). Each stage soft-clips through
    diodes in its feedback loop. Before the first stage the lows are boosted
    hard, which is why it gets fuzzy and touch-sensitive as GAIN goes up; a
    band-pass between the stages takes that bass back out and trims the top.
    The second stage also hard-limits at the rails. Then a wide-range TONE
    control and LEVEL. */
class BlueHighway final : public DrivePedal
{
public:
    BlueHighway()
        : iLevel (control ("blueHighway", "LEVEL")), iGain (control ("blueHighway", "GAIN")), iTone (control ("blueHighway", "TONE"))
    {
    }

protected:
    void setRate (double r) override { rate = r; }

    void resetModel() override
    {
        for (auto& s : st) s = {};
        lastGain = lastTone = -1.0f;
    }

    void setControls (const float* v) noexcept override
    {
        if (v[iGain] != lastGain || v[iTone] != lastTone)
        {
            lastGain = v[iGain]; lastTone = v[iTone];
            stageGain = 34.0 * logTaper (lastGain, 2.2);                  // per stage, ganged
            toneGain = 0.06 + 1.9 * std::pow ((double) lastTone, 1.4);    // wide: very dark to bright
            for (auto& s : st)
            {
                s.inHp.setCutoff (20.0, rate);
                s.bass.setLowShelf (260.0, 9.0, rate);       // the pre-clipping bass boost
                s.fb1.setCutoff (9000.0, rate);
                s.midHp.setCutoff (140.0, rate);             // takes the boosted bass back out
                s.midLp.setCutoff (5200.0, rate);
                s.fb2.setCutoff (7000.0, rate);
                s.toneLp.setCutoff (1000.0, rate);
                s.outLp.setCutoff (9000.0, rate);
                s.dcHp.setCutoff (10.0, rate);
            }
        }
        level = 4.0 * logTaper (v[iLevel]);
    }

    double tick (double x, int c) noexcept override
    {
        auto& s = st[(size_t) c];
        const double pre = s.bass.process (s.inHp.highpass (x));
        double y = pre + diodeClip (stageGain * s.fb1.lowpass (pre), 0.55, 0.55, 2.2);
        y = s.midLp.lowpass (s.midHp.highpass (y));
        y = railClip (y + diodeClip (stageGain * s.fb2.lowpass (y), 0.6, 0.6, 2.2), 3.2);
        const double lp = s.toneLp.lowpass (y);
        y = lp + toneGain * (y - lp);
        return s.dcHp.highpass (s.outLp.lowpass (y)) * level;
    }

private:
    struct State { OnePole inHp, fb1, midHp, midLp, fb2, toneLp, outLp, dcHp; Biquad bass; };
    std::array<State, 2> st;
    const int iLevel, iGain, iTone;
    double rate = 192000.0, stageGain = 4.0, toneGain = 1.0, level = 1.0;
    float lastGain = -1.0f, lastTone = -1.0f;
};

//==============================================================================
/** Scotland: the Kilt V2, a heavily tweaked Expandora-style op-amp drive.

    One op-amp gain stage into a clipper whose character the two gain toggles
    change: both down is the mildest, boost-to-overdrive setting; G1 up adds
    overdrive (more gain, a harder clip); G2 up heads into fuzz (lots of gain,
    asymmetric, saturated clipping with long sustain); both up is the gated
    fuzz, where quiet signal is choked off and notes sputter as they decay.
    TONE sweeps dark to bright, CUT/FLAT rounds off the top (CUT) or leaves it
    open (FLAT), VOLUME sets the output. */
class Scotland final : public DrivePedal
{
public:
    Scotland()
        : iVolume (control ("scotland", "volume")), iGain (control ("scotland", "gain")), iTone (control ("scotland", "tone")),
          iG1 (control ("scotland", "g1")), iG2 (control ("scotland", "g2")), iCut (control ("scotland", "cutflat"))
    {
    }

protected:
    void setRate (double r) override { rate = r; }

    void resetModel() override
    {
        for (auto& s : st) s = {};
        lastKey = -1.0f;
    }

    void setControls (const float* v) noexcept override
    {
        g1 = v[iG1] < 0.5f;     // toggles: up = ON
        g2 = v[iG2] < 0.5f;
        cut = v[iCut] < 0.5f;   // up = CUT
        const float key = v[iGain] + v[iTone] * 10.0f + (g1 ? 100.0f : 0.0f) + (g2 ? 200.0f : 0.0f);
        if (key != lastKey)
        {
            lastKey = key;
            gain = (1.5 + 70.0 * logTaper (v[iGain], 2.0)) * (g1 ? 2.5 : 1.0) * (g2 ? 4.0 : 1.0);
            toneGain = 0.1 + 1.5 * (double) v[iTone];
            for (auto& s : st)
            {
                s.inHp.setCutoff (35.0, rate);
                s.gainHp.setCutoff (g2 ? 90.0 : 160.0, rate);
                s.gainLp.setCutoff (g2 ? 5000.0 : 7000.0, rate);
                s.toneLp.setCutoff (900.0, rate);
                s.cutLp.setCutoff (2800.0, rate);
                s.outLp.setCutoff (10000.0, rate);
                s.dcHp.setCutoff (12.0, rate);
            }
        }
        volume = 4.0 * logTaper (v[iVolume]) * (g2 ? 0.7 : 1.0);
    }

    double tick (double x, int c) noexcept override
    {
        auto& s = st[(size_t) c];
        x = s.inHp.highpass (x);

        // Both toggles up: a starved, gated front end chokes quiet signal.
        double drive = x;
        if (g1 && g2)
        {
            constexpr double gate = 0.006;
            drive = std::abs (x) <= gate ? 0.0 : x - std::copysign (gate, x);
        }

        double y = drive + s.gainLp.lowpass (s.gainHp.highpass (drive)) * gain;
        if (g2)       y = diodeClip (y + 0.12, 0.42, 0.28, 3.5) - diodeClip (0.12, 0.42, 0.28, 3.5);   // fuzz: asymmetric, saturated
        else if (g1)  y = diodeClip (y, 0.6, 0.6, 3.0);                                                 // overdrive: harder clip
        else          y = diodeClip (y, 0.75, 0.75, 2.0);                                               // stock: soft

        const double lp = s.toneLp.lowpass (y);
        y = lp + toneGain * (y - lp);
        const double cutY = s.cutLp.lowpass (y);
        if (cut) y = cutY;
        return s.dcHp.highpass (s.outLp.lowpass (y)) * volume;
    }

private:
    struct State { OnePole inHp, gainHp, gainLp, toneLp, cutLp, outLp, dcHp; };
    std::array<State, 2> st;
    const int iVolume, iGain, iTone, iG1, iG2, iCut;
    double rate = 192000.0, gain = 4.0, toneGain = 1.0, volume = 1.0;
    bool g1 = false, g2 = false, cut = false;
    float lastKey = -1.0f;
};

//==============================================================================
/** Nutrient: the Protein Dual Overdrive V4, two drives in one box with a
    footswitch each. The signal enters the right (Green) side first and runs
    into the left (Blue) side, so both on is the Protein's stacked voice.

    Green (right), Nobels ODR-1 style "Nashville" drive: tight lows, extra
    low-mid weight, an op-amp stage into diodes to ground, and a tilting TONE.
    Blue (left), Bluesbreaker style: a soft-clipping gain stage into a gentle
    second clip, a brightening TONE and the 3-way HI-CUT (open, mid, dark). */
class Nutrient final : public DrivePedal
{
public:
    Nutrient()
        : iVolA (control ("nutrient", "volA")), iGainA (control ("nutrient", "gainA")), iToneA (control ("nutrient", "toneA")),
          iVolB (control ("nutrient", "volB")), iGainB (control ("nutrient", "gainB")), iToneB (control ("nutrient", "toneB")),
          iHiCut (control ("nutrient", "hicut"))
    {
    }

    void setSecondFootswitch (bool on) noexcept override { greenOn = on; }

    void process (float* const* ch, int numCh, int n, const float* v, bool on, const PedalContext& ctx) noexcept override
    {
        blueOn = on;
        DrivePedal::process (ch, numCh, n, v, on || greenOn, ctx);
    }

protected:
    void setRate (double r) override
    {
        rate = r;
        rampCoef = 1.0 - std::exp (-1.0 / (0.012 * rate));
    }

    void resetModel() override
    {
        for (auto& s : st) s = {};
        rampBlue = { blueOn ? 1.0 : 0.0, blueOn ? 1.0 : 0.0 };
        rampGreen = { greenOn ? 1.0 : 0.0, greenOn ? 1.0 : 0.0 };
        lastKey = -1.0f;
    }

    void setControls (const float* v) noexcept override
    {
        const int cutPos = juce::roundToInt (v[iHiCut] * 2.0f);
        const float key = v[iGainA] + v[iToneA] * 10.0f + v[iGainB] * 100.0f + v[iToneB] * 1000.0f + (float) cutPos * 10000.0f;
        if (key != lastKey)
        {
            lastKey = key;
            gainGreen = 1.0 + 40.0 * logTaper (v[iGainB], 2.0);
            const double t = (double) v[iToneB];
            tiltLow = 1.35 - 0.7 * t;
            tiltHigh = 0.35 + 1.3 * t;
            gainBlue = 30.0 * logTaper (v[iGainA], 2.0);
            toneBlue = 0.2 + 1.2 * (double) v[iToneA];
            const double cutHz = cutPos == 0 ? 16000.0 : cutPos == 1 ? 6000.0 : 3200.0;
            for (auto& s : st)
            {
                s.gIn.setCutoff (90.0, rate);                 // Green: tight low end
                s.gBeef.setPeak (420.0, 0.9, 4.0, rate);      // ...and low-mid weight
                s.gLp.setCutoff (7500.0, rate);
                s.gTilt.setCutoff (1000.0, rate);
                s.bHp.setCutoff (240.0, rate);
                s.bLp.setCutoff (8000.0, rate);
                s.bTone.setCutoff (1200.0, rate);
                s.bCut.setCutoff (cutHz, rate);
                s.dc.setCutoff (10.0, rate);
            }
        }
        levelGreen = 3.0 * logTaper (v[iVolB]);
        levelBlue = 3.5 * logTaper (v[iVolA]);
    }

    double tick (double x, int c) noexcept override
    {
        auto& s = st[(size_t) c];
        auto& rg = rampGreen[(size_t) c];
        auto& rb = rampBlue[(size_t) c];
        rg += ((greenOn ? 1.0 : 0.0) - rg) * rampCoef;
        rb += ((blueOn ? 1.0 : 0.0) - rb) * rampCoef;

        // Green (right side, first)
        double gIn = s.gIn.highpass (x);
        double g = diodeClip (gainGreen * s.gBeef.process (gIn), 0.62, 0.62, 2.6);
        g = s.gLp.lowpass (g);
        const double gl = s.gTilt.lowpass (g);
        g = (tiltLow * gl + tiltHigh * (g - gl)) * levelGreen;
        const double afterGreen = x + rg * (g - x);

        // Blue (left side, second)
        const double b0 = afterGreen;
        double b = b0 + diodeClip (gainBlue * s.bLp.lowpass (s.bHp.highpass (b0)), 0.5, 0.5, 2.2);
        b = diodeClip (b * 1.6, 0.95, 0.95, 2.4);
        const double bl = s.bTone.lowpass (b);
        b = s.bCut.lowpass (bl + toneBlue * (b - bl)) * levelBlue;
        const double out = b0 + rb * (b - b0);

        return s.dc.highpass (out);
    }

private:
    struct State { OnePole gIn, gLp, gTilt, bHp, bLp, bTone, bCut, dc; Biquad gBeef; };
    std::array<State, 2> st;
    const int iVolA, iGainA, iToneA, iVolB, iGainB, iToneB, iHiCut;
    double rate = 192000.0, rampCoef = 0.001;
    double gainGreen = 4.0, tiltLow = 1.0, tiltHigh = 1.0, levelGreen = 1.0;
    double gainBlue = 4.0, toneBlue = 1.0, levelBlue = 1.0;
    std::array<double, 2> rampBlue { 1.0, 1.0 }, rampGreen { 1.0, 1.0 };
    bool blueOn = true, greenOn = true;
    float lastKey = -1.0f;
};

} // namespace stompbox::dsp
