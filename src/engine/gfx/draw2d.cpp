// The 2D draw list: see draw2d.h.

#include "engine/gfx/draw2d.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/gfx/render_state.h"
#include "engine/core/code_address.h"

namespace render {

namespace {
extern void** const kSDraw2dVtable;
} // namespace

namespace {

OverlayDraw* g_draw2d_head;
uint32_t g_draw2d_z_test_next_frame;

OverlayDraw*& foremost() { return g_draw2d_head; }
uint32_t& z_test_next_frame() { return g_draw2d_z_test_next_frame; }
void** base_vtable() { return kSDraw2dVtable; }

void get_call(OverlayDraw* item, int slot) { reinterpret_cast<void(__thiscall*)(OverlayDraw*)>(item->vtable[slot])(item); }

} // namespace

// 0x004DF900
void OverlayDraw::splice()
{
    OverlayDraw* head = foremost();
    // (a priority that isn't a number goes on past every item)
    if (!head || priority <= head->priority) {
        next = head;
        foremost() = this;
        return;
    }
    OverlayDraw* at = head;
    while (at->next && !(priority <= at->next->priority))
        at = at->next;
    next = at->next;
    at->next = this;
}

// 0x004DF950
void OverlayDraw::drop_out()
{
    OverlayDraw* at = foremost();
    if (at == this) {
        foremost() = next;
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
OverlayDraw* OverlayDraw::make_new(float new_priority, bool new_hidden)
{
    priority = new_priority;
    vtable = base_vtable();
    hidden = new_hidden;
    z = 0.0f;
    next = nullptr;
    if (!new_hidden)
        splice();
    return this;
}

// 0x004DFAA0
void OverlayDraw::destroy_body()
{
    vtable = base_vtable();
    drop_out();
}

// 0x004DFC90
OverlayDraw* OverlayDraw::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::sole()->dealloc(this);
    return this;
}

// 0x004DFAE0
void OverlayDraw::change_rank(float new_priority)
{
    if (priority == new_priority)
        return;
    priority = new_priority;
    if (!hidden) {
        drop_out();
        splice();
    }
}

// 0x004DFB20
void OverlayDraw::set_z(float new_z)
{
    z = new_z;
    if (!(new_z > 0.0f))
        return;
    if (priority == 0.0f)
        return;
    priority = 0.0f;
    if (!hidden) {
        drop_out();
        splice();
    }
}

// 0x004DFB70
void OverlayDraw::change_concealed(uint8_t new_hidden)
{
    if (hidden == new_hidden)
        return;
    hidden = new_hidden;
    if (new_hidden)
        drop_out();
    else
        splice();
}

// 0x004DFB90
void __cdecl draw_2d()
{
    choose_blend_mode(5);
    change_phase(kAddressMode0, 0x10001);
    change_phase(kDepthBias, 0);
    change_phase(kPhaseAlphaTest, 1);
    change_phase(kZWrite, 0);
    set_texture(1, nullptr);
    set_texture(2, nullptr);
    set_texture(3, nullptr);
    change_phase(kZTest, z_test_next_frame());
    uint32_t z_test = 0;
    for (OverlayDraw* item = foremost(); item; item = item->next) {
        if (item->hidden)
            continue;
        get_call(item, 1); // open_sketch
        get_call(item, 2); // Draw
        get_call(item, 3); // close_sketch
        if (!z_test && item->z > 0.0f)
            z_test = 1;
    }
    z_test_next_frame() = z_test;
}

namespace {
static VtableSlot g_s_draw_2d_vtable_slots[] = {&render::OverlayDraw::teardown, &_purecall,
                &_purecall, &_purecall};
void** const kSDraw2dVtable = reinterpret_cast<void**>(g_s_draw_2d_vtable_slots);
} // namespace

} // namespace Nx
