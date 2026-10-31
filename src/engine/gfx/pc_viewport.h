#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"

// The PC viewport (Nx engine: the engine's ViewRegion - constructor 0x004A0DB0, destructor body 0x004A0E60 - over it;
// the viewports at 0x00701350): a world point onto the 640 x 448 screen, and the occlusion queries the sun's
// visibility test draws (nothing drawn to the colour buffer; not on a Radeon 8500).
namespace render {


// The PC viewport (0x24 bytes, vtable 0x0064A848).
struct PcViewport {
    void** vtable;
    uint8_t engine04[0x20];

    // 0x004CB8E0: construct (the engine's constructor with both). Returns this.
    PcViewport* make_new(uint32_t a1, uint32_t a2);
    // 0x004CB900: the destructor's body (a jump to the engine's).
    void destroy_body();
    // 0x004CB910 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    PcViewport* teardown(unsigned int free_flags);
    // 0x004CB940 (virtual [1]): a world point through the view (0x00786BD0) and projection (0x00786C10) matrices onto
    // the screen (*x, *y); in front of the far plane its depth in 65536ths into *depth and a size factor returned -
    // 300 / the view depth, kept within 0x0070137C and 3 (0x0067E54C) - else -1 returned.
    float project_to_screen(const maths::Vec4* point, float* x, float* y, int* depth);
    // 0x004CBC00 (virtual [2]): an occlusion query (under `query`) round a degenerate strip at a world point. Returns the
    // query (0 for none made: query 0, or a Radeon 8500).
    uint8_t query_point(const maths::Vec4* point, int unused, uint8_t query);
    // 0x004CBA90 (virtual [3]): the same over a screen rectangle at depth `z`.
    uint8_t query_rect(float x0, float y0, float x1, float y1, uint32_t z, uint8_t query);
};
static_assert(sizeof(PcViewport) == 0x24, "layout must match the original");

// 0x004CB860: a new PC viewport (null without memory).
PcViewport* __cdecl create_viewport(uint32_t a1, uint32_t a2);
// 0x004CBD40 (the engine's 0x004A0F20 jumps here): the pixels the first request's query saw - its flag
// (0x0067E550) cleared and the requests moved on (0x004A0F80) when it's in, the last count while it's coming, 0 when
// there's none (or on a Radeon 8500).
uint32_t __cdecl occlusion_pixels();

extern uint8_t g_occlusion_slots[1024];
extern float g_scale_2d_in_3d_numerator;
extern float g_scale_2d_in_3d_max;
extern float g_scale_2d_in_3d_min;

} // namespace Nx
