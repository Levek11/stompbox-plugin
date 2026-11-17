#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "Royal30.h"
#include "CabIR.h"

namespace stompbox
{

/** The full StompBox signal chain for build step 1:
    input -> [oversampled] dual Royal 30 -> cab IR (L and R) -> output level.

    The input jack is mono: a stereo input is folded to the side that carries
    the guitar (or averaged if both do). Both amps then get the same signal, so
    only one amp and one convolution actually run and the result is copied to
    the right side, which is sample-identical to running both. Stereo before
    the amps arrives with the stereo pedals in step 2. */
class Engine
{
public:
    enum class Quality { live = 0, studio = 1 };

    Engine();

    void prepare (double sampleRate, int maximumBlockSize);
    void reset();

    void setAmpSettings (const dsp::Royal30Settings& s) noexcept { amp.setSettings (s); }
    void setOutputDb (float db) noexcept { outputDb.store (db); }
    void setQuality (Quality q) noexcept { quality.store ((int) q); }
    Quality getQuality() const noexcept { return (Quality) quality.load(); }

    /** buffer has 2 channels; numInputChannels is 1 (mono in) or 2.
        offline = true forces Studio quality (bounces always render at full quality). */
    void process (juce::AudioBuffer<float>& buffer, int numInputChannels, bool offline) noexcept;

    CabIR& getCab() noexcept { return cab; }

    /** StompBox adds no latency: no lookahead anywhere in the chain. */
    static constexpr int latencySamples = 0;

    int getOversamplingFactor (Quality q) const noexcept { return q == Quality::live ? 2 : 4; }

private:
    juce::dsp::Oversampling<float> osLive, osStudio;
    dsp::Royal30 amp;
    CabIR cab;

    std::atomic<int> quality { (int) Quality::studio };
    int activeQuality = -1;
    double baseRate = 48000.0;

    void foldInputToMono (juce::AudioBuffer<float>& buffer) noexcept;
    float inputEnv[2] { 0.0f, 0.0f };
    float foldWeight[2] { 0.5f, 0.5f };

    std::atomic<float> outputDb { 0.0f };
    juce::SmoothedValue<float> outputGain;
};

} // namespace stompbox
