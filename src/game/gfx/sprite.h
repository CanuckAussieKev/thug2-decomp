#pragma once

#include <cstddef>
#include <cstdint>

// 2D sprites: a texture drawn as a quad in the 640x480 screen layout (Nx engine, PC). The front end's sprite
// screen elements draw through these.
namespace Nx {

// The size of a sprite's texture: the image's own size and the (power-of-two) size of the texture holding it.
struct SSpriteTexture {
    uint32_t unknown0;
    uint16_t baseWidth, baseHeight;     // the texture's
    uint16_t actualWidth, actualHeight; // the image's, at the texture's top left
};
static_assert(offsetof(SSpriteTexture, actualWidth) == 8, "layout must match the original");

// A sprite. Virtual (vtable 0x0064AD28): [0] destructor 0x4DFCC0, [1] BeginDraw 0x4DF990, [2] Draw, [3] EndDraw
// 0x4DFA40. Built on the base of things drawn in priority order (vtable 0x0064AD18, as SText - see text.h).
struct SSprite {
    void** vtable;
    uint8_t base[0xC]; // the base drawable's
    float z;
    SSpriteTexture* texture; // or null: the whole of whatever texture is set
    float x, y;              // position (of the hot spot), in 640x480 screen layout units
    uint16_t width, height;  // in pixels at scale 1
    float xscale, yscale;    // negative: mirrored
    float hotX, hotY;        // the point drawn at (x, y), in pixels from the top left
    float rotation;          // radians, about the hot spot; 0 = upright
    uint32_t color;          // RGBA as bytes (alpha 0: not drawn)
    bool flipV;              // swap the texture's top and bottom (see Draw)
    bool flipU;              // swap its left and right

    // 0x004DFCF0 (virtual [2]): draw the quad (2 triangles) on the device with the texture BeginDraw set.
    void Draw();
    // 0x004DFC50: construct with this priority - hidden (not in the draw list), no texture. Returns this.
    SSprite* Construct(float priority);
    // 0x004DFC80: the destructor's body (the base's: out of the draw list).
    void DestroyBody();
    // 0x004DFCC0 (virtual [0]): destroy, and free if flags & 1. Returns this.
    SSprite* Destroy(unsigned int flags);
    // 0x004DF990 (virtual [1]): the device made ready - format 0x144, no pixel shader, the texture (+0x10 of the
    // sprite's texture) modulated by the vertex colour, or the vertex colour alone without one.
    void BeginDraw();
    // 0x004DFA40 (virtual [3]): the first stage back to modulate.
    void EndDraw();
};
static_assert(offsetof(SSprite, texture) == 0x14, "layout must match the original");
static_assert(offsetof(SSprite, width) == 0x20, "layout must match the original");
static_assert(offsetof(SSprite, rotation) == 0x34, "layout must match the original");
static_assert(offsetof(SSprite, flipU) == 0x3D, "layout must match the original");

} // namespace Nx
