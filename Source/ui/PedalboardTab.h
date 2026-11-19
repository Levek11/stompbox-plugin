#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "board/BoardModel.h"
#include "PedalPainter.h"

class StompBoxProcessor;

namespace stompbox::ui
{

class PedalCloseUp;

/** The Pedalboard tab: the board on its rug with auto-routed cables, and the
    pedal shelf. Pedals are dragged between the shelf and the board; clicking a
    pedal on the board opens its close-up; its footswitch toggles it. */
class PedalboardTab : public juce::Component, private juce::Timer
{
public:
    explicit PedalboardTab (StompBoxProcessor&);
    ~PedalboardTab() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Brief note in the caption (e.g. after resizing the board). */
    void showNote (const juce::String& text);

    static constexpr int shelfWidth = 360;

private:
    void timerCallback() override;
    std::array<std::array<float, 4>, (size_t) stompbox::board::numPedals> shownLive {};
    void refreshModel();
    void updateGeometry();

    // --- Geometry (board space: origin at the board's top-left, unscaled) ---
    juce::AffineTransform boardToLocal() const;
    juce::Point<float> localToBoard (juce::Point<float>) const;
    juce::Rectangle<float> slotRectBoard (int slot, int widthSlots) const;
    juce::Rectangle<float> pedalRectBoard (int index) const;
    juce::Rectangle<float> pedalRectLocal (int index) const;
    juce::Rectangle<float> shelfRect (int index) const;          // local
    juce::Rectangle<float> mainArea() const;

    float boardW() const { return 56.0f + (float) model.columns() * 112.0f + (float) (model.columns() - 1) * 16.0f; }
    float boardH() const { return 56.0f + (float) model.rows() * 168.0f + (float) (model.rows() - 1) * 40.0f; }
    float rowTop (int r) const { return 28.0f + (float) r * 208.0f; }

    // --- Hit testing --------------------------------------------------------
    int boardPedalAt (juce::Point<float> local) const;
    int shelfPedalAt (juce::Point<float> local) const;
    int footswitchAt (int index, juce::Point<float> local) const;

    // --- Drawing ------------------------------------------------------------
    void drawFloor (juce::Graphics&);
    void drawRugAndBoard (juce::Graphics&);
    void drawCables (juce::Graphics&);
    void drawJacks (juce::Graphics&);
    void drawDropTargets (juce::Graphics&);
    void drawShelf (juce::Graphics&);
    void drawGhost (juce::Graphics&);
    void drawCaption (juce::Graphics&);

    // --- Drag ---------------------------------------------------------------
    void updateDragTarget (juce::Point<float> local);
    board::BoardModel liftedModel() const;

    StompBoxProcessor& processor;
    board::BoardModel model;
    int shownVersion = -1;

    float scale = 1.0f;
    juce::Point<float> boardOrigin;   // local position of the board's top-left

    int hoverPedal = -1, hoverFootswitch = -1, hoverShelf = -1;

    struct Press
    {
        int pedal = -1;
        int footswitch = -1;
        bool fromShelf = false;
        juce::Point<float> at;
    } press;

    struct Drag
    {
        int index = -1;
        bool fromBoard = false;
        juce::Point<float> grab;     // mouse offset from the ghost's top-left, local units at board scale
        juce::Point<float> pos;
        int target = -1;
        bool overShelf = false;
        bool active() const { return index >= 0; }
    } drag;

    juce::String note;
    juce::uint32 noteUntil = 0;

    std::unique_ptr<PedalCloseUp> closeUp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardTab)
};

} // namespace stompbox::ui
