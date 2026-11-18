#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include "../Filters.h"
#include "board/PedalCatalog.h"

namespace stompbox::dsp
{

/** What every pedal gets each block besides its knobs. */
struct PedalContext
{
    double bpm = 120.0;        // Logic's tempo (follows tempo changes)
    bool studio = true;        // Studio quality (else Live)
};

/** Base for all pedals. One instance per pedal type lives in the engine for
    the whole session; the board decides whether and where it runs.

    Signals are in the same "volts" scale as the amp (1.0 = 1 V at the jack). */
class Pedal
{
public:
    virtual ~Pedal() = default;

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;

    /** Dual pedals: the second footswitch (called before process each block). */
    virtual void setSecondFootswitch (bool) noexcept {}

    /** Stereo pedals turn a mono chain into a stereo one from here on. */
    virtual bool isStereo() const noexcept { return false; }

    /** In-place processing of 1 (mono chain) or 2 channels.
        `values` are the pedal's control values (0..1) in catalog order. */
    virtual void process (float* const* channels, int numChannels, int numSamples,
                          const float* values, bool on, const PedalContext&) noexcept = 0;

    /** Looks up a control's index by id (in catalog order). */
    static int control (const char* pedalId, const char* controlId)
    {
        const int p = board::indexOf (pedalId);
        if (p < 0) return -1;
        const auto& cs = board::catalog()[(size_t) p].controls;
        for (size_t i = 0; i < cs.size(); ++i)
            if (std::strcmp (cs[i].id, controlId) == 0) return (int) i;
        jassertfalse;
        return -1;
    }
};

//==============================================================================
/** Base for the analog drive pedals: oversampled per-sample model, a click-
    free crossfade when the footswitch is pressed, and per-side state so a
    drive placed after a stereo pedal runs as dual mono. */
class DrivePedal : public Pedal
{
public:
    DrivePedal()
        : os2 (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false),
          os4 (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false)
    {
    }

    void prepare (double sampleRate, int maxBlock) override
    {
        fs = sampleRate;
        os2.initProcessing ((size_t) maxBlock);
        os4.initProcessing ((size_t) maxBlock);
        dry.setSize (2, maxBlock);
        fadeStep = 1.0f / (float) (0.015 * fs);
        factor = 0;
        reset();
    }

    void reset() override
    {
        os2.reset(); os4.reset();
        wet = 0.0f;
        resetModel();
    }

    void process (float* const* ch, int numCh, int n, const float* v, bool on, const PedalContext& ctx) noexcept override
    {
        const float target = on ? 1.0f : 0.0f;
        if (wet == 0.0f && target == 0.0f)
            return;   // fully bypassed: the signal passes untouched

        auto& os = ctx.studio ? os4 : os2;
        const int f = (int) os.getOversamplingFactor();
        if (f != factor)
        {
            factor = f;
            os.reset();
            setRate (fs * f);
            resetModel();
        }

        for (int c = 0; c < numCh; ++c)
            dry.copyFrom (c, 0, ch[c], n);

        setControls (v);

        juce::dsp::AudioBlock<float> block (ch, (size_t) numCh, (size_t) n);
        auto up = os.processSamplesUp (block);
        for (int c = 0; c < numCh; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = (float) tick ((double) d[i], c);
        }
        os.processSamplesDown (block);

        // Crossfade between the dry signal and the pedal (footswitch).
        if (wet != target || wet < 1.0f)
        {
            float w = wet;
            for (int i = 0; i < n; ++i)
            {
                w = target > w ? std::min (target, w + fadeStep) : std::max (target, w - fadeStep);
                for (int c = 0; c < numCh; ++c)
                    ch[c][i] = dry.getSample (c, i) + w * (ch[c][i] - dry.getSample (c, i));
            }
            wet = w;
        }
    }

protected:
    virtual void setRate (double oversampledRate) = 0;
    virtual void resetModel() = 0;
    virtual void setControls (const float* values) noexcept = 0;
    virtual double tick (double x, int channel) noexcept = 0;

    double fs = 48000.0;

private:
    juce::dsp::Oversampling<float> os2, os4;
    juce::AudioBuffer<float> dry;
    int factor = 0;
    float wet = 0.0f, fadeStep = 0.01f;
};

//==============================================================================
/** Smooth diode-style limiter with separate thresholds per polarity.
    `knee` sets how hard the corner is (silicon diodes: about 2-2.5). */
inline double diodeClip (double v, double posThreshold, double negThreshold, double knee) noexcept
{
    const double t = v >= 0.0 ? posThreshold : negThreshold;
    const double r = std::abs (v) / t;
    return v / std::pow (1.0 + std::pow (r, knee), 1.0 / knee);
}

/** Op-amp output swing limit on a 9 V supply. */
inline double railClip (double v, double rail = 4.0) noexcept
{
    return diodeClip (v, rail, rail, 6.0);
}

inline double logTaper (double p, double decades = 2.0) noexcept
{
    return (std::pow (10.0, decades * p) - 1.0) / (std::pow (10.0, decades) - 1.0);
}

} // namespace stompbox::dsp
