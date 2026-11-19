#pragma once

#include <cmath>
#include <vector>
#include <algorithm>

// Building blocks for the digital pedals: a fractional delay buffer, allpass
// diffusers, a two-grain pitch shifter, a swell (attack) detector and the
// wet/dry law used by Open Sky and Time Machine.

namespace stompbox::dsp
{

/** Power-of-two ring buffer with cubic (Hermite) fractional reads. */
struct DelayBuffer
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void allocate (int minSize)
    {
        int s = 4;
        while (s < minSize) s <<= 1;
        buf.assign ((size_t) s, 0.0f);
        mask = s - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int size() const noexcept { return mask + 1; }

    inline void push (double x) noexcept { buf[(size_t) w] = (float) x; w = (w + 1) & mask; }

    /** d samples behind the newest sample (d >= 2 for the cubic read). */
    inline double read (double d) const noexcept
    {
        d = std::clamp (d, 2.0, (double) (mask - 3));
        const double pos = (double) (w - 1) - d;
        const double fl = std::floor (pos);
        const int i = (int) fl;
        const double f = pos - fl;
        auto at = [this] (int k) { return (double) buf[(size_t) (k & mask)]; };
        const double y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    /** Integer read, d >= 0 samples behind the newest. */
    inline double tap (int d) const noexcept { return buf[(size_t) ((w - 1 - d) & mask)]; }
};

/** Schroeder allpass diffuser with an (optionally modulated) fractional delay. */
struct Allpass
{
    DelayBuffer b;
    double delay = 100.0, g = 0.5;

    void allocate (int maxSamples) { b.allocate (maxSamples + 8); }
    void clear() { b.clear(); }

    inline double process (double x, double mod = 0.0) noexcept
    {
        const double v = b.read (delay + mod);
        const double u = x + g * v;
        b.push (u);
        return v - g * u;
    }
};

/** Two crossfading grains read from a delay line: pitch shift by a ratio.
    window = grain length; short grains track faster, long ones sound smoother. */
struct PitchShifter
{
    DelayBuffer b;
    double phase = 0.0, window = 4096.0, ratio = 2.0;

    void allocate (int maxWindow) { b.allocate (maxWindow + 16); }
    void clear() { b.clear(); phase = 0.0; }
    void set (double semitones, double windowSamples) noexcept
    {
        ratio = std::pow (2.0, semitones / 12.0);
        window = std::min (windowSamples, (double) b.size() - 8.0);
    }

    inline double process (double x) noexcept
    {
        b.push (x);
        phase += (1.0 - ratio) / window;
        phase -= std::floor (phase);
        double out = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            double p = phase + 0.5 * k;
            p -= std::floor (p);
            const double s = std::sin (3.14159265358979323846 * p);
            out += s * s * b.read (2.0 + p * window);
        }
        return out;
    }
};

/** Swell: restarts a fade-in whenever a new note or chord is picked. */
struct SwellDetector
{
    double fast = 0.0, slow = 0.0, ramp = 1.0, fastA = 0.0, fastR = 0.0, slowA = 0.0, slowR = 0.0;
    int holdoff = 0, holdSamples = 2400;

    void prepare (double fs)
    {
        fastA = 1.0 - std::exp (-1.0 / (0.001 * fs));
        fastR = 1.0 - std::exp (-1.0 / (0.050 * fs));
        slowA = 1.0 - std::exp (-1.0 / (0.060 * fs));
        slowR = 1.0 - std::exp (-1.0 / (0.400 * fs));
        holdSamples = (int) (0.15 * fs);
        reset();
    }
    void reset() { fast = slow = 0.0; ramp = 1.0; holdoff = 0; }

    /** Returns the swell gain (0..1) for this sample. */
    inline double process (double x, double riseSamples) noexcept
    {
        const double a = std::abs (x);
        fast += (a > fast ? fastA : fastR) * (a - fast);
        slow += (a > slow ? slowA : slowR) * (a - slow);
        if (holdoff > 0) --holdoff;
        else if (fast > 0.004 && fast > 3.0 * slow + 0.003)   // a real new attack, not chord beating
        {
            ramp = 0.0;               // a new attack: start the swell again
            holdoff = holdSamples;
        }
        ramp = std::min (1.0, ramp + 1.0 / std::max (1.0, riseSamples));
        return ramp * ramp * (3.0 - 2.0 * ramp);   // smooth S curve
    }
};

/** Strymon-style MIX: dry stays full up to noon, then fades; wet reaches full at noon.
    Fully clockwise is 100% wet. */
inline void mixLaw (double m, double& dry, double& wet) noexcept
{
    dry = std::min (1.0, 2.0 * (1.0 - m));
    wet = std::min (1.0, 2.0 * m);
}

/** Small, fast, deterministic noise source. */
struct Noise
{
    unsigned int s = 22222u;
    inline double next() noexcept { s = s * 1664525u + 1013904223u; return ((double) (s >> 8) / 8388608.0) - 1.0; }
};

} // namespace stompbox::dsp
