#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/sprite.h"

// The PC sprite (Nx engine: the engine's CSprite - constructor 0x0049ED60, destructor body 0x0049EBA0 - over it): the
// engine's settings copied into the 2D sprite that draws it (SSprite, sprite.h).
namespace Nx {

// The PC sprite (0x48 bytes, vtable 0x0064A744; made by CreateSprite 0x004B2A80).
struct CPcSprite {
    void** vtable;
    uint8_t* texture;     // +0x04: the engine's texture holder (+0x10 the texture, through one more pointer with
                          // flag 1 of its +0x08), or null
    uint8_t engine08[4];
    float x, y;           // +0x0C
    float xscale, yscale; // +0x14
    float hotX, hotY;     // +0x1C: the hot spot, -1..1 across the sprite
    float rotation;       // +0x24
    float priority;       // +0x28
    uint8_t engine2C[4];
    uint16_t width, height; // +0x30
    uint8_t hidden;       // +0x34
    uint8_t engine35[2];
    uint8_t flipV, flipU; // +0x37
    uint8_t colour[4];    // +0x39 (unaligned)
    uint8_t engine3D[7];
    SSprite* sprite;      // +0x44

    // 0x004C70E0: construct - the 2D sprite made with priority 0 (none without memory). Returns this.
    CPcSprite* Construct();
    // 0x004C7050: the destructor's body (the 2D sprite destroyed).
    void DestroyBody();
    // 0x004C7180 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcSprite* Destroy(unsigned int freeFlags);
    // 0x004C70B0 (virtual [2]): the 2D sprite hidden or shown as the engine's flag says.
    void UpdateHidden();
    // 0x004C71B0 (virtual [3]): the engine's settings copied into the 2D sprite (the texture only when there is
    // one; the hot spot into pixels).
    void Update();
    // 0x004C70C0 (virtual [4]): the 2D sprite's priority from the engine's (when there is a 2D sprite).
    void UpdatePriority();
};
static_assert(offsetof(CPcSprite, x) == 0xC && offsetof(CPcSprite, priority) == 0x28 &&
                  offsetof(CPcSprite, width) == 0x30 && offsetof(CPcSprite, flipV) == 0x37 &&
                  offsetof(CPcSprite, colour) == 0x39 && offsetof(CPcSprite, sprite) == 0x44 &&
                  sizeof(CPcSprite) == 0x48,
              "layout must match the original");

} // namespace Nx
