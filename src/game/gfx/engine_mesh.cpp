// The engine's mesh, a skin's collision file name, the splat sets' slots and the vertex stack: see engine_mesh.h and
// engine_effects.h.

#include "game/gfx/engine_mesh.h"

#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/file/pip.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_glue.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tri.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kMeshVtable = 0x006492E4;
constexpr uint32_t kObjectSize = 0x40;

uint32_t Align(uint32_t value, uint32_t to) { return (value + to - 1) & ~(to - 1); }

} // namespace

// 0x00490A60
CMesh* CMesh::Construct()
{
    m_vtable = reinterpret_cast<void**>(kMeshVtable);
    new (&m_box) Mth::BBox();
    m_unknownB0 = 0;
    mp_hierarchy = nullptr;
    m_hierarchyCount = 0;
    m_collisionName[0] = 0;
    return this;
}

// 0x00490A90
void CMesh::RemoveCollision()
{
    if (m_collisionName[0]) {
        Pip::Unload(m_collisionName);
        m_collisionName[0] = 0;
    }
}

// 0x00490B60
void CMesh::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kMeshVtable);
    RemoveCollision();
}

// 0x00490C90
CMesh* CMesh::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00490AB0
void* CMesh::GetHierarchy()
{
    return mp_hierarchy;
}

// 0x00490AC0
uint32_t CMesh::GetHierarchyCount()
{
    return m_hierarchyCount;
}

// 0x00490B80
bool CMesh::AddCollision(const uint8_t* data)
{
    auto word = [data](int offset) { return *reinterpret_cast<const uint32_t*>(data + offset); };
    m_collisionCount = static_cast<int>(word(4));
    mp_collision = const_cast<uint8_t*>(data) + 0x20;
    uint32_t objects = reinterpret_cast<uint32_t>(mp_collision);
    uint32_t vertices = Align((static_cast<uint32_t>(m_collisionCount) << 6) + objects, 16);
    uint32_t vertexBytes = vertices + 2 * (3 * (word(0x18) + 2 * word(0x14)));
    uint32_t faces = Align(word(8) + vertexBytes, 4);
    uint32_t afterFaces = Align(word(0xC) * 11 + word(0x10) * 8, 4) + faces;
    uint32_t nodes = afterFaces + 4;
    uint32_t faceLists = *reinterpret_cast<const uint32_t*>(afterFaces) + nodes;
    for (int i = 0; i < static_cast<int>(word(4)); ++i) {
        auto* object = reinterpret_cast<CCollObjTriData*>(mp_collision + i * kObjectSize);
        object->Relocate(0, vertices, vertexBytes, faces, nodes, faceLists);
        object->SetUpAllFaces();
        m_box.AddPoint(*reinterpret_cast<const Mth::Vector*>(mp_collision + i * kObjectSize + 0x10));
        m_box.AddPoint(*reinterpret_cast<const Mth::Vector*>(mp_collision + i * kObjectSize + 0x20));
    }
    return true;
}

// 0x00490AD0
void __cdecl SkinCollisionFileName(char* out, const char* name)
{
    char* end = out;
    while ((*end++ = *name++) != '\0') { // (no length limit, as the original)
    }
    char* extension = std::strstr(out, ".");
    if (!extension)
        extension = out + std::strlen(out);
    std::memcpy(extension, ".col.", 6);
    const char* platform = PlatformExtension();
    end = out + std::strlen(out);
    while ((*end++ = *platform++) != '\0') {
    }
}

// 0x00490CD0
int sSplatSlots::OldestSlot(uint32_t count)
{
    int oldest = 0;
    int most = steps[0] - static_cast<int>(alive[0]);
    for (uint32_t i = 0; i < count; ++i) {
        if (!alive[i])
            return static_cast<int>(i);
        int age = steps[i] - static_cast<int>(alive[i]);
        if (age > most) {
            most = age;
            oldest = static_cast<int>(i);
        }
    }
    return oldest;
}

// 0x00490D20
void sVertexStack::Construct()
{
    used = 0;
    vertexSize = 0;
    std::memset(data, 3, sizeof(data));
}

// 0x00490D50
void sVertexStack::Push(const uint8_t* in)
{
    std::memcpy(data + used, in, vertexSize);
    used += vertexSize;
}

} // namespace Nx

REPLACE(0x00490A60, &Nx::CMesh::Construct);
REPLACE(0x00490A90, &Nx::CMesh::RemoveCollision);
REPLACE(0x00490B60, &Nx::CMesh::DestroyBody);
REPLACE(0x00490C90, &Nx::CMesh::Destroy);
REPLACE(0x00490B80, &Nx::CMesh::AddCollision);
REPLACE(0x00490AB0, &Nx::CMesh::GetHierarchy); // (folded with other classes' getters)
REPLACE(0x00490AC0, &Nx::CMesh::GetHierarchyCount);
REPLACE(0x00490AD0, Nx::SkinCollisionFileName);
REPLACE(0x00490CD0, &Nx::sSplatSlots::OldestSlot);
REPLACE(0x00490D20, &Nx::sVertexStack::Construct);
REPLACE(0x00490D50, &Nx::sVertexStack::Push);
