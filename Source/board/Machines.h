#pragma once

#include <algorithm>
#include <cmath>

// The machines (types) of the two big digital pedals, Open Sky (reverb) and
// Time Machine (delay): names, what their machine-specific knobs do, and the
// knob-to-value mappings. Shared by the DSP and the UI so the display and
// close-up always read exactly what the sound is doing.

namespace stompbox::board::machines
{

/** A knob's meaning on one machine. steps = "A|B|C" for a stepped choice
    (the knob is split evenly between them), "" for a continuous knob. */
struct KnobRole
{
    const char* label;
    const char* steps = "";
};

inline int stepIndex (float v, int count) noexcept
{
    return std::clamp ((int) std::lround (v * (float) (count - 1)), 0, count - 1);
}

inline int countSteps (const char* steps) noexcept
{
    if (steps == nullptr || *steps == 0) return 0;
    int n = 1;
    for (const char* p = steps; *p; ++p) n += *p == '|';
    return n;
}

//==============================================================================
// Open Sky (BigSky): the worship core seven.
enum Reverb { room, hall, plate, swell, bloom, cloud, shimmer, numReverbs };

inline constexpr const char* reverbNames[numReverbs] = { "ROOM", "HALL", "PLATE", "SWELL", "BLOOM", "CLOUD", "SHIMMER" };
inline constexpr const char* reverbSteps = "ROOM|HALL|PLATE|SWELL|BLOOM|CLOUD|SHIMMER";

/** Interval choices shared by Shimmer's SHIFT and Ice's INTERVAL (octave up in the middle). */
inline constexpr const char* intervalSteps = "-OCT|+4TH|+5TH|+OCT|+OCT+4|+OCT+5|+2OCT";
inline constexpr int intervalSemis[7] = { -12, 5, 7, 12, 17, 19, 24 };

/** PARAM 1 and PARAM 2 per reverb machine (the BigSky's most useful menu
    parameters for each, fixed so the knobs always mean the same thing). */
inline constexpr KnobRole reverbParams[numReverbs][2] = {
    { { "LOW END" }, { "DIFFUSION" } },                          // Room
    { { "LOW END" }, { "SIZE", "CONCERT|ARENA" } },              // Hall
    { { "LOW END" }, { "SIZE", "SMALL|LARGE" } },                // Plate
    { { "RISE" },    { "LOW END" } },                            // Swell
    { { "LENGTH" },  { "FEEDBACK" } },                           // Bloom
    { { "DIFFUSION" }, { "LOW END" } },                          // Cloud
    { { "AMOUNT" },  { "SHIFT", intervalSteps } },               // Shimmer
};

/** Decay (RT60) range in seconds, per machine, as on the BigSky. */
inline void decayRange (int machine, double& lo, double& hi) noexcept
{
    lo = machine == room ? 0.2 : machine == cloud ? 1.0 : 0.5;
    hi = machine == cloud ? 50.0 : 20.0;
}

inline double decaySeconds (int machine, float v) noexcept
{
    double lo, hi; decayRange (machine, lo, hi);
    return lo * std::pow (hi / lo, (double) v);
}

/** Pre-delay 0 .. 1.5 s, finer at the short end. */
inline double preDelaySeconds (float v) noexcept { return 1.5 * std::pow ((double) v, 3.0); }

/** Swell rise time 0.08 .. 4 s. */
inline double riseSeconds (float v) noexcept { return 0.08 * std::pow (50.0, (double) v); }

//==============================================================================
// Time Machine (TimeLine): the worship core seven.
enum Delay { digital, dTape, dBucket, dual, ice, dSwell, duck, numDelays };

inline constexpr const char* delayNames[numDelays] = { "DIGITAL", "dTAPE", "dBUCKET", "DUAL", "ICE", "SWELL", "DUCK" };
inline constexpr const char* delaySteps = "DIGITAL|dTAPE|dBUCKET|DUAL|ICE|SWELL|DUCK";

/** Ratios of Dual's second delay to the first. 4:3 in the middle: a dotted
    eighth on one side and a quarter on the other. */
inline constexpr const char* dualSteps = "1:4|1:3|1:2|2:3|4:3|3:2|2:1|3:1|4:1";
inline constexpr double dualRatios[9] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 4.0 / 3.0, 1.5, 2.0, 3.0, 4.0 };

/** FILTER, GRIT, SPEED, DEPTH and TWEAK per delay machine (the TimeLine's
    knob functions, plus its key menu parameter on TWEAK). */
inline constexpr KnobRole delayKnobs[numDelays][5] = {
    { { "FILTER" },   { "GRIT" },      { "SPEED" },   { "DEPTH" },         { "SMEAR" } },                    // Digital
    { { "TAPE AGE" }, { "TAPE BIAS" }, { "CRINKLE" }, { "WOW/FLUTTER" },   { "LOW END" } },                  // dTape
    { { "FILTER" },   { "BUCKET LOSS" }, { "SPEED" }, { "DEPTH" },         { "RANGE", "SINGLE|DOUBLE" } },   // dBucket
    { { "FILTER" },   { "GRIT" },      { "SPEED" },   { "DEPTH" },         { "TIME 2", dualSteps } },        // Dual
    { { "FILTER" },   { "GRIT" },      { "SPEED" },   { "DEPTH" },         { "INTERVAL", intervalSteps } },  // Ice
    { { "FILTER" },   { "GRIT" },      { "SPEED" },   { "DEPTH" },         { "RISE" } },                     // Swell
    { { "FILTER" },   { "GRIT" },      { "SPEED" },   { "DEPTH" },         { "SENSITIVITY" } },              // Duck
};

/** Free-running delay time range (ms), per machine, as on the TimeLine. */
inline void delayRangeMs (int machine, float tweak, double& lo, double& hi) noexcept
{
    if (machine == dBucket)
    {
        const bool doubleChip = stepIndex (tweak, 2) == 1;
        lo = doubleChip ? 80.0 : 40.0;
        hi = doubleChip ? 800.0 : 400.0;
        return;
    }
    lo = 60.0; hi = 2500.0;
}

inline double freeDelayMs (int machine, float tweak, float v) noexcept
{
    double lo, hi; delayRangeMs (machine, tweak, lo, hi);
    return lo * std::pow (hi / lo, (double) v);
}

/** Tempo-synced note values, shortest to longest (in quarter notes). */
inline constexpr int numDivisions = 8;
inline constexpr const char* divisionNames[numDivisions] = { "1/16", "1/8T", "1/8", "1/4T", "D1/8", "1/4", "D1/4", "1/2" };
inline constexpr double divisionBeats[numDivisions] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0 };

/** Delay time in ms: synced to the tempo, or free. Clamped to the machine's range. */
inline double delayMs (int machine, float tweak, float timeKnob, bool sync, double bpm) noexcept
{
    double lo, hi; delayRangeMs (machine, tweak, lo, hi);
    if (! sync)
        return freeDelayMs (machine, tweak, timeKnob);
    const double beat = 60000.0 / std::clamp (bpm, 30.0, 300.0);
    return std::clamp (beat * divisionBeats[stepIndex (timeKnob, numDivisions)], lo, hi);
}

/** Swell (delay) rise time 0.1 .. 4 s. */
inline double delayRiseSeconds (float v) noexcept { return 0.1 * std::pow (40.0, (double) v); }

} // namespace stompbox::board::machines
