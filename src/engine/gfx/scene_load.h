#pragma once

#include <cstddef>
#include <cstdint>
#include "engine/memory/slab_bank.h"
#include <gsl/pointers>
#include <array>
#include "base/maths/mat4.h"

// The PC engine's scene and mesh loaders (Nx engine's platform calls, cdecl): a scene's geometry from a file
// or from memory - its materials (with the texture dictionary's textures), its sectors (each with a PC geom), the
// mesh list's bounds and its hierarchy - and the meshes (a model's scene and its texture dictionary) made from them.
namespace render {

constexpr int kNoParent = 0xFFFF; // a sector without a parent in the hierarchy

class PcMesh;
class TextureSet;
struct PcScene;
struct VertexArena;

constexpr std::size_t kHierarchyNodeBoneSlotOffset = 0xA;
constexpr std::size_t kHierarchyNodeMatrixOffset = 0x10;
constexpr std::size_t kHierarchyNodeBytes = 0x50;

// A model's hierarchy node (0x50 bytes; the scene's array at +0x74, its count at +0x78; read as they are from the
// file): a checksum, its parent's, the parent's index (0xFFFF: none), a bone index and a matrix.
struct HierarchyNode {
    // The fields other code reads and sets.
    [[nodiscard]] uint32_t checksum() const { return checksum_; }
    [[nodiscard]] uint16_t parent_index() const { return parent_index_; }
    [[nodiscard]] uint8_t bone_slot() const { return bone_slot_; }
    maths::Mat4& matrix() { return matrix_; }
    [[nodiscard]] const maths::Mat4& matrix() const { return matrix_; }

private:
    uint32_t checksum_{0};
    uint32_t parent_checksum_{0};
    uint16_t parent_index_{kNoParent};
    uint8_t bone_slot_{0};
    uint8_t pad0b_{};
    uint32_t unknown0c_{};
    maths::Mat4 matrix_{}; // (relative to its parent)

public:

    // 0x004B2D30 (Ghidra never made it: the array constructor's callback): the checksums 0, no parent, bone 0, the
    // identity matrix (+0x0C left).
    HierarchyNode();
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(HierarchyNode, bone_slot_) == kHierarchyNodeBoneSlotOffset && offsetof(HierarchyNode, matrix_) == kHierarchyNodeMatrixOffset &&
                          sizeof(HierarchyNode) == kHierarchyNodeBytes,
                      "layout must match the original");
    }
};

// 0x004B3490: a scene from geometry in memory: after a 12-byte header, the materials (with `textures`' (+0x0C)
// textures), the sector count, each sector (a PC geom of its own; kept when it loads), the hierarchy (count and
// nodes). The scene's hash table has 2^n entries for twice the sectors (n 1-12). `with_super_sectors` / `flag_8d` go
// to the scene's +0x8C / +0x8D, `mesh_flag` to its mesh list's +0x40.
// The scene's `size` bytes are read through scene_format::MemoryStream (scene_format.h): a damaged record ends it.
gsl::owner<PcScene*> load_scene_from_memory(const uint8_t* data, size_t size, const TextureSet* textures, bool with_super_sectors,
                                bool flag_8d, bool mesh_flag);
// 0x004B36D0: the same from a file (opened "rb"; null if there's none) - the header's three words read and not
// used; the sectors' vertices packed into `arena` when there is one (not for billboards).
gsl::owner<PcScene*> load_scene_from_file(const char* name, const TextureSet* textures, bool with_super_sectors, bool flag_8d,
                              bool mesh_flag, VertexArena* arena);
// 0x004B3990: a new animation frame source (0x214 bytes from the frame sources' pools 0x006FA7F4; 0x0048CF70).
gsl::owner<void*> create_frame_source();
// 0x004B3A20: a mesh (0xC8 bytes) from a scene file: the scene named by the file's name checksum (+0x84) with its
// texture dictionary (+0x88), its sectors set up (0x0049C910) and the 2D clip turned off.
gsl::owner<PcMesh*> load_mesh_file(const char* name, TextureSet* textures);
// 0x004B3AD0: a mesh from geometry in memory, the scene named by `checksum` as a decimal number and a line feed; the skin data, when
// there is any, loaded into the mesh first (0x004B7EB0). `size` / `skin_size`: their bytes (the original's `size`
// went unused).
gsl::owner<PcMesh*> load_mesh_from_memory(uint32_t checksum, const uint8_t* data, size_t size, const void* skin, size_t skin_size,
                              TextureSet* textures);

memory::PoolPair& frame_source_pools();
} // namespace render
