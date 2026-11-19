#pragma once

// Pedal and chain checks, included from DspTest.cpp.

#include "dsp/pedals/DrivePedals.h"
#include "dsp/pedals/Afterglow.h"
#include "dsp/pedals/Dynamics.h"
#include "dsp/pedals/Utility.h"
#include "dsp/Engine.h"

namespace pedaltests
{
using namespace stompbox;
using namespace stompbox::dsp;

inline std::vector<float> defaults (const char* id)
{
    std::vector<float> v (16, 0.0f);
    const auto& cs = board::catalog()[(size_t) board::indexOf (id)].controls;
    for (size_t i = 0; i < cs.size(); ++i) v[i] = cs[i].defaultValue;
    return v;
}

inline void set (std::vector<float>& v, const char* pedal, const char* control, float value)
{
    v[(size_t) Pedal::control (pedal, control)] = value;
}

/** THD (%) of a 220 Hz sine through a pedal. */
inline double thd (Pedal& p, const std::vector<float>& v, double amp, bool on = true)
{
    const double fs = 48000.0;
    p.prepare (fs, 256);
    const int n = (int) fs;
    std::vector<float> buf ((size_t) n);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (amp * std::sin (2.0 * pi * 220.0 * i / fs));
    PedalContext ctx;
    for (int i = 0; i < n; i += 256)
    {
        float* ch[1] = { buf.data() + i };
        p.process (ch, 1, std::min (256, n - i), v.data(), on, ctx);
    }
    // Goertzel on the second half
    auto g = [&] (double f)
    {
        const double w = 2.0 * pi * f / fs, c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = n / 2; i < n / 2 + (int) (fs / 220.0) * 80; ++i) { const double s0 = buf[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
    };
    double h = 0;
    for (int k = 2; k <= 10; ++k) { const double m = g (220.0 * k); h += m * m; }
    return 100.0 * std::sqrt (h) / (g (220.0) + 1e-12);
}

/** Feeds an impulse; returns the stereo output. */
inline std::array<std::vector<float>, 2> impulse (Pedal& p, const std::vector<float>& v, double seconds, double bpm,
                                                   int switchOffAt = -1)
{
    const double fs = 48000.0;
    p.prepare (fs, 256);
    const int n = (int) (fs * seconds);
    std::array<std::vector<float>, 2> out { std::vector<float> ((size_t) n, 0.0f), std::vector<float> ((size_t) n, 0.0f) };
    out[0][0] = out[1][0] = 1.0f;
    PedalContext ctx; ctx.bpm = bpm;
    for (int i = 0; i < n; i += 256)
    {
        float* ch[2] = { out[0].data() + i, out[1].data() + i };
        const bool on = switchOffAt < 0 || i < switchOffAt;
        p.process (ch, 2, std::min (256, n - i), v.data(), on, ctx);
    }
    return out;
}

inline double peakTimeMs (const std::vector<float>& x, double fromMs, double toMs)
{
    const int a = (int) (fromMs * 48.0), b = std::min ((int) x.size(), (int) (toMs * 48.0));
    int best = a; float m = 0.0f;
    for (int i = a; i < b; ++i) if (std::abs (x[(size_t) i]) > m) { m = std::abs (x[(size_t) i]); best = i; }
    return best / 48.0;
}

inline float peakIn (const std::vector<float>& x, double fromMs, double toMs)
{
    float m = 0.0f;
    for (int i = (int) (fromMs * 48.0); i < std::min ((int) x.size(), (int) (toMs * 48.0)); ++i) m = std::max (m, std::abs (x[(size_t) i]));
    return m;
}

template <typename Check>
void run (Check&& check)
{
    std::cout << "\nPedals\n";

    // --- Hot Rod ----------------------------------------------------------------
    {
        HotRod p;
        auto v = defaults ("hotRod");
        set (v, "hotRod", "DRIVE", 0.0f); const double t0 = thd (p, v, 0.3);
        set (v, "hotRod", "DRIVE", 0.5f); const double t5 = thd (p, v, 0.3);
        set (v, "hotRod", "DRIVE", 1.0f); const double t10 = thd (p, v, 0.3);
        std::cout << "    Hot Rod THD at Drive 0 / 5 / 10: " << juce::String (t0, 1) << "% / "
                  << juce::String (t5, 1) << "% / " << juce::String (t10, 1) << "%\n";
        check (t0 < t5 && t5 < t10 && t10 > 10.0, "Hot Rod: more Drive = more overdrive");

        // Bypass is exact
        const float before = 0.123f;
        std::vector<float> buf (512, before);
        float* ch[1] = { buf.data() };
        p.prepare (48000.0, 512);
        p.process (ch, 1, 512, v.data(), false, {});
        check (buf[100] == before && buf[511] == before, "Hot Rod: off = signal untouched");
    }

    // --- Golden Hour --------------------------------------------------------------
    {
        GoldenHour p;
        auto v = defaults ("goldenHour");
        set (v, "goldenHour", "drive", 0.0f); const double clean = thd (p, v, 0.3);
        set (v, "goldenHour", "drive", 0.6f); const double lo = thd (p, v, 0.3);
        set (v, "goldenHour", "gain", 0.0f);  const double hi = thd (p, v, 0.3);
        std::cout << "    Golden Hour THD: Drive 0 " << juce::String (clean, 1) << "%, Drive 6 LO "
                  << juce::String (lo, 1) << "%, HI " << juce::String (hi, 1) << "%\n";
        check (clean < 3.0, "Golden Hour: Drive 0 is nearly clean (clean boost)");
        check (hi > lo && lo > clean, "Golden Hour: Gain switch HI adds drive");
    }

    // --- Afterglow ----------------------------------------------------------------
    {
        Afterglow p;
        auto v = defaults ("afterglow");
        set (v, "afterglow", "depth", 0.0f);
        set (v, "afterglow", "repeats", 0.0f);
        set (v, "afterglow", "mix", 1.0f);

        // Always HALO, always synced: dotted eighth leaning left, quarter leaning right.
        auto o = impulse (p, v, 1.0, 120.0);
        check (peakIn (o[0], 360.0, 400.0) > 0.1f, "Afterglow: the very first note gets its echo at full level");
        const double hL = peakTimeMs (o[0], 50.0, 900.0), hR = peakTimeMs (o[1], 50.0, 900.0);
        check (std::abs (hL - 375.0) < 2.0 && std::abs (hR - 500.0) < 2.0,
               "Afterglow HALO at 120 BPM: dotted eighth left (" + juce::String (hL, 0) + " ms), quarter right ("
               + juce::String (hR, 0) + " ms)");

        o = impulse (p, v, 1.2, 90.0);
        check (std::abs (peakTimeMs (o[0], 50.0, 1100.0) - 500.0) < 2.0 && std::abs (peakTimeMs (o[1], 50.0, 1100.0) - 666.7) < 2.0,
               "Afterglow follows Logic's tempo (90 BPM: 500 ms and 667 ms)");

        // TONE: brighter repeats clockwise. Compare high-frequency energy of the 2nd repeat.
        {
            auto hfEnergy = [&] (float tone)
            {
                auto w = v; set (w, "afterglow", "tone", tone); set (w, "afterglow", "repeats", 0.6f);
                auto r = impulse (p, w, 1.0, 120.0);
                double e = 0;
                for (int i = (int) (740.0 * 48); i < (int) (760.0 * 48); ++i)
                {
                    const double d = r[0][(size_t) i] - r[0][(size_t) i - 1];   // first difference ~ highs
                    e += d * d;
                }
                return e;
            };
            const double dark = hfEnergy (0.0f), mid = hfEnergy (0.6f), bright = hfEnergy (1.0f);
            check (dark < mid && mid < bright, "Afterglow TONE: repeats get brighter clockwise");
        }

        // Trails: switch off right after the note; the echo still comes.
        set (v, "afterglow", "repeats", 0.5f);
        o = impulse (p, v, 1.5, 120.0, 4800);
        check (peakIn (o[0], 360.0, 400.0) > 0.05f && peakIn (o[0], 735.0, 775.0) > 0.01f,
               "Afterglow trails: switched off, the repeats still ring out");

        // Fully off and idle: untouched.
        Afterglow q; q.prepare (48000.0, 256);
        std::vector<float> a (256, 0.2f), b (256, 0.2f);
        float* ch[2] = { a.data(), b.data() };
        q.process (ch, 2, 256, v.data(), false, {});
        check (a[200] == 0.2f && b[200] == 0.2f, "Afterglow: off with no trails = signal untouched");
    }

    // --- Levels: switching a drive on at its default settings shouldn't jump the
    // volume wildly (real pedals set to taste sit within a few dB of unity).
    {
        auto gainDb = [] (Pedal& p, const char* id)
        {
            auto v = defaults (id);
            const double fs = 48000.0;
            p.prepare (fs, 256);
            const int n = (int) fs;
            std::vector<float> buf ((size_t) n);
            for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (0.3 * std::sin (2.0 * pi * 220.0 * i / fs));
            double inSq = 0, outSq = 0;
            for (int i = n / 2; i < n; ++i) inSq += (double) buf[(size_t) i] * buf[(size_t) i];
            for (int i = 0; i < n; i += 256) { float* ch[1] = { buf.data() + i }; p.process (ch, 1, std::min (256, n - i), v.data(), true, {}); }
            for (int i = n / 2; i < n; ++i) outSq += (double) buf[(size_t) i] * buf[(size_t) i];
            return 10.0 * std::log10 (outSq / inSq);
        };
        HotRod h; GoldenHour g;
        const double hdb = gainDb (h, "hotRod"), gdb = gainDb (g, "goldenHour");
        check (hdb > -8.0 && hdb < 8.0 && gdb > -8.0 && gdb < 8.0,
               "default settings near unity: Hot Rod " + juce::String (hdb, 1) + " dB, Golden Hour " + juce::String (gdb, 1) + " dB");
    }

    // --- Hot Rod clips asymmetrically (2 diodes one way, 1 the other) ----------
    {
        HotRod p; auto v = defaults ("hotRod"); set (v, "hotRod", "DRIVE", 0.8f);
        const double fs = 48000.0; p.prepare (fs, 256);
        const int n = (int) fs; std::vector<float> buf ((size_t) n);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (0.3 * std::sin (2.0 * pi * 220.0 * i / fs));
        for (int i = 0; i < n; i += 256) { float* ch[1] = { buf.data() + i }; p.process (ch, 1, std::min (256, n - i), v.data(), true, {}); }
        float hi = 0.0f, lo = 0.0f;
        for (int i = n / 2; i < n; ++i) { hi = std::max (hi, buf[(size_t) i]); lo = std::min (lo, buf[(size_t) i]); }
        check (std::abs (hi / lo) > 1.2f || std::abs (lo / hi) > 1.2f,
               "Hot Rod: asymmetric clipping (+" + juce::String (hi, 2) + " / " + juce::String (lo, 2) + ")");
    }

    // --- Afterglow at full feedback, loud input: stays bounded ------------------
    {
        Afterglow p; auto v = defaults ("afterglow");
        set (v, "afterglow", "repeats", 1.0f); set (v, "afterglow", "mix", 1.0f); set (v, "afterglow", "depth", 1.0f);
        p.prepare (48000.0, 256);
        const int n = 48000 * 10;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = i < 48000 ? (float) std::sin (i * 0.05) : 0.0f;
        float peak = 0.0f; bool finite = true;
        PedalContext ctx;
        for (int i = 0; i < n; i += 256)
        {
            ctx.bpm = 90.0 + 60.0 * std::sin (i / 48000.0);   // tempo changes too
            float* ch[2] = { l.data() + i, r.data() + i };
            p.process (ch, 2, std::min (256, n - i), v.data(), true, ctx);
        }
        for (int i = 0; i < n; ++i) { finite = finite && std::isfinite (l[(size_t) i]); peak = std::max (peak, std::abs (l[(size_t) i])); }
        check (finite && peak < 8.0f, "Afterglow: full feedback + tempo changes stays bounded (peak " + juce::String (peak, 2) + ")");
    }

    // --- Batch 3 ----------------------------------------------------------------
    auto unityDb = [] (Pedal& p, std::vector<float> v)
    {
        const double fs = 48000.0;
        p.prepare (fs, 256);
        const int n = (int) fs;
        std::vector<float> buf ((size_t) n);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (0.3 * std::sin (2.0 * pi * 220.0 * i / fs));
        double inSq = 0, outSq = 0;
        for (int i = n / 2; i < n; ++i) inSq += (double) buf[(size_t) i] * buf[(size_t) i];
        for (int i = 0; i < n; i += 256) { float* ch[1] = { buf.data() + i }; p.process (ch, 1, std::min (256, n - i), v.data(), true, {}); }
        for (int i = n / 2; i < n; ++i) outSq += (double) buf[(size_t) i] * buf[(size_t) i];
        return 10.0 * std::log10 (outSq / inSq);
    };
    auto bypassExact = [] (Pedal& p, std::vector<float> v)
    {
        std::vector<float> buf (512, 0.123f);
        float* ch[1] = { buf.data() };
        p.prepare (48000.0, 512);
        p.process (ch, 1, 512, v.data(), false, {});
        return buf[100] == 0.123f && buf[511] == 0.123f;
    };

    {
        BlueHighway p; auto v = defaults ("blueHighway");
        set (v, "blueHighway", "GAIN", 0.0f); const double t0 = thd (p, v, 0.3);
        set (v, "blueHighway", "GAIN", 0.5f); const double t5 = thd (p, v, 0.3);
        set (v, "blueHighway", "GAIN", 1.0f); const double t10 = thd (p, v, 0.3);
        std::cout << "    Blue Highway THD at Gain 0 / 5 / 10: " << juce::String (t0, 1) << "% / " << juce::String (t5, 1)
                  << "% / " << juce::String (t10, 1) << "%\n";
        check (t0 < t5 && t5 < t10 && t10 > 15.0, "Blue Highway: more Gain = more drive, up to fuzzy");
        // Touch sensitivity: soft picking cleaner than hard picking at the same setting
        auto w = defaults ("blueHighway");
        const double soft = thd (p, w, 0.05), hard = thd (p, w, 0.5);
        check (hard > soft * 2.0, "Blue Highway: touch-sensitive (soft " + juce::String (soft, 1) + "%, hard " + juce::String (hard, 1) + "%)");
        const double db = unityDb (p, defaults ("blueHighway"));
        check (db > -8.0 && db < 8.0 && bypassExact (p, defaults ("blueHighway")),
               "Blue Highway: default level " + juce::String (db, 1) + " dB, off = untouched");
    }

    {
        Scotland p; auto v = defaults ("scotland");
        const double stock = thd (p, v, 0.3);
        set (v, "scotland", "g1", 0.0f); const double od = thd (p, v, 0.3);
        set (v, "scotland", "g1", 1.0f); set (v, "scotland", "g2", 0.0f); const double fuzz = thd (p, v, 0.3);
        std::cout << "    Scotland THD stock / G1 / G2: " << juce::String (stock, 1) << "% / " << juce::String (od, 1)
                  << "% / " << juce::String (fuzz, 1) << "%\n";
        check (stock < od && od < fuzz, "Scotland: G1 adds overdrive, G2 goes into fuzz");

        // Both up = gated: a very quiet signal is choked off, unlike G2 alone.
        auto quietRms = [&] (std::vector<float> w)
        {
            p.prepare (48000.0, 256);
            const int n = 48000; std::vector<float> buf ((size_t) n);
            for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (0.004 * std::sin (2.0 * pi * 220.0 * i / 48000.0));
            for (int i = 0; i < n; i += 256) { float* ch[1] = { buf.data() + i }; p.process (ch, 1, std::min (256, n - i), w.data(), true, {}); }
            double e = 0; for (int i = n / 2; i < n; ++i) e += (double) buf[(size_t) i] * buf[(size_t) i];
            return std::sqrt (e / (n / 2));
        };
        auto gated = v; set (gated, "scotland", "g1", 0.0f);
        const double open = quietRms (v), closed = quietRms (gated);
        check (closed < open * 0.25, "Scotland: G1 + G2 gates quiet notes (" + juce::String (20.0 * std::log10 (closed / open + 1e-12), 1) + " dB)");

        const double db = unityDb (p, defaults ("scotland"));
        check (db > -8.0 && db < 8.0 && bypassExact (p, defaults ("scotland")),
               "Scotland: default level " + juce::String (db, 1) + " dB, off = untouched");
    }

    {
        Nutrient p; auto v = defaults ("nutrient");
        auto run = [&] (bool blue, bool green, float amp)
        {
            p.setSecondFootswitch (green);
            const double fs = 48000.0; p.prepare (fs, 256);
            const int n = (int) fs; std::vector<float> buf ((size_t) n);
            for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (amp * std::sin (2.0 * pi * 220.0 * i / fs));
            for (int i = 0; i < n; i += 256) { float* ch[1] = { buf.data() + i }; p.process (ch, 1, std::min (256, n - i), v.data(), blue, {}); }
            return buf;
        };
        auto thdOf = [] (const std::vector<float>& buf)
        {
            const double fs = 48000.0; const int n = (int) buf.size();
            auto g = [&] (double f)
            {
                const double w = 2.0 * pi * f / fs, c = 2.0 * std::cos (w); double s1 = 0, s2 = 0;
                for (int i = n / 2; i < n / 2 + (int) (fs / 220.0) * 80; ++i) { const double s0 = buf[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
            };
            double h = 0; for (int k = 2; k <= 10; ++k) { const double m = g (220.0 * k); h += m * m; }
            return 100.0 * std::sqrt (h) / (g (220.0) + 1e-12);
        };
        const double green = thdOf (run (false, true, 0.3)), blue = thdOf (run (true, false, 0.3)), both = thdOf (run (true, true, 0.3));
        std::cout << "    Nutrient THD Green / Blue / both: " << juce::String (green, 1) << "% / " << juce::String (blue, 1)
                  << "% / " << juce::String (both, 1) << "%\n";
        check (green > 1.0 && blue > 1.0 && both > green && both > blue, "Nutrient: each side drives on its own; stacked drives harder");
        const auto off = run (false, false, 0.3);
        check (std::abs (off[30000] - (float) (0.3 * std::sin (2.0 * pi * 220.0 * 30000 / 48000.0))) < 1.0e-6f,
               "Nutrient: both footswitches off = untouched");
    }

    // --- Batch 4: compressors, tuner, EQ ------------------------------------------
    std::cout << "\nCompressors, tuner and EQ\n";

    /** Steady-state gain (dB) of a sine through a pedal. */
    auto gainDb = [] (Pedal& p, const std::vector<float>& v, double amp, double hz = 220.0)
    {
        const double fs = 48000.0;
        p.prepare (fs, 256);
        const int n = (int) fs;
        std::vector<float> buf ((size_t) n);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (amp * std::sin (2.0 * pi * hz * i / fs));
        double inSq = 0, outSq = 0;
        for (int i = n / 2; i < n; ++i) inSq += (double) buf[(size_t) i] * buf[(size_t) i];
        for (int i = 0; i < n; i += 256) { float* ch[1] = { buf.data() + i }; p.process (ch, 1, std::min (256, n - i), v.data(), true, {}); }
        for (int i = n / 2; i < n; ++i) outSq += (double) buf[(size_t) i] * buf[(size_t) i];
        return 10.0 * std::log10 (outSq / inSq);
    };
    /** How many dB the output moves when the input moves 24 dB (less = more compression). */
    auto range = [&] (Pedal& p, const std::vector<float>& v)
    {
        // output dB at 0.32 minus output dB at 0.02 (input differs by 24 dB)
        return (gainDb (p, v, 0.32) + 20.0 * std::log10 (0.32)) - (gainDb (p, v, 0.02) + 20.0 * std::log10 (0.02));
    };

    {
        Pacific76 p; auto v = defaults ("pacific76");
        const double r = range (p, v);
        set (v, "pacific76", "ratio", 0.0f); const double r4 = range (p, v);
        set (v, "pacific76", "ratio", 1.0f); const double r20 = range (p, v);
        const double db = gainDb (p, defaults ("pacific76"), 0.3);
        std::cout << "    Pacific 76: 24 dB in -> " << juce::String (r, 1) << " dB out (4:1 " << juce::String (r4, 1)
                  << ", 20:1 " << juce::String (r20, 1) << "), level " << juce::String (db, 1) << " dB\n";
        check (r < 16.0 && r20 < r4, "Pacific 76: compresses, and harder at higher Ratio");
        auto w = defaults ("pacific76"); set (w, "pacific76", "dry", 1.0f);
        check (range (p, w) > r + 2.0, "Pacific 76: Dry blends the uncompressed attack back in");
        float t[4]; gainDb (p, defaults ("pacific76"), 0.3); p.telemetry (t);
        check (db > -8.0 && db < 8.0 && t[0] > 2.0f && bypassExact (p, defaults ("pacific76")),
               "Pacific 76: default level, gain-reduction meter reads " + juce::String (t[0], 1) + " dB, off = untouched");
    }

    {
        Silver76 p; auto v = defaults ("silver76");
        const double single = range (p, v);
        set (v, "silver76", "mode", 0.5f); const double dual = range (p, v);
        auto a = defaults ("silver76"); const double clean = thd (p, a, 0.3);
        set (a, "silver76", "ratio", 1.0f); const double all = thd (p, a, 0.3);
        const double db = gainDb (p, defaults ("silver76"), 0.3);
        std::cout << "    Silver 76: 24 dB in -> " << juce::String (single, 1) << " dB (single) / " << juce::String (dual, 1)
                  << " dB (dual); THD 4:1 " << juce::String (clean, 2) << "%, ALL " << juce::String (all, 1) << "%; level "
                  << juce::String (db, 1) << " dB\n";
        check (single < 16.0 && dual < single, "Silver 76: compresses; Dual compresses deeper");
        check (all > clean * 3.0, "Silver 76: all buttons in is grittier");
        auto m = defaults ("silver76"); set (m, "silver76", "mix", 0.0f);
        check (std::abs (gainDb (p, m, 0.3)) < 0.5, "Silver 76: Mix 0 = dry");
        check (db > -8.0 && db < 8.0 && bypassExact (p, defaults ("silver76")), "Silver 76: default level, off = untouched");
    }

    {
        FreshSqueeze p; auto v = defaults ("freshSqueeze");
        set (v, "freshSqueeze", "blend", 1.0f);
        set (v, "freshSqueeze", "comp", 0.2f); const double light = range (p, v);
        set (v, "freshSqueeze", "comp", 0.9f); const double heavy = range (p, v);
        auto d = defaults ("freshSqueeze"); set (d, "freshSqueeze", "blend", 0.0f);
        const double dryDb = gainDb (p, d, 0.3);
        auto c = defaults ("freshSqueeze"); const double cleanThd = thd (p, c, 0.3);
        set (c, "freshSqueeze", "dirt", 1.0f); const double dirtThd = thd (p, c, 0.3);
        const double db = gainDb (p, defaults ("freshSqueeze"), 0.3);
        std::cout << "    Fresh Squeeze: 24 dB in -> " << juce::String (light, 1) << " dB (comp 2) / " << juce::String (heavy, 1)
                  << " dB (comp 9); THD clean " << juce::String (cleanThd, 2) << "%, dirt " << juce::String (dirtThd, 1)
                  << "%; level " << juce::String (db, 1) << " dB\n";
        check (heavy < light && heavy < 14.0, "Fresh Squeeze: more Comp = more squeeze");
        check (std::abs (dryDb) < 0.5, "Fresh Squeeze: Blend fully left = your dry tone");
        check (dirtThd > cleanThd * 3.0, "Fresh Squeeze: Dirt adds overdrive");
        check (db > -8.0 && db < 8.0 && bypassExact (p, defaults ("freshSqueeze")), "Fresh Squeeze: default level, off = untouched");
    }

    {
        // Tuner: guitar-like notes (fundamental + harmonics), read within 2 cents.
        auto read = [] (double hz)
        {
            TrueTone t; const double fs = 48000.0; t.prepare (fs, 256);
            const int n = (int) (fs * 0.6);
            std::vector<float> buf ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double ph = 2.0 * pi * hz * i / fs;
                buf[(size_t) i] = (float) (0.2 * std::sin (ph) + 0.15 * std::sin (2 * ph + 0.3) + 0.08 * std::sin (3 * ph + 1.0) + 0.04 * std::sin (4 * ph));
            }
            float peakOut = 0.0f;
            for (int i = 0; i < n; i += 256)
            {
                float* ch[1] = { buf.data() + i };
                t.process (ch, 1, std::min (256, n - i), nullptr, true, {});
                if (i > (int) (0.05 * fs)) for (int k = 0; k < std::min (256, n - i); ++k) peakOut = std::max (peakOut, std::abs (ch[0][k]));
            }
            return std::make_pair (t.readingHz(), peakOut);
        };
        double worst = 0.0; bool muted = true;
        for (double hz : { 73.42, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 659.26 })
        {
            const auto [r, peak] = read (hz);
            worst = std::max (worst, r > 0 ? std::abs (1200.0 * std::log2 (r / hz)) : 1000.0);
            muted = muted && peak < 1.0e-6f;
        }
        const auto [sharp, unused] = read (110.0 * std::pow (2.0, 15.0 / 1200.0));
        juce::ignoreUnused (unused);
        const double sharpCents = 1200.0 * std::log2 (sharp / 110.0);
        std::cout << "    True Tone: worst error D2..E5 " << juce::String (worst, 2) << " cents; A +15 cents reads +"
                  << juce::String (sharpCents, 1) << "\n";
        check (worst < 1.0 && std::abs (sharpCents - 15.0) < 1.0, "True Tone: reads every string within 1 cent");
        check (muted, "True Tone: output muted while tuning");
        TrueTone t; std::vector<float> v (4, 0.0f);
        check (bypassExact (t, v), "True Tone: off = signal untouched");
    }

    {
        Sculptor p; auto v = defaults ("sculptor");
        const double flat1k = gainDb (p, v, 0.1, 1000.0), flat100 = gainDb (p, v, 0.1, 100.0);
        set (v, "sculptor", "b800", 1.0f);
        const double boost = gainDb (p, v, 0.1, 800.0), off = gainDb (p, v, 0.1, 100.0);
        auto l = defaults ("sculptor"); set (l, "sculptor", "level", 0.0f);
        const double lvl = gainDb (p, l, 0.1, 1000.0);
        std::cout << "    Sculptor: flat " << juce::String (flat1k, 2) << " / " << juce::String (flat100, 2) << " dB; 800 Hz +15 -> "
                  << juce::String (boost, 1) << " dB (100 Hz " << juce::String (off, 1) << "); level min " << juce::String (lvl, 1) << " dB\n";
        check (std::abs (flat1k) < 0.1 && std::abs (flat100) < 0.1, "Sculptor: centred sliders are flat");
        check (std::abs (boost - 15.0) < 1.5 && std::abs (off) < 1.5, "Sculptor: bands boost +15 dB where they should");
        check (std::abs (lvl + 15.0) < 0.2 && bypassExact (p, defaults ("sculptor")), "Sculptor: Level -15 dB, off = untouched");
    }

    // --- Chain ------------------------------------------------------------------
    std::cout << "\nPedal chain\n";
    {
        auto run = [] (bool withAfterglow, bool drivesOn)
        {
            board::BoardModel b;
            b.place (board::indexOf ("hotRod"), 9);
            b.place (board::indexOf ("goldenHour"), 8);
            if (withAfterglow) b.place (board::indexOf ("afterglow"), 7);
            b.setOn (board::indexOf ("hotRod"), drivesOn);
            b.setOn (board::indexOf ("goldenHour"), drivesOn);
            Engine e; e.prepare (48000.0, 256); e.setBoard (b); e.setTempo (120.0);
            const int n = 48000;
            juce::AudioBuffer<float> io (2, n); io.clear();
            juce::Random rng (5);
            for (int i = 0; i < n; ++i) io.setSample (0, i, (i % 9600) < 2400 ? 0.3f * std::sin ((float) i * 0.06f) * (1.0f - (float) (i % 9600) / 2400.0f) : 0.0f);
            for (int i = 0; i < n; i += 256)
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, i, std::min (256, n - i));
                e.process (view, 1, false);
            }
            bool same = true; bool finite = true;
            for (int i = 0; i < n; ++i)
            {
                same = same && io.getSample (0, i) == io.getSample (1, i);
                finite = finite && std::isfinite (io.getSample (0, i)) && std::isfinite (io.getSample (1, i));
            }
            return std::make_pair (same, finite);
        };
        const auto drivesOnly = run (false, true);
        const auto withEcho = run (true, true);
        check (drivesOnly.first && drivesOnly.second, "drives only: chain stays mono, both amps identical");
        check (! withEcho.first && withEcho.second, "Afterglow on the board: stereo from there on, L and R differ");
    }
}

} // namespace pedaltests
