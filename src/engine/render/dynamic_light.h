#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"

namespace Engine3D {

// A dynamic light (made by 0x0048FB80, no source yet): a point light at a position with a radius, an intensity and a
// colour, held by an object (the dynamic light component). Only what has source so far.
struct CDynamicLight {
    // 0x0041CB90: the radius, and its inverse (1e10 for a radius of 0).
    void SetReach(float radius);

    uint8_t unknown00[4];
    Maths::Vec4 m_atPoint;       // +0x04
    uint8_t unknown14[4];
    float m_radius;          // +0x18
    float m_inverseRadius;   // +0x1C
    float m_strength;       // +0x20
    uint32_t m_tint;        // +0x24 (bytes r, g, b, a)
    uint32_t m_holderKey;      // +0x28 the id of the object it belongs to (0 once it's let go)
};
static_assert(offsetof(CDynamicLight, m_atPoint) == 0x04 && offsetof(CDynamicLight, m_radius) == 0x18 &&
                  offsetof(CDynamicLight, m_tint) == 0x24 && offsetof(CDynamicLight, m_holderKey) == 0x28,
              "layout must match the original");

} // namespace Nx
