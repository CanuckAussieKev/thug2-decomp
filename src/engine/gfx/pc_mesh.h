#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/engine_mesh.h"

// The PC mesh (Nx engine): a model's geometry - its scene and hierarchy - and its skin data, the strip breaks a
// skinned model's triangles need ("<name>.cas.xbx" for "<name>.skin.xbx", or the same data in memory): a version, a
// bone count from version 2 on, then 12-byte records sorted by their second word (SceneRef::break_strips' records).
namespace render {

struct SceneRef;
struct PcScene;

// A strip break (16 bytes: the file's 12 and a word cleared): a mask, mesh << 16 | an index, two more indices.
struct StripBreak {
    uint32_t mask;
    uint32_t mesh_and_index_;
    uint32_t indices;
    uint32_t strip_index_;
};
static_assert(sizeof(StripBreak) == 0x10, "layout must match the original");

// 0x004B7AF0 (Ghidra never made it: qsort's comparison): by the records' second word.
int compare_strip_breaks(const void* a, const void* b);

// The PC mesh (0xC8 bytes, vtable 0x0064A490; the engine's SubMesh - its hierarchy the scene's, SHierarchyNode).
class PcMesh final : public SubMesh {
public:
    PcMesh(const PcMesh&) = delete;
    PcMesh& operator=(const PcMesh&) = delete;
    // 0x004B7B30
    PcMesh() = default;
    // 0x004B7D50: for a model file: a "skin" one has its skin file (".cas.xbx" in place of ".skin.xbx", lower case)
    // loaded.
    explicit PcMesh(const char* name);
    // 0x004B7E30 (0x004B7F00 the scalar deleting destructor): the breaks freed, the scene unloaded (with its
    // collision), then the engine's.
    ~PcMesh() override;

    StripBreak* breaks{};    // +0xBC
    uint32_t break_count_{}; // +0xC0
    PcScene* scene{};        // +0xC4: the scene loaded with it (a model file's)

    // 0x004B7B50: the skin file read (opened "rb"). Whether it had breaks.
    bool load_skin_file(const char* name);
    // 0x004B7C60: the same from memory (`size` bytes; scene_format.h's skin).
    bool load_skin_from_memory(const uint8_t* data, size_t size);
    // 0x004B7EB0: load_skin_from_memory when there is data.
    void load_skin(const uint8_t* data, size_t size);
    // 0x004B7ED0: its scene, and the scene's hierarchy.
    void choose_scene(PcScene* scene);
};
static_assert(offsetof(PcMesh, scene) == 0xC4 && sizeof(PcMesh) == 0xC8,
              "layout must match the original");

} // namespace render
