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

    juce::ValueTree toValueTree (const juce::Identifier& type = treeType) const;
    static IRData fromValueTree (const juce::ValueTree& tree, const juce::Identifier& type = treeType);

    bool sameAs (const IRData& other) const;

    /** Peak-per-bucket overview for drawing, values 0..1. */
    std::vector<float> overview (int buckets) const;

    static inline const juce::Identifier treeType { "CabIR" };          // left (or both, when linked)
    static inline const juce::Identifier rightTreeType { "CabIRRight" };
};

//==============================================================================
/** Cab stage: zero-latency convolution, one engine per side.

    Linked (the default), one IR plays on both sides. Unlinked, the left and
    right amps each get their own IR. Two different IRs are lined up first
    (start time and polarity), so the two sides still add up cleanly when the
    mix is summed to mono, as it often is on a church PA. With no IR at all,
    a basic speaker-voicing filter keeps the amp usable. */
class CabIR
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** Processes 1 or 2 channels in place: channel 0 -> left IR, 1 -> right IR. Audio thread. */
    void process (juce::dsp::AudioBlock<float> block) noexcept;

    /** Message thread (or setState thread). Wait-free for the audio thread.
        A missing side falls back to the other side's IR. */
    void load (const IRData& left, const IRData& right, bool linked);
    void load (const IRData& both) { load (both, {}, true); }

    bool hasImpulse() const noexcept { return active[0].load(); }

    /** True when the two sides use different IRs (so even a mono chain comes
        out in stereo after the cab). */
    bool isDual() const noexcept { return dual.load(); }

    /** Added latency in samples (always 0: the convolution runs with no lookahead). */
    int getLatency() const { return conv[0].getLatency(); }

    /** Lines up two IRs for mono compatibility: trims each to start just
        before its first strong peak, refines the offset by cross-correlation
        and flips the second one's polarity if it's inverted. */
    static void alignPair (IRData& a, IRData& b);

private:
    void loadSide (int side, const IRData& ir);

    std::array<juce::dsp::Convolution, 2> conv;   // default construction = zero latency
    std::array<std::atomic<bool>, 2> active { false, false };
    std::array<bool, 2> wasActive { false, false };
    std::atomic<bool> dual { false };

    struct Fallback
    {
        dsp::Biquad hp, lp1, lp2, presence;
    };
    std::array<Fallback, 2> fallback;
};

} // namespace stompbox
