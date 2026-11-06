// NOT ORIGINAL - the script pools' sizes (engine/memory/memory_sizes.h). Pool sizes as THUGPRO's.

#include "engine/memory/memory_sizes.h"

#include <algorithm>
#include <cstdint>

#include "engine/lists/key_map.h"
#include "engine/script/cache.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "platform/run_log.h"
#include "base/core/ints.h"

namespace memory::sizes {

namespace {
constexpr int kMinStringHeapBytes = 0x1000;
constexpr int kMaxStringHeapBytes = 0x4000000;
// One item's size in each of the script runtime's pools (and the string table's entry, the HashItem pool's item).
struct ItemBytes {
    uint32_t script_cache_entry;
    uint32_t component;
    uint32_t structure;
    uint32_t vector;
    uint32_t pair;
    uint32_t array;
    uint32_t symbol;
    uint32_t script;
    uint32_t stored_random;
    uint32_t string_entry;
    uint32_t hash_item;
};
// The original's (x86): what its script region was sized for.
constexpr ItemBytes kOriginalItems = {.script_cache_entry = 0x14, .component = 0x10, .structure = 8, .vector = 0x10, .pair = 0xC, .array = 0xC,
                                      .symbol = 0x14, .script = 0xF4, .stored_random = 0x60, .string_entry = 8, .hash_item = 12};
// This build's: the classes' sizes (the original's on x86; bigger on x64, where the cells and the pointers are 64 bits).
constexpr ItemBytes kItems = {.script_cache_entry = sizeof(script::ScriptCacheEntry), .component = sizeof(script::Component),
                              .structure = sizeof(script::Struct), .vector = sizeof(script::Vector), .pair = sizeof(script::Pair),
                              .array = sizeof(script::Array), .symbol = sizeof(script::SymbolTableEntry), .script = sizeof(script::Script),
                              .stored_random = sizeof(script::StoredRandom), .string_entry = sizeof(script::PermanentString),
                              .hash_item = sizeof(lists::KeyMap::Item)};
constexpr bool same_items(const ItemBytes& a, const ItemBytes& b)
{
    return a.script_cache_entry == b.script_cache_entry && a.component == b.component && a.structure == b.structure && a.vector == b.vector &&
           a.pair == b.pair && a.array == b.array && a.symbol == b.symbol && a.script == b.script && a.stored_random == b.stored_random &&
           a.string_entry == b.string_entry && a.hash_item == b.hash_item;
}
static_assert(sizeof(void*) != sizeof(uint32_t) || same_items(kItems, kOriginalItems), "x86: the original's item sizes");
constexpr uint32_t kScriptHeapOverhead = 0x1000; // the heap's block headers and alignment for the eleven allocations
constexpr int kAlign16 = 15;                      // rounding up to 16 bytes
constexpr unsigned kAlign16MaskUnsigned = 15U;
constexpr int kMegabyteShift = 20;
constexpr int kBlockGranuleMask = 0xFFFF;         // the main block is a whole number of 64 KB
constexpr unsigned kBlockGranuleMaskUnsigned = 0xFFFFU;

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

bool& use_original()
{
    static bool use_original{false};
    return use_original;
}

int total_of(const char* key, int fallback) { return std::clamp(config_int("memory", key, fallback), 1, kMostItems); }

Pools get_take_in()
{
    if (config_int("memory", "original_sizes", 0) != 0) {
        return kOriginal;
    }
    Pools pools = kDefaults;
    pools.script_cache_entries_ = total_of("script_cache_pool", pools.script_cache_entries_);
    pools.components = total_of("component_pool", pools.components);
    pools.structs = total_of("struct_pool", pools.structs);
    pools.vectors = total_of("vector_pool", pools.vectors);
    pools.pairs = total_of("pair_pool", pools.pairs);
    pools.arrays = total_of("array_pool", pools.arrays);
    pools.symbols = total_of("symbol_pool", pools.symbols);
    pools.scripts = total_of("script_pool", pools.scripts);
    pools.string_bytes_ = static_cast<uint32_t>(
        std::clamp(config_int("memory", "string_heap", static_cast<int>(pools.string_bytes_)), kMinStringHeapBytes, kMaxStringHeapBytes));
    pools.strings = static_cast<uint32_t>(total_of("string_count", static_cast<int>(pools.strings)));
    pools.hash_items_ = total_of("hash_item_pool", pools.hash_items_);
    return pools;
}

uint32_t get_grown(uint32_t ours, uint32_t original) { return ours > original ? ours - original : 0; }

uint32_t hash_item_bytes(const Pools& pools, const ItemBytes& items) { return static_cast<uint32_t>(pools.hash_items_) * items.hash_item; }

uint32_t pool_bytes(const Pools& pools, const ItemBytes& items)
{
    return (static_cast<uint32_t>(pools.script_cache_entries_) * items.script_cache_entry) +
           (static_cast<uint32_t>(pools.components) * items.component) + (static_cast<uint32_t>(pools.structs) * items.structure) +
           (static_cast<uint32_t>(pools.vectors) * items.vector) + (static_cast<uint32_t>(pools.pairs) * items.pair) +
           (static_cast<uint32_t>(pools.arrays) * items.array) + (static_cast<uint32_t>(pools.symbols) * items.symbol) +
           (static_cast<uint32_t>(pools.scripts) * items.script) + (static_cast<uint32_t>(pools.stored_randoms_) * items.stored_random) +
           pools.string_bytes_ + (pools.strings * items.string_entry);
}

} // namespace

const Pools& get_original() { return kOriginal; }
const Pools& get_defaults() { return kDefaults; }
const Pools& get_thugpro() { return kThugpro; }

const Pools& at_hand()
{
    if (use_original()) {
        return kOriginal;
    }
    static const Pools kCurrent = get_take_in();
    return kCurrent;
}

uint32_t script_pool_bytes(const Pools& pools) { return pool_bytes(pools, kItems); }

uint32_t script_region_size()
{
    // (the original's region held the original's pools at the original's item sizes)
    uint32_t extra = get_grown(script_pool_bytes(at_hand()), pool_bytes(kOriginal, kOriginalItems));
    if (extra != 0U) {
        extra += kScriptHeapOverhead; // (the heap's block headers and alignment for the eleven allocations)
    }
    // (x64: the original's room beside its pools - for scripts, strings, node prefixes - doubled, as every region's)
    const uint32_t beside_pools = kOriginalScriptRegion - pool_bytes(kOriginal, kOriginalItems);
    extra += region_bytes(beside_pools) - beside_pools;
    return kOriginalScriptRegion + ((extra + kAlign16) & ~kAlign16MaskUnsigned) + script_heap_extra();
}

uint32_t script_heap_extra()
{
    if (use_original()) {
        return 0;
    }
    static const uint32_t kExtra = [] {
        if (config_int("memory", "original_sizes", 0) != 0) {
            return 0U;
        }
        const int mb = std::clamp(config_int("memory", "script_heap_extra_mb", kScriptHeapExtraMb), 0, 256);
        return static_cast<uint32_t>(mb) << kMegabyteShift;
    }();
    return kExtra;
}

uint32_t main_block_size()
{
    // (x64: what the original's block held beside the script region doubled, as every region's)
    constexpr uint32_t kBesideScriptRegion = kOriginalMainBlock - kOriginalScriptRegion;
    const uint32_t extra = script_region_size() - kOriginalScriptRegion +
                           get_grown(hash_item_bytes(at_hand(), kItems), hash_item_bytes(kOriginal, kOriginalItems)) +
                           (region_bytes(kBesideScriptRegion) - kBesideScriptRegion);
    return kOriginalMainBlock + ((extra + kBlockGranuleMask) & ~kBlockGranuleMaskUnsigned);
}

uint32_t node_hash_slots()
{
    if (use_original()) {
        return kOriginalNodeHashSlots;
    }
    static const uint32_t kSlots = [] {
        if (config_int("memory", "original_sizes", 0) != 0) {
            return kOriginalNodeHashSlots;
        }
        const int asked = std::clamp(config_int("memory", "node_hash_slots", 0x10000), 0x4000, 0x100000);
        uint32_t slots = kOriginalNodeHashSlots;
        while (slots < ints::as_unsigned(asked)) {
            slots <<= 1;
        }
        return slots;
    }();
    return kSlots;
}

UseOriginal::UseOriginal() : was_(use_original()) { use_original() = true; }
UseOriginal::~UseOriginal() { use_original() = was_; }

} // namespace memory::sizes
