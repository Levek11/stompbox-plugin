#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "Royal30.h"
#include "CabIR.h"
#include "pedals/Pedal.h"
#include "board/BoardModel.h"

namespace stompbox
{

/** The full StompBox signal chain:
    input jack (mono) -> pedals in board order -> [oversampled] Royal 30 x2
    -> cab IR (L and R) -> output level.

    The input jack is mono: a stereo input is folded to the side that carries
    the guitar (or averaged if both do). The chain stays mono until it reaches
    a stereo pedal; from there on it is stereo, mono pedals after it run as
    dual mono (never collapsing the width), and each side feeds its own
    Royal 30 and cab. With no stereo pedal, both amps would get the same
    signal, so one amp and one convolution run and the result is copied to
    the right side, which is sample-identical. */
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

    /** Message thread: hand the board (order, on/off, knob values) to the audio thread. */
    void setBoard (const board::BoardModel& board);

    /** Audio thread: Logic's tempo for synced pedals. */
    void setTempo (double bpm) noexcept { context.bpm = bpm; }

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

    // --- Pedal chain ------------------------------------------------------
    std::array<std::unique_ptr<dsp::Pedal>, (size_t) board::numPedals> pedals;
    std::array<std::array<std::atomic<float>, 16>, (size_t) board::numPedals> pedalValues;
    std::array<std::atomic<bool>, (size_t) board::numPedals> pedalOn;

    struct Chain { int count = 0; std::array<int, (size_t) board::numPedals> order {}; };
    juce::SpinLock chainLock;
    Chain pendingChain, activeChain;
    int pendingVersion = 0, activeVersion = 0;
    dsp::PedalContext context;

    std::atomic<float> outputDb { 0.0f };
    juce::SmoothedValue<float> outputGain;
};

} // namespace stompbox
