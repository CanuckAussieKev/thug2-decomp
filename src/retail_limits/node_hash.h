#pragma once

// NOT ORIGINAL - the node name hash, bigger (limits.h kNodeHashSlots): the original game's node manager keeps its name
// hash inside itself (0x4000 slots of 16-bit node indexes at its start, the walk's place after them), so it can't
// grow there. The DLL keeps a bigger one of its own, 32-bit indexes, and the eight functions of the original that
// touch the slots or the walk jump to these instead (patches.cpp): the constructor, HashNode, UnhashNodeArray, GetNode,
// StartWalk, EndWalk, NextNode and RemoveNodeArray. They do what the original's do (this project's
// src/engine/script/node_manager.cpp is the same code, from the same disassembly), with three differences: no index
// is cut to 16 bits, a node past a full table is left out and logged instead of looping for ever, and a lookup or a
// walk start never goes round the table more than once. Everything else of the manager (its node arrays, their
// count, its owner) stays where the original keeps it, in the original's layout.
//
// The functions are __thiscall in the game (the manager in ECX, the arguments on the stack, popped by the callee):
// here they are __fastcall with an unused second argument (EDX) - the same call on x86, in standard C++ for MSVC and
// GCC alike.

#include <array>
#include <cstddef>
#include <cstdint>

namespace retail_limits::node_hash {

// The game's functions these call (verified at their addresses before anything is patched - patches.cpp).
// CArray::GetElement (0x0046CC30): element `index` (a node array's: the node structure).
using GetElement = void*(__fastcall*)(void* array, void* edx, int index);
// CStruct::GetChecksum (0x00476950): the checksum named `key` into `out`; whether it was there.
using GetChecksum = bool(__fastcall*)(void* node, void* edx, uint32_t key, uint32_t* out, int assert_found);
// RemoveNodeArrayPrefixes (0x0045CE40, __cdecl): the node name prefixes of the node array `name` taken out.
using RemovePrefixes = void(__cdecl*)(uint32_t name);

struct ExeCalls {
    GetElement get_element = nullptr;
    GetChecksum get_checksum = nullptr;
    RemovePrefixes remove_prefixes = nullptr;
};
void use_exe_calls(const ExeCalls& calls);

// The original's node manager (0x100FC bytes), as far as these read it.
struct NodeArrayEntry {
    uint32_t name;
    void* nodes; // the script array of node structures
    uint32_t flag;
};
constexpr std::size_t kMostNodeArrays = 20;
constexpr std::size_t kInlineSlots = 0x4000;
struct ManagerLayout {
    std::array<uint32_t, kInlineSlots> slots; // the original's own hash (left unused, set up as the original does)
    uint16_t walk_array;                      // the original's walk (left unused)
    int16_t walk_node;
    uint32_t array_count;
    std::array<NodeArrayEntry, kMostNodeArrays> arrays;
    void* owner;
};
constexpr std::size_t kArrayCountOffset = 0x10004;
constexpr std::size_t kArraysOffset = 0x10008;
constexpr std::size_t kOwnerOffset = 0x100F8;
constexpr std::size_t kManagerBytes = 0x100FC;
static_assert(sizeof(void*) != 4 || (offsetof(ManagerLayout, array_count) == kArrayCountOffset && offsetof(ManagerLayout, arrays) == kArraysOffset &&
                                     offsetof(ManagerLayout, owner) == kOwnerOffset && sizeof(ManagerLayout) == kManagerBytes),
              "the original's node manager layout");

// The script array's element count (CArray +4).
struct ArrayHead {
    uint32_t kind;
    uint32_t count;
};

// The replacements, at the original's addresses (patches.cpp).
// 0x0045D370: the constructor (returns the manager).
void* __fastcall construct(void* manager, void* edx);
// 0x0045CE60: node `index` of node array `array` put in the hash under its name= (none, or class= link: not hashed).
void __fastcall hash_node(void* manager, void* edx, uint32_t array, uint32_t index, void* node);
// 0x0045CEF0: node array `array`'s nodes taken out of the hash; every other node put back from its name's slot on.
void __fastcall unhash_node_array(void* manager, void* edx, uint32_t array);
// 0x0045CFC0: the node named `name`, or null (the second argument isn't used, as in the original).
void* __fastcall find_node(void* manager, void* edx, uint32_t name, uint32_t unused);
// 0x0045D050: the walk started at the node named `name` (0: the first node).
void __fastcall start_walk(void* manager, void* edx, uint32_t name);
// 0x0045D110: the walk ended.
void __fastcall end_walk(void* manager, void* edx);
// 0x0045D3F0: the walk's next node, or null at its end.
void* __fastcall next_node(void* manager, void* edx);
// 0x0045D1C0: the node array `name` taken out (its prefixes, its nodes; the last array moves into its place); whether
// it was there.
bool __fastcall remove_node_array(void* manager, void* edx, uint32_t name);

// How full the hash is now and was at its fullest since the manager was made; nodes left out of a full table.
struct Fill {
    uint32_t used;
    uint32_t peak;
    uint32_t left_out;
};
Fill fill();

} // namespace retail_limits::node_hash
