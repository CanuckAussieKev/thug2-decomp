#pragma once

#include <cstdint>

// The 2D draw list (Nx engine, PC): screen texts and sprites are kept in one list sorted by priority (0x007CC248,
// lowest first) and drawn in that order by draw_2d. The base of GlyphRun (text.h) and QuadSprite (billboard_quad.h).
namespace render {

struct OverlayDraw {
    void** vtable;   // 0x0064AD18 for the base: [0] destroy, [1] open_sketch, [2] Draw, [3] close_sketch
    OverlayDraw* next;
    uint8_t hidden;  // not in the list (a byte: change_concealed keeps any value it's given)
    uint8_t pad[3];
    float priority;
    float z;         // above 0: the next frame's z test on (0x007CC24C)

    // 0x004DFA70: construct with this priority, in the list unless hidden. Returns this.
    OverlayDraw* make_new(float priority, bool hidden);
    // 0x004DFAA0: the destructor's body (out of the list).
    void destroy_body();
    // 0x004DFC90 (virtual [0]): destroy, and free if flags & 1. Returns this.
    OverlayDraw* teardown(unsigned int flags);
    // 0x004DF900: into the list: before the first of higher or equal priority.
    void splice();
    // 0x004DF950: out of the list.
    void drop_out();
    // 0x004DFAE0: a new priority (moved in the list if it's in).
    void change_rank(float priority);
    // 0x004DFB20: the z; above 0 the priority becomes 0.
    void set_z(float z);
    // 0x004DFB70: hide (out of the list) or show (in).
    void change_concealed(uint8_t hidden);
};
static_assert(sizeof(OverlayDraw) == 0x14, "layout must match the original");

// 0x004DFB90: draw the list (each shown item's open_sketch, Draw, close_sketch) with the 2D states, the z test as the last
// frame's items wanted.
void __cdecl draw_2d();

} // namespace Nx
