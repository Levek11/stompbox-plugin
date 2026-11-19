#include "PedalCloseUp.h"
#include "StompLookAndFeel.h"
#include "../PluginProcessor.h"

namespace stompbox::ui
{

using namespace board;

static const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00b7);

static juce::String soundNote (const PedalDef& def)
{
    switch (def.soundBatch)
    {
        case 2:  return "Not modeled yet: its sound arrives in step 2b";
        default: return "Not modeled yet: its sound arrives in step " + juce::String (def.soundBatch);
    }
}

PedalCloseUp::PedalCloseUp (StompBoxProcessor& p, int pedalIndex, std::function<void()> close)
    : processor (p), index (pedalIndex), onClose (std::move (close))
{
    setWantsKeyboardFocus (true);
    setTitle (displayName (catalog()[(size_t) index]) + " close-up");
}

juce::Rectangle<float> PedalCloseUp::pedalBounds() const
{
    const auto& def = catalog()[(size_t) index];
    const float s = juce::jmin (3.0f, ((float) getHeight() - 200.0f) / PedalDef::height);
    return juce::Rectangle<float> (def.width() * s, PedalDef::height * s)
        .withCentre (getLocalBounds().toFloat().getCentre());
}

juce::Point<float> PedalCloseUp::toDesign (juce::Point<float> local) const
{
    const auto& def = catalog()[(size_t) index];
    return local.transformedBy (PedalPainter::transformFor (def, pedalBounds()).inverted());
}

int PedalCloseUp::footswitchAt (juce::Point<float> local) const
{
    const auto& def = catalog()[(size_t) index];
    const auto d = toDesign (local);
    for (int f = 0; f < def.footswitches; ++f)
        if (d.getDistanceFrom (PedalPainter::footswitchCentre (def, f)) <= PedalPainter::footswitchRadius + 2.0f)
            return f;
    return -1;
}

float PedalCloseUp::valueOf (int control) const
{
    return processor.getBoard().pedal (index).values[(size_t) control];
}

void PedalCloseUp::setValue (int control, float v)
{
    const auto& c = catalog()[(size_t) index].controls[(size_t) control];
    if (c.positions > 1)   // toggles and stepped knobs snap
    {
        const float steps = (float) (c.positions - 1);
        v = std::round (v * steps) / steps;
    }
    processor.editBoard ([&] (BoardModel& b) { b.setValue (index, control, v); });
    repaint();
}

//==============================================================================
void PedalCloseUp::paint (juce::Graphics& g)
{
    const auto& def = catalog()[(size_t) index];
    const auto board = processor.getBoard();

    g.fillAll (juce::Colour (0x5e080604));   // dim the board behind

    const auto pedal = pedalBounds();

    // Title pill
    {
        const juce::String name = displayName (def);
        const juce::String sub = juce::String (def.category).substring (0, 1) + juce::String (def.category).substring (1).toLowerCase()
                               + "  " + dot + "  inspired by the " + def.inspiredBy;
        const auto nameFont = Fonts::ui (18.0f, true), subFont = Fonts::ui (13.0f);
        const float nw = juce::GlyphArrangement::getStringWidth (nameFont, name);
        const float sw = juce::GlyphArrangement::getStringWidth (subFont, sub);
        auto pill = juce::Rectangle<float> (nw + sw + 12.0f + 36.0f, 40.0f).withCentre ({ pedal.getCentreX(), pedal.getY() - 22.0f - 20.0f });
        g.setColour (juce::Colour (0xd9120e0a));
        g.fillRoundedRectangle (pill, 20.0f);
        auto inner = pill.reduced (18.0f, 0.0f);
        g.setColour (colours::text);
        g.setFont (nameFont);
        g.drawText (name, inner.removeFromLeft (nw), juce::Justification::centredLeft);
        inner.removeFromLeft (12.0f);
        g.setColour (colours::textSoft);
        g.setFont (subFont);
        g.drawText (sub, inner.translated (0.0f, 1.5f), juce::Justification::centredLeft);
    }

    // The pedal
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 60, { 0, 30 }).drawForRectangle (g, pedal.toNearestInt());
        PedalPainter::Look look;
        look.showLabels = true;
        look.activeControl = activeControl >= 0 ? activeControl : hoverControl;
        look.footswitchHover = hoverFootswitch;
        look.liveCount = processor.pedalTelemetry (index, look.live.data());
        PedalPainter::draw (g, def, board.pedal (index), pedal, look);
    }

    // Help pill
    {
        const juce::String a = "Drag a knob to adjust  " + dot + "  double-click to reset";
        const juce::String b = "Click outside to return to the board";
        const juce::String c = def.soundBatch >= 2 ? soundNote (def) : juce::String();
        const auto font = Fonts::ui (13.0f);
        const float aw = juce::GlyphArrangement::getStringWidth (font, a);
        const float bw = juce::GlyphArrangement::getStringWidth (font, b);
        const float cw = c.isNotEmpty() ? juce::GlyphArrangement::getStringWidth (font, c) + 48.0f : 0.0f;
        auto pill = juce::Rectangle<float> (cw + aw + bw + 48.0f + 36.0f, 40.0f).withCentre ({ pedal.getCentreX(), pedal.getBottom() + 22.0f + 20.0f });
        g.setColour (juce::Colour (0xd9120e0a));
        g.fillRoundedRectangle (pill, 20.0f);
        auto inner = pill.reduced (18.0f, 0.0f);
        g.setFont (font);
        if (c.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffe9b38a));
            g.drawText (c, inner.removeFromLeft (cw - 48.0f), juce::Justification::centredLeft);
            g.setColour (colours::faint);
            g.drawText ("|", inner.removeFromLeft (48.0f), juce::Justification::centred);
        }
        g.setColour (colours::textSoft);
        g.drawText (a, inner.removeFromLeft (aw), juce::Justification::centredLeft);
        g.setColour (colours::faint);
        g.drawText ("|", inner.removeFromLeft (48.0f), juce::Justification::centred);
        g.setColour (colours::textSoft);
        g.drawText (b, inner, juce::Justification::centredLeft);
    }
}

//==============================================================================
void PedalCloseUp::mouseMove (const juce::MouseEvent& e)
{
    const auto& def = catalog()[(size_t) index];
    const int c = pedalBounds().contains (e.position) ? PedalPainter::controlAt (def, toDesign (e.position)) : -1;
    const int f = footswitchAt (e.position);
    if (c != hoverControl || f != hoverFootswitch)
    {
        hoverControl = c; hoverFootswitch = f;
        setMouseCursor (c >= 0 ? (def.controls[(size_t) c].kind == ControlDef::Kind::toggle
                                    ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor)
                        : f >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PedalCloseUp::mouseDown (const juce::MouseEvent& e)
{
    const auto& def = catalog()[(size_t) index];
    pressedOutside = ! pedalBounds().contains (e.position);
    if (pressedOutside)
        return;

    if (const int f = footswitchAt (e.position); f >= 0)
    {
        processor.editBoard ([&] (BoardModel& b)
        {
            const auto& st = b.pedal (index);
            b.setOn (index, ! (f == 0 ? st.on : st.onB), f);
        });
        repaint();
        return;
    }

    const int c = PedalPainter::controlAt (def, toDesign (e.position));
    if (c < 0)
        return;

    const auto& ctl = def.controls[(size_t) c];
    if (ctl.kind == ControlDef::Kind::toggle)
    {
        // Click cycles through the positions, top to bottom.
        const int pos = juce::roundToInt (valueOf (c) * (float) (ctl.positions - 1));
        setValue (c, (float) ((pos + 1) % ctl.positions) / (float) (ctl.positions - 1));
        activeControl = c;
        return;
    }

    activeControl = c;
    dragStartValue = valueOf (c);
    dragStartY = e.position.y;
    repaint();
}

void PedalCloseUp::mouseDrag (const juce::MouseEvent& e)
{
    if (activeControl < 0) return;
    const auto& ctl = catalog()[(size_t) index].controls[(size_t) activeControl];
    if (ctl.kind == ControlDef::Kind::toggle) return;

    // Knobs: 220 px of vertical drag for the full range (shift for fine).
    // Sliders: follow the cap.
    const float dy = dragStartY - e.position.y;
    float range = ctl.kind == ControlDef::Kind::slider
                      ? (ctl.size - 6.0f) * pedalBounds().getWidth() / catalog()[(size_t) index].width()
                      : 220.0f;
    if (e.mods.isShiftDown()) range *= 4.0f;
    setValue (activeControl, juce::jlimit (0.0f, 1.0f, dragStartValue + dy / range));
}

void PedalCloseUp::mouseUp (const juce::MouseEvent& e)
{
    if (pressedOutside && ! pedalBounds().contains (e.position))
    {
        if (onClose) onClose();
        return;
    }
    activeControl = -1;
    repaint();
}

void PedalCloseUp::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! pedalBounds().contains (e.position)) return;
    const int c = PedalPainter::controlAt (catalog()[(size_t) index], toDesign (e.position));
    if (c >= 0 && catalog()[(size_t) index].controls[(size_t) c].kind != ControlDef::Kind::toggle)
    {
        processor.editBoard ([&] (BoardModel& b) { b.resetValue (index, c); });
        repaint();
    }
}

void PedalCloseUp::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! pedalBounds().contains (e.position)) return;
    const int c = PedalPainter::controlAt (catalog()[(size_t) index], toDesign (e.position));
    if (c < 0 || catalog()[(size_t) index].controls[(size_t) c].kind == ControlDef::Kind::toggle) return;
    const float delta = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    setValue (c, juce::jlimit (0.0f, 1.0f, valueOf (c) + delta * 0.25f));
}

bool PedalCloseUp::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (onClose) onClose();
        return true;
    }
    return false;
}

} // namespace stompbox::ui
