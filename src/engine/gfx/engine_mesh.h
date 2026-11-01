#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"

// The engine's mesh (render::SubMesh - the platform-independent part of the PC mesh, PcMesh in pc_mesh.h, its first 0xBC
// bytes; vtable 0x006492E4): a skinned model's collision (from a .col file kept loaded by name) and its hierarchy.
namespace render {

class SubMesh : public memory::HeapObject {
public:
    // 0x00490A60: an empty box, no collision, no hierarchy.
    SubMesh();
    // 0x00490B60 (0x00490C90 the scalar deleting destructor, vtable slot 0 - its only one): the collision file
    // unloaded.
    virtual ~SubMesh();
    SubMesh(const SubMesh&) = delete;
    SubMesh& operator=(const SubMesh&) = delete;
    // 0x00490A90: the collision file unloaded (Pip), its name cleared.
    void detach_hit_mesh();
    // 0x00490B80: the collision from a .col file's data in memory: its objects (0x40 bytes each, after a 0x20-byte
    // header) relocated to the data after them (the vertices 16-aligned after the objects, then 6 bytes per vertex
    // word, the faces 4-aligned, the face-rejection buffers, the nodes and the face lists - as the scene's), their
    // faces set up, the box grown by each one's. True.
    // (not original: `size` - the file's bytes - bounds its reader: collision_format.h)
    bool add_collision(const uint8_t* data, size_t size);
    // 0x00490AB0 / 0x00490AC0 (folded with other classes' getters): the hierarchy / its count.
    void* fetch_bone_tree();
    uint32_t get_hierarchy_count();

    void* textures_;            // +0x04: its texture dictionary (the PC loaders set it)
    char collision_name_[0x80]; // +0x08: the .col file kept loaded (Pip)
    int collision_count_;       // +0x88
    uint8_t* hit_shape_;        // +0x8C: the objects (0x40 bytes each)
    maths::BBox box_;           // +0x90
    uint32_t bone_count_;       // +0xB0 (the PC skin file's, from version 2)
    void* hierarchy_;           // +0xB4
    uint32_t hierarchy_count_;  // +0xB8
};
static_assert(offsetof(SubMesh, box_) == 0x90 && offsetof(SubMesh, hierarchy_count_) == 0xB8 && sizeof(SubMesh) == 0xBC,
              "layout must match the original");

// 0x00490AD0: a skin's collision file name: `name` with ".col." in place of its extension (or added) and the
// platform's extension after it.
void __cdecl skin_collision_file_name(char* out, const char* name);

// The splat sets' slots (0x00490CD0, thiscall on a set): the first free one of the first `count` (the alive word 0),
// else the one with the most steps past its start (the first on ties).
struct SplatSlots {
    uint32_t alive[0x100];
    int steps[0x100];

    int oldest_slot(uint32_t count);
};

} // namespace render
