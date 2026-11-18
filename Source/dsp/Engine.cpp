#include "Engine.h"
#include "pedals/DrivePedals.h"
#include "pedals/Afterglow.h"

namespace stompbox
{

using OS = juce::dsp::Oversampling<float>;

// Polyphase IIR half-band filters: minimum-phase style, no lookahead, so the
// oversampling adds no reported latency (a few samples of phase shift only).
Engine::Engine()
    : osLive   (2, 1, OS::filterHalfBandPolyphaseIIR, true, false),
      osStudio (2, 2, OS::filterHalfBandPolyphaseIIR, true, false)
{
    // One processor per modeled pedal. Pedals without a sound yet are null and
    // pass the signal through untouched.
    pedals[(size_t) board::indexOf ("hotRod")]     = std::make_unique<dsp::HotRod>();
    pedals[(size_t) board::indexOf ("goldenHour")] = std::make_unique<dsp::GoldenHour>();
    pedals[(size_t) board::indexOf ("afterglow")]  = std::make_unique<dsp::Afterglow>();

    const auto& cat = board::catalog();
    for (size_t i = 0; i < cat.size(); ++i)
    {
        pedalOn[i].store (true);
        for (size_t k = 0; k < cat[i].controls.size() && k < 16; ++k)
            pedalValues[i][k].store (cat[i].controls[k].defaultValue);
    }
}

void Engine::setBoard (const board::BoardModel& b)
{
    const auto& cat = board::catalog();
    for (int i = 0; i < board::numPedals; ++i)
    {
        const auto& st = b.pedal (i);
        pedalOn[(size_t) i].store (st.on);
        for (size_t k = 0; k < cat[(size_t) i].controls.size() && k < 16; ++k)
            pedalValues[(size_t) i][k].store (st.values[k]);
    }

    Chain c;
    for (int index : b.signalOrder())
        c.order[(size_t) c.count++] = index;

    const juce::SpinLock::ScopedLockType sl (chainLock);
    pendingChain = c;
    ++pendingVersion;
}

void Engine::prepare (double sampleRate, int maximumBlockSize)
{
    baseRate = sampleRate;
    (void) dsp::TubeCurves::get();   // build lookup tables off the audio thread

    osLive.initProcessing ((size_t) maximumBlockSize);
    osStudio.initProcessing ((size_t) maximumBlockSize);

    cab.prepare ({ sampleRate, (juce::uint32) maximumBlockSize, 2 });
    for (auto& p : pedals)
        if (p != nullptr) p->prepare (sampleRate, maximumBlockSize);

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
    for (auto& p : pedals)
        if (p != nullptr) p->reset();
    inputEnv[0] = inputEnv[1] = 0.0f;
    foldWeight[0] = foldWeight[1] = 0.5f;
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

    // The input jack is mono, like a real guitar cable. A stereo track whose
    // guitar sits on one side (e.g. Input 1-2 with the guitar in input 2) is
    // folded to that side; a true dual-mono input is averaged.
    if (numInputChannels >= 2 && buffer.getNumChannels() >= 2)
        foldInputToMono (buffer);

    // --- Pedals --------------------------------------------------------------
    // Pick up board changes without ever blocking the audio thread.
    if (chainLock.tryEnter())
    {
        if (pendingVersion != activeVersion)
        {
            // Pedals that just arrived on the board start from silence.
            for (int k = 0; k < pendingChain.count; ++k)
            {
                const int idx = pendingChain.order[(size_t) k];
                bool wasThere = false;
                for (int j = 0; j < activeChain.count; ++j) wasThere = wasThere || activeChain.order[(size_t) j] == idx;
                if (! wasThere && pedals[(size_t) idx] != nullptr) pedals[(size_t) idx]->reset();
            }
            activeChain = pendingChain;
            activeVersion = pendingVersion;
        }
        chainLock.exit();
    }

    context.studio = q == (int) Quality::studio;
    const bool canStereo = buffer.getNumChannels() >= 2;
    int procChannels = 1;
    float values[16];
    for (int k = 0; k < activeChain.count; ++k)
    {
        const int idx = activeChain.order[(size_t) k];
        auto* pedal = pedals[(size_t) idx].get();
        if (pedal == nullptr) continue;

        if (pedal->isStereo() && canStereo && procChannels == 1)
        {
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);   // the chain turns stereo here
            procChannels = 2;
        }
        for (int v = 0; v < 16; ++v) values[v] = pedalValues[(size_t) idx][(size_t) v].load (std::memory_order_relaxed);
        pedal->process (buffer.getArrayOfWritePointers(), procChannels, numSamples, values,
                        pedalOn[(size_t) idx].load (std::memory_order_relaxed), context);
    }

    const bool monoPath = procChannels == 1;

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

void Engine::foldInputToMono (juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    const float* r = buffer.getReadPointer (1);

    // Slow-release peak envelopes per side decide which side carries the guitar.
    const float decay = std::pow (0.001f, (float) n / (float) (baseRate * 2.0));   // -60 dB over ~2 s
    inputEnv[0] = juce::jmax (buffer.getMagnitude (0, 0, n), inputEnv[0] * decay);
    inputEnv[1] = juce::jmax (buffer.getMagnitude (1, 0, n), inputEnv[1] * decay);

    constexpr float silentRatio = 0.01f;   // a side 40 dB below the other counts as empty
    float targetL = foldWeight[0], targetR = foldWeight[1];
    if (inputEnv[0] > 1.0e-5f || inputEnv[1] > 1.0e-5f)
    {
        if (inputEnv[0] < inputEnv[1] * silentRatio)      { targetL = 0.0f; targetR = 1.0f; }
        else if (inputEnv[1] < inputEnv[0] * silentRatio) { targetL = 1.0f; targetR = 0.0f; }
        else                                              { targetL = 0.5f; targetR = 0.5f; }
    }

    // Ramp the weights across the block so a change never clicks.
    const float stepL = (targetL - foldWeight[0]) / (float) n, stepR = (targetR - foldWeight[1]) / (float) n;
    float wl = foldWeight[0], wr = foldWeight[1];
    for (int i = 0; i < n; ++i)
    {
        wl += stepL; wr += stepR;
        l[i] = l[i] * wl + r[i] * wr;
    }
    foldWeight[0] = targetL; foldWeight[1] = targetR;
}

} // namespace stompbox
