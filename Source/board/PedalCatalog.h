#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Every StompBox pedal: name, colour, category, size, I/O and the layout of its
// controls. Positions are in the pedal's own design space, matching the
// mockup: a 1-slot pedal is 112 x 168, a 2-slot pedal is 240 x 168.
// The same layout draws the pedal on the board, on the shelf (scaled down)
// and in the close-up (scaled up).
//
// IDs are saved in projects and presets: never rename them.

namespace stompbox::board
{

struct ControlDef
{
    enum class Kind { knob, toggle, slider };

    const char* id;
    const char* label;
    Kind kind;
    float x, y;          // centre, pedal design space
    float size;          // knob diameter / slider travel length
    float defaultValue;  // 0..1 (toggle: position index / (positions - 1))
    int positions = 1;   // toggles only
    std::uint32_t cap = 0xff161618;      // knob cap colour (ARGB)
    std::uint32_t pointer = 0xfff4f1ea;  // knob pointer colour
    const char* positionNames = "";      // toggles: "A|B|C", top to bottom
    const char* lockedBy = "";           // id of a toggle: while it's in its first position, this control is inactive
    float msMin = 0.0f, msMax = 0.0f;    // time knobs: shown in milliseconds (exponential sweep)
};

struct PedalDef
{
    enum class Decor { none, display, tuner, meter };

    const char* id;
    const char* name;          // as printed, '\n' for a line break
    const char* category;
    const char* inspiredBy;
    std::uint32_t body;        // ARGB
    std::uint32_t text;        // lettering colour
    int slots;                 // 1 or 2
    bool stereo;
    int footswitches;          // 1, or 2 for the dual overdrive
    float nameY;               // centre of the name lettering
    float nameSize;            // font size of the name
    Decor decor;
    const char* displayText;   // display/tuner text
    std::uint32_t displayColour;
    int soundBatch;            // build batch in which its sound arrives
    std::vector<ControlDef> controls;

    float width() const noexcept { return slots == 2 ? 240.0f : 112.0f; }
    static constexpr float height = 168.0f;
};

/** The 13 pedals, in shelf order. */
const std::vector<PedalDef>& catalog();

/** Index into catalog() for an id, or -1. */
int indexOf (const char* id);

constexpr int numPedals = 13;

} // namespace stompbox::board
