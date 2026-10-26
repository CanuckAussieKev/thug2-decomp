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
    Mem::ListNode m_node;     // +0x00 (data itself; its vtable 0x0064F10C is the cluster's)
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
    // 0x00588BD0: the constructor - named `name`, no scores, holder or objects. Returns this.
    CTrickObjectCluster* Construct(uint32_t name);
    // 0x00588C30: the destructor's body - each object deleted (its virtual destructor, slot 0: the first one each
    // time), the list emptied, the head and the cluster's own node out of their lists.
    void DestroyBody();
    // 0x00588D20: every object given graffiti colour `index`; in a created park (the level loaded by
    // load_sk5ed_gameplay) the rail editor's rail of the cluster's name too (0x004F4950). True.
    bool SetGraffitiColor(int index);
    // 0x00588F40: every object's colour cleared if it's graffiti colour `index` (false at the first that fails); in a
    // created park the rail editor's rail of the cluster's name cleared too (0x004F3D10). True.
    bool ClearGraffitiColor(int index);
    // 0x005891D0: the scalar deleting destructor (vtable slot 0).
    CTrickObjectCluster* Destroy(unsigned int flags);
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

    // 0x00588DC0: the constructor (vtable 0x0064F110): both tables of 256 buckets, the parameters' structure made on
    // the script heap. (The cluster count isn't set.) Returns this.
    CTrickObjectManager* Construct();
    // 0x00588E90: the destructor's body - every cluster deleted, both tables emptied and their buckets freed.
    void DestroyBody();
    // 0x00588FF0: trick object `objectName`'s cluster coloured for `player` (colour player + 1; -1 stays -1). False
    // if it has no cluster.
    bool SetObjectGraffitiColor(uint32_t objectName, int player);
    // 0x00589030: every cluster's graffiti colour `index` cleared (in the table's order; false at the first that fails).
    bool ClearGraffitiColor(int index);
    // 0x00589080: trick object `objectName` filed under the cluster named `clusterName` (null if there's none). True.
    bool AddObjectToCluster(uint32_t objectName, uint32_t clusterName);
    // 0x005890B0: the parameters' arrays Skater0 ... Skater7 (trick object names) - each object's cluster coloured for
    // that player - then the parameters emptied.
    void ApplyPlayerGraffiti();
    // 0x00589230: "Clearing observer state": the parameters emptied; on the server the clusters `player` holds
    // released (0x00588300; false if none - else the skate module told, 0x0056BB70 with 0); then graffiti colour
    // player + 1 cleared everywhere. True.
    bool ReleasePlayerClusters(int player);
    // 0x00589200: the scalar deleting destructor (vtable slot 0).
    CTrickObjectManager* Destroy(unsigned int flags);
    // 0x00589670: up to 32 clusters nobody holds (0x00588350) given to `player` - scored 1000 and sent
    // (ScoreTrickObjects) - if there are any.
    void GiveFreeClusters(int player);
    // 0x005896D0: a cluster named `name` made (0x58 bytes; null if out of memory) and listed by name, counted and filed
    // as its own trick object; in a created park the rail editor's rail of that name cleared first (0x004F3D10).
    // False if there's one already.
    bool AddCluster(uint32_t name);
    // 0x00589760: trick object `objectName` added to the cluster named `clusterName` (which must exist) and, unless
    // it's the cluster's own name, filed under it. Whether it was added.
    bool AddObject(uint32_t objectName, uint32_t clusterName);
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

// The graffiti messages (the receivers' layouts; bytes the original leaves unset are zero here) - sent by
// ScoreTrickObjects and Mdl::Score (score_net.cpp).
constexpr uint8_t kMsgTagStolen = 0x4B;
constexpr uint8_t kMsgTrickObjects = 0x3B;
constexpr uint8_t kMsgObserverTrickObjects = 0x5B;
constexpr uint8_t kGroupGame = 8;
constexpr uint32_t kMaxMessageObjects = CTrickObjectList::kCapacity;
struct TagStolenMessage {
    uint8_t gameId;
    uint8_t pad01[3];
    int thief;
    int victim;
};
struct TrickObjectsMessage {
    uint8_t one; // (always 1)
    uint8_t unknown01[9];
    uint8_t gameId; // +0x0A
    uint8_t pad0B;
    int player;     // +0x0C
    uint32_t score; // +0x10
    uint32_t count; // +0x14
    uint32_t objects[kMaxMessageObjects];
};
struct ObserverTrickObjectsMessage {
    uint8_t gameId;
    uint8_t pad01[3];
    int player;
    uint32_t count;
    uint32_t objects[kMaxMessageObjects];
};
static_assert(sizeof(TagStolenMessage) == 0xC && offsetof(TrickObjectsMessage, objects) == 0x18 &&
                  offsetof(ObserverTrickObjectsMessage, objects) == 0xC,
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
// 0x00588300: a cluster held by the player asked for released (no holder, no scores) and its name listed.
// 0x00588350: a cluster nobody holds listed (at most 32).
struct HeldClusterRelease {
    int player;
    uint32_t count;
    uint32_t names[CTrickObjectManager::kMaxLoggedClusters]; // (THUG2's room: 128 - see ReleasePlayerClusters)
};
struct FreeClusterList {
    uint32_t names[32];
    uint32_t count; // +0x80
};
void __cdecl ReleaseHeldCluster(CTrickObjectCluster* cluster, HeldClusterRelease* release);
void __cdecl ListFreeCluster(CTrickObjectCluster* cluster, FreeClusterList* list);
// 0x005892C0: a trick (or combo) by `player` scoring `score` on the `count` trick objects named in `objects`. With
// `send` (the server): logged (CTrickObjectManager::LogTrickObjects - nothing more if no cluster changed); for each
// player whose clusters were taken, the thief told (message 0x4B: game id, thief, victim) and the victim sent the
// still-empty descriptor (the original's slip); the changed objects streamed to every player (0x3B, "TrickObj
// Buffer") and to every observer (0x5B); the skate module told (0x0056BB70 with 0); done if the scoring skater's
// connection is local. Then (and without `send`) each object's cluster coloured for the player.
void __cdecl ScoreTrickObjects(int player, uint32_t score, uint32_t count, uint32_t* objects, bool send);
// 0x00589180: the trick object manager (0x007CE500), made first if there's none and `create` (0x3C bytes, Calloc mode
// 1 - null if that fails); either way one more count of it (0x007CE504; ReleaseTrickObjectManager gives it back).
CTrickObjectManager* __cdecl AcquireTrickObjectManager(bool create);
void __cdecl FindClusterIndex(CTrickObjectCluster* cluster, ClusterIndexSearch* search);
void __cdecl FindClusterAtIndex(CTrickObjectCluster* cluster, ClusterAtIndex* search);
void __cdecl WriteHeldCluster(CTrickObjectCluster* cluster, GraffitiStateWriter* writer);

} // namespace Obj
