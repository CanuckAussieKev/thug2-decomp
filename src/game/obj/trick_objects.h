#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/lst/hashtable.h"

// Pieces of the trick object code at the end of the viewer range (0x005880E0-0x0058817C) and the trick object
// manager's (0x007CE500) lookups, graffiti colours and graffiti log (0x00588200-0x005889E0); the rest of it (its
// clusters' own code, 0x00588A20 on) is another range.
namespace Script {
class CStruct;
}

namespace Obj {

// 0x005880E0: one count of the trick object manager (0x007CE500, counted at 0x007CE504) released; the last deletes it
// (its scalar deleting destructor, slot 0).
void ReleaseTrickObjectManager();

// 0x00588120: a cluster walk's callback (cdecl): counted when the object (+0x40 set) is in the cluster asked for.
// (The walk is over the manager's clusters: a cluster held - +0x40 - by the player asked for, +0x3C.)
struct ClusterCount {
    int count;
    uint32_t cluster;
};
ClusterCount* __cdecl CountObjectInCluster(uint8_t* object, ClusterCount* counter);

// A trick cluster's parameters (its structure at +0x38). 0x00588150: replaced by a copy of `params` (none: emptied).
// (Its one caller, HandleGraffitiColors, calls it on the trick object manager itself: +0x38 is the manager's.)
struct CTrickCluster {
    uint8_t m_unknown00[0x38];
    Script::CStruct* m_params; // +0x38
    void SetParams(Script::CStruct* params);
};

// A list in the skater's trick component (its +0x13B4): 0x00588170 the constructor (no entries; its count at +0x800);
// 0x00588180 emptied (true). Its entries are the names of the clusters the skater's current trick (or combo) hit -
// a ring of 0x200: past that the oldest are written over.
struct CTrickObjectList {
    static constexpr uint32_t kCapacity = 0x200;
    uint32_t m_items[kCapacity];
    int m_count; // +0x800
    CTrickObjectList* Construct();
    bool Clear();
    // 0x00588A20: the cluster of trick object `objectName` listed, unless it has none (or no name) or is listed
    // already; the goal manager is told first (0x00559150). Always false.
    bool Add(uint32_t objectName);
    // 0x00588AA0: the listed clusters that still exist (in the manager's table of objects' clusters, with a name)
    // copied to `out` in order, until `maxBytes` bytes are written. Returns the bytes written.
    uint32_t CopyClusterNames(uint32_t* out, uint32_t maxBytes);
};

// A trick object (0x18 bytes, vtable 0x0064F0D8): a node in its cluster's list (data itself) with the name of its
// level sector, which graffiti colours.
struct CTrickObject {
    Mem::ListNode m_node;  // +0x00
    uint32_t m_name;       // +0x14 (its sector's)

    // 0x00588BB0: the destructor's body - out of its list (the node's vtable 0x0064F0D0 put back first).
    void DestroyBody();
    // 0x00588B40: the scalar deleting destructor (vtable slot 0): the body, then freed with flags bit 0.
    CTrickObject* Destroy(unsigned int flags);

    // 0x00588200: its sector coloured as graffiti colour `index` (graffitiColors[index]: r, g, b; alpha 0x80 - the
    // sector's mesh, when it has one, vtable +0x18). True.
    bool SetGraffitiColor(int index);
    // 0x00588270: its sector's colour cleared (0x0049E670) if it's graffiti colour `index`. True.
    bool ClearGraffitiColor(int index);
};
static_assert(sizeof(CTrickObject) == 0x18, "layout must match the original");

// The destructors of the trick object list's node (Lst::Node, vtable 0x0064F0D4): 0x00588590 the body (out of its
// list), 0x005885F0 scalar deleting; and of the cluster list's (vtable 0x0064F0D0) 0x005885B0 scalar deleting.
struct TrickObjectNode : Mem::ListNode {
    void DestroyBody();
    TrickObjectNode* Destroy(unsigned int flags);
};
struct TrickClusterNode : Mem::ListNode {
    TrickClusterNode* Destroy(unsigned int flags);
};

// 0x00588B70: every node after this head taken out of the list (each linked to itself; none freed).
struct TrickObjectHead : Mem::ListNode {
    void UnlinkAll();
};

// A trick object cluster (0x58 bytes, constructor 0x00588BD0): its name, the score that holds it (graffiti), each
// player's best score on it (by the skater's object id), the player holding it and whether anyone does, its objects.
struct CTrickObjectCluster {
    uint8_t m_unknown00[0x14];
    uint32_t m_name;          // +0x14
    uint32_t m_bestScore;     // +0x18
    uint32_t m_scores[8];     // +0x1C
    int m_owner;              // +0x3C (an object id)
    bool m_owned;             // +0x40
    uint8_t m_pad41[3];
    Mem::ListNode m_objects;  // +0x44 its CTrickObjects (the head: a sentinel)

    // 0x00588630: a trick object for sector `name` made (0x18 bytes) and put last in the list; its sector's colour
    // cleared first (and, with show_all_trick_objects set outside the 0x00684FA8 mode, given graffiti colour 2).
    // False (nothing made) when out of memory.
    bool AddObject(uint32_t name);
};
static_assert(offsetof(CTrickObjectCluster, m_objects) == 0x44 && sizeof(CTrickObjectCluster) == 0x58,
              "layout must match the original");

// The trick object manager (0x007CE500): the clusters, by name and by each of their objects' names.
struct CTrickObjectManager {
    // How many clusters one log can change (THUG2's own mask had room for 128: 0x007CE508-0x007CE587, more wrote
    // over the trick object code's globals after it - THUGPRO's "256 graff tag crash"; ours is THUGPRO's 2048).
    static constexpr uint32_t kMaxLoggedClusters = 2048;

    void** m_vtable;
    int m_clusterCount;               // +0x04
    Lst::HashTable m_clusters;        // +0x08 CTrickObjectClusters by name
    Lst::HashTable m_objectClusters;  // +0x20 each trick object's cluster, by the object's name (a cluster's own too)
    Script::CStruct* m_params;        // +0x38 (the graffiti colours' - CTrickCluster::SetParams)

    // 0x005889F0: every cluster deleted (through its virtual destructor, 0x004B22A0) and both tables emptied. True.
    bool RemoveAllClusters();
    // 0x00588380 / 0x005883C0 (and a jump to it, 0x005889E0): a trick object's cluster / a cluster by name (null if
    // none).
    CTrickObjectCluster* GetObjectCluster(uint32_t objectName);
    CTrickObjectCluster* GetCluster(uint32_t name);
    // 0x00588400: how many clusters `player` (an object id) holds.
    int CountClustersHeldBy(int player);
    // 0x00588480: the name of the cluster `index` in the table's order (0 past the end).
    uint32_t GetClusterName(uint8_t index);
    // 0x00588520: the graffiti state (the observers' message 0x5C, 0x00532460): 8 bytes of how many clusters each
    // player holds, then each held cluster's index in the table's order (a byte: past 255 they wrap - the format
    // THUG2's and THUGPRO's receivers read). "Building graffiti state message %d" printed for each player. Returns
    // the bytes written.
    int WriteGraffitiState(uint8_t* buffer);

    // 0x005886C0: a trick (or combo) by `player` (a skater's object id) scoring `score` on the `count` trick objects
    // named in `objects`: their clusters' best scores for each player and the goal manager told (0x00558770); in
    // graffiti a cluster changes hands when the player's score - in team graffiti with team scoring, the player's
    // team's summed scores - beats the score that holds it; a previous holder's flag in `stolen` (8 bytes, by object
    // id) set. `objects` is compacted to those whose cluster changed (or was first taken); returns how many.
    uint32_t LogTrickObjects(uint32_t count, uint32_t* objects, uint8_t* stolen, int player, uint32_t score);
};
static_assert(offsetof(CTrickObjectManager, m_clusters) == 0x08 &&
                  offsetof(CTrickObjectManager, m_objectClusters) == 0x20 &&
                  offsetof(CTrickObjectManager, m_params) == 0x38,
              "layout must match the original");

// The manager's table walks' callbacks (cdecl, the cluster and the walk's state). 0x00588430: a cluster's index (the
// clusters before it counted until it's found); 0x00588450: the cluster at an index (counted down); 0x005884B0: a
// player's held clusters written to the graffiti state.
struct ClusterIndexSearch {
    uint32_t name;
    uint8_t index;
    bool found;
};
struct ClusterAtIndex {
    uint32_t name;
    uint8_t countdown;
    bool found;
};
struct GraffitiStateWriter {
    int player;
    int count;
    uint8_t* buffer;
    CTrickObjectManager* manager;
};
void __cdecl FindClusterIndex(CTrickObjectCluster* cluster, ClusterIndexSearch* search);
void __cdecl FindClusterAtIndex(CTrickObjectCluster* cluster, ClusterAtIndex* search);
void __cdecl WriteHeldCluster(CTrickObjectCluster* cluster, GraffitiStateWriter* writer);

} // namespace Obj
