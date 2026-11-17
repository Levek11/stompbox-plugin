#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace stompbox::params
{
    // Parameter IDs are part of saved projects and presets: never rename them.
    inline constexpr const char* channel = "ampChannel";
    inline constexpr const char* volume  = "ampVolume";
    inline constexpr const char* treble  = "ampTreble";
    inline constexpr const char* bass    = "ampBass";
    inline constexpr const char* cut     = "ampCut";
    inline constexpr const char* master  = "ampMaster";
    inline constexpr const char* output  = "outputLevel";

    inline constexpr int version = 1;

    /** Amp knobs are 0..1 internally and read 0..10 like the real amp. */
    inline std::unique_ptr<juce::AudioParameterFloat> makeKnob (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, version }, name,
            juce::NormalisableRange<float> (0.0f, 1.0f), def,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v * 10.0f, 1); })
                .withValueFromStringFunction ([] (const juce::String& s) { return juce::jlimit (0.0f, 1.0f, s.getFloatValue() / 10.0f); }));
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { channel, version }, "Channel", juce::StringArray { "Normal", "Top Boost" }, 1));

        layout.add (makeKnob (volume, "Volume", 0.5f));
        layout.add (makeKnob (treble, "Treble", 0.6f));
        layout.add (makeKnob (bass,   "Bass",   0.4f));
        layout.add (makeKnob (cut,    "Cut",    0.3f));
        layout.add (makeKnob (master, "Master", 0.65f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { output, version }, "Output",
            juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; })));

        return layout;
    }
}
