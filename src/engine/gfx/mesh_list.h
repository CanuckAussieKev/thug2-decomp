#pragma once

#include <span>
#include <cstddef>
#include <cstdint>
#include <array>

#include "base/maths/shape_tests.h"
#include "gpu/gpu_handles.h"


namespace lists {
class KeyMap;
}

// A loaded scene's meshes and materials (Nx engine, PC: the platform part of a scene, the "mesh group" the
// render pass draws): a list of meshes sorted for drawing - opaque first, then the ones whose material is
// transparent (flag 0x40) - and the materials by checksum.
namespace render {

constexpr std::size_t kMeshListUnknown4Bytes = 0x14;
constexpr std::size_t kMeshListUnknown38Bytes = 8;

struct MeshRef;
struct HierarchyNode;
struct StripBreak;

constexpr std::size_t kSceneRefMeshesOffset = 0x1C;
constexpr std::size_t kSceneRefBoundsOffset = 0x44;
constexpr std::size_t kSceneRefRadiusOffset = 0x70;
constexpr std::size_t kSceneRefArenaBufferOffset = 0x7C;

struct SceneRef {
    // The fields other code reads and sets.
    uint32_t& flags() { return flags_; }
    [[nodiscard]] uint32_t flags() const { return flags_; }
    [[nodiscard]] lists::KeyMap* materials() const { return materials_; }
    void set_materials(lists::KeyMap* value) { materials_ = value; }
    [[nodiscard]] MeshRef** meshes() const { return meshes_; }
    void set_meshes(MeshRef** value) { meshes_ = value; }
    [[nodiscard]] int mesh_count() const { return mesh_count_; }
    [[nodiscard]] int transparent_count() const { return transparent_count_; }
    [[nodiscard]] int first_transparent() const { return first_transparent_; }
    [[nodiscard]] int first_flagged() const { return first_flagged_; }
    [[nodiscard]] int flagged_count() const { return flagged_count_; }
    [[nodiscard]] uint8_t unknown40() const { return unknown40_; }
    void set_unknown40(uint8_t value) { unknown40_ = value; }
    maths::BBox& bounds() { return bounds_; }
    [[nodiscard]] const maths::BBox& bounds() const { return bounds_; }
    std::array<float, 3>& centre() { return centre_; }
    [[nodiscard]] const std::array<float, 3>& centre() const { return centre_; }
    float& radius() { return radius_; }
    [[nodiscard]] float radius() const { return radius_; }
    [[nodiscard]] HierarchyNode* hierarchy() const { return hierarchy_; }
    void set_hierarchy(HierarchyNode* value) { hierarchy_ = value; }
    [[nodiscard]] uint32_t unknown78() const { return unknown78_; }
    void set_unknown78(uint32_t value) { unknown78_ = value; }
    GpuVertexBuffer*& arena_buffer() { return arena_buffer_; }
    [[nodiscard]] GpuVertexBuffer* arena_buffer() const { return arena_buffer_; }
    void set_arena_buffer(GpuVertexBuffer* value) { arena_buffer_ = value; }
    [[nodiscard]] uint32_t unknown80() const { return unknown80_; }
    void set_unknown80(uint32_t value) { unknown80_ = value; }

private:
    uint32_t flags_{0}; // 0x100 once its meshes are prepared
    std::array<uint8_t, kMeshListUnknown4Bytes> unknown4{};
    lists::KeyMap* materials_{nullptr}; // +0x18, by checksum
    MeshRef** meshes_{nullptr};         // +0x1C
    int mesh_count_{0};                // +0x20
    int transparent_count_{0};         // +0x24: meshes with a transparent material
    int filled{0};                     // +0x28: where append_meshes puts the next
    int first_transparent_{-1};        // +0x2C (meshCount when none)
    int first_flagged_{-1};            // +0x30: the first whose material has its byte +0xC set
    int flagged_count_{0};             // +0x34
    std::array<uint8_t, kMeshListUnknown38Bytes> unknown38{};
    uint8_t unknown40_{0};
    std::array<uint8_t, 3> pad41{};
    maths::BBox bounds_; // +0x44
    std::array<float, 3> centre_{};  // +0x64: the bounds' sphere
    float radius_{};     // +0x70
    HierarchyNode* hierarchy_{nullptr}; // +0x74: the scene's hierarchy (new_hierarchy's)
    uint32_t unknown78_{0};
    GpuVertexBuffer* arena_buffer_{nullptr}; // +0x7C: the vertex arena's buffer (released with the scene)
    uint32_t unknown80_{0};

public:

    // NOT ORIGINAL - the meshes as a run of mesh_count_.
    [[nodiscard]] std::span<MeshRef*> mesh_run() const { return {meshes_, static_cast<size_t>(mesh_count_)}; }

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
    void mesh_total(std::span<MeshRef* const> list);
    // 0x004DDE20: add meshes to the list.
    void append_meshes(std::span<MeshRef* const> list);
    // 0x004DDE60: take meshes out of the list (closing the gaps) and count again.
    void detach_meshes(std::span<MeshRef* const> list);
    // 0x004DDFA0: sort the list (compare_meshes) and count again.
    void order_meshes();
    // 0x004DE020: the mesh with this load index (+0x20), or null.
    [[nodiscard]] MeshRef* find_mesh(uint32_t index) const;
    // 0x004DE420: the material with this checksum (its first field), or null.
    [[nodiscard]] void* find_material(uint32_t checksum) const;
    // 0x004DE060: break the meshes' triangle strips where the records (mask, mesh << 16 | a, c << 16 | b, the strip
    // index to search from) say: the index before a triangle (a, c, b) found is flagged, and the strips of meshes
    // with flags are rebuilt with each flagged index three times.
    void break_strips(uint32_t mask, const StripBreak* records, uint32_t count) const;

    // The counts after a change (shared by detach_meshes and order_meshes).
    void recount();
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(SceneRef, meshes_) == kSceneRefMeshesOffset && offsetof(SceneRef, bounds_) == kSceneRefBoundsOffset &&
                          offsetof(SceneRef, radius_) == kSceneRefRadiusOffset && offsetof(SceneRef, arena_buffer_) == kSceneRefArenaBufferOffset,
                      "layout must match the original");
    }
};

// 0x004DDD50 (qsort's comparison of two mesh pointers): by the material's sort key (+0x1C), then its byte +0xC,
// then its flag 0x10, then the mesh's +0x44, then the material's address.
int compare_meshes(const void* a, const void* b);
// 0x004DE560: free a scene's materials, destroy it and free it.
void discard_scene(SceneRef* scene);
// A scene's hierarchy: `count` nodes (the game heap's), constructed; null without memory. Freed by free_hierarchy.
HierarchyNode* new_hierarchy(int count);
void free_hierarchy(HierarchyNode* nodes);

} // namespace render
