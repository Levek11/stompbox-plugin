#pragma once

#include "Pedal.h"
#include "DelayTools.h"
#include "board/Machines.h"

namespace stompbox::dsp
{

/** Open Sky: a stereo multi-reverb in the spirit of the Strymon BigSky, with
    seven machines: Room, Hall, Plate, Swell, Bloom, Cloud and Shimmer.

    One engine underneath: pre-delay -> input diffusion (allpass chains) ->
    an 8-line feedback delay network (Hadamard mixing, modulated lines) whose
    per-line gains are set from the DECAY time (RT60) so the decay is accurate,
    with two-band damping in the loop (TONE sets the highs, LOW END the low
    band's decay). Each machine reshapes it:
      Room    small tank, strong early reflections; DIFFUSION thickens the attack
      Hall    big tank, early reflections that build; SIZE Concert / Arena
      Plate   no early reflections, dense fast build; SIZE Small / Large
      Swell   the reverb fades in behind each new note (RISE time)
      Bloom   a feedback "bloom" network ahead of the tank (LENGTH, FEEDBACK);
              TONE is a resonant filter
      Cloud   long cascaded diffusers ahead of the tank, up to 50 s decays;
              MOD past 2 o'clock speeds the diffuser modulation up
      Shimmer a pitch shifter in the tank's feedback (regenerating, so the
              shimmer climbs); AMOUNT and SHIFT (octave up by default)

    MIX: dry full up to noon, 100% wet fully clockwise. LEVEL +-6 dB.
    Trails: switching it off stops new input; the tail rings out. */
class OpenSky final : public Pedal
{
public:
    static constexpr int numLines = 8;

    OpenSky()
        : iType (control ("openSky", "type")), iDecay (control ("openSky", "decay")), iPre (control ("openSky", "predelay")),
          iMix (control ("openSky", "mix")), iTone (control ("openSky", "tone")), iP1 (control ("openSky", "param1")),
          iP2 (control ("openSky", "param2")), iMod (control ("openSky", "mod")), iLevel (control ("openSky", "level"))
    {
    }

    bool isStereo() const noexcept override { return true; }

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        const auto ms = [this] (double m) { return (int) std::ceil (m * 0.001 * fs); };
        for (auto& p : pre) p.allocate (ms (1550.0));
        er.allocate (ms (200.0));
        for (int c = 0; c < 2; ++c)
        {
            for (auto& a : diff[c]) a.allocate (ms (16.0));
            for (auto& a : cloudDiff[c]) a.allocate (ms (170.0));
            for (auto& a : bloomAp[c]) a.allocate (ms (520.0));
        }
        for (auto& l : lines) l.allocate (ms (maxLineMs * maxScale + 12.0));
        shifter.allocate (ms (120.0));
        swellDet.prepare (fs);
        gateStep = 1.0 / (0.004 * fs);
        fadeStep = 1.0 / (0.03 * fs);
        reset();
    }

    void reset() override
    {
        for (auto& p : pre) p.clear();
        er.clear();
        for (int c = 0; c < 2; ++c)
        {
            for (auto& a : diff[c]) a.clear();
            for (auto& a : cloudDiff[c]) a.clear();
            for (auto& a : bloomAp[c]) a.clear();
            bloomLast[c] = 0.0;
            erLp[c].reset(); toneLp1[c].reset(); toneLp2[c].reset();
        }
        clearTank();
        swellDet.reset();
        gate = 0.0; idle = true; silent = 0; machine = -1; fade = 1.0;
        preSmooth = -1.0;
    }

    void process (float* const* ch, int numCh, int n, const float* v, bool on, const PedalContext&) noexcept override
    {
        if (idle && ! on) return;
        if (idle) { gate = 1.0; idle = false; }
        juce::ScopedNoDenormals noDenormals;

        // --- Machine changes: fade the wet out, clear, fade back in -------------
        const int wanted = board::machines::stepIndex (v[iType], board::machines::numReverbs);
        if (machine < 0) { machine = wanted; clearTank(); }
        const bool switching = wanted != machine;

        setControls (v);

        double dryMix, wetMix;
        mixLaw ((double) v[iMix], dryMix, wetMix);
        const double level = std::pow (10.0, ((double) v[iLevel] - 0.5) * 12.0 / 20.0);

        float* L = ch[0];
        float* R = numCh > 1 ? ch[1] : nullptr;
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            gate += on ? std::min (gateStep, 1.0 - gate) : -std::min (gateStep, gate);
            fade += switching ? -std::min (fadeStep, fade) : std::min (fadeStep, 1.0 - fade);

            const double xL = L[i], xR = R != nullptr ? R[i] : xL;
            double inL = xL * gate, inR = xR * gate;

            if (machine == board::machines::swell)
            {
                const double s = swellDet.process (0.5 * (xL + xR) * gate, riseSamples);
                inL *= s; inR *= s;
            }

            double wL, wR;
            tick (inL, inR, wL, wR);
            wL *= fade * wetGain; wR *= fade * wetGain;
            peak = std::max (peak, (float) std::max (std::abs (wL), std::abs (wR)));

            // Pedal on: the mix law and LEVEL; off: dry untouched (the tail still rings).
            const double dry = 1.0 + gate * (dryMix * level - 1.0);
            const double wet = wetMix * (1.0 + gate * (level - 1.0));
            if (R != nullptr)
            {
                L[i] = (float) (dry * xL + wet * wL);
                R[i] = (float) (dry * xR + wet * wR);
            }
            else
                L[i] = (float) (dry * xL + wet * 0.5 * (wL + wR));
        }

        if (switching && fade <= 0.0)
        {
            machine = wanted;
            clearTank();
        }

        // Off and the tail has gone: go idle.
        if (! on && gate == 0.0 && peak < 1.0e-5f) silent += n; else silent = 0;
        if (silent > (int) (0.25 * fs)) reset();
    }

private:
    // --- Tables ----------------------------------------------------------------
    static constexpr double maxLineMs = 79.33, maxScale = 1.6;
    static constexpr double lineMs[numLines] = { 31.71, 37.13, 41.07, 43.73, 53.31, 59.93, 67.71, maxLineMs };
    static constexpr double diffMs[4] = { 4.771, 3.595, 12.73, 9.307 };
    static constexpr double cloudMs[6] = { 31.7, 47.9, 67.3, 89.1, 113.7, 151.3 };
    static constexpr double bloomMs[4] = { 0.21, 0.27, 0.24, 0.28 };   // fractions of LENGTH
    static constexpr double denseMs[numLines] = { 6.91, 9.37, 5.53, 11.07, 7.83, 4.79, 10.19, 8.61 };
    // Input and output patterns (found by search over sign/line patterns for
    // the least L-R correlation across all machines): a mono guitar comes out
    // wide, and the two sides are never out of phase, so a mono mix is safe.
    static constexpr double inPat[numLines] = { 1, -1, 1, 1, -1, -1, 1, 1 };
    static constexpr double tapL[numLines] = { 1, 1, 0, 1, 0, 0, 0, -1 };
    static constexpr double tapR[numLines] = { 0, 0, 1, 0, -1, -1, -1, 0 };
    static constexpr double tapGain = 0.354;

    struct Line
    {
        DelayBuffer b;
        double delay = 1000.0, gHigh = 0.5, gLow = 0.5, lfoPhase = 0.0, lfoStep = 0.0, dampB = 0.0, dampZ = 0.0;
        OnePole lowSplit;
        Allpass dense;   // in-loop allpass: builds echo density fast (smooth, not metallic)
        void allocate (int s) { b.allocate (s); dense.allocate (s / 4); }
        void clear() { b.clear(); dense.clear(); lowSplit.reset(); dampZ = 0.0; }
    };

    void clearTank()
    {
        for (auto& l : lines) l.clear();
        shifter.clear();
        shimmerFb = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            for (auto& a : diff[c]) a.clear();
            for (auto& a : cloudDiff[c]) a.clear();
            for (auto& a : bloomAp[c]) a.clear();
            bloomLast[c] = 0.0;
        }
    }

    // --- Per-block settings -------------------------------------------------------
    void setControls (const float* v) noexcept
    {
        using namespace board::machines;
        const int m = machine;
        const double p1 = v[iP1], p2 = v[iP2], mod = v[iMod], tone = v[iTone];

        // Tank size per machine
        double scale = 1.0;
        switch (m)
        {
            case room:    scale = 0.42; break;
            case hall:    scale = stepIndex ((float) p2, 2) == 0 ? 1.0 : 1.5; break;
            case plate:   scale = stepIndex ((float) p2, 2) == 0 ? 0.55 : 0.8; break;
            case cloud:   scale = 1.35; break;
            case shimmer: scale = 1.2; break;
            default:      scale = 1.0; break;
        }

        // LOW END (where the machine has it): low band decays 0.4x .. 1.6x as long
        double lowEnd = 0.5;
        if (m == room || m == hall || m == plate) lowEnd = p1;
        else if (m == swell || m == cloud) lowEnd = p2;
        const double lowRatio = 0.5 * std::pow (4.0, lowEnd);   // noon = neutral

        const double rt = decaySeconds (m, (float) v[iDecay]);
        // TONE: how long the highs ring relative to the mids (high-frequency RT60
        // ratio), from dark (0.12x) through warm (0.4x at noon) to nearly full
        // bandwidth (0.95x). Set per line so every line agrees on the decay.
        const double hfRatio = m == bloom ? 0.8 : 0.12 * std::pow (0.95 / 0.12, tone);
        const double dampHz = 1500.0 * std::pow (12.0, tone);   // early reflections' tone
        const double modDepth = std::pow (mod, 1.5) * (m == plate ? 0.0008 : 0.0016) * fs;
        const double rateScale = (m == cloud && mod > 0.67) ? 1.0 + (mod - 0.67) * 15.0 : 1.0;

        for (int k = 0; k < numLines; ++k)
        {
            auto& l = lines[(size_t) k];
            l.delay = lineMs[k] * scale * 0.001 * fs;
            l.dense.delay = denseMs[k] * std::sqrt (scale) * 0.001 * fs;
            l.dense.g = m == plate ? 0.62 : 0.5;
            const double loopLen = l.delay + l.dense.delay;   // the allpass adds to the loop time
            const double g = std::pow (10.0, -3.0 * loopLen / (fs * rt));
            l.gHigh = g;
            l.gLow = std::min (0.99995, std::pow (10.0, -3.0 * loopLen / (fs * rt * lowRatio)));
            const double r = std::pow (10.0, -3.0 * loopLen / fs * (1.0 / (rt * hfRatio) - 1.0 / rt));   // Nyquist / DC gain
            l.dampB = (1.0 - r) / (1.0 + r);
            l.lowSplit.setCutoff (250.0, fs);
            l.lfoStep = (0.31 + 0.137 * k) * rateScale / fs;
        }
        lineMod = modDepth;

        // Input diffusion
        double dg = 0.6;
        if (m == plate) dg = 0.72;
        else if (m == room) dg = 0.3 + 0.45 * p2;
        diffGain = dg;
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
            {
                diff[c][(size_t) k].delay = diffMs[k] * (c == 0 ? 1.0 : 1.083) * 0.001 * fs * (m == plate ? 0.8 : 1.0);
                diff[c][(size_t) k].g = dg;
            }

        // Cloud: long cascaded diffusers; DIFFUSION from grainy to smooth
        if (m == cloud)
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 6; ++k)
                {
                    cloudDiff[c][(size_t) k].delay = cloudMs[k] * (c == 0 ? 1.0 : 1.07) * 0.001 * fs;
                    cloudDiff[c][(size_t) k].g = 0.25 + 0.5 * p1;
                }
        cloudMod = std::pow (std::min (mod, 0.67) / 0.67, 1.5) * 0.004 * fs;
        cloudRate = 0.25 * rateScale / fs;

        // Bloom: LENGTH 100 ms .. 1 s of blooming delays, FEEDBACK around them
        if (m == bloom)
        {
            const double len = 0.1 * std::pow (10.0, p1) * fs;
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 4; ++k)
                {
                    bloomAp[c][(size_t) k].delay = len * bloomMs[k] * (c == 0 ? 1.0 : 1.05);
                    bloomAp[c][(size_t) k].g = 0.55;
                }
            bloomFb = 0.85 * p2;
            // TONE: a resonant low-pass, synth-like
            const double fc = 300.0 * std::pow (40.0, tone);
            for (int c = 0; c < 2; ++c) { toneLp1[c].setLowpass (fc, 2.2, fs); toneLp2[c].setLowpass (fc * 1.02, 0.6, fs); }
        }

        // Shimmer: interval and amount in the regenerating loop
        if (m == shimmer)
        {
            shifter.set (intervalSemis[stepIndex ((float) p2, 7)], 0.09 * fs);
            shimmerAmount = 1.3 * p1;
        }
        else
            shimmerAmount = 0.0;

        riseSamples = riseSeconds ((float) p1) * fs;

        // Early reflections
        erGain = m == room ? 0.55 : m == hall ? 0.32 : 0.0;
        erScale = m == hall ? (stepIndex ((float) p2, 2) == 0 ? 1.0 : 1.5) : 0.45;
        for (auto& f : erLp) f.setCutoff (std::min (16000.0, dampHz * 1.5), fs);

        // Pre-delay (glides when turned)
        // (the tank's shortest line already delays the reverb a little: take it off)
        const double preTarget = std::max (3.0, preDelaySeconds (v[iPre]) * fs - lineMs[0] * scale * 0.001 * fs);
        if (preSmooth < 0.0) preSmooth = preTarget;
        preTargetSamples = preTarget;

        // Wet loudness: roughly even across machines and decays
        static constexpr double trims[board::machines::numReverbs] = { 0.70, 0.95, 0.93, 1.40, 0.92, 0.80, 0.83 };
        wetGain = trims[m] / std::sqrt (std::max (0.5, rt));
        wetGain *= std::sqrt (2.0);
    }

    // --- One stereo sample through the reverb ---------------------------------------
    inline void tick (double inL, double inR, double& outL, double& outR) noexcept
    {
        using namespace board::machines;
        const int m = machine;

        // Pre-delay
        preSmooth += (preTargetSamples - preSmooth) * 0.0005;
        pre[0].push (inL); pre[1].push (inR);
        double x[2] = { pre[0].read (preSmooth - 1.0), pre[1].read (preSmooth - 1.0) };

        // Early reflections (Room, Hall) from the pre-delayed signal
        double erL = 0.0, erR = 0.0;
        if (erGain > 0.0)
        {
            er.push (0.5 * (x[0] + x[1]));
            static constexpr double erMs[12] = { 3.1, 5.3, 7.9, 10.6, 13.9, 17.3, 21.2, 25.9, 30.7, 36.1, 42.3, 49.1 };
            static constexpr double erG[12] = { 0.82, -0.74, 0.69, 0.63, -0.58, 0.52, -0.47, 0.42, 0.37, -0.33, 0.28, 0.24 };
            for (int k = 0; k < 12; ++k)
            {
                // Hall builds: later reflections a little louder
                const double g = erG[k] * (m == hall ? (0.5 + 0.08 * k) : 1.0);
                const double t = er.tap ((int) (erMs[k] * erScale * 0.001 * fs));
                if (k & 1) erR += g * t; else erL += g * t;
            }
            erL = erLp[0].lowpass (erL) * erGain;
            erR = erLp[1].lowpass (erR) * erGain;
        }

        // Input diffusion
        cloudPhase += cloudRate; cloudPhase -= std::floor (cloudPhase);
        for (int c = 0; c < 2; ++c)
        {
            double s = x[c];
            if (m != room || diffGain > 0.3)
                for (auto& a : diff[c]) s = a.process (s);
            if (m == cloud)
                for (int k = 0; k < 6; ++k)
                {
                    const double ph = cloudPhase + 0.17 * k + 0.5 * c;
                    s = cloudDiff[c][(size_t) k].process (s, cloudMod * (1.0 + std::sin (6.283185307179586 * ph)));
                }
            if (m == bloom)
            {
                double b = s + bloomFb * bloomLast[c];
                for (auto& a : bloomAp[c]) b = a.process (b);
                b = b / std::sqrt (1.0 + 0.25 * b * b);   // keep the bloom loop tame
                bloomLast[c] = b;
                s = b;
            }
            x[c] = s;
        }

        // Tank: read, mix (Hadamard), decay, damp, add input, write back
        double o[numLines];
        for (int k = 0; k < numLines; ++k)
        {
            auto& l = lines[(size_t) k];
            l.lfoPhase += l.lfoStep; l.lfoPhase -= std::floor (l.lfoPhase);
            const double modS = lineMod * (1.0 + std::sin (6.283185307179586 * l.lfoPhase));
            o[k] = l.b.read (l.delay - modS - 2.0);
        }

        double h[numLines];
        for (int k = 0; k < numLines; ++k) h[k] = o[k];
        hadamard8 (h);

        const double shimmerIn = shimmerFb;
        for (int k = 0; k < numLines; ++k)
        {
            auto& l = lines[(size_t) k];
            const double low = l.lowSplit.lowpass (h[k]);
            double s = l.gLow * low + l.gHigh * (h[k] - low);
            l.dampZ = (1.0 - l.dampB) * s + l.dampB * l.dampZ;
            s = l.dense.process (l.dampZ);
            s += 0.5 * inPat[k] * (x[k & 1] + shimmerIn);
            l.b.push (s);
        }

        double wl = 0.0, wr = 0.0;
        for (int k = 0; k < numLines; ++k) { wl += tapL[k] * o[k]; wr += tapR[k] * o[k]; }
        wl *= tapGain; wr *= tapGain;

        // Shimmer: pitch-shift the tank's output back into it (regenerating)
        if (shimmerAmount > 0.0)
        {
            double sum = 0.0;
            for (int k = 0; k < numLines; ++k) sum += (tapL[k] + tapR[k]) * o[k];   // the wet, in mono
            double s = shifter.process (tapGain * sum);
            s *= shimmerAmount;
            shimmerFb = s / std::sqrt (1.0 + 4.0 * s * s);
        }
        else
            shimmerFb = 0.0;

        if (m == bloom)
        {
            wl = toneLp2[0].process (toneLp1[0].process (wl));
            wr = toneLp2[1].process (toneLp1[1].process (wr));
        }

        outL = wl + erL;
        outR = wr + erR;
    }

    static inline void hadamard8 (double* a) noexcept
    {
        for (int len = 1; len < numLines; len <<= 1)
            for (int i = 0; i < numLines; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const double u = a[j], w = a[j + len];
                    a[j] = u + w; a[j + len] = u - w;
                }
        constexpr double norm = 0.35355339059327373;   // 1/sqrt(8)
        for (int i = 0; i < numLines; ++i) a[i] *= norm;
    }

    const int iType, iDecay, iPre, iMix, iTone, iP1, iP2, iMod, iLevel;
    double fs = 48000.0;

    std::array<DelayBuffer, 2> pre;
    DelayBuffer er;
    std::array<std::array<Allpass, 4>, 2> diff, bloomAp;
    std::array<std::array<Allpass, 6>, 2> cloudDiff;
    std::array<Line, numLines> lines;
    std::array<OnePole, 2> erLp;
    std::array<Biquad, 2> toneLp1, toneLp2;
    PitchShifter shifter;
    SwellDetector swellDet;

    double bloomLast[2] = { 0.0, 0.0 };
    double gate = 0.0, gateStep = 0.01, fade = 1.0, fadeStep = 0.001;
    double lineMod = 0.0, diffGain = 0.6, cloudMod = 0.0, cloudRate = 0.0, cloudPhase = 0.0;
    double bloomFb = 0.0, shimmerAmount = 0.0, shimmerFb = 0.0, riseSamples = 1000.0;
    double erGain = 0.0, erScale = 1.0, preSmooth = -1.0, preTargetSamples = 2.0, wetGain = 0.5;
    int machine = -1, silent = 0;
    bool idle = true;
};

} // namespace stompbox::dsp
