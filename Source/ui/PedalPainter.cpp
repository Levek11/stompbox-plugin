#include "PedalPainter.h"
#include "board/Machines.h"
#include "StompLookAndFeel.h"

namespace stompbox::ui
{

using namespace board;

static juce::Colour col (std::uint32_t argb) { return juce::Colour (argb); }

juce::AffineTransform PedalPainter::transformFor (const PedalDef& def, juce::Rectangle<float> b)
{
    const float s = b.getWidth() / def.width();
    return juce::AffineTransform::scale (s).translated (b.getX(), b.getY());
}

juce::Point<float> PedalPainter::footswitchCentre (const PedalDef& def, int which)
{
    if (def.footswitches == 2)
        return { which == 0 ? 68.0f : 172.0f, footswitchY };
    return { def.width() * 0.5f, footswitchY };
}

int PedalPainter::controlAt (const PedalDef& def, juce::Point<float> p)
{
    for (size_t i = 0; i < def.controls.size(); ++i)
    {
        const auto& c = def.controls[i];
        juce::Rectangle<float> r;
        switch (c.kind)
        {
            case ControlDef::Kind::knob:   r = juce::Rectangle<float> (c.size, c.size).withCentre ({ c.x, c.y }).expanded (2.0f); break;
            case ControlDef::Kind::toggle: r = juce::Rectangle<float> (12.0f, c.size + 6.0f).withCentre ({ c.x, c.y }); break;
            case ControlDef::Kind::slider: r = juce::Rectangle<float> (10.0f, c.size + 6.0f).withCentre ({ c.x, c.y }); break;
        }
        if (r.contains (p))
            return (int) i;
    }
    return -1;
}

//==============================================================================
// Open Sky and Time Machine: knobs whose meaning depends on the machine.
namespace mc = stompbox::board::machines;

static bool isBigDigital (const PedalDef& def)
{
    return std::strcmp (def.id, "openSky") == 0 || std::strcmp (def.id, "timeMachine") == 0;
}

static float valueOf (const PedalDef& def, const BoardModel::PedalState& st, const char* id)
{
    for (size_t k = 0; k < def.controls.size(); ++k)
        if (std::strcmp (def.controls[k].id, id) == 0)
            return k < st.values.size() ? st.values[k] : def.controls[k].defaultValue;
    return 0.0f;
}

/** The role of a machine-dependent knob, or nullptr if it's a fixed one. */
static const mc::KnobRole* roleOf (const PedalDef& def, const BoardModel::PedalState& st, const char* id)
{
    if (std::strcmp (def.id, "openSky") == 0)
    {
        const int m = mc::stepIndex (valueOf (def, st, "type"), mc::numReverbs);
        if (std::strcmp (id, "param1") == 0) return &mc::reverbParams[m][0];
        if (std::strcmp (id, "param2") == 0) return &mc::reverbParams[m][1];
    }
    else if (std::strcmp (def.id, "timeMachine") == 0)
    {
        const int m = mc::stepIndex (valueOf (def, st, "type"), mc::numDelays);
        static const char* const ids[5] = { "filter", "grit", "speed", "depth", "tweak" };
        for (int k = 0; k < 5; ++k)
            if (std::strcmp (id, ids[k]) == 0) return &mc::delayKnobs[m][k];
    }
    return nullptr;
}

static juce::String secondsText (double s)
{
    return s < 1.0 ? juce::String (juce::roundToInt (s * 1000.0)) + " ms" : juce::String (s, s < 10.0 ? 2 : 1) + " s";
}

static juce::String bigDigitalValue (const PedalDef& def, const BoardModel::PedalState& st, const ControlDef& c, float v)
{
    if (auto* role = roleOf (def, st, c.id))
    {
        if (const int steps = mc::countSteps (role->steps); steps > 0)
            return juce::StringArray::fromTokens (role->steps, "|", "")[mc::stepIndex (v, steps)];
        if (std::strcmp (role->label, "RISE") == 0)
            return secondsText (std::strcmp (def.id, "openSky") == 0 ? mc::riseSeconds (v) : mc::delayRiseSeconds (v));
        return juce::String (v * 10.0f, 1);
    }
    if (std::strcmp (def.id, "openSky") == 0)
    {
        const int m = mc::stepIndex (valueOf (def, st, "type"), mc::numReverbs);
        if (std::strcmp (c.id, "decay") == 0)    return secondsText (mc::decaySeconds (m, v));
        if (std::strcmp (c.id, "predelay") == 0) return secondsText (mc::preDelaySeconds (v));
        if (std::strcmp (c.id, "level") == 0)
        {
            const float db = (v - 0.5f) * 12.0f;
            return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
        }
    }
    else if (std::strcmp (c.id, "time") == 0)
    {
        const int m = mc::stepIndex (valueOf (def, st, "type"), mc::numDelays);
        if (valueOf (def, st, "sync") < 0.5f)
            return mc::divisionNames[mc::stepIndex (v, mc::numDivisions)];
        return juce::String (juce::roundToInt (mc::freeDelayMs (m, valueOf (def, st, "tweak"), v))) + " ms";
    }
    return juce::String (v * 10.0f, 1);
}

juce::String PedalPainter::labelText (const PedalDef& def, const BoardModel::PedalState& st, int index)
{
    const auto& c = def.controls[(size_t) index];
    if (auto* role = roleOf (def, st, c.id))
        return role->label;
    return c.label;
}

juce::String PedalPainter::displayText (const PedalDef& def, const BoardModel::PedalState& st)
{
    if (std::strcmp (def.id, "openSky") == 0)
    {
        const int m = mc::stepIndex (valueOf (def, st, "type"), mc::numReverbs);
        return juce::String (mc::reverbNames[m]) + "  " + secondsText (mc::decaySeconds (m, valueOf (def, st, "decay")));
    }
    if (std::strcmp (def.id, "timeMachine") == 0)
    {
        const int m = mc::stepIndex (valueOf (def, st, "type"), mc::numDelays);
        const float t = valueOf (def, st, "time");
        const juce::String time = valueOf (def, st, "sync") < 0.5f
                                    ? juce::String (mc::divisionNames[mc::stepIndex (t, mc::numDivisions)])
                                    : juce::String (juce::roundToInt (mc::freeDelayMs (m, valueOf (def, st, "tweak"), t))) + "ms";
        return juce::String (mc::delayNames[m]) + "  " + time;
    }
    return def.displayText;
}

juce::String PedalPainter::valueText (const PedalDef& def, const BoardModel::PedalState& st, int index)
{
    const auto& c = def.controls[(size_t) index];
    const float v = st.values[(size_t) index];
    if (isBigDigital (def) && c.kind == ControlDef::Kind::knob && c.positions <= 1)
        return bigDigitalValue (def, st, c, v);
    if (c.msMax > 0.0f)
    {
        for (size_t k = 0; k < def.controls.size(); ++k)
            if (std::strcmp (def.controls[k].id, c.lockedBy) == 0 && st.values[k] < 0.5f)
                return "SYNC";
        return juce::String (juce::roundToInt (c.msMin * std::pow (c.msMax / c.msMin, v))) + " ms";
    }
    if (c.kind == ControlDef::Kind::toggle || c.positions > 1)
    {
        const auto names = juce::StringArray::fromTokens (c.positionNames, "|", "");
        const int pos = juce::roundToInt (v * (float) (c.positions - 1));
        return juce::isPositiveAndBelow (pos, names.size()) ? names[pos] : juce::String (pos + 1);
    }
    if (c.kind == ControlDef::Kind::slider)
    {
        const float db = (v - 0.5f) * 30.0f;   // GE-7 style +-15 dB bands and level
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }
    return juce::String (v * 10.0f, 1);
}

//==============================================================================
static void drawKnob (juce::Graphics& g, const ControlDef& c, float value)
{
    const auto r = juce::Rectangle<float> (c.size, c.size).withCentre ({ c.x, c.y });

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (r.translated (0.0f, c.size * 0.07f));

    const auto cap = col (c.cap);
    g.setGradientFill (juce::ColourGradient (cap.brighter (0.35f), r.getX() + r.getWidth() * 0.4f, r.getY() + r.getHeight() * 0.3f,
                                             cap.darker (0.25f), r.getRight(), r.getBottom(), true));
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (r.reduced (0.6f).translated (0.0f, 0.3f), 0.6f);

    const float rad = c.size * 0.5f;
    juce::Path ptr;
    ptr.addRoundedRectangle (-c.size * 0.045f, -rad * 0.8f, c.size * 0.09f, rad * 0.62f, c.size * 0.04f);
    g.setColour (col (c.pointer));
    g.fillPath (ptr, juce::AffineTransform::rotation (knobAngle (value)).translated (c.x, c.y));
}

static void drawToggle (juce::Graphics& g, const ControlDef& c, float value)
{
    // Bat-handle toggle: dark collar, metal lever pointing to the position.
    const float h = c.size;
    const auto collar = juce::Rectangle<float> (h * 0.72f, h * 0.72f).withCentre ({ c.x, c.y });
    g.setColour (juce::Colour (0xff2a2a2c));
    g.fillEllipse (collar);

    const int pos = juce::roundToInt (value * (float) (c.positions - 1));
    const float t = c.positions > 1 ? (float) pos / (float) (c.positions - 1) : 0.0f;   // 0 = up
    const float offset = (t - 0.5f) * h * 0.55f;
    auto lever = juce::Rectangle<float> (h * 0.42f, h * 0.68f).withCentre ({ c.x, c.y + offset });
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (lever.translated (0.0f, h * 0.06f), h * 0.2f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f2), 0.0f, lever.getY(),
                                             juce::Colour (0xff9da0a5), 0.0f, lever.getBottom(), false));
    g.fillRoundedRectangle (lever, h * 0.2f);
}

static void drawSlider (juce::Graphics& g, const ControlDef& c, float value)
{
    const auto track = juce::Rectangle<float> (4.0f, c.size).withCentre ({ c.x, c.y });
    g.setColour (juce::Colour (0xff2a2622));
    g.fillRoundedRectangle (track, 2.0f);
    const float y = track.getBottom() - 3.0f - value * (c.size - 6.0f);
    const auto capR = juce::Rectangle<float> (10.0f, 6.0f).withCentre ({ c.x, y });
    g.setColour (juce::Colour (0xff1b1b1b));
    g.fillRoundedRectangle (capR, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.fillRect (capR.getX() + 1.0f, capR.getBottom() - 0.8f, capR.getWidth() - 2.0f, 0.8f);
}

static void drawFootswitch (juce::Graphics& g, juce::Point<float> c, float r, bool hover)
{
    const auto b = juce::Rectangle<float> (r * 2, r * 2).withCentre (c);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (b.translated (0.0f, 3.0f).expanded (0.5f));
    juce::ColourGradient grad (juce::Colour (0xfffbfbfb), b.getX() + b.getWidth() * 0.35f, b.getY() + b.getHeight() * 0.3f,
                               juce::Colour (0xff5c5f64), b.getRight(), b.getBottom(), true);
    grad.addColour (0.4, juce::Colour (0xffc3c6cb));
    grad.addColour (0.72, juce::Colour (0xff868a90));
    g.setGradientFill (grad);
    g.fillEllipse (b);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawEllipse (b.reduced (1.0f), 2.0f);
    if (hover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawEllipse (b.expanded (1.5f), 3.0f);
    }
}

static void drawLed (juce::Graphics& g, juce::Point<float> c, bool on)
{
    const auto b = juce::Rectangle<float> (6.0f, 6.0f).withCentre (c);
    if (on)
    {
        g.setColour (juce::Colour (0xffff4a3d).withAlpha (0.35f));
        g.fillEllipse (b.expanded (3.0f));
        g.setColour (juce::Colour (0xffff4a3d));
    }
    else
        g.setColour (juce::Colour (0xff4a1210));
    g.fillEllipse (b);
}

static void drawLettering (juce::Graphics& g, const juce::String& text, juce::Font font, float tracking,
                           juce::Colour colour, float centreY, float width)
{
    const auto lines = juce::StringArray::fromLines (text);
    const float lineH = font.getHeight() * 1.15f;
    float y = centreY - lineH * (float) lines.size() * 0.5f;
    g.setColour (colour);
    for (const auto& line : lines)
    {
        drawTracked (g, line, font, tracking, { 0.0f, y, width, lineH }, juce::Justification::centred);
        y += lineH;
    }
}

static void drawDecor (juce::Graphics& g, const PedalDef& def, const BoardModel::PedalState& st, const PedalPainter::Look& look)
{
    switch (def.decor)
    {
        case PedalDef::Decor::display:
        {
            const auto screen = juce::Rectangle<float> (15.0f, 14.0f, 78.0f, 22.0f);
            g.setColour (juce::Colour (0xff0c1418));
            g.fillRoundedRectangle (screen, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (screen.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (col (def.displayColour));
            drawTracked (g, PedalPainter::displayText (def, st), Fonts::ui (8.0f, true), 0.04f, screen, juce::Justification::centred);

            // Machine LEDs either side of the TYPE knob: 4 on the left, 3 on the right
            const int machine = mc::stepIndex (valueOf (def, st, "type"), 7);
            for (int side = 0; side < 2; ++side)
                for (int i = 0; i < (side == 0 ? 4 : 3); ++i)
                {
                    const bool lit = (side == 0 ? i : 4 + i) == machine;
                    const auto d = juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ side == 0 ? 33.0f : 75.0f, 45.0f + 7.0f * (float) i });
                    g.setColour (lit ? juce::Colour (0xff8dff7a) : juce::Colour (0xff0d3a47));
                    g.fillEllipse (d);
                }
            break;
        }
        case PedalDef::Decor::tuner:
        {
            const auto screen = juce::Rectangle<float> (14.0f, 10.0f, 84.0f, 40.0f);
            g.setColour (juce::Colour (0xff0d0f0e));
            g.fillRoundedRectangle (screen, 4.0f);

            // Static look on the shelf; live on the board (dark while bypassed).
            juce::String name = def.displayText;
            int lit = 3;              // centre LED
            bool show = true;
            if (look.liveCount >= 2)
            {
                show = look.live[1] > 0.5f;
                if (look.live[0] < 0.0f)
                {
                    name = "-";
                    lit = -1;
                }
                else
                {
                    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    const int n = juce::roundToInt (look.live[0]);
                    const float cents = (look.live[0] - (float) n) * 100.0f;
                    name = names[((n % 12) + 12) % 12];
                    // In tune (within 3 cents): the centre LED; else 1-3 LEDs out by 1/3/~17+ cents steps.
                    if (std::abs (cents) <= 3.0f) lit = 3;
                    else lit = 3 + (cents > 0.0f ? 1 : -1) * juce::jlimit (1, 3, 1 + (int) (std::abs (cents) / 17.0f));
                }
            }
            if (show)
            {
                const auto on = col (def.displayColour), sharpFlat = juce::Colour (0xffff5a4a);
                g.setColour (lit == 3 ? on : sharpFlat);
                g.setFont (Fonts::ui (15.0f, true));
                g.drawText (name, screen.withHeight (26.0f).translated (0.0f, 3.0f), juce::Justification::centred);
                for (int i = 0; i < 7; ++i)
                {
                    g.setColour (i == lit ? (i == 3 ? on : sharpFlat) : juce::Colour (0xff3a3a3a));
                    g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ 38.0f + 6.0f * (float) i, 39.0f }));
                }
            }
            break;
        }
        case PedalDef::Decor::meter:
        {
            // Gain-reduction meter: 2 dB a dot, green / amber / red.
            const int litDots = look.liveCount >= 1 ? juce::jlimit (0, 10, juce::roundToInt (look.live[0] / 2.0f)) : -1;
            for (int i = 0; i < 10; ++i)
            {
                auto c = col (def.displayColour);
                if (i < litDots)
                    c = i < 5 ? juce::Colour (0xff6dff6a) : i < 8 ? juce::Colour (0xffffc23a) : juce::Colour (0xffff4a3a);
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ 24.5f + 7.0f * (float) i, 64.0f }));
            }
            break;
        }
        case PedalDef::Decor::none:
            break;
    }
}

//==============================================================================
void PedalPainter::draw (juce::Graphics& g, const PedalDef& def, const BoardModel::PedalState& st,
                         juce::Rectangle<float> bounds, const Look& look)
{
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (transformFor (def, bounds));
    if (look.alpha < 1.0f)
        g.beginTransparencyLayer (look.alpha);

    const float w = def.width(), h = PedalDef::height;
    const auto body = juce::Rectangle<float> (0.0f, 0.0f, w, h);
    const auto bodyColour = col (def.body);
    const auto text = col (def.text);

    // Shadow, enclosure, bevel
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 14, { 0, 8 }).drawForRectangle (g, body.toNearestInt());
    g.setColour (bodyColour);
    g.fillRoundedRectangle (body, 10.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0.0f, 0.0f,
                                             juce::Colours::black.withAlpha (0.08f), 0.0f, h, false));
    g.fillRoundedRectangle (body, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawHorizontalLine (1, 8.0f, w - 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, h - 3.0f, w - 8.0f, 3.0f), 1.5f);

    drawDecor (g, def, st, look);

    // Controls
    for (size_t i = 0; i < def.controls.size(); ++i)
    {
        const auto& c = def.controls[i];
        const float v = i < st.values.size() ? st.values[i] : c.defaultValue;
        switch (c.kind)
        {
            case ControlDef::Kind::knob:   drawKnob (g, c, v); break;
            case ControlDef::Kind::toggle: drawToggle (g, c, v); break;
            case ControlDef::Kind::slider: drawSlider (g, c, v); break;
        }

        if (look.showLabels)
        {
            const bool active = look.activeControl == (int) i;
            const float below = c.kind == ControlDef::Kind::knob ? c.size * 0.5f + 1.5f
                              : c.kind == ControlDef::Kind::toggle ? c.size * 0.5f + 2.5f
                              : c.size * 0.5f + 1.5f;
            const float fontH = c.kind == ControlDef::Kind::slider ? 2.9f : 3.4f;
            const float labelW = c.kind == ControlDef::Kind::slider ? 14.0f : 34.0f;
            auto area = juce::Rectangle<float> (labelW, fontH + 1.5f).withCentre ({ c.x, c.y + below + fontH * 0.6f });
            if (active)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (area.expanded (1.5f, 0.6f), 1.5f);
            }
            g.setColour (active ? juce::Colours::white : text.withAlpha (0.9f));
            drawTracked (g, active ? PedalPainter::valueText (def, st, (int) i) : PedalPainter::labelText (def, st, (int) i),
                         Fonts::label (fontH), 0.08f, area, juce::Justification::centred);
        }
    }

    // Name
    drawLettering (g, def.name, Fonts::lettering (def.nameSize), def.slots == 2 ? 0.08f : 0.05f, text, def.nameY, w);

    // LED(s), footswitch(es), category
    for (int f = 0; f < def.footswitches; ++f)
    {
        const auto fs = footswitchCentre (def, f);
        drawLed (g, { fs.x, ledY }, f == 0 ? st.on : st.onB);
        drawFootswitch (g, fs, footswitchRadius, look.footswitchHover == f);
    }
    if (look.showCategory)
    {
        g.setColour (text.withAlpha (0.8f));
        drawTracked (g, def.category, Fonts::label (8.5f), 0.16f, { 0.0f, categoryY - 5.0f, w, 10.0f }, juce::Justification::centred);
    }

    // "Click to open" hover
    if (look.hover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (body, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, w, h).expanded (4.0f), 13.0f, 2.0f);
    }

    if (look.alpha < 1.0f)
        g.endTransparencyLayer();
}

} // namespace stompbox::ui
