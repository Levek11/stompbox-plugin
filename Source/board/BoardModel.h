#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>
#include "PedalCatalog.h"

namespace stompbox::board
{

/** The pedalboard: which pedals sit in which slots, which are switched on, and
    every pedal's knob settings (kept while a pedal is on the shelf too, like a
    real pedal).

    Slots are numbered row by row from the top-left. The signal enters at the
    bottom-right slot and runs right to left; on two-row boards it continues
    from the top-right slot, right to left, out to the amps.

    Not thread-safe: owned by the message thread. The audio thread will get an
    immutable snapshot of the chain (step 2b). */
class BoardModel
{
public:
    enum class Size { small = 0, medium = 1, large = 2 };

    struct PedalState
    {
        int slot = -1;          // -1 = on the shelf; for 2-slot pedals, the left slot
        bool on = true;
        bool onB = true;        // second footswitch (dual pedals)
        std::vector<float> values;
    };

    BoardModel();

    // --- Geometry -------------------------------------------------------------
    Size getSize() const noexcept { return size; }
    int columns() const noexcept;
    int rows() const noexcept;
    int numSlots() const noexcept { return columns() * rows(); }
    static int columnsFor (Size s) noexcept { return s == Size::small ? 6 : s == Size::medium ? 5 : 7; }
    static int rowsFor (Size s) noexcept { return s == Size::small ? 1 : 2; }

    /** Changes size. Pedals keep their signal order and are packed into the new
        board right to left, bottom row first; any that don't fit go back to
        the shelf. Returns how many went back. */
    int setSize (Size newSize);

    // --- Pedals -----------------------------------------------------------------
    const PedalState& pedal (int index) const { return pedals[(size_t) index]; }
    bool isOnBoard (int index) const { return pedals[(size_t) index].slot >= 0; }
    int numOnShelf() const;

    /** Pedal occupying a slot, or -1. */
    int pedalAt (int slot) const;

    /** Can `index` be placed with its left edge at `slot`? Allows sliding
        other pedals in that row to make room. */
    bool canPlace (int index, int slot) const;

    /** Slots that are empty and can take the pedal without moving anything
        (for the drop outlines). Excludes the pedal's own current slot(s). */
    std::vector<int> openTargets (int index) const;

    /** Places a pedal (from the shelf or elsewhere on the board), sliding other
        pedals in the target row over if needed. Returns false (and changes
        nothing) if there's no room. */
    bool place (int index, int slot);

    void returnToShelf (int index);
    void setOn (int index, bool on, int footswitch = 0);
    void setValue (int index, int control, float value);
    void resetValue (int index, int control);

    /** Pedal indices in signal order, guitar first. */
    std::vector<int> signalOrder() const;

    // --- State ------------------------------------------------------------------
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);   // invalid tree = empty board
    void clear();

    static inline const juce::Identifier treeType { "Board" };

    int row (int slot) const noexcept { return slot / columns(); }
    int column (int slot) const noexcept { return slot % columns(); }
    int slotAt (int row, int col) const noexcept { return row * columns() + col; }

private:
    struct Span { int index, start, width; };

    /** Computes a row arrangement with `index` at column `col`; fills `out`
        with new starting columns for the other pedals in that row. */
    bool arrangeRow (int index, int rowIdx, int col, std::vector<Span>& out) const;

    Size size = Size::medium;
    std::array<PedalState, (size_t) numPedals> pedals;
};

} // namespace stompbox::board
