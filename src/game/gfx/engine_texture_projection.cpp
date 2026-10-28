// The engine's texture projection (see engine_texture_projection.h). The x87 arithmetic is followed at 53 bits: what
// the original keeps on the FPU stays a double here, what it stores is rounded to a float there.

#include "game/gfx/engine_texture_projection.h"

#include <cstring>

#include "game/core/dbg.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kProjectionVtable = 0x00649C14;
void** const kProjectionNodeVtable = reinterpret_cast<void**>(0x00646600);

float F(double value) { return static_cast<float>(value); }

// The camera of the first active viewport (0x004A09C0; null if none) and its position (0x00485A00).
void* FirstCamera() { return GameFunction<void*(__cdecl*)(int)>(0x004A09C0)(0); }
const Mth::Vector* CameraPosition(void* camera)
{
    return GameFunction<const Mth::Vector*(__thiscall*)(void*)>(0x00485A00)(camera);
}
// Whether a sphere is out of the camera's view (0x0048C490).
bool SphereOutOfView(const Mth::Vector* centre, float radius)
{
    return GameFunction<int(__cdecl*)(const Mth::Vector*, float)>(0x0048C490)(centre, radius) != 0;
}
// The collision grids a line reaches (0x0048C980: null-terminated), and the sector of a name in any scene
// (0x0048C510).
CCollGrid** GridsAlongLine(const Mth::Line* line)
{
    return GameFunction<CCollGrid**(__cdecl*)(const Mth::Line*)>(0x0048C980)(line);
}
void* FindSector(uint32_t name) { return GameFunction<void*(__cdecl*)(uint32_t)>(0x0048C510)(name); }

// The sectors and collision objects a projection reaches (0x00700F40 / 0x00701140, null-terminated).
void** ProjectedSectors() { return &GameVariable<void*>(0x00700F40); }
void** ProjectedCollision() { return &GameVariable<void*>(0x00701140); }

} // namespace

// 0x0049FE20
CTextureProjection* CTextureProjection::Construct()
{
    m_nodeData = reinterpret_cast<Mem::ListNode*>(this);
    m_nodePriority = 0;
    m_next = reinterpret_cast<Mem::ListNode*>(this);
    m_prev = reinterpret_cast<Mem::ListNode*>(this);
    vtable = reinterpret_cast<void**>(kProjectionVtable);
    m_name = 0;
    mp_texture = nullptr;
    m_blendMode = 0;
    m_fineness = 0;
    m_projection.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_projection.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_projection.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_projection.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_size = 0.0f;
    m_fov = 0.0f;
    m_aspect = 0.0f;
    m_direction = {0.0f, 0.0f, 0.0f, 1.0f};
    m_position = {0.0f, 0.0f, 0.0f, 1.0f};
    m_range = 0.0f;
    m_unknownA4 = 0;
    m_offset = {0.0f, 0.0f, 0.0f, 1.0f};
    m_angle = 0.0f;
    m_unknownBC = 0.0f;
    m_viewDistance = 0.0f;
    m_flags = 0;
    return this;
}

// 0x004A04A0
void CTextureProjection::DestroyBody() { reinterpret_cast<Mem::ListNode*>(this)->UnlinkAs(kProjectionNodeVtable); }

// 0x0049FE10
void CTextureProjection::PlatProject(void**, void**)
{
    Dbg::Printf(reinterpret_cast<const char*>(0x00649C00)); // "STUB: plat_update..."
}

// 0x004A04C0
void CTextureProjection::InitFromStructure(Script::CStruct* params)
{
    auto vector = [](Mth::Vector* v) { return reinterpret_cast<Script::VectorValue*>(v); };
    m_size = 0.0f;
    m_fov = 60.0f;
    m_aspect = 1.0f;
    m_direction = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0xB1DE1E91, vector(&m_direction), 1);
    m_direction.Normalize();
    m_worldDirection = m_direction;
    m_range = 0.0f;
    params->GetFloat(0xDF61A4BD, &m_range, 1);
    if (params->GetFloat(0x083FDB95, &m_size, 0)) {
        m_flags = (m_flags & ~kProjectionPerspective) | kProjectionOrthographic;
    } else if (params->GetFloat(0x17E7321E, &m_fov, 0)) {
        params->GetFloat(0x0F7C3CAA, &m_aspect, 0);
        m_flags = (m_flags & ~kProjectionOrthographic) | kProjectionPerspective;
    }
    m_unknownA4 = 0;
    params->GetChecksum(0xDC46BA66, &m_unknownA4, 0);
    m_offset = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0xA6F5352F, vector(&m_offset), 0);
    m_angle = 0.0f;
    params->GetFloat(0xFF7EBAF6, &m_angle, 0);
    m_angle = F(static_cast<double>(m_angle) * 0.0174532924f); // degrees to radians (0x00645464)
    uint32_t texture = 0;
    params->GetChecksum(0x7D99F28D, &texture, 1);
    m_blendMode = 0x54628ED7; // "blend"
    params->GetChecksum(0xDD6BB3D5, &m_blendMode, 0);
    int fineness = 0;
    params->GetInteger(0x12602598, &fineness, 0);
    m_fineness = static_cast<uint32_t>(fineness);
    mp_texture = reinterpret_cast<uint8_t*>(GameVariable<CTexDict*>(0x00700F20)->GetTexture(texture));
    int flag = 0;
    params->GetInteger(0xAF06447B, &flag, 0);
    if (flag)
        m_flags |= kProjectionFlag1;
    else
        m_flags &= ~kProjectionFlag1;
    m_unknownBC = 100000.0f;
    params->GetFloat(0xE979B3D3, &m_unknownBC, 0);
    m_viewDistance = 15000.0f;
    params->GetFloat(0x7FCAEAB1, &m_viewDistance, 0);
    UpdateMatrix();
}

// 0x0049FEF0
void CTextureProjection::UpdateMatrix()
{
    Mth::Vector target = {F(static_cast<double>(m_worldDirection.x) + m_position.x),
                          F(static_cast<double>(m_worldDirection.y) + m_position.y),
                          F(static_cast<double>(m_worldDirection.z) + m_position.z),
                          F(static_cast<double>(m_worldDirection.w) + m_position.w)};
    Mth::Vector up = {0.0f, 1.0f, 0.0f, 0.0f};
    Mth::Matrix view;
    Mth::CreateLookAtMatrix(view, m_position, target, up);
    Mth::Matrix lens;
    if (m_flags & kProjectionOrthographic)
        Mth::CreateOrthographicMatrix(lens, m_size, m_size, 0.1f, m_range);
    else if (m_flags & kProjectionPerspective)
        Mth::CreatePerspectiveMatrix(lens, m_fov, m_aspect, 0.1f, m_range);
    else
        return;
    // (D3DXMatrixMultiply)
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(&m_projection,
                                                                                                     &view, &lens);
}

// 0x0049FFE0
void CTextureProjection::Update(Obj::CCompositeObject* object)
{
    Mth::Matrix m;
    m = object->m_displayMatrix;
    const Mth::Vector& pos = object->m_pos;
    double ox = m_offset.x, oy = m_offset.y, oz = m_offset.z;
    // the position: the object's plus the offset through its matrix (x kept at 53 bits but for the third row's
    // term; y, z, w rounded at each step; w's third term unrounded)
    double x = static_cast<double>(m.row[0].x) * ox + pos.x;
    x += static_cast<double>(m.row[1].x) * oy;
    x += F(static_cast<double>(m.row[2].x) * oz);
    auto rounded = [&](int c) {
        float a = F(F(F((&m.row[0].x)[c] * ox) + static_cast<double>((&pos.x)[c])) +
                    static_cast<double>(F((&m.row[1].x)[c] * oy)));
        return a;
    };
    float y = F(static_cast<double>(rounded(1)) + F(static_cast<double>(m.row[2].y) * oz));
    float z = F(static_cast<double>(rounded(2)) + F(static_cast<double>(m.row[2].z) * oz));
    float w = F(static_cast<double>(rounded(3)) + static_cast<double>(m.row[2].w) * oz);
    m_position = {F(x), y, z, w};
    m_worldDirection = m_direction * m;

    if (GameVariable<int>(0x00701378) == 1 && FirstCamera()) {
        const Mth::Vector* camera = CameraPosition(FirstCamera());
        double dx = static_cast<double>(m_position.x) - camera->x;
        double dy = static_cast<double>(m_position.y) - camera->y;
        double dz = static_cast<double>(m_position.z) - camera->z;
        double reach = m_viewDistance;
        if (!(reach * reach > dx * dx + dz * dz + dy * dy))
            return; // (too far, or NaN)
        // a sphere half the range ahead - along the object-space direction, as the original -, as big as that plus
        // the size
        double half = static_cast<double>(m_range) * 0.5f;
        Mth::Vector centre = {F(static_cast<double>(F(half * m_direction.x)) + m_position.x),
                              F(static_cast<double>(F(half * m_direction.y)) + m_position.y),
                              F(static_cast<double>(F(half * m_direction.z)) + m_position.z),
                              F(half * m_direction.w + m_position.w)};
        if (SphereOutOfView(&centre, F(half + m_size)))
            return;
    }
    UpdateMatrix();
    // the line along the range, its box grown by 2.5 sizes
    double range = m_range;
    Mth::Vector end = {F(range * m_worldDirection.x + m_position.x),
                       F(static_cast<double>(F(range * m_worldDirection.y)) + m_position.y),
                       F(static_cast<double>(F(range * m_worldDirection.z)) + m_position.z),
                       F(static_cast<double>(F(range * m_worldDirection.w)) + m_position.w)};
    Mth::Line line(m_position, end);
    Mth::BBox box(line.start);
    box.AddPoint(line.end);
    double grow = static_cast<double>(m_size) * 2.5f; // (0x00649C1C)
    Mth::BBox reach;
    reach.min = {F(box.min.x - grow), F(box.min.y - grow), F(box.min.z - grow), 1.0f};
    grow = static_cast<double>(m_size) * 2.5f;
    reach.max = {F(box.max.x + grow), F(box.max.y + grow), F(box.max.z + grow), 1.0f};
    CCollGrid** grids = GridsAlongLine(&line);
    int count = 0;
    for (CCollGrid** grid = grids; *grid; ++grid) {
        CCollObj** objects = (*grid)->ObjectsInBox(&reach);
        if (!objects)
            return; // (nothing projected - even what earlier grids found)
        for (CCollObj** found = objects; *found; ++found) {
            const auto* bounds = (*found)->Call<const Mth::BBox*>(21);
            // (touching: no comparison says apart - NaNs count as touching)
            if (bounds->min.x > reach.max.x || reach.min.x > bounds->max.x ||
                bounds->min.z > reach.max.z || reach.min.z > bounds->max.z ||
                bounds->min.y > reach.max.y || reach.min.y > bounds->max.y)
                continue;
            void* sector = FindSector((*found)->m_checksum);
            ProjectedCollision()[count] = *found;
            ProjectedSectors()[count] = sector;
            ++count;
        }
    }
    if (count > 0) {
        ProjectedSectors()[count] = nullptr;
        ProjectedCollision()[count] = nullptr;
        reinterpret_cast<void(__thiscall*)(CTextureProjection*, void**, void**)>(vtable[1])(this, ProjectedSectors(),
                                                                                           ProjectedCollision());
    }
}

} // namespace Nx

REPLACE(0x0049FE20, &Nx::CTextureProjection::Construct);
REPLACE(0x004A04A0, &Nx::CTextureProjection::DestroyBody);
REPLACE(0x0049FE10, &Nx::CTextureProjection::PlatProject);
REPLACE(0x004A04C0, &Nx::CTextureProjection::InitFromStructure);
REPLACE(0x0049FEF0, &Nx::CTextureProjection::UpdateMatrix);
REPLACE(0x0049FFE0, &Nx::CTextureProjection::Update);
