// The 2D draw list: see draw2d.h.

#include "game/gfx/draw2d.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/gfx/render_state.h"
#include "game/core/code_address.h"

namespace Engine3D {

namespace {
extern void** const kSDraw2DVtable;
} // namespace

namespace {

SOverlayDraw* g_draw2dHead;
uint32_t g_draw2dZTestNextFrame;

SOverlayDraw*& Head() { return g_draw2dHead; }
uint32_t& ZTestNextFrame() { return g_draw2dZTestNextFrame; }
void** BaseVtable() { return kSDraw2DVtable; }

void Call(SOverlayDraw* item, int slot) { reinterpret_cast<void(__thiscall*)(SOverlayDraw*)>(item->vtable[slot])(item); }

} // namespace

// 0x004DF900
void SOverlayDraw::Insert()
{
    SOverlayDraw* head = Head();
    // (a priority that isn't a number goes on past every item)
    if (!head || priority <= head->priority) {
        next = head;
        Head() = this;
        return;
    }
    SOverlayDraw* at = head;
    while (at->next && !(priority <= at->next->priority))
        at = at->next;
    next = at->next;
    at->next = this;
}

// 0x004DF950
void SOverlayDraw::Remove()
{
    SOverlayDraw* at = Head();
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
SOverlayDraw* SOverlayDraw::Construct(float newPriority, bool newHidden)
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
void SOverlayDraw::DestroyBody()
{
    vtable = BaseVtable();
    Remove();
}

// 0x004DFC90
SOverlayDraw* SOverlayDraw::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Instance()->Free(this);
    return this;
}

// 0x004DFAE0
void SOverlayDraw::SetPriority(float newPriority)
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
void SOverlayDraw::SetZ(float newZ)
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
void SOverlayDraw::SetHidden(uint8_t newHidden)
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
    ChooseBlendMode(5);
    SetState(kAddressMode0, 0x10001);
    SetState(kDepthBias, 0);
    SetState(kAlphaRef, 1);
    SetState(kZWrite, 0);
    SetTexture(1, nullptr);
    SetTexture(2, nullptr);
    SetTexture(3, nullptr);
    SetState(kZTest, ZTestNextFrame());
    uint32_t zTest = 0;
    for (SOverlayDraw* item = Head(); item; item = item->next) {
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

namespace {
static VtableSlot kSDraw2DVtable_slots[] = {&Engine3D::SOverlayDraw::Destroy, &_purecall,
                &_purecall, &_purecall};
void** const kSDraw2DVtable = reinterpret_cast<void**>(kSDraw2DVtable_slots);
} // namespace

} // namespace Nx
