#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PedalPainter.h"

class StompBoxProcessor;

namespace stompbox::ui
{

/** The close-up: a large version of one pedal over the dimmed board, with
    live controls. Drag a knob (or slider) to adjust, double-click to reset,
    click a toggle to flip it, scroll over a control to nudge it. The
    footswitch works here too. Clicking outside the pedal closes it. */
class PedalCloseUp : public juce::Component
{
public:
    PedalCloseUp (StompBoxProcessor&, int pedalIndex, std::function<void()> onClose);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> pedalBounds() const;
    juce::Point<float> toDesign (juce::Point<float> local) const;
    int footswitchAt (juce::Point<float> local) const;
    float valueOf (int control) const;
    void setValue (int control, float v);

    StompBoxProcessor& processor;
    const int index;
    std::function<void()> onClose;

    int hoverControl = -1, hoverFootswitch = -1;
    int activeControl = -1;
    float dragStartValue = 0.0f;
    float dragStartY = 0.0f;
    bool pressedOutside = false;
};

} // namespace stompbox::ui
