#pragma once

// Pedal and chain checks, included from DspTest.cpp.

#include "dsp/pedals/DrivePedals.h"
#include "dsp/pedals/Afterglow.h"
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

        set (v, "afterglow", "rhythm", 0.5f);   // D.1/8, synced
        auto o = impulse (p, v, 1.0, 120.0);
        check (peakIn (o[0], 360.0, 400.0) > 0.1f, "Afterglow: the very first note gets its echo at full level");
        const double tL = peakTimeMs (o[0], 50.0, 900.0), tR = peakTimeMs (o[1], 50.0, 900.0);
        check (std::abs (tL - 375.0) < 2.0 && std::abs (tR - 375.0) < 2.0,
               "Afterglow D.1/8 at 120 BPM: echo at " + juce::String (tL, 1) + " ms (375 expected)");

        o = impulse (p, v, 1.0, 90.0);
        check (std::abs (peakTimeMs (o[0], 50.0, 900.0) - 500.0) < 2.0, "Afterglow follows Logic's tempo (D.1/8 at 90 BPM = 500 ms)");

        set (v, "afterglow", "rhythm", 1.0f);   // HALO
        o = impulse (p, v, 1.0, 120.0);
        const double hL = peakTimeMs (o[0], 50.0, 900.0), hR = peakTimeMs (o[1], 50.0, 900.0);
        check (std::abs (hL - 375.0) < 2.0 && std::abs (hR - 500.0) < 2.0,
               "Afterglow HALO: dotted eighth leans left (" + juce::String (hL, 0) + " ms), quarter leans right ("
               + juce::String (hR, 0) + " ms)");

        set (v, "afterglow", "rhythm", 0.0f);   // 1/4, FREE
        set (v, "afterglow", "sync", 1.0f);
        set (v, "afterglow", "time", 0.5f);
        o = impulse (p, v, 1.0, 120.0);
        const double expected = 60.0 * std::sqrt (25.0);
        check (std::abs (peakTimeMs (o[0], 50.0, 900.0) - expected) < 2.0, "Afterglow FREE: TIME sets milliseconds (" + juce::String (expected, 0) + " ms)");

        // Trails: switch off right after the note; the echo still comes.
        set (v, "afterglow", "sync", 0.0f);
        set (v, "afterglow", "rhythm", 0.5f);
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
