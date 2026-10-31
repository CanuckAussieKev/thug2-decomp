// The engine's mesh, a skin's collision file name, the splat sets' slots and the vertex stack: see engine_mesh.h and
// engine_effects.h.

#include "game/gfx/engine_mesh.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/file/packs.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_glue.h"
#include "game/maths/vec4.h"
#include "game/engine3d/coll_tri.h"
#include "game/core/code_address.h"

namespace Engine3D {

namespace {

extern void** const kMeshVtable;
constexpr uint32_t kObjectSize = 0x40;

uint32_t Justify(uint32_t value, uint32_t to) { return (value + to - 1) & ~(to - 1); }

} // namespace

// 0x00490A60
CSubMesh* CSubMesh::MakeNew()
{
    m_vtable = reinterpret_cast<void**>(kMeshVtable);
    new (&m_box) Maths::BBox();
    m_unknownB0 = 0;
    mp_hierarchy = nullptr;
    m_hierarchyCount = 0;
    m_collisionName[0] = 0;
    return this;
}

// 0x00490A90
void CSubMesh::RemoveCollision()
{
    if (m_collisionName[0]) {
        Packs::Offload(m_collisionName);
        m_collisionName[0] = 0;
    }
}

// 0x00490B60
void CSubMesh::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kMeshVtable);
    RemoveCollision();
}

// 0x00490C90
CSubMesh* CSubMesh::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00490AB0
void* CSubMesh::FetchBoneTree()
{
    return mp_hierarchy;
}

// 0x00490AC0
uint32_t CSubMesh::GetHierarchyCount()
{
    return m_hierarchyCount;
}

// 0x00490B80
bool CSubMesh::AddCollision(const uint8_t* data)
{
    auto word = [data](int offset) { return *reinterpret_cast<const uint32_t*>(data + offset); };
    m_collisionCount = static_cast<int>(word(4));
    m_hitShape = const_cast<uint8_t*>(data) + 0x20;
    uint32_t objects = reinterpret_cast<uint32_t>(m_hitShape);
    uint32_t vertices = Justify((static_cast<uint32_t>(m_collisionCount) << 6) + objects, 16);
    uint32_t vertexBytes = vertices + 2 * (3 * (word(0x18) + 2 * word(0x14)));
    uint32_t faces = Justify(word(8) + vertexBytes, 4);
    uint32_t afterFaces = Justify(word(0xC) * 11 + word(0x10) * 8, 4) + faces;
    uint32_t nodes = afterFaces + 4;
    uint32_t faceLists = *reinterpret_cast<const uint32_t*>(afterFaces) + nodes;
    for (int i = 0; i < static_cast<int>(word(4)); ++i) {
        auto* object = reinterpret_cast<CTriMeshSource*>(m_hitShape + i * kObjectSize);
        object->Relocate(0, vertices, vertexBytes, faces, nodes, faceLists);
        object->SetUpAllFaces();
        m_box.AppendSpot(*reinterpret_cast<const Maths::Vec4*>(m_hitShape + i * kObjectSize + 0x10));
        m_box.AppendSpot(*reinterpret_cast<const Maths::Vec4*>(m_hitShape + i * kObjectSize + 0x20));
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
void sVertexStack::MakeNew()
{
    used = 0;
    vertexSize = 0;
    std::memset(data, 3, sizeof(data));
}

// 0x00490D50
void sVertexStack::StackOn(const uint8_t* in)
{
    std::memcpy(data + used, in, vertexSize);
    used += vertexSize;
}

namespace {
static VtableSlot kMeshVtable_slots[] = {&Engine3D::CSubMesh::Teardown};
void** const kMeshVtable = reinterpret_cast<void**>(kMeshVtable_slots);
} // namespace

} // namespace Nx
