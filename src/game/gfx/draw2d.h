#pragma once

#include <cstdint>

// The 2D draw list (Nx engine, PC): screen texts and sprites are kept in one list sorted by priority (0x007CC248,
// lowest first) and drawn in that order by Draw2D. The base of SText (text.h) and SSprite (sprite.h).
namespace Nx {

struct SDraw2D {
    void** vtable;   // 0x0064AD18 for the base: [0] destroy, [1] BeginDraw, [2] Draw, [3] EndDraw
    SDraw2D* next;
    bool hidden;     // not in the list
    uint8_t pad[3];
    float priority;
    float z;         // above 0: the next frame's z test on (0x007CC24C)

    // 0x004DFA70: construct with this priority, in the list unless hidden. Returns this.
    SDraw2D* Construct(float priority, bool hidden);
    // 0x004DFAA0: the destructor's body (out of the list).
    void DestroyBody();
    // 0x004DFC90 (virtual [0]): destroy, and free if flags & 1. Returns this.
    SDraw2D* Destroy(unsigned int flags);
    // 0x004DF900: into the list: before the first of higher or equal priority.
    void Insert();
    // 0x004DF950: out of the list.
    void Remove();
    // 0x004DFAE0: a new priority (moved in the list if it's in).
    void SetPriority(float priority);
    // 0x004DFB20: the z; above 0 the priority becomes 0.
    void SetZ(float z);
    // 0x004DFB70: hide (out of the list) or show (in).
    void SetHidden(bool hidden);
};
static_assert(sizeof(SDraw2D) == 0x14, "layout must match the original");

// 0x004DFB90: draw the list (each shown item's BeginDraw, Draw, EndDraw) with the 2D states, the z test as the last
// frame's items wanted.
void __cdecl Draw2D();

} // namespace Nx
