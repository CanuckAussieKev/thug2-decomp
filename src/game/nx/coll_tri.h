#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

namespace Nx {

// A collision object's geometry (Nx::CCollObjTriData, from a .col file): its vertices (floats, or 16-bit offsets
// from its box's minimum in sixteenths when "compressed"), faces and BSP tree. Only what's been rewritten so far
// is laid out here.
struct CCollObjTriData {
    uint32_t m_checksum;      // +0x00 the sector's name
    uint8_t pad04[7];
    bool m_compressed;        // +0x0B the vertices are 16-bit
    uint8_t pad0C[4];
    Mth::Vector m_min;        // +0x10 the box (+0x20 its max)
    Mth::Vector m_max;
    void* m_vertices;         // +0x30 float[3] each, or uint16_t[3] when compressed

    // 0x0040B230: vertex `index` (w = 0).
    Mth::Vector GetRawVertexPos(int index) const;
    // 0x0040B2D0: the same into `out`.
    void GetRawVertexPos(int index, Mth::Vector* out) const;
};
static_assert(offsetof(CCollObjTriData, m_compressed) == 0x0B, "layout must match the original");
static_assert(offsetof(CCollObjTriData, m_min) == 0x10, "layout must match the original");
static_assert(offsetof(CCollObjTriData, m_vertices) == 0x30, "layout must match the original");

} // namespace Nx
