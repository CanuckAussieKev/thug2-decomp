#pragma once

#include <cstddef>
#include <cstdint>

#include "game/maths/shape_tests.h"

// The engine's mesh (Engine3D::CSubMesh - the platform-independent part of the PC mesh, CPcMesh in pc_mesh.h, its first 0xBC
// bytes; vtable 0x006492E4): a skinned model's collision (from a .col file kept loaded by name) and its hierarchy.
namespace Engine3D {

class CSubMesh {
public:
    // 0x00490A60: construct - an empty box, no collision, no hierarchy. Returns this.
    CSubMesh* MakeNew();
    // 0x00490B60: the destructor's body - the collision file unloaded.
    void DestroyBody();
    // 0x00490C90 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CSubMesh* Teardown(unsigned int flags);
    // 0x00490A90: the collision file unloaded (Pip), its name cleared.
    void RemoveCollision();
    // 0x00490B80: the collision from a .col file's data in memory: its objects (0x40 bytes each, after a 0x20-byte
    // header) relocated to the data after them (the vertices 16-aligned after the objects, then 6 bytes per vertex
    // word, the faces 4-aligned, the face-rejection buffers, the nodes and the face lists - as the scene's), their
    // faces set up, the box grown by each one's. True.
    bool AddCollision(const uint8_t* data);
    // 0x00490AB0 / 0x00490AC0 (folded with other classes' getters): the hierarchy / its count.
    void* FetchBoneTree();
    uint32_t GetHierarchyCount();

    void** m_vtable;
    uint32_t m_unknown4;
    char m_collisionName[0x80]; // +0x08: the .col file kept loaded (Pip)
    int m_collisionCount;       // +0x88
    uint8_t* m_hitShape;      // +0x8C: the objects (0x40 bytes each)
    Maths::BBox m_box;            // +0x90
    uint32_t m_unknownB0;
    void* mp_hierarchy;         // +0xB4
    uint32_t m_hierarchyCount;  // +0xB8
};
static_assert(offsetof(CSubMesh, m_box) == 0x90 && offsetof(CSubMesh, m_hierarchyCount) == 0xB8 && sizeof(CSubMesh) == 0xBC,
              "layout must match the original");

// 0x00490AD0: a skin's collision file name: `name` with ".col." in place of its extension (or added) and the
// platform's extension after it.
void __cdecl SkinCollisionFileName(char* out, const char* name);

// The splat sets' slots (0x00490CD0, thiscall on a set): the first free one of the first `count` (the alive word 0),
// else the one with the most steps past its start (the first on ties).
struct sSplatSlots {
    uint32_t alive[0x100];
    int steps[0x100];

    int OldestSlot(uint32_t count);
};

} // namespace Nx
