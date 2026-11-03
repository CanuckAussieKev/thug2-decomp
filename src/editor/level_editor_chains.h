#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "editor/level_editor.h"

// NOT ORIGINAL - the level editor's chains: grind rails, ladders and paths (ours; see level_editor.h - phase 4 of the
// plan).
//
// What a chain is: nodes linked by their links= (RailNode - grind rails, ClimbingNode - ladders, Waypoint - the
// paths peds and cars follow; a Pedestrian's / Vehicle's own links= names the first waypoint of its path).
//   - Points are moved through the node edits (level_editor_node_edit.h): a rail or ladder point where the rail manager
//     has it (the layout's `rail` records), a waypoint through its pos= (`set` records); with the whole chain (L).
//   - New points (rails and paths) are nodes of the editor's overlay array (level_editor_node_create.h: phase 3's
//     mechanism). Their links= are names (the overlay is registered again on every change, and the node manager's
//     resolve_links would read names as indices: node_create keeps them out of it). A new point copies the fields of the
//     point it was drawn from (`like`: a ped's waypoint keeps its PedType, a rail its TerrainType), else a plain
//     RailNode (CreatedAtStart, a TerrainType) / Waypoint (CreatedAtStart).
//   - A level's chain node relinked: its own links= array is rewritten in place (its links as the level had them kept
//     to put back). A level's point deleted: hidden with phase 3's marker class (`drop`), the points before it linked
//     on to the one after it.
//   - Rails: the game makes the rail manager's nodes only at the level's load (0x0057E960 GrindRailHub::add_from_node_array
//     from cfunc_parse_node_array 0x005A34E0, after 0x0057C330 Cleanup - the only other caller is a moving object's rail
//     manager 0x0042B880) and has no way to add one later, so the editor builds it again the same way (rebuild_rails):
//     every node it has is kept as it is (moved places, terrain, on / off - remove_overlaps' choices), new rail points
//     are added with the game's own append_vertex (0x0057D090), and every node's next / previous is made again from the
//     links= as add_from_node_array makes them (a node's next: its first link, or a DefaultLine one). The old node array
//     isn't freed while the level is up (a skater's grind may still point into it): it goes with the level.
//   - Terrain: a rail's terrain is the rail manager node's byte (0x004442C0 maps the TerrainType checksum, from the
//     script array Terrain_Types): set for every point of the rail, live; `terrain` records.
//   - A ped's or car's path: its node's links= set to the path's first waypoint; the object is then made again from its
//     node (killed, and once the game has removed it, 0x005A2E50 - CreateFromStructure, as the level's load makes it):
//     the path code reads the links when the object starts, so this is how the switch shows at once (and after a
//     reload: the layout applies a second after the level is up, and does the same).
//   - Ladders (phase 4b): two ClimbingNodes of Type Ladder (CreatedAtStart, the same Angles (0, a, 0) - THUG2's own
//     ladders: AP's palms, PH's and NO's buildings), the foot linked to the top. The rail manager keeps a climbing
//     node's orientation as a quaternion (GrindPoint::climb_, from its Angles in append_vertex); the skater climbing it
//     stands back from it against its at row (0x004413E0: the point minus at * a script distance), so the at row -
//     (sin a, 0, cos a) - points into the wall: measured on the levels' ladders against their collision (the nearer
//     front face is along +at for buildings). Drawn: the foot clicked on a wall (4 out from it, down to the ground
//     below), then the top (on the line straight up from the foot); facing into the wall the first click hit. Turned
//     with Z / X (15 degrees, the whole ladder): an added ladder's Angles, a level ladder's rail manager orientation
//     (`facing` records). Deleted whole (a level's: both points dropped - their rail manager nodes off).
//   - Trick objects (phase 4b): the level's TrickObject rails are registered at load by add_from_node_array with the trick
//     object manager (0x007CE500: 0x005896D0 adds the cluster, 0x00589080 maps the node's name to it - plain run-time
//     calls, also made for other objects by ParseNodeArray 0x005A37B4); a skater grinding one adds its cluster to the
//     combo's trick objects (0x00588A20 through 0x00588380 - graffiti / trick-object scoring). A drawn rail is
//     registered the same way when the rail manager is built again: its cluster the scenery piece the first click hit
//     (as the levels name their rails' clusters), else its first point; Inspector "Trick object" off takes its points
//     out of the map again (lists::KeyMap::erase_entry). Gaps aren't trick objects: a level's gaps are its nodes'
//     scripts (StartGap / EndGap) - a drawn rail has none.
//   - Grindable on flat ground (phase 4b): the game has no node flag for it - RailClear (0x0057C600, all its callers:
//     the rail manager's finds and the skater's 0x0043E29F, 0x0050E5FB, 0x00516ABF...) passes every rail only in the
//     park editor's level. Ours lets a rail the editor marks pass too (GameObjects::set_rail_clear_override: its segments by their
//     ends' places): an added point's `grindflat=1`, a level point's `grindflat` record, and in a baked level
//     (tools/game/apply_layout.ts) the node flag ed_grind_flat, which only this game reads.
//   - Ledges (the feature map): ClimbingNode chains of Type Ledge (THUG2's own form: CreatedAtStart, Type=Ledge, and
//     HangLeft / HangRight when the level says which side the skater hangs from - ST's TRG_619... - append_vertex
//     0x0057D090 reads them as for any climbing node), drawn and edited as rails are (Blit::Ledge: the clicks snapped
//     to edges, Insert / Delete / Extend / Link / Unlink), their hang side for the whole ledge (ChainOp::kHang). Ladders keep
//     their own tools (a ledge isn't turned or deleted whole).
//   - Rail speed-ups (the feature map; THAW's RailAccelerate, which the THAW ports carry in their scripts - skutils.qb's:
//     while the skater grinds a rail and climbs, more speed each frame): a rail point's TriggerScript runs when the
//     skater lands on its segment (the skater's rail code calls ActivationZoneModule::get_trigger_node 0x00434A60, which reads
//     the node's TriggerScript then - so a change shows at once). A point given a speed-up gets a script of the
//     editor's as its TriggerScript (its own component, over any shared one; the node's script before is kept and
//     called first, its arguments passed on): ed_railspeed_<node> = `[<its script> <...>]` + `ed_RailSpeed rate=<r>`,
//     and the editor's fixed scripts ed_RailSpeed / ed_RailSpeedLoop (THAW's two, compiled - rail_speed_qb) do the rest:
//     on getting onto the rail (TriggerType [3, 13, -1]: onto, grind - THUG2's rail event, on landing and at each next
//     segment; THAW's own `land onto` is never true on a THUG2 rail: TriggerType needs every number - see
//     thug2-thaw-level-converter/thaw/thaw_scripts.ts) the loop is spawned on the skater; each frame on the rail and
//     higher than the last, its speed * (1 + rate). Taken off: the node's own TriggerScript put back as it was.
//     ([features] debug_rail_speed=1 logs the loops' boosts - Skater::SkaterPhysicsModule::SetSpeed.)
//     Shown as chevrons along the segment (the level's own RailAccelerate rails too, in grey - read only).
// Layout records (version 2 still - an older reader skips them):
//   point <name> <Class> pos=x,y,z [like=<node>] [terrain=<T>] [links=<node>,<node>] [facing=<radians>]
//         [cluster=<node>] [grindflat=1] [type=Ledge] [hang=left|right] [speed=<rate>]
//                                                            a point the editor added
//   links <node> <node>,<node>... | -                       a level node's links= as edited (- : none)
//   drop <node>                                              a level's chain point deleted (hidden; relinked)
//   terrain <node> <T>                                       a level rail point's terrain
//   facing <node> <radians>                                  a level ladder point turned (its heading)
//   grindflat <node>                                         a level rail point grindable on flat ground
//   railspeed <node> <rate>                                  a level rail point given a speed-up (rate 0.005 - 0.5)
// Nodes are 8 hex digits (their checksums), terrains their names (TERRAIN_GRINDMETAL).
namespace frontend::level_editor::chains {

// --- the pure parts -----------------------------------------------------------------------------------------------

// A point the editor added (in the overlay, in order).
struct Point {
    std::string name; // ed_rail01, ed_path01, ed_ladder01...
    std::string cls;  // RailNode, Waypoint, ClimbingNode (a ladder's: Type Ladder)
    V3 pos = {};
    uint32_t like = 0;   // the node it copies its fields from (0: none)
    std::string terrain; // a rail's TerrainType ("": the default / the copied one)
    std::vector<uint32_t> links;
    float facing = 0.0f;      // a ladder's heading (radians: Angles (0, facing, 0) - its at row points into the wall)
    uint32_t cluster = 0;     // a rail's trick-object cluster (0: not a trick object)
    bool grind_flat_ = false; // a rail's: grindable on flat ground (RailClear passed)
    std::string type;         // a ClimbingNode's Type: "" (a ladder - Type Ladder) or "Ledge"
    int hang = 0;       // a ledge's: 0 as the game decides, kHangLeft / kHangRight (the flags HangLeft / HangRight)
    float speed = 0.0f; // a rail point's speed-up rate (0: none)
};
constexpr int kHangLeft = 1, kHangRight = 2;
// A speed-up's rate kept within these (THAW's rails: 0.02 - 0.1; its default 0.05).
constexpr float kSpeedMin = 0.005f, kSpeedMax = 0.5f, kSpeedDefault = 0.05f;
float clamp_speed(float rate);
struct SpeedEdit {
    uint32_t name;
    float rate;
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
    std::vector<LinkEdit> links;       // (sorted by name)
    std::vector<uint32_t> dropped;     // (sorted)
    std::vector<TerrainEdit> terrains; // (sorted by name)
    std::vector<FacingEdit> facings;   // level ladder points turned (sorted by name)
    std::vector<uint32_t> grind_flat_; // level rail points grindable on flat ground (sorted)
    std::vector<SpeedEdit> speeds;     // level rail points given a speed-up (sorted by name)
};
bool same_point(const Point& a, const Point& b);
bool same_state(const State& a, const State& b);
bool vacant(const State& state);
// The records' lines, and one record read (`kind` its first word: point / links / drop / terrain; false: malformed -
// a repeated key, a bad value, no pos; unknown keys are skipped).
std::string write_records(const State& state);
bool is_record(const std::string& kind);
bool read_record(const std::string& kind, std::istringstream& in, State* state);
// The classes the editor draws (RailNode, Waypoint, ClimbingNode).
bool is_chain_class(const std::string& cls);

// Ladders. The heading a ladder on a wall of this (outward) normal gets: its at row (sin a, 0, cos a) into the wall.
float facing_into_wall(V3 normal);
// The at row of a heading (flat).
V3 facing_direction(float facing);
// A ladder's foot from the wall hit: `out` from the wall along the normal (flattened), then down onto the ground below
// it (ground_at as snap_to_edge's: the ground's height below a point; none within 64 down: the hit's height). False: the
// hit isn't on a wall (|normal.y| >= 0.7).
bool ladder_foot(V3 hit, V3 normal, float out, const std::function<bool(V3 from, float* ground_y)>& ground_at,
                 V3* foot);
// The ladder's top for a click: the point of the vertical line through `foot` nearest the ray from `from` along
// `dir` (a ray along the line: the foot). False: below kLadderMin above the foot.
constexpr float kLadderMin = 24.0f, kLadderOut = 4.0f;
bool ladder_top(V3 foot, V3 from, V3 dir, V3* top);
// Whether the segment a-b (either way) is p-q, each within `tolerance` on every axis.
bool same_segment(V3 a, V3 b, V3 p, V3 q, float tolerance);

// The links of chain nodes by name: what the operations below change.
using Graph = std::map<uint32_t, std::vector<uint32_t>>;
// The nodes linking to `name`.
std::vector<uint32_t> predecessors(const Graph& graph, uint32_t name);
// A chain's end: no next (no links) or no previous (nothing links to it).
bool is_last(const Graph& graph, uint32_t name);
bool is_first(const Graph& graph, uint32_t name);
// `point` added after `from`'s end: appended (from has no next: from -> point) or prepended (from has no previous:
// point -> from). False: `from` isn't an end.
bool extend(Graph* graph, uint32_t from, uint32_t point);
// `point` put between `from` and its next. False: `from` has no next.
bool insert_after(Graph* graph, uint32_t from, uint32_t point);
// `name` taken out: the nodes linking to it link on to its next (if any, and not already); its links cleared.
void drop_out(Graph* graph, uint32_t name);
// Two ends linked: a (no next) -> b, else b (no next) -> a. False: neither has a free next, or the same node.
bool get_link(Graph* graph, uint32_t a, uint32_t b);
// `name`'s link to its next (its first link) removed: the chain split there. False: none.
bool uncouple(Graph* graph, uint32_t name);
// The points of `name`'s chain (linked either way), `name` first.
std::vector<uint32_t> get_connected(const Graph& graph, uint32_t name);

// Snapping a drawn point: the ray's hit on an upward surface (normal.y >= 0.7) moved to the nearest edge within
// `reach` - the first place, going out in 8 directions in steps of 4, where the ground (ground_at: the ground's height
// below a point 20 above; false: none) falls more than 8 below the hit - half a step back from it. Elsewhere (walls,
// no edge near): the hit itself. `snapped`: whether it moved.
V3 snap_to_edge(V3 hit, V3 normal, float reach, const std::function<bool(V3 from, float* ground_y)>& ground_at,
                bool* snapped);

// The game's grind terrains (Terrain_Types' TERRAIN_GRIND... in scripts\game\terrain.qb), in the script's order.
const std::vector<std::string>& grind_terrain_names();

// Rail speed-ups: the editor's fixed scripts (a .qb image in the loader's old format, the names after them: THAW's
// RailAccelerate / RailAccelerate2 as ed_RailSpeed / ed_RailSpeedLoop, compiled by NeverScript from
//     script ed_RailSpeed {rate = 0.05
//         if TriggerType [3, 13, -1] {
//             KillSpawnedScript name=ed_RailSpeedLoop
//             Obj_SpawnScript ed_RailSpeedLoop params={rate=<rate>}
//         }
//     }
//     script ed_RailSpeedLoop {rate = 0.05
//         OldPos = 0
//         while {
//             if OnRail {
//                 Obj_GetPosition
//                 NewPos = (<Pos>.(0.0, 1.0, 0.0))
//                 if (<NewPos> > <OldPos>) { GetSpeed  SetSpeed (<speed> * (1.0 + <rate>)) }
//                 OldPos = <NewPos>
//                 wait 1 gameframe
//             } else { break }
//         }
//     }
// - the rate passed on to the loop: THAW's passes all of its parameters, params=<...>, which the THAW converter's
// RailAccelerate does too since thaw-railspeed; here only rate=, the one the loop reads).
const std::vector<uint8_t>& rail_speed_qb();
// A point's script: "ed_railspeed_" and its node's checksum in 8 hex digits.
std::string speed_script_name(uint32_t node);
// Its body: the node's script before (`original`, 0: none) called with the arguments passed on - 16 <original> 2C 01
// - then 16 <ed_RailSpeed> 16 <rate> 07 1A <rate> 01. A .qb image of several ("script <name> ... endscript", the names
// after them).
struct SpeedScript {
    uint32_t node;
    uint32_t original;
    float rate;
};
// A speed change's original script and its rate.
struct SpeedChange {
    uint32_t original{};
    float rate{};
};

std::vector<uint8_t> speed_body(SpeedChange speed_change);
std::vector<uint8_t> speed_qb_file(const std::vector<SpeedScript>& scripts);
// A ClimbingNode's `type=` word (Ledge; "" - a ladder - isn't written) and its hang side's (left / right).
bool is_ledge(const Point& point);
const char* hang_word(int hang);

// --- the game side ------------------------------------------------------------------------------------------------

// What the editor has changed in the level's chains now.
State at_hand();
// `state` put in place (from the current one): points added / removed / moved / relinked, level links rewritten or put
// back, points dropped or shown, terrains set or put back; the rail manager built again; objects whose path changed
// made again. False: not in a level.
bool change_phase(const State& state);
// A new level: forgotten (the last level's links put back if its node array is still the one loaded).
void forget_level();
// Once a frame (on or off): objects waiting to be made again.
void tick();

// The rail manager built again (see the top); its node array before is kept until the level goes.
void rebuild_rails();

// A chain point / a chain node (rail, ladder, waypoint).
bool is_chain_node(uint32_t name);
bool is_path(uint32_t name);
bool is_ladder(uint32_t name);
// A point the editor added (moved by the node edits: its pos= and its rail manager node).
bool is_added_point(uint32_t name);
bool move_added_point(uint32_t name, V3 pos);
// The points of `name`'s chain (paths: by links either way among waypoints; rails: the rail manager's).
std::vector<uint32_t> chain_of(uint32_t name);

// The editing operations (the Inspector, keys): each builds the new state and puts it in place. `message` says what
// happened (or why not); `select`: the node to select after (0: as it was).
enum class ChainOp : uint8_t {
    kInsert = 1,       // a point put halfway to the next one
    kDelete = 2,       // the point deleted, its neighbours linked
    kUnlink = 3,       // its link to the next removed
    kTerrain = 4,      // `arg`: the terrain's checksum - for the whole rail
    kExtend = 5,       // drawing goes on from this end (draw_at)
    kLinkArm = 6,      // the next node clicked is linked to this end (Picked)
    kAssignArm = 7,    // (a ped / car) the next waypoint clicked becomes its path's start (Picked)
    kUnassign = 8,     // (a ped / car) no path
    kCancel = 9,       // drawing / an armed pick stopped
    kTurn = 10,        // (a ladder) turned `arg` 15-degree notches (a signed int), the whole ladder
    kFacingHome = 11,  // (a ladder) facing as the level has it
    kTrickObject = 12, // (a drawn rail) `arg` 1: a trick object (its cluster), 0: not - the whole rail
    kGrindFlat = 13, // (a rail) `arg` 1: grindable on flat ground, 0: as the game decides - the whole rail
    kSpeed = 14,     // (a rail point) `arg`: its speed-up's rate in 1/10000ths (0: none) - its segment
    kSpeedRail = 15, // (a rail point) the same for every point of its rail but the last
    kHang = 16,      // (a ledge) `arg` 0 / kHangLeft / kHangRight - the whole ledge
};
bool apply_op(ChainOp op, uint32_t name, uint32_t arg, std::string* message, uint32_t* select);
// Drawing new chains (the Place palette's Rail / Path / Ladder, or Extend): each click a point linked on from the
// last; a ladder: its foot on a wall, then its top.
enum class Blit : uint8_t { kNone, kRail, kRouteWay, kLadder, kLedge };
// A ClimbingNode of Type Ledge (a level's or the editor's).
bool is_ledge_node(uint32_t name);
// Once a frame (Tick) and after every state change: the speed-ups' TriggerScripts in place (the scripts defined, the
// nodes' own TriggerScript set - a node array rebuilt since: set again), those taken off put back. In a network
// session (level_editor_layout.h's Refused) none.
void apply_speeds();
// The speed-ups to draw: each segment (a point and its next where the rail manager has them), its rate, whether it's
// the level's own (a RailAccelerate in its TriggerScript - read only).
struct SpeedSegment {
    V3 a, b;
    float rate;
    bool level;
};
std::vector<SpeedSegment> speed_segments(V3 near, float radius);
// A click while drawing a ladder: the ray from `from` along `dir`. True: the ladder was made (drawing stops).
bool ladder_click(V3 from, V3 dir, float max_t, std::string* message, uint32_t* select);
// The rail manager's RailClear override (GameObjects::set_rail_clear_override): a segment of a rail marked grindable on flat
// ground.
bool flat_grindable(V3 a, V3 b);
void start_drawing(Blit kind);
Blit get_drawing();
// `end`: a chain's free end the click snapped to (magnet_point) - the first click draws on from it, a later one links
// onto it and finishes. True: a point was added.
bool draw_at(V3 at, uint32_t end, std::string* message, uint32_t* select);
void stop_drawing();
// (UX7: auto-rail along an edge - level_editor_arrange.h) A new rail through `points` in order (each a point of its own,
// linked to the next), a trick object of `cluster` (0: of its own first point) as a drawn rail's first click makes it.
// False (`message`: why): fewer than two points, not in a level. `select`: its first point.
bool add_rail(const std::vector<V3>& points, uint32_t cluster, std::string* message, uint32_t* select);
// The point a click in the view gives: the first collision the ray from `from` along `dir` meets (edges snapped:
// snap_to_edge, unless `free`), a rail's end within 24 units taken as it is (`end`: its name - drawing links to it).
bool magnet_point(V3 from, V3 dir, float max_t, bool free, V3* point, uint32_t* end);
// An armed pick (Link / Assign): waiting, and the node clicked.
bool wants_pick();
bool get_picked(uint32_t name, std::string* message, uint32_t* select);
const char* get_waiting(); // (what the armed pick / drawing waits for; null: nothing)

// For the Inspector.
struct View {
    bool show = false;
    bool rail = false, ladder = false, path = false, object = false; // what the node is (object: a ped / car)
    bool added = false;
    bool can_insert_ = false, can_delete_ = false, can_unlink_ = false, can_extend_ = false, can_link_ = false;
    std::string next, prev;            // neighbours' labels
    std::string why;                   // why Delete can't (empty: it can)
    std::vector<std::string> terrains; // the grind terrains (names)
    int terrain = -1;                  // the rail's (index; -1 unknown)
    std::string path_start_;           // an object's path's first waypoint ("": none)
    const char* waiting = nullptr;
    int points = 0; // the chain's points
    // Ladders: its heading (degrees), turned from the level's.
    bool can_turn_ = false, turned = false;
    float facing_degrees_ = 0.0f;
    // Rails: a trick object (its cluster's label), whether that can be switched (drawn rails); grindable on flat ground.
    bool trick_object_ = false, can_trick_object_ = false, grind_flat_ = false;
    std::string cluster;
    // Ledges: a ledge (rail-like tools), its hang side (kHangLeft...). Rails: the point's speed-up (0: none) and the
    // level's own one (its TriggerScript's RailAccelerate: read only, its rate - 0.05 when it gives none).
    bool ledge = false;
    int hang = 0;
    float speed = 0.0f;
    bool level_speed_ = false;
    float level_rate_ = 0.0f;
};
View view_of(uint32_t name);

} // namespace frontend::level_editor::chains
