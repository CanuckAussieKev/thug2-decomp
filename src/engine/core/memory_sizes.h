#pragma once

#include <cstdint>

// NOT ORIGINAL - the sizes of the script pools and of the memory they come from, ours (pool sizes as THUGPRO's: the
// idea and the numbers from THUGPRO's shipped thugpro.dll, which writes them over THUG2's CreateScriptPools pushes at
// start-up; Morten1337's thugpro_proxy, Code/SkPro/Miscellaneous.cpp, has an older version of the same patch).
//
// The original's script component pool (86,000 items) ran dry loading converted levels (THAW's Kyoto);
// the parser writes into the null item it gets. Each count can be set in
// thug2-decomp.ini [memory] (component_pool=, struct_pool=, vector_pool=, pair_pool=, array_pool=, symbol_pool=,
// script_pool=, script_cache_pool=, string_heap=, string_count=, hash_item_pool=); original_sizes=1 puts them all
// back to the original's. The script region and the main block grow by exactly what the bigger pools take, so
// every other heap keeps the room it had (they never shrink below the original's).
namespace memory::sizes {

struct Pools {
    int script_cache_entries_; // CScriptCacheEntry (0x14 bytes)
    int components;         // CComponent (0x10)
    int structs;            // CStruct (8)
    int vectors;            // CVector (0x10)
    int pairs;              // CPair (0xC)
    int arrays;             // CArray (0xC)
    int symbols;            // CSymbolTableEntry (0x14)
    int scripts;            // CScript (0xF4)
    int stored_randoms_;      // CStoredRandom (0x60) - THUGPRO leaves it, so do we
    uint32_t string_bytes_;   // the permanent string heap (AllocatePermanentStringHeap)
    uint32_t strings;       // its string slots (8 bytes each)
    int hash_items_;          // the HashItem pool (memory::slot_pool_setup::SetUp, 12 bytes; from the bottom-up heap)
};

// The original's (CreateScriptPools 0x0046F520, Main 0x004E1EF0).
const Pools& get_original();
// Our defaults: THUGPRO's, four pools bigger (memory_sizes.cpp kDefaults).
const Pools& get_defaults();
// THUGPRO's (its shipped thugpro.dll).
const Pools& get_thugpro();
// What this run uses: the ini's (read once), else the defaults.
const Pools& at_hand();

// Bytes the script pools and the string heap take from the script region (not the HashItem pool's).
uint32_t script_pool_bytes(const Pools& pools);
// The script region (original 0x434770 bytes) - grown by what the current pools take more than the original's, and
// by script_heap_extra.
uint32_t script_region_size();
// NOT ORIGINAL - room added to the script heap for the scripts and the node name prefixes a level brings (the original
// left ~2 MB beside the pools: THP8's whole town, ~34,000 nodes, ran it dry adding its node prefixes); [memory]
// script_heap_extra_mb= (0 to 256), original_sizes=1: none. Part of script_region_size (so of the main block).
constexpr int kScriptHeapExtraMb = 16;
uint32_t script_heap_extra();
// The main block (original 64 MB) - grown by the script region's growth and the HashItem pool's.
uint32_t main_block_size();

// NOT ORIGINAL - the slots of the node manager's name hash (engine/script/node_manager.h): the original's 0x4000 (16,384
// - a level with more named nodes filled it and its lookups never ended), ours 0x10000 by default (THP8's whole town,
// tools/pipeline/thp8_levels, has ~34,000), [memory] node_hash_slots= (a power of two is taken: rounded up; 0x4000 to
// 0x100000); original_sizes=1 the original's. More than the original's live outside the node manager's object (its
// layout is the original's): level_nodes::CNodeManager keeps them beside it, node indices in 32 bits.
uint32_t node_hash_slots();
constexpr uint32_t kOriginalNodeHashSlots = 0x4000;

// Self-tests comparing the original code with ours: the original sizes while one of these is alive.
class UseOriginal {
public:
    UseOriginal();
    ~UseOriginal();
    UseOriginal(const UseOriginal&) = delete;
    UseOriginal& operator=(const UseOriginal&) = delete;

private:
    bool was_;
};

} // namespace memory::sizes
