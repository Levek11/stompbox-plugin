#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "StompLookAndFeel.h"

class StompBoxProcessor;

namespace stompbox::ui
{

//==============================================================================
/** Cream amp knob with its panel label. While dragging, the label shows the value. */
class AmpKnob : public juce::Component
{
public:
    AmpKnob (juce::AudioProcessorValueTreeState& state, const char* paramID, const juce::String& label);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
/** NORMAL / TOP BOOST switch. */
class ChannelSwitch : public juce::Component
{
public:
    explicit ChannelSwitch (juce::RangedAudioParameter& param);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Segment : juce::Button
    {
        Segment (const juce::String& t) : juce::Button (t) { setClickingTogglesState (false); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    Segment normal { "NORMAL" }, topBoost { "TOP BOOST" };
    juce::ParameterAttachment attachment;
    void update (float value);
};

//==============================================================================
/** CAB IR card: name, overview of the IR, Load and Clear. */
class CabCard : public juce::Component, private juce::Timer
{
public:
    explicit CabCard (StompBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void chooseFile();

    StompBoxProcessor& processor;
    juce::TextButton loadButton { "Load IR..." }, clearButton { "Clear" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String irName, irDetail, error;
    std::vector<float> overview;
    int shownVersion = -1;
};

//==============================================================================
class OutputCard : public juce::Component
{
public:
    explicit OutputCard (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
/** The Amp/Cab tab: the Royal 30 in the room on the left, cab and output on the right. */
class AmpCabTab : public juce::Component
{
public:
    explicit AmpCabTab (StompBoxProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> ampBounds() const;
    juce::Rectangle<float> controlPanelBounds() const;

    StompBoxProcessor& processor;

    ChannelSwitch channel;
    AmpKnob volume, treble, bass, cut, master;
    CabCard cabCard;
    OutputCard outputCard;

    static constexpr int asideWidth = 340;
};

} // namespace stompbox::ui
