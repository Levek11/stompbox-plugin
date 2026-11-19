#include "PedalboardTab.h"
#include "PedalCloseUp.h"
#include "StompLookAndFeel.h"
#include "../PluginProcessor.h"

namespace stompbox::ui
{

using namespace board;

static const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00b7);

PedalboardTab::PedalboardTab (StompBoxProcessor& p) : processor (p)
{
    refreshModel();
    startTimerHz (30);
}

PedalboardTab::~PedalboardTab() = default;

void PedalboardTab::timerCallback()
{
    if (processor.getBoardVersion() != shownVersion)
        refreshModel();

    // Live displays (tuner, gain-reduction meter): repaint only what changed.
    for (int i = 0; i < numPedals; ++i)
    {
        std::array<float, 4> v {};
        const int n = model.isOnBoard (i) ? processor.pedalTelemetry (i, v.data()) : 0;
        if (n == 0 || v == shownLive[(size_t) i]) continue;
        shownLive[(size_t) i] = v;
        repaint (pedalRectLocal (i).toNearestInt().expanded (2));
        if (closeUp != nullptr && closeUp->getPedalIndex() == i) closeUp->repaint();
    }
    if (note.isNotEmpty() && juce::Time::getMillisecondCounter() > noteUntil)
    {
        note.clear();
        repaint();
    }
}

void PedalboardTab::refreshModel()
{
    shownVersion = processor.getBoardVersion();
    model = processor.getBoard();
    updateGeometry();
    repaint();
    if (closeUp != nullptr) closeUp->repaint();
}

void PedalboardTab::showNote (const juce::String& text)
{
    note = text;
    noteUntil = juce::Time::getMillisecondCounter() + 4500;
    repaint();
}

void PedalboardTab::resized()
{
    updateGeometry();
    if (closeUp != nullptr) closeUp->setBounds (getLocalBounds());
}

//==============================================================================
juce::Rectangle<float> PedalboardTab::mainArea() const
{
    return getLocalBounds().withTrimmedRight (shelfWidth).toFloat();
}

void PedalboardTab::updateGeometry()
{
    const auto main = mainArea();
    const float rugW = boardW() + 180.0f, rugH = boardH() + 208.0f;
    scale = juce::jmin (1.0f, (main.getWidth() - 40.0f) / rugW, (main.getHeight() - 110.0f) / rugH);
    boardOrigin = { main.getCentreX() - boardW() * scale * 0.5f, main.getCentreY() - 20.0f - boardH() * scale * 0.5f };
}

juce::AffineTransform PedalboardTab::boardToLocal() const
{
    return juce::AffineTransform::scale (scale).translated (boardOrigin);
}

juce::Point<float> PedalboardTab::localToBoard (juce::Point<float> p) const
{
    return (p - boardOrigin) / scale;
}

juce::Rectangle<float> PedalboardTab::slotRectBoard (int slot, int widthSlots) const
{
    const int r = model.row (slot), c = model.column (slot);
    return { 28.0f + (float) c * 128.0f, rowTop (r), 112.0f * (float) widthSlots + 16.0f * (float) (widthSlots - 1), 168.0f };
}

juce::Rectangle<float> PedalboardTab::pedalRectBoard (int index) const
{
    return slotRectBoard (model.pedal (index).slot, catalog()[(size_t) index].slots);
}

juce::Rectangle<float> PedalboardTab::pedalRectLocal (int index) const
{
    return pedalRectBoard (index).transformedBy (boardToLocal());
}

juce::Rectangle<float> PedalboardTab::shelfRect (int index) const
{
    // Shelf rows of 286 px, filled in catalog order: 64 px per 1-slot pedal, 138 px per 2-slot.
    const float left = (float) getWidth() - (float) shelfWidth + 28.0f + 8.0f;
    float x = 0.0f;
    int row = 0;
    for (int i = 0; i < numPedals; ++i)
    {
        const float w = catalog()[(size_t) i].slots == 2 ? 138.0f : 64.0f;
        if (x > 0.0f && x + w > 286.0f + 0.5f) { ++row; x = 0.0f; }
        if (i == index)
        {
            const float rowTopY = 64.0f + (float) row * 150.0f;
            return { left + x, rowTopY + 112.0f - 96.0f, w, 96.0f };
        }
        x += w + 10.0f;
    }
    return {};
}

//==============================================================================
int PedalboardTab::boardPedalAt (juce::Point<float> local) const
{
    for (int i = 0; i < numPedals; ++i)
        if (model.isOnBoard (i) && i != drag.index && pedalRectLocal (i).contains (local))
            return i;
    return -1;
}

int PedalboardTab::shelfPedalAt (juce::Point<float> local) const
{
    for (int i = 0; i < numPedals; ++i)
        if (! model.isOnBoard (i) && i != drag.index && shelfRect (i).contains (local))
            return i;
    return -1;
}

int PedalboardTab::footswitchAt (int index, juce::Point<float> local) const
{
    if (index < 0) return -1;
    const auto& def = catalog()[(size_t) index];
    const auto design = local.transformedBy (PedalPainter::transformFor (def, pedalRectLocal (index)).inverted());
    for (int f = 0; f < def.footswitches; ++f)
        if (design.getDistanceFrom (PedalPainter::footswitchCentre (def, f)) <= PedalPainter::footswitchRadius + 3.0f)
            return f;
    return -1;
}

//==============================================================================
void PedalboardTab::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const int bp = boardPedalAt (p);
    const int fs = footswitchAt (bp, p);
    const int sp = bp < 0 ? shelfPedalAt (p) : -1;
    if (bp != hoverPedal || fs != hoverFootswitch || sp != hoverShelf)
    {
        hoverPedal = bp; hoverFootswitch = fs; hoverShelf = sp;
        setMouseCursor (sp >= 0 ? juce::MouseCursor::DraggingHandCursor
                        : bp >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PedalboardTab::mouseExit (const juce::MouseEvent&)
{
    if (hoverPedal >= 0 || hoverShelf >= 0)
    {
        hoverPedal = hoverFootswitch = hoverShelf = -1;
        repaint();
    }
}

void PedalboardTab::mouseDown (const juce::MouseEvent& e)
{
    press = {};
    press.at = e.position;
    press.pedal = boardPedalAt (e.position);
    if (press.pedal >= 0)
        press.footswitch = footswitchAt (press.pedal, e.position);
    else if ((press.pedal = shelfPedalAt (e.position)) >= 0)
        press.fromShelf = true;
}

board::BoardModel PedalboardTab::liftedModel() const
{
    auto m = model;
    if (drag.index >= 0) m.returnToShelf (drag.index);
    return m;
}

void PedalboardTab::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.active())
    {
        if (press.pedal < 0 || press.footswitch >= 0 || e.position.getDistanceFrom (press.at) < 4.0f)
            return;

        drag = {};
        drag.index = press.pedal;
        drag.fromBoard = ! press.fromShelf;
        // The ghost is drawn at board size; keep the grab point at the same
        // relative spot on the pedal (a shelf pedal grows as it's lifted).
        const auto src = press.fromShelf ? shelfRect (press.pedal) : pedalRectLocal (press.pedal);
        const auto& def = catalog()[(size_t) drag.index];
        const auto rel = press.at - src.getPosition();
        drag.grab = { rel.x / src.getWidth() * def.width() * scale, rel.y / src.getHeight() * PedalDef::height * scale };
        hoverPedal = hoverFootswitch = hoverShelf = -1;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    drag.pos = e.position;
    updateDragTarget (e.position);
    repaint();
}

void PedalboardTab::updateDragTarget (juce::Point<float> local)
{
    const auto& def = catalog()[(size_t) drag.index];
    drag.overShelf = local.x >= (float) getWidth() - (float) shelfWidth;
    drag.target = -1;
    if (drag.overShelf)
        return;

    // Centre of the ghost pedal, in board space
    const auto ghostSize = juce::Point<float> (def.width(), PedalDef::height) * scale;
    const auto centre = localToBoard (local - drag.grab + ghostSize * 0.5f);
    const auto board = juce::Rectangle<float> (0.0f, 0.0f, boardW(), boardH()).expanded (60.0f);
    if (! board.contains (centre))
        return;

    const int cols = model.columns(), rows = model.rows(), w = def.slots;
    const int col = juce::jlimit (0, cols - w, juce::roundToInt ((centre.x - 28.0f - def.width() * 0.5f) / 128.0f));
    const int row = juce::jlimit (0, rows - 1, juce::roundToInt ((centre.y - 28.0f - 84.0f) / 208.0f));
    const int slot = model.slotAt (row, col);
    if (liftedModel().canPlace (drag.index, slot))
        drag.target = slot;
}

void PedalboardTab::mouseUp (const juce::MouseEvent& e)
{
    if (drag.active())
    {
        const int index = drag.index, target = drag.target;
        const bool toShelf = drag.overShelf;
        drag = {};
        if (target >= 0)
            processor.editBoard ([&] (BoardModel& b) { return b.place (index, target); });
        else if (toShelf)
            processor.editBoard ([&] (BoardModel& b) { b.returnToShelf (index); });
        refreshModel();
        mouseMove (e);
        return;
    }

    // A click, not a drag
    if (press.pedal >= 0 && ! press.fromShelf)
    {
        const int index = press.pedal;
        if (press.footswitch >= 0 && footswitchAt (index, e.position) == press.footswitch)
        {
            const int f = press.footswitch;
            processor.editBoard ([&] (BoardModel& b)
            {
                const auto& st = b.pedal (index);
                b.setOn (index, ! (f == 0 ? st.on : st.onB), f);
            });
            refreshModel();
        }
        else if (press.footswitch < 0 && boardPedalAt (e.position) == index)
        {
            closeUp = std::make_unique<PedalCloseUp> (processor, index, [this]
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PedalboardTab> (this)]
                {
                    if (safe != nullptr) { safe->closeUp.reset(); safe->refreshModel(); }
                });
            });
            addAndMakeVisible (*closeUp);
            closeUp->setBounds (getLocalBounds());
            hoverPedal = hoverFootswitch = -1;
        }
    }
    press = {};
}

//==============================================================================
void PedalboardTab::paint (juce::Graphics& g)
{
    drawFloor (g);
    drawRugAndBoard (g);

    // Pedals on the board
    for (int i = 0; i < numPedals; ++i)
    {
        if (! model.isOnBoard (i) || i == drag.index) continue;
        PedalPainter::Look look;
        look.hover = (i == hoverPedal && hoverFootswitch < 0);
        look.footswitchHover = i == hoverPedal ? hoverFootswitch : -1;
        look.liveCount = processor.pedalTelemetry (i, look.live.data());
        PedalPainter::draw (g, catalog()[(size_t) i], model.pedal (i), pedalRectLocal (i), look);
    }

    drawCables (g);
    drawJacks (g);
    if (drag.active()) drawDropTargets (g);
    drawCaption (g);
    drawShelf (g);
    if (drag.active()) drawGhost (g);
}

void PedalboardTab::drawFloor (juce::Graphics& g)
{
    const auto main = mainArea();
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (main.toNearestInt());
    for (float x = main.getX(), i = 0; x < main.getRight(); x += 181.0f, ++i)
    {
        g.setColour ((int) i % 2 == 0 ? juce::Colour (0xff7a5435) : juce::Colour (0xff6f4a2e));
        g.fillRect (x, main.getY(), 178.0f, main.getHeight());
        g.setColour (juce::Colour (0xff5a3a22));
        g.fillRect (x + 178.0f, main.getY(), 3.0f, main.getHeight());
    }
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    for (float y = main.getY(); y < main.getBottom(); y += 11.0f)
        g.fillRect (main.getX(), y, main.getWidth(), 2.0f);
}

void PedalboardTab::drawRugAndBoard (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (boardToLocal());

    // --- Rug -----------------------------------------------------------------
    const auto rug = juce::Rectangle<float> (-90.0f, -104.0f, boardW() + 180.0f, boardH() + 208.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 24, { 0, 14 }).drawForRectangle (g, rug.toNearestInt());
    g.setColour (juce::Colour (0xff6b2422));
    g.fillRoundedRectangle (rug, 6.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (rug.toNearestInt());
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        const float span = rug.getWidth() + rug.getHeight();
        for (float d = -span; d < span; d += 26.0f)
        {
            g.drawLine (rug.getX() + d, rug.getY(), rug.getX() + d + rug.getHeight(), rug.getBottom(), 2.0f);
            g.drawLine (rug.getX() + d, rug.getBottom(), rug.getX() + d + rug.getHeight(), rug.getY(), 2.0f);
        }
    }
    g.setColour (juce::Colour (0xff3f1514));
    g.drawRoundedRectangle (rug.reduced (8.0f), 4.0f, 16.0f);
    g.setColour (juce::Colour (0xffc49a5a));
    g.drawRoundedRectangle (rug.reduced (17.5f), 3.0f, 3.0f);
    g.drawRoundedRectangle (rug.reduced (39.0f), 2.0f, 2.0f);

    // --- Board: dark brown wood, horizontal planks -----------------------------
    const auto board = juce::Rectangle<float> (0.0f, 0.0f, boardW(), boardH());
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 32, { 0, 20 }).drawForRectangle (g, board.toNearestInt());
    juce::Path shape;
    shape.addRoundedRectangle (board, 12.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (shape);
        g.setColour (juce::Colour (0xff4a3020));
        g.fillRect (board);
        g.setColour (juce::Colour (0xff362116));
        for (float y = 52.0f; y < board.getBottom(); y += 54.0f)
            g.fillRect (0.0f, y, board.getWidth(), 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (float x = 0.0f; x < board.getRight(); x += 15.0f)
            g.fillRect (x, 0.0f, 2.0f, board.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (0.0f, 0.0f, board.getWidth(), 2.0f);
    }
    g.setColour (juce::Colour (0xff2b1a0f));
    g.strokePath (shape, juce::PathStrokeType (3.0f));
}

void PedalboardTab::drawJacks (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (boardToLocal());

    auto box = [&] (juce::Rectangle<float> r, const juce::String& big, const juce::String& small)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 8, { 0, 4 }).drawForRectangle (g, r.toNearestInt());
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colours::text);
        g.setFont (Fonts::ui (12.0f, true));
        g.drawText (big, r.withTrimmedBottom (r.getHeight() * 0.42f).withTrimmedTop (4.0f), juce::Justification::centredBottom);
        g.setColour (juce::Colour (0xffc9c3b8));
        g.setFont (Fonts::ui (10.0f));
        g.drawText (small, r.withTrimmedTop (r.getHeight() * 0.58f).withTrimmedBottom (4.0f), juce::Justification::centredTop);
    };

    box ({ boardW() + 32.0f, rowTop (model.rows() - 1) + 30.0f, 46.0f, 48.0f }, "IN", "guitar");
    box ({ -80.0f, rowTop (0) + 30.0f, 46.0f, 60.0f }, "OUT", "L " + dot + " R");
}

void PedalboardTab::drawCables (juce::Graphics& g)
{
    // Cables follow slot order automatically. Each pedal's input is on its
    // right edge and its output on its left (signal runs right to left).
    // After a stereo pedal every cable is doubled (L and R).
    const float W = boardW();
    const int rowsN = model.rows();
    auto jackY = [&] (int r, int ch) { return rowTop (r) + 50.0f + 18.0f * (float) ch; };

    struct Node { juce::Point<float> p; int row; };
    const auto order = model.signalOrder();

    std::vector<juce::Path> paths;
    auto droop = [&] (juce::Path& path, juce::Point<float> a, juce::Point<float> b)
    {
        const float d = 14.0f + std::abs (a.x - b.x) * 0.03f;
        path.startNewSubPath (a);
        path.cubicTo (a.x - 4.0f, a.y + d, b.x + 4.0f, b.y + d, b.x, b.y);
    };
    auto upAndAround = [&] (juce::Path& path, juce::Point<float> a, juce::Point<float> b, float inset)
    {
        // From an output in a lower row, round the left edge, across the top,
        // down the right edge and into an input in a higher row.
        const float L = 12.0f + inset, T = 14.0f + inset, R = W - 12.0f - inset;
        path.startNewSubPath (a);
        path.cubicTo (a.x - 12.0f, a.y, L, a.y - 8.0f, L, a.y - 22.0f);
        path.lineTo (L, T + 20.0f);
        path.cubicTo (L, T + 6.0f, L + 6.0f, T, L + 20.0f, T);
        path.lineTo (R - 18.0f, T);
        path.cubicTo (R - 6.0f, T, R, T + 6.0f, R, T + 18.0f);
        path.lineTo (R, b.y - 14.0f);
        path.cubicTo (R, b.y - 4.0f, R - 6.0f, b.y, R - 16.0f, b.y);
        path.lineTo (b.x, b.y);
    };
    auto leftAndUp = [&] (juce::Path& path, juce::Point<float> a, juce::Point<float> b, float inset)
    {
        // From a lower-row output up the left edge to the OUT jack.
        const float L = 12.0f + inset;
        path.startNewSubPath (a);
        path.cubicTo (a.x - 12.0f, a.y, L, a.y - 8.0f, L, a.y - 22.0f);
        path.lineTo (L, b.y + 18.0f);
        path.cubicTo (L, b.y + 4.0f, L - 6.0f, b.y, b.x, b.y);
    };

    juce::Point<float> from[2] { { W + 32.0f, jackY (rowsN - 1, 0) + 4.0f }, {} };
    int fromRow = rowsN - 1;
    bool stereo = false;

    auto connect = [&] (juce::Point<float> toL, juce::Point<float> toR, int toRow, bool toIsOut)
    {
        for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
        {
            const auto a = from[ch];
            const auto b = ch == 0 ? toL : toR;
            juce::Path path;
            if (toIsOut && fromRow > 0)        leftAndUp (path, a, b, ch * 10.0f);
            else if (toRow == fromRow)         droop (path, a, b);
            else                               upAndAround (path, a, b, ch * 10.0f);
            paths.push_back (path);
        }
    };

    for (int index : order)
    {
        const auto r = pedalRectBoard (index);
        const int row = model.row (model.pedal (index).slot);
        connect ({ r.getRight(), jackY (row, 0) }, { r.getRight(), jackY (row, 1) }, row, false);
        if (catalog()[(size_t) index].stereo) stereo = true;
        from[0] = { r.getX(), jackY (row, 0) };
        from[1] = { r.getX(), jackY (row, 1) };
        fromRow = row;
    }

    const float outMid = rowTop (0) + 60.0f;
    if (stereo) connect ({ -34.0f, outMid - 14.0f }, { -34.0f, outMid + 12.0f }, 0, true);
    else        connect ({ -34.0f, outMid }, {}, 0, true);

    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (boardToLocal());
    for (const auto& p : paths)
    {
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (0.0f, 2.5f));
        g.setColour (juce::Colour (0xff0b0b0b));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (0.0f, -1.2f));
    }
}

void PedalboardTab::drawDropTargets (juce::Graphics& g)
{
    const auto lifted = liftedModel();
    const int w = catalog()[(size_t) drag.index].slots;

    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (boardToLocal());

    // Every open place the pedal fits gets a subtle dashed outline.
    std::vector<bool> outlined ((size_t) model.numSlots(), false);
    for (int slot : lifted.openTargets (drag.index))
        for (int k = 0; k < w; ++k) outlined[(size_t) (slot + k)] = true;

    const float dashes[] = { 6.0f, 5.0f };
    for (int slot = 0; slot < model.numSlots(); ++slot)
    {
        if (! outlined[(size_t) slot]) continue;
        juce::Path p, dashed;
        p.addRoundedRectangle (slotRectBoard (slot, 1).reduced (1.0f), 10.0f);
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.fillPath (dashed);
    }

    // The place under the cursor brightens.
    if (drag.target >= 0)
    {
        const auto r = slotRectBoard (drag.target, w).reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 10.0f);
        juce::Path p, dashed;
        p.addRoundedRectangle (r, 10.0f);
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillPath (dashed);
        g.setFont (Fonts::ui (11.0f, true));
        g.setColour (juce::Colours::white);
        const bool occupied = [&] { for (int k = 0; k < w; ++k) if (lifted.pedalAt (drag.target + k) >= 0) return true; return false; }();
        g.drawText (occupied ? "Drop here (slides the others over)" : "Drop here",
                    r.withTrimmedBottom (10.0f), juce::Justification::centredBottom);
    }
}

void PedalboardTab::drawGhost (juce::Graphics& g)
{
    const auto& def = catalog()[(size_t) drag.index];
    const auto size = juce::Point<float> (def.width(), PedalDef::height) * scale;
    const auto topLeft = drag.pos - drag.grab;
    const auto r = juce::Rectangle<float> (topLeft.x, topLeft.y, size.x, size.y);

    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-4.0f), r.getCentreX(), r.getCentreY()));
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 40, { 0, 26 }).drawForRectangle (g, r.toNearestInt());
    PedalPainter::Look look;
    look.alpha = 0.95f;
    PedalPainter::draw (g, def, model.pedal (drag.index), r, look);
}

void PedalboardTab::drawCaption (juce::Graphics& g)
{
    juce::String text;
    if (drag.active())
        text = "Dragging " + displayName (catalog()[(size_t) drag.index]) + "  " + dot + "  " + (drag.overShelf ? "drop on the shelf to put it back" : "open slots are outlined");
    else if (note.isNotEmpty())
        text = note;
    else
        text = "Signal flows right to left, bottom row first  " + dot + "  click a pedal for its close-up";

    const auto font = Fonts::ui (12.0f);
    const float w = juce::GlyphArrangement::getStringWidth (font, text) + 24.0f;
    const auto r = juce::Rectangle<float> (24.0f, mainArea().getBottom() - 20.0f - 32.0f, w, 32.0f);
    g.setColour (juce::Colour (0xb3140e0a));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (0xfff1e7d8));
    g.setFont (font);
    g.drawText (text, r, juce::Justification::centred);
}

void PedalboardTab::drawShelf (juce::Graphics& g)
{
    const auto aside = getLocalBounds().removeFromRight (shelfWidth).toFloat();
    g.setColour (juce::Colour (0xff495049));
    g.fillRect (aside);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.25f), aside.getX(), 0.0f,
                                             juce::Colours::transparentBlack, aside.getX() + 30.0f, 0.0f, false));
    g.fillRect (aside.withWidth (30.0f));

    // Header
    const auto head = juce::Rectangle<float> (aside.getX() + 28.0f, 24.0f, aside.getWidth() - 56.0f, 16.0f);
    g.setColour (juce::Colour (0xfff1ede5));
    drawTracked (g, "PEDAL SHELF", Fonts::label (12.0f), 0.12f, head, juce::Justification::left);
    g.setColour (juce::Colour (0xffd6d2c8));
    g.setFont (Fonts::ui (12.0f));
    const int onShelf = model.numOnShelf();
    g.drawText (juce::String (onShelf) + " of 13 on the shelf", head, juce::Justification::right);

    // Planks
    for (int row = 0; row < 4; ++row)
    {
        const auto plank = juce::Rectangle<float> (aside.getX() + 28.0f, 64.0f + (float) row * 150.0f + 112.0f, aside.getWidth() - 56.0f, 14.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 12, { 0, 10 }).drawForRectangle (g, plank.toNearestInt());
        juce::ColourGradient grad (juce::Colour (0xffe2bf8a), 0.0f, plank.getY(), juce::Colour (0xffa87b45), 0.0f, plank.getBottom(), false);
        grad.addColour (0.6, juce::Colour (0xffc4955c));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (plank, 2.0f);
    }

    // Pedals still on the shelf (a pedal on the board leaves a plain gap)
    for (int i = 0; i < numPedals; ++i)
    {
        const bool beingDraggedFromShelf = drag.index == i && ! drag.fromBoard;
        if (model.isOnBoard (i) || beingDraggedFromShelf) continue;
        const auto& def = catalog()[(size_t) i];
        const auto r = shelfRect (i);

        g.setColour (juce::Colour (0xffe4e0d8));
        drawTracked (g, def.category, Fonts::label (def.slots == 2 ? 9.0f : 7.6f), def.slots == 2 ? 0.14f : 0.08f, r.withHeight (12.0f).translated (0.0f, -18.0f).expanded (4.0f, 0.0f), juce::Justification::centred);

        if (i == hoverShelf)
        {
            // Hover: the soft shadow behind it glows brighter
            juce::DropShadow (juce::Colour (0x55ffeccd), 26, { 0, 0 }).drawForRectangle (g, r.expanded (2.0f).toNearestInt());
        }
        auto st = model.pedal (i);
        st.on = st.onB = false;
        PedalPainter::Look look;
        look.showCategory = false;
        PedalPainter::draw (g, def, st, r, look);
    }
}

} // namespace stompbox::ui
