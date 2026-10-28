#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/geometry.h"

namespace Lst {
class HashTable;
}
namespace Script {
class CStruct;
}

// The engine's scene (Nx; the platform-independent base of the PC scene in pc_scene.h, its first 0x280 bytes): its
// sectors in a hash table, their collision (the static collision objects and a grid over them), its names, and a scene
// added over it (its own collision file: new sectors, and the base's it hides, swapped in and out). The members of
// 0x0049C910-0x0049D18E are in engine_scene.cpp, those of 0x0049D1A0-0x0049E2EF (the constructor and destructor,
// the super sectors' update, clones, the added scene) in engine_scene_sectors.cpp.
namespace Nx {

class CCollGrid;
class CCollStatic;
class CSector;
class CTexDict;

// (vtable 0x006499E0: 0 the destructor, 1 PostLoad, 2 PostAdd, 3 LoadTextures, 4 LoadCollision, 5 AddCollision,
// 6 UnloadAddScene, 7 CreateSector (the platform's), 8 -, 9 GetDefaultColour, 10 SetRendered.)
struct CScene {
    void** m_vtable;
    char m_name[0x80];            // +0x04: the file it was loaded from
    uint32_t m_checksum;          // +0x84: its name's
    CTexDict* mp_texDict;         // +0x88
    bool m_hasCollision;          // +0x8C: its collision gets a grid (super sectors)
    bool m_unknown8D;             // +0x8D (cleared when made)
    uint8_t pad8E[2];
    Mth::BBox m_unknownBox;       // +0x90 (made empty)
    Mth::BBox m_collisionBox;     // +0xB0
    Lst::HashTable* mp_sectors;   // +0xD0
    CCollGrid* mp_collGrid;       // +0xD4
    char m_pipName[0x80];         // +0xD8: the collision file's (unloaded with the collision)
    int m_collStaticCount;        // +0x158
    CCollStatic* mp_collStatics;  // +0x15C (an array new)
    uint8_t* mp_collObjects;      // +0x160: the collision geometry (LoadCollisionData's)
    char m_addName[0x80];         // +0x164: the scene added to it
    uint32_t m_addData;           // +0x1E4: the added scene's texture dictionary (unloaded with the scene)
    char m_addPipName[0x80];      // +0x1E8: the added scene's collision file
    int m_addCollStaticCount;     // +0x268: the added scene's collision, as the base's
    CCollStatic* mp_addCollStatics; // +0x26C
    uint8_t* mp_addCollObjects;   // +0x270
    bool m_sectorKept;            // +0x274: a sector couldn't be deleted (flag 0x40): marked instead
    bool m_added;                 // +0x275: the added scene shown
    uint8_t pad276[2];
    Mem::ListNode* mp_hiddenSectors;   // +0x278: Lst::Head<CSector> - the base's sectors the added scene hides
    Mem::ListNode* mp_addSceneSectors; // +0x27C: Lst::Head<CSector> - the sectors the added scene made

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

    // --- engine_scene_sectors.cpp ---
    // 0x0049D390: construct - empty boxes, no grid, a sector table of 2^`bits` buckets (none without memory), no
    // names, no collision, empty lists of hidden and added sectors (none without memory). Returns this.
    CScene* Construct(int bits);
    // 0x0049DD10: the destructor's body - every sector deleted (out of the grid first), the table emptied and freed,
    // the collision let go of (0x0049CFB0), the two lists' heads deleted (their nodes left), the added scene's
    // texture dictionary unloaded.
    void DestroyBody();
    // 0x0049E1D0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CScene* Destroy(unsigned int freeFlags);
    // 0x0049D1A0: its statistics into `info` - name, sectors, collision objects, and its geometry's counts.
    void GetDebugInfo(Script::CStruct* info);
    // 0x0049D4E0: `sector` put in the table under its name.
    void AddSector(CSector* sector);
    // 0x0049D510: a clone of `sector` (Clone's `instance`, `flag`, the scene) named `name` (when not 0) in `scene`
    // (this one if null) - kept there; with `flag`, that scene's super sectors marked to be brought up to date. The
    // clone's name (0 if none was made).
    uint32_t CloneSector(CSector* sector, CScene* scene, int instance, bool flag, uint32_t name);
    // 0x0049DF30: the same for the sector `sectorName` of this scene (0 if there's none).
    uint32_t CloneSectorNamed(uint32_t sectorName, CScene* scene, int instance, bool flag, uint32_t name);
    // 0x0049D590: the collision from a scene file's collision data - the static objects (and with m_hasCollision
    // their grid), each given to its sector (a new one, shown, if there's none of its name) and the sector put in the
    // grid (0x40); the weather told (0x004A1090) when the script integer 0xBA1E4254 is set. True.
    bool AddCollisionData(const uint8_t* data, int flag);
    // 0x0049DF70: the collision file "levels\<name>\<name>.col.<ext>" (its name kept) loaded and added. Whether it
    // was there.
    bool LoadCollisionFile(const char* name, int flag);
    // 0x0049D780: the super sectors (the collision grid) brought up to date: the sectors flagged 4 put in (0x40),
    // those flagged 8 or 0x10 taken out - 8: deleted, out of the table, after -, those flagged 0x20 moved; `added`
    // (a list of sectors) taken out too unless they already are. Nothing without a grid.
    void UpdateSuperSectors(Mem::ListNode* added);
    // 0x0049E1B0: UpdateSuperSectors(`added`) if a sector was marked (m_sectorKept). Whether it was.
    bool UpdateSuperSectorsIfMarked(Mem::ListNode* added);
    // 0x0049DFD0: a scene added over this one: (with collision) the base's collision file loaded again (as the
    // original - the added scene's name is only kept), its objects given to their sectors - new ones, shown, flagged
    // 4 and listed as the added scene's -, the super sectors brought up to date; then the platform's slot 5 with the
    // name. Its result (false without collision).
    bool LoadAddScene(const char* name);
    // 0x0049DDE0: the added scene shown or hidden in turn: its sectors put in the table (4) and shown, the base's it
    // hides taken out (0x10) and hidden - or the other way round; the super sectors brought up to date. Whether it's
    // shown now (false, and nothing done, while the added scene has no sectors).
    bool ToggleAddScene();
    // 0x0049E200: the added scene let go of - hidden first, the platform's slot 6 (false: nothing more), its static
    // collision objects deleted (the array), its collision file unloaded, the hidden list emptied, its own sectors
    // deleted (0x0049CB40). True.
    bool UnloadAddedScene();
};
static_assert(offsetof(CScene, m_hasCollision) == 0x8C && offsetof(CScene, m_collisionBox) == 0xB0 &&
                  offsetof(CScene, mp_sectors) == 0xD0 && offsetof(CScene, mp_collGrid) == 0xD4 &&
                  offsetof(CScene, m_pipName) == 0xD8 && offsetof(CScene, m_collStaticCount) == 0x158 &&
                  offsetof(CScene, mp_collStatics) == 0x15C && offsetof(CScene, m_addName) == 0x164 &&
                  offsetof(CScene, m_addData) == 0x1E4 && offsetof(CScene, m_sectorKept) == 0x274 &&
                  offsetof(CScene, m_added) == 0x275 && offsetof(CScene, m_unknownBox) == 0x90 &&
                  offsetof(CScene, mp_addCollStatics) == 0x26C && offsetof(CScene, mp_addSceneSectors) == 0x27C &&
                  sizeof(CScene) == 0x280,
              "layout must match the original");

// 0x0049CA20 (cdecl): a level's collision file name - "levels\<level>\<level>.col.<the platform's extension>" -
// into `out`. The length.
int __cdecl MakeCollisionFileName(char* out, const char* level);

// The nodes of the scene's lists of sectors (Lst::Node<CSector>, vtable 0x006499D8; the heads' 0x006499DC).
struct SectorListNodes {
    // 0x0049D300: the destructor's body (unlinked).
    void DestroyBody();
    // 0x0049D320 / 0x0049D360 (virtual [0] of the node / the head): destroyed, and freed with flags & 1. Return this.
    Mem::ListNode* DestroyNode(unsigned int flags);
    Mem::ListNode* DestroyHead(unsigned int flags);
};

} // namespace Nx
