// The engine's sprite and textured quad (see engine_sprite.h).

#include "engine/gfx/engine_sprite.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"

namespace render {

namespace {

extern void** const kSpriteVtable;
extern void** const kQuadVtable;
constexpr uint32_t kBlend = 0x54628ED7; // "blend"

// The platform's slot `slot` (no arguments).
template <typename T>
void notify(T* object, int slot)
{
    reinterpret_cast<void(__thiscall*)(T*)>(object->vtable[slot])(object);
}

} // namespace

// --- the sprite ---------------------------------------------------------------------------------------------

// 0x0049ED60
BillboardQuad* BillboardQuad::make_new(void* window)
{
    pos_x_ = -1.0f;
    y_ = -1.0f;
    hot_x_ = -1.0f;
    hot_y_ = -1.0f;
    vtable = reinterpret_cast<void**>(kSpriteVtable);
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
    return this;
}

// 0x0049EBA0
void BillboardQuad::destroy_body() { vtable = reinterpret_cast<void**>(kSpriteVtable); }

// 0x0049EDD0
BillboardQuad* BillboardQuad::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x0049EBB0
void BillboardQuad::change_concealed(bool hidden)
{
    invisible_ = hidden;
    notify(this, 2);
}

// 0x0049EBC0
void BillboardQuad::set_texture(uint8_t* texture)
{
    bitmap_ = texture;
    if (texture) {
        span_ = texels_across(texture);
        level_y_ = texels_down(texture);
    }
    notify(this, 3);
}

// 0x0049EC00
void BillboardQuad::choose_blend_mode(uint32_t blend)
{
    mix_mode_ = blend;
    notify(this, 3);
}

// 0x0049EC10
void BillboardQuad::change_location(float x, float y)
{
    pos_x_ = x;
    y_ = y;
    notify(this, 3);
}

// 0x0049EC30
void BillboardQuad::change_extent(uint16_t width, uint16_t height)
{
    span_ = width;
    level_y_ = height;
    notify(this, 3);
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
    notify(this, 3);
}

// 0x0049EC50
void BillboardQuad::set_flip(bool flip_v, bool flip_u)
{
    flip_v_ = flip_v;
    flip_u_ = flip_u;
    notify(this, 3);
}

// 0x0049EC70
void BillboardQuad::set_hot_spot(float x, float y)
{
    hot_x_ = x;
    hot_y_ = y;
    notify(this, 3);
}

// 0x0049EC90
void BillboardQuad::change_spin(float rotation)
{
    orientation_turn_ = rotation;
    notify(this, 3);
}

// 0x0049ECA0
void BillboardQuad::change_rank(float priority)
{
    rank_ = priority;
    use_depth_ = false;
    notify(this, 4);
}

// 0x0049ECC0
void BillboardQuad::set_depth(uint32_t depth)
{
    layer_ = depth;
    use_depth_ = true;
    notify(this, 4);
}

// 0x0049ECE0
void BillboardQuad::set_z_write(bool z_write)
{
    z_write_ = z_write;
    notify(this, 3);
}

// 0x0049ECF0
void BillboardQuad::change_rgba(uint32_t rgba)
{
    std::memcpy(rgba_value_, &rgba, sizeof(rgba_value_));
    notify(this, 3);
}

// --- the textured quad ----------------------------------------------------------------------------------------

// 0x0049EED0
TexturedQuad* TexturedQuad::make_new(void* window)
{
    screen_region_ = window;
    vtable = reinterpret_cast<void**>(kQuadVtable);
    bitmap_ = nullptr;
    std::memset(rgba_value_, 0x80, sizeof(rgba_value_));
    invisible_ = true;
    use_depth_ = false;
    return this;
}

// 0x0049EF00
TexturedQuad* TexturedQuad::teardown(unsigned int free_flags)
{
    vtable = reinterpret_cast<void**>(kQuadVtable);
    if (free_flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x0049EF30
TexturedQuad* TexturedQuad::fabricate()
{
    void* memory = memory::alloc(sizeof(TexturedQuad));
    return memory ? static_cast<TexturedQuad*>(memory)->make_new(nullptr) : nullptr;
}

// 0x0049ED10
void TexturedQuad::change_concealed(bool hidden)
{
    invisible_ = hidden;
    notify(this, 4);
}

// 0x0049ED40
void TexturedQuad::set_texture(uint8_t* texture)
{
    bitmap_ = texture;
    notify(this, 5);
}

// 0x0049ED00
void TexturedQuad::choose_blend_mode(uint32_t blend)
{
    mix_mode_ = blend;
    notify(this, 5);
}

// 0x0049ED20
void TexturedQuad::change_rank(float priority)
{
    rank_ = priority;
    use_depth_ = false;
    notify(this, 6);
}

// 0x0049ED50
void TexturedQuad::change_rgba(uint32_t rgba)
{
    std::memcpy(rgba_value_, &rgba, sizeof(rgba_value_));
    notify(this, 5);
}

namespace {
static VtableSlot g_sprite_vtable_slots[] = {&render::BillboardQuad::teardown,
                                             stubs::do_nothing,
                                             stubs::do_nothing,
                                             stubs::do_nothing,
                                             stubs::do_nothing,
                                             stubs::do_nothing};
void** const kSpriteVtable = reinterpret_cast<void**>(g_sprite_vtable_slots);
static VtableSlot g_quad_vtable_slots[] = {&render::TexturedQuad::teardown,
                                           &render::TexturedQuad::fabricate,
                                           stubs::do_nothing,
                                           stubs::do_nothing,
                                           stubs::do_nothing,
                                           stubs::do_nothing,
                                           stubs::do_nothing,
                                           stubs::do_nothing,
                                           stubs::ignore_argument,
                                           stubs::ignore_argument};
void** const kQuadVtable = reinterpret_cast<void**>(g_quad_vtable_slots);
} // namespace

} // namespace render
