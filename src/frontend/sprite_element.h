#pragma once

#include <cstddef>
#include <cstdint>

#include "frontend/screen_element.h"

// The picture elements: TextureElement (a texture drawn in its own 2D piece), SpriteElement (a sprite, also
// placed in 3D with pos3D) and WindowElement (a 2D window - a clip rectangle - its descendants' sprites draw in).
// Their drawing pieces (0x0048BF30 / 0x0048BF40 and their setters 0x0049EBxx-0x0049EExx, the windows 0x004A15C0-
// 0x004A1670) are still game code, reached by address.
namespace render {
class BillboardQuad;
class TexturedQuad;
} // namespace render

namespace frontend {

// TextureElement (vtable 0x0064A1F8, 0x180 bytes).
class TextureElement final : public UiWidget {
public:
    // 0x004AED90: no texture, a new textured piece (0x0048BF40); type
    // TextureElement.
    TextureElement();
    // 0x004AEDF0 (0x004AEE60 the deleting one): the piece freed (0x0048C080), then UiWidget's.
    ~TextureElement() override;
    // 0x004AEE90 (slot 1): UiWidget's, then Texture (the piece's texture; kVisibleNow), rot_angle (at once),
    // Blend.
    void change_traits(script::Struct* params) override;
    // 0x004AEFD0 (slot 8): only the summed colour, as the array rgba (not UiWidget's).
    void get_properties(script::Struct* out) override;
    // 0x004AEF40 (slot 10): the piece shown, at the element's z_priority, in its shown colour with the summed
    // alpha. (Slot 9 is 0x004AEE50, a jump to UiWidget::blend_towards.)
    void tick_frame() override;

    uint32_t texture_;            // +0x178 the texture's checksum
    render::TexturedQuad* piece_; // +0x17C
};

// SpriteElement (vtable 0x0064A228, 0x184 bytes).
class ImageWidget final : public UiWidget {
public:
    // 0x004AF1A0: no texture, a new sprite (0x0048BF30), z writes on; type
    // SpriteElement.
    ImageWidget();
    // 0x004AF0D0 (0x004AF210 the deleting one): the sprite freed (0x0048C080), then UiWidget's.
    ~ImageWidget() override;
    // 0x004AF130: the texture (by checksum) on the sprite; the element takes the texture's size; kVisibleNow.
    void set_texture(uint32_t texture);
    // 0x004AF240: the rotation (degrees, plus a constant) morphed to and with `now` shown (and the sprite's).
    void set_sprite_rotation(float degrees, int now);
    // 0x004AF520 (slot 1): UiWidget's, then Texture, rot_angle (at once), Blend, no_zwrite; the sprite
    // goes in its window's viewport.
    void change_traits(script::Struct* params) override;
    // 0x004AF4E0 (slot 8): UiWidget's, then rot_angle (the radians shown) and Texture.
    void get_properties(script::Struct* out) override;
    // 0x004AF5E0 (slot 9): UiWidget's, then rot_angle morphed to.
    void blend_towards(script::Struct* params) override;
    // 0x004AF2C0 (slot 10): the sprite hidden (hidden, or its top off the layout), and after a change its
    // position, size, flips, rotation, justification, scale, priority (z_priority or pos3D's depth), z writes
    // and colour (the shown colour with the summed alpha).
    void tick_frame() override;

    uint32_t texture_;            // +0x178 the texture's checksum
    bool z_write_;                // +0x17C (no_zwrite clears it)
    render::BillboardQuad* quad_; // +0x180
};

// WindowElement (vtable 0x0064A2C0, 0x17C bytes): the root window and script-made windows.
class PanelWidget final : public UiWidget {
public:
    // 0x004B2060: type WindowElement, a window of 640x448 at 0, 0 (0x004A1620),
    // justified top left at 0, 0, its size 640x448.
    PanelWidget();
    // 0x004B2100 (0x004B2170 the deleting one): the window back to the free list (0x004A1670), then UiWidget's.
    ~PanelWidget() override;
    // 0x004B2160: its window.
    void* fetch_screen_rect() const;
    // 0x004B21A0 (slot 10): after a change, the window moved to the element's top left and sized as it.
    void tick_frame() override;

    void* screen_region_; // +0x178
};

} // namespace frontend
