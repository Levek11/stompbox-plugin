#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "board/BoardModel.h"

namespace stompbox::ui
{

/** Draws any pedal from its catalog entry, at any size: on the board, on the
    shelf and in the close-up. One knob design and one footswitch design are
    shared by every pedal; only their colours change. */
struct PedalPainter
{
    struct Look
    {
        bool hover = false;            // subtle light overlay + outline ("click to open")
        int footswitchHover = -1;      // which footswitch shows its hover ring
        bool showCategory = true;      // under the footswitch (board, close-up)
        bool showLabels = false;       // control labels (close-up)
        int activeControl = -1;        // close-up: control being adjusted (its label shows the value)
        float alpha = 1.0f;
        std::array<float, 4> live {};  // live readings from the engine (tuner, meter)
        int liveCount = 0;             // 0 = none (shelf, drag): draw the static look
    };

    static void draw (juce::Graphics&, const board::PedalDef&, const board::BoardModel::PedalState&,
                      juce::Rectangle<float> bounds, const Look&);

    /** Transform from pedal design space to the given bounds. */
    static juce::AffineTransform transformFor (const board::PedalDef&, juce::Rectangle<float> bounds);

    /** Footswitch centre in design space. */
    static juce::Point<float> footswitchCentre (const board::PedalDef&, int which);
    static constexpr float footswitchRadius = 15.0f;
    static constexpr float ledY = 105.0f, footswitchY = 128.0f, categoryY = 153.0f;

    /** Hit test in design space: control index under a point, or -1. */
    static int controlAt (const board::PedalDef&, juce::Point<float> designPoint);

    /** Value shown for a control: 0-10 for knobs and sliders, position name for
        toggles, milliseconds for time knobs ("SYNC" while synced to Logic). */
    static juce::String valueText (const board::PedalDef&, const board::BoardModel::PedalState&, int control);
};

/** "BLUE\nHIGHWAY" -> "Blue Highway". */
inline juce::String displayName (const board::PedalDef& def)
{
    auto words = juce::StringArray::fromTokens (juce::String (def.name).replace ("\n", " "), " ", "");
    for (auto& w : words) w = w.substring (0, 1) + w.substring (1).toLowerCase();
    return words.joinIntoString (" ");
}

/** Rotation of a knob pointer for a 0..1 value. */
inline float knobAngle (float value) { return juce::degreesToRadians (-135.0f + 270.0f * value); }

} // namespace stompbox::ui
