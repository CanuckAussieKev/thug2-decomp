#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/draw2d.h"
#include "engine/gfx/bitmap_texture.h"

// 2D sprites: a texture drawn as a quad in the 640x480 screen layout (Nx engine, PC). The front end's sprite
// screen elements draw through these.
namespace render {

// A sprite. Virtual (vtable 0x0064AD28): [0] destructor 0x4DFCC0, [1] open_sketch 0x4DF990, [2] Draw, [3] close_sketch
// 0x4DFA40. Built on the base of things drawn in priority order (vtable 0x0064AD18, as GlyphRun - see text.h).
class QuadSprite final : public OverlayDraw {
public:
    // 0x004DFC50: with this priority - hidden (not in the draw list), no texture.
    explicit QuadSprite(float priority) : OverlayDraw(priority, true) {}
    // 0x004DFC80 (0x004DFCC0 the scalar deleting destructor): the base's (out of the draw list).
    ~QuadSprite() override = default;

    // The fields other code reads and sets.
    void set_texture(const TextureImage* value) { texture = value; }
    void set_x(float value) { x = value; }
    void set_y(float value) { y = value; }
    void set_width(uint16_t value) { width = value; }
    void set_height(uint16_t value) { height = value; }
    void set_xscale(float value) { xscale = value; }
    void set_yscale(float value) { yscale = value; }
    void set_hot_x(float value) { hot_x_ = value; }
    void set_hot_y(float value) { hot_y_ = value; }
    void set_rotation(float value) { rotation = value; }
    uint32_t& color_value() { return color; }
    [[nodiscard]] uint32_t color_value() const { return color; }
    bool& flip_v() { return flip_v_; }
    [[nodiscard]] bool flip_v() const { return flip_v_; }

private:
    const TextureImage* texture{nullptr}; // its image (sizes, device texture); or null: the whole of whatever texture is set
    float x{}, y{};         // position (of the hot spot), in 640x480 screen layout units
    uint16_t width{}, height{}; // in pixels at scale 1
    float xscale{}, yscale{};   // negative: mirrored
    float hot_x_{}, hot_y_{};   // the point drawn at (x, y), in pixels from the top left
    float rotation{};           // radians, about the hot spot; 0 = upright
    uint32_t color{};           // RGBA as bytes (alpha 0: not drawn)
    bool flip_v_{};             // swap the texture's top and bottom (see Draw)
    bool flip_u_{};             // swap its left and right

public:

    // 0x004DFCF0 (vtable slot 2): draw the quad (2 triangles) on the device with the texture open_sketch set.
    void draw() override;
    // 0x004DF990 (slot 1): the device made ready - format 0x144, no pixel shader, the texture (the sprite's
    // texture's device texture) modulated by the vertex colour, or the vertex colour alone without one.
    void open_sketch() override;
    // 0x004DFA40 (slot 3): the first stage back to modulate.
    void close_sketch() override;
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(QuadSprite, texture) == 0x14, "layout must match the original");
        static_assert(offsetof(QuadSprite, width) == 0x20, "layout must match the original");
        static_assert(offsetof(QuadSprite, rotation) == 0x34, "layout must match the original");
        static_assert(offsetof(QuadSprite, flip_u_) == 0x3D, "layout must match the original");
    }
};

} // namespace render
