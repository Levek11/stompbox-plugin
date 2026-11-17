#include "PluginEditor.h"

namespace stompbox::ui
{

//==============================================================================
void TabButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    g.setColour (on ? colours::text : over ? colours::textSoft : colours::muted);
    g.setFont (Fonts::ui (14.0f, true));
    g.drawText (getButtonText(), r.withTrimmedBottom (2.0f), juce::Justification::centred);
    if (on)
    {
        g.setColour (colours::accent);
        g.fillRect (r.removeFromBottom (2.0f));
    }
}

void SegmentButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    if (on || over)
    {
        g.setColour (on ? colours::segmentOn : juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r, 6.0f);
    }
    g.setColour (on ? colours::text : colours::muted);
    g.setFont (Fonts::ui (12.0f, on));
    g.drawText (getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
void PedalboardTab::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // Rug
    g.setColour (juce::Colour (0xff7d3632));
    g.fillAll();
    g.setColour (colours::accent);
    g.fillRoundedRectangle (r.reduced (40.0f), 18.0f);

    // Dark wood board
    const auto board = juce::Rectangle<float> (820.0f, 300.0f).withCentre (r.getCentre());
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (board.translated (0.0f, 10.0f), 14.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a3020), 0.0f, board.getY(),
                                             juce::Colour (0xff3a2416), 0.0f, board.getBottom(), false));
    g.fillRoundedRectangle (board, 14.0f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    for (float y = board.getY() + 10.0f; y < board.getBottom() - 6.0f; y += 7.0f)
        g.drawHorizontalLine ((int) y, board.getX() + 10.0f, board.getRight() - 10.0f);

    g.setColour (colours::text);
    g.setFont (Fonts::ui (18.0f, true));
    g.drawText ("The pedalboard arrives in build step 2", board.withTrimmedBottom (40.0f), juce::Justification::centred);
    g.setColour (colours::textSoft);
    g.setFont (Fonts::ui (13.0f));
    g.drawText ("For now your guitar goes straight into the two Royal 30s.", board.withTrimmedTop (30.0f), juce::Justification::centred);
}

//==============================================================================
MainView::MainView (StompBoxProcessor& p) : processor (p), ampCab (p)
{
    for (auto* b : { (juce::Button*) &pedalboardTab, (juce::Button*) &ampTab })
    {
        b->setRadioGroupId (1);
        addAndMakeVisible (b);
    }
    pedalboardTab.onClick = [this] { showTab (0); };
    ampTab.onClick = [this] { showTab (1); };

    liveButton.setTooltip ("Live: lighter on CPU so small buffers play without crackles. Bounces always render in Studio quality.");
    studioButton.setTooltip ("Studio: full quality. Remembered for every StompBox instance.");
    liveButton.onClick = [this] { processor.setQuality (StompBoxProcessor::Quality::live); updateQualityButtons(); };
    studioButton.onClick = [this] { processor.setQuality (StompBoxProcessor::Quality::studio); updateQualityButtons(); };
    addAndMakeVisible (liveButton);
    addAndMakeVisible (studioButton);
    updateQualityButtons();

    addChildComponent (pedalboard);
    addChildComponent (ampCab);
    showTab (1);   // Build step 1: the amp is what's here
}

void MainView::showTab (int index)
{
    pedalboardTab.setToggleState (index == 0, juce::dontSendNotification);
    ampTab.setToggleState (index == 1, juce::dontSendNotification);
    pedalboard.setVisible (index == 0);
    ampCab.setVisible (index == 1);
}

void MainView::updateQualityButtons()
{
    const bool live = processor.getQuality() == StompBoxProcessor::Quality::live;
    liveButton.setToggleState (live, juce::dontSendNotification);
    studioButton.setToggleState (! live, juce::dontSendNotification);
}

void MainView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);

    auto tabs = header.withTrimmedLeft (24 + 150 + 32);
    pedalboardTab.setBounds (tabs.removeFromLeft (96));
    tabs.removeFromLeft (16);
    ampTab.setBounds (tabs.removeFromLeft (90));

    auto seg = header.removeFromRight (24 + 136).withTrimmedRight (24).withSizeKeepingCentre (136, 36).reduced (3);
    liveButton.setBounds (seg.removeFromLeft (seg.getWidth() / 2));
    studioButton.setBounds (seg);

    pedalboard.setBounds (r);
    ampCab.setBounds (r);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto header = getLocalBounds().removeFromTop (headerHeight).toFloat();
    g.setColour (colours::headerLine);
    g.fillRect (header.removeFromBottom (1.0f));

    // Wordmark: STOMP in the rug colour, BOX in cream. Goldman Bold, slight tracking.
    const auto font = Fonts::wordmark (22.0f);
    const float tracking = 0.12f;
    auto area = juce::Rectangle<float> (24.0f, 0.0f, 160.0f, (float) headerHeight);
    float stompWidth = 0.0f;
    for (auto ch : juce::String ("STOMP"))
        stompWidth += juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (ch)) + tracking * font.getHeight();
    g.setColour (colours::accent);
    drawTracked (g, "STOMP", font, tracking, area, juce::Justification::left);
    g.setColour (colours::text);
    drawTracked (g, "BOX", font, tracking, area.withTrimmedLeft (stompWidth), juce::Justification::left);

    // Segment track
    const auto seg = juce::Rectangle<float> ((float) liveButton.getX() - 3.0f, (float) liveButton.getY() - 3.0f,
                                             (float) (studioButton.getRight() - liveButton.getX()) + 6.0f, (float) liveButton.getHeight() + 6.0f);
    g.setColour (colours::card);
    g.fillRoundedRectangle (seg, 8.0f);
    g.setColour (colours::cardBorder);
    g.drawRoundedRectangle (seg.reduced (0.5f), 8.0f, 1.0f);
}

} // namespace stompbox::ui

//==============================================================================
StompBoxEditor::StompBoxEditor (StompBoxProcessor& p)
    : AudioProcessorEditor (p), view (p)
{
    using MV = stompbox::ui::MainView;
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    setResizable (true, true);
    setResizeLimits (MV::designWidth * 6 / 10, MV::designHeight * 6 / 10, MV::designWidth * 3 / 2, MV::designHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) MV::designWidth / MV::designHeight);
    setSize (MV::designWidth * 85 / 100, MV::designHeight * 85 / 100);
}

StompBoxEditor::~StompBoxEditor()
{
    setLookAndFeel (nullptr);
}

void StompBoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (stompbox::ui::colours::background);
}

void StompBoxEditor::resized()
{
    using MV = stompbox::ui::MainView;
    const float scale = (float) getWidth() / (float) MV::designWidth;
    view.setBounds (0, 0, MV::designWidth, MV::designHeight);
    view.setTransform (juce::AffineTransform::scale (scale));
}
