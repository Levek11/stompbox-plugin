#pragma once

#include "Pedal.h"
#include "DelayTools.h"
#include "board/Machines.h"

namespace stompbox::dsp
{

/** Time Machine: a stereo multi-delay in the spirit of the Strymon TimeLine,
    with seven machines: Digital, dTape, dBucket, Dual, Ice, Swell and Duck.

    TIME syncs to Logic's tempo (1/16 to 1/2, dotted and triplet values) or,
    with the SYNC switch on FREE, runs free in milliseconds (60 ms - 2.5 s;
    dBucket 40 - 400 ms, or 80 - 800 ms on the DOUBLE range). REPEATS goes up
    to self-oscillation; MIX: dry full up to noon, 100% wet fully clockwise.

    Common knobs (Digital, Dual, Ice, Swell, Duck), as on the TimeLine:
      FILTER  minimum = flat; up to noon an analog-style darkening; 12-2 the
              darkest; past that the lows thin out and some highs return (tape EQ)
      GRIT    distortion and digital artifacts; hotter playing = more grit
      SPEED / DEPTH  delay modulation
    Machine specifics:
      Digital clean, crystal-clear repeats; TWEAK = SMEAR (diffuses each repeat)
      dTape   FILTER = TAPE AGE, GRIT = TAPE BIAS (9 o'clock = optimal),
              SPEED = TAPE CRINKLE, DEPTH = WOW & FLUTTER, TWEAK = LOW END contour
      dBucket bucket-brigade: repeats darken with longer times (the BBD clock);
              FILTER noon = neutral, left = brighter, right = darker;
              GRIT = BUCKET LOSS; TWEAK = RANGE (Single / Double chip)
      Dual    two delays, the second at TWEAK x the first (4:3 by default),
              one per side (parallel)
      Ice     pitch-shifted slices (TWEAK = interval) blended into the delays
      Swell   the delays fade in behind each new note (TWEAK = rise time)
      Duck    the delays duck while you play, bloom when you stop (TWEAK =
              sensitivity)
    Trails: switching it off stops new input; the repeats carry on. */
class TimeMachine final : public Pedal
{
public:
    TimeMachine()
        : iType (control ("timeMachine", "type")), iTime (control ("timeMachine", "time")), iRepeats (control ("timeMachine", "repeats")),
          iMix (control ("timeMachine", "mix")), iFilter (control ("timeMachine", "filter")), iGrit (control ("timeMachine", "grit")),
          iSpeed (control ("timeMachine", "speed")), iDepth (control ("timeMachine", "depth")), iTweak (control ("timeMachine", "tweak")),
          iSync (control ("timeMachine", "sync"))
    {
    }

    bool isStereo() const noexcept override { return true; }

    static constexpr double maxDelaySeconds = 2.6;

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        for (auto& c : chans)
        {
            c.line.allocate ((int) (fs * maxDelaySeconds) + (int) (0.02 * fs) + 64);
            for (auto& a : c.smear) a.allocate ((int) (0.016 * fs));
            c.shifter.allocate ((int) (0.12 * fs));
            c.crinkleLp.setCutoff (14.0, fs);
        }
        swellDet.prepare (fs);
        gateStep = 1.0 / (0.004 * fs);
        duckA = 1.0 - std::exp (-1.0 / (0.002 * fs));
        duckR = 1.0 - std::exp (-1.0 / (0.060 * fs));
        duckGainA = 1.0 - std::exp (-1.0 / (0.010 * fs));
        duckGainR = 1.0 - std::exp (-1.0 / (0.250 * fs));
        reset();
    }

    void reset() override
    {
        for (auto& c : chans)
        {
            c.line.clear();
            for (auto& a : c.smear) a.clear();
            c.shifter.clear();
            c.lp1.reset(); c.lp2.reset(); c.hp.reset(); c.post.reset(); c.bump.reset(); c.crinkleLp.reset();
            c.delay = -1.0;
        }
        swellDet.reset();
        gate = 0.0; lfo = 0.0; wow = 0.0; wowTarget = 0.0; wowPhase = 0.0; flutterPhase = 0.0; duckEnv = 0.0; duckGain = 1.0;
        idle = true; silent = 0;
    }

    void process (float* const* ch, int numCh, int n, const float* v, bool on, const PedalContext& ctx) noexcept override
    {
        if (idle && ! on) return;
        if (idle) { gate = 1.0; idle = false; }
        juce::ScopedNoDenormals noDenormals;

        setControls (v, ctx.bpm);

        double dryMix, wetMix;
        mixLaw ((double) v[iMix], dryMix, wetMix);

        float* L = ch[0];
        float* R = numCh > 1 ? ch[1] : nullptr;
        float peak = 0.0f;
        const bool dualLines = machine == board::machines::dual;

        for (int i = 0; i < n; ++i)
        {
            gate += on ? std::min (gateStep, 1.0 - gate) : -std::min (gateStep, gate);
            const double xL = L[i], xR = R != nullptr ? R[i] : xL;
            double in[2] = { xL * gate, xR * gate };

            // Swell: the delays fade in behind each new attack
            if (machine == board::machines::dSwell)
            {
                const double s = swellDet.process (0.5 * (in[0] + in[1]), riseSamples);
                in[0] *= s; in[1] *= s;
            }

            // Duck: while you play above the threshold, the repeats tuck under;
            // when you stop, they bloom back up.
            double duck = 1.0;
            if (machine == board::machines::duck)
            {
                const double a = std::abs (0.5 * (xL + xR)) * gate;
                duckEnv += (a > duckEnv ? duckA : duckR) * (a - duckEnv);
                const double overDb = 20.0 * std::log10 (duckEnv + 1.0e-9) - duckThresholdDb;
                const double target = std::pow (10.0, -std::clamp (overDb * 1.5, 0.0, 26.0) / 20.0);
                duckGain += (target < duckGain ? duckGainA : duckGainR) * (target - duckGain);
                duck = duckGain;
            }

            // Modulation: LFO (both sides a quarter cycle apart), or tape wow & flutter
            const double mod0 = modulation (0), mod1 = modulation (1);

            double out[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& s = chans[(size_t) c];
                const double target = c == 1 && dualLines ? target2 : target1;
                if (s.delay < 0.0) s.delay = target;
                s.delay += (target - s.delay) * glide;
                const double o = s.line.read (s.delay - 1.0 + (c == 0 ? mod0 : mod1));   // -1: read before write

                double w = in[c];
                if (machine == board::machines::ice)
                    w = (1.0 - iceBlend) * w + iceBlend * s.shifter.process (w);
                w += feedback * o;
                w = loop (s, w);
                s.line.push (w);

                out[c] = post (s, o) * duck;
            }

            peak = std::max (peak, (float) std::max (std::abs (out[0]), std::abs (out[1])));
            const double dry = 1.0 + gate * (dryMix - 1.0);
            if (R != nullptr)
            {
                L[i] = (float) (dry * xL + wetMix * out[0]);
                R[i] = (float) (dry * xR + wetMix * out[1]);
            }
            else
                L[i] = (float) (dry * xL + wetMix * 0.5 * (out[0] + out[1]));
        }

        // Off and the repeats have died away: go idle.
        if (! on && gate == 0.0 && peak < 1.0e-5f) silent += n; else silent = 0;
        if (silent > (int) (std::max (target1, target2) + 0.05 * fs)) reset();
    }

    /** Test hook: current delay times in ms (left / Delay 1, right / Delay 2). */
    double delay1Ms() const noexcept { return target1 / fs * 1000.0; }
    double delay2Ms() const noexcept { return target2 / fs * 1000.0; }

private:
    struct Chan
    {
        DelayBuffer line;
        std::array<Allpass, 3> smear;
        PitchShifter shifter;
        Biquad lp1, lp2, post, bump;
        OnePole hp, crinkleLp;
        double delay = -1.0;
    };

    // --- Per-block settings -------------------------------------------------------
    void setControls (const float* v, double bpm) noexcept
    {
        using namespace board::machines;
        machine = stepIndex (v[iType], numDelays);
        const float tweak = v[iTweak];
        const bool sync = v[iSync] < 0.5f;

        const double ms = delayMs (machine, tweak, v[iTime], sync, bpm);
        target1 = ms * 0.001 * fs;
        target2 = target1;
        if (machine == dual)
            target2 = std::clamp (ms * dualRatios[stepIndex (tweak, 9)], 20.0, 2500.0) * 0.001 * fs;

        // Tape and bucket glide in pitch when the time changes; digital glides fast.
        const double glideSeconds = (machine == dTape || machine == dBucket) ? 0.25 : 0.06;
        glide = 1.0 - std::exp (-1.0 / (glideSeconds * fs));

        // REPEATS: up to (just past) self-oscillation; the loop's limiter keeps it bounded.
        feedback = 1.04 * std::pow ((double) v[iRepeats], 0.9);

        // Modulation
        const double speed = v[iSpeed], depth = v[iDepth];
        lfoStep = 0.1 * std::pow (60.0, speed) / fs;               // 0.1 .. 6 Hz
        lfoDepth = depth * depth * 0.005 * fs;                     // up to 5 ms
        if (machine == dBucket) lfoDepth = depth * depth * 0.003 * fs;
        // dTape: DEPTH = wow & flutter, SPEED = crinkle
        wowDepth = std::pow (depth, 1.5) * 0.003 * fs;
        flutterDepth = std::pow (depth, 1.5) * 0.00025 * fs;
        crinkle = speed;

        riseSamples = delayRiseSeconds (tweak) * fs;
        duckThresholdDb = -18.0 - 36.0 * tweak;   // SENSITIVITY: higher = ducks for quieter playing
        iceBlend = 0.6;
        if (machine == ice)
            for (auto& c : chans) c.shifter.set (intervalSemis[stepIndex (tweak, 7)], 0.09 * fs);
        for (int k = 0; k < 3; ++k)
        {
            static constexpr double smearMs[3] = { 3.13, 7.31, 11.87 };
            for (int c = 0; c < 2; ++c)
            {
                chans[(size_t) c].smear[(size_t) k].delay = smearMs[k] * (c ? 1.07 : 1.0) * 0.001 * fs;
                chans[(size_t) c].smear[(size_t) k].g = 0.7 * (machine == digital ? (double) tweak : 0.0);
            }
        }
        smearOn = machine == digital && tweak > 0.01f;

        grit = v[iGrit];

        setFilters (v[iFilter], tweak, (double) v[iGrit]);
    }

    void setFilters (double f, double tweak, double g) noexcept
    {
        using namespace board::machines;
        double lpHz = 20000.0, hpHz = 20.0, q = 0.707, postDb = 0.0, bumpDb = 0.0;

        if (machine == dTape)
        {
            lpHz = 14000.0 * std::pow (1800.0 / 14000.0, f);        // TAPE AGE: fresh -> dark
            hpHz = 20.0 * std::pow (30.0, tweak);                   // LOW END contour: full .. 600 Hz
            postDb = 7.0 * std::max (0.0, 0.25 - g) / 0.25;         // under-biased: extra highs
            bumpDb = 2.0;                                            // playback head bump
        }
        else if (machine == dBucket)
        {
            // The BBD's clock falls as the delay gets longer: darker repeats.
            const double stages = stepIndex ((float) tweak, 2) == 1 ? 8192.0 : 4096.0;
            const double seconds = std::max (0.01, target1 / fs);
            lpHz = std::clamp (0.27 * stages / (2.0 * seconds), 900.0, 9000.0);
            if (f > 0.5) lpHz *= std::pow (0.25, (f - 0.5) * 2.0);  // right of noon: darker
            else postDb = 9.0 * (0.5 - f) * 2.0;                     // left of noon: brighter (post shelf)
            hpHz = 60.0;
            q = 0.6;
        }
        else
        {
            if (f < 0.5)       { const double t = f / 0.5;            lpHz = 20000.0 * std::pow (2500.0 / 20000.0, t); hpHz = 20.0 + 60.0 * t; }
            else if (f < 0.67) { const double t = (f - 0.5) / 0.17;  lpHz = 2500.0 * std::pow (1200.0 / 2500.0, t);   hpHz = 80.0; }
            else               { const double t = (f - 0.67) / 0.33; lpHz = 1200.0 * std::pow (5000.0 / 1200.0, t);   hpHz = 80.0 * std::pow (600.0 / 80.0, t); }
        }
        lpHz = std::min (lpHz, 0.45 * fs);
        for (auto& c : chans)
        {
            c.lp1.setLowpass (lpHz, q, fs);
            c.lp2.setLowpass (lpHz, 0.54, fs);
            c.hp.setCutoff (hpHz, fs);
            c.post.setHighShelf (3500.0, postDb, fs);
            c.bump.setPeak (110.0, 1.0, bumpDb, fs);
        }
        filterFlat = lpHz >= 19000.0 && hpHz <= 21.0;
    }

    // --- The feedback loop: everything that colours each pass round ------------------
    inline double loop (Chan& s, double w) noexcept
    {
        using namespace board::machines;
        if (! filterFlat || machine == dTape || machine == dBucket)
        {
            w = s.lp2.process (s.lp1.process (w));
            w = s.hp.highpass (w);
        }

        if (machine == dTape)
        {
            w = s.bump.process (w);
            // Bias: past optimal (9 o'clock) less level and headroom, more compression
            const double drive = 1.0 + 3.0 * grit;
            const double lvl = 1.0 - 0.45 * std::max (0.0, grit - 0.25);
            w = std::tanh (w * drive * 0.8) / (drive * 0.8) * lvl;
            // Crinkle: worn tape, small dropouts
            if (crinkle > 0.01)
            {
                const double drop = std::abs (s.crinkleLp.lowpass (noise.next()));
                w *= 1.0 - std::min (0.6, crinkle * crinkle * drop * 9.0);
            }
        }
        else if (machine == dBucket)
        {
            // Bucket loss: distortion, level loss and hiss growing per pass
            const double loss = grit;
            w = diodeClip (w * (1.0 + 2.5 * loss), 1.1, 1.0, 2.2) / (1.0 + 0.9 * loss);
            w += noise.next() * loss * loss * 0.004;
            w = w * 0.985;
        }
        else
        {
            if (smearOn)
                for (auto& a : s.smear) w = a.process (w);
            if (grit > 0.001)
            {
                const double drive = 1.0 + 14.0 * grit * grit;
                w = diodeClip (w * drive, 0.9, 0.9, 2.0) / std::sqrt (drive);
                if (grit > 0.55)
                {
                    const double steps = std::pow (2.0, 14.0 - 18.0 * (grit - 0.55));   // down to ~6 bits
                    w = std::round (w * steps) / steps;
                }
            }
        }
        // Always: a soft limiter so runaway repeats stay musical and bounded.
        return diodeClip (w, 1.6, 1.6, 3.0);
    }

    inline double post (Chan& s, double o) noexcept
    {
        using namespace board::machines;
        if (machine == dTape || machine == dBucket)
            return s.post.process (o);
        return o;
    }

    inline double modulation (int c) noexcept
    {
        using namespace board::machines;
        if (c == 0)
        {
            lfo += lfoStep; lfo -= std::floor (lfo);
            if (machine == dTape)
            {
                // Wow: a slow, wandering drift; flutter: faster and smaller.
                wowPhase += 0.55 / fs; wowPhase -= std::floor (wowPhase);
                flutterPhase += 7.3 / fs; flutterPhase -= std::floor (flutterPhase);
                if (++wowCount >= 512) { wowCount = 0; wowTarget = noise.next(); }
                wow += (wowTarget - wow) * (2.0 / fs);
            }
        }
        if (machine == dTape)
            return wowDepth * (1.0 + 0.6 * std::sin (6.283185307179586 * wowPhase) + 0.4 * wow)
                 + flutterDepth * (1.0 + std::sin (6.283185307179586 * (flutterPhase + 0.25 * c)));
        double p = lfo + 0.25 * c; p -= std::floor (p);
        if (machine == dBucket)   // triangle, like an analog delay's chorus LFO
            return lfoDepth * (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p) * 2.0;
        return lfoDepth * (1.0 + std::sin (6.283185307179586 * p));
    }

    const int iType, iTime, iRepeats, iMix, iFilter, iGrit, iSpeed, iDepth, iTweak, iSync;
    double fs = 48000.0;
    std::array<Chan, 2> chans;
    SwellDetector swellDet;
    Noise noise;

    int machine = 0, silent = 0, wowCount = 0;
    double target1 = 1000.0, target2 = 1000.0, glide = 0.001, feedback = 0.3;
    double lfo = 0.0, lfoStep = 0.0, lfoDepth = 0.0, wow = 0.0, wowTarget = 0.0, wowPhase = 0.0, flutterPhase = 0.0;
    double wowDepth = 0.0, flutterDepth = 0.0, crinkle = 0.0, grit = 0.0, riseSamples = 1000.0;
    double duckThresholdDb = -30.0, duckEnv = 0.0, duckGain = 1.0, duckA = 0.01, duckR = 0.001, duckGainA = 0.01, duckGainR = 0.001, iceBlend = 0.6;
    double gate = 0.0, gateStep = 0.01;
    bool idle = true, smearOn = false, filterFlat = true;
};

} // namespace stompbox::dsp
