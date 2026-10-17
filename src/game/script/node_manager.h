#pragma once

#include <cstddef>
#include <cstdint>

// The node arrays the level's objects come from (SkateScript::CNodeManager, a reference-counted singleton at
// 0x006F0D84 / 0x006F0D88): up to 20 named script arrays of node structures, and a hash from every node's name
// to where it is (0x4000 slots, open addressing). From the disassembly of 0x0045D050-0x0045D6E0; its hash helpers
// (0x0045CE40 / 0x0045CE60 / 0x0045CEF0, the lookup 0x0045CFC0) and the node functions (0x0045BE40, 0x0045C900...)
// sit just before it and aren't rewritten yet.
namespace Script {
class CArray;
class CStruct;
} // namespace Script

namespace SkateScript {

// A hash slot: the node's array and its index in it (-1: empty).
struct NodeSlot {
    uint16_t m_array;
    int16_t m_node;
};

// A node array: its name, the script array and a flag AddNodeArray was given.
struct NodeArrayEntry {
    uint32_t m_name;
    Script::CArray* mp_nodes;
    bool m_flag;
    uint8_t pad9[3];
};

class CNodeManager {
public:
    static constexpr int kSlots = 0x4000;
    static constexpr int kMaxArrays = 20;

    // 0x0045D370: the constructor (returns this): no arrays, every slot empty, no walk.
    CNodeManager* Construct();

    // 0x0045D520: `name` (a global array of nodes) added as the next node array, unless it is there already
    // (false): its nodes hashed (0x0045CE60), 0x0045C900(name), every node's position= turned into pos= with z
    // negated, its links turned from indices into node names (ResolveLinks); with `flag`, 0x004696C0 on
    // 0x004694E0's object. True.
    bool AddNodeArray(uint32_t name, bool flag);
    // 0x0045D1C0: the node array `name` taken out (0x0045CE40(name) first, then 0x0045CEF0 unhashes it): the last
    // one moves into its place (the slots that pointed at the last one now point there). Whether it was there.
    bool RemoveNodeArray(uint32_t name);
    // 0x0045D670: RemoveNodeArray of the node array named after the file `path` (NodeArrayChecksum).
    bool RemoveNodeArrayOfFile(const char* path);
    // 0x0045D270: whether there is a node array `name`.
    bool HasNodeArray(uint32_t name);
    // 0x0045D170: 0x0045CE60(its index, a, b) for the node array `name`; false if there is none.
    bool HashNodeIn(uint32_t name, uint32_t a, uint32_t b);
    // 0x0045D3A0: every node of node array `index` hashed (0x0045CE60(index, node index, node)).
    void HashNodes(uint32_t index);
    // 0x0045D480 (`this` isn't used): each node's links= array (indices into `nodes`) becomes the names of those
    // nodes (0x0045BE40), as a checksum array.
    void ResolveLinks(Script::CArray* nodes);
    // 0x0045D120 / 0x0045D150: the last node array's nodes / name (none: null / 0).
    Script::CArray* GetLastNodeArray();
    uint32_t GetLastNodeArrayName();

    // The walk over every node (one at a time, across the arrays).
    // 0x0045D050: start at the node named `name` (0: at the first node). A name that isn't hashed is searched for
    // past the empty slots [bug: the lookup reads the array 0xFFFF / node -1].
    void StartWalk(uint32_t name);
    // 0x0045D110: end the walk.
    void EndWalk();
    // 0x0045D3F0: the next node of the walk, or null at the end.
    Script::CStruct* NextNode();

    // 0x0045D140 / 0x0045D470: an object kept with the manager (+0x100F8), and its +0x58.
    void SetOwner(void* owner);
    uint32_t GetOwnerValue58();

    NodeSlot m_slots[kSlots];               // +0x00000
    NodeSlot m_walk;                        // +0x10000 the walk's place (m_node -1: no walk)
    uint32_t m_count;                       // +0x10004 node arrays in use
    NodeArrayEntry m_arrays[kMaxArrays];    // +0x10008
    void* mp_owner;                         // +0x100F8
};
static_assert(offsetof(CNodeManager, m_walk) == 0x10000 && offsetof(CNodeManager, m_count) == 0x10004 &&
                  offsetof(CNodeManager, m_arrays) == 0x10008 && offsetof(CNodeManager, mp_owner) == 0x100F8 &&
                  sizeof(CNodeManager) == 0x100FC,
              "layout must match the original (allocated as 0x100FC bytes)");

// 0x0045D2A0 (cdecl): `out` = the file name of `path` (after its last \ or /) with its extension replaced by
// "_nodearray" (sprintf). True.
bool MakeNodeArrayName(const char* path, char* out);
// 0x0045D310 (cdecl): the checksum of MakeNodeArrayName(path).
uint32_t NodeArrayChecksum(const char* path);

// 0x0045D690: the instance, made (zeroed memory on the current heap) by the first call with `create`; every call
// takes a reference. 0x0045D6E0: drop a reference; the last frees it (no destructor runs).
CNodeManager* CreateNodeManager(bool create);
void ReleaseNodeManager();

} // namespace SkateScript
