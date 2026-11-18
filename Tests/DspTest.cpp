// Headless checks for the StompBox DSP. Runs in CI after every build.
#include <juce_events/juce_events.h>
//
//   StompBoxDspTest            run all checks, exit code 0 = pass
//   StompBoxDspTest <dir>      also render demo WAVs into <dir>

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/Engine.h"
#include "dsp/ToneStack.h"
#include "BoardTests.h"
#include "PedalTests.h"

using namespace stompbox;

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [pass] " : "  [FAIL] ") << what << "\n";
    if (! ok) ++failures;
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static double rmsDb (const float* d, int n)
{
    double s = 0; for (int i = 0; i < n; ++i) s += (double) d[i] * d[i];
    return 10.0 * std::log10 (s / juce::jmax (1, n) + 1e-20);
}

static double goertzel (const std::vector<double>& x, double f, double fs)
{
    const double w = 2.0 * dsp::pi * f / fs, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (double v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) x.size();
}

/** Plucked-string guitar-ish DI signal (Karplus-Strong), peak about `peak`. */
static juce::AudioBuffer<float> makeGuitar (double fs, double seconds, float peak)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (1, n);
    b.clear();
    juce::Random rng (1234);
    const double notes[] = { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6 };   // open E chord-ish strum
    for (int strum = 0; strum < (int) seconds; ++strum)
    {
        for (int s = 0; s < 6; ++s)
        {
            const int start = (int) ((strum + s * 0.012) * fs);
            const int period = (int) (fs / notes[s]);
            std::vector<float> line ((size_t) period);
            for (auto& v : line) v = rng.nextFloat() * 2.0f - 1.0f;
            float prev = 0;
            for (int i = start, k = 0; i < n && i < start + (int) fs; ++i, ++k)
            {
                const float cur = line[(size_t) (k % period)];
                const float next = 0.4985f * (cur + prev);
                prev = cur;
                line[(size_t) (k % period)] = next;
                b.addSample (0, i, cur * 0.25f);
            }
        }
    }
    b.applyGain (peak / juce::jmax (1e-9f, b.getMagnitude (0, 0, n)));
    return b;
}

/** Synthetic stand-in for a 2x12 cab IR: band-limited, decaying noise. */
static IRData makeFakeIR (double fs)
{
    IRData ir;
    ir.name = "Test cab";
    ir.sampleRate = fs;
    const int n = (int) (0.2 * fs);
    ir.samples.setSize (1, n);
    juce::Random rng (99);
    dsp::Biquad hp, lp; hp.setHighpass (90, 0.7, fs); lp.setLowpass (4500, 0.7, fs);
    for (int i = 0; i < n; ++i)
    {
        const double env = std::exp (-i / (0.004 * fs));
        ir.samples.setSample (0, i, (float) lp.process (hp.process ((rng.nextFloat() * 2 - 1) * env)));
    }
    return ir;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    const int block = 128;

    //==========================================================================
    std::cout << "Tone stack (AC30 Top Boost values)\n";
    {
        dsp::ToneStack ts; ts.prepare (192000.0);
        bool ok = true;
        const double freqs[] = { 60, 150, 400, 1000, 2500, 6000 };
        for (double t : { 0.0, 0.1, 0.5, 1.0 })
            for (double l : { 0.0, 0.1, 0.5, 1.0 })
            {
                ts.setControls (t, l);
                juce::String line = "    t=" + juce::String (t, 1) + " l=" + juce::String (l, 1) + ":";
                for (double f : freqs)
                {
                    const double m = ts.magnitudeAt (f);
                    ok &= std::isfinite (m) && m < 1.05;
                    line << " " << juce::String (20 * std::log10 (m + 1e-12), 1);
                }
                std::cout << line << "  (dB @ 60/150/400/1k/2.5k/6k)\n";
            }
        check (ok, "passive stack never boosts and stays finite");
        ts.setControls (1.0, 0.0); const double hiT = ts.magnitudeAt (4000) / ts.magnitudeAt (100);
        ts.setControls (0.0, 0.0); const double loT = ts.magnitudeAt (4000) / ts.magnitudeAt (100);
        check (hiT > loT * 3.0, "Treble raises highs relative to lows");
        ts.setControls (0.5, 1.0); const double hiB = ts.magnitudeAt (100) / ts.magnitudeAt (1000);
        ts.setControls (0.5, 0.0); const double loB = ts.magnitudeAt (100) / ts.magnitudeAt (1000);
        check (hiB > loB * 2.0, "Bass raises lows relative to mids");
    }

    //==========================================================================
    std::cout << "\nGain staging (single amp, 220 Hz sine, no cab)\n";
    {
        const double ofs = 192000.0;
        struct Case { const char* name; bool tb; float vol, master; };
        const Case cases[] = {
            { "Normal  vol 5 ", false, 0.5f, 0.5f }, { "Normal  vol 10", false, 1.0f, 0.5f },
            { "TopBst  vol 3 ", true, 0.3f, 0.5f },  { "TopBst  vol 5 ", true, 0.5f, 0.5f },
            { "TopBst  vol 7 ", true, 0.7f, 0.5f },  { "TopBst  vol 10", true, 1.0f, 0.5f },
            { "TopBst  vol 5 M10", true, 0.5f, 1.0f },
        };
        double prevThd = -1; bool monotonic = true;
        for (const auto& cs : cases)
        {
            juce::String line; line << "    " << cs.name << "  THD% @ in 0.05/0.15/0.3/0.6 V:";
            for (double amp : { 0.05, 0.15, 0.3, 0.6 })
            {
                dsp::Royal30 r; r.prepare (ofs);
                dsp::Royal30Settings s; s.topBoost = cs.tb; s.volume = cs.vol; s.master = cs.master;
                s.treble = 0.6f; s.bass = 0.5f; s.cut = 0.0f;
                r.setSettings (s);
                const int n = (int) (ofs * 0.6);
                std::vector<float> buf ((size_t) n);
                for (int i = 0; i < n; ++i) buf[(size_t) i] = (float) (amp * std::sin (2 * dsp::pi * 220.0 * i / ofs));
                float* ch[1] = { buf.data() };
                for (int i = 0; i < n; i += 512) { float* p[1] = { ch[0] + i }; r.process (p, 1, std::min (512, n - i)); }
                std::vector<double> tail (buf.begin() + n / 2, buf.begin() + n / 2 + (int) (ofs / 220.0) * 40);
                const double f1 = goertzel (tail, 220, ofs);
                double h = 0; for (int k = 2; k <= 12; ++k) { const double hk = goertzel (tail, 220.0 * k, ofs); h += hk * hk; }
                const double thd = 100.0 * std::sqrt (h) / (f1 + 1e-12);
                double pk = 0; for (double v : tail) pk = std::max (pk, std::abs (v));
                line << "  " << juce::String (thd, 1) << " (" << juce::String (20 * std::log10 (pk + 1e-12), 1) << "dB)";
                if (cs.tb && cs.master == 0.5f && amp == 0.3) { if (thd < prevThd) monotonic = false; prevThd = thd; }
            }
            std::cout << line << "\n";
        }
        check (monotonic, "Top Boost: more Volume = more distortion");
    }

    //==========================================================================
    std::cout << "\nCab IR\n";
    {
        IRData ir = makeFakeIR (44100.0);
        auto tree = ir.toValueTree();
        auto back = IRData::fromValueTree (tree);
        bool same = back.isValid() && back.samples.getNumSamples() == ir.samples.getNumSamples()
                    && back.sampleRate == ir.sampleRate && back.name == ir.name;
        for (int i = 0; same && i < ir.samples.getNumSamples(); ++i)
            same = back.samples.getSample (0, i) == ir.samples.getSample (0, i);
        check (same, "IR survives save/restore bit-exact (" + juce::String (tree.getProperty ("data").toString().length() / 1024) + " KB in state)");

        // Zero latency: a dirac IR must pass an impulse straight through.
        CabIR cab; cab.prepare ({ fs, (juce::uint32) block, 2 });
        IRData dirac; dirac.name = "dirac"; dirac.sampleRate = fs; dirac.samples.setSize (1, 64); dirac.samples.clear();
        dirac.samples.setSample (0, 0, 1.0f);
        cab.load (dirac);
        juce::Thread::sleep (300);   // background IR loader
        juce::AudioBuffer<float> b (2, block);
        int peakAt = -1;
        for (int rep = 0; rep < 40 && peakAt < 0; ++rep)
        {
            b.clear(); b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
            cab.process (juce::dsp::AudioBlock<float> (b));
            float best = 0.1f;
            for (int i = 0; i < block; ++i) if (std::abs (b.getSample (0, i)) > best) { best = std::abs (b.getSample (0, i)); peakAt = i; }
            if (peakAt < 0) juce::Thread::sleep (20);
        }
        check (peakAt == 0 && cab.getLatency() == 0, "convolution adds zero latency (impulse out at sample " + juce::String (peakAt) + ")");
    }

    //==========================================================================
    std::cout << "\nDual cab (Link L/R off)\n";
    {
        // Two "cabs": the same burst, but B starts 60 samples later and is
        // inverted. Unaligned, they'd largely cancel when summed to mono.
        auto burst = [&] (int offset, float polarity)
        {
            IRData ir; ir.name = "b"; ir.sampleRate = fs; ir.samples.setSize (1, 4800); ir.samples.clear();
            juce::Random rng (7);
            dsp::Biquad lp; lp.setLowpass (5000, 0.7, fs);
            for (int i = 0; i < 2400; ++i)
                ir.samples.setSample (0, offset + i, polarity * (float) lp.process ((rng.nextFloat() * 2 - 1) * std::exp (-i / 200.0)));
            return ir;
        };
        IRData a = burst (100, 1.0f), b = burst (160, -1.0f);
        auto monoEnergy = [] (const IRData& x, const IRData& y)
        {
            double e = 0; const int n = juce::jmin (x.samples.getNumSamples(), y.samples.getNumSamples());
            for (int i = 0; i < n; ++i) { const double s = x.samples.getSample (0, i) + y.samples.getSample (0, i); e += s * s; }
            return e;
        };
        const double before = monoEnergy (a, b);
        CabIR::alignPair (a, b);
        const double after = monoEnergy (a, b);
        double single = 0; for (int i = 0; i < a.samples.getNumSamples(); ++i) single += (double) a.samples.getSample (0, i) * a.samples.getSample (0, i);
        check (after > 0.97 * 4.0 * single && after > before * 1.8,
               "alignment: offset + inverted IRs line up exactly, so they sum to mono at full strength ("
               + juce::String (100.0 * after / (4.0 * single), 1) + "% of ideal)");

        // Through the cab: both sides answer at the same moment, and differ.
        CabIR cab; cab.prepare ({ fs, (juce::uint32) block, 2 });
        cab.load (burst (100, 1.0f), makeFakeIR (fs), false);
        juce::Thread::sleep (400);
        bool dual = cab.isDual();
        juce::AudioBuffer<float> io (2, block);
        int peakL = -1, peakR = -1; bool differ = false;
        for (int rep = 0; rep < 40 && (peakL < 0 || peakR < 0); ++rep)
        {
            io.clear(); io.setSample (0, 0, 1.0f); io.setSample (1, 0, 1.0f);
            cab.process (juce::dsp::AudioBlock<float> (io));
            float mL = 0.05f, mR = 0.05f;
            for (int i = 0; i < block; ++i)
            {
                if (std::abs (io.getSample (0, i)) > mL) { mL = std::abs (io.getSample (0, i)); peakL = i; }
                if (std::abs (io.getSample (1, i)) > mR) { mR = std::abs (io.getSample (1, i)); peakR = i; }
                differ = differ || io.getSample (0, i) != io.getSample (1, i);
            }
            if (peakL < 0 || peakR < 0) juce::Thread::sleep (20);
        }
        check (dual && differ, "unlinked: left and right amps get their own IR");
        check (peakL >= 0 && peakR >= 0 && std::abs (peakL - peakR) <= 2 && peakL < 24,
               "unlinked: both sides answer together, no added latency (peaks at " + juce::String (peakL) + " / " + juce::String (peakR) + ")");

        CabIR linked; linked.prepare ({ fs, (juce::uint32) block, 2 });
        linked.load (burst (100, 1.0f), makeFakeIR (fs), true);
        check (! linked.isDual(), "linked: one IR on both sides (the right IR is kept but not used)");
    }

    //==========================================================================
    std::cout << "\nFull engine\n";
    {
        auto guitar = makeGuitar (fs, 6.0, 0.35f);
        const int n = guitar.getNumSamples();

        auto run = [&] (Engine& e, juce::AudioBuffer<float>& io, int inCh)
        {
            for (int i = 0; i < n; i += block)
            {
                const int len = std::min (block, n - i);
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, i, len);
                e.process (view, inCh, false);
            }
        };

        for (auto q : { Engine::Quality::live, Engine::Quality::studio })
        {
            Engine e; e.setQuality (q); e.prepare (fs, block);
            e.getCab().load (makeFakeIR (fs));
            juce::Thread::sleep (300);
            dsp::Royal30Settings s;   // defaults
            e.setAmpSettings (s);

            juce::AudioBuffer<float> io (2, n);
            io.copyFrom (0, 0, guitar, 0, 0, n); io.clear (1, 0, n);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            run (e, io, 1);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const char* qn = q == Engine::Quality::live ? "Live" : "Studio";

            check (allFinite (io), juce::String (qn) + ": output finite");
            bool lrSame = true;
            for (int i = 0; i < n && lrSame; ++i) lrSame = io.getSample (0, i) == io.getSample (1, i);
            check (lrSame, juce::String (qn) + ": mono in -> identical L and R amps");
            const double rms = rmsDb (io.getReadPointer (0), n);
            const float pk = io.getMagnitude (0, 0, n);
            check (rms > -32 && rms < -8 && pk < 1.0f, juce::String (qn) + ": default level " + juce::String (rms, 1)
                   + " dBFS RMS, peak " + juce::String (juce::Decibels::gainToDecibels (pk), 1) + " dBFS");

            // Stereo: CPU with both amps running.
            juce::AudioBuffer<float> st (2, n);
            st.copyFrom (0, 0, guitar, 0, 0, n);
            st.copyFrom (1, 0, guitar, 0, 0, n); st.applyGain (1, 0, n, 0.7f);
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            run (e, st, 2);
            const double ms2 = juce::Time::getMillisecondCounterHiRes() - t1;
            check (allFinite (st), juce::String (qn) + ": stereo output finite");
            std::cout << "    " << qn << " CPU: mono " << juce::String (100.0 * ms / 6000.0, 2)
                      << "% of one core, stereo input " << juce::String (100.0 * ms2 / 6000.0, 2) << "%\n";
        }

        // Stereo track with the guitar on one side only (Input 1-2, guitar in 2)
        {
            Engine em; em.prepare (fs, block);
            Engine es; es.prepare (fs, block);
            juce::AudioBuffer<float> mono (2, n), rightOnly (2, n);
            mono.copyFrom (0, 0, guitar, 0, 0, n); mono.clear (1, 0, n);
            rightOnly.clear (0, 0, n); rightOnly.copyFrom (1, 0, guitar, 0, 0, n);
            run (em, mono, 1);
            run (es, rightOnly, 2);
            bool lr = true;
            for (int i = 0; i < n && lr; ++i) lr = rightOnly.getSample (0, i) == rightOnly.getSample (1, i);
            const double dm = rmsDb (mono.getReadPointer (0), n), ds = rmsDb (rightOnly.getReadPointer (0), n);
            check (lr && std::abs (dm - ds) < 0.5, "guitar on right input only -> centred on both amps, same level ("
                   + juce::String (ds - dm, 2) + " dB)");
        }

        // Link off with two different IRs: a mono chain comes out in stereo.
        {
            Engine e; e.prepare (fs, block);
            IRData other = makeFakeIR (fs); other.samples.applyGain (-1.0f); other.samples.setSample (0, 40, 0.5f);
            e.getCab().load (makeFakeIR (fs), other, false);
            juce::Thread::sleep (400);
            juce::AudioBuffer<float> io (2, n); io.copyFrom (0, 0, guitar, 0, 0, n); io.clear (1, 0, n);
            run (e, io, 1);
            bool differ = false;
            for (int i = n / 2; i < n && ! differ; ++i) differ = io.getSample (0, i) != io.getSample (1, i);
            check (differ && allFinite (io), "Link off: one mono guitar, a different cab on each side");
        }

        // Mono in -> mono out (Logic's "Mono" format on a mono track)
        {
            Engine e; e.prepare (fs, block);
            juce::AudioBuffer<float> m (1, n);
            m.copyFrom (0, 0, guitar, 0, 0, n);
            for (int i = 0; i < n; i += block)
            {
                juce::AudioBuffer<float> view (m.getArrayOfWritePointers(), 1, i, std::min (block, n - i));
                e.process (view, 1, false);
            }
            check (allFinite (m) && m.getMagnitude (0, 0, n) > 0.01f, "mono in -> mono out works");
        }

        // Silence in -> silence out (no DC, no self-noise blowups)
        Engine e; e.prepare (fs, block);
        juce::AudioBuffer<float> z (2, n); z.clear();
        run (e, z, 2);
        check (z.getMagnitude (0, n - 4800, 4800) < 1.0e-4f, "silence in -> silence out");
    }

    //==========================================================================
    boardtests::run ([] (bool ok, const juce::String& what) { check (ok, what); });
    pedaltests::run ([] (bool ok, const juce::String& what) { check (ok, what); });

    //==========================================================================
    if (argc > 1)
    {
        const juce::File dir { juce::String (argv[1]) };
        dir.createDirectory();
        auto guitar = makeGuitar (fs, 4.0, 0.35f);
        struct R { const char* name; bool tb; float vol, master, treble, bass, cut; };
        const R renders[] = {
            { "normal_clean", false, 0.6f, 0.6f, 0.6f, 0.5f, 0.2f },
            { "topboost_edge", true, 0.45f, 0.6f, 0.6f, 0.4f, 0.3f },
            { "topboost_crunch", true, 0.7f, 0.6f, 0.6f, 0.4f, 0.3f },
            { "topboost_full", true, 1.0f, 0.8f, 0.6f, 0.4f, 0.4f },
        };
        juce::WavAudioFormat wav;
        for (const auto& r : renders)
        {
            Engine e; e.prepare (fs, block);
            e.getCab().load (makeFakeIR (fs)); juce::Thread::sleep (300);
            dsp::Royal30Settings s; s.topBoost = r.tb; s.volume = r.vol; s.master = r.master;
            s.treble = r.treble; s.bass = r.bass; s.cut = r.cut;
            e.setAmpSettings (s);
            const int n = guitar.getNumSamples();
            juce::AudioBuffer<float> io (2, n); io.copyFrom (0, 0, guitar, 0, 0, n); io.clear (1, 0, n);
            for (int i = 0; i < n; i += block)
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, i, std::min (block, n - i));
                e.process (view, 1, true);
            }
            auto f = dir.getChildFile (juce::String (r.name) + ".wav");
            f.deleteFile();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), fs, 2, 24, {}, 0));
            if (w) w->writeFromAudioSampleBuffer (io, 0, n);
            std::cout << "    rendered " << f.getFullPathName() << "  (" << juce::String (rmsDb (io.getReadPointer (0), n), 1) << " dBFS RMS, peak " << juce::String (juce::Decibels::gainToDecibels (io.getMagnitude (0, 0, n)), 1) << " dBFS)\n";
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : juce::String (failures) + " CHECK(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
