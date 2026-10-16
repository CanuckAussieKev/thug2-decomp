// Self-tests for the script cache (src/game/script/cache.cpp): the original against ours on a stand-in memory
// manager (a default heap and a script heap), entry pools and symbol table.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/lst/hashtable.h"
#include "game/script/cache.h"
#include "game/script/struct.h"
#include "game/util/lzss.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using Script::CScriptCache;
using Script::CScriptCacheEntry;
using Script::CSymbolTableEntry;

constexpr uint32_t kArenaSize = 0x40000;
alignas(16) uint8_t g_scache_arenas[2][kArenaSize];

// Globals the cache code touches: saved before the test, put back after (also by the cleanup after a crash).
struct SavedGlobal {
    uint32_t address;
    uint32_t size;
    std::vector<uint8_t> bytes;
};
std::vector<SavedGlobal> g_scache_saved;
bool g_scache_swapped = false;

void SaveGlobals()
{
    g_scache_saved = {
        {0x006F3558, 4, {}},   // the cache
        {0x006F355C, 4, {}},   // its references
        {0x006F3560, 16, {}},  // the entry pools
        {0x007D1808, 16, {}},  // the shared item pools (hash table items)
        {0x007CC979, 1, {}},   // the memory manager is ready
        {0x007D1800, 4, {}},   // the memory manager
        {0x006F3624, 4, {}},   // the symbol table
        {0x0068FF68, 0x1000, {}}, // the LZSS decoder's window
    };
    for (SavedGlobal& s : g_scache_saved) {
        s.bytes.resize(s.size);
        std::memcpy(s.bytes.data(), reinterpret_cast<void*>(s.address), s.size);
    }
    g_scache_swapped = true;
}

void RestoreGlobals()
{
    if (!g_scache_swapped)
        return;
    for (SavedGlobal& s : g_scache_saved)
        std::memcpy(reinterpret_cast<void*>(s.address), s.bytes.data(), s.size);
    g_scache_swapped = false;
}

// A script's stored form: {checksum, size, stored size, code}.
struct StoredScript {
    std::vector<uint8_t> block;
    void Set(const std::vector<uint8_t>& decoded, const std::vector<uint8_t>& stored)
    {
        block.assign(12 + stored.size(), 0);
        uint32_t header[3] = {0xC0DE0000u + static_cast<uint32_t>(decoded.size()),
                              static_cast<uint32_t>(decoded.size()), static_cast<uint32_t>(stored.size())};
        std::memcpy(block.data(), header, 12);
        std::memcpy(block.data() + 12, stored.data(), stored.size());
    }
};

// LZSS data decoding to `decoded`: references back `distance` bytes where they match, else literals.
std::vector<uint8_t> Compress(const std::vector<uint8_t>& decoded, size_t distance)
{
    std::vector<uint8_t> out;
    size_t flagAt = 0;
    int bit = 8;
    auto item = [&](bool literal) {
        if (bit == 8) {
            flagAt = out.size();
            out.push_back(0);
            bit = 0;
        }
        if (literal)
            out[flagAt] |= static_cast<uint8_t>(1 << bit);
        ++bit;
    };
    size_t p = 0;
    while (p < decoded.size()) {
        size_t length = 0;
        if (p >= distance) {
            while (length < 18 && p + length < decoded.size() && decoded[p + length] == decoded[p + length - distance])
                ++length;
        }
        if (length >= 3) {
            item(false);
            uint32_t position = static_cast<uint32_t>((4096 - 18 + p - distance) & 4095);
            out.push_back(static_cast<uint8_t>(position & 0xFF));
            out.push_back(static_cast<uint8_t>(((position >> 4) & 0xF0) | (length - 3)));
            p += length;
        } else {
            item(true);
            out.push_back(decoded[p]);
            ++p;
        }
    }
    return out;
}

// A script: a line end, `count` statements calling `call`, then a line end and the end of script. With
// `other`, every third statement calls that name instead.
std::vector<uint8_t> MakeScript(uint32_t call, size_t count, uint32_t other = 0)
{
    std::vector<uint8_t> code = {0x01};
    for (size_t i = 0; i < count; ++i) {
        uint32_t name = other && i % 3 == 2 ? other : call;
        code.push_back(0x16);
        for (int b = 0; b < 4; ++b)
            code.push_back(static_cast<uint8_t>(name >> (8 * b)));
        code.push_back(0x01);
    }
    code.push_back(0x24);
    return code;
}

constexpr uint32_t kCFunction = 0x5EEDC0DE;
constexpr uint32_t kPlainName = 0x0000ABCD; // no symbol: left as a name by the pre-processing
constexpr uint32_t kS1 = 0x10000101, kS2 = 0x20000201, kS3 = 0x30000301, kAlias = 0x40000401;
constexpr uint32_t kInteger = 0x50000501, kUnknown = 0x60000601, kFlagged = 0x70000701;
constexpr uint32_t kN1 = 0x80000801, kN2 = 0x90000901;
constexpr uint32_t kEdge[2] = {0xE0000E01, 0xE1000E11};
constexpr int kBigCount = 5, kManyCount = 110;
uint32_t BigName(int i) { return 0xB0000000u + 0x10000u * static_cast<uint32_t>(i) + 0x0B01u; }
uint32_t ManyName(int i) { return 0x9E3779B9u * static_cast<uint32_t>(i + 1); }

struct Scripts {
    StoredScript s1, s1v2, s2, s3, flagged, n1, n2, edge[2];
    StoredScript big[kBigCount];
    StoredScript many[kManyCount];
    bool ok = true;

    void Build()
    {
        auto compressed = [&](StoredScript& script, const std::vector<uint8_t>& decoded) {
            std::vector<uint8_t> stored = Compress(decoded, 18);
            std::vector<uint8_t> check(decoded.size() + 64);
            uint8_t* end = Lzss::Decode(stored.data(), check.data(), static_cast<int>(stored.size()));
            if (static_cast<size_t>(end - check.data()) != decoded.size() ||
                std::memcmp(check.data(), decoded.data(), decoded.size()) != 0 || stored.size() >= decoded.size())
                ok = false;
            script.Set(decoded, stored);
        };
        auto raw = [&](StoredScript& script, const std::vector<uint8_t>& decoded) { script.Set(decoded, decoded); };
        compressed(s1, MakeScript(kCFunction, 12, kPlainName));
        compressed(s1v2, MakeScript(kPlainName, 15, kCFunction));
        raw(s2, MakeScript(kCFunction, 2, kPlainName));
        raw(s3, MakeScript(kPlainName, 3, kCFunction));
        raw(flagged, MakeScript(kCFunction, 1));
        raw(n1, MakeScript(kPlainName, 1));
        compressed(n2, MakeScript(kCFunction, 9, kPlainName));
        for (int i = 0; i < kBigCount; ++i)
            compressed(big[i], MakeScript(i % 2 ? kCFunction : kPlainName, 6600 + 300 * i, kCFunction));
        for (int i = 0; i < kManyCount; ++i) {
            if (i % 4 == 0)
                compressed(many[i], MakeScript(kCFunction, 6 + i % 7, kPlainName));
            else
                raw(many[i], MakeScript(i % 2 ? kCFunction : kPlainName, 1 + i % 3));
        }
    }
};
Scripts g_scripts;

void TestScriptCache()
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    using TestStandIns::AddressOf;
    using TestStandIns::Branch;
    using TestStandIns::Patch;
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestScriptCache skipped (the memory manager is already set up)");
        return;
    }
    SaveGlobals();
    g_scripts.Build();
    Check(g_scripts.ok, "script cache: the test's compressed scripts decode");

    static CSymbolTableEntry* table[0x1000];
    static CSymbolTableEntry symbols[18 + kBigCount + kManyCount];
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;

    auto run = [&](bool original) {
        std::vector<uint32_t> trace;
        for (SavedGlobal& s : g_scache_saved) {
            if (s.address != 0x006F3624)
                std::memset(reinterpret_cast<void*>(s.address), 0, s.size);
        }
        GameVariable<bool>(0x007CC979) = true;

        // The symbol table: a C function, the scripts, a name naming S1, an integer.
        std::memset(table, 0, sizeof(table));
        std::memset(symbols, 0, sizeof(symbols));
        int used = 0;
        auto add = [&](uint32_t name, Script::ESymbolType type, uint32_t raw) {
            CSymbolTableEntry* symbol = &symbols[used++];
            symbol->unknown0 = 1;
            symbol->type = type;
            symbol->name = name;
            symbol->raw = raw;
            symbol->next = table[name & 0xFFF];
            table[name & 0xFFF] = symbol;
            return symbol;
        };
        auto script = [&](uint32_t name, StoredScript& stored) {
            return add(name, Script::ESYMBOLTYPE_QSCRIPT, reinterpret_cast<uint32_t>(stored.block.data()));
        };
        add(kCFunction, Script::ESYMBOLTYPE_CFUNCTION, 0x12345678);
        CSymbolTableEntry* s1 = script(kS1, g_scripts.s1);
        script(kS2, g_scripts.s2);
        script(kS3, g_scripts.s3);
        script(kFlagged, g_scripts.flagged);
        script(kN1, g_scripts.n1);
        script(kN2, g_scripts.n2);
        add(kAlias, Script::ESYMBOLTYPE_NAME, kS1);
        add(kInteger, Script::ESYMBOLTYPE_INTEGER, 7);
        for (int i = 0; i < kBigCount; ++i)
            script(BigName(i), g_scripts.big[i]);
        for (int i = 0; i < kManyCount; ++i)
            script(ManyName(i), g_scripts.many[i]);

        // The stand-in manager: a default heap and a script heap over the arena (dirty memory).
        uint8_t* arena = g_scache_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        std::memset(arena + 0x1000, 0xCD, kArenaSize - 0x1000);
        auto* manager = reinterpret_cast<Manager*>(arena);
        GameVariable<void*>(0x007D1800) = manager;
        auto* region = new (arena + 0x600) Region(arena + 0x1000, arena + 0x10000);
        auto* heap = new (arena + 0x620) Heap(region, 1, "default");
        auto* scriptRegion = new (arena + 0x6C0) Region(arena + 0x10000, arena + kArenaSize);
        auto* scriptHeap = new (arena + 0x6E0) Heap(scriptRegion, 1, "script");
        manager->m_heaps[0] = heap;
        manager->m_heaps[1] = scriptHeap;
        manager->m_heapCount = 2;
        manager->m_scriptHeap = scriptHeap;
        manager->PushContext(heap);
        // Two entry pools: 120 entries (current) and 30.
        auto& pools = GameVariable<Mem::PoolPair>(0x006F3560);
        pools.pools[0] = new (arena + 0x800) Mem::Pool(0x14, 120, "CScriptCacheEntry");
        pools.pools[1] = new (arena + 0x880) Mem::Pool(0x14, 30, "CScriptCacheEntry 2");
        pools.current = 0;
        auto base = reinterpret_cast<uint32_t>(arena);
        auto relocate = [&](uint32_t word) {
            return word >= base && word <= base + kArenaSize ? 0xA0000000u + (word - base) : word;
        };
        auto push = [&](const void* pointer) { trace.push_back(relocate(reinterpret_cast<uint32_t>(pointer))); };

        // Ours reaches our destructor through the cache's vtable (the game's slot 0 is 0x004761E0).
        Patch* destroy = nullptr;
        if (!original)
            destroy = new Patch(0x004761E0,
                                Branch(0xE9, 0x004761E0, reinterpret_cast<void*>(AddressOf(&CScriptCache::Destroy))));

        auto create = [&](bool make) {
            return original ? Original<CScriptCache*(__cdecl*)(bool)>(0x00476040)(make)
                            : Script::CreateScriptCache(make);
        };
        auto release = [&]() {
            if (original)
                Original<void(__cdecl*)()>(0x00475A00)();
            else
                Script::ReleaseScriptCache();
        };
        CScriptCache* cache = nullptr;
        auto get = [&](uint32_t name) {
            uint8_t* code = original ? Original<uint8_t*(__thiscall*)(CScriptCache*, uint32_t)>(0x00475E80)(cache, name)
                                     : cache->GetScript(name);
            push(code);
            return code;
        };
        auto drop = [&](uint32_t name) {
            if (original)
                Original<void(__thiscall*)(CScriptCache*, uint32_t)>(0x00475D60)(cache, name);
            else
                cache->ReleaseScript(name);
        };
        auto invalidate = [&](uint32_t name) {
            if (original)
                Original<void(__thiscall*)(CScriptCache*, uint32_t)>(0x00475FD0)(cache, name);
            else
                cache->Invalidate(name);
        };
        auto lookUp = [&](uint32_t key) {
            void* value =
                original
                    ? Original<void*(__thiscall*)(Lst::HashTable*, const uint32_t*, bool)>(0x00462050)(cache->m_entries,
                                                                                                         &key, true)
                    : cache->m_entries->GetItem(key, true);
            push(value);
        };
        // The cache's state: the unused list both ways, the references.
        auto state = [&]() {
            for (CScriptCacheEntry* e = cache->m_newestUnused; e; e = e->older)
                trace.push_back(e->name);
            trace.push_back(0xEEEE);
            for (CScriptCacheEntry* e = cache->m_oldestUnused; e; e = e->newer)
                trace.push_back(e->name);
            trace.push_back(0xFFFF);
            trace.push_back(GameVariable<uint32_t>(0x006F355C));
        };

        // References without and with a cache.
        push(create(false));
        trace.push_back(GameVariable<uint32_t>(0x006F355C));
        release();
        trace.push_back(GameVariable<uint32_t>(0x006F355C));
        cache = create(true);
        push(cache);
        push(create(true));
        release();
        trace.push_back(GameVariable<uint32_t>(0x006F355C));
        push(GameVariable<void*>(0x006F3558));

        // Uses: compressed and stored scripts, the same one through a name, things that aren't scripts.
        get(kS1);
        get(kS1);
        get(kAlias);
        get(kInteger);
        get(kUnknown);
        get(kCFunction);
        get(kS2);
        get(kS3);
        lookUp(kS1);
        lookUp(kAlias);
        lookUp(kUnknown);
        state();
        // Releases: S1 three times (then unused), S2, S3, a name not cached.
        drop(kS1);
        drop(kS1);
        state();
        drop(kS1);
        drop(kS2);
        drop(kS3);
        drop(kUnknown);
        state();
        // Taken back from the middle of the list, and the ends.
        get(kS2);
        state();
        get(kS3);
        get(kS1);
        state();
        drop(kS1);
        drop(kS3);
        drop(kS2);
        get(kS1);
        state();
        drop(kS1);
        state();
        // A new version of S1 (unused, at the old end), S2 in use, a name not cached.
        s1->raw = reinterpret_cast<uint32_t>(g_scripts.s1v2.block.data());
        get(kS2);
        invalidate(kS1);
        state();
        invalidate(kS2);
        invalidate(kUnknown);
        lookUp(kS1);
        lookUp(kS2);
        state();
        drop(kS2);
        get(kS1); // (no longer on the unused list: count 0 -> 1)
        drop(kS1);
        state();

        // Big scripts: the oldest unused ones are dropped until there's room in the script heap.
        for (int i = 0; i < kBigCount; ++i) {
            get(BigName(i));
            state();
            drop(BigName(i));
        }
        get(BigName(0));
        get(BigName(3));
        state();
        drop(BigName(3));
        drop(BigName(0));
        state();
        trace.push_back(static_cast<uint32_t>(pools.pools[0]->m_used));
        // Scripts sized to the margin: the largest free block less 128 bytes still fits; one byte more drops the
        // oldest unused entry. (Sized from the heap as it is - the same in both runs.)
        for (int i = 0; i < 2; ++i) {
            uint32_t size = scriptHeap->LargestFree() - 0x80 + static_cast<uint32_t>(i);
            std::vector<uint8_t> code = MakeScript(kCFunction, 1);
            code.resize(size, 0x24);
            g_scripts.edge[i].Set(code, code);
            script(kEdge[i], g_scripts.edge[i]);
            get(kEdge[i]);
            state();
            drop(kEdge[i]);
        }

        // Many small scripts: at 100 entries in use in the pool, the oldest unused ones go.
        for (int i = 0; i < kManyCount; ++i) {
            get(ManyName(i));
            if (i % 5 != 4)
                drop(ManyName(i));
            trace.push_back(static_cast<uint32_t>(pools.pools[0]->m_used));
        }
        state();
        for (int i = 0; i < kManyCount; i += 5)
            get(ManyName(i));
        state();

        // The other pool current: new entries from it; the old ones are still freed into theirs.
        pools.current = 1;
        get(kN1);
        get(kN2);
        drop(ManyName(0));
        drop(ManyName(1));
        drop(kN1);
        state();
        get(kN1);
        trace.push_back(static_cast<uint32_t>(pools.pools[1]->m_used));

        // Unused entries freed at once.
        cache->m_deleteUnused = true;
        get(kFlagged);
        get(kFlagged);
        drop(kFlagged);
        lookUp(kFlagged);
        drop(kFlagged);
        lookUp(kFlagged);
        drop(kN2);
        state();

        // An entry with no current pool comes from the default heap (and goes back to it).
        Mem::Pool* saved = pools.pools[1];
        pools.pools[1] = nullptr;
        void* loose = original ? Original<void*(__cdecl*)(size_t)>(0x00475AC0)(0x14)
                               : CScriptCacheEntry::operator new(0x14);
        push(loose);
        if (original)
            Original<void(__cdecl*)(void*)>(0x00475B00)(loose);
        else
            CScriptCacheEntry::operator delete(loose);
        pools.pools[1] = saved;
        // An entry from the current pool, freed.
        void* pooled = original ? Original<void*(__cdecl*)(size_t)>(0x00475AC0)(0x14)
                                : CScriptCacheEntry::operator new(0x14);
        push(pooled);
        if (original)
            Original<void(__cdecl*)(void*)>(0x00475B00)(pooled);
        else
            CScriptCacheEntry::operator delete(pooled);

        // The last reference: the cache and every entry freed.
        release();
        push(GameVariable<void*>(0x006F3558));
        trace.push_back(GameVariable<uint32_t>(0x006F355C));
        trace.push_back(static_cast<uint32_t>(pools.pools[0]->m_used));
        trace.push_back(static_cast<uint32_t>(pools.pools[1]->m_used));
        delete destroy;

        // The whole arena, pointers made arena-relative (and stale half-pointers in 16-bit fields).
        std::vector<uint32_t> image(kArenaSize / 4);
        std::memcpy(image.data(), arena, kArenaSize);
        for (uint32_t& word : image) {
            word = relocate(word);
            uint32_t high = word >> 16;
            if (high >= (base >> 16) && high <= ((base + kArenaSize) >> 16))
                word = (word & 0xFFFF) | ((0xA000u + high - (base >> 16)) << 16);
        }
        trace.insert(trace.end(), image.begin(), image.end());
        GameVariable<void*>(0x007D1800) = nullptr;
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    RestoreGlobals();
    Check(expected == actual,
          "script cache: create/release, uses and releases (compressed, stored, through a name, not scripts), the "
          "unused list, invalidation, dropping for heap room and for the pool count, both pools, free-at-once, "
          "the table lookup 0x462050, entry new/delete, destruction");
    if (expected.size() != actual.size())
        Log("  script cache trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 8; ++i) {
        if (expected[i] != actual[i]) {
            Log("  script cache entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

} // namespace

SELFTEST(Early, TestScriptCache, RestoreGlobals);
