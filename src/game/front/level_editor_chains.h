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
//   - Ladders (phase 4b): two ClimbingNodes of Type Ladder (CreatedAtStart, the same Angles (0, a, 0) - THUG2's own
//     ladders: AP's palms, PH's and NO's buildings), the foot linked to the top. The rail manager keeps a climbing
//     node's orientation as a quaternion (CRailNode::m_climb, from its Angles in AddNode); the skater climbing it
//     stands back from it against its at row (0x004413E0: the point minus at * a script distance), so the at row -
//     (sin a, 0, cos a) - points into the wall: measured on the levels' ladders against their collision (the nearer
//     front face is along +at for buildings). Drawn: the foot clicked on a wall (4 out from it, down to the ground
//     below), then the top (on the line straight up from the foot); facing into the wall the first click hit. Turned
//     with Z / X (15 degrees, the whole ladder): an added ladder's Angles, a level ladder's rail manager orientation
//     (`facing` records). Deleted whole (a level's: both points dropped - their rail manager nodes off).
//   - Trick objects (phase 4b): the level's TrickObject rails are registered at load by AddFromNodeArray with the trick
//     object manager (0x007CE500: 0x005896D0 adds the cluster, 0x00589080 maps the node's name to it - plain run-time
//     calls, also made for other objects by ParseNodeArray 0x005A37B4); a skater grinding one adds its cluster to the
//     combo's trick objects (0x00588A20 through 0x00588380 - graffiti / trick-object scoring). A drawn rail is
//     registered the same way when the rail manager is built again: its cluster the scenery piece the first click hit
//     (as the levels name their rails' clusters), else its first point; Inspector "Trick object" off takes its points
//     out of the map again (Lst::HashTable::FlushItem). Gaps aren't trick objects: a level's gaps are its nodes'
//     scripts (StartGap / EndGap) - a drawn rail has none.
//   - Grindable on flat ground (phase 4b): the game has no node flag for it - RailClear (0x0057C600, all its callers:
//     the rail manager's finds and the skater's 0x0043E29F, 0x0050E5FB, 0x00516ABF...) passes every rail only in the
//     park editor's level. Ours lets a rail the editor marks pass too (Obj::SetRailClearOverride: its segments by their
//     ends' places): an added point's `grindflat=1`, a level point's `grindflat` record, and in a baked level
//     (tools/game/apply_layout.ts) the node flag ed_grind_flat, which only this DLL reads.
// Layout records (version 2 still - an older reader skips them):
//   point <name> <Class> pos=x,y,z [like=<node>] [terrain=<T>] [links=<node>,<node>] [facing=<radians>]
//         [cluster=<node>] [grindflat=1]                     a point the editor added
//   links <node> <node>,<node>... | -                       a level node's links= as edited (- : none)
//   drop <node>                                              a level's chain point deleted (hidden; relinked)
//   terrain <node> <T>                                       a level rail point's terrain
//   facing <node> <radians>                                  a level ladder point turned (its heading)
//   grindflat <node>                                         a level rail point grindable on flat ground
// Nodes are 8 hex digits (their checksums), terrains their names (TERRAIN_GRINDMETAL).
namespace Front::LevelEditor::Chains {

// --- the pure parts (self-tested: src/hook/selftests_editor_chains.cpp) -------------------------------------------

// A point the editor added (in the overlay, in order).
struct Point {
    std::string name;   // ed_rail01, ed_path01, ed_ladder01...
    std::string cls;    // RailNode, Waypoint, ClimbingNode (a ladder's: Type Ladder)
    V3 pos = {};
    uint32_t like = 0;  // the node it copies its fields from (0: none)
    std::string terrain; // a rail's TerrainType ("": the default / the copied one)
    std::vector<uint32_t> links;
    float facing = 0.0f;   // a ladder's heading (radians: Angles (0, facing, 0) - its at row points into the wall)
    uint32_t cluster = 0;  // a rail's trick-object cluster (0: not a trick object)
    bool grindFlat = false; // a rail's: grindable on flat ground (RailClear passed)
};
struct FacingEdit {
    uint32_t name;
    float facing;
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
    std::vector<FacingEdit> facings;   // level ladder points turned (sorted by name)
    std::vector<uint32_t> grindFlat;   // level rail points grindable on flat ground (sorted)
};
bool SamePoint(const Point& a, const Point& b);
bool SameState(const State& a, const State& b);
bool Empty(const State& state);
// The records' lines, and one record read (`kind` its first word: point / links / drop / terrain; false: malformed -
// a repeated key, a bad value, no pos; unknown keys are skipped).
std::string WriteRecords(const State& state);
bool IsRecord(const std::string& kind);
bool ReadRecord(const std::string& kind, std::istringstream& in, State* state);
// The classes the editor draws (RailNode, Waypoint, ClimbingNode).
bool IsChainClass(const std::string& cls);

// Ladders. The heading a ladder on a wall of this (outward) normal gets: its at row (sin a, 0, cos a) into the wall.
float FacingIntoWall(V3 normal);
// The at row of a heading (flat).
V3 FacingDirection(float facing);
// A ladder's foot from the wall hit: `out` from the wall along the normal (flattened), then down onto the ground below
// it (groundAt as SnapToEdge's: the ground's height below a point; none within 64 down: the hit's height). False: the
// hit isn't on a wall (|normal.y| >= 0.7).
bool LadderFoot(V3 hit, V3 normal, float out, const std::function<bool(V3 from, float* groundY)>& groundAt,
                V3* foot);
// The ladder's top for a click: the point of the vertical line through `foot` nearest the ray from `from` along
// `dir` (a ray along the line: the foot). False: below kLadderMin above the foot.
constexpr float kLadderMin = 24.0f, kLadderOut = 4.0f;
bool LadderTop(V3 foot, V3 from, V3 dir, V3* top);
// Whether the segment a-b (either way) is p-q, each within `tolerance` on every axis.
bool SameSegment(V3 a, V3 b, V3 p, V3 q, float tolerance);

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
    kOpTurn,        // (a ladder) turned `arg` 15-degree notches (a signed int), the whole ladder
    kOpFacingHome,  // (a ladder) facing as the level has it
    kOpTrickObject, // (a drawn rail) `arg` 1: a trick object (its cluster), 0: not - the whole rail
    kOpGrindFlat,   // (a rail) `arg` 1: grindable on flat ground, 0: as the game decides - the whole rail
};
bool Do(Op op, uint32_t name, uint32_t arg, std::string* message, uint32_t* select);
// Drawing new chains (the Place palette's Rail / Path / Ladder, or Extend): each click a point linked on from the
// last; a ladder: its foot on a wall, then its top.
enum class Draw : uint8_t { None, Rail, Path, Ladder };
// A click while drawing a ladder: the ray from `from` along `dir`. True: the ladder was made (drawing stops).
bool LadderClick(V3 from, V3 dir, float maxT, std::string* message, uint32_t* select);
// The rail manager's RailClear override (Obj::SetRailClearOverride): a segment of a rail marked grindable on flat
// ground.
bool FlatGrindable(V3 a, V3 b);
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
    // Ladders: its heading (degrees), turned from the level's.
    bool canTurn = false, turned = false;
    float facingDegrees = 0.0f;
    // Rails: a trick object (its cluster's label), whether that can be switched (drawn rails); grindable on flat ground.
    bool trickObject = false, canTrickObject = false, grindFlat = false;
    std::string cluster;
};
View ViewOf(uint32_t name);

} // namespace Front::LevelEditor::Chains
