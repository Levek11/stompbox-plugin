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

} // namespace stompbox::dsp
