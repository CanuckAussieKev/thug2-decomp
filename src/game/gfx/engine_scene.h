#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"

namespace Lst {
class HashTable;
}

// The engine's scene (Nx; the platform-independent base of the PC scene in pc_scene.h, its first 0x280 bytes): its
// sectors in a hash table, their collision (the static collision objects and a grid over them), its names. The
// members here are those of 0x0049C910-0x0049D18E; the constructor (0x0049D390), destructor (0x0049DD10) and the rest
// follow it in the engine.
namespace Nx {

class CCollGrid;
class CCollStatic;

// (vtable 0x006499E0: 0 the destructor, 1 PostLoad, 2 PostAdd, 3 LoadTextures, 4 LoadCollision, 5 AddCollision,
// 6 UnloadAddScene, 7 CreateSector (the platform's), 8 -, 9 GetDefaultColour, 10 SetRendered.)
struct CScene {
    void** m_vtable;
    char m_name[0x80];            // +0x04: the file it was loaded from
    uint8_t unknown84[8];         // +0x84: its name's checksum, its texture dictionary
    bool m_hasCollision;          // +0x8C
    uint8_t unknown8D[0x23];
    Mth::BBox m_collisionBox;     // +0xB0
    Lst::HashTable* mp_sectors;   // +0xD0
    CCollGrid* mp_collGrid;       // +0xD4
    char m_pipName[0x80];         // +0xD8: the collision file's (unloaded with the collision)
    int m_collStaticCount;        // +0x158
    CCollStatic* mp_collStatics;  // +0x15C (an array new)
    uint8_t unknown160[4];
    char m_addName[0x80];         // +0x164: the scene added to it
    uint32_t m_addData;           // +0x1E4
    uint8_t unknown1E8[0x8C];
    bool m_sectorKept;            // +0x274: a sector couldn't be deleted (flag 0x40): marked instead
    bool m_added;                 // +0x275
    uint8_t unknown276[0xA];

    // 0x0049C910: the name kept, then PostLoad (vtable slot 1) with `arena`.
    void FinishLoad(const char* name, void* arena);
    // 0x0049C940: an added scene's name and data kept, marked added, then PostAdd (vtable slot 2).
    void FinishAdd(const char* name, uint32_t data);
    // 0x0049C9A0: the static collision objects listed (each "%4d/%d " - the object's own print compiled out).
    void PrintCollStatics();
    // 0x0049C9F0: static collision object `index`.
    CCollStatic* GetCollStatic(int index);
    // 0x0049CA10: a new sector (vtable slot 7) with +0x14 / +0x18 cleared (no null check, as the original).
    void* CreateSector();
    // 0x0049CB00: the sector named `name` (the hash table's lookup, inlined), or null.
    void* GetSector(uint32_t name);
    // 0x0049CB40: `sector` deleted (out of the table if it's the one there under its name) - true; a sector with
    // flag 0x40 is only given flag 8 and the scene marked (+0x274) - false.
    bool DeleteSector(void* sector);
    // 0x0049CF80: the sector named `name` deleted (false if there's none).
    bool DeleteSectorNamed(uint32_t name);
    // 0x0049CBB0: the collision from a scene file's collision data in memory: `*count` objects from the header,
    // their geometry (`*objects`, 0x40 bytes each) relocated to the data after it, a static collision object each
    // (`*statics`, an array new) given its geometry and face-rejection buffer; `box` grown by every geometry with
    // vertices - (-100, -100, -100) - (100, 100, 100) when none has. True.
    bool LoadCollisionData(const uint8_t* data, int* count, CCollStatic** statics, uint8_t** objects, Mth::BBox* box,
                           int unused);
    // 0x0049CE60: with collision, `box` kept and the collision grid made over it. Whether there's collision.
    bool InitCollisionGrid(const Mth::BBox* box);
    // 0x0049CFB0: the grid, the static collision objects and the collision file let go of. True.
    bool UnloadCollision();
    // 0x0049D030: with a grid: every sector with flag 8 deleted (flag 0x40 cleared first) as far as the first gap in
    // the table, the grid cleared, the table emptied.
    void DeleteMarkedSectors();
    // 0x0049D0B0: every sector whose box touches `box` (two points) given 0x0049E9B0(`on`) - stopping at the first
    // gap in the table.
    void SetSectorsInBox(const Mth::Vector* box, uint32_t on, int unused);

    // The virtuals' base bodies (STUB messages; false / null).
    void PostLoad(void* arena);          // 0x0049CA50
    void SetRendered(int rendered);      // 0x0049CA60
    void PostAdd();                      // 0x0049CA70
    bool LoadTextures(const char* name); // 0x0049CA80
    bool LoadCollision(const char* name);  // 0x0049CAA0
    bool AddCollision(const char* name); // 0x0049CAC0
    bool UnloadAddScene();               // 0x0049CAE0
    void* PlatCreateSector();            // 0x0049CAF0 (not a Ghidra function)
    // 0x0049CF60 (slot 9): grey (0x80 each of RGBA) into `out`. Returns out.
    uint8_t* GetDefaultColour(uint8_t* out);
};
static_assert(offsetof(CScene, m_hasCollision) == 0x8C && offsetof(CScene, m_collisionBox) == 0xB0 &&
                  offsetof(CScene, mp_sectors) == 0xD0 && offsetof(CScene, mp_collGrid) == 0xD4 &&
                  offsetof(CScene, m_pipName) == 0xD8 && offsetof(CScene, m_collStaticCount) == 0x158 &&
                  offsetof(CScene, mp_collStatics) == 0x15C && offsetof(CScene, m_addName) == 0x164 &&
                  offsetof(CScene, m_addData) == 0x1E4 && offsetof(CScene, m_sectorKept) == 0x274 &&
                  offsetof(CScene, m_added) == 0x275 && sizeof(CScene) == 0x280,
              "layout must match the original");

// 0x0049CA20 (cdecl): a level's collision file name - "levels\<level>\<level>.col.<the platform's extension>" -
// into `out`. The length.
int __cdecl MakeCollisionFileName(char* out, const char* level);

} // namespace Nx
