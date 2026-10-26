#include "game/script/cache.h"

#include <cstddef>
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
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

static_assert(offsetof(CScriptCacheEntry, older) == 0xC && offsetof(CScriptCacheEntry, newer) == 0x10,
              "layout must match the original");
static_assert(offsetof(CScriptCache, m_entries) == 0x14 && offsetof(CScriptCache, m_newestUnused) == 0x18 &&
                  offsetof(CScriptCache, m_oldestUnused) == 0x1C && offsetof(CScriptCache, m_deleteUnused) == 0x20,
              "layout must match the original");

namespace {

CScriptCache*& TheCache() { return GameVariable<CScriptCache*>(0x006F3558); }
uint32_t& CacheReferences() { return GameVariable<uint32_t>(0x006F355C); }
Mem::PoolPair& EntryPools() { return GameVariable<Mem::PoolPair>(0x006F3560); }

// Mdl::Module's constructor (0x0044F7E0) and destructor body (0x0044F790).
void ConstructModule(void* module) { static_cast<Mdl::Module*>(module)->Construct(); }
void DestroyModule(void* module) { static_cast<Mdl::Module*>(module)->DestroyBody(); }

// Scripts in the symbol table: {checksum, size, stored size, code - LZSS-compressed when stored < size}.
struct StoredScript {
    uint32_t checksum;
    uint32_t size;
    uint32_t storedSize;
    uint8_t code[1];
};

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
CScriptCache* CScriptCache::Construct()
{
    ConstructModule(this);
    m_vtable = reinterpret_cast<void**>(0x00648468);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    void* memory = Mem::New(sizeof(Lst::HashTable));
    m_entries = memory ? new (memory) Lst::HashTable(8) : nullptr;
    Mem::Manager::Get()->PopContext();
    m_newestUnused = nullptr;
    m_oldestUnused = nullptr;
    m_deleteUnused = false;
    return this;
}

// 0x00476090
void CScriptCache::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x00648468);
    // Every entry, by iterating the table (the iteration carries on from where it was after each removal).
    m_entries->m_iterBucket = -1;
    m_entries->m_iterItem = nullptr;
    while (m_entries->IterateNext() && m_entries->m_iterItem) {
        auto* entry = static_cast<CScriptCacheEntry*>(m_entries->m_iterItem->value);
        if (!entry)
            break;
        m_entries->FlushItem(entry->name);
        // (left on the unused list: its links are just cleared)
        entry->older = nullptr;
        entry->newer = nullptr;
        if (entry->code)
            Mem::FreeMalloc(entry->code);
        delete entry;
    }
    if (Lst::HashTable* entries = m_entries) {
        entries->~HashTable();
        Mem::Free(entries);
    }
    DestroyModule(this);
}

// 0x004761E0
CScriptCache* CScriptCache::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00475A40
void CScriptCache::AddToUnused(CScriptCacheEntry* entry)
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
void CScriptCache::RemoveFromUnused(CScriptCacheEntry* entry)
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
void CScriptCache::DeleteEntry(CScriptCacheEntry* entry)
{
    m_entries->FlushItem(entry->name);
    RemoveFromUnused(entry);
    if (entry->code)
        Mem::FreeMalloc(entry->code);
    delete entry;
}

// 0x00475BD0
void CScriptCache::MakeRoom(int size, uint32_t /*name*/)
{
    while (size > static_cast<int>(Mem::Manager::Get()->m_scriptHeap->LargestFree()) - 0x80)
        DeleteEntry(m_oldestUnused); // (null when nothing is left to drop: the original crashes too)
    while (EntryPools().pools[EntryPools().current]->m_used >= 100 && m_oldestUnused)
        DeleteEntry(m_oldestUnused);
}

// 0x00475E80
uint8_t* CScriptCache::GetScript(uint32_t name)
{
    CSymbolTableEntry* symbol = LookUpSymbol(name);
    if (symbol) {
        if (symbol->type != ESYMBOLTYPE_QSCRIPT)
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
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    auto* code = static_cast<uint8_t*>(Mem::Malloc(size));
    Mem::Manager::Get()->PopContext();
    if (storedSize < size) {
        Lzss::Decode(stored->code, code, static_cast<int>(storedSize));
    } else {
        for (uint32_t i = 0; i < size; ++i)
            code[i] = stored->code[i];
    }
    PreProcessScript(code);
    // (a null entry - the pool full - is written through, as in the original)
    entry->code = code;
    entry->usage = 1;
    entry->name = name;
    m_entries->PutItem(name, entry);
    return entry->code;
}

// 0x00475D60
void CScriptCache::ReleaseScript(uint32_t name)
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
void CScriptCache::Invalidate(uint32_t name)
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
CScriptCache* CreateScriptCache(bool create)
{
    if (!TheCache() && create) {
        void* memory = Mem::Calloc(sizeof(CScriptCache), nullptr, 1);
        TheCache() = memory ? static_cast<CScriptCache*>(memory)->Construct() : nullptr;
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
    if (CScriptCache* cache = TheCache())
        reinterpret_cast<void(__thiscall*)(CScriptCache*, unsigned int)>(cache->m_vtable[0])(cache, 1);
    TheCache() = nullptr;
    CacheReferences() = 0;
}

} // namespace Script

// (cast: the argument-size check doesn't know noexcept function types)
REPLACE(0x00475AC0, static_cast<void* (*)(size_t)>(&Script::CScriptCacheEntry::operator new));
REPLACE(0x00475B00, static_cast<void (*)(void*)>(&Script::CScriptCacheEntry::operator delete));
REPLACE(0x00475DD0, &Script::CScriptCache::Construct);
REPLACE(0x00476090, &Script::CScriptCache::DestroyBody);
REPLACE(0x004761E0, &Script::CScriptCache::Destroy);
REPLACE(0x00475E80, &Script::CScriptCache::GetScript);
REPLACE(0x00475D60, &Script::CScriptCache::ReleaseScript);
REPLACE(0x00475FD0, &Script::CScriptCache::Invalidate);
REPLACE(0x00475A40, &Script::CScriptCache::AddToUnused);
REPLACE(0x00475A70, &Script::CScriptCache::RemoveFromUnused);
REPLACE(0x00475B60, &Script::CScriptCache::DeleteEntry);
REPLACE(0x00475BD0, &Script::CScriptCache::MakeRoom);
REPLACE(0x00476040, Script::CreateScriptCache);
REPLACE(0x00475A00, Script::ReleaseScriptCache);
