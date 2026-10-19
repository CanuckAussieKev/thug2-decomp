#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"

namespace Lst {
class HashTable;
}

// A loaded scene's meshes and materials (Nx engine, PC: the platform part of a scene, the "mesh group" the
// render pass draws): a list of meshes sorted for drawing - opaque first, then the ones whose material is
// transparent (flag 0x40) - and the materials by checksum.
namespace Nx {

struct sScene {
    uint32_t flags;          // 0x100 once its meshes are prepared
    uint8_t unknown4[0x14];
    Lst::HashTable* materials; // +0x18, by checksum
    uint8_t** meshes;        // +0x1C
    int meshCount;           // +0x20
    int transparentCount;    // +0x24: meshes with a transparent material
    int filled;              // +0x28: where AddMeshes puts the next
    int firstTransparent;    // +0x2C (meshCount when none)
    int firstFlagged;        // +0x30: the first whose material has its byte +0xC set
    int flaggedCount;        // +0x34
    uint8_t unknown38[8];
    uint8_t unknown40;
    uint8_t pad41[3];
    Mth::BBox bounds;        // +0x44
    float centre[3];         // +0x64: the bounds' sphere
    float radius;            // +0x70
    uint8_t* extra;          // +0x74: an array of 0x50-byte records (new[]'d: the count before it)
    uint32_t unknown78;
    void* comObject;         // +0x7C: released with the scene
    uint32_t unknown80;

    // 0x004DE300: construct empty. Returns this.
    sScene* Construct();
    // 0x004DE490: the destructor's body: the material table, the meshes' vertex buffer 0, the list, the records,
    // the object.
    void DestroyBody();
    // 0x004DE340: the bounding sphere from the bounds.
    void UpdateSphere();
    // 0x004DE3E0: room for meshCount meshes.
    void AllocateMeshList();
    // 0x004DDDF0: count meshes to be added (and the transparent ones).
    void CountMeshes(int count, uint8_t* const* list);
    // 0x004DDE20: add meshes to the list.
    void AddMeshes(int count, uint8_t* const* list);
    // 0x004DDE60: take meshes out of the list (closing the gaps) and count again.
    void RemoveMeshes(int count, uint8_t* const* list);
    // 0x004DDFA0: sort the list (CompareMeshes) and count again.
    void SortMeshes();
    // 0x004DE020: the mesh with this load index (+0x20), or null.
    uint8_t* FindMesh(uint32_t index) const;
    // 0x004DE420: the material with this checksum (its first field), or null.
    void* FindMaterial(uint32_t checksum) const;
    // 0x004DE060: break the meshes' triangle strips where the records (mask, mesh << 16 | a, c << 16 | b, the strip
    // index to search from) say: the index before a triangle (a, c, b) found is flagged, and the strips of meshes
    // with flags are rebuilt with each flagged index three times.
    void BreakStrips(uint32_t mask, const uint32_t* records, uint32_t count);

    // The counts after a change (shared by RemoveMeshes and SortMeshes).
    void Recount();
};
static_assert(offsetof(sScene, meshes) == 0x1C && offsetof(sScene, bounds) == 0x44 && offsetof(sScene, radius) == 0x70 &&
                  offsetof(sScene, comObject) == 0x7C,
              "layout must match the original");

// 0x004DDD50 (qsort's comparison of two mesh pointers): by the material's sort key (+0x1C), then its byte +0xC,
// then its flag 0x10, then the mesh's +0x44, then the material's address.
int __cdecl CompareMeshes(const void* a, const void* b);
// 0x004DE560: free a scene's materials, destroy it and free it.
void __cdecl DeleteScene(sScene* scene);

} // namespace Nx
