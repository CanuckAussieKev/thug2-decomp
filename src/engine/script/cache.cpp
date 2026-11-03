#include "engine/script/cache.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/lists/key_map.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/script/tokens.h"
#include "formats/lzss.h"
#include <gsl/pointers>
#include "engine/core/checked_index.h"
#include "engine/script/script_state.h"

namespace script {

constexpr size_t kScriptCacheEntryOlderOffset = 0xC;
constexpr size_t kScriptCacheEntryNewerOffset = 0x10;
static_assert(offsetof(ScriptCacheEntry, older) == kScriptCacheEntryOlderOffset && offsetof(ScriptCacheEntry, newer) == kScriptCacheEntryNewerOffset,
              "layout must match the original");

script::ScriptStore* script_store() { return script_state().script_store().get(); }

namespace {
// Buckets of the cache's table; the heap a new script leaves free; the most cache entries taken from their pool.
constexpr int kEntryTableBuckets = 8;
constexpr int kHeapSlackBytes = 0x80;
constexpr int kCacheEntryLimit = 100;

uint32_t& cache_references()
{
    static uint32_t cache_references{};
    return cache_references;
}

} // namespace

// 0x00475AC0
void* ScriptCacheEntry::operator new(size_t size) noexcept
{
    return script_state().cache_entry_pools().carve(static_cast<unsigned int>(size));
}

// 0x00475B00
void ScriptCacheEntry::operator delete(void* entry) { script_state().cache_entry_pools().dealloc(entry); }

// 0x00475DD0
ScriptStore::ScriptStore()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    entries_ = memory ? memory::construct_in<lists::KeyMap>(memory, kEntryTableBuckets) : nullptr;
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
    entries_->set_iter_bucket(-1);
    entries_->set_iter_item(nullptr);
    while (entries_->step_iterator() && entries_->iter_item()) {
        gsl::owner<ScriptCacheEntry*> entry = memory::adopt<ScriptCacheEntry*>(entries_->iter_item()->value);
        if (!entry)
            break;
        entries_->erase_entry(entry->name);
        // (left on the unused list: its links are just cleared)
        entry->older = nullptr;
        entry->newer = nullptr;
        if (entry->code) {
            forget_code_block(entry->code);
            memory::free_malloc(entry->code);
        }
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
    if (entry->code) {
        forget_code_block(entry->code);
        memory::free_malloc(entry->code);
    }
    gsl::owner<ScriptCacheEntry*> owned = memory::owned_element(entry); // (the store's: its table's value)
    delete owned;
}

// 0x00475BD0
void ScriptStore::make_room(int size, uint32_t /*name*/)
{
    while (size > static_cast<int>(memory::Hub::get_pull()->script_heap()->largest_free()) - kHeapSlackBytes)
        delete_entry(oldest_unused_); // (null when nothing is left to drop: the original crashes too)
    while (core::at(script_state().cache_entry_pools().pools(), script_state().cache_entry_pools().current())->taken_count() >= kCacheEntryLimit && oldest_unused_)
        delete_entry(oldest_unused_);
}

// 0x00475E80
uint8_t* ScriptStore::fetch_script(uint32_t name)
{
    const SymbolTableEntry* symbol = resolve_symbol(name);
    if (symbol) {
        if (symbol->type() != kQbvalScriptBody)
            return nullptr;
        name = symbol->name_value();
    }
    if (auto* entry = static_cast<ScriptCacheEntry*>(entries_->fetch_entry(name, true))) {
        ++entry->usage;
        remove_from_unused(entry);
        return entry->code;
    }
    if (!symbol)
        return nullptr;

    const StoredScript stored = read_stored_script(symbol->value().code());
    const uint32_t size = stored.size_;
    const uint32_t stored_size = stored.stored_size_;
    make_room(static_cast<int>(size), name);
    auto entry = memory::make_owned_value<ScriptCacheEntry>();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    auto* code = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::Hub::get_pull()->leave_heap_scope();
    const std::span<uint8_t> script(code, size);
    note_code_block(script);
    if (stored_size < size) {
        // (not original: bounded by the script's size - lzss::unpack, decode's bytes for valid code)
        lzss::unpack(stored.code_.data(), stored_size, code, size, nullptr);
    } else {
        std::ranges::copy(stored.code_.first(size), script.begin());
    }
    prepare_script_tokens(script);
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
    if (!script_state().script_store() && create) {
        script_state().script_store() = std::make_unique<ScriptStore>();
    }
    ++cache_references();
    return script_state().script_store().get();
}

// 0x00475A00
void release_script_cache()
{
    if (cache_references() > 1) {
        --cache_references();
        return;
    }
    memory::destroy_then_clear(script_state().script_store());
    cache_references() = 0;
}

} // namespace script
