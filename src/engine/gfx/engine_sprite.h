#pragma once

#include <cstddef>
#include <cstdint>

// The engine's 2D pieces (Nx engine): the sprite (render::BillboardQuad; the PC sprite PcSprite - pc_sprite.h - extends it and
// draws it) and the textured quad (render::TexturedQuad, used as it is on PC). Each setter stores its value and tells
// the platform through a virtual slot.
namespace render {

// The sprite (0x44 bytes of the engine's; vtable 0x00649A80 - PC 0x0064A744). Its virtual slots: [0] destroy, [2]
// the hidden flag changed, [3] the settings changed, [4] the priority / depth changed, [5] the window changed (the
// engine's own do nothing).
class BillboardQuad {
public:
    // 0x0049ED60: construct in `window` - hidden, no texture, blend "blend", at (-1, -1), scale 1, hot spot (-1, -1),
    // unturned, priority 0, size 0, no flips, colour 0x80 each. Returns this.
    BillboardQuad* make_new(void* window);
    // 0x0049EBA0: the destructor's body.
    void destroy_body();
    // 0x0049EDD0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    BillboardQuad* teardown(unsigned int free_flags);

    // 0x0049EBB0: hidden or shown.
    void change_concealed(bool hidden);
    // 0x0049EBC0: the texture (an engine BitmapTexture, or null) - and the size, its image's, when there is one.
    void set_texture(uint8_t* texture);
    // 0x0049EC00 (not a function in Ghidra): the blend mode (a checksum).
    void choose_blend_mode(uint32_t blend);
    // 0x0049EC10: where it's drawn (640x480 layout).
    void change_location(float x, float y);
    // 0x0049EC30: its size in pixels at scale 1.
    void change_extent(uint16_t width, uint16_t height);
    // 0x0049EE00: its scale - a positive scale raised so that the texture's size times it is more than 1.25 wide
    // and 1.1666 high (to exactly that, divided by the size; without a texture the size is 1).
    void change_scale(float xscale, float yscale);
    // 0x0049EC50: mirrored top to bottom / left to right.
    void set_flip(bool flip_v, bool flip_u);
    // 0x0049EC70: the hot spot (-1..1 across it).
    void set_hot_spot(float x, float y);
    // 0x0049EC90: its rotation (radians).
    void change_spin(float rotation);
    // 0x0049ECA0 / 0x0049ECC0: drawn in priority order, or at a depth (its bits).
    void change_rank(float priority);
    void set_depth(uint32_t depth);
    // 0x0049ECE0: written to the depth buffer.
    void set_z_write(bool z_write);
    // 0x0049ECF0: its colour (RGBA).
    void change_rgba(uint32_t rgba);

    void** vtable;
    uint8_t* bitmap_;   // +0x04: an engine BitmapTexture
    uint32_t mix_mode_;      // +0x08
    float pos_x_, y_;        // +0x0C
    float scale_h_, scale_v_; // +0x14
    float hot_x_, hot_y_;  // +0x1C
    float orientation_turn_;      // +0x24
    float rank_;      // +0x28
    uint32_t layer_;      // +0x2C: (not set when made)
    uint16_t span_, level_y_; // +0x30
    bool invisible_;         // +0x34
    bool use_depth_;       // +0x35: layer_ rather than rank_
    bool z_write_;         // +0x36: (not set when made)
    bool flip_v_, flip_u_; // +0x37
    uint8_t rgba_value_[4];     // +0x39 (unaligned)
    uint8_t pad3d_[3];
    void* screen_region_;       // +0x40: its 2D window (set by 0x0048D1B0)
};
static_assert(offsetof(BillboardQuad, orientation_turn_) == 0x24 && offsetof(BillboardQuad, span_) == 0x30 &&
                  offsetof(BillboardQuad, rgba_value_) == 0x39 && offsetof(BillboardQuad, screen_region_) == 0x40 && sizeof(BillboardQuad) == 0x44,
              "layout must match the original");

// The textured quad (0x20 bytes; vtable 0x00649A98: [0] destroy, [1] a new one, [4] the hidden flag changed, [5] the
// settings changed, [6] the priority changed - the engine's do nothing).
class TexturedQuad {
public:
    // 0x0049EED0: construct in `window` - hidden, no texture, priority order, colour 0x80 each. Returns this.
    TexturedQuad* make_new(void* window);
    // 0x0049EF00 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    TexturedQuad* teardown(unsigned int free_flags);
    // 0x0049EF30 (virtual [1]): a new quad (no window) - null without memory.
    TexturedQuad* fabricate();

    // 0x0049ED10: hidden or shown.
    void change_concealed(bool hidden);
    // 0x0049ED40: the texture (an engine BitmapTexture).
    void set_texture(uint8_t* texture);
    // 0x0049ED00: the blend mode (a checksum).
    void choose_blend_mode(uint32_t blend);
    // 0x0049ED20: drawn in priority order.
    void change_rank(float priority);
    // 0x0049ED50: its colour (RGBA).
    void change_rgba(uint32_t rgba);

    void** vtable;
    void* screen_region_;       // +0x04
    uint8_t* bitmap_;   // +0x08
    bool invisible_;         // +0x0C
    uint8_t padd_[3];
    float rank_;      // +0x10
    bool use_depth_;       // +0x14: (always cleared)
    uint8_t pad15[3];
    uint32_t mix_mode_;      // +0x18: (not set when made)
    uint8_t rgba_value_[4];     // +0x1C
};
static_assert(offsetof(TexturedQuad, rank_) == 0x10 && offsetof(TexturedQuad, rgba_value_) == 0x1C &&
                  sizeof(TexturedQuad) == 0x20,
              "layout must match the original");

} // namespace Nx
