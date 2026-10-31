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

SOverlayDraw*& Foremost() { return g_draw2dHead; }
uint32_t& ZTestNextFrame() { return g_draw2dZTestNextFrame; }
void** BaseVtable() { return kSDraw2DVtable; }

void Call(SOverlayDraw* item, int slot) { reinterpret_cast<void(__thiscall*)(SOverlayDraw*)>(item->vtable[slot])(item); }

} // namespace

// 0x004DF900
void SOverlayDraw::Splice()
{
    SOverlayDraw* head = Foremost();
    // (a priority that isn't a number goes on past every item)
    if (!head || priority <= head->priority) {
        next = head;
        Foremost() = this;
        return;
    }
    SOverlayDraw* at = head;
    while (at->next && !(priority <= at->next->priority))
        at = at->next;
    next = at->next;
    at->next = this;
}

// 0x004DF950
void SOverlayDraw::DropOut()
{
    SOverlayDraw* at = Foremost();
    if (at == this) {
        Foremost() = next;
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
SOverlayDraw* SOverlayDraw::MakeNew(float newPriority, bool newHidden)
{
    priority = newPriority;
    vtable = BaseVtable();
    hidden = newHidden;
    z = 0.0f;
    next = nullptr;
    if (!newHidden)
        Splice();
    return this;
}

// 0x004DFAA0
void SOverlayDraw::DestroyBody()
{
    vtable = BaseVtable();
    DropOut();
}

// 0x004DFC90
SOverlayDraw* SOverlayDraw::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Sole()->Dealloc(this);
    return this;
}

// 0x004DFAE0
void SOverlayDraw::ChangeRank(float newPriority)
{
    if (priority == newPriority)
        return;
    priority = newPriority;
    if (!hidden) {
        DropOut();
        Splice();
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
        DropOut();
        Splice();
    }
}

// 0x004DFB70
void SOverlayDraw::ChangeConcealed(uint8_t newHidden)
{
    if (hidden == newHidden)
        return;
    hidden = newHidden;
    if (newHidden)
        DropOut();
    else
        Splice();
}

// 0x004DFB90
void __cdecl Draw2D()
{
    ChooseBlendMode(5);
    ChangePhase(kAddressMode0, 0x10001);
    ChangePhase(kDepthBias, 0);
    ChangePhase(kAlphaRef, 1);
    ChangePhase(kZWrite, 0);
    SetTexture(1, nullptr);
    SetTexture(2, nullptr);
    SetTexture(3, nullptr);
    ChangePhase(kZTest, ZTestNextFrame());
    uint32_t zTest = 0;
    for (SOverlayDraw* item = Foremost(); item; item = item->next) {
        if (item->hidden)
            continue;
        Call(item, 1); // OpenSketch
        Call(item, 2); // Draw
        Call(item, 3); // CloseSketch
        if (!zTest && item->z > 0.0f)
            zTest = 1;
    }
    ZTestNextFrame() = zTest;
}

namespace {
static VtableSlot kSDraw2DVtable_slots[] = {&Engine3D::SOverlayDraw::Teardown, &_purecall,
                &_purecall, &_purecall};
void** const kSDraw2DVtable = reinterpret_cast<void**>(kSDraw2DVtable_slots);
} // namespace

} // namespace Nx
