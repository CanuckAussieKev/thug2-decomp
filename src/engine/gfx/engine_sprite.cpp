// The engine's sprite and textured quad (see engine_sprite.h).

#include "engine/gfx/engine_sprite.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/core/owner.h"

namespace render {

namespace {

constexpr uint32_t kBlend = 0x54628ED7; // "blend"

} // namespace

// --- the sprite ---------------------------------------------------------------------------------------------

// 0x0049ED60
BillboardQuad::BillboardQuad(void* window)
{
    pos_x_ = -1.0f;
    y_ = -1.0f;
    hot_x_ = -1.0f;
    hot_y_ = -1.0f;
    bitmap_ = nullptr;
    scale_h_ = 1.0f;
    scale_v_ = 1.0f;
    orientation_turn_ = 0.0f;
    rank_ = 0.0f;
    span_ = 0;
    level_y_ = 0;
    std::memset(rgba_value_, 0x80, sizeof(rgba_value_));
    screen_region_ = window;
    invisible_ = true;
    use_depth_ = false;
    flip_v_ = false;
    flip_u_ = false;
    mix_mode_ = kBlend;
}

// 0x0048D1B0 (folded with GlyphString::change_pane)
void BillboardQuad::change_pane(void* window)
{
    screen_region_ = window;
    update_window();
}

// 0x0049EBB0
void BillboardQuad::change_concealed(bool hidden)
{
    invisible_ = hidden;
    update_hidden();
}

// 0x0049EBC0
void BillboardQuad::set_texture(uint8_t* texture)
{
    bitmap_ = texture;
    if (texture) {
        span_ = texels_across(texture);
        level_y_ = texels_down(texture);
    }
    update_settings();
}

// 0x0049EC00
void BillboardQuad::choose_blend_mode(uint32_t blend)
{
    mix_mode_ = blend;
    update_settings();
}

// 0x0049EC10
void BillboardQuad::change_location(float x, float y)
{
    pos_x_ = x;
    y_ = y;
    update_settings();
}

// 0x0049EC30
void BillboardQuad::change_extent(uint16_t width, uint16_t height)
{
    span_ = width;
    level_y_ = height;
    update_settings();
}

// 0x0049EE00 (the products at 53 bits - exact for floats -, the quotients rounded to floats once)
void BillboardQuad::change_scale(float xscale, float yscale)
{
    float width = 1.0f;
    float height = 1.0f;
    if (bitmap_) {
        width = static_cast<float>(texels_across(bitmap_));
        height = static_cast<float>(texels_down(bitmap_));
    }
    constexpr float kNarrowest = 1.25f;                             // 0x00649AC4
    constexpr float kMinHeight = std::bit_cast<float>(0x3F955326u); // 0x00649AC0: 1.1666
    if (xscale > 0.0f && static_cast<double>(width) * xscale <= kNarrowest)
        xscale = static_cast<float>(static_cast<double>(kNarrowest) / width);
    if (yscale > 0.0f && static_cast<double>(height) * yscale <= kMinHeight)
        yscale = static_cast<float>(static_cast<double>(kMinHeight) / height);
    scale_v_ = yscale;
    scale_h_ = xscale;
    update_settings();
}

// 0x0049EC50
void BillboardQuad::set_flip(bool flip_v, bool flip_u)
{
    flip_v_ = flip_v;
    flip_u_ = flip_u;
    update_settings();
}

// 0x0049EC70
void BillboardQuad::set_hot_spot(float x, float y)
{
    hot_x_ = x;
    hot_y_ = y;
    update_settings();
}

// 0x0049EC90
void BillboardQuad::change_spin(float rotation)
{
    orientation_turn_ = rotation;
    update_settings();
}

// 0x0049ECA0
void BillboardQuad::change_rank(float priority)
{
    rank_ = priority;
    use_depth_ = false;
    update_priority();
}

// 0x0049ECC0
void BillboardQuad::set_depth(uint32_t depth)
{
    layer_ = depth;
    use_depth_ = true;
    update_priority();
}

// 0x0049ECE0
void BillboardQuad::set_z_write(bool z_write)
{
    z_write_ = z_write;
    update_settings();
}

// 0x0049ECF0
void BillboardQuad::change_rgba(uint32_t rgba)
{
    std::memcpy(rgba_value_, &rgba, sizeof(rgba_value_));
    update_settings();
}

// --- the textured quad ----------------------------------------------------------------------------------------

// 0x0049EED0
TexturedQuad::TexturedQuad(void* window)
{
    screen_region_ = window;
    bitmap_ = nullptr;
    std::memset(rgba_value_, 0x80, sizeof(rgba_value_));
    invisible_ = true;
    use_depth_ = false;
}

// 0x0049EF30
memory::owner<TexturedQuad*> TexturedQuad::fabricate() { return new TexturedQuad(nullptr); }

// 0x0049ED10
void TexturedQuad::change_concealed(bool hidden) { invisible_ = hidden; }

// 0x0049ED40
void TexturedQuad::set_texture(uint8_t* texture) { bitmap_ = texture; }

// 0x0049ED00
void TexturedQuad::choose_blend_mode(uint32_t blend) { mix_mode_ = blend; }

// 0x0049ED20
void TexturedQuad::change_rank(float priority)
{
    rank_ = priority;
    use_depth_ = false;
}

// 0x0049ED50
void TexturedQuad::change_rgba(uint32_t rgba) { std::memcpy(rgba_value_, &rgba, sizeof(rgba_value_)); }

} // namespace render
