#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

// NOT ORIGINAL - the level editor's chains: grind rails, ladders and paths (ours; see level_editor.h - phase 4 of the
// plan).
//
// What a chain is: nodes linked by their links= (RailNode - grind rails, ClimbingNode - ladders, Waypoint - the
// paths peds and cars follow; a Pedestrian's / Vehicle's own links= names the first waypoint of its path).
//   - Points are moved through the node edits (level_editor_node_edit.h): a rail or ladder point where the rail manager
//     has it (the layout's `rail` records), a waypoint through its pos= (`set` records); with the whole chain (L).
//   - New points (rails and paths) are nodes of the editor's overlay array (level_editor_node_create.h: phase 3's
//     mechanism). Their links= are names (the overlay is registered again on every change, and the node manager's
//     ResolveLinks would read names as indices: NodeCreate keeps them out of it). A new point copies the fields of the
//     point it was drawn from (`like`: a ped's waypoint keeps its PedType, a rail its TerrainType), else a plain
//     RailNode (CreatedAtStart, a TerrainType) / Waypoint (CreatedAtStart).
//   - A level's chain node relinked: its own links= array is rewritten in place (its links as the level had them kept
//     to put back). A level's point deleted: hidden with phase 3's marker class (`drop`), the points before it linked
//     on to the one after it.
//   - Rails: the game makes the rail manager's nodes only at the level's load (0x0057E960 CRailManager::AddFromNodeArray
//     from CFunc_ParseNodeArray 0x005A34E0, after 0x0057C330 Cleanup - the only other caller is a moving object's rail
//     manager 0x0042B880) and has no way to add one later, so the editor builds it again the same way (RebuildRails):
//     every node it has is kept as it is (moved places, terrain, on / off - RemoveOverlaps' choices), new rail points
//     are added with the game's own AddNode (0x0057D090), and every node's next / previous is made again from the
//     links= as AddFromNodeArray makes them (a node's next: its first link, or a DefaultLine one). The old node array
//     isn't freed while the level is up (a skater's grind may still point into it): it goes with the level.
//   - Terrain: a rail's terrain is the rail manager node's byte (0x004442C0 maps the TerrainType checksum, from the
//     script array Terrain_Types): set for every point of the rail, live; `terrain` records.
//   - A ped's or car's path: its node's links= set to the path's first waypoint; the object is then made again from its
//     node (killed, and once the game has removed it, 0x005A2E50 - CreateFromStructure, as the level's load makes it):
//     the path code reads the links when the object starts, so this is how the switch shows at once (and after a
//     reload: the layout applies a second after the level is up, and does the same).
// Layout records (version 2 still - an older reader skips them):
//   point <name> <Class> pos=x,y,z [like=<node>] [terrain=<T>] [links=<node>,<node>]   a point the editor added
//   links <node> <node>,<node>... | -                       a level node's links= as edited (- : none)
//   drop <node>                                              a level's chain point deleted (hidden; relinked)
//   terrain <node> <T>                                       a level rail point's terrain
// Nodes are 8 hex digits (their checksums), terrains their names (TERRAIN_GRINDMETAL).
namespace Front::LevelEditor::Chains {

// --- the pure parts (self-tested: src/hook/selftests_editor_chains.cpp) -------------------------------------------

// A point the editor added (in the overlay, in order).
struct Point {
    std::string name;   // ed_rail01, ed_path01...
    std::string cls;    // RailNode, Waypoint
    V3 pos = {};
    uint32_t like = 0;  // the node it copies its fields from (0: none)
    std::string terrain; // a rail's TerrainType ("": the default / the copied one)
    std::vector<uint32_t> links;
};
struct LinkEdit {
    uint32_t name;
    std::vector<uint32_t> links;
};
struct TerrainEdit {
    uint32_t name;
    std::string terrain;
};
// Everything the editor changed in the level's chains.
struct State {
    std::vector<Point> points;
    std::vector<LinkEdit> links;      // (sorted by name)
    std::vector<uint32_t> dropped;    // (sorted)
    std::vector<TerrainEdit> terrains; // (sorted by name)
};
bool SamePoint(const Point& a, const Point& b);
bool SameState(const State& a, const State& b);
bool Empty(const State& state);
// The records' lines, and one record read (`kind` its first word: point / links / drop / terrain; false: malformed -
// a repeated key, a bad value, no pos; unknown keys are skipped).
std::string WriteRecords(const State& state);
bool IsRecord(const std::string& kind);
bool ReadRecord(const std::string& kind, std::istringstream& in, State* state);
// The classes the editor draws.
bool IsChainClass(const std::string& cls);

// The links of chain nodes by name: what the operations below change.
using Graph = std::map<uint32_t, std::vector<uint32_t>>;
// The nodes linking to `name`.
std::vector<uint32_t> Predecessors(const Graph& graph, uint32_t name);
// A chain's end: no next (no links) or no previous (nothing links to it).
bool IsLast(const Graph& graph, uint32_t name);
bool IsFirst(const Graph& graph, uint32_t name);
// `point` added after `from`'s end: appended (from has no next: from -> point) or prepended (from has no previous:
// point -> from). False: `from` isn't an end.
bool Extend(Graph* graph, uint32_t from, uint32_t point);
// `point` put between `from` and its next. False: `from` has no next.
bool InsertAfter(Graph* graph, uint32_t from, uint32_t point);
// `name` taken out: the nodes linking to it link on to its next (if any, and not already); its links cleared.
void Remove(Graph* graph, uint32_t name);
// Two ends linked: a (no next) -> b, else b (no next) -> a. False: neither has a free next, or the same node.
bool Link(Graph* graph, uint32_t a, uint32_t b);
// `name`'s link to its next (its first link) removed: the chain split there. False: none.
bool Unlink(Graph* graph, uint32_t name);
// The points of `name`'s chain (linked either way), `name` first.
std::vector<uint32_t> Connected(const Graph& graph, uint32_t name);

// Snapping a drawn point: the ray's hit on an upward surface (normal.y >= 0.7) moved to the nearest edge within
// `reach` - the first place, going out in 8 directions in steps of 4, where the ground (groundAt: the ground's height
// below a point 20 above; false: none) falls more than 8 below the hit - half a step back from it. Elsewhere (walls,
// no edge near): the hit itself. `snapped`: whether it moved.
V3 SnapToEdge(V3 hit, V3 normal, float reach, const std::function<bool(V3 from, float* groundY)>& groundAt,
              bool* snapped);

// The game's grind terrains (Terrain_Types' TERRAIN_GRIND... in scripts\game\terrain.qb), in the script's order.
const std::vector<std::string>& GrindTerrainNames();

// --- the game side ------------------------------------------------------------------------------------------------

// What the editor has changed in the level's chains now.
State Current();
// `state` put in place (from the current one): points added / removed / moved / relinked, level links rewritten or put
// back, points dropped or shown, terrains set or put back; the rail manager built again; objects whose path changed
// made again. False: not in a level.
bool SetState(const State& state);
// A new level: forgotten (the last level's links put back if its node array is still the one loaded).
void ForgetLevel();
// Once a frame (on or off): objects waiting to be made again.
void Tick();

// The rail manager built again (see the top); its node array before is kept until the level goes.
void RebuildRails();

// A chain point / a chain node (rail, ladder, waypoint).
bool IsChainNode(uint32_t name);
bool IsPath(uint32_t name);
bool IsLadder(uint32_t name);
// A point the editor added (moved by the node edits: its pos= and its rail manager node).
bool IsAddedPoint(uint32_t name);
bool MoveAddedPoint(uint32_t name, V3 pos);
// The points of `name`'s chain (paths: by links either way among waypoints; rails: the rail manager's).
std::vector<uint32_t> ChainOf(uint32_t name);

// The editing operations (the Inspector, keys): each builds the new state and puts it in place. `message` says what
// happened (or why not); `select`: the node to select after (0: as it was).
enum Op : int {
    kOpInsert = 1,  // a point put halfway to the next one
    kOpDelete,      // the point deleted, its neighbours linked
    kOpUnlink,      // its link to the next removed
    kOpTerrain,     // `arg`: the terrain's checksum - for the whole rail
    kOpExtend,      // drawing goes on from this end (DrawAt)
    kOpLinkArm,     // the next node clicked is linked to this end (Picked)
    kOpAssignArm,   // (a ped / car) the next waypoint clicked becomes its path's start (Picked)
    kOpUnassign,    // (a ped / car) no path
    kOpCancel,      // drawing / an armed pick stopped
};
bool Do(Op op, uint32_t name, uint32_t arg, std::string* message, uint32_t* select);
// Drawing new chains (the Place palette's Rail / Path, or Extend): each click a point linked on from the last.
enum class Draw : uint8_t { None, Rail, Path };
void StartDrawing(Draw kind);
Draw Drawing();
// `end`: a chain's free end the click snapped to (SnapPoint) - the first click draws on from it, a later one links
// onto it and finishes. True: a point was added.
bool DrawAt(V3 at, uint32_t end, std::string* message, uint32_t* select);
void StopDrawing();
// The point a click in the view gives: the first collision the ray from `from` along `dir` meets (edges snapped:
// SnapToEdge, unless `free`), a rail's end within 24 units taken as it is (`end`: its name - drawing links to it).
bool SnapPoint(V3 from, V3 dir, float maxT, bool free, V3* point, uint32_t* end);
// An armed pick (Link / Assign): waiting, and the node clicked.
bool WantsPick();
bool Picked(uint32_t name, std::string* message, uint32_t* select);
const char* Waiting(); // (what the armed pick / drawing waits for; null: nothing)

// For the Inspector.
struct View {
    bool show = false;
    bool rail = false, ladder = false, path = false, object = false; // what the node is (object: a ped / car)
    bool added = false;
    bool canInsert = false, canDelete = false, canUnlink = false, canExtend = false, canLink = false;
    std::string next, prev; // neighbours' labels
    std::string why;        // why Delete can't (empty: it can)
    std::vector<std::string> terrains; // the grind terrains (names)
    int terrain = -1;       // the rail's (index; -1 unknown)
    std::string pathStart;  // an object's path's first waypoint ("": none)
    const char* waiting = nullptr;
    int points = 0;         // the chain's points
};
View ViewOf(uint32_t name);

} // namespace Front::LevelEditor::Chains
