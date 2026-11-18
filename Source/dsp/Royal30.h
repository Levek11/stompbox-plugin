#pragma once

#include <array>
#include <atomic>
#include "Filters.h"
#include "Tubes.h"
#include "ToneStack.h"

// Royal 30: a circuit-based model of an AC30 Top Boost style amp.
//
// Signal path of one amp (all at the oversampled rate):
//
//   Top Boost:  V1 (ECC83) -> Volume -> V2a (ECC83) -> V2b cathode follower -> Treble/Bass stack ─┐
//   Normal:     V1 (ECC83) -> Volume ──────────────────────────────────────────────────────────────┤
//                                                                                                   v
//               mixer -> V3 (ECC83) -> Master -> long-tailed-pair PI -> Cut -> 4 x EL84 (sag) -> OT
//
// StompBox always runs two identical amps, one per stereo side, sharing one set
// of controls. Each Royal30Amp is one amp; Royal30 owns the pair.

namespace stompbox::dsp
{

struct Royal30Settings
{
    bool topBoost = true;
    float volume = 0.6f;   // all knobs 0..1 (shown as 0..10)
    float treble = 0.6f;
    float bass   = 0.3f;
    float cut    = 0.3f;
    float master = 0.65f;
};

//==============================================================================
class Royal30Amp
{
public:
    void prepare (double oversampledRate);
    void reset();

    /** Called every control tick (a few samples) with smoothed control values. */
    void setControls (bool topBoost, double volumeGain, double treble, double bass,
                      double cutAmount, double masterGain, bool updateToneStack) noexcept;

    /** One sample, in volts at the input jack -> normalised speaker-output level. */
    inline double process (double x) noexcept;

private:
    double fs = 192000.0;

    TriodeStage v1TopBoost, v2TopBoost, v1Normal, v3Mixer;
    ToneStack toneStack;
    OnePole normalVoicing;
    PoleZero cutNetwork;

    OnePole piCoupling, powerCoupling;
    double sagEnv = 0.0, sagAttack = 0.0, sagRelease = 0.0;

    Biquad otHighpass, otLowpass, speakerResonance, voiceCoilRise;

    bool tb = true;
    double channelWeight = 1.0, channelFadeStep = 0.001;   // 1 = Top Boost, 0 = Normal
    double volumeGain = 0.05, masterGain = 0.05;
    double cutCached = -1.0;
};

//==============================================================================
/** The dual amp. Processes up to 2 channels; channel 0 -> left amp, 1 -> right amp. */
class Royal30
{
public:
    void prepare (double oversampledRate);
    void reset();

    /** Settings can be changed from any thread; they are read once per block. */
    void setSettings (const Royal30Settings& s) noexcept;

    /** Process an oversampled block in place. When numChannels == 1, only one
        amp runs (the caller copies the result to the other side). */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Input level calibration: full-scale digital input corresponds to this
        many volts at the amp's input jack. */
    static constexpr double voltsAtFullScale = 1.0;

    /** Output trim: a fully driven power amp peaks a few dB under full scale after the cab. */
    static constexpr double outputTrim = 0.28;

private:
    std::array<Royal30Amp, 2> amps;
    double fs = 192000.0;

    struct Atomic
    {
        std::atomic<bool> topBoost { true };
        std::atomic<float> volume { 0.5f }, treble { 0.6f }, bass { 0.4f }, cut { 0.3f }, master { 0.5f };
    } target;

    // Smoothed control values (control-rate ramps)
    struct Ramp
    {
        double current = 0.0, target = 0.0, step = 0.0;
        int remaining = 0;
        void setTarget (double t, int steps) { if (t != target) { target = t; remaining = steps; step = (target - current) / steps; } }
        void snap (double t) { current = target = t; remaining = 0; }
        bool tick() { if (remaining > 0) { current += step; if (--remaining == 0) current = target; return true; } return false; }
    };
    Ramp volume, treble, bass, cut, master;
    bool firstBlock = true;
    bool lastTopBoost = true;
};

//==============================================================================
// Pot tapers
inline double audioTaper (double p) noexcept   // log ("A") pot, ~9% at half rotation
{
    constexpr double k = 2.0;  // decades of range
    return (std::pow (10.0, k * p) - 1.0) / (std::pow (10.0, k) - 1.0);
}

} // namespace stompbox::dsp

#include "Royal30.inl"
