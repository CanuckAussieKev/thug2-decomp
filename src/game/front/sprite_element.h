#pragma once

#include <cstddef>
#include <cstdint>

#include "game/front/screen_element.h"

// The picture elements: TextureElement (a texture drawn in its own 2D piece), SpriteElement (a sprite, also
// placed in 3D with pos3D) and WindowElement (a 2D window - a clip rectangle - its descendants' sprites draw in).
// Their drawing pieces (0x0048BF30 / 0x0048BF40 and their setters 0x0049EBxx-0x0049EExx, the windows 0x004A15C0-
// 0x004A1670) are still game code, reached by address.
namespace Front {

// TextureElement (vtable 0x0064A1F8, 0x180 bytes).
class CTextureElement : public CScreenElement {
public:
    // 0x004AED90: the constructor (returns this): no texture, a new textured piece (0x0048BF40); type
    // TextureElement.
    CTextureElement* Construct();
    // 0x004AEDF0: the destructor body: the piece freed (0x0048C080), then CScreenElement's.
    void DestroyBody();
    // 0x004AEE60 (vtable slot 0)
    CTextureElement* Destroy(uint8_t flags);
    // 0x004AEE90 (slot 1): CScreenElement's, then Texture (the piece's texture; kVisibleNow), rot_angle (at once),
    // Blend.
    void SetProperties(Script::CStruct* params);
    // 0x004AEFD0 (slot 8): only the summed colour, as the array rgba (not CScreenElement's).
    void GetProperties(Script::CStruct* out);
    // 0x004AEF40 (slot 10): the piece shown, at the element's z_priority, in its shown colour with the summed
    // alpha. (Slot 9 is 0x004AEE50, a jump to CScreenElement::SetMorph.)
    void Update();

    uint32_t m_texture;  // +0x178 the texture's checksum
    void* mp_piece;      // +0x17C
};
static_assert(offsetof(CTextureElement, m_texture) == 0x178 && sizeof(CTextureElement) == 0x180,
              "layout must match the original");

// SpriteElement (vtable 0x0064A228, 0x184 bytes).
class CSpriteElement : public CScreenElement {
public:
    // 0x004AF1A0: the constructor (returns this): no texture, a new sprite (0x0048BF30), z writes on; type
    // SpriteElement.
    CSpriteElement* Construct();
    // 0x004AF0D0: the destructor body: the sprite freed (0x0048C080), then CScreenElement's.
    void DestroyBody();
    // 0x004AF210 (vtable slot 0)
    CSpriteElement* Destroy(uint8_t flags);
    // 0x004AF130: the texture (by checksum) on the sprite; the element takes the texture's size; kVisibleNow.
    void SetTexture(uint32_t texture);
    // 0x004AF240: the rotation (degrees, plus a constant) morphed to and with `now` shown (and the sprite's).
    void SetSpriteRotation(float degrees, int now);
    // 0x004AF520 (slot 1): CScreenElement's, then Texture, rot_angle (at once), Blend, no_zwrite; the sprite
    // goes in its window's viewport.
    void SetProperties(Script::CStruct* params);
    // 0x004AF4E0 (slot 8): CScreenElement's, then rot_angle (the radians shown) and Texture.
    void GetProperties(Script::CStruct* out);
    // 0x004AF5E0 (slot 9): CScreenElement's, then rot_angle morphed to.
    void SetMorph(Script::CStruct* params);
    // 0x004AF2C0 (slot 10): the sprite hidden (hidden, or its top off the layout), and after a change its
    // position, size, flips, rotation, justification, scale, priority (z_priority or pos3D's depth), z writes
    // and colour (the shown colour with the summed alpha).
    void Update();

    uint32_t m_texture;  // +0x178 the texture's checksum
    bool m_zWrite;       // +0x17C (no_zwrite clears it)
    uint8_t m_pad17D[3];
    void* mp_sprite;     // +0x180
};
static_assert(offsetof(CSpriteElement, m_zWrite) == 0x17C && offsetof(CSpriteElement, mp_sprite) == 0x180 &&
                  sizeof(CSpriteElement) == 0x184,
              "layout must match the original");

// WindowElement (vtable 0x0064A2C0, 0x17C bytes): the root window and script-made windows.
class CWindowElement : public CScreenElement {
public:
    // 0x004B2060: the constructor (returns this): type WindowElement, a window of 640x448 at 0, 0 (0x004A1620),
    // justified top left at 0, 0, its size 640x448.
    CWindowElement* Construct();
    // 0x004B2100: the destructor body: the window back to the free list (0x004A1670), then CScreenElement's.
    void DestroyBody();
    // 0x004B2170 (vtable slot 0)
    CWindowElement* Destroy(uint8_t flags);
    // 0x004B2160: its window.
    void* GetWindow2d() const;
    // 0x004B21A0 (slot 10): after a change, the window moved to the element's top left and sized as it.
    void Update();

    void* mp_window;  // +0x178
};
static_assert(sizeof(CWindowElement) == 0x17C, "layout must match the original");

} // namespace Front
