#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include "engine/core/slab_bank.h"
#include "engine/sys/game_module.h"
#include <gsl/pointers>

namespace lists {
class KeyMap;
}

namespace script {

// The script cache (one instance, 0x006F3558, reference counted by 0x006F355C): the scripts' code, decompressed
// and pre-processed, by script name. Scripts are stored compressed in the symbol table (see define_script_symbol);
// a running script (and every script it has called and is waiting on) holds a use of its cached code, which
// fetch_script takes and release_script gives back.
//
// Code nobody uses is kept, on a list of unused entries (newest first), until room is needed: before a script is
// decompressed, the oldest unused entries are dropped while the script heap's largest free block, less 128
// bytes, is smaller than the script, and while 100 or more entries are in use in the entry pool. (With no unused
// entry left to drop and still too little room, the original reads through a null pointer - kept.)
struct ScriptCacheEntry {
    // 0x00475AC0: from the current pool of 0x006F3560 (300 items in the original, 1200 ours - engine/core/memory_sizes.h; null when it's full), or the default heap
    // when there's no pool.
    static void* operator new(size_t size) noexcept;
    // 0x00475B00: back to whichever pool holds it, or the heap.
    static void operator delete(void* entry);

    uint32_t name;
    uint8_t* code;           // decompressed, from the script heap
    int usage;               // the scripts using it
    ScriptCacheEntry* older; // the unused list
    ScriptCacheEntry* newer;
};
static_assert(sizeof(ScriptCacheEntry) == 0x14, "allocated as 0x14 bytes by the original");

// An engine module (vtable 0x00648468; registered and started with the module manager, but its start and stop do
// nothing - the empty 0x005480A0).
class ScriptStore final : public modules::PartUnit {
public:
    ScriptStore(const ScriptStore&) = delete;
    ScriptStore& operator=(const ScriptStore&) = delete;
    // 0x00475DD0
    ScriptStore();
    // 0x00476090 / 0x004761E0: every entry and its code is freed, then the table.
    ~ScriptStore() override;
    void start_callback() override {}
    void stop_callback() override {}

    // 0x00475E80: the code of script `name` (a name naming another symbol is followed), taking a use of it;
    // null if `name` isn't a script. Decompressed and pre-processed on first use.
    uint8_t* fetch_script(uint32_t name);
    // 0x00475D60: give back a use of script `name` (from its cached name). Unused code goes on the unused list
    // (or, with delete_unused_, is freed at once).
    void release_script(uint32_t name);
    // 0x00475FD0: script `name` has changed: its code is decompressed again (from the symbol table as it is
    // now), keeping its use count. The old code is freed - scripts still running it are restarted by the loader
    // (restart_changed_scripts). An entry that was unused gets a count of 0 but stays off the unused list, so it's
    // never dropped (an original quirk, kept).
    void mark_stale(uint32_t name);

    // 0x00475A40: put an entry at the new end of the unused list.
    void add_to_unused(ScriptCacheEntry* entry);
    // 0x00475A70: take an entry off the unused list (it may not be on it).
    void remove_from_unused(ScriptCacheEntry* entry);
    // 0x00475B60: drop an entry: out of the table and the unused list, its code freed.
    void delete_entry(ScriptCacheEntry* entry);
    // 0x00475BD0: drop the oldest unused entries until a script of `size` bytes fits (see above). `name` is
    // unused.
    void make_room(int size, uint32_t name);

private:
    lists::KeyMap* entries_; // 8 bits, from the script heap
    ScriptCacheEntry* newest_unused_;
    ScriptCacheEntry* oldest_unused_;
    bool delete_unused_; // never set in this game
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(ScriptStore, entries_) == 0x14 && offsetof(ScriptStore, newest_unused_) == 0x18 &&
                          offsetof(ScriptStore, oldest_unused_) == 0x1C && offsetof(ScriptStore, delete_unused_) == 0x20,
                      "layout must match the original");
    }
};
static_assert(sizeof(ScriptStore) == 0x24, "allocated as 0x24 bytes by the original");

// 0x00476040: with `create`, make the cache if there's none; either way take a reference. Returns the cache.
ScriptStore* create_script_cache(bool create);
// 0x00475A00: give back a reference; the last one destroys the cache.
void release_script_cache();

script::ScriptStore* script_store();

} // namespace script
