#include "PedalCatalog.h"
#include <cstring>

namespace stompbox::board
{

using K = ControlDef::Kind;
using D = PedalDef::Decor;

static constexpr std::uint32_t cream = 0xffefe9dc, dark = 0xff161618, light = 0xfff4f1ea, silver = 0xffd9dcdf;

static std::vector<ControlDef> threeKnobBoss (const char* a, const char* b, const char* c,
                                              float da = 0.5f, float db = 0.5f, float dc = 0.5f)
{
    // Two large knobs on top, the smaller one centred below (mockup layout).
    return { { a, a, K::knob, 25.0f, 22.0f, 28.0f, da },
             { b, b, K::knob, 87.0f, 22.0f, 28.0f, db },
             { c, c, K::knob, 56.0f, 48.0f, 20.0f, dc } };
}

static std::vector<ControlDef> bigDigital (const char* const top[4], const char* const bottom[4])
{
    std::vector<ControlDef> c;
    c.push_back ({ "type", "TYPE", K::knob, 54.0f, 55.0f, 26.0f, 0.0f });
    const float topX[4] = { 115.0f, 144.0f, 172.0f, 200.0f };
    for (int i = 0; i < 4; ++i)
        c.push_back ({ top[i], top[i], K::knob, topX[i], i == 0 ? 27.0f : 26.0f, i == 0 ? 24.0f : 22.0f, 0.5f });
    const float botX[4] = { 130.0f, 158.0f, 186.0f, 214.0f };
    for (int i = 0; i < 4; ++i)
        c.push_back ({ bottom[i], bottom[i], K::knob, botX[i], 58.0f, 22.0f, 0.5f });
    return c;
}

static std::vector<PedalDef> build()
{
    std::vector<PedalDef> v;

    // --- Row 1 of the shelf: overdrives ---------------------------------------
    v.push_back ({ "goldenHour", "GOLDEN HOUR", "OVERDRIVE", "JHS Morning Glory V4",
                   0xffb69754, 0xff1e1e1e, 1, false, 1, 84.0f, 11.0f, D::none, "", 0, 0,
                   { { "volume", "VOLUME", K::knob, 24.0f, 22.0f, 24.0f, 0.45f },
                     { "drive",  "DRIVE",  K::knob, 88.0f, 22.0f, 24.0f, 0.45f },
                     { "gain",   "GAIN",   K::toggle, 24.0f, 55.0f, 14.0f, 1.0f, 2, dark, light, "HI|LO" },
                     { "tone",   "TONE",   K::knob, 88.0f, 58.0f, 24.0f, 0.35f },
                     { "hicut",  "HI-CUT", K::toggle, 56.0f, 58.0f, 11.0f, 1.0f, 2, dark, light, "ON|OFF" } } });

    v.push_back ({ "scotland", "SCOTLAND", "OVERDRIVE", "JHS Kilt V2",
                   0xffbfc1c3, 0xff1e1e1e, 1, false, 1, 90.0f, 12.0f, D::none, "", 0, 0,
                   { { "volume",  "VOLUME",   K::knob, 22.0f, 26.0f, 22.0f, 0.5f },
                     { "gain",    "GAIN",     K::knob, 90.0f, 26.0f, 22.0f, 0.4f },
                     { "tone",    "TONE",     K::knob, 22.0f, 64.0f, 22.0f, 0.5f },
                     { "g1",      "G1",       K::toggle, 50.0f, 30.0f, 12.0f, 1.0f, 2, dark, light, "ON|OFF" },
                     { "g2",      "G2",       K::toggle, 61.0f, 47.0f, 12.0f, 1.0f, 2, dark, light, "ON|OFF" },
                     { "cutflat", "CUT/FLAT", K::toggle, 72.0f, 64.0f, 12.0f, 1.0f, 2, dark, light, "CUT|FLAT" } } });

    v.push_back ({ "blueHighway", "BLUE\nHIGHWAY", "OVERDRIVE", "Boss BD-2 Blues Driver",
                   0xff246a9b, light, 1, false, 1, 82.0f, 12.0f, D::none, "", 0, 0,
                   threeKnobBoss ("LEVEL", "GAIN", "TONE", 0.5f, 0.3f, 0.5f) });

    v.push_back ({ "hotRod", "HOT\nROD", "OVERDRIVE", "Boss SD-1 Super OverDrive",
                   0xffebc45b, 0xff1e1e1e, 1, false, 1, 82.0f, 12.0f, D::none, "", 0, 0,
                   threeKnobBoss ("LEVEL", "DRIVE", "TONE") });

    // --- Row 2: compressors and the dual overdrive ------------------------------
    v.push_back ({ "freshSqueeze", "FRESH\nSQUEEZE", "COMPRESSOR", "JHS Pulp 'N' Peel V4",
                   0xffef7822, 0xff1e1e1e, 1, false, 1, 88.0f, 12.0f, D::none, "", 0, 4,
                   { { "volume", "VOLUME", K::knob, 26.0f, 26.0f, 20.0f, 0.5f, 1, cream, dark },
                     { "comp",   "COMP",   K::knob, 86.0f, 26.0f, 20.0f, 0.5f, 1, cream, dark },
                     { "eq",     "EQ",     K::knob, 26.0f, 62.0f, 20.0f, 0.5f, 1, cream, dark },
                     { "blend",  "BLEND",  K::knob, 86.0f, 62.0f, 20.0f, 0.7f, 1, cream, dark },
                     { "dirt",   "DIRT",   K::toggle, 56.0f, 29.0f, 12.0f, 0.0f, 2, dark, light, "OFF|ON" } } });

    v.push_back ({ "pacific76", "PACIFIC 76", "COMPRESSOR", "Origin Effects Cali76",
                   0xffb8b7b0, 0xff1e1e1e, 1, false, 1, 84.0f, 11.0f, D::meter, "", 0xff8a7a4a, 4,
                   { { "in",      "IN",      K::knob, 30.0f, 20.0f, 20.0f, 0.5f },
                     { "out",     "OUT",     K::knob, 56.0f, 20.0f, 20.0f, 0.5f },
                     { "dry",     "DRY",     K::knob, 82.0f, 20.0f, 20.0f, 0.0f },
                     { "attack",  "ATTACK",  K::knob, 30.0f, 46.0f, 20.0f, 0.5f },
                     { "release", "RELEASE", K::knob, 56.0f, 46.0f, 20.0f, 0.5f },
                     { "ratio",   "RATIO",   K::knob, 82.0f, 46.0f, 20.0f, 0.3f } } });

    {
        // Left half = Blue (Bluesbreaker style), right half = Green (Nobels ODR-1
        // style). Signal runs right to left, so Green feeds Blue, as on the real pedal.
        PedalDef p { "nutrient", "NUTRIENT", "OVERDRIVE", "Browne Protein Dual Overdrive V4",
                     0xff626f4b, 0xfff1ede5, 2, false, 2, 86.0f, 13.0f, D::none, "", 0, 0, {} };
        p.controls = { { "volA",  "VOLUME", K::knob, 38.0f,  26.0f, 18.0f, 0.5f, 1, silver, dark },
                       { "gainA", "GAIN",   K::knob, 94.0f,  26.0f, 18.0f, 0.4f, 1, silver, dark },
                       { "volB",  "LEVEL",  K::knob, 146.0f, 26.0f, 18.0f, 0.5f, 1, silver, dark },
                       { "gainB", "DRIVE",  K::knob, 202.0f, 26.0f, 18.0f, 0.4f, 1, silver, dark },
                       { "toneA", "TONE",   K::knob, 63.0f,  53.0f, 12.0f, 0.5f },
                       { "toneB", "TONE",   K::knob, 175.0f, 53.0f, 12.0f, 0.5f },
                       { "hicut", "HI-CUT", K::toggle, 29.0f, 55.0f, 12.0f, 0.0f, 3, dark, light, "OPEN|MID|DARK" } };
        v.push_back (p);
    }

    // --- Row 3: the big stereo digital pedals ------------------------------------
    {
        static const char* const top[4] = { "DECAY", "PRE-DELAY", "MIX", "TONE" };
        static const char* const bot[4] = { "PARAM 1", "PARAM 2", "MOD", "LEVEL" };
        v.push_back ({ "openSky", "OPEN SKY", "REVERB", "Strymon BigSky",
                       0xff27a8c8, 0xff0f2a33, 2, true, 1, 85.0f, 14.0f, D::display, "SHIMMER", 0xff7fe7ff, 5,
                       bigDigital (top, bot) });
    }
    {
        static const char* const top[4] = { "TIME", "REPEATS", "MIX", "FILTER" };
        static const char* const bot[4] = { "GRIT", "SPEED", "DEPTH", "TWEAK" };
        v.push_back ({ "timeMachine", "TIME MACHINE", "DELAY", "Strymon TimeLine",
                       0xff66686e, light, 2, true, 1, 85.0f, 14.0f, D::display, "D.8TH SYNC", 0xff9dff9a, 5,
                       bigDigital (top, bot) });
    }

    // --- Row 4: delay, utilities, 1176 -------------------------------------------
    v.push_back ({ "afterglow", "AFTERGLOW", "DELAY", "Keeley HALO Dual Echo",
                   0xff292a2c, light, 1, true, 1, 83.0f, 12.0f, D::none, "", 0, 0,
                   { { "tone",    "TONE",    K::knob, 23.0f, 20.0f, 24.0f, 0.4f, 1, cream, dark },
                     { "repeats", "FDBK",    K::knob, 89.0f, 20.0f, 24.0f, 0.40f, 1, cream, dark },
                     { "mix",     "LEVEL",   K::knob, 56.0f, 32.0f, 24.0f, 0.40f, 1, cream, dark },
                     { "rate",    "RATE",    K::knob, 23.0f, 56.0f, 24.0f, 0.25f, 1, cream, dark },
                     { "depth",   "DEPTH",   K::knob, 89.0f, 56.0f, 24.0f, 0.70f, 1, cream, dark } } });

    v.push_back ({ "trueTone", "TRUE\nTONE", "TUNER", "Boss TU-3",
                   0xfff0f0e9, 0xff1e1e1e, 1, false, 1, 76.0f, 12.0f, D::tuner, "E", 0xff7dff8a, 6, {} });

    {
        PedalDef p { "sculptor", "SCULPTOR", "EQUALIZER", "Boss GE-7 Equalizer",
                     0xffe5d5b8, 0xff1e1e1e, 1, false, 1, 65.0f, 12.0f, D::none, "", 0, 6, {} };
        static const char* const bands[8] = { "100", "200", "400", "800", "1.6K", "3.2K", "6.4K", "LEVEL" };
        static const char* const ids[8] = { "b100", "b200", "b400", "b800", "b1k6", "b3k2", "b6k4", "level" };
        const float xs[8] = { 12.0f, 24.0f, 36.0f, 48.0f, 60.0f, 72.0f, 84.0f, 100.0f };
        for (int i = 0; i < 8; ++i)
            p.controls.push_back ({ ids[i], bands[i], K::slider, xs[i], 31.0f, 34.0f, 0.5f });
        v.push_back (p);
    }

    v.push_back ({ "silver76", "SILVER 76", "COMPRESSOR", "Universal Audio UAFX 1176 Studio Compressor",
                   0xffb5bac0, 0xff1e1e1e, 1, false, 1, 91.0f, 11.0f, D::none, "", 0, 4,
                   { { "input",   "INPUT",   K::knob, 21.0f, 28.0f, 22.0f, 0.4f },
                     { "output",  "OUTPUT",  K::knob, 91.0f, 28.0f, 22.0f, 0.5f },
                     { "mode",    "MODE",    K::toggle, 56.0f, 30.0f, 12.0f, 0.0f, 3, dark, light, "SINGLE|DUAL|SUSTAIN" },
                     { "attack",  "ATTACK",  K::knob, 21.0f, 66.0f, 16.0f, 0.5f },
                     { "release", "RELEASE", K::knob, 56.0f, 66.0f, 16.0f, 0.5f },
                     { "mix",     "MIX",     K::knob, 91.0f, 66.0f, 16.0f, 1.0f } } });

    return v;
}

const std::vector<PedalDef>& catalog()
{
    static const std::vector<PedalDef> c = build();
    return c;
}

int indexOf (const char* id)
{
    const auto& c = catalog();
    for (size_t i = 0; i < c.size(); ++i)
        if (std::strcmp (c[i].id, id) == 0)
            return (int) i;
    return -1;
}

} // namespace stompbox::board
