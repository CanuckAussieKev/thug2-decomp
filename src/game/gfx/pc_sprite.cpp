// The PC sprite: see pc_sprite.h.

#include "game/gfx/pc_sprite.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/draw2d.h"
#include "game/gfx/engine_sprite.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/core/stubs.h"
#include "game/gfx/pc_font.h"

namespace Nx {

namespace {

extern void** const kSpriteVtable;

SDraw2D* AsDrawn(SSprite* sprite) { return reinterpret_cast<SDraw2D*>(sprite); }

} // namespace

// 0x004C70E0
CPcSprite* CPcSprite::Construct()
{
    reinterpret_cast<CSprite*>(this)->CSprite::Construct(nullptr);
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    void* memory = Mem::New(0x40);
    sprite = memory ? static_cast<SSprite*>(memory)->Construct(0.0f) : nullptr;
    return this;
}

// 0x004C7050
void CPcSprite::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kSpriteVtable);
    if (sprite) // (through its vtable: its destructor, freed)
        reinterpret_cast<SSprite*(__thiscall*)(SSprite*, unsigned int)>(sprite->vtable[0])(sprite, 1);
    reinterpret_cast<CSprite*>(this)->CSprite::DestroyBody();
}

// 0x004C7180
CPcSprite* CPcSprite::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C70B0
void CPcSprite::UpdateHidden()
{
    // (the byte as it is: SetHidden compares and keeps what it's given, a 2 included - not normalised to 0 / 1)
    bool raw;
    std::memcpy(&raw, &hidden, 1);
    AsDrawn(sprite)->SetHidden(raw);
}

// 0x004C71B0
void CPcSprite::Update()
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
        AsDrawn(sprite)->SetPriority(priority);
}

namespace {
GAME_VTABLE(kSpriteVtable, 0x0064A744, &Nx::CPcSprite::Destroy, &Nx::CPcText::Update, &Nx::CPcSprite::UpdateHidden,
            &Nx::CPcSprite::Update, &Nx::CPcSprite::UpdatePriority, Stubs::DoNothing);
} // namespace

} // namespace Nx

REPLACE(0x004C70E0, &Nx::CPcSprite::Construct);
REPLACE(0x004C7050, &Nx::CPcSprite::DestroyBody);
REPLACE(0x004C7180, &Nx::CPcSprite::Destroy);
REPLACE(0x004C70B0, &Nx::CPcSprite::UpdateHidden);
REPLACE(0x004C71B0, &Nx::CPcSprite::Update);
REPLACE(0x004C70C0, &Nx::CPcSprite::UpdatePriority);
