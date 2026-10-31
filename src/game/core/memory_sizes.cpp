// NOT ORIGINAL - the script pools' sizes (game/core/memory_sizes.h). Pool sizes as THUGPRO's.

#include "game/core/memory_sizes.h"

#include <algorithm>

#include "game/sys/log.h"

namespace Mem::Sizes {

namespace {

constexpr Pools kOriginal = {300, 86000, 19800, 9500, 800, 7000, 12500, 300, 100, 190000, 7000, 10000};
// THUGPRO's shipped thugpro.dll: the CreateScriptPools pushes 0x0046F559... 0x0046F667 and Main's HashItem push
// 0x004E211B (CStoredRandom untouched).
constexpr Pools kThugpro = {1200, 184000, 49600, 19000, 1600, 16000, 45000, 1200, 100, 0x100000, 28000, 40000};
// Ours: THUGPRO's, the CComponent / CStruct / CVector / CArray / HashItem pools bigger - THP8's whole town
// (tools/pipeline/thp8_levels, ~34,000 nodes) took 284,043 / 43,362 / 37,294 / 19,691 / 39,923 at its peak (THUGPRO's: 184,000 /
// 49,600 / 19,000 / 16,000 / 40,000); a quarter or more as headroom. They cost ~3.7 MB more of the main block.
constexpr Pools kDefaults = {1200, 360000, 64000, 48000, 1600, 26000, 45000, 1200, 100, 0x100000, 28000, 56000};

constexpr uint32_t kOriginalScriptRegion = 0x434770;
constexpr uint32_t kOriginalMainBlock = 0x4000000; // 64 MB
constexpr int kMostItems = 4000000;                 // (an ini value above this is taken as this)

bool g_useOriginal = false;

int Count(const char* key, int fallback)
{
    return std::clamp(ConfigInt("memory", key, fallback), 1, kMostItems);
}

Pools Read()
{
    if (ConfigInt("memory", "original_sizes", 0) != 0)
        return kOriginal;
    Pools p = kDefaults;
    p.scriptCacheEntries = Count("script_cache_pool", p.scriptCacheEntries);
    p.components = Count("component_pool", p.components);
    p.structs = Count("struct_pool", p.structs);
    p.vectors = Count("vector_pool", p.vectors);
    p.pairs = Count("pair_pool", p.pairs);
    p.arrays = Count("array_pool", p.arrays);
    p.symbols = Count("symbol_pool", p.symbols);
    p.scripts = Count("script_pool", p.scripts);
    p.stringBytes = static_cast<uint32_t>(std::clamp(ConfigInt("memory", "string_heap", static_cast<int>(p.stringBytes)),
                                                     0x1000, 0x4000000));
    p.strings = static_cast<uint32_t>(Count("string_count", static_cast<int>(p.strings)));
    p.hashItems = Count("hash_item_pool", p.hashItems);
    return p;
}

uint32_t Grown(uint32_t ours, uint32_t original) { return ours > original ? ours - original : 0; }

uint32_t HashItemBytes(const Pools& p) { return static_cast<uint32_t>(p.hashItems) * 12; }

} // namespace

const Pools& Original() { return kOriginal; }
const Pools& Defaults() { return kDefaults; }
const Pools& Thugpro() { return kThugpro; }

const Pools& Current()
{
    if (g_useOriginal)
        return kOriginal;
    static const Pools current = Read();
    return current;
}

uint32_t ScriptPoolBytes(const Pools& p)
{
    return static_cast<uint32_t>(p.scriptCacheEntries) * 0x14 + static_cast<uint32_t>(p.components) * 0x10 +
           static_cast<uint32_t>(p.structs) * 8 + static_cast<uint32_t>(p.vectors) * 0x10 +
           static_cast<uint32_t>(p.pairs) * 0xC + static_cast<uint32_t>(p.arrays) * 0xC +
           static_cast<uint32_t>(p.symbols) * 0x14 + static_cast<uint32_t>(p.scripts) * 0xF4 +
           static_cast<uint32_t>(p.storedRandoms) * 0x60 + p.stringBytes + p.strings * 8;
}

uint32_t ScriptRegionSize()
{
    uint32_t extra = Grown(ScriptPoolBytes(Current()), ScriptPoolBytes(kOriginal));
    if (extra)
        extra += 0x1000; // (the heap's block headers and alignment for the eleven allocations)
    return kOriginalScriptRegion + ((extra + 15) & ~15u) + ScriptHeapExtra();
}

uint32_t ScriptHeapExtra()
{
    if (g_useOriginal)
        return 0;
    static const uint32_t extra = [] {
        if (ConfigInt("memory", "original_sizes", 0) != 0)
            return 0u;
        int mb = std::clamp(ConfigInt("memory", "script_heap_extra_mb", kScriptHeapExtraMb), 0, 256);
        return static_cast<uint32_t>(mb) << 20;
    }();
    return extra;
}

uint32_t MainBlockSize()
{
    uint32_t extra = ScriptRegionSize() - kOriginalScriptRegion + Grown(HashItemBytes(Current()), HashItemBytes(kOriginal));
    return kOriginalMainBlock + ((extra + 0xFFFF) & ~0xFFFFu);
}

uint32_t NodeHashSlots()
{
    if (g_useOriginal)
        return kOriginalNodeHashSlots;
    static const uint32_t slots = [] {
        if (ConfigInt("memory", "original_sizes", 0) != 0)
            return kOriginalNodeHashSlots;
        int asked = std::clamp(ConfigInt("memory", "node_hash_slots", 0x10000), 0x4000, 0x100000);
        uint32_t n = kOriginalNodeHashSlots;
        while (n < static_cast<uint32_t>(asked))
            n <<= 1;
        return n;
    }();
    return slots;
}

UseOriginal::UseOriginal() : m_was(g_useOriginal) { g_useOriginal = true; }
UseOriginal::~UseOriginal() { g_useOriginal = m_was; }

} // namespace Mem::Sizes
