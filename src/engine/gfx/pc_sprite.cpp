// The PC sprite: see pc_sprite.h.

#include "engine/gfx/pc_sprite.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/draw2d.h"
#include "engine/gfx/engine_sprite.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/gfx/pc_font.h"

namespace render {

namespace {

extern void** const kSpriteVtable;

OverlayDraw* as_drawn(QuadSprite* sprite) { return reinterpret_cast<OverlayDraw*>(sprite); }

} // namespace

// 0x004C70E0
PcSprite* PcSprite::make_new()
{
    reinterpret_cast<BillboardQuad*>(this)->BillboardQuad::make_new(nullptr);
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    void* memory = memory::alloc(0x40);
    sprite = memory ? static_cast<QuadSprite*>(memory)->make_new(0.0f) : nullptr;
    return this;
}

// 0x004C7050
void PcSprite::destroy_body()
{
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    if (sprite) // (through its vtable: its destructor, freed)
        reinterpret_cast<QuadSprite*(__thiscall*)(QuadSprite*, unsigned int)>(sprite->vtable[0])(sprite, 1);
    reinterpret_cast<BillboardQuad*>(this)->BillboardQuad::destroy_body();
}

// 0x004C7180
PcSprite* PcSprite::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004C70B0
void PcSprite::update_hidden()
{
    // (the byte as it is: change_concealed compares and keeps what it's given, a 2 included - not normalised to 0 / 1)
    as_drawn(sprite)->change_concealed(hidden);
}

// 0x004C71B0
void PcSprite::tick_frame()
{
    if (texture) {
        uint8_t* holder = *reinterpret_cast<uint8_t**>(texture + 0x10);
        if (texture[8] & 1)
            holder = *reinterpret_cast<uint8_t**>(holder);
        sprite->texture = reinterpret_cast<SpriteTexture*>(holder);
    }
    std::memcpy(&sprite->flip_v_, &flip_v_, 2); // (the bytes as they are)
    sprite->x = x;
    sprite->y = y;
    sprite->width = width;
    sprite->height = height;
    sprite->xscale = xscale;
    sprite->yscale = yscale;
    sprite->hot_x_ = static_cast<float>((static_cast<double>(hot_x_) + 1.0f) * 0.5f * static_cast<int>(width));
    sprite->hot_y_ = static_cast<float>((static_cast<double>(hot_y_) + 1.0f) * 0.5f * static_cast<int>(height));
    sprite->rotation = rotation;
    std::memcpy(&sprite->color, colour, 4);
}

// 0x004C70C0
void PcSprite::update_priority()
{
    if (sprite)
        as_drawn(sprite)->change_rank(priority);
}

namespace {
static VtableSlot g_sprite_vtable_slots[] = {&render::PcSprite::teardown,        &render::PcText::tick_frame,
                                             &render::PcSprite::update_hidden,   &render::PcSprite::tick_frame,
                                             &render::PcSprite::update_priority, stubs::do_nothing};
void** const kSpriteVtable = reinterpret_cast<void**>(g_sprite_vtable_slots);
} // namespace

} // namespace render
