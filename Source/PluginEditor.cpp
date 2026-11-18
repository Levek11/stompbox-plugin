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
MainView::MainView (StompBoxProcessor& p) : processor (p), pedalboard (p), ampCab (p)
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

    for (int i = 0; i < 3; ++i)
    {
        sizeButtons[i].onClick = [this, i] { setBoardSize (i); };
        addChildComponent (sizeButtons[i]);
    }
    sizeButtons[0].setTooltip ("Small board: 6 slots in one row");
    sizeButtons[1].setTooltip ("Medium board: 10 slots, two rows of 5");
    sizeButtons[2].setTooltip ("Large board: 14 slots, two rows of 7");
    updateBoardSizeButtons();

    addChildComponent (pedalboard);
    addChildComponent (ampCab);
    showTab (0);
}

void MainView::updateBoardSizeButtons()
{
    const int current = (int) processor.getBoard().getSize();
    for (int i = 0; i < 3; ++i)
        sizeButtons[i].setToggleState (i == current, juce::dontSendNotification);
}

void MainView::setBoardSize (int size)
{
    const int sentBack = processor.editBoard ([size] (stompbox::board::BoardModel& b)
    {
        return b.setSize ((stompbox::board::BoardModel::Size) size);
    });
    updateBoardSizeButtons();
    if (sentBack > 0)
        pedalboard.showNote (juce::String (sentBack) + (sentBack == 1 ? " pedal didn't fit and went back to the shelf"
                                                                     : " pedals didn't fit and went back to the shelf"));
}

void MainView::showTab (int index)
{
    pedalboardTab.setToggleState (index == 0, juce::dontSendNotification);
    ampTab.setToggleState (index == 1, juce::dontSendNotification);
    pedalboard.setVisible (index == 0);
    ampCab.setVisible (index == 1);
    for (auto& b : sizeButtons) b.setVisible (index == 0);
    updateBoardSizeButtons();
    repaint();
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

    header.removeFromRight (20);
    auto sizes = header.removeFromRight (216).withSizeKeepingCentre (216, 36).reduced (3);
    for (auto& b : sizeButtons)
        b.setBounds (sizes.removeFromLeft (70));

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

    // Segment tracks
    auto track = [&] (juce::Component& first, juce::Component& last)
    {
        const auto r = juce::Rectangle<float> ((float) first.getX() - 3.0f, (float) first.getY() - 3.0f,
                                               (float) (last.getRight() - first.getX()) + 6.0f, (float) first.getHeight() + 6.0f);
        g.setColour (colours::card);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::cardBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    };
    track (liveButton, studioButton);
    if (sizeButtons[0].isVisible())
    {
        track (sizeButtons[0], sizeButtons[2]);
        g.setColour (colours::muted);
        drawTracked (g, "BOARD", Fonts::ui (11.0f), 0.08f,
                     juce::Rectangle<float> ((float) sizeButtons[0].getX() - 60.0f, 0.0f, 48.0f, (float) headerHeight),
                     juce::Justification::right);
    }
}

} // namespace stompbox::ui

//==============================================================================
StompBoxEditor::StompBoxEditor (StompBoxProcessor& p)
    : AudioProcessorEditor (p), processor (p), view (p)
{
    using MV = stompbox::ui::MainView;
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    setResizable (true, true);
    setResizeLimits (MV::designWidth * 6 / 10, MV::designHeight * 6 / 10, MV::designWidth * 3 / 2, MV::designHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) MV::designWidth / MV::designHeight);
    const float scale = p.getEditorScale();   // 88% by default, then whatever size the user last chose
    setSize (juce::roundToInt (MV::designWidth * scale), juce::roundToInt (MV::designHeight * scale));
    sizeRestored = true;
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
    if (sizeRestored)
        processor.setEditorScale (scale);
}
