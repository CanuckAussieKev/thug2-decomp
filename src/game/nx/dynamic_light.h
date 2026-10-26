#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

namespace Nx {

// A dynamic light (made by 0x0048FB80, no source yet): a point light at a position with a radius, an intensity and a
// colour, held by an object (the dynamic light component). Only what has source so far.
struct CDynamicLight {
    // 0x0041CB90: the radius, and its inverse (1e10 for a radius of 0).
    void SetRadius(float radius);

    uint8_t unknown00[4];
    Mth::Vector m_pos;       // +0x04
    uint8_t unknown14[4];
    float m_radius;          // +0x18
    float m_inverseRadius;   // +0x1C
    float m_intensity;       // +0x20
    uint32_t m_color;        // +0x24 (bytes r, g, b, a)
    uint32_t m_ownerId;      // +0x28 the id of the object it belongs to (0 once it's let go)
};
static_assert(offsetof(CDynamicLight, m_pos) == 0x04 && offsetof(CDynamicLight, m_radius) == 0x18 &&
                  offsetof(CDynamicLight, m_color) == 0x24 && offsetof(CDynamicLight, m_ownerId) == 0x28,
              "layout must match the original");

} // namespace Nx
