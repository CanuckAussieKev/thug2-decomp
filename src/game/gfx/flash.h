#pragma once

#include <cstdint>

namespace Nx {

// A screen flash (only the fields the draw uses).
struct Flash {
    int viewport;      // +0x00
    uint8_t unknown04[8];
    uint8_t red;       // +0x0C
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;     // +0x0F
    uint8_t unknown10[0x0C];
    float z;           // +0x1C
    uint8_t* texture;  // +0x20 (null: untextured)
};

// 0x004B9030: draw a flash over its viewport, with the PC port's vertex order fix.
void DrawFlash(const Flash* flash);

} // namespace Nx
