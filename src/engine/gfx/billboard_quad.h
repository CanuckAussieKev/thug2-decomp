#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/draw2d.h"

// 2D sprites: a texture drawn as a quad in the 640x480 screen layout (Nx engine, PC). The front end's sprite
// screen elements draw through these.
namespace render {

// The size of a sprite's texture: the image's own size and the (power-of-two) size of the texture holding it.
struct SpriteTexture {
    uint32_t unknown0;
    uint16_t base_width_, base_height_;     // the texture's
    uint16_t actual_width_, actual_height_; // the image's, at the texture's top left
    uint32_t unknownc_;
    void* device_texture_; // +0x10 what the device is given
};
static_assert(offsetof(SpriteTexture, actual_width_) == 8, "layout must match the original");

// A sprite. Virtual (vtable 0x0064AD28): [0] destructor 0x4DFCC0, [1] open_sketch 0x4DF990, [2] Draw, [3] close_sketch
// 0x4DFA40. Built on the base of things drawn in priority order (vtable 0x0064AD18, as GlyphRun - see text.h).
class QuadSprite final : public OverlayDraw {
public:
    // 0x004DFC50: with this priority - hidden (not in the draw list), no texture.
    explicit QuadSprite(float priority) : OverlayDraw(priority, true) { texture = nullptr; }
    // 0x004DFC80 (0x004DFCC0 the scalar deleting destructor): the base's (out of the draw list).
    ~QuadSprite() override = default;

    SpriteTexture* texture; // or null: the whole of whatever texture is set
    float x, y;             // position (of the hot spot), in 640x480 screen layout units
    uint16_t width, height; // in pixels at scale 1
    float xscale, yscale;   // negative: mirrored
    float hot_x_, hot_y_;   // the point drawn at (x, y), in pixels from the top left
    float rotation;         // radians, about the hot spot; 0 = upright
    uint32_t color;         // RGBA as bytes (alpha 0: not drawn)
    bool flip_v_;           // swap the texture's top and bottom (see Draw)
    bool flip_u_;           // swap its left and right

    // 0x004DFCF0 (vtable slot 2): draw the quad (2 triangles) on the device with the texture open_sketch set.
    void draw() override;
    // 0x004DF990 (slot 1): the device made ready - format 0x144, no pixel shader, the texture (the sprite's
    // texture's device texture) modulated by the vertex colour, or the vertex colour alone without one.
    void open_sketch() override;
    // 0x004DFA40 (slot 3): the first stage back to modulate.
    void close_sketch() override;
};
static_assert(offsetof(QuadSprite, texture) == 0x14, "layout must match the original");
static_assert(offsetof(QuadSprite, width) == 0x20, "layout must match the original");
static_assert(offsetof(QuadSprite, rotation) == 0x34, "layout must match the original");
static_assert(offsetof(QuadSprite, flip_u_) == 0x3D, "layout must match the original");

} // namespace render
