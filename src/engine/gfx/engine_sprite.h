#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"

// The engine's 2D pieces (Nx engine): the sprite (render::BillboardQuad; the PC sprite PcSprite - pc_sprite.h - extends it and
// draws it) and the textured quad (render::TexturedQuad, used as it is on PC). Each setter stores its value and tells
// the platform through a virtual slot.
namespace render {

// The sprite (0x44 bytes of the engine's; vtable 0x00649A80 - PC 0x0064A744).
class BillboardQuad : public memory::HeapObject {
public:
    static constexpr uint32_t kBlend = 0x54628ED7; // the blend mode "blend"

    // 0x0049ED60: in `window` - hidden, no texture, blend "blend", at (-1, -1), scale 1, hot spot (-1, -1), unturned,
    // priority 0, size 0, no flips, colour 0x80 each.
    explicit BillboardQuad(void* window);
    // 0x0049EBA0 (0x0049EDD0 the scalar deleting destructor, vtable slot 0)
    virtual ~BillboardQuad() = default;
    BillboardQuad(const BillboardQuad&) = delete;
    BillboardQuad& operator=(const BillboardQuad&) = delete;
    // Vtable slots 1-5, the platform told what changed (the engine's do nothing): everything, the hidden flag, the
    // settings, the priority / depth, the window.
    virtual void refresh_all() {}
    virtual void update_hidden() {}
    virtual void update_settings() {}
    virtual void update_priority() {}
    virtual void update_window() {}
    // 0x0048D1B0 (the text's, folded: the window at +0x40, then update_window): the window it's drawn in.
    void change_pane(void* window);

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

    uint8_t* bitmap_{nullptr};            // +0x04: an engine BitmapTexture
    uint32_t mix_mode_{kBlend};           // +0x08
    float pos_x_{-1.0f}, y_{-1.0f};       // +0x0C
    float scale_h_{1.0f}, scale_v_{1.0f}; // +0x14
    float hot_x_{-1.0f}, hot_y_{-1.0f};   // +0x1C
    float orientation_turn_{0.0f};        // +0x24
    float rank_{0.0f};                    // +0x28
    uint32_t layer_{};        // +0x2C: (not set when made)
    uint16_t span_{0}, level_y_{0}; // +0x30
    bool invisible_{true};          // +0x34
    bool use_depth_{false};         // +0x35: layer_ rather than rank_
    bool z_write_{};          // +0x36: (not set when made)
    bool flip_v_{false}, flip_u_{false}; // +0x37
    uint8_t rgba_value_[4]{}; // +0x39 (unaligned)
    uint8_t pad3d_[3]{};
    void* screen_region_; // +0x40: its 2D window (set by 0x0048D1B0)
};
static_assert(offsetof(BillboardQuad, orientation_turn_) == 0x24 && offsetof(BillboardQuad, span_) == 0x30 &&
                  offsetof(BillboardQuad, rgba_value_) == 0x39 && offsetof(BillboardQuad, screen_region_) == 0x40 &&
                  sizeof(BillboardQuad) == 0x44,
              "layout must match the original");

// The textured quad (0x20 bytes in the original with its vtable 0x00649A98 - [1] a new one, [4]-[6] the platform told
// of the hidden flag, the settings, the priority: all empty, and no PC class over it - so no vtable here: 0x1C).
class TexturedQuad : public memory::HeapObject {
public:
    // 0x0049EED0: in `window` - hidden, no texture, priority order, colour 0x80 each.
    explicit TexturedQuad(void* window);
    // (0x0049EF00 the scalar deleting destructor: nothing of its own)
    // 0x0049EF30 (vtable slot 1): a new quad (no window) - null without memory.
    static TexturedQuad* fabricate();

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

    void* screen_region_; // (the original's offsets: +0x04)
    uint8_t* bitmap_{nullptr}; // +0x08
    bool invisible_{true};     // +0x0C
    uint8_t padd_[3]{};
    float rank_{};   // +0x10
    bool use_depth_{false}; // +0x14: (always cleared)
    uint8_t pad15[3]{};
    uint32_t mix_mode_{};     // +0x18: (not set when made)
    uint8_t rgba_value_[4]{}; // +0x1C
};
static_assert(sizeof(TexturedQuad) == 0x1C, "layout must match the original");

} // namespace render
