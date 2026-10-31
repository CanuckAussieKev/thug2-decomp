#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include "game/core/pool.h"

namespace Lst {
class HashTable;
}

namespace Script {

// The script cache (one instance, 0x006F3558, reference counted by 0x006F355C): the scripts' code, decompressed
// and pre-processed, by script name. Scripts are stored compressed in the symbol table (see CreateScriptSymbol);
// a running script (and every script it has called and is waiting on) holds a use of its cached code, which
// GetScript takes and ReleaseScript gives back.
//
// Code nobody uses is kept, on a list of unused entries (newest first), until room is needed: before a script is
// decompressed, the oldest unused entries are dropped while the script heap's largest free block, less 128
// bytes, is smaller than the script, and while 100 or more entries are in use in the entry pool. (With no unused
// entry left to drop and still too little room, the original reads through a null pointer - kept.)
struct CScriptCacheEntry {
    // 0x00475AC0: from the current pool of 0x006F3560 (300 items in the original, 1200 ours - game/core/memory_sizes.h; null when it's full), or the default heap
    // when there's no pool.
    static void* operator new(size_t size) noexcept;
    // 0x00475B00: back to whichever pool holds it, or the heap.
    static void operator delete(void* entry);

    uint32_t name;
    uint8_t* code;              // decompressed, from the script heap
    int usage;                  // the scripts using it
    CScriptCacheEntry* older;   // the unused list
    CScriptCacheEntry* newer;
};
static_assert(sizeof(CScriptCacheEntry) == 0x14, "allocated as 0x14 bytes by the original");

// An engine module (Mdl::Module: registered and started with the module manager, but its Start and Stop do
// nothing).
struct CScriptCache {
    // 0x00475DD0
    CScriptCache* Construct();
    // 0x00476090 (destructor body): every entry and its code is freed, then the table.
    void DestroyBody();
    // 0x004761E0 (scalar deleting destructor, vtable slot 0)
    CScriptCache* Destroy(unsigned int flags);

    // 0x00475E80: the code of script `name` (a name naming another symbol is followed), taking a use of it;
    // null if `name` isn't a script. Decompressed and pre-processed on first use.
    uint8_t* GetScript(uint32_t name);
    // 0x00475D60: give back a use of script `name` (from its cached name). Unused code goes on the unused list
    // (or, with m_deleteUnused, is freed at once).
    void ReleaseScript(uint32_t name);
    // 0x00475FD0: script `name` has changed: its code is decompressed again (from the symbol table as it is
    // now), keeping its use count. The old code is freed - scripts still running it are restarted by the loader
    // (RestartChangedScripts). An entry that was unused gets a count of 0 but stays off the unused list, so it's
    // never dropped (an original quirk, kept).
    void Invalidate(uint32_t name);

    // 0x00475A40: put an entry at the new end of the unused list.
    void AddToUnused(CScriptCacheEntry* entry);
    // 0x00475A70: take an entry off the unused list (it may not be on it).
    void RemoveFromUnused(CScriptCacheEntry* entry);
    // 0x00475B60: drop an entry: out of the table and the unused list, its code freed.
    void DeleteEntry(CScriptCacheEntry* entry);
    // 0x00475BD0: drop the oldest unused entries until a script of `size` bytes fits (see above). `name` is
    // unused.
    void MakeRoom(int size, uint32_t name);

    void** m_vtable;            // 0x00648468: {Destroy, Start, Stop}
    uint8_t m_module[0x14 - 4]; // Mdl::Module's fields
    Lst::HashTable* m_entries;  // 8 bits, from the script heap
    CScriptCacheEntry* m_newestUnused;
    CScriptCacheEntry* m_oldestUnused;
    bool m_deleteUnused;        // never set in this game
};
static_assert(sizeof(CScriptCache) == 0x24, "allocated as 0x24 bytes by the original");

// 0x00476040: with `create`, make the cache if there's none; either way take a reference. Returns the cache.
CScriptCache* CreateScriptCache(bool create);
// 0x00475A00: give back a reference; the last one destroys the cache.
void ReleaseScriptCache();

extern Script::CScriptCache* g_scriptCache;
extern Mem::PoolPair g_cacheEntryPools;

} // namespace Script
