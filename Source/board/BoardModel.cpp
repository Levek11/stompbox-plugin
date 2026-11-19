#include "BoardModel.h"
#include <algorithm>
#include <limits>
#include <cstring>

namespace stompbox::board
{

/** Pedals arrive on the board switched on, except the tuner (it mutes while on). */
static bool startsOn (int index) { return std::strcmp (catalog()[(size_t) index].id, "trueTone") != 0; }

BoardModel::BoardModel()
{
    clear();
}

void BoardModel::clear()
{
    size = Size::medium;
    const auto& cat = catalog();
    for (size_t i = 0; i < pedals.size(); ++i)
    {
        auto& p = pedals[i];
        p = {};
        for (const auto& c : cat[i].controls)
            p.values.push_back (c.defaultValue);
        p.on = p.onB = startsOn ((int) i);
    }
}

int BoardModel::columns() const noexcept { return columnsFor (size); }
int BoardModel::rows() const noexcept    { return rowsFor (size); }

int BoardModel::numOnShelf() const
{
    int n = 0;
    for (const auto& p : pedals) if (p.slot < 0) ++n;
    return n;
}

int BoardModel::pedalAt (int slot) const
{
    const auto& cat = catalog();
    for (size_t i = 0; i < pedals.size(); ++i)
    {
        const int s = pedals[i].slot;
        if (s < 0) continue;
        if (slot == s || (cat[i].slots == 2 && slot == s + 1))
            return (int) i;
    }
    return -1;
}

//==============================================================================
bool BoardModel::arrangeRow (int index, int rowIdx, int col, std::vector<Span>& out) const
{
    const auto& cat = catalog();
    const int cols = columns();
    const int w = cat[(size_t) index].slots;
    if (col < 0 || col + w > cols)
        return false;

    std::vector<Span> others;
    for (size_t i = 0; i < pedals.size(); ++i)
    {
        if ((int) i == index || pedals[i].slot < 0 || row (pedals[i].slot) != rowIdx)
            continue;
        others.push_back ({ (int) i, column (pedals[i].slot), cat[i].slots });
    }
    std::sort (others.begin(), others.end(), [] (const Span& a, const Span& b) { return a.start < b.start; });

    // Try every split point: pedals before it slide left, the rest slide right.
    // Keep the arrangement that moves pedals the least.
    int bestCost = std::numeric_limits<int>::max();
    std::vector<Span> best;
    const int n = (int) others.size();
    for (int k = 0; k <= n; ++k)
    {
        std::vector<Span> trial = others;
        bool ok = true;

        int cursor = col + w;
        for (int i = k; i < n && ok; ++i)
        {
            trial[(size_t) i].start = std::max (trial[(size_t) i].start, cursor);
            cursor = trial[(size_t) i].start + trial[(size_t) i].width;
            ok = cursor <= cols;
        }
        cursor = col;
        for (int i = k - 1; i >= 0 && ok; --i)
        {
            trial[(size_t) i].start = std::min (trial[(size_t) i].start, cursor - trial[(size_t) i].width);
            cursor = trial[(size_t) i].start;
            ok = cursor >= 0;
        }
        if (! ok) continue;

        int cost = 0;
        for (int i = 0; i < n; ++i)
            cost += std::abs (trial[(size_t) i].start - others[(size_t) i].start);
        if (cost < bestCost) { bestCost = cost; best = trial; }
    }

    if (bestCost == std::numeric_limits<int>::max())
        return false;
    out = best;
    return true;
}

bool BoardModel::canPlace (int index, int slot) const
{
    if (index < 0 || index >= numPedals || slot < 0 || slot >= numSlots())
        return false;

    BoardModel copy (*this);
    copy.pedals[(size_t) index].slot = -1;   // lift it first
    std::vector<Span> spans;
    return copy.arrangeRow (index, row (slot), column (slot), spans);
}

std::vector<int> BoardModel::openTargets (int index) const
{
    std::vector<int> result;
    const int w = catalog()[(size_t) index].slots;
    const int cols = columns();
    for (int r = 0; r < rows(); ++r)
        for (int c = 0; c + w <= cols; ++c)
        {
            bool free = true;
            for (int k = 0; k < w && free; ++k)
            {
                const int p = pedalAt (slotAt (r, c + k));
                free = p < 0;
            }
            if (free) result.push_back (slotAt (r, c));
        }
    return result;
}

bool BoardModel::place (int index, int slot)
{
    if (index < 0 || index >= numPedals || slot < 0 || slot >= numSlots())
        return false;

    const int previous = pedals[(size_t) index].slot;
    pedals[(size_t) index].slot = -1;

    std::vector<Span> spans;
    if (! arrangeRow (index, row (slot), column (slot), spans))
    {
        pedals[(size_t) index].slot = previous;
        return false;
    }

    const int r = row (slot);
    for (const auto& s : spans)
        pedals[(size_t) s.index].slot = slotAt (r, s.start);
    pedals[(size_t) index].slot = slot;
    return true;
}

void BoardModel::returnToShelf (int index)
{
    if (index < 0 || index >= numPedals)
        return;

    // Back on the shelf, a pedal goes back to its default settings.
    auto& p = pedals[(size_t) index];
    p.slot = -1;
    p.on = p.onB = startsOn (index);
    const auto& defs = catalog()[(size_t) index].controls;
    for (size_t k = 0; k < defs.size(); ++k)
        p.values[k] = defs[k].defaultValue;
}

void BoardModel::setOn (int index, bool on, int footswitch)
{
    if (index < 0 || index >= numPedals) return;
    (footswitch == 1 ? pedals[(size_t) index].onB : pedals[(size_t) index].on) = on;
}

void BoardModel::setValue (int index, int control, float value)
{
    if (index < 0 || index >= numPedals) return;
    auto& v = pedals[(size_t) index].values;
    if (control >= 0 && control < (int) v.size())
        v[(size_t) control] = juce::jlimit (0.0f, 1.0f, value);
}

void BoardModel::resetValue (int index, int control)
{
    if (index < 0 || index >= numPedals) return;
    const auto& defs = catalog()[(size_t) index].controls;
    if (control >= 0 && control < (int) defs.size())
        setValue (index, control, defs[(size_t) control].defaultValue);
}

std::vector<int> BoardModel::signalOrder() const
{
    std::vector<int> order;
    for (int r = rows() - 1; r >= 0; --r)
        for (int c = columns() - 1; c >= 0; --c)
        {
            // A pedal is visited at its rightmost slot (its input side).
            const int p = pedalAt (slotAt (r, c));
            if (p >= 0 && std::find (order.begin(), order.end(), p) == order.end())
                order.push_back (p);
        }
    return order;
}

int BoardModel::setSize (Size newSize)
{
    const auto order = signalOrder();
    for (auto& p : pedals) p.slot = -1;
    size = newSize;

    const auto& cat = catalog();
    int r = rows() - 1, c = columns() - 1;   // next free position, moving right to left
    int sentBack = 0;
    for (int index : order)
    {
        const int w = cat[(size_t) index].slots;
        if (r >= 0 && c - (w - 1) < 0) { --r; c = columns() - 1; }   // doesn't fit: next row up
        if (r < 0) { ++sentBack; returnToShelf (index); continue; }
        pedals[(size_t) index].slot = slotAt (r, c - (w - 1));
        c -= w;
        if (c < 0) { --r; c = columns() - 1; }
    }
    return sentBack;
}

//==============================================================================
juce::ValueTree BoardModel::toValueTree() const
{
    juce::ValueTree t (treeType);
    t.setProperty ("size", (int) size, nullptr);
    const auto& cat = catalog();
    for (size_t i = 0; i < pedals.size(); ++i)
    {
        juce::ValueTree p ("Pedal");
        p.setProperty ("id", juce::String (cat[i].id), nullptr);
        p.setProperty ("slot", pedals[i].slot, nullptr);
        p.setProperty ("on", pedals[i].on, nullptr);
        if (cat[i].footswitches > 1)
            p.setProperty ("onB", pedals[i].onB, nullptr);
        for (size_t k = 0; k < cat[i].controls.size(); ++k)
            p.setProperty (juce::Identifier (juce::String ("c_") + cat[i].controls[k].id),
                           pedals[i].values[k], nullptr);
        t.appendChild (p, nullptr);
    }
    return t;
}

void BoardModel::fromValueTree (const juce::ValueTree& t)
{
    clear();
    if (! t.hasType (treeType))
        return;

    const int s = juce::jlimit (0, 2, (int) t.getProperty ("size", 1));
    size = (Size) s;
    const auto& cat = catalog();

    for (const auto& p : t)
    {
        const int index = indexOf (p.getProperty ("id").toString().toRawUTF8());
        if (index < 0) continue;   // pedal from a newer version: ignore
        auto& st = pedals[(size_t) index];
        st.on = (bool) p.getProperty ("on", true);
        st.onB = (bool) p.getProperty ("onB", true);
        const auto& defs = cat[(size_t) index].controls;
        for (size_t k = 0; k < defs.size(); ++k)
            st.values[k] = juce::jlimit (0.0f, 1.0f, (float) p.getProperty (
                juce::Identifier (juce::String ("c_") + defs[k].id), defs[k].defaultValue));
    }

    // Place pedals last, validating every slot (a damaged state must not
    // produce overlapping pedals).
    for (const auto& p : t)
    {
        const int index = indexOf (p.getProperty ("id").toString().toRawUTF8());
        const int slot = (int) p.getProperty ("slot", -1);
        if (index < 0 || slot < 0 || slot >= numSlots()) continue;
        const int w = cat[(size_t) index].slots;
        if (column (slot) + w > columns()) continue;
        bool free = true;
        for (int k = 0; k < w; ++k) free = free && pedalAt (slot + k) < 0;
        if (free) pedals[(size_t) index].slot = slot;
    }
}

} // namespace stompbox::board
