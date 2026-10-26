#pragma once

#include <cstddef>
#include <cstdint>

// The PC engine's scene and mesh loaders (Nx engine's platform calls, cdecl): a scene's geometry from a file
// or from memory - its materials (with the texture dictionary's textures), its sectors (each with a PC geom), the
// mesh list's bounds and its hierarchy - and the meshes (a model's scene and its texture dictionary) made from them.
namespace Nx {

// A model's hierarchy node (0x50 bytes; the scene's array at +0x74, its count at +0x78; read as they are from the
// file): a checksum, its parent's, the parent's index (0xFFFF: none), a bone index and a matrix.
struct sHierarchyNode {
    uint32_t checksum;
    uint32_t parentChecksum;
    uint16_t parentIndex;
    uint8_t boneIndex;
    uint8_t pad0B;
    uint32_t unknown0C;
    float matrix[16];

    // 0x004B2D30 (Ghidra never made it: the array constructor's callback): the checksums 0, no parent, bone 0, the
    // identity matrix (+0x0C left). Returns this.
    sHierarchyNode* Construct();
};
static_assert(offsetof(sHierarchyNode, boneIndex) == 0xA && offsetof(sHierarchyNode, matrix) == 0x10 &&
                  sizeof(sHierarchyNode) == 0x50,
              "layout must match the original");

// 0x004B3490: a scene from geometry in memory: after a 12-byte header, the materials (with `textures`' (+0x0C)
// textures), the sector count, each sector (a PC geom of its own; kept when it loads), the hierarchy (count and
// nodes). The scene's hash table has 2^n entries for twice the sectors (n 1-12). `addSuperSectors` / `flag8D` go
// to the scene's +0x8C / +0x8D, `meshFlag` to its mesh list's +0x40.
void* __cdecl LoadSceneFromMemory(const uint8_t* data, void* textures, bool addSuperSectors, bool flag8D, bool meshFlag);
// 0x004B36D0: the same from a file (opened "rb"; null if there's none) - the header's three words read and not
// used; `sectorFlags` passed to each sector's load.
void* __cdecl LoadSceneFromFile(const char* name, void* textures, bool addSuperSectors, bool flag8D, bool meshFlag,
                                int sectorFlags);
// 0x004B3990: a new animation frame source (0x214 bytes from the frame sources' pools 0x006FA7F4; 0x0048CF70).
void* __cdecl CreateFrameSource();
// 0x004B3A20: a mesh (0xC8 bytes) from a scene file: the scene named by the file's name checksum (+0x84) with its
// texture dictionary (+0x88), its sectors set up (0x0049C910) and the 2D clip turned off.
void* __cdecl LoadMeshFile(const char* name, void* textures);
// 0x004B3AD0: a mesh from geometry in memory, the scene named by `checksum` as a decimal number and a line feed; the skin data, when
// there is any, loaded into the mesh first (0x004B7EB0). `unused` isn't.
void* __cdecl LoadMeshFromMemory(uint32_t checksum, const uint8_t* data, int unused, void* skin, void* textures);

} // namespace Nx
