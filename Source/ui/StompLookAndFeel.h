#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stompbox::ui
{

namespace colours
{
    inline const juce::Colour background   { 0xff17181a };
    inline const juce::Colour headerLine   { 0xff2a2b2e };
    inline const juce::Colour aside        { 0xff1a1b1d };
    inline const juce::Colour card         { 0xff202124 };
    inline const juce::Colour cardBorder   { 0xff2f3134 };
    inline const juce::Colour control      { 0xff2a2b2e };
    inline const juce::Colour controlLine  { 0xff3a3c40 };
    inline const juce::Colour segmentOn    { 0xff34363a };
    inline const juce::Colour text         { 0xfff4f1ea };
    inline const juce::Colour textSoft     { 0xffcfc9be };
    inline const juce::Colour muted        { 0xffa9a39a };
    inline const juce::Colour faint        { 0xff8f897f };
    inline const juce::Colour accent       { 0xffa04640 };   // matches the rug

    inline const juce::Colour ampUpper     { 0xff252527 };   // Royal 30 upper panel
    inline const juce::Colour ampLower     { 0xff633634 };   // Royal 30 lower panel (grille)
    inline const juce::Colour panelLabel   { 0xffe9e3d6 };
    inline const juce::Colour cream        { 0xffefe7d4 };
}

/** Shared fonts. The wordmark uses Goldman Bold (SIL OFL, bundled). */
struct Fonts
{
    static juce::Font wordmark (float height);
    static juce::Font ui (float height, bool bold = false);
    static juce::Font label (float height);   // small caps-style panel labels
    static juce::Font lettering (float height); // pedal names and the ROYAL 30 badge (Archivo Black, OFL)
};

/** Draws text with extra letter spacing (tracking as a fraction of font height). */
void drawTracked (juce::Graphics& g, const juce::String& text, juce::Font font, float tracking,
                  juce::Rectangle<float> area, juce::Justification just);

//==============================================================================
class StompLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StompLookAndFeel();

    // Cream amp knob, shared by every knob in StompBox.
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Fonts::ui (13.0f, true); }

    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
};

} // namespace stompbox::ui
