#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/shape_tests.h"

namespace lists {
class KeyMap;
}

// A loaded scene's meshes and materials (Nx engine, PC: the platform part of a scene, the "mesh group" the
// render pass draws): a list of meshes sorted for drawing - opaque first, then the ones whose material is
// transparent (flag 0x40) - and the materials by checksum.
namespace render {

struct MeshRef;

struct SceneRef {
    uint32_t flags{0}; // 0x100 once its meshes are prepared
    uint8_t unknown4[0x14]{};
    lists::KeyMap* materials{nullptr}; // +0x18, by checksum
    MeshRef** meshes{nullptr};         // +0x1C
    int mesh_count_{0};                // +0x20
    int transparent_count_{0};         // +0x24: meshes with a transparent material
    int filled{0};                     // +0x28: where append_meshes puts the next
    int first_transparent_{-1};        // +0x2C (meshCount when none)
    int first_flagged_{-1};            // +0x30: the first whose material has its byte +0xC set
    int flagged_count_{0};             // +0x34
    uint8_t unknown38[8]{};
    uint8_t unknown40{0};
    uint8_t pad41[3]{};
    maths::BBox bounds; // +0x44
    float centre[3]{};  // +0x64: the bounds' sphere
    float radius{};     // +0x70
    uint8_t* extra{nullptr}; // +0x74: an array of 0x50-byte records (new[]'d: the count before it)
    uint32_t unknown78{0};
    void* com_object_{nullptr}; // +0x7C: released with the scene
    uint32_t unknown80{0};

    // 0x004DE300: construct empty.
    SceneRef();
    // 0x004DE490: the destructor's body: the material table, the meshes' vertex buffer 0, the list, the records,
    // the object.
    void destroy_body();
    // 0x004DE340: the bounding sphere from the bounds.
    void update_sphere();
    // 0x004DE3E0: room for meshCount meshes.
    void allocate_mesh_list();
    // 0x004DDDF0: count meshes to be added (and the transparent ones).
    void mesh_total(int count, MeshRef* const* list);
    // 0x004DDE20: add meshes to the list.
    void append_meshes(int count, MeshRef* const* list);
    // 0x004DDE60: take meshes out of the list (closing the gaps) and count again.
    void detach_meshes(int count, MeshRef* const* list);
    // 0x004DDFA0: sort the list (compare_meshes) and count again.
    void order_meshes();
    // 0x004DE020: the mesh with this load index (+0x20), or null.
    MeshRef* find_mesh(uint32_t index) const;
    // 0x004DE420: the material with this checksum (its first field), or null.
    void* find_material(uint32_t checksum) const;
    // 0x004DE060: break the meshes' triangle strips where the records (mask, mesh << 16 | a, c << 16 | b, the strip
    // index to search from) say: the index before a triangle (a, c, b) found is flagged, and the strips of meshes
    // with flags are rebuilt with each flagged index three times.
    void break_strips(uint32_t mask, const uint32_t* records, uint32_t count);

    // The counts after a change (shared by detach_meshes and order_meshes).
    void recount();
};
static_assert(offsetof(SceneRef, meshes) == 0x1C && offsetof(SceneRef, bounds) == 0x44 &&
                  offsetof(SceneRef, radius) == 0x70 && offsetof(SceneRef, com_object_) == 0x7C,
              "layout must match the original");

// 0x004DDD50 (qsort's comparison of two mesh pointers): by the material's sort key (+0x1C), then its byte +0xC,
// then its flag 0x10, then the mesh's +0x44, then the material's address.
int compare_meshes(const void* a, const void* b);
// 0x004DE560: free a scene's materials, destroy it and free it.
void discard_scene(SceneRef* scene);

} // namespace render
