// The 2D draw list: see draw2d.h.

#include "game/gfx/draw2d.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

SDraw2D*& Head() { return GameVariable<SDraw2D*>(0x007CC248); }
uint32_t& ZTestNextFrame() { return GameVariable<uint32_t>(0x007CC24C); }
void** BaseVtable() { return reinterpret_cast<void**>(0x0064AD18); }

void Call(SDraw2D* item, int slot) { reinterpret_cast<void(__thiscall*)(SDraw2D*)>(item->vtable[slot])(item); }

} // namespace

// 0x004DF900
void SDraw2D::Insert()
{
    SDraw2D* head = Head();
    // (a priority that isn't a number goes on past every item)
    if (!head || priority <= head->priority) {
        next = head;
        Head() = this;
        return;
    }
    SDraw2D* at = head;
    while (at->next && !(priority <= at->next->priority))
        at = at->next;
    next = at->next;
    at->next = this;
}

// 0x004DF950
void SDraw2D::Remove()
{
    SDraw2D* at = Head();
    if (at == this) {
        Head() = next;
        return;
    }
    if (!at)
        return;
    for (; at->next; at = at->next) {
        if (at->next == this) {
            at->next = next;
            return;
        }
    }
}

// 0x004DFA70
SDraw2D* SDraw2D::Construct(float newPriority, bool newHidden)
{
    priority = newPriority;
    vtable = BaseVtable();
    hidden = newHidden;
    z = 0.0f;
    next = nullptr;
    if (!newHidden)
        Insert();
    return this;
}

// 0x004DFAA0
void SDraw2D::DestroyBody()
{
    vtable = BaseVtable();
    Remove();
}

// 0x004DFC90
SDraw2D* SDraw2D::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Instance()->Free(this);
    return this;
}

// 0x004DFAE0
void SDraw2D::SetPriority(float newPriority)
{
    if (priority == newPriority)
        return;
    priority = newPriority;
    if (!hidden) {
        Remove();
        Insert();
    }
}

// 0x004DFB20
void SDraw2D::SetZ(float newZ)
{
    z = newZ;
    if (!(newZ > 0.0f))
        return;
    if (priority == 0.0f)
        return;
    priority = 0.0f;
    if (!hidden) {
        Remove();
        Insert();
    }
}

// 0x004DFB70
void SDraw2D::SetHidden(bool newHidden)
{
    if (hidden == newHidden)
        return;
    hidden = newHidden;
    if (newHidden)
        Remove();
    else
        Insert();
}

// 0x004DFB90
void __cdecl Draw2D()
{
    SetBlendMode(5);
    SetState(kAddressMode0, 0x10001);
    SetState(kDepthBias, 0);
    SetState(kAlphaRef, 1);
    SetState(kZWrite, 0);
    SetTexture(1, nullptr);
    SetTexture(2, nullptr);
    SetTexture(3, nullptr);
    SetState(kZTest, ZTestNextFrame());
    uint32_t zTest = 0;
    for (SDraw2D* item = Head(); item; item = item->next) {
        if (item->hidden)
            continue;
        Call(item, 1); // BeginDraw
        Call(item, 2); // Draw
        Call(item, 3); // EndDraw
        if (!zTest && item->z > 0.0f)
            zTest = 1;
    }
    ZTestNextFrame() = zTest;
}

} // namespace Nx

REPLACE(0x004DF900, &Nx::SDraw2D::Insert);
REPLACE(0x004DF950, &Nx::SDraw2D::Remove);
REPLACE(0x004DFA70, &Nx::SDraw2D::Construct);
REPLACE(0x004DFAA0, &Nx::SDraw2D::DestroyBody);
REPLACE(0x004DFC90, &Nx::SDraw2D::Destroy);
REPLACE(0x004DFAE0, &Nx::SDraw2D::SetPriority);
REPLACE(0x004DFB20, &Nx::SDraw2D::SetZ);
REPLACE(0x004DFB70, &Nx::SDraw2D::SetHidden);
REPLACE(0x004DFB90, Nx::Draw2D);
