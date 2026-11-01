#pragma once

#include <cstdint>
#include <array>

namespace render {

class BitmapTexture;

// A screen flash (0x24 bytes; made by add_flash, engine_effects.h).
struct Flash {
    int viewport;     // +0x00
    std::array<uint8_t, 4> start; // +0x04: the colour it starts with (RGBA)
    std::array<uint8_t, 4> end;   // +0x08: and ends with
    uint8_t red;      // +0x0C: the colour now
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;    // +0x0F
    uint32_t flags;   // +0x10: bit 1 picks which of draw_flashes' passes draws it; 8: it goes on while paused
    float duration;   // +0x14
    float time_left_; // +0x18
    float z;          // +0x1C
    BitmapTexture* texture; // +0x20 (null: untextured; nothing sets one)
};

// 0x004B9030: draw a flash over its viewport, with PartyMod's vertex order fix.
void draw_flash(const Flash* flash);

} // namespace render
