// NOT ORIGINAL - the script pools' sizes (engine/core/memory_sizes.h). Pool sizes as THUGPRO's.

#include "engine/core/memory_sizes.h"

#include <algorithm>

#include "platform/run_log.h"
#include "engine/core/ints.h"

namespace memory::sizes {

namespace {

constexpr Pools kOriginal = {.script_cache_entries_ = 300, .components = 86000, .structs = 19800, .vectors = 9500, .pairs = 800, .arrays = 7000, .symbols = 12500, .scripts = 300, .stored_randoms_ = 100, .string_bytes_ = 190000, .strings = 7000, .hash_items_ = 10000};
// THUGPRO's shipped thugpro.dll: the CreateScriptPools pushes 0x0046F559... 0x0046F667 and Main's HashItem push
// 0x004E211B (CStoredRandom untouched).
constexpr Pools kThugpro = {.script_cache_entries_ = 1200, .components = 184000, .structs = 49600, .vectors = 19000, .pairs = 1600, .arrays = 16000, .symbols = 45000, .scripts = 1200, .stored_randoms_ = 100, .string_bytes_ = 0x100000, .strings = 28000, .hash_items_ = 40000};
// Ours: THUGPRO's, the CComponent / CStruct / CVector / CArray / HashItem pools bigger - THP8's whole town
// (thug2-thp8-level-converter/thp8_levels, ~34,000 nodes) took 284,043 / 43,362 / 37,294 / 19,691 / 39,923 at its peak (THUGPRO's: 184,000 /
// 49,600 / 19,000 / 16,000 / 40,000); a quarter or more as headroom. They cost ~3.7 MB more of the main block.
constexpr Pools kDefaults = {.script_cache_entries_ = 1200, .components = 360000, .structs = 64000, .vectors = 48000, .pairs = 1600, .arrays = 26000, .symbols = 45000, .scripts = 1200, .stored_randoms_ = 100, .string_bytes_ = 0x100000, .strings = 28000, .hash_items_ = 56000};

constexpr uint32_t kOriginalScriptRegion = 0x434770;
constexpr uint32_t kOriginalMainBlock = 0x4000000; // 64 MB
constexpr int kMostItems = 4000000;                // (an ini value above this is taken as this)

bool g_use_original = false;

int total_of(const char* key, int fallback) { return std::clamp(config_int("memory", key, fallback), 1, kMostItems); }

Pools get_take_in()
{
    if (config_int("memory", "original_sizes", 0) != 0)
        return kOriginal;
    Pools p = kDefaults;
    p.script_cache_entries_ = total_of("script_cache_pool", p.script_cache_entries_);
    p.components = total_of("component_pool", p.components);
    p.structs = total_of("struct_pool", p.structs);
    p.vectors = total_of("vector_pool", p.vectors);
    p.pairs = total_of("pair_pool", p.pairs);
    p.arrays = total_of("array_pool", p.arrays);
    p.symbols = total_of("symbol_pool", p.symbols);
    p.scripts = total_of("script_pool", p.scripts);
    p.string_bytes_ = static_cast<uint32_t>(
        std::clamp(config_int("memory", "string_heap", static_cast<int>(p.string_bytes_)), 0x1000, 0x4000000));
    p.strings = static_cast<uint32_t>(total_of("string_count", static_cast<int>(p.strings)));
    p.hash_items_ = total_of("hash_item_pool", p.hash_items_);
    return p;
}

uint32_t get_grown(uint32_t ours, uint32_t original) { return ours > original ? ours - original : 0; }

uint32_t hash_item_bytes(const Pools& p) { return static_cast<uint32_t>(p.hash_items_) * 12; }

} // namespace

const Pools& get_original() { return kOriginal; }
const Pools& get_defaults() { return kDefaults; }
const Pools& get_thugpro() { return kThugpro; }

const Pools& at_hand()
{
    if (g_use_original)
        return kOriginal;
    static const Pools kCurrent = get_take_in();
    return kCurrent;
}

uint32_t script_pool_bytes(const Pools& p)
{
    return static_cast<uint32_t>(p.script_cache_entries_) * 0x14 + static_cast<uint32_t>(p.components) * 0x10 +
           static_cast<uint32_t>(p.structs) * 8 + static_cast<uint32_t>(p.vectors) * 0x10 +
           static_cast<uint32_t>(p.pairs) * 0xC + static_cast<uint32_t>(p.arrays) * 0xC +
           static_cast<uint32_t>(p.symbols) * 0x14 + static_cast<uint32_t>(p.scripts) * 0xF4 +
           static_cast<uint32_t>(p.stored_randoms_) * 0x60 + p.string_bytes_ + p.strings * 8;
}

uint32_t script_region_size()
{
    uint32_t extra = get_grown(script_pool_bytes(at_hand()), script_pool_bytes(kOriginal));
    if (extra)
        extra += 0x1000; // (the heap's block headers and alignment for the eleven allocations)
    return kOriginalScriptRegion + ((extra + 15) & ~15u) + script_heap_extra();
}

uint32_t script_heap_extra()
{
    if (g_use_original)
        return 0;
    static const uint32_t kExtra = [] {
        if (config_int("memory", "original_sizes", 0) != 0)
            return 0u;
        const int mb = std::clamp(config_int("memory", "script_heap_extra_mb", kScriptHeapExtraMb), 0, 256);
        return static_cast<uint32_t>(mb) << 20;
    }();
    return kExtra;
}

uint32_t main_block_size()
{
    const uint32_t extra = script_region_size() - kOriginalScriptRegion +
                           get_grown(hash_item_bytes(at_hand()), hash_item_bytes(kOriginal));
    return kOriginalMainBlock + ((extra + 0xFFFF) & ~0xFFFFu);
}

uint32_t node_hash_slots()
{
    if (g_use_original)
        return kOriginalNodeHashSlots;
    static const uint32_t kSlots = [] {
        if (config_int("memory", "original_sizes", 0) != 0)
            return kOriginalNodeHashSlots;
        const int asked = std::clamp(config_int("memory", "node_hash_slots", 0x10000), 0x4000, 0x100000);
        uint32_t n = kOriginalNodeHashSlots;
        while (n < ints::as_unsigned(asked))
            n <<= 1;
        return n;
    }();
    return kSlots;
}

UseOriginal::UseOriginal() : was_(g_use_original) { g_use_original = true; }
UseOriginal::~UseOriginal() { g_use_original = was_; }

} // namespace memory::sizes
