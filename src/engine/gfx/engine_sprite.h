#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/mem.h"
#include "engine/maths/vec2.h"

// The engine's 2D pieces (Nx engine): the sprite (render::BillboardQuad; the PC sprite PcSprite - pc_sprite.h - extends it and
// draws it) and the textured quad (render::TexturedQuad, used as it is on PC). Each setter stores its value and tells
// the platform through a virtual slot.
namespace render {

class BitmapTexture;

constexpr std::size_t kBillboardQuadOrientationTurnOffset = 0x24;
constexpr std::size_t kBillboardQuadSpanOffset = 0x30;
constexpr std::size_t kBillboardQuadRgbaValueOffset = 0x39;
constexpr std::size_t kBillboardQuadScreenRegionOffset = 0x40;
constexpr std::size_t kBillboardQuadBytes = 0x44;

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
    void set_texture(BitmapTexture* texture);
    // 0x0049EC00 (not a function in Ghidra): the blend mode (a checksum).
    void choose_blend_mode(uint32_t blend);
    // 0x0049EC10: where it's drawn (640x480 layout).
    void change_location(maths::Vec2 at);
    // A width and a height in pixels, as the sprite keeps them.
    struct Extent16 {
        uint16_t width{};
        uint16_t height{};
    };

    // 0x0049EC30: its size in pixels at scale 1.
    void change_extent(Extent16 extent);
    // 0x0049EE00: its scale - a positive scale raised so that the texture's size times it is more than 1.25 wide
    // and 1.1666 high (to exactly that, divided by the size; without a texture the size is 1).
    void change_scale(maths::Vec2 scale);
    // 0x0049EC50: mirrored top to bottom / left to right.
    void set_flip(bool flip_v, bool flip_u);
    // 0x0049EC70: the hot spot (-1..1 across it).
    void set_hot_spot(maths::Vec2 hot_spot);
    // 0x0049EC90: its rotation (radians).
    void change_spin(float rotation);
    // 0x0049ECA0 / 0x0049ECC0: drawn in priority order, or at a depth (its bits).
    void change_rank(float priority);
    void set_depth(uint32_t depth);
    // 0x0049ECE0: written to the depth buffer.
    void set_z_write(bool z_write);
    // 0x0049ECF0: its colour (RGBA).
    void change_rgba(uint32_t rgba);

    // The fields other code reads and sets.
    [[nodiscard]] BitmapTexture* bitmap() const { return bitmap_; }
    [[nodiscard]] float pos_x() const { return pos_x_; }
    [[nodiscard]] float scale_h() const { return scale_h_; }
    [[nodiscard]] float hot_x() const { return hot_x_; }
    [[nodiscard]] float orientation_turn() const { return orientation_turn_; }
    [[nodiscard]] float rank() const { return rank_; }
    [[nodiscard]] uint16_t span() const { return span_; }
    [[nodiscard]] bool invisible() const { return invisible_; }
    bool& flip_v() { return flip_v_; }
    [[nodiscard]] bool flip_v() const { return flip_v_; }
    std::array<uint8_t, 4>& rgba_value() { return rgba_value_; }
    [[nodiscard]] const std::array<uint8_t, 4>& rgba_value() const { return rgba_value_; }
    [[nodiscard]] float y() const { return y_; }
    [[nodiscard]] float scale_v() const { return scale_v_; }
    [[nodiscard]] float hot_y() const { return hot_y_; }
    [[nodiscard]] uint16_t level_y() const { return level_y_; }

private:
    BitmapTexture* bitmap_{nullptr};      // +0x04
    uint32_t mix_mode_{kBlend};           // +0x08
    float pos_x_{-1.0F}, y_{-1.0F};       // +0x0C
    float scale_h_{1.0F}, scale_v_{1.0F}; // +0x14
    float hot_x_{-1.0F}, hot_y_{-1.0F};   // +0x1C
    float orientation_turn_{0.0F};        // +0x24
    float rank_{0.0F};                    // +0x28
    uint32_t layer_{};        // +0x2C: (not set when made)
    uint16_t span_{0}, level_y_{0}; // +0x30
    bool invisible_{true};          // +0x34
    bool use_depth_{false};         // +0x35: layer_ rather than rank_
    bool z_write_{};          // +0x36: (not set when made)
    bool flip_v_{false}, flip_u_{false}; // +0x37
    std::array<uint8_t, 4> rgba_value_{}; // +0x39 (unaligned)
    std::array<uint8_t, 3> pad3d_{};
    void* screen_region_; // +0x40: its 2D window (set by 0x0048D1B0)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(BillboardQuad, orientation_turn_) == kBillboardQuadOrientationTurnOffset && offsetof(BillboardQuad, span_) == kBillboardQuadSpanOffset &&
                          offsetof(BillboardQuad, rgba_value_) == kBillboardQuadRgbaValueOffset && offsetof(BillboardQuad, screen_region_) == kBillboardQuadScreenRegionOffset &&
                          sizeof(BillboardQuad) == kBillboardQuadBytes,
                      "layout must match the original");
    }
};

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
    void set_texture(BitmapTexture* texture);
    // 0x0049ED00: the blend mode (a checksum).
    void choose_blend_mode(uint32_t blend);
    // 0x0049ED20: drawn in priority order.
    void change_rank(float priority);
    // 0x0049ED50: its colour (RGBA).
    void change_rgba(uint32_t rgba);

private:
    void* screen_region_; // (the original's offsets: +0x04)
    BitmapTexture* bitmap_{nullptr}; // +0x08
    bool invisible_{true};     // +0x0C
    std::array<uint8_t, 3> padd_{};
    float rank_{};   // +0x10
    bool use_depth_{false}; // +0x14: (always cleared)
    std::array<uint8_t, 3> pad15{};
    uint32_t mix_mode_{};     // +0x18: (not set when made)
    std::array<uint8_t, 4> rgba_value_{}; // +0x1C
};
constexpr std::size_t kTexturedQuadBytes = 0x1C;

static_assert(sizeof(TexturedQuad) == kTexturedQuadBytes, "layout must match the original");

} // namespace render
