#include "StompLookAndFeel.h"
#include "StompBoxAssets.h"

namespace stompbox::ui
{

juce::Font Fonts::wordmark (float height)
{
    static auto typeface = juce::Typeface::createSystemTypefaceFor (StompBoxAssets::GoldmanBold_ttf,
                                                                    StompBoxAssets::GoldmanBold_ttfSize);
    return juce::Font (juce::FontOptions (typeface).withHeight (height));
}

juce::Font Fonts::ui (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font Fonts::label (float height)
{
    return juce::Font (juce::FontOptions (height, juce::Font::bold));
}

void drawTracked (juce::Graphics& g, const juce::String& text, juce::Font font, float tracking,
                  juce::Rectangle<float> area, juce::Justification just)
{
    juce::GlyphArrangement ga;
    float x = 0.0f;
    const float extra = tracking * font.getHeight();
    for (auto ch : text)
    {
        const juce::String s = juce::String::charToString (ch);
        ga.addLineOfText (font, s, x, 0.0f);
        x += juce::GlyphArrangement::getStringWidth (font, s) + extra;
    }
    const float width = x - extra;
    const auto bounds = juce::Rectangle<float> (0.0f, -font.getAscent(), width, font.getHeight());
    const auto placed = bounds.withPosition (
        just.testFlags (juce::Justification::horizontallyCentred) ? area.getCentreX() - width / 2
        : just.testFlags (juce::Justification::right) ? area.getRight() - width : area.getX(),
        area.getCentreY() - font.getHeight() / 2);
    ga.moveRangeOfGlyphs (0, -1, placed.getX() - bounds.getX(), placed.getY() - bounds.getY());
    ga.draw (g);
}

//==============================================================================
StompLookAndFeel::StompLookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, colours::card);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::cardBorder);
    setColour (juce::AlertWindow::backgroundColourId, colours::card);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::control);
    setColour (juce::TextButton::textColourOffId, colours::text);
}

void StompLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const float size = (float) juce::jmin (w, h) - 6.0f;
    const auto knob = juce::Rectangle<float> (size, size).withCentre ({ x + w * 0.5f, y + h * 0.5f });

    // Drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (knob.translated (0.0f, 3.0f).expanded (0.5f));

    // Cream cap with a soft top-left highlight
    juce::ColourGradient cap (juce::Colour (0xfffffaf0), knob.getX() + knob.getWidth() * 0.4f, knob.getY() + knob.getHeight() * 0.3f,
                              juce::Colour (0xffbfb49c), knob.getRight(), knob.getBottom(), true);
    cap.addColour (0.55, juce::Colour (0xffe9e0cc));
    g.setGradientFill (cap);
    g.fillEllipse (knob);

    g.setColour (juce::Colours::black.withAlpha (0.15f));
    g.drawEllipse (knob.reduced (0.5f), 1.0f);

    if (s.isMouseOverOrDragging())
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillEllipse (knob);
    }

    // Pointer
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float r = size * 0.5f;
    juce::Path p;
    p.addRoundedRectangle (-1.0f, -r + size * 0.1f, 2.0f, size * 0.3f, 1.0f);
    g.setColour (juce::Colour (0xff1b1b1b));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (knob.getCentreX(), knob.getCentreY()));
}

void StompLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto track = juce::Rectangle<float> ((float) x, y + h * 0.5f - 2.0f, (float) w, 4.0f);
    g.setColour (colours::segmentOn);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);

    const float t = s.isMouseOverOrDragging() ? 16.0f : 14.0f;
    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (t, t).withCentre ({ pos, track.getCentreY() }));
}

void StompLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                                             bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = base;
    if (down) c = c.brighter (0.15f);
    else if (over) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::controlLine);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void StompLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (Fonts::ui (13.0f));
    g.setColour (b.findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void StompLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (colours::card);
    g.setColour (colours::cardBorder);
    g.drawRect (0, 0, w, h);
    g.setColour (colours::text);
    g.setFont (Fonts::ui (12.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 3);
}

} // namespace stompbox::ui
