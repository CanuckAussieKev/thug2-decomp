#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <gsl/pointers>
#include <array>
#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/lists/key_map.h"

// Pieces of the trick object code at the end of the viewer range (0x005880E0-0x0058817C) and the trick object
// manager's (0x007CE500) lookups, graffiti colours and graffiti log (0x00588200-0x005889E0); the rest of it (its
// clusters' own code, 0x00588A20 on) is another range.
namespace script {
class Struct;
}

namespace objects {

// 0x005880E0: one count of the trick object manager (0x007CE500, counted at 0x007CE504) released; the last deletes it
// (its scalar deleting destructor, slot 0).
void release_trick_object_manager();

// 0x00588120: a cluster walk's callback (cdecl): counted when the object (+0x40 set) is in the cluster asked for.
// (The walk is over the manager's clusters: a cluster held - +0x40 - by the player asked for, +0x3C.)
struct ClusterCount {
    int count;
    uint32_t cluster;
};
ClusterCount* count_object_in_cluster(uint8_t* object, ClusterCount* counter);

// A list in the skater's trick component (its +0x13B4): 0x00588170 the constructor (no entries; its count at +0x800);
// 0x00588180 emptied (true). Its entries are the names of the clusters the skater's current trick (or combo) hit -
// a ring of 0x200: past that the oldest are written over.
struct HitClusterLog {
    static constexpr uint32_t kCapacity = 0x200;
    std::array<uint32_t, kCapacity> items_{};
    int num_{0}; // +0x800
    HitClusterLog();
    bool get_clear();
    // 0x00588A20: the cluster of trick object `objectName` listed, unless it has none (or no name) or is listed
    // already; the goal manager is told first (0x00559150). Always false.
    bool append_one(uint32_t object_name);
    // 0x00588AA0: the listed clusters that still exist (in the manager's table of objects' clusters, with a name)
    // copied to `out` in order, until `max_bytes` bytes are written. Returns the bytes written.
    uint32_t copy_cluster_names(uint32_t* out, uint32_t max_bytes);
};

// A trick object (0x18 bytes): a node in its cluster's list (data itself; the node keeps the original's size) with the
// name of its level sector, which graffiti colours.
struct TrickSpot {
    memory::ListNode list_link_; // +0x00
    uint32_t label_;             // +0x14 (its sector's)

    // 0x00588BB0: the destructor's body - out of its list.
    void destroy_body();
    // 0x00588B40: the scalar deleting destructor: the body, then freed with flags bit 0.
    TrickSpot* teardown(unsigned int flags);

    // 0x00588200: its sector coloured as graffiti colour `index` (graffitiColors[index]: r, g, b; alpha 0x80 - the
    // sector's mesh, when it has one, vtable +0x18). True.
    bool set_graffiti_color(int index);
    // 0x00588270: its sector's colour cleared (0x0049E670) if it's graffiti colour `index`. True.
    bool clear_graffiti_color(int index);
};

// (The trick object list's node destructors - 0x00588590 the body, 0x005885F0 scalar deleting - and the cluster list's,
// 0x005885B0, take the node out of its list: memory::ListNode.)

// 0x00588B70: every node after this head taken out of the list (each linked to itself; none freed).
struct TrickObjectHead : memory::ListNode {
    void unlink_all();
};

// A trick object cluster (0x58 bytes, constructor 0x00588BD0): its name, the score that holds it (graffiti), each
// player's best score on it (by the skater's object id), the player holding it and whether anyone does, its objects.
struct TrickObjectCluster {
    memory::ListNode list_link_{}; // +0x00 (data itself)
    uint32_t label_;             // +0x14
    uint32_t record_points_{0};  // +0x18
    std::array<uint32_t, 8> scores_{};       // +0x1C
    int owner_{0};               // +0x3C (an object id)
    bool owned_{false};          // +0x40
    std::array<uint8_t, 3> pad41_{};
    memory::ListNode entities_{}; // +0x44 its CTrickObjects (the head: a sentinel)

    // 0x00588630: a trick object for sector `name` made (0x18 bytes) and put last in the list; its sector's colour
    // cleared first (and, with show_all_trick_objects set outside the 0x00684FA8 mode, given graffiti colour 2).
    // False (nothing made) when out of memory.
    bool add_object(uint32_t name);
    // 0x00588BD0: the constructor - named `name`, no scores, holder or objects.
    explicit TrickObjectCluster(uint32_t name);
    // 0x00588C30: the destructor's body - each object deleted (the first one each time), the list emptied, the head
    // and the cluster's own node out of their lists.
    ~TrickObjectCluster();
    TrickObjectCluster(const TrickObjectCluster&) = delete;
    TrickObjectCluster& operator=(const TrickObjectCluster&) = delete;
    // 0x00588D20: every object given graffiti colour `index`; in a created park (the level loaded by
    // load_sk5ed_gameplay) the rail editor's rail of the cluster's name too (0x004F4950). True.
    bool set_graffiti_color(int index);
    // 0x00588F40: every object's colour cleared if it's graffiti colour `index` (false at the first that fails); in a
    // created park the rail editor's rail of the cluster's name cleared too (0x004F3D10). True.
    bool clear_graffiti_color(int index);
    // 0x005891D0: the scalar deleting destructor.
    TrickObjectCluster* teardown(unsigned int flags);
};

// The trick object manager (0x007CE500): the clusters, by name and by each of their objects' names.
// (The original's vtable 0x0064F110 holds only its destructor.)
struct TrickSpotHub : memory::HeapObject {
    // How many clusters one log can change (THUG2's own mask had room for 128: 0x007CE508-0x007CE587, more wrote
    // over the trick object code's globals after it - THUGPRO's "256 graff tag crash"; ours is THUGPRO's 2048).
    static constexpr uint32_t kMaxLoggedClusters = 2048;

    int cluster_count_{};           // (not set by the constructor)
    lists::KeyMap clusters_;        // CTrickObjectClusters by name
    lists::KeyMap object_clusters_; // each trick object's cluster, by the object's name (a cluster's own too)
    script::Struct* args_;          // the graffiti colours' (change_args)

    // 0x00588DC0: both tables of 256 buckets, the parameters' structure made on the script heap.
    TrickSpotHub();
    // 0x00588E90 (0x00589200 the deleting one): every cluster deleted, both tables emptied and their buckets freed.
    ~TrickSpotHub();
    TrickSpotHub(const TrickSpotHub&) = delete;
    TrickSpotHub& operator=(const TrickSpotHub&) = delete;
    // 0x00588150: the parameters replaced by a copy of `params` (none: emptied). (The original's code is shared with
    // the clusters' structures at the same offset.)
    void change_args(script::Struct* params);
    // 0x00588FF0: trick object `objectName`'s cluster coloured for `player` (colour player + 1; -1 stays -1). False
    // if it has no cluster.
    bool set_object_graffiti_color(uint32_t object_name, int player);
    // 0x00589030: every cluster's graffiti colour `index` cleared (in the table's order; false at the first that fails).
    bool clear_graffiti_color(int index);
    // 0x00589080: trick object `objectName` filed under the cluster named `cluster_name` (null if there's none). True.
    bool add_object_to_cluster(uint32_t object_name, uint32_t cluster_name);
    // 0x005890B0: the parameters' arrays Skater0 ... Skater7 (trick object names) - each object's cluster coloured for
    // that player - then the parameters emptied.
    void apply_player_graffiti();
    // 0x00589230: "Clearing observer state": the parameters emptied; on the server the clusters `player` holds
    // released (0x00588300; false if none - else the skate module told, 0x0056BB70 with 0); then graffiti colour
    // player + 1 cleared everywhere. True.
    bool release_player_clusters(int player);
    // 0x00589670: up to 32 clusters nobody holds (0x00588350) given to `player` - scored 1000 and sent
    // (score_trick_objects) - if there are any.
    void give_free_clusters(int player);
    // 0x005896D0: a cluster named `name` made (0x58 bytes; null if out of memory) and listed by name, counted and filed
    // as its own trick object; in a created park the rail editor's rail of that name cleared first (0x004F3D10).
    // False if there's one already.
    bool add_cluster(uint32_t name);
    // 0x00589760: trick object `objectName` added to the cluster named `cluster_name` (which must exist) and, unless
    // it's the cluster's own name, filed under it. Whether it was added.
    bool add_object(uint32_t object_name, uint32_t cluster_name);
    // 0x005889F0: every cluster deleted (0x004B22A0, the generic delete through slot 0) and both tables emptied. True.
    bool remove_all_clusters();
    // 0x00588380 / 0x005883C0 (and a jump to it, 0x005889E0): a trick object's cluster / a cluster by name (null if
    // none).
    TrickObjectCluster* get_object_cluster(uint32_t object_name);
    TrickObjectCluster* get_cluster(uint32_t name);
    // 0x00588400: how many clusters `player` (an object id) holds.
    int count_clusters_held_by(int player);
    // 0x00588480: the name of the cluster `index` in the table's order (0 past the end).
    uint32_t get_cluster_name(uint8_t index);
    // 0x00588520: the graffiti state (the observers' message 0x5C, 0x00532460): 8 bytes of how many clusters each
    // player holds, then each held cluster's index in the table's order (a byte: past 255 they wrap - the format
    // THUG2's and THUGPRO's receivers read). "Building graffiti state message %d" printed for each player. Returns
    // the bytes written.
    int write_graffiti_state(uint8_t* buffer);

    // 0x005886C0: a trick (or combo) by `player` (a skater's object id) scoring `score` on the `count` trick objects
    // named in `objects`: their clusters' best scores for each player and the goal manager told (0x00558770); in
    // graffiti a cluster changes hands when the player's score - in team graffiti with team scoring, the player's
    // team's summed scores - beats the score that holds it; a previous holder's flag in `stolen` (8 bytes, by object
    // id) set. `objects` is compacted to those whose cluster changed (or was first taken); returns how many.
    uint32_t log_trick_objects(uint32_t count, uint32_t* objects, uint8_t* stolen, int player, uint32_t score);
};

// The graffiti messages' group (their formats: wire/skater_messages.h - sent by score_trick_objects and
// modules::PointTally, score_net.cpp).
constexpr uint8_t kGroupGame = 8;

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
    TrickSpotHub* manager;
};
// 0x00588300: a cluster held by the player asked for released (no holder, no scores) and its name listed.
// 0x00588350: a cluster nobody holds listed (at most 32).
struct HeldClusterRelease {
    int player;
    uint32_t count;
    std::array<uint32_t, TrickSpotHub::kMaxLoggedClusters> names; // (THUG2's room: 128 - see release_player_clusters)
};
struct FreeClusterList {
    std::array<uint32_t, 32> names;
    uint32_t count; // +0x80
};
void release_held_cluster(TrickObjectCluster* cluster, HeldClusterRelease* release);
void list_free_cluster(TrickObjectCluster* cluster, FreeClusterList* list);
// 0x005892C0: a trick (or combo) by `player` scoring `score` on the `count` trick objects named in `objects`. With
// `send` (the server): logged (TrickSpotHub::log_trick_objects - nothing more if no cluster changed); for each
// player whose clusters were taken, the thief told (message 0x4B: game id, thief, victim) and the victim sent the
// still-empty descriptor (the original's slip); the changed objects streamed to every player (0x3B, "TrickObj
// Buffer") and to every observer (0x5B); the skate module told (0x0056BB70 with 0); done if the scoring skater's
// connection is local. Then (and without `send`) each object's cluster coloured for the player.
void score_trick_objects(int player, uint32_t score, uint32_t count, uint32_t* objects, bool send);
// 0x00589180: the trick object manager (0x007CE500), made first if there's none and `create` (0x3C bytes, Calloc mode
// 1 - null if that fails); either way one more count of it (0x007CE504; release_trick_object_manager gives it back).
TrickSpotHub* acquire_trick_object_manager(bool create);
void find_cluster_index(TrickObjectCluster* cluster, ClusterIndexSearch* search);
void find_cluster_at_index(TrickObjectCluster* cluster, ClusterAtIndex* search);
void write_held_cluster(TrickObjectCluster* cluster, GraffitiStateWriter* writer);

extern std::unique_ptr<objects::TrickSpotHub> g_trick_spot_hub; // (made / deleted by the module's start-up / shutdown)

} // namespace objects
