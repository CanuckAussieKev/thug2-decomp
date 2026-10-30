#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "hook/game_global.h"

// The PC scene and sector (Nx engine), and the PC screen effects object's setters.
namespace Nx {


struct sScene;

// What the scene's PostLoad packs the sectors' vertices into (one vertex buffer).
struct sVertexArena {
    void* buffer;  // +0x00: IDirect3DVertexBuffer9
    uint32_t used; // +0x04
    uint32_t size; // +0x08: bytes
    void* data;    // +0x0C: while locked
};

// The PC sector (0x34 bytes, vtable 0x0064A718; the engine's CSector - constructor 0x0049E900, destructor body
// 0x0049E630 - for its first 0x20 bytes: +0x14 its geometry).
struct CPcSector {
    void** vtable;
    uint8_t engine04[0x10];
    void* geom;    // +0x14
    uint8_t engine18[4];
    uint32_t flags; // +0x1C: 0x800000 billboards, 0x200000 the meshes' flag 0x200
    float unknown20[3]; // +0x20 (zero)
    float unknown2C;    // +0x2C (1)
    uint8_t engine30[4];

    // 0x004C6550: construct. Returns this.
    CPcSector* Construct();
    // 0x004C65B0: the destructor's body (a jump to the engine's).
    void DestroyBody();
    // 0x004C6580 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcSector* Destroy(unsigned int freeFlags);
    // 0x004C6530 (virtual [1]): with `on`, its geometry given to the engine's 0x00490DC0.
    void SetActive(bool on);
    // 0x004C65C0 (virtual [3]): a new sector (the arguments unused). Null if no memory.
    CPcSector* Clone(int unused1, int unused2);
    // 0x004C6650: loaded from a scene file in memory (moved past it): its checksum, flags, box, billboard and
    // meshes (each made, its vertices loaded by 0x004D5CC0, its index lists read and dropped). True.
    bool LoadFromMemory(uint8_t** cursor);
    // 0x004C6B10: the same from a file (the vertices by 0x004D6100, given `unknown` unless a billboard). True.
    bool LoadFromFile(void* file, uint32_t unknown);
    // 0x004C64E0 (virtual [9]): an empty box (a static one, made the first time).
    const Mth::BBox* GetBoundingBox();
};
static_assert(offsetof(CPcSector, geom) == 0x14 && offsetof(CPcSector, unknown20) == 0x20 && sizeof(CPcSector) == 0x34,
              "layout must match the original");

// The PC scene (0x284 bytes, vtable 0x0064A6B8; the engine's CScene - constructor 0x0049D390, destructor body
// 0x0049DD10 - for its first 0x280 bytes: +0xD0 the sectors' hash table).
struct CPcScene {
    void** vtable;
    uint8_t engine04[0xCC];
    void* sectors;           // +0xD0: Lst::HashTable of the sectors
    uint8_t engine0D4[0x1AC];
    sScene* meshes;          // +0x280: its mesh list

    // 0x004C6130: construct (`arg` for the engine's constructor). Returns this.
    CPcScene* Construct(int arg);
    // 0x004C6180: the destructor's body (a jump to the engine's).
    void DestroyBody();
    // 0x004C6150 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcScene* Destroy(unsigned int flags);
    // 0x004C6270 (virtual [1]): after loading: with `arena`, a write-only vertex buffer of its size made and locked
    // and the sectors' first vertex buffers moved into it; the sectors' meshes counted, the mesh list allocated, the
    // meshes added to it and sorted.
    void PostLoad(sVertexArena* arena);
    // 0x004C6190 (virtual [7]): a new sector. Null if no memory.
    CPcSector* CreateSector();
    // 0x004C6200: the sectors gone through (nothing done with them - stripped).
    void UnloadSectors();
};
static_assert(offsetof(CPcScene, sectors) == 0xD0 && offsetof(CPcScene, meshes) == 0x280 && sizeof(CPcScene) == 0x284,
              "layout must match the original");

// The PC screen effects (8 bytes, vtable 0x0064A6E4; the engine's 0x0049E2F0 - destructor body 0x0049E300): its
// setters write the renderer's settings (screen_fx.cpp: the bloom).
struct CPcScreenEffects {
    void** vtable;
    uint8_t engine04[4];

    // 0x004C6480: construct. Returns this.
    CPcScreenEffects* Construct();
    // 0x004C64A0: the destructor's body.
    void DestroyBody();
    // 0x004C64B0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcScreenEffects* Destroy(unsigned int flags);
    // 0x004C63C0 (virtual [2]): the bloom on / off (0x00705720).
    void SetBloom(int on);
    // 0x004C63E0 (virtual [3]): the bloom's cut-off colour (0x0067E98C).
    void SetBloomCutOff(uint32_t colour);
    // 0x004C63F0 (virtual [4]): the bloom passes' alphas (0x0067E97C, 0x0067E980).
    void SetBloomAlphas(uint32_t first, uint32_t second);
    // 0x004C6410 (virtual [5]): 0x0067E990.
    void SetUnknown990(uint32_t value);
    // 0x004C6420 (virtual [6]): 0x00705724 on / off.
    void SetUnknown5724(int on);
    // 0x004C6460 (virtual [8]): 0x0067E994.
    void SetUnknown994(uint32_t value);
    // 0x004C6470 (virtual [10]): 0x0067E984.
    void SetUnknown984(uint32_t value);
    // 0x004C6440 (virtual [11]): 0x0067E988.
    void SetUnknown988(uint32_t value);
    // 0x004C6450 (virtual [12]): 0x00705728.
    void SetUnknown5728(uint32_t value);
};

GAME_GLOBAL_EXTERN(uint32_t, g_bloomOn);
GAME_GLOBAL_EXTERN(uint32_t, g_bloomAlphaFirst);
GAME_GLOBAL_EXTERN(uint32_t, g_bloomAlphaSecond);
GAME_GLOBAL_EXTERN(uint32_t, g_bloomCutOff);

} // namespace Nx
