// NOT ORIGINAL - the script pools' sizes (game/core/memory_sizes.h). Pool sizes as THUGPRO's.

#include "game/core/memory_sizes.h"

#include <algorithm>

#include "hook/log.h"

namespace Mem::Sizes {

namespace {

constexpr Pools kOriginal = {300, 86000, 19800, 9500, 800, 7000, 12500, 300, 100, 190000, 7000, 10000};
// THUGPRO's shipped thugpro.dll: the CreateScriptPools pushes 0x0046F559... 0x0046F667 and Main's HashItem push
// 0x004E211B (CStoredRandom untouched).
constexpr Pools kThugpro = {1200, 184000, 49600, 19000, 1600, 16000, 45000, 1200, 100, 0x100000, 28000, 40000};

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
    Pools p = kThugpro;
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
const Pools& Defaults() { return kThugpro; }

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
    return kOriginalScriptRegion + ((extra + 15) & ~15u);
}

uint32_t MainBlockSize()
{
    uint32_t extra = ScriptRegionSize() - kOriginalScriptRegion + Grown(HashItemBytes(Current()), HashItemBytes(kOriginal));
    return kOriginalMainBlock + ((extra + 0xFFFF) & ~0xFFFFu);
}

UseOriginal::UseOriginal() : m_was(g_useOriginal) { g_useOriginal = true; }
UseOriginal::~UseOriginal() { g_useOriginal = m_was; }

} // namespace Mem::Sizes
