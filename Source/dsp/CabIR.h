#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include "Filters.h"

namespace stompbox
{

//==============================================================================
/** A cab impulse response as stored in StompBox: mono samples plus the
    original sample rate and a display name. The samples themselves are
    saved inside the plugin state (and later inside presets), so a setup keeps
    working even if the original file is moved or deleted. */
struct IRData
{
    juce::String name;
    double sampleRate = 0.0;
    juce::AudioBuffer<float> samples;   // 1 channel

    static constexpr double maxSeconds = 1.0;

    bool isValid() const { return sampleRate > 0.0 && samples.getNumSamples() > 0; }

    /** Reads a WAV/AIFF file. Multichannel files are summed to mono. */
    static juce::Result loadFromFile (const juce::File& file, IRData& out);

    juce::ValueTree toValueTree() const;
    static IRData fromValueTree (const juce::ValueTree& tree);

    /** Peak-per-bucket overview for drawing, values 0..1. */
    std::vector<float> overview (int buckets) const;

    static inline const juce::Identifier treeType { "CabIR" };
};

//==============================================================================
/** Cab stage: zero-latency convolution with the loaded IR, the same IR on both
    sides. With no IR loaded it falls back to a basic speaker-voicing filter so
    the amp is still usable. */
class CabIR
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Processes up to 2 channels in place. Audio thread. */
    void process (juce::dsp::AudioBlock<float> block) noexcept;

    /** Message thread (or setState thread). Wait-free for the audio thread. */
    void load (const IRData& ir);
    void clear();

    bool hasImpulse() const noexcept { return active.load(); }

    /** Added latency in samples (always 0: the convolution runs with no lookahead). */
    int getLatency() const { return convolution.getLatency(); }

private:
    juce::dsp::Convolution convolution;   // default construction = zero latency
    std::atomic<bool> active { false };
    bool wasActive = false;

    struct Fallback
    {
        dsp::Biquad hp, lp1, lp2, presence;
    };
    std::array<Fallback, 2> fallback;
};

} // namespace stompbox
