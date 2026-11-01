#include "engine/script/cache.h"

#include <cstddef>
#include <cstdint>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/lists/key_map.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_module.h"
#include "engine/util/lzss.h"
#include "engine/core/owner.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag

namespace script {

static_assert(offsetof(ScriptCacheEntry, older) == 0xC && offsetof(ScriptCacheEntry, newer) == 0x10,
              "layout must match the original");
static_assert(offsetof(ScriptStore, entries_) == 0x14 && offsetof(ScriptStore, newest_unused_) == 0x18 &&
                  offsetof(ScriptStore, oldest_unused_) == 0x1C && offsetof(ScriptStore, delete_unused_) == 0x20,
              "layout must match the original");

memory::owner<script::ScriptStore*> g_script_store;
static uint32_t g_cache_references;
memory::PoolPair g_cache_entry_pools;

namespace {

ScriptStore*& the_cache() { return g_script_store; }
uint32_t& cache_references() { return g_cache_references; }
memory::PoolPair& entry_pools() { return g_cache_entry_pools; }

// Scripts in the symbol table: {checksum, size, stored size, code - LZSS-compressed when stored < size}.
struct StoredScript {
    uint32_t checksum;
    uint32_t size;
    uint32_t stored_size_;
    uint8_t code[1];
};

} // namespace

// 0x00475AC0
void* ScriptCacheEntry::operator new(size_t size) noexcept
{
    return entry_pools().carve(static_cast<unsigned int>(size));
}

// 0x00475B00
void ScriptCacheEntry::operator delete(void* entry) { entry_pools().dealloc(entry); }

// 0x00475DD0
ScriptStore::ScriptStore()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    entries_ = memory ? new (memory) lists::KeyMap(8) : nullptr;
    memory::Hub::get_pull()->leave_heap_scope();
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    newest_unused_ = nullptr;
    oldest_unused_ = nullptr;
    delete_unused_ = false;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00476090
ScriptStore::~ScriptStore()
{
    // Every entry, by iterating the table (the iteration carries on from where it was after each removal).
    entries_->iter_bucket_ = -1;
    entries_->iter_item_ = nullptr;
    while (entries_->step_iterator() && entries_->iter_item_) {
        auto* entry = static_cast<ScriptCacheEntry*>(entries_->iter_item_->value);
        if (!entry)
            break;
        entries_->erase_entry(entry->name);
        // (left on the unused list: its links are just cleared)
        entry->older = nullptr;
        entry->newer = nullptr;
        if (entry->code)
            memory::free_malloc(entry->code);
        delete entry;
    }
    if (lists::KeyMap* entries = entries_) {
        entries->~KeyMap();
        memory::dealloc(entries);
    }
}

// 0x00475A40
void ScriptStore::add_to_unused(ScriptCacheEntry* entry)
{
    entry->newer = nullptr;
    entry->older = newest_unused_;
    newest_unused_ = entry;
    if (entry->older)
        entry->older->newer = entry;
    if (!oldest_unused_)
        oldest_unused_ = newest_unused_;
}

// 0x00475A70
void ScriptStore::remove_from_unused(ScriptCacheEntry* entry)
{
    if (entry->newer)
        entry->newer->older = entry->older;
    if (entry->older)
        entry->older->newer = entry->newer;
    if (entry == newest_unused_)
        newest_unused_ = entry->older;
    if (entry == oldest_unused_)
        oldest_unused_ = entry->newer;
    entry->older = nullptr;
    entry->newer = nullptr;
}

// 0x00475B60
void ScriptStore::delete_entry(ScriptCacheEntry* entry)
{
    entries_->erase_entry(entry->name);
    remove_from_unused(entry);
    if (entry->code)
        memory::free_malloc(entry->code);
    delete entry;
}

// 0x00475BD0
void ScriptStore::make_room(int size, uint32_t /*name*/)
{
    while (size > static_cast<int>(memory::Hub::get_pull()->script_heap_->largest_free()) - 0x80)
        delete_entry(oldest_unused_); // (null when nothing is left to drop: the original crashes too)
    while (entry_pools().pools[entry_pools().current]->taken_count_ >= 100 && oldest_unused_)
        delete_entry(oldest_unused_);
}

// 0x00475E80
uint8_t* ScriptStore::fetch_script(uint32_t name)
{
    const SymbolTableEntry* symbol = resolve_symbol(name);
    if (symbol) {
        if (symbol->type != kQbvalScriptBody)
            return nullptr;
        name = symbol->name;
    }
    if (auto* entry = static_cast<ScriptCacheEntry*>(entries_->fetch_entry(name, true))) {
        ++entry->usage;
        remove_from_unused(entry);
        return entry->code;
    }
    if (!symbol)
        return nullptr;

    const auto* stored = reinterpret_cast<const StoredScript*>(symbol->raw);
    const uint32_t size = stored->size;
    const uint32_t stored_size = stored->stored_size_;
    make_room(static_cast<int>(size), name);
    auto* entry = new ScriptCacheEntry{};
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    auto* code = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::Hub::get_pull()->leave_heap_scope();
    if (stored_size < size) {
        // (not original: bounded by the script's size - lzss::unpack, decode's bytes for valid code)
        lzss::unpack(stored->code, stored_size, code, size, nullptr);
    } else {
        for (uint32_t i = 0; i < size; ++i)
            code[i] = stored->code[i];
    }
    prepare_script_tokens(code);
    // (a null entry - the pool full - is written through, as in the original)
    entry->code = code;
    entry->usage = 1;
    entry->name = name;
    entries_->store_entry(name, entry);
    return entry->code;
}

// 0x00475D60
void ScriptStore::release_script(uint32_t name)
{
    auto* entry = static_cast<ScriptCacheEntry*>(entries_->fetch_entry(name));
    if (!entry || --entry->usage != 0)
        return;
    if (delete_unused_)
        delete_entry(entry);
    else
        add_to_unused(entry);
}

// 0x00475FD0
void ScriptStore::mark_stale(uint32_t name)
{
    auto* entry = static_cast<ScriptCacheEntry*>(entries_->fetch_entry(name));
    if (!entry)
        return;
    const int usage = entry->usage;
    delete_entry(entry);
    fetch_script(name);
    static_cast<ScriptCacheEntry*>(entries_->fetch_entry(name, true))->usage = usage;
}

// 0x00476040
ScriptStore* create_script_cache(bool create)
{
    if (!the_cache() && create) {
        the_cache() = new ScriptStore;
    }
    ++cache_references();
    return the_cache();
}

// 0x00475A00
void release_script_cache()
{
    if (cache_references() > 1) {
        --cache_references();
        return;
    }
    delete the_cache();
    the_cache() = nullptr;
    cache_references() = 0;
}

} // namespace script
// NOLINTEND(cppcoreguidelines-pro-type-union-access)
