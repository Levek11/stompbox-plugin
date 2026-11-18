#pragma once

#include "Pedal.h"
#include <vector>

namespace stompbox::dsp
{

/** Afterglow: a modulated dual echo in the spirit of the Keeley HALO, which
    recreates Andy Timmons' dual-head tape echo.

    Two echo lines (A and B), each with its own feedback loop that behaves
    like tape: a high-pass and a gentle low-pass in the loop (every repeat
    gets darker and thinner, as on tape) and soft saturation (tape
    compression). A slow LFO wobbles the delay times, A and B a quarter
    cycle apart, which spreads the repeats in stereo.

    Rhythm:  1/4    both lines at a quarter note
             D.1/8  both lines at a dotted eighth
             HALO   A at a dotted eighth, B at a quarter note, A leaning left
                    and B leaning right: the "halo" of interlocking echoes

    SYNC ON follows Logic's tempo (and its tempo changes, gliding like tape);
    SYNC OFF uses the TIME knob in milliseconds.

    The dry signal passes untouched. Turning it off stops new input, but the
    repeats carry on and fade naturally (trails). */
class Afterglow final : public Pedal
{
public:
    Afterglow()
        : iTime (control ("afterglow", "time")), iRepeats (control ("afterglow", "repeats")),
          iMix (control ("afterglow", "mix")), iRate (control ("afterglow", "rate")),
          iDepth (control ("afterglow", "depth")), iRhythm (control ("afterglow", "rhythm")),
          iSync (control ("afterglow", "sync"))
    {
    }

    bool isStereo() const noexcept override { return true; }

    static constexpr double maxDelaySeconds = 2.0;

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        int size = 1;
        while (size < (int) (fs * maxDelaySeconds) + 8) size <<= 1;
        for (auto& l : lines) { l.buffer.assign ((size_t) size, 0.0f); l.mask = size - 1; }
        glide = 1.0 - std::exp (-1.0 / (0.12 * fs));            // tape-like glide on time changes
        gateStep = 1.0 / (0.005 * fs);
        for (auto& l : lines)
        {
            l.loopHp.setCutoff (140.0, fs);
            l.loopLp.setCutoff (3600.0, fs);
        }
        reset();
    }

    void reset() override
    {
        for (auto& l : lines)
        {
            std::fill (l.buffer.begin(), l.buffer.end(), 0.0f);
            l.write = 0; l.delay = -1.0; l.last = 0.0;
            l.loopHp.reset(); l.loopLp.reset();
        }
        phase = 0.0; gate = 0.0; silentSamples = 0; idle = true;
    }

    void process (float* const* ch, int numCh, int n, const float* v, bool on, const PedalContext& ctx) noexcept override
    {
        if (idle && ! on)
            return;   // off and silent: nothing to add
        if (idle)
            gate = 1.0;   // waking from silence: open straight away so the first note isn't softened
        idle = false;

        // --- Controls --------------------------------------------------------
        const int rhythm = juce::roundToInt (v[iRhythm] * 2.0f);   // 0 = 1/4, 1 = D.1/8, 2 = HALO
        const bool synced = v[iSync] < 0.5f;
        const double quarter = synced ? 60.0 / juce::jlimit (30.0, 300.0, ctx.bpm)
                                      : 0.001 * 60.0 * std::pow (1500.0 / 60.0, (double) v[iTime]);
        const double tA = rhythm == 0 ? quarter : 0.75 * quarter;
        const double tB = rhythm == 1 ? 0.75 * quarter : quarter;
        const double maxT = maxDelaySeconds - 0.01;
        lines[0].target = juce::jlimit (0.001, maxT, tA) * fs;
        lines[1].target = juce::jlimit (0.001, maxT, tB) * fs;

        const double feedback = 0.95 * std::pow ((double) v[iRepeats], 0.8);
        const double wetGain = 1.2 * (double) v[iMix];
        const double cross = rhythm == 2 ? 0.30 : 0.15;            // how much of each line reaches the other side
        const double lfoRate = 0.1 * std::pow (60.0, (double) v[iRate]);
        const double depth = 0.004 * fs * std::pow ((double) v[iDepth], 1.5);   // up to 4 ms of wobble
        const double phaseStep = lfoRate / fs;

        float* L = ch[0];
        float* R = numCh > 1 ? ch[1] : nullptr;
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            gate += on ? std::min (gateStep, 1.0 - gate) : -std::min (gateStep, gate);

            const double inL = L[i];
            const double inR = R != nullptr ? R[i] : inL;

            phase += phaseStep;
            if (phase >= 1.0) phase -= 1.0;
            const double modA = depth * (1.0 + std::sin (2.0 * pi * phase));
            const double modB = depth * (1.0 + std::sin (2.0 * pi * (phase + 0.25)));

            const double a = lines[0].tick (gate * inL, feedback, glide, modA);
            const double b = lines[1].tick (gate * inR, feedback, glide, modB);

            const double wetL = a + cross * b, wetR = b + cross * a;
            peak = std::max (peak, (float) std::max (std::abs (wetL), std::abs (wetR)));

            if (R != nullptr)
            {
                L[i] = (float) (inL + wetGain * wetL);
                R[i] = (float) (inR + wetGain * wetR);
            }
            else
                L[i] = (float) (inL + wetGain * 0.5 * (wetL + wetR));   // mono track: fold the echoes in
        }

        // Once off and the trails have died away, go idle (and clear the tape).
        // "Died away" means silent for longer than the longest echo: until
        // then a repeat can still be on its way round the tape.
        if (! on && gate == 0.0 && peak < 1.0e-5f)
            silentSamples += n;
        else
            silentSamples = 0;
        const double longest = std::max (lines[0].target, lines[1].target) + 0.05 * fs;
        if (silentSamples > (int) longest)
            reset();
    }

private:
    struct Line
    {
        std::vector<float> buffer;
        int mask = 0, write = 0;
        double delay = -1.0, target = 0.0, last = 0.0;
        OnePole loopHp, loopLp;

        // Cubic (Hermite) read, d samples behind the write head.
        double read (double d) const noexcept
        {
            const double pos = (double) write - d;
            const int i = (int) std::floor (pos);
            const double f = pos - i;
            auto at = [this] (int k) { return (double) buffer[(size_t) (k & mask)]; };
            const double y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
            const double c1 = 0.5 * (y2 - y0);
            const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
            const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }

        double tick (double in, double feedback, double glide, double mod) noexcept
        {
            if (delay < 0.0) delay = target;
            delay += (target - delay) * glide;
            const double out = read (delay + mod + 2.0);

            // Tape loop: thin, darken and gently saturate what goes back on.
            double w = in + feedback * out;
            w = loopLp.lowpass (loopHp.highpass (w));
            w = w / std::sqrt (1.0 + 0.3 * w * w);
            buffer[(size_t) write] = (float) w;
            write = (write + 1) & mask;
            return out;
        }
    };

    std::array<Line, 2> lines;
    const int iTime, iRepeats, iMix, iRate, iDepth, iRhythm, iSync;
    double fs = 48000.0, glide = 0.001, gateStep = 0.01, gate = 0.0, phase = 0.0;
    int silentSamples = 0;
    bool idle = true;
};

} // namespace stompbox::dsp
