#pragma once

#include <cstddef>
#include <cstdint>

// The PC mesh (Nx engine): a model's geometry - its scene and hierarchy - and its skin data, the strip breaks a
// skinned model's triangles need ("<name>.cas.xbx" for "<name>.skin.xbx", or the same data in memory): a version, a
// bone count from version 2 on, then 12-byte records sorted by their second word (sScene::BreakStrips' records).
namespace Nx {

struct sScene;

// A strip break (16 bytes: the file's 12 and a word cleared): a mask, mesh << 16 | an index, two more indices.
struct sStripBreak {
    uint32_t mask;
    uint32_t meshAndIndex;
    uint32_t indices;
    uint32_t stripIndex;
};
static_assert(sizeof(sStripBreak) == 0x10, "layout must match the original");

// 0x004B7AF0 (Ghidra never made it: qsort's comparison): by the records' second word.
int __cdecl CompareStripBreaks(const void* a, const void* b);

// The PC mesh (0xC8 bytes, vtable 0x0064A490; the engine's CMesh - constructor 0x00490A60, vtable 0x006492E4,
// destructor body 0x00490B60 - for its first 0xBC bytes: +0xB4 / +0xB8 the hierarchy and its count).
struct CPcMesh {
    void** vtable;
    uint8_t base[0xAC];
    uint32_t boneCount;      // +0xB0 (the skin file's, from version 2)
    void* hierarchy;         // +0xB4: the scene's (sHierarchyNode)
    uint32_t hierarchyCount; // +0xB8
    sStripBreak* breaks;     // +0xBC
    uint32_t breakCount;     // +0xC0
    uint8_t* scene;          // +0xC4

    // 0x004B7B30: construct. Returns this.
    CPcMesh* Construct();
    // 0x004B7D50: construct for a model file: a "skin" one has its skin file (".cas.xbx" in place of ".skin.xbx",
    // lower case) loaded. Returns this.
    CPcMesh* ConstructFromFile(const char* name);
    // 0x004B7E30: the destructor's body - the breaks freed, the scene unloaded (with its collision).
    void DestroyBody();
    // 0x004B7F00 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcMesh* Destroy(unsigned int flags);
    // 0x004B7B50: the skin file read (opened "rb"). Whether it had breaks.
    bool LoadSkinFile(const char* name);
    // 0x004B7C60: the same from memory (*data moved past it).
    bool LoadSkinFromMemory(const uint8_t** data);
    // 0x004B7EB0: LoadSkinFromMemory when there is data.
    void LoadSkin(const uint8_t* data);
    // 0x004B7ED0: its scene, and the scene's hierarchy.
    void SetScene(uint8_t* scene);
};
static_assert(offsetof(CPcMesh, boneCount) == 0xB0 && offsetof(CPcMesh, scene) == 0xC4 && sizeof(CPcMesh) == 0xC8,
              "layout must match the original");

} // namespace Nx
