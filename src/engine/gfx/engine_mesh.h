#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"

// The engine's mesh (render::SubMesh - the platform-independent part of the PC mesh, PcMesh in pc_mesh.h, its first 0xBC
// bytes; vtable 0x006492E4): a skinned model's collision (from a .col file kept loaded by name) and its hierarchy.
namespace render {

constexpr std::size_t kCollisionNameSize = 0x80;

struct TriMeshSource;

struct HierarchyNode;
class TextureSet;

constexpr std::size_t kSubMeshBoneCountOffset = 0xB0;
constexpr std::size_t kSubMeshBoxOffset = 0x90;
constexpr std::size_t kSubMeshHierarchyCountOffset = 0xB8;
constexpr std::size_t kSubMeshBytes = 0xBC;

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
    bool add_collision(uint8_t* data, size_t size);
    // 0x00490AB0 / 0x00490AC0 (folded with other classes' getters): the hierarchy / its count.
    HierarchyNode* fetch_bone_tree();
    uint32_t get_hierarchy_count();

    // The fields other code reads and sets.
    [[nodiscard]] TextureSet* textures() const { return textures_; }
    void set_textures(TextureSet* value) { textures_ = value; }
    [[nodiscard]] int collision_count() const { return collision_count_; }
    [[nodiscard]] TriMeshSource* hit_shape() const { return hit_shape_; }
    [[nodiscard]] uint32_t bone_count() const { return bone_count_; }
    void set_bone_count(uint32_t value) { bone_count_ = value; }
    void set_hierarchy(HierarchyNode* value) { hierarchy_ = value; }
    void set_hierarchy_count(uint32_t value) { hierarchy_count_ = value; }

private:
    TextureSet* textures_{};      // +0x04: its texture dictionary (the PC loaders set it)
    std::array<char, kCollisionNameSize> collision_name_{}; // +0x08: the .col file kept loaded (Pip)
    int collision_count_{};       // +0x88
    TriMeshSource* hit_shape_{};  // +0x8C: the objects (the collision file's records, in place)
    maths::BBox box_;           // +0x90
    uint32_t bone_count_{0};    // +0xB0 (the PC skin file's, from version 2)
    HierarchyNode* hierarchy_{nullptr}; // +0xB4: its scene's
    uint32_t hierarchy_count_{0}; // +0xB8
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(SubMesh, bone_count_) == kSubMeshBoneCountOffset, "layout must match the original");
        static_assert(offsetof(SubMesh, box_) == kSubMeshBoxOffset && offsetof(SubMesh, hierarchy_count_) == kSubMeshHierarchyCountOffset && sizeof(SubMesh) == kSubMeshBytes,
                      "layout must match the original");
    }
};

// 0x00490AD0: a skin's collision file name: `name` with ".col." in place of its extension (or added) and the
// platform's extension after it.
void skin_collision_file_name(char* out, const char* name);

} // namespace render
