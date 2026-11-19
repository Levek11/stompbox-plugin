#pragma once

#include <atomic>
#include <vector>
#include "Pedal.h"

// True Tone (Boss TU-3 tuner) and Sculptor (Boss GE-7 equalizer).

namespace stompbox::dsp
{

//==============================================================================
/** True Tone: a chromatic tuner. While it's on, the output is muted (as the
    TU-3 does by default) and the pitch is tracked with the YIN method on a
    decimated copy of the input. The display reads the nearest note and how
    many cents off it is. */
class TrueTone final : public Pedal
{
public:
    static constexpr int window = 1024;       // analysis window at the decimated rate
    static constexpr int maxLag = 400;        // ~ 30 Hz at 12 kHz

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::round (fs / 12000.0));
        rate = fs / decim;
        aa1.setLowpass (0.4 * rate, 0.707, fs);
        aa2.setLowpass (0.4 * rate, 0.707, fs);
        ring.assign ((size_t) (window + maxLag), 0.0f);
        diff.assign ((size_t) maxLag + 1, 0.0f);
        fadeStep = 1.0f / (float) (0.01 * fs);
        reset();
    }

    void reset() override
    {
        aa1.reset(); aa2.reset();
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0; decimCount = 0; sinceAnalysis = 0;
        gain = 1.0f;
        note.store (-1.0f);
        smoothed = -1.0;
    }

    /** out[0] = MIDI note with fraction (69.0 = A440, 69.1 = A +10 cents), or -1
        when nothing steady is playing; out[1] = 1 while the tuner is engaged. */
    int telemetry (float* out) const noexcept override
    {
        out[0] = note.load();
        out[1] = engaged.load() ? 1.0f : 0.0f;
        return 2;
    }

    void process (float* const* ch, int numCh, int n, const float*, bool on, const PedalContext&) noexcept override
    {
        engaged.store (on);
        if (! on && gain >= 1.0f)
        {
            if (note.load() >= 0.0f) { note.store (-1.0f); smoothed = -1.0; }
            return;
        }

        if (on)
        {
            for (int i = 0; i < n; ++i)
            {
                double x = ch[0][i];
                if (numCh > 1) x = 0.5 * (x + ch[1][i]);
                x = aa2.process (aa1.process (x));
                if (++decimCount >= decim)
                {
                    decimCount = 0;
                    ring[(size_t) writePos] = (float) x;
                    writePos = (writePos + 1) % (int) ring.size();
                    if (++sinceAnalysis >= window / 4)
                    {
                        sinceAnalysis = 0;
                        analyse();
                    }
                }
            }
        }

        // Mute while tuning, with a short fade either way.
        const float target = on ? 0.0f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            gain = target > gain ? std::min (target, gain + fadeStep) : std::max (target, gain - fadeStep);
            for (int c = 0; c < numCh; ++c)
                ch[c][i] *= gain;
        }
    }

    /** Test hook: the frequency (Hz) the tuner currently reads, or -1. */
    double readingHz() const noexcept
    {
        const float m = note.load();
        return m < 0.0f ? -1.0 : 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
    }

private:
    void analyse() noexcept
    {
        const int size = (int) ring.size();
        auto at = [&] (int k) { return ring[(size_t) ((writePos + k) % size)]; };   // oldest first

        // Signal level: ignore silence.
        double energy = 0.0;
        for (int k = 0; k < window; ++k) energy += (double) at (k) * at (k);
        if (energy / window < 1.0e-6)
        {
            note.store (-1.0f); smoothed = -1.0;
            return;
        }

        // YIN: difference function and its cumulative-mean normalisation.
        const int minLag = std::max (2, (int) (rate / 1400.0));
        diff[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0.0;
            for (int k = 0; k < window; ++k)
            {
                const double e = (double) at (k) - at (k + tau);
                d += e * e;
            }
            running += d;
            diff[(size_t) tau] = (float) (running > 0.0 ? d * tau / running : 1.0);
        }

        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
            if (diff[(size_t) tau] < 0.12f)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            note.store (-1.0f); smoothed = -1.0;
            return;
        }

        // Parabolic interpolation around the dip.
        const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const double den = a - 2.0 * b + c;
        double lag = best + (std::abs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0);

        // High notes have short periods; measure across several periods (the
        // difference function dips again at each multiple) for finer resolution.
        const int m = (int) ((maxLag - 3) / lag);
        if (m >= 2)
        {
            int t = (int) std::round (m * lag);
            for (int k = t - 2; k <= t + 2; ++k)
                if (k > 1 && k < maxLag && diff[(size_t) k] < diff[(size_t) t]) t = k;
            const double a2 = diff[(size_t) t - 1], b2 = diff[(size_t) t], c2 = diff[(size_t) t + 1];
            const double den2 = a2 - 2.0 * b2 + c2;
            lag = (t + (std::abs (den2) > 1.0e-12 ? 0.5 * (a2 - c2) / den2 : 0.0)) / m;
        }
        const double hz = rate / lag;
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);

        // Settle the needle: smooth small moves, jump on a new note.
        smoothed = (smoothed < 0.0 || std::abs (midi - smoothed) > 0.5) ? midi : smoothed + 0.35 * (midi - smoothed);
        note.store ((float) smoothed);
    }

    double fs = 48000.0, rate = 12000.0, smoothed = -1.0;
    int decim = 4, decimCount = 0, writePos = 0, sinceAnalysis = 0;
    Biquad aa1, aa2;
    std::vector<float> ring, diff;
    float gain = 1.0f, fadeStep = 0.002f;
    std::atomic<float> note { -1.0f };
    std::atomic<bool> engaged { false };
};

//==============================================================================
/** Sculptor: the GE-7. Seven constant-Q bands at 100 Hz to 6.4 kHz, each
    +-15 dB, plus a +-15 dB LEVEL slider. Centred sliders are flat. */
class Sculptor final : public CleanPedal
{
public:
    Sculptor()
    {
        static const char* const ids[7] = { "b100", "b200", "b400", "b800", "b1k6", "b3k2", "b6k4" };
        for (int b = 0; b < 7; ++b) iBand[(size_t) b] = control ("sculptor", ids[b]);
        iLevel = control ("sculptor", "level");
    }

    static constexpr double centres[7] = { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };

protected:
    void setRate (double r) override { rate = r; last.fill (-1.0f); }
    void resetModel() override { for (auto& ch : bands) for (auto& f : ch) f.reset(); last.fill (-1.0f); }

    void setControls (const float* v) noexcept override
    {
        for (size_t b = 0; b < 7; ++b)
            if (v[iBand[b]] != last[b])
            {
                last[b] = v[iBand[b]];
                const double db = (last[b] - 0.5) * 30.0;
                for (auto& ch : bands) ch[b].setPeak (centres[b], 1.4, db, rate);
            }
        level = std::pow (10.0, (v[iLevel] - 0.5) * 30.0 / 20.0);
    }

    double tick (double x, int c) noexcept override
    {
        for (auto& f : bands[(size_t) c]) x = f.process (x);
        return x * level;
    }

private:
    std::array<std::array<Biquad, 7>, 2> bands;
    std::array<int, 7> iBand {};
    std::array<float, 7> last {};
    int iLevel = -1;
    double rate = 48000.0, level = 1.0;
};

} // namespace stompbox::dsp
