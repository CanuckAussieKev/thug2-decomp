#include "game/script/cache.h"

#include <cstddef>
#include <cstdint>
#include <new>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/lst/hashtable.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sys/module.h"
#include "game/util/lzss.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"

namespace Scripts {

static_assert(offsetof(CScriptCacheEntry, older) == 0xC && offsetof(CScriptCacheEntry, newer) == 0x10,
              "layout must match the original");
static_assert(offsetof(CScriptStore, m_entries) == 0x14 && offsetof(CScriptStore, m_newestUnused) == 0x18 &&
                  offsetof(CScriptStore, m_oldestUnused) == 0x1C && offsetof(CScriptStore, m_deleteUnused) == 0x20,
              "layout must match the original");

Scripts::CScriptStore* g_scriptCache;
uint32_t g_cacheReferences;
MemSys::PoolPair g_cacheEntryPools;

namespace {

CScriptStore*& TheCache() { return g_scriptCache; }
uint32_t& CacheReferences() { return g_cacheReferences; }
MemSys::PoolPair& EntryPools() { return g_cacheEntryPools; }

// GameModules::Module's constructor (0x0044F7E0) and destructor body (0x0044F790).
void ConstructModule(void* module) { static_cast<GameModules::Module*>(module)->Construct(); }
void DestroyModule(void* module) { static_cast<GameModules::Module*>(module)->DestroyBody(); }

// Scripts in the symbol table: {checksum, size, stored size, code - LZSS-compressed when stored < size}.
struct StoredScript {
    uint32_t checksum;
    uint32_t size;
    uint32_t storedSize;
    uint8_t code[1];
};

static VtableSlot kScriptCacheVtable_slots[] = {&Scripts::CScriptStore::Destroy, Stubs::DoNothing, Stubs::DoNothing};
void** const kScriptCacheVtable = reinterpret_cast<void**>(kScriptCacheVtable_slots);
} // namespace

// 0x00475AC0
void* CScriptCacheEntry::operator new(size_t size) noexcept
{
    return EntryPools().Allocate(static_cast<unsigned int>(size));
}

// 0x00475B00
void CScriptCacheEntry::operator delete(void* entry)
{
    EntryPools().Free(entry);
}

// 0x00475DD0
CScriptStore* CScriptStore::Construct()
{
    ConstructModule(this);
    m_vtable = kScriptCacheVtable;
    MemSys::Manager* manager = MemSys::Manager::Get();
    manager->EnterHeapScope(manager->m_scriptHeap);
    void* memory = MemSys::New(sizeof(ListKit::HashTable));
    m_entries = memory ? new (memory) ListKit::HashTable(8) : nullptr;
    MemSys::Manager::Get()->LeaveHeapScope();
    m_newestUnused = nullptr;
    m_oldestUnused = nullptr;
    m_deleteUnused = false;
    return this;
}

// 0x00476090
void CScriptStore::DestroyBody()
{
    m_vtable = kScriptCacheVtable;
    // Every entry, by iterating the table (the iteration carries on from where it was after each removal).
    m_entries->m_iterBucket = -1;
    m_entries->m_iterItem = nullptr;
    while (m_entries->StepIterator() && m_entries->m_iterItem) {
        auto* entry = static_cast<CScriptCacheEntry*>(m_entries->m_iterItem->value);
        if (!entry)
            break;
        m_entries->EraseEntry(entry->name);
        // (left on the unused list: its links are just cleared)
        entry->older = nullptr;
        entry->newer = nullptr;
        if (entry->code)
            MemSys::FreeMalloc(entry->code);
        delete entry;
    }
    if (ListKit::HashTable* entries = m_entries) {
        entries->~HashTable();
        MemSys::Free(entries);
    }
    DestroyModule(this);
}

// 0x004761E0
CScriptStore* CScriptStore::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x00475A40
void CScriptStore::AddToUnused(CScriptCacheEntry* entry)
{
    entry->newer = nullptr;
    entry->older = m_newestUnused;
    m_newestUnused = entry;
    if (entry->older)
        entry->older->newer = entry;
    if (!m_oldestUnused)
        m_oldestUnused = m_newestUnused;
}

// 0x00475A70
void CScriptStore::RemoveFromUnused(CScriptCacheEntry* entry)
{
    if (entry->newer)
        entry->newer->older = entry->older;
    if (entry->older)
        entry->older->newer = entry->newer;
    if (entry == m_newestUnused)
        m_newestUnused = entry->older;
    if (entry == m_oldestUnused)
        m_oldestUnused = entry->newer;
    entry->older = nullptr;
    entry->newer = nullptr;
}

// 0x00475B60
void CScriptStore::DeleteEntry(CScriptCacheEntry* entry)
{
    m_entries->EraseEntry(entry->name);
    RemoveFromUnused(entry);
    if (entry->code)
        MemSys::FreeMalloc(entry->code);
    delete entry;
}

// 0x00475BD0
void CScriptStore::MakeRoom(int size, uint32_t /*name*/)
{
    while (size > static_cast<int>(MemSys::Manager::Get()->m_scriptHeap->LargestFree()) - 0x80)
        DeleteEntry(m_oldestUnused); // (null when nothing is left to drop: the original crashes too)
    while (EntryPools().pools[EntryPools().current]->m_used >= 100 && m_oldestUnused)
        DeleteEntry(m_oldestUnused);
}

// 0x00475E80
uint8_t* CScriptStore::GetScript(uint32_t name)
{
    CSymbolTableEntry* symbol = ResolveSymbol(name);
    if (symbol) {
        if (symbol->type != QBVAL_SCRIPT_BODY)
            return nullptr;
        name = symbol->name;
    }
    if (auto* entry = static_cast<CScriptCacheEntry*>(m_entries->GetItem(name, true))) {
        ++entry->usage;
        RemoveFromUnused(entry);
        return entry->code;
    }
    if (!symbol)
        return nullptr;

    const auto* stored = reinterpret_cast<const StoredScript*>(symbol->raw);
    uint32_t size = stored->size;
    uint32_t storedSize = stored->storedSize;
    MakeRoom(static_cast<int>(size), name);
    auto* entry = new CScriptCacheEntry{};
    MemSys::Manager* manager = MemSys::Manager::Get();
    manager->EnterHeapScope(manager->m_scriptHeap);
    auto* code = static_cast<uint8_t*>(MemSys::AllocBlock(size));
    MemSys::Manager::Get()->LeaveHeapScope();
    if (storedSize < size) {
        Lzss::Decode(stored->code, code, static_cast<int>(storedSize));
    } else {
        for (uint32_t i = 0; i < size; ++i)
            code[i] = stored->code[i];
    }
    PrepareScriptTokens(code);
    // (a null entry - the pool full - is written through, as in the original)
    entry->code = code;
    entry->usage = 1;
    entry->name = name;
    m_entries->StoreEntry(name, entry);
    return entry->code;
}

// 0x00475D60
void CScriptStore::ReleaseScript(uint32_t name)
{
    auto* entry = static_cast<CScriptCacheEntry*>(m_entries->GetItem(name));
    if (!entry || --entry->usage != 0)
        return;
    if (m_deleteUnused)
        DeleteEntry(entry);
    else
        AddToUnused(entry);
}

// 0x00475FD0
void CScriptStore::Invalidate(uint32_t name)
{
    auto* entry = static_cast<CScriptCacheEntry*>(m_entries->GetItem(name));
    if (!entry)
        return;
    int usage = entry->usage;
    DeleteEntry(entry);
    GetScript(name);
    static_cast<CScriptCacheEntry*>(m_entries->GetItem(name, true))->usage = usage;
}

// 0x00476040
CScriptStore* CreateScriptCache(bool create)
{
    if (!TheCache() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CScriptStore), nullptr, 1);
        TheCache() = memory ? static_cast<CScriptStore*>(memory)->Construct() : nullptr;
    }
    ++CacheReferences();
    return TheCache();
}

// 0x00475A00
void ReleaseScriptCache()
{
    if (CacheReferences() > 1) {
        --CacheReferences();
        return;
    }
    if (CScriptStore* cache = TheCache())
        reinterpret_cast<void(__thiscall*)(CScriptStore*, unsigned int)>(cache->m_vtable[0])(cache, 1);
    TheCache() = nullptr;
    CacheReferences() = 0;
}

} // namespace Script
