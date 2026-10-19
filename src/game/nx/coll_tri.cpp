// Collision geometry (CCollObjTriData).

#include "game/nx/coll_tri.h"

#include <cstring>

#include "hook/hook.h"

namespace Nx {

// 0x0040B230
Mth::Vector CCollObjTriData::GetRawVertexPos(int index) const
{
    if (m_compressed) {
        const uint16_t* v = static_cast<const uint16_t*>(m_vertices) + 3 * index;
        return {v[0] * 0.0625f + m_min.x, v[1] * 0.0625f + m_min.y, v[2] * 0.0625f + m_min.z, 0.0f};
    }
    const float* v = static_cast<const float*>(m_vertices) + 3 * index;
    return {v[0], v[1], v[2], 0.0f};
}

// 0x0040B2D0 (the uncompressed vertex copied as raw words)
void CCollObjTriData::GetRawVertexPos(int index, Mth::Vector* out) const
{
    if (m_compressed) {
        const uint16_t* v = static_cast<const uint16_t*>(m_vertices) + 3 * index;
        out->x = v[0] * 0.0625f + m_min.x;
        out->y = v[1] * 0.0625f + m_min.y;
        out->z = v[2] * 0.0625f + m_min.z;
        out->w = 0.0f;
        return;
    }
    std::memcpy(out, static_cast<const float*>(m_vertices) + 3 * index, 12);
    out->w = 0.0f;
}

} // namespace Nx

REPLACE(0x0040B230, static_cast<Mth::Vector (Nx::CCollObjTriData::*)(int) const>(&Nx::CCollObjTriData::GetRawVertexPos));
REPLACE(0x0040B2D0,
        static_cast<void (Nx::CCollObjTriData::*)(int, Mth::Vector*) const>(&Nx::CCollObjTriData::GetRawVertexPos));
