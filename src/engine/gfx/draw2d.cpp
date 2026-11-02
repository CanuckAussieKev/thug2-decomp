// The 2D draw list: see draw2d.h.

#include "engine/gfx/draw2d.h"

#include <cstdint>

#include "engine/gfx/render_state.h"
#include "engine/core/no_destroy.h"

namespace render {

namespace {

// NOT ORIGINAL grouping - draw2d.cpp's state (were globals). Made on first use, never destroyed (draw2d_state()).
struct Draw2dState {
    OverlayDraw* draw2d_head_{};
    uint32_t draw2d_z_test_next_frame_{};
};

Draw2dState& draw2d_state()
{
    static core::NoDestroy<Draw2dState> state;
    return state.get();
}

OverlayDraw*& foremost() { return draw2d_state().draw2d_head_; }
uint32_t& z_test_next_frame() { return draw2d_state().draw2d_z_test_next_frame_; }

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
OverlayDraw::OverlayDraw(float new_priority, bool new_hidden) : hidden(new_hidden), priority(new_priority)
{
    if (!new_hidden)
        splice();
}

// 0x004DFAA0
OverlayDraw::~OverlayDraw() { drop_out(); }

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
void draw_2d()
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
        item->open_sketch();
        item->draw();
        item->close_sketch();
        if (!z_test && item->z > 0.0f)
            z_test = 1;
    }
    z_test_next_frame() = z_test;
}

} // namespace render
