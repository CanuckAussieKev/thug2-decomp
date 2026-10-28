#pragma once

#include <cstddef>
#include <cstdint>

namespace Lst {
class HashTable;
}

// The engine's texture dictionary (Nx::CTexDict; the PC's CPcTexDict - pc_texture.h - extends it): a hash table of
// textures by checksum, loaded, made, copied, replaced and flushed through the platform's slots; and the loaded
// dictionaries themselves, shared by name and counted (the table at 0x00700F04).
namespace Nx {

class CTexture;

// The dictionary (0x10 bytes of the engine's; vtable 0x00649BB4 - its own slots are stubs; PC 0x0064A824). Its
// virtual slots (CPcTexDict's): [0] destroy, [1] a texture loaded from a file, [2] one loaded from memory, [3] one
// made, [5] a texture destroyed (true if it was), [8] a copy of a texture.
class CTexDict {
public:
    // 0x0049F830: construct named `checksum`, used once, with an empty table of 32 buckets when `makeTable` (none
    // without memory) or none. Returns this.
    CTexDict* Construct(uint32_t checksum, bool makeTable);
    // 0x0049F8F0: the same, named by the CRC of `name`.
    CTexDict* ConstructNamed(const char* name, bool makeTable);
    // 0x0049FC70: the destructor's body - every texture flushed, the table emptied and freed (its buckets too).
    void DestroyBody();
    // 0x0049FCC0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CTexDict* Destroy(unsigned int freeFlags);

    // 0x0049F540: the texture `checksum`, or null (no table: null).
    CTexture* GetTexture(uint32_t checksum);
    // 0x0049F560: the texture named by the CRC of `name` after its last slash or backslash.
    CTexture* GetTextureByName(const char* name);
    // 0x0049F660: the texture `name` under any of the keys its CRC times 2, 3, 4, 6, 12, 8 and 24 (the list at
    // 0x0067E520, up to its -1) - else the same for the name with its last four characters replaced by
    // "_auto32m0.png" (when it's longer than four). Null if none.
    CTexture* FindTexture(const char* name);
    // 0x0049F9B0: a texture loaded from a file (slot 1), kept on flushes with `permanent`, added under its checksum.
    // Returns it (null if the platform made none).
    CTexture* LoadTexture(const char* name, uint32_t a1, uint32_t a2, bool permanent);
    // 0x0049FA20: a texture loaded from memory (slot 2 with all the arguments but `permanent`), kept and added as
    // LoadTexture's.
    CTexture* LoadTextureFromBuffer(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4,
                                    bool permanent, uint32_t a6);
    // 0x0049FAA0: a texture made (slot 3 with the first six arguments), kept and added as LoadTexture's.
    CTexture* CreateTexture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bitsPerPixel, uint32_t a4,
                            uint32_t a5, bool permanent);
    // 0x0049F500: a texture destroyed by the platform (slot 5) and, if it was, taken out of the table. Whether it was.
    bool UnloadTexture(CTexture* texture);
    // 0x0049F5C0: every texture destroyed and taken out - but the permanent ones unless `all`.
    void FlushTextures(bool all);
    // 0x0049F800: the texture FindTexture finds made a copy of `source` (its slot 4). False if there's none.
    bool ReplaceTexture(const char* name, CTexture* source);
    // 0x0049FB20: the same from a file - loaded (permanent) on the top-down heap, copied, then unloaded again. False
    // if there's no such texture to replace.
    bool ReplaceTextureFromFile(const char* name, const char* file);
    // 0x0049FBE0: a copy of `source` (slot 8) named `checksum` and added. Returns it.
    CTexture* CopyTexture(uint32_t checksum, CTexture* source);
    // 0x0049FC20: the same, then `overlay` drawn over it (the copy's slot 41 with `argument`).
    CTexture* CopyTextureWithOverlay(uint32_t checksum, CTexture* source, CTexture* overlay, uint32_t argument);

    void** vtable;
    uint32_t m_checksum;      // +0x04
    uint16_t m_refCount;      // +0x08: its loads (UnloadTexDict deletes it at 0)
    uint8_t padA[2];
    Lst::HashTable* mp_textures; // +0x0C
};
static_assert(offsetof(CTexDict, mp_textures) == 0xC && sizeof(CTexDict) == 0x10, "layout must match the original");

// The loaded dictionaries (the hash table 0x00700F04), by checksum.
// 0x0049F010: the dictionary `checksum`, or null.
CTexDict* __cdecl FindTexDict(uint32_t checksum);
// 0x0049F0C0: a dictionary loaded from "<path>.<platform extension>" (0x004C7270 with the other arguments) - or the one
// loaded already, counted once more - under the CRC of the path plus `offset`.
CTexDict* __cdecl LoadTexDict(const char* path, int level, uint32_t offset, uint32_t a3, uint32_t a4);
// 0x0049F050: one loaded from memory (0x004C72E0) under `checksum + offset` - or the one loaded already (not counted
// again).
CTexDict* __cdecl LoadTexDictFromMemory(uint32_t checksum, const uint8_t* data, uint32_t size, uint32_t a3,
                                        uint32_t offset, uint32_t a5, uint32_t a6);
// 0x0049F180: a model's textures - "<base>.tex" of its name (0x0048BFE0 splits it: whether it's a skin) loaded as
// LoadTexDict's (level 0; the skin flag as the third argument).
CTexDict* __cdecl LoadModelTextures(const char* name, uint32_t offset, uint32_t a4);
// 0x0049EFD0: a dictionary used once less - taken out and deleted (0x004B6920) at none. Whether it was.
bool __cdecl UnloadTexDict(CTexDict* dict);
// 0x0049EF90: "<name>.<platform extension>" into `out`.
void __cdecl TexDictFileName(char* out, const char* name);
// 0x0049EFB0: the particle texture dictionary's (0x00700F20) textures flushed (FlushTextures).
void __cdecl FlushParticleTextures(bool all);

} // namespace Nx
