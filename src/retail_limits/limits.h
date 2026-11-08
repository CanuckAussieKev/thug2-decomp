#pragma once

// NOT ORIGINAL - the limits the retail-limits DLL (src/retail_limits, docs/retail-limits.md) raises in the original
// game, as named constants: the original's value and ours side by side. Tools that check a level against the original
// game's limits (a budget checker with a "the limits DLL is installed" option) mirror this file.
//
// The raised values are this project's engine's own (src/engine/memory/memory_sizes.cpp: THUGPRO's pool sizes, the
// CComponent / CStruct / CVector / CArray / HashItem pools bigger still - THP8's whole town needed them), so a level
// that loads in thug2-decomp.exe fits in the original game with the DLL.

#include <cstdint>

namespace retail_limits {

// --- The script runtime's pools (made once at start-up) ---------------------------------------------------------

// How many items each pool holds; the permanent string table's bytes and entries; the HashItem pool (the hash
// tables' items).
struct PoolCounts {
    uint32_t script_cache_entries;
    uint32_t components;
    uint32_t structs;
    uint32_t vectors;
    uint32_t pairs;
    uint32_t arrays;
    uint32_t symbols;
    uint32_t scripts;
    uint32_t stored_randoms;
    uint32_t string_bytes;
    uint32_t strings;
    uint32_t hash_items;
};

// The original game's.
constexpr PoolCounts kOriginalPools = {.script_cache_entries = 300,
                                       .components = 86000,
                                       .structs = 19800,
                                       .vectors = 9500,
                                       .pairs = 800,
                                       .arrays = 7000,
                                       .symbols = 12500,
                                       .scripts = 300,
                                       .stored_randoms = 100,
                                       .string_bytes = 190000,
                                       .strings = 7000,
                                       .hash_items = 10000};

// With the DLL (CStoredRandom is left as it is).
constexpr PoolCounts kRaisedPools = {.script_cache_entries = 1200,
                                     .components = 360000,
                                     .structs = 64000,
                                     .vectors = 48000,
                                     .pairs = 1600,
                                     .arrays = 26000,
                                     .symbols = 45000,
                                     .scripts = 1200,
                                     .stored_randoms = 100,
                                     .string_bytes = 0x100000,
                                     .strings = 28000,
                                     .hash_items = 56000};

// One item's bytes in each pool (the 32-bit game's).
constexpr PoolCounts kItemBytes = {.script_cache_entries = 20,
                                   .components = 16,
                                   .structs = 8,
                                   .vectors = 16,
                                   .pairs = 12,
                                   .arrays = 12,
                                   .symbols = 20,
                                   .scripts = 244,
                                   .stored_randoms = 96,
                                   .string_bytes = 1,
                                   .strings = 8,
                                   .hash_items = 12};

// The bytes the script pools and the string table take out of the script heap (the HashItem pool is elsewhere).
constexpr uint32_t script_pool_bytes(const PoolCounts& pools)
{
    return (pools.script_cache_entries * kItemBytes.script_cache_entries) + (pools.components * kItemBytes.components) +
           (pools.structs * kItemBytes.structs) + (pools.vectors * kItemBytes.vectors) + (pools.pairs * kItemBytes.pairs) +
           (pools.arrays * kItemBytes.arrays) + (pools.symbols * kItemBytes.symbols) + (pools.scripts * kItemBytes.scripts) +
           (pools.stored_randoms * kItemBytes.stored_randoms) + (pools.string_bytes * kItemBytes.string_bytes) +
           (pools.strings * kItemBytes.strings);
}
constexpr uint32_t kOriginalScriptPoolBytes = 2364800;
static_assert(script_pool_bytes(kOriginalPools) == kOriginalScriptPoolBytes);

// --- The heaps ----------------------------------------------------------------------------------------------------

// The script region (the script heap: the pools above, the scripts, the node name prefixes...).
constexpr uint32_t kOriginalScriptRegionBytes = 0x434770;
// The heap's block headers and alignment for the bigger pools' allocations.
constexpr uint32_t kPoolHeapOverheadBytes = 0x1000;
// More script heap beside the pools: a big level's node name prefixes ran the original's ~2 MB dry.
constexpr uint32_t kScriptHeapExtraBytes = 16U << 20U;
// The main block every other heap is cut from (64 MB).
constexpr uint32_t kOriginalMainBlockBytes = 0x4000000;
// The main block is a whole number of 64 KB.
constexpr uint32_t kMainBlockGranule = 0x10000;

// --- The level's name tables ------------------------------------------------------------------------------------

// The node name hash (every named node of the level's node arrays): slots, open addressing. The original's has 16-bit
// node indexes (a node array of at most 32,767 nodes) and loops for ever adding a node to a full table - a level of
// more than 0x4000 named nodes hangs its load. Ours: 32-bit indexes; a node past a full table is left out and logged.
constexpr uint32_t kOriginalNodeHashSlots = 0x4000;
constexpr uint32_t kNodeHashSlots = 0x10000;
constexpr uint32_t kOriginalMostNodesPerArray = 0x7FFF;

// The rail manager's name table (made while a level's rails are set up, freed after): 16-bit slots. Past 0x4000 rail
// and climbing nodes the original's table is full and its load loops for ever. Ours has 0x10000 slots; its indexes
// stay 16 bits (read sign-extended), so a level may have at most 32,767 rail nodes.
constexpr uint32_t kOriginalRailSlots = 0x4000;
constexpr uint32_t kRailSlots = 0x10000;
constexpr uint32_t kOriginalMostRailNodes = 0x4000;
constexpr uint32_t kMostRailNodes = 0x7FFF;
constexpr uint32_t kRailSlotBytes = 2;

// --- What the DLL raises (each can be switched off in its ini) ---------------------------------------------------

struct Raises {
    bool script_pools = true; // the script pools and the string table (their counts above)
    bool hash_items = true;   // the HashItem pool
    bool script_heap = true;  // kScriptHeapExtraBytes more script heap
    bool node_hash = true;    // the node name hash
    bool rail_table = true;   // the rail name table
};

constexpr uint32_t grown(uint32_t ours, uint32_t original) { return ours > original ? ours - original : 0; }

// The script region with these raises.
constexpr uint32_t script_region_bytes(const Raises& raises)
{
    uint32_t bytes = kOriginalScriptRegionBytes;
    if (raises.script_pools) {
        bytes += grown(script_pool_bytes(kRaisedPools), script_pool_bytes(kOriginalPools)) + kPoolHeapOverheadBytes;
    }
    if (raises.script_heap) {
        bytes += kScriptHeapExtraBytes;
    }
    return bytes;
}

// The main block with these raises: the original's, plus exactly what the raises take out of it (the script region's
// growth, the HashItem pool's, the bigger rail table's while it lives), rounded up to 64 KB - every other heap keeps
// the room it had.
constexpr uint32_t main_block_bytes(const Raises& raises)
{
    uint32_t bytes = kOriginalMainBlockBytes + (script_region_bytes(raises) - kOriginalScriptRegionBytes);
    if (raises.hash_items) {
        bytes += grown(kRaisedPools.hash_items, kOriginalPools.hash_items) * kItemBytes.hash_items;
    }
    if (raises.rail_table) {
        bytes += (kRailSlots - kOriginalRailSlots) * kRailSlotBytes;
    }
    return (bytes + kMainBlockGranule - 1) & ~(kMainBlockGranule - 1);
}

// With every raise: a script region of 28,695,888 bytes, a main block of 92,078,080 (87.8 MB).
constexpr uint32_t kRaisedScriptRegionBytes = 28695888;
constexpr uint32_t kRaisedMainBlockBytes = 92078080;
static_assert(script_region_bytes(Raises{}) == kRaisedScriptRegionBytes);
static_assert(main_block_bytes(Raises{}) == kRaisedMainBlockBytes);
static_assert(main_block_bytes(Raises{.script_pools = false, .hash_items = false, .script_heap = false, .node_hash = false, .rail_table = false}) ==
              kOriginalMainBlockBytes);

} // namespace retail_limits
