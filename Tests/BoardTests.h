#pragma once

// Board model checks, included from DspTest.cpp.

#include "board/BoardModel.h"

namespace boardtests
{
using namespace stompbox::board;

inline int idx (const char* id) { return indexOf (id); }

inline bool noOverlaps (const BoardModel& b)
{
    std::vector<int> owner ((size_t) b.numSlots(), -1);
    for (int i = 0; i < numPedals; ++i)
    {
        const int s = b.pedal (i).slot;
        if (s < 0) continue;
        const int w = catalog()[(size_t) i].slots;
        if (b.column (s) + w > b.columns()) return false;   // a 2-slot pedal never wraps rows
        for (int k = 0; k < w; ++k)
        {
            if (owner[(size_t) (s + k)] != -1) return false;
            owner[(size_t) (s + k)] = i;
        }
    }
    return true;
}

template <typename Check>
void run (Check&& check)
{
    std::cout << "\nPedalboard\n";

    check (catalog().size() == (size_t) numPedals, "catalog has all 13 pedals");
    {
        int twoSlot = 0, stereo = 0;
        for (const auto& p : catalog()) { twoSlot += p.slots == 2; stereo += p.stereo; }
        check (twoSlot == 3 && stereo == 3, "3 two-slot pedals (Open Sky, Time Machine, Nutrient), 3 stereo");
    }

    BoardModel b;
    check (b.numOnShelf() == 13 && b.columns() == 5 && b.rows() == 2, "new board: Medium (2 x 5), all 13 on the shelf");

    // Signal order: bottom-right first, right to left, then the top row.
    b.place (idx ("hotRod"), 9);
    b.place (idx ("goldenHour"), 6);
    b.place (idx ("afterglow"), 0);
    b.place (idx ("openSky"), 3);   // top row, cols 3-4
    {
        const auto o = b.signalOrder();
        const std::vector<int> want { idx ("hotRod"), idx ("goldenHour"), idx ("openSky"), idx ("afterglow") };
        check (o == want, "signal order runs right to left, bottom row first");
    }

    check (! b.canPlace (idx ("timeMachine"), 4), "a 2-slot pedal can't straddle two rows");
    {
        const auto t = b.openTargets (idx ("timeMachine"));
        bool ok = ! t.empty();
        for (int s : t) ok = ok && b.pedalAt (s) < 0 && b.pedalAt (s + 1) < 0 && b.row (s) == b.row (s + 1);
        check (ok, "2-slot drop targets are only open side-by-side pairs in one row");
    }

    // Dropping on an occupied slot slides the others over.
    check (b.place (idx ("blueHighway"), 9) && b.pedal (idx ("blueHighway")).slot == 9
           && b.pedal (idx ("hotRod")).slot >= 5 && noOverlaps (b), "dropping on an occupied slot slides the other pedal over");

    // Fill the bottom row, then try one more: refused, nothing changes.
    b.place (idx ("scotland"), 5);
    b.place (idx ("trueTone"), 7);
    {
        int bottom = 0;
        for (int s = 5; s < 10; ++s) bottom += b.pedalAt (s) >= 0;
        const auto before = b.toValueTree();
        const bool placed = b.place (idx ("sculptor"), 8);
        check (bottom == 5 && ! placed && b.toValueTree().isEquivalentTo (before), "full row: drop refused, board unchanged");
    }

    // Moving a pedal within the board.
    check (b.place (idx ("afterglow"), 1) && b.pedal (idx ("afterglow")).slot == 1 && noOverlaps (b), "move a pedal to another slot");

    // State round trip
    b.setOn (idx ("hotRod"), false);
    b.setValue (idx ("hotRod"), 1, 0.83f);
    {
        BoardModel c;
        c.fromValueTree (b.toValueTree());
        check (c.toValueTree().isEquivalentTo (b.toValueTree()), "board survives save/restore");
    }

    // Corrupt state: two pedals claiming one slot -> second ignored.
    {
        auto t = b.toValueTree();
        for (auto child : t)
            if (child.getProperty ("id").toString() == "sculptor") child.setProperty ("slot", 9, nullptr);
        BoardModel c; c.fromValueTree (t);
        check (noOverlaps (c) && c.pedal (idx ("sculptor")).slot == -1, "damaged state never produces overlapping pedals");
    }

    // Shrink Medium -> Small: order kept, overflow back to the shelf.
    {
        const auto before = b.signalOrder();
        const int back = b.setSize (BoardModel::Size::small);
        const auto after = b.signalOrder();
        bool prefix = after.size() <= before.size();
        for (size_t i = 0; prefix && i < after.size(); ++i) prefix = after[i] == before[i];
        check (noOverlaps (b) && prefix && back == (int) (before.size() - after.size()) && b.rows() == 1,
               "Medium -> Small keeps signal order (" + juce::String (back) + " back to the shelf)");
        b.setSize (BoardModel::Size::large);
        check (noOverlaps (b) && b.signalOrder() == after && b.numSlots() == 14, "Small -> Large keeps every pedal and the order");
    }

    // Shelf return
    b.returnToShelf (idx ("hotRod"));
    check (b.pedal (idx ("hotRod")).slot == -1 && b.pedal (idx ("hotRod")).values[1] == 0.83f,
           "back on the shelf, a pedal keeps its knob settings");
}

} // namespace boardtests
