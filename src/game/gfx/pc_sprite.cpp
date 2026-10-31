// The PC sprite: see pc_sprite.h.

#include "game/gfx/pc_sprite.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/draw2d.h"
#include "game/gfx/engine_sprite.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/gfx/pc_font.h"

namespace Engine3D {

namespace {

extern void** const kSpriteVtable;

SOverlayDraw* AsDrawn(SQuadSprite* sprite) { return reinterpret_cast<SOverlayDraw*>(sprite); }

} // namespace

// 0x004C70E0
CPcSprite* CPcSprite::MakeNew()
{
    reinterpret_cast<CBillboardQuad*>(this)->CBillboardQuad::MakeNew(nullptr);
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    void* memory = MemSys::Alloc(0x40);
    sprite = memory ? static_cast<SQuadSprite*>(memory)->MakeNew(0.0f) : nullptr;
    return this;
}

// 0x004C7050
void CPcSprite::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    if (sprite) // (through its vtable: its destructor, freed)
        reinterpret_cast<SQuadSprite*(__thiscall*)(SQuadSprite*, unsigned int)>(sprite->vtable[0])(sprite, 1);
    reinterpret_cast<CBillboardQuad*>(this)->CBillboardQuad::DestroyBody();
}

// 0x004C7180
CPcSprite* CPcSprite::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004C70B0
void CPcSprite::UpdateHidden()
{
    // (the byte as it is: ChangeConcealed compares and keeps what it's given, a 2 included - not normalised to 0 / 1)
    AsDrawn(sprite)->ChangeConcealed(hidden);
}

// 0x004C71B0
void CPcSprite::TickFrame()
{
    if (texture) {
        uint8_t* holder = *reinterpret_cast<uint8_t**>(texture + 0x10);
        if (texture[8] & 1)
            holder = *reinterpret_cast<uint8_t**>(holder);
        sprite->texture = reinterpret_cast<SSpriteTexture*>(holder);
    }
    std::memcpy(&sprite->flipV, &flipV, 2); // (the bytes as they are)
    sprite->x = x;
    sprite->y = y;
    sprite->width = width;
    sprite->height = height;
    sprite->xscale = xscale;
    sprite->yscale = yscale;
    sprite->hotX = static_cast<float>((static_cast<double>(hotX) + 1.0f) * 0.5f * static_cast<int>(width));
    sprite->hotY = static_cast<float>((static_cast<double>(hotY) + 1.0f) * 0.5f * static_cast<int>(height));
    sprite->rotation = rotation;
    std::memcpy(&sprite->color, colour, 4);
}

// 0x004C70C0
void CPcSprite::UpdatePriority()
{
    if (sprite)
        AsDrawn(sprite)->ChangeRank(priority);
}

namespace {
static VtableSlot kSpriteVtable_slots[] = {&Engine3D::CPcSprite::Teardown, &Engine3D::CPcText::TickFrame, &Engine3D::CPcSprite::UpdateHidden,
                &Engine3D::CPcSprite::TickFrame, &Engine3D::CPcSprite::UpdatePriority, Stubs::DoNothing};
void** const kSpriteVtable = reinterpret_cast<void**>(kSpriteVtable_slots);
} // namespace

} // namespace Nx
