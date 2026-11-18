#include "AmpCabTab.h"
#include "../PluginProcessor.h"

namespace stompbox::ui
{

static const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00b7);

//==============================================================================
AmpKnob::AmpKnob (juce::AudioProcessorValueTreeState& state, const char* paramID, const juce::String& l)
    : label (l), attachment (state, paramID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
    slider.setMouseDragSensitivity (220);
    slider.setDoubleClickReturnValue (true, 0.5);
    slider.setTitle (l);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.onDragStart = slider.onDragEnd = slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void AmpKnob::resized()
{
    slider.setBounds (getLocalBounds().removeFromTop (getWidth()));
}

void AmpKnob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().withTrimmedTop ((float) getWidth() + 4.0f);
    const bool dragging = slider.isMouseButtonDown();
    g.setColour (dragging ? colours::text : colours::panelLabel);
    drawTracked (g, dragging ? juce::String (slider.getValue() * 10.0, 1) : label,
                 Fonts::label (10.5f), 0.1f, area.removeFromTop (14.0f), juce::Justification::centred);
}

//==============================================================================
ChannelSwitch::ChannelSwitch (juce::RangedAudioParameter& param)
    : attachment (param, [this] (float v) { update (v); })
{
    normal.onClick = [this] { attachment.setValueAsCompleteGesture (0.0f); };
    topBoost.onClick = [this] { attachment.setValueAsCompleteGesture (1.0f); };
    normal.setTitle ("Normal channel");
    topBoost.setTitle ("Top Boost channel");
    addAndMakeVisible (normal);
    addAndMakeVisible (topBoost);
    attachment.sendInitialUpdate();
}

void ChannelSwitch::update (float v)
{
    normal.setToggleState (v < 0.5f, juce::dontSendNotification);
    topBoost.setToggleState (v >= 0.5f, juce::dontSendNotification);
}

void ChannelSwitch::resized()
{
    auto r = getLocalBounds().withTrimmedTop (20).reduced (3);
    normal.setBounds (r.removeFromLeft (r.getWidth() * 44 / 100));
    r.removeFromLeft (2);
    topBoost.setBounds (r);
}

void ChannelSwitch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panelLabel);
    drawTracked (g, "CHANNEL", Fonts::label (10.5f), 0.1f, r.removeFromTop (14.0f), juce::Justification::left);
    r.removeFromTop (6.0f);
    g.setColour (juce::Colour (0xff0e0e10));
    g.fillRoundedRectangle (r, 6.0f);
}

void ChannelSwitch::Segment::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    if (on)
    {
        g.setColour (colours::accent);
        g.fillRoundedRectangle (r, 4.0f);
    }
    else if (over)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 4.0f);
    }
    g.setColour (on ? juce::Colours::white : colours::muted);
    drawTracked (g, getButtonText(), Fonts::label (10.0f), 0.08f, r, juce::Justification::centred);
}

//==============================================================================
CabCard::CabCard (StompBoxProcessor& p) : processor (p)
{
    loadButton.onClick = [this] { chooseFile(); };
    clearButton.onClick = [this] { processor.clearIR(); error.clear(); refresh(); };
    loadButton.setTooltip ("Load a WAV or AIFF cab impulse response. It's copied into the project, so it travels with it.");
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    refresh();
    startTimerHz (5);
}

void CabCard::timerCallback()
{
    if (processor.getIRVersion() != shownVersion)
        refresh();
}

void CabCard::refresh()
{
    shownVersion = processor.getIRVersion();
    const auto ir = processor.getIR();
    if (ir.isValid())
    {
        irName = ir.name;
        irDetail = juce::String (ir.samples.getNumSamples() * 1000.0 / ir.sampleRate, 0) + " ms  " + dot + "  "
                 + juce::String (ir.sampleRate / 1000.0, 1) + " kHz  " + dot + "  applied to L and R";
        overview = ir.overview (48);
    }
    else
    {
        irName = "No IR loaded";
        irDetail = "Using a basic speaker filter. Load your York Audio or ToneFactor IR.";
        overview.clear();
    }
    clearButton.setEnabled (ir.isValid());
    repaint();
}

void CabCard::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a cab IR", processor.getLastIRFolder(), "*.wav;*.aif;*.aiff");
    juce::Component::SafePointer<CabCard> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        const auto r = safe->processor.loadIRFile (file);
        safe->error = r.failed() ? r.getErrorMessage() : juce::String();
        safe->refresh();
    });
}

void CabCard::resized()
{
    auto r = getLocalBounds().reduced (18);
    r.removeFromTop (14 + 12 + 40 + 12 + 44 + 12);
    auto row = r.removeFromTop (34);
    loadButton.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (8);
    clearButton.setBounds (row.removeFromLeft (70));
}

void CabCard::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::card);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (colours::cardBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    auto r = bounds.reduced (18.0f);
    g.setColour (colours::muted);
    drawTracked (g, "CAB IR", Fonts::label (11.0f), 0.12f, r.removeFromTop (14.0f), juce::Justification::left);
    r.removeFromTop (12.0f);

    auto nameArea = r.removeFromTop (40.0f);
    g.setColour (colours::text);
    g.setFont (Fonts::ui (15.0f, true));
    g.drawText (irName, nameArea.removeFromTop (20.0f), juce::Justification::left, true);
    g.setColour (colours::muted);
    g.setFont (Fonts::ui (12.0f));
    g.drawFittedText (irDetail, nameArea.toNearestInt(), juce::Justification::topLeft, 2);
    r.removeFromTop (12.0f);

    // IR overview
    auto wave = r.removeFromTop (44.0f);
    g.setColour (juce::Colour (0xff141416));
    g.fillRoundedRectangle (wave, 6.0f);
    if (! overview.empty())
    {
        auto inner = wave.reduced (10.0f, 7.0f);
        const float bw = inner.getWidth() / (float) overview.size();
        for (size_t i = 0; i < overview.size(); ++i)
        {
            const float v = juce::jlimit (0.04f, 1.0f, std::sqrt (overview[i]));
            const float t = (float) i / (float) overview.size();
            g.setColour (colours::accent.interpolatedWith (juce::Colour (0xff3a2f1d), t));
            g.fillRoundedRectangle (inner.getX() + i * bw, inner.getCentreY() - v * inner.getHeight() * 0.5f,
                                    juce::jmax (1.5f, bw - 2.0f), v * inner.getHeight(), 1.0f);
        }
    }
    else
    {
        g.setColour (colours::faint);
        g.setFont (Fonts::ui (12.0f));
        g.drawText ("Empty", wave, juce::Justification::centred);
    }
    r.removeFromTop (12.0f + 34.0f + 12.0f);

    g.setFont (Fonts::ui (12.0f));
    if (error.isNotEmpty())
    {
        g.setColour (juce::Colour (0xffe07a6f));
        g.drawFittedText (error, r.toNearestInt(), juce::Justification::topLeft, 3);
    }
    else
    {
        g.setColour (colours::faint);
        g.drawFittedText ("Saved inside the project, so it travels with it.", r.toNearestInt(), juce::Justification::topLeft, 2);
    }
}

//==============================================================================
OutputCard::OutputCard (juce::AudioProcessorValueTreeState& state)
    : attachment (state, "outputLevel", slider)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setTitle ("Output level");
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void OutputCard::resized()
{
    slider.setBounds (getLocalBounds().reduced (18).withTrimmedTop (14 + 12 + 18 + 4).removeFromTop (24));
}

void OutputCard::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::card);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (colours::cardBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    auto r = bounds.reduced (18.0f);
    g.setColour (colours::muted);
    drawTracked (g, "OUTPUT", Fonts::label (11.0f), 0.12f, r.removeFromTop (14.0f), juce::Justification::left);
    r.removeFromTop (12.0f);
    const double v = slider.getValue();
    g.setColour (colours::textSoft);
    g.setFont (Fonts::ui (12.5f));
    g.drawText ("Level  " + dot + "  " + juce::String (v > 0 ? "+" : "") + juce::String (v, 1) + " dB",
                r.removeFromTop (18.0f), juce::Justification::left);
}

//==============================================================================
AmpCabTab::AmpCabTab (StompBoxProcessor& p)
    : processor (p),
      channel (*p.apvts.getParameter ("ampChannel")),
      volume (p.apvts, "ampVolume", "VOLUME"),
      treble (p.apvts, "ampTreble", "TREBLE"),
      bass   (p.apvts, "ampBass",   "BASS"),
      cut    (p.apvts, "ampCut",    "CUT"),
      master (p.apvts, "ampMaster", "MASTER"),
      cabCard (p),
      outputCard (p.apvts)
{
    for (auto* c : std::initializer_list<juce::Component*> { &channel, &volume, &treble, &bass, &cut, &master, &cabCard, &outputCard })
        addAndMakeVisible (c);
}

juce::Rectangle<float> AmpCabTab::ampBounds() const
{
    const auto main = getLocalBounds().withTrimmedRight (asideWidth).toFloat();
    return juce::Rectangle<float> (680.0f, 430.0f).withCentre ({ main.getCentreX(), main.getCentreY() - 24.0f });
}

juce::Rectangle<float> AmpCabTab::controlPanelBounds() const
{
    return ampBounds().reduced (20.0f).removeFromTop (112.0f);
}

void AmpCabTab::resized()
{
    const auto panel = controlPanelBounds().toNearestInt();
    channel.setBounds (panel.getX() + 26, panel.getCentreY() - 24, 164, 48);

    const int knobW = 48, gap = 24, labelH = 22;
    const int total = 5 * knobW + 4 * gap;
    int x = panel.getX() + 222;
    jassert (x + total < (int) panel.getRight() - 70);
    juce::ignoreUnused (total);
    const int y = panel.getCentreY() - (knobW + labelH) / 2 + 4;
    for (auto* k : { &volume, &treble, &bass, &cut, &master })
    {
        k->setBounds (x, y, knobW, knobW + labelH);
        x += knobW + gap;
    }

    auto aside = getLocalBounds().removeFromRight (asideWidth).reduced (22, 24);
    cabCard.setBounds (aside.removeFromTop (262));
    aside.removeFromTop (16);
    outputCard.setBounds (aside.removeFromTop (102));
}

void AmpCabTab::paint (juce::Graphics& g)
{
    const auto main = getLocalBounds().withTrimmedRight (asideWidth).toFloat();

    // --- Room: wall over a wooden floor ---------------------------------------
    const float floorY = main.getY() + main.getHeight() * 0.64f;
    g.setColour (juce::Colour (0xff495049));
    g.fillRect (main.withBottom (floorY));
    for (float x = main.getX(), i = 0; x < main.getRight(); x += 180.0f, ++i)
    {
        g.setColour ((int) i % 2 == 0 ? juce::Colour (0xff7a5435) : juce::Colour (0xff6f4a2e));
        g.fillRect (juce::Rectangle<float> (x, floorY, 180.0f, main.getBottom() - floorY));
        g.setColour (juce::Colour (0xff5a3a22));
        g.fillRect (juce::Rectangle<float> (x + 177.0f, floorY, 3.0f, main.getBottom() - floorY));
    }
    g.setColour (juce::Colour (0xff3a2a1e));
    g.fillRect (main.getX(), floorY, main.getWidth(), main.getHeight() * 0.01f);

    // --- The amp ------------------------------------------------------------------
    const auto amp = ampBounds();

    // floor shadow
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (amp.getX() + 20.0f, amp.getBottom() - 6.0f, amp.getWidth() - 40.0f, 26.0f);

    // handle
    g.setColour (juce::Colour (0xff141415));
    g.fillRoundedRectangle (juce::Rectangle<float> (190.0f, 24.0f).withCentre ({ amp.getCentreX(), amp.getY() - 6.0f }), 10.0f);

    // feet
    g.setColour (juce::Colour (0xff0f0f10));
    g.fillRoundedRectangle (amp.getX() + 30.0f, amp.getBottom() - 4.0f, 56.0f, 12.0f, 5.0f);
    g.fillRoundedRectangle (amp.getRight() - 86.0f, amp.getBottom() - 4.0f, 56.0f, 12.0f, 5.0f);

    // cabinet
    g.setColour (colours::ampUpper);
    g.fillRoundedRectangle (amp, 14.0f);
    g.setColour (juce::Colour (0xff1b1b1d));
    g.drawRoundedRectangle (amp.reduced (2.0f), 13.0f, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) amp.getY() + 4, amp.getX() + 14.0f, amp.getRight() - 14.0f);

    // control panel
    const auto panel = controlPanelBounds();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1c1e), 0.0f, panel.getY(),
                                             juce::Colour (0xff141416), 0.0f, panel.getBottom(), false));
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);

    // ON lamp
    const auto lamp = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ panel.getRight() - 40.0f, panel.getCentreY() - 8.0f });
    g.setColour (juce::Colour (0xffff4a3d).withAlpha (0.35f));
    g.fillEllipse (lamp.expanded (5.0f));
    g.setColour (juce::Colour (0xffff4a3d));
    g.fillEllipse (lamp);
    g.setColour (colours::panelLabel);
    drawTracked (g, "ON", Fonts::label (10.5f), 0.1f, juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ lamp.getCentreX(), lamp.getBottom() + 16.0f }),
                 juce::Justification::centred);

    // grille
    auto grille = amp.reduced (20.0f).withTrimmedTop (112.0f + 18.0f);
    g.setColour (juce::Colour (0xff1b1b1d));
    g.fillRoundedRectangle (grille, 8.0f);
    auto cloth = grille.reduced (3.0f);
    g.setColour (juce::Colour (0xffc9bfa8));   // piping
    g.fillRoundedRectangle (cloth, 7.0f);
    cloth = cloth.reduced (3.0f);
    g.setColour (juce::Colour (0xff1b1b1d));
    g.fillRoundedRectangle (cloth, 6.0f);
    cloth = cloth.reduced (2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff734240), cloth.getCentreX(), cloth.getY() + cloth.getHeight() * 0.4f,
                                             juce::Colour (0xff4f2a29), cloth.getRight(), cloth.getBottom(), true));
    g.fillRoundedRectangle (cloth, 5.0f);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (cloth.toNearestInt());
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        for (float y = cloth.getY(); y < cloth.getBottom(); y += 4.0f) g.drawHorizontalLine ((int) y, cloth.getX(), cloth.getRight());
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        for (float x = cloth.getX(); x < cloth.getRight(); x += 4.0f) g.drawVerticalLine ((int) x, cloth.getY(), cloth.getBottom());
    }

    // badge
    {
        const auto font = Fonts::lettering (19.0f);
        const auto badge = juce::Rectangle<float> (170.0f, 38.0f).withCentre ({ cloth.getCentreX(), cloth.getY() + 46.0f });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (badge.translated (0.0f, 3.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (colours::cream, 0.0f, badge.getY(), juce::Colour (0xffd8cdb3), 0.0f, badge.getBottom(), false));
        g.fillRoundedRectangle (badge, 6.0f);
        g.setColour (colours::ampUpper);
        drawTracked (g, "ROYAL 30", font, 0.14f, badge, juce::Justification::centred);
    }

    // --- caption ------------------------------------------------------------------
    {
        const juce::String text = "Two identical amps, one per side (L and R)  " + dot + "  controls are linked";
        const auto font = Fonts::ui (13.0f);
        const float w = juce::GlyphArrangement::getStringWidth (font, text) + 70.0f;
        auto pill = juce::Rectangle<float> (w, 34.0f).withCentre ({ main.getCentreX(), amp.getBottom() + 46.0f });
        g.setColour (juce::Colour (0xbf140e0a));
        g.fillRoundedRectangle (pill, 17.0f);
        auto inner = pill.reduced (16.0f, 0.0f);
        g.setColour (colours::accent);
        g.setFont (Fonts::ui (13.0f, true));
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97" "2"), inner.removeFromLeft (26.0f), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xfff1e7d8));
        g.setFont (font);
        g.drawText (text, inner, juce::Justification::centredLeft);
    }

    // --- aside --------------------------------------------------------------------
    const auto aside = getLocalBounds().removeFromRight (asideWidth).toFloat();
    g.setColour (colours::aside);
    g.fillRect (aside);
    g.setColour (colours::headerLine);
    g.drawVerticalLine ((int) aside.getX(), aside.getY(), aside.getBottom());
}

} // namespace stompbox::ui
