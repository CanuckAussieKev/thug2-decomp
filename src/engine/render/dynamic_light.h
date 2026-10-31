#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"

namespace render {

// A dynamic light (made by 0x0048FB80, no source yet): a point light at a position with a radius, an intensity and a
// colour, held by an object (the dynamic light component). Only what has source so far.
struct DynamicLight {
    // 0x0041CB90: the radius, and its inverse (1e10 for a radius of 0).
    void set_reach(float radius);

    uint8_t unknown00[4];
    maths::Vec4 at_point_;       // +0x04
    uint8_t unknown14[4];
    float radius_;          // +0x18
    float inverse_radius_;   // +0x1C
    float strength_;       // +0x20
    uint32_t tint_;        // +0x24 (bytes r, g, b, a)
    uint32_t holder_key_;      // +0x28 the id of the object it belongs to (0 once it's let go)
};
static_assert(offsetof(DynamicLight, at_point_) == 0x04 && offsetof(DynamicLight, radius_) == 0x18 &&
                  offsetof(DynamicLight, tint_) == 0x24 && offsetof(DynamicLight, holder_key_) == 0x28,
              "layout must match the original");

} // namespace Nx
