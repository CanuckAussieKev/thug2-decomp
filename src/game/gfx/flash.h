#pragma once

#include <cstdint>

namespace Nx {

// A screen flash (0x24 bytes; made by AddFlash, engine_effects.h).
struct Flash {
    int viewport;      // +0x00
    uint8_t start[4];  // +0x04: the colour it starts with (RGBA)
    uint8_t end[4];    // +0x08: and ends with
    uint8_t red;       // +0x0C: the colour now
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;     // +0x0F
    uint32_t flags;    // +0x10: bit 1 picks which of DrawFlashes' passes draws it; 8: it goes on while paused
    float duration;    // +0x14
    float timeLeft;    // +0x18
    float z;           // +0x1C
    uint8_t* texture;  // +0x20 (null: untextured; nothing sets one)
};

// 0x004B9030: draw a flash over its viewport, with PartyMod's vertex order fix.
void DrawFlash(const Flash* flash);

} // namespace Nx
