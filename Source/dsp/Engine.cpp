#include "Engine.h"

namespace stompbox
{

using OS = juce::dsp::Oversampling<float>;

// Polyphase IIR half-band filters: minimum-phase style, no lookahead, so the
// oversampling adds no reported latency (a few samples of phase shift only).
Engine::Engine()
    : osLive   (2, 1, OS::filterHalfBandPolyphaseIIR, true, false),
      osStudio (2, 2, OS::filterHalfBandPolyphaseIIR, true, false)
{
}

void Engine::prepare (double sampleRate, int maximumBlockSize)
{
    baseRate = sampleRate;
    (void) dsp::TubeCurves::get();   // build lookup tables off the audio thread

    osLive.initProcessing ((size_t) maximumBlockSize);
    osStudio.initProcessing ((size_t) maximumBlockSize);

    cab.prepare ({ sampleRate, (juce::uint32) maximumBlockSize, 2 });

    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputDb.load()));

    activeQuality = -1;   // forces amp prepare on first block
}

void Engine::reset()
{
    osLive.reset();
    osStudio.reset();
    amp.reset();
    cab.reset();
}

void Engine::process (juce::AudioBuffer<float>& buffer, int numInputChannels, bool offline) noexcept
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    const int q = offline ? (int) Quality::studio : quality.load();
    auto& os = (q == (int) Quality::live) ? osLive : osStudio;

    if (q != activeQuality)
    {
        // Real-time safe: Royal30::prepare only recomputes coefficients.
        activeQuality = q;
        os.reset();
        amp.prepare (baseRate * (double) os.getOversamplingFactor());
    }

    const bool monoPath = numInputChannels < 2;
    const int procChannels = monoPath ? 1 : 2;

    juce::dsp::AudioBlock<float> block (buffer);
    auto active = block.getSubsetChannelBlock (0, (size_t) procChannels);

    // Amps, oversampled
    auto up = os.processSamplesUp (active);
    {
        float* chans[2] = { up.getChannelPointer (0), procChannels > 1 ? up.getChannelPointer (1) : nullptr };
        amp.process (chans, procChannels, (int) up.getNumSamples());
    }
    os.processSamplesDown (active);

    // Cab
    cab.process (active);

    // Output level
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputDb.load()));
    if (outputGain.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outputGain.getNextValue();
            for (int c = 0; c < procChannels; ++c)
                buffer.getWritePointer (c)[i] *= g;
        }
    }
    else
    {
        for (int c = 0; c < procChannels; ++c)
            buffer.applyGain (c, 0, numSamples, outputGain.getTargetValue());
    }

    if (monoPath && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

} // namespace stompbox
