#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include "base/core/checksums.h"
#include "base/core/layout_32bit.h"

// The nodes by name prefix (level_nodes): when a node array is added, each node name's prefixes - the part before
// each '_' (but not "TRG") and the name without its trailing digits - get the node's index, in one table per node
// array, so scripts can find e.g. every "Ped_" node. From the disassembly of 0x0045BDA0-0x0045CE40.
namespace lists {
class KeyMap;
} // namespace lists

namespace level_nodes {

constexpr size_t kPrefixNodesBytes = 0xC;
// The nodes with one prefix in one node array: their indices (a block grown 100 at a time, memory::alloc_block).
struct PrefixNodes {
    // 0x0045BDA0: `index` added at the end.
    void append_one(uint16_t index);

    // The fields other code reads and sets.
    [[nodiscard]] int num() const { return num_; }
    void set_num(int value) { num_ = value; }
    [[nodiscard]] uint16_t* indices() const { return indices_; }
    void set_indices(uint16_t* value) { indices_ = value; }
    [[nodiscard]] uint32_t name_prefix() const { return name_prefix_; }
    void set_name_prefix(uint32_t value) { name_prefix_ = value; }

private:
    int num_;
    uint16_t* indices_;

public:
    // NOT ORIGINAL: the indices as a run of num_.
    [[nodiscard]] std::span<uint16_t> index_run() const { return {indices_, static_cast<std::size_t>(num_)}; }
private:
    uint32_t name_prefix_;
};
static_assert(core::layout_32bit(sizeof(PrefixNodes) == kPrefixNodesBytes), "allocated as 0xC bytes");

constexpr size_t kPrefixTableFollowingOffset = 8;
constexpr size_t kPrefixTableBytes = 0xC;
// One node array's prefixes: a hash table (2^8 buckets) from the prefix to its PrefixNodes.
struct PrefixTable {
    // 0x0045C5D0: the constructor.
    PrefixTable();
    // 0x0045CD30: the destructor: every PrefixNodes freed, then the table.
    ~PrefixTable();
    PrefixTable(const PrefixTable&) = delete;
    PrefixTable& operator=(const PrefixTable&) = delete;
    // 0x0045C650: node `index` added under `prefix` (a new PrefixNodes if it has none yet).
    void append_one(checksums::Name prefix, uint16_t index);
    // 0x0045C240: every PrefixNodes' block cut down to its size.
    void compact();

    // The fields other code reads and sets.
    [[nodiscard]] lists::KeyMap* slot_table() const { return slot_table_; }
    [[nodiscard]] uint32_t node_array() const { return node_array_; }
    void set_node_array(uint32_t value) { node_array_ = value; }
    [[nodiscard]] PrefixTable* following() const { return following_; }
    void set_following(PrefixTable* value) { following_ = value; }

private:
    lists::KeyMap* slot_table_;
    uint32_t node_array_{0}; // the node array's name
    PrefixTable* following_{nullptr};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(PrefixTable, following_) == kPrefixTableFollowingOffset && sizeof(PrefixTable) == kPrefixTableBytes), "allocated as 0xC bytes");
    }
};

// The tables of all node arrays (the instance at 0x006F0D80, made by the first add_node_array_prefixes).
struct PrefixTables {
    // 0x0045C700: the table of node array `name` (a global array of nodes), made and put first.
    void add_node_array(uint32_t name);
    // 0x0045CDF0: the tables of node array `name` destroyed.
    void remove_node_array(uint32_t name);
    // 0x0045BE90: the names of the nodes with `prefix` in every node array (in a static buffer; `count` = how
    // many).
    std::span<const uint32_t> get_nodes(uint32_t prefix);

    // The fields other code reads and sets.
    void set_first_ptr(PrefixTable* value) { first_ptr_ = value; }

private:
    PrefixTable* first_ptr_;
};

PrefixTables*& node_prefixes();

// 0x0045C900: AddNodeArray(name) on the script heap (the instance made first if need be).
void add_node_array_prefixes(uint32_t name);
// 0x0045CE40: remove_node_array(name).
void remove_node_array_prefixes(uint32_t name);
// 0x0045BF90: get_nodes on the instance.
std::span<const uint32_t> collect_nodes_by_prefix(uint32_t prefix);
// 0x0045BFB0: the same by text (a trailing '_' is dropped) [the name is copied to 100 bytes unchecked].
std::span<const uint32_t> collect_nodes_by_prefix(const char* prefix);

} // namespace level_nodes
