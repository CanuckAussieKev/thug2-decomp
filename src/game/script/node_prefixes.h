#pragma once

#include <cstddef>
#include <cstdint>

// The nodes by name prefix (SkateScript): when a node array is added, each node name's prefixes - the part before
// each '_' (but not "TRG") and the name without its trailing digits - get the node's index, in one table per node
// array, so scripts can find e.g. every "Ped_" node. From the disassembly of 0x0045BDA0-0x0045CE40.
namespace Lst {
class HashTable;
} // namespace Lst

namespace SkateScript {

// The nodes with one prefix in one node array: their indices (a block grown 100 at a time, Mem::Malloc).
struct PrefixNodes {
    // 0x0045BDA0: `index` added at the end.
    void Add(uint16_t index);

    int m_count;
    uint16_t* mp_indices;
    uint32_t m_prefix;
};
static_assert(sizeof(PrefixNodes) == 0xC, "allocated as 0xC bytes");

// One node array's prefixes: a hash table (2^8 buckets) from the prefix to its PrefixNodes.
struct PrefixTable {
    // 0x0045C5D0: the constructor (returns this).
    PrefixTable* Construct();
    // 0x0045CD30: the destructor: every PrefixNodes freed, then the table.
    void Destruct();
    // 0x0045C650: node `index` added under `prefix` (a new PrefixNodes if it has none yet).
    void Add(uint32_t prefix, uint16_t index);
    // 0x0045C240: every PrefixNodes' block cut down to its size.
    void Compact();

    Lst::HashTable* mp_table;
    uint32_t m_nodeArray; // the node array's name
    PrefixTable* mp_next;
};
static_assert(offsetof(PrefixTable, mp_next) == 8 && sizeof(PrefixTable) == 0xC, "allocated as 0xC bytes");

// The tables of all node arrays (the instance at 0x006F0D80, made by the first AddNodeArrayPrefixes).
struct PrefixTables {
    // 0x0045C700: the table of node array `name` (a global array of nodes), made and put first.
    void AddNodeArray(uint32_t name);
    // 0x0045CDF0: the tables of node array `name` destroyed.
    void RemoveNodeArray(uint32_t name);
    // 0x0045BE90: the names of the nodes with `prefix` in every node array (in a static buffer; `count` = how
    // many).
    uint32_t* GetNodes(uint32_t prefix, uint16_t* count);

    PrefixTable* mp_first;
};

PrefixTables*& NodePrefixes();

// 0x0045C900: AddNodeArray(name) on the script heap (the instance made first if need be).
void AddNodeArrayPrefixes(uint32_t name);
// 0x0045CE40: RemoveNodeArray(name).
void RemoveNodeArrayPrefixes(uint32_t name);
// 0x0045BF90: GetNodes on the instance.
uint32_t* GetPrefixedNodes(uint32_t prefix, uint16_t* count);
// 0x0045BFB0: the same by text (a trailing '_' is dropped) [the name is copied to 100 bytes unchecked].
uint32_t* GetPrefixedNodes(const char* prefix, uint16_t* count);

} // namespace SkateScript
