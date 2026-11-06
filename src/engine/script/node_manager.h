#pragma once

#include <cstddef>
#include <cstdint>

#include <gsl/pointers>
#include <array>
#include <span>
#include "base/core/layout_32bit.h"
#include "engine/memory/mem.h"
#include "base/core/checksums.h"

// The node arrays the level's objects come from (level_nodes::CNodeManager, a reference-counted singleton at
// 0x006F0D84 / 0x006F0D88): up to 20 named script arrays of node structures, and a hash from every node's name
// to where it is (0x4000 slots, open addressing). From the disassembly of 0x0045CE60-0x0045D6E0; the node
// functions (nodes.h) and the prefix tables (node_prefixes.h) sit just before it.
namespace assets {
struct NodeTableResource;
} // namespace assets
namespace script {
class Array;
class Struct;
} // namespace script

namespace level_nodes {

// A hash slot: the node's array and its index in it (-1: empty).
struct NodeSlot {
    uint16_t element_list_;
    int16_t list_link_;
};

// A node array: its name, the script array and a flag AddNodeArray was given.
struct NodeArrayEntry {
    uint32_t label_;
    script::Array* node_entries_;
    bool flag_;
    std::array<uint8_t, 3> pad9;
};

constexpr size_t kNodeManagerWalkOffset = 0x10000;
constexpr size_t kNodeManagerNumOffset = 0x10004;
constexpr size_t kNodeManagerArraysOffset = 0x10008;
constexpr size_t kNodeManagerOwnerPtrOffset = 0x100F8;
constexpr size_t kNodeManagerBytes = 0x100FC;
class NodeManager {
public:
    static constexpr int kSlots = 0x4000;
    static constexpr int kMaxArrays = 20;

    // 0x0045D370: the constructor: no arrays, every slot empty, no walk.
    NodeManager();

    // 0x0045D520: `name` (a global array of nodes) added as the next node array, unless it is there already
    // (false): its nodes hashed, its prefixes added (add_node_array_prefixes), every node's position= turned into pos=
    // with z negated, its links turned from indices into node names (resolve_links); with `flag`, 0x004696C0 on
    // 0x004694E0's object. True.
    bool add_node_array(uint32_t name, bool flag);
    // 0x0045D1C0: the node array `name` taken out (its prefixes first, then unhash_node_array): the last
    // one moves into its place (the slots that pointed at the last one now point there). Whether it was there.
    bool remove_node_array(uint32_t name);
    // 0x0045D670: remove_node_array of the node array named after the file `path` (node_array_checksum).
    bool remove_node_array_of_file(const char* path);
    // 0x0045D270: whether there is a node array `name`.
    bool has_node_array(uint32_t name);
    // 0x0045D170: HashNode(its index, a, b) for the node array `name`; false if there is none.
    bool hash_node_in(checksums::Name array, uint32_t index, script::Struct* node);
    // 0x0045D3A0: every node of node array `index` hashed (HashNode(index, node index, node)).
    void hash_nodes(uint32_t index);
    // 0x0045CE60: node `index` of node array `array` put in the hash under its name= (in the first empty slot from
    // the name's low 14 bits on), unless it has none or its class= is "link".
    void hash_node(uint32_t array, uint32_t index, script::Struct* node);
    // 0x0045CEF0: the slots of node array `array` emptied, then every other node hashed again (slot by slot, each
    // taken out and put back in the first empty slot from its name on).
    void unhash_node_array(uint32_t array);
    // 0x0045CFC0: the node named `name` (null if there's none; `assert` isn't used).
    script::Struct* fetch_vertex(uint32_t name, bool assert);
    // 0x0045D480 (`this` isn't used): each node's links= array (indices into `nodes`) becomes the names of those
    // nodes (GetNodeName), as a checksum array.
    static void resolve_links(script::Array* nodes);
    // 0x0045D120 / 0x0045D150: the last node array's nodes / name (none: null / 0).
    script::Array* get_last_node_array();
    uint32_t get_last_node_array_name();

    // The walk over every node (one at a time, across the arrays).
    // 0x0045D050: start at the node named `name` (0: at the first node). A name that isn't hashed is searched for
    // past the empty slots [bug: the lookup reads the array 0xFFFF / node -1].
    void start_walk(uint32_t name);
    // 0x0045D110: end the walk.
    void end_walk();
    // 0x0045D3F0: the next node of the walk, or null at the end.
    script::Struct* get_next_node();

    // 0x0045D140 / 0x0045D470: the node table that owns the nodes (+0x100F8: a loaded node array's, or the park's),
    // and its script array.
    void set_owner(assets::NodeTableResource* owner);
    script::Array* get_owner_nodes();

    // The fields other code reads and sets.
    std::array<NodeSlot, kSlots>& slots() { return slots_; }
    [[nodiscard]] const std::array<NodeSlot, kSlots>& slots() const { return slots_; }
    NodeSlot& walk() { return walk_; }
    [[nodiscard]] const NodeSlot& walk() const { return walk_; }
    uint32_t& num() { return num_; }
    [[nodiscard]] uint32_t num() const { return num_; }
    std::array<NodeArrayEntry, kMaxArrays>& arrays() { return arrays_; }
    [[nodiscard]] const std::array<NodeArrayEntry, kMaxArrays>& arrays() const { return arrays_; }

private:
    std::array<NodeSlot, kSlots> slots_{};          // +0x00000
    NodeSlot walk_{};                   // +0x10000 the walk's place (list_link_ -1: no walk)
    uint32_t num_{0};                   // +0x10004 node arrays in use
    std::array<NodeArrayEntry, kMaxArrays> arrays_{}; // +0x10008
    assets::NodeTableResource* owner_ptr_{}; // +0x100F8
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(NodeManager, walk_) == kNodeManagerWalkOffset && offsetof(NodeManager, num_) == kNodeManagerNumOffset &&
                          offsetof(NodeManager, arrays_) == kNodeManagerArraysOffset && offsetof(NodeManager, owner_ptr_) == kNodeManagerOwnerPtrOffset &&
                          sizeof(NodeManager) == kNodeManagerBytes),
                      "layout must match the original (allocated as 0x100FC bytes)");
    }
};

// NOT ORIGINAL - `manager`'s name hash `slots` big (a power of two), kept beside it with 32-bit node indices, its walk
// too (null / 0: none - every manager its own slots_, as the original). create_node_manager gives the game's one
// memory::sizes::node_hash_slots() when that's more than kSlots; the self-tests give theirs one.
void use_big_name_hash(NodeManager* manager, uint32_t slots);

// 0x0045D2A0 (cdecl): `out` = the file name of `path` (after its last \ or /) with its extension replaced by
// "_nodearray" (sprintf). True.
bool make_node_array_name(const char* path, std::span<char> out);
// 0x0045D310 (cdecl): the checksum of make_node_array_name(path).
uint32_t node_array_checksum(const char* path);

// 0x0045D690: the instance, made (zeroed memory on the current heap) by the first call with `create`; every call
// takes a reference. 0x0045D6E0: drop a reference; the last frees it (no destructor runs).
NodeManager* create_node_manager(bool create);
// NOT ORIGINAL - the level editor's overlay node array (editor/level_editor_node_create.h): nodes it adds live in
// an array of their own, added after the level's. get_last_node_array / get_last_node_array_name pass over the array named
// here (0: none - the original behaviour), so the park editor and LoadNodeArray still get the level's; the hook is told
// after remove_node_array took an array out (null: none), so the overlay can leave with its level.
void set_editor_node_array(uint32_t name);
uint32_t editor_node_array();
using NodeArrayRemovedHook = void (*)(uint32_t name);
void set_node_array_removed_hook(NodeArrayRemovedHook hook);
// NOT ORIGINAL - told after AddNodeArray added an array (its nodes hashed, pos= and links made): the level editor's
// edited copy writes its layout's field edits into the level's nodes there, before ParseNodeArray makes the level's
// objects from them (editor/level_editor_fields.h). Null: none (the original behaviour).
using NodeArrayAddedHook = void (*)(uint32_t name);
void set_node_array_added_hook(NodeArrayAddedHook hook);

inline memory::HeapUnique<NodeManager>& node_manager_instance();
void release_node_manager();
} // namespace level_nodes
