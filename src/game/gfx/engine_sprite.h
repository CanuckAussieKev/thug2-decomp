#pragma once

#include <cstddef>
#include <cstdint>

// The engine's 2D pieces (Nx engine): the sprite (Engine3D::CBillboardQuad; the PC sprite CPcSprite - pc_sprite.h - extends it and
// draws it) and the textured quad (Engine3D::CTexturedQuad, used as it is on PC). Each setter stores its value and tells
// the platform through a virtual slot.
namespace Engine3D {

// The sprite (0x44 bytes of the engine's; vtable 0x00649A80 - PC 0x0064A744). Its virtual slots: [0] destroy, [2]
// the hidden flag changed, [3] the settings changed, [4] the priority / depth changed, [5] the window changed (the
// engine's own do nothing).
class CBillboardQuad {
public:
    // 0x0049ED60: construct in `window` - hidden, no texture, blend "blend", at (-1, -1), scale 1, hot spot (-1, -1),
    // unturned, priority 0, size 0, no flips, colour 0x80 each. Returns this.
    CBillboardQuad* MakeNew(void* window);
    // 0x0049EBA0: the destructor's body.
    void DestroyBody();
    // 0x0049EDD0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CBillboardQuad* Teardown(unsigned int freeFlags);

    // 0x0049EBB0: hidden or shown.
    void ChangeConcealed(bool hidden);
    // 0x0049EBC0: the texture (an engine CBitmapTexture, or null) - and the size, its image's, when there is one.
    void SetTexture(uint8_t* texture);
    // 0x0049EC00 (not a function in Ghidra): the blend mode (a checksum).
    void ChooseBlendMode(uint32_t blend);
    // 0x0049EC10: where it's drawn (640x480 layout).
    void ChangeLocation(float x, float y);
    // 0x0049EC30: its size in pixels at scale 1.
    void ChangeExtent(uint16_t width, uint16_t height);
    // 0x0049EE00: its scale - a positive scale raised so that the texture's size times it is more than 1.25 wide
    // and 1.1666 high (to exactly that, divided by the size; without a texture the size is 1).
    void ChangeScale(float xscale, float yscale);
    // 0x0049EC50: mirrored top to bottom / left to right.
    void SetFlip(bool flipV, bool flipU);
    // 0x0049EC70: the hot spot (-1..1 across it).
    void SetHotSpot(float x, float y);
    // 0x0049EC90: its rotation (radians).
    void ChangeSpin(float rotation);
    // 0x0049ECA0 / 0x0049ECC0: drawn in priority order, or at a depth (its bits).
    void ChangeRank(float priority);
    void SetDepth(uint32_t depth);
    // 0x0049ECE0: written to the depth buffer.
    void SetZWrite(bool zWrite);
    // 0x0049ECF0: its colour (RGBA).
    void ChangeRgba(uint32_t rgba);

    void** vtable;
    uint8_t* m_bitmap;   // +0x04: an engine CBitmapTexture
    uint32_t m_mixMode;      // +0x08
    float m_posX, m_y;        // +0x0C
    float m_scaleH, m_scaleV; // +0x14
    float m_hotX, m_hotY;  // +0x1C
    float m_orientationTurn;      // +0x24
    float m_rank;      // +0x28
    uint32_t m_layer;      // +0x2C: (not set when made)
    uint16_t m_span, m_levelY; // +0x30
    bool m_invisible;         // +0x34
    bool m_useDepth;       // +0x35: m_layer rather than m_rank
    bool m_zWrite;         // +0x36: (not set when made)
    bool m_flipV, m_flipU; // +0x37
    uint8_t m_rgbaValue[4];     // +0x39 (unaligned)
    uint8_t pad3D[3];
    void* m_screenRegion;       // +0x40: its 2D window (set by 0x0048D1B0)
};
static_assert(offsetof(CBillboardQuad, m_orientationTurn) == 0x24 && offsetof(CBillboardQuad, m_span) == 0x30 &&
                  offsetof(CBillboardQuad, m_rgbaValue) == 0x39 && offsetof(CBillboardQuad, m_screenRegion) == 0x40 && sizeof(CBillboardQuad) == 0x44,
              "layout must match the original");

// The textured quad (0x20 bytes; vtable 0x00649A98: [0] destroy, [1] a new one, [4] the hidden flag changed, [5] the
// settings changed, [6] the priority changed - the engine's do nothing).
class CTexturedQuad {
public:
    // 0x0049EED0: construct in `window` - hidden, no texture, priority order, colour 0x80 each. Returns this.
    CTexturedQuad* MakeNew(void* window);
    // 0x0049EF00 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CTexturedQuad* Teardown(unsigned int freeFlags);
    // 0x0049EF30 (virtual [1]): a new quad (no window) - null without memory.
    CTexturedQuad* Fabricate();

    // 0x0049ED10: hidden or shown.
    void ChangeConcealed(bool hidden);
    // 0x0049ED40: the texture (an engine CBitmapTexture).
    void SetTexture(uint8_t* texture);
    // 0x0049ED00: the blend mode (a checksum).
    void ChooseBlendMode(uint32_t blend);
    // 0x0049ED20: drawn in priority order.
    void ChangeRank(float priority);
    // 0x0049ED50: its colour (RGBA).
    void ChangeRgba(uint32_t rgba);

    void** vtable;
    void* m_screenRegion;       // +0x04
    uint8_t* m_bitmap;   // +0x08
    bool m_invisible;         // +0x0C
    uint8_t padD[3];
    float m_rank;      // +0x10
    bool m_useDepth;       // +0x14: (always cleared)
    uint8_t pad15[3];
    uint32_t m_mixMode;      // +0x18: (not set when made)
    uint8_t m_rgbaValue[4];     // +0x1C
};
static_assert(offsetof(CTexturedQuad, m_rank) == 0x10 && offsetof(CTexturedQuad, m_rgbaValue) == 0x1C &&
                  sizeof(CTexturedQuad) == 0x20,
              "layout must match the original");

} // namespace Nx
