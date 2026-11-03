#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/maths/vec2.h"
#include "editor/level_editor.h"

namespace script {
class Struct;
}

// NOT ORIGINAL - the level editor's node layer (ours; see level_editor.h): every node of the level's node arrays (the
// node manager's, engine/script/node_manager.h - what the game parsed at the level's load), sorted into families by
// class and type, with what the gizmos need (position, heading, links, colour, radius), and the pure parts of
// classifying and picking them. Read here; point nodes are moved and
// turned by level_editor_node_edit.h (phase 2 of the plan).
namespace frontend::level_editor::nodes {

// The families, as the Outliner groups them and the chips filter them.
enum class Family : uint8_t {
    kSpawn,    // Restart
    kCrown,    // GenericNode Type=Crown (King of the Hill: the n-th crown in node order)
    kBitMark,  // GameObject Type=Flag_<colour>[_Base] (CTF and team flags)
    kRail,     // RailNode (chains by Links)
    kLadder,   // ClimbingNode
    kRouteWay, // Waypoint (peds' and cars' paths)
    kObject,   // LevelObject, GameObject, Pedestrian, Vehicle
    kScenery,  // LevelGeometry
    kLight,    // LevelLight
    kSound,    // EmitterObject (sound emitters)
    kParticle, // ParticleObject
    kFireOff,  // ProximNode
    kOther,    // anything else (GenericNode, unknown classes)
};
constexpr int kFamilies = 13;
const char* family_name(Family family); // plural, for the Outliner ("Spawns")
inline uint32_t get_bit(Family family) { return 1u << static_cast<int>(family); }
constexpr uint32_t kAllFamilies = (1u << kFamilies) - 1;

// The family of a node's class= and type= (checksums).
Family get_classify(uint32_t cls, uint32_t type);
// How a family is drawn: a point (an icon and a heading), a chain (lines along its links), a volume (as a point, with
// a sphere when it has a radius) or nothing (scenery: its own geometry shows it).
enum class Shape : uint8_t { kPoint, kChain, kLoudness, kNone };
Shape shape_of(Family family);

// A restart's restart_types as bits.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): values fixed by a file, a script, the wire or the SDK, used as integers
enum SpawnPointKind : uint8_t {
    kPlayer1 = 1,
    kPlayer2 = 2,
    kMultiplayer = 4,
    kHorse = 8,
    kCtf = 16,
    kTeam = 32,
    kKing = 64,
    kGeneric = 128,
};
uint32_t restart_type_bit(uint32_t kind_hash);
// A team colour named in a node's name or type (_Red, _Blue, _Green, _Yellow; ARGB), 0 if none.
uint32_t team_colour(const char* text);
// A spawn's gizmo colour: its team's (CTF / team restarts), else by type - multiplayer blue, player 1 green, player 2
// lime, horse orange, others grey.
uint32_t spawn_colour(uint32_t types, uint32_t team);

// --- the level's nodes ---

struct Elem {
    int array = 0, index = 0; // the node manager's array and the node's index in it
    uint32_t name = 0, cls = 0, type = 0;
    Family family = Family::kOther;
    bool has_pos_ = false;
    V3 pos = {}, angles = {};
    V3 forward = {.x = 0.0f, .y = 0.0f, .z = 1.0f};           // its heading (the matrix the game makes of its angles: its at row)
    std::vector<int> links;                    // linked nodes (indices into All())
    uint32_t colour = 0xFFC0C0C0;              // ARGB
    int order = -1;                            // crowns: the index among the crowns; spawns: among the restarts
    float radius = 0.0f, inner_radius_ = 0.0f; // a light's outer / inner radius, a proximity node's radius
    uint32_t restart_types_ = 0;
    uint32_t terrain = 0;
    bool trick_object_ = false;
    bool trigger_script_ = false;
    bool added = false;  // in the editor's overlay array (level_editor_node_create.h)
    bool hidden = false; // hidden by the editor: class / type are the marker's, cls / type here its own
};

// The editor's overlay array and the hidden marker (level_editor_node_create.h): its class and type, and the
// components that keep a hidden node's own.
extern const char* const kOverlayArrayName; // "thug2_decomp_editor_nodes"
uint32_t overlay_array();
uint32_t hidden_marker();    // ed_hidden
uint32_t hidden_class_key(); // ed_hidden_class
uint32_t hidden_type_key();  // ed_hidden_type

// The nodes (scanned again when the node arrays change: their names, sizes or count) - empty outside a level.
const std::vector<Elem>& get_all();
// The count of each family (hidden ones too).
const std::array<int, kFamilies>& get_counts();
// How many of a family the game counts (spawns, crowns: hidden ones not).
int walked(Family family);
// A number that changes whenever the nodes are scanned again (the UI's caches key on it).
uint32_t get_generation();
// The next All() scans again (a node hidden or shown: the arrays' sizes didn't change).
void mark_stale();
// The node named `name` (its index in All(), -1 if none).
int get_find(uint32_t name);
// The node's structure (null if the arrays changed).
script::Struct* struct_of(const Elem& node);
// Node `index`'s place read again from its structure (pos, angles, heading) - after the editor moved it: the scan is
// made again only when the arrays' names or sizes change.
void reread(int index);
// The node array's name (for the Inspector).
uint32_t get_array_name(const Elem& node);
// "TRG_Restart01" / its checksum in hex.
std::string caption(uint32_t checksum);

// A node's fields for the Inspector: each component (the included structures - ncomp_<class>_n - expanded), names
// resolved where they're known.
struct Prop {
    std::string name, value;
    bool included; // (from an included structure)
};
std::vector<Prop> get_fields(const Elem& node);

// Whether the level loaded is one of the ported levels (tools/pipeline/menu_level's lists, level_select_menu_<game>_level_info),
// which are shared with THUGPRO: their spawns, crowns and flags are what net games use. `game`: "THPS3", "THPS4",
// "THUG1" or "THAW".
bool level_is_shared(const char** game);
// Whether a family matters online (spawns, crowns, flags).
inline bool get_online(Family family)
{
    return family == Family::kSpawn || family == Family::kCrown || family == Family::kBitMark;
}

// --- picking (pure) ---

// Something drawn, in back buffer pixels: a point (a..a) or a segment (a..b) of node `node`, `depth` along the view
// (a segment's: at its middle; `depth_a_` / `depth_b_` at its ends - none given: `depth` all along).
struct Pickable {
    int node{};
    float ax{}, ay{}, bx{}, by{};
    bool segment{};
    float depth{};
    float depth_a_ = -1.0f, depth_b_ = -1.0f;
};
// The first occluding surface under the cursor, along the view (level_editor_picking.h's gizmo_in_front): gizmos further
// than `depth` and the slack (gizmo_slack: `unitsPerPixel` at depth 1, `pixels` of reach) aren't picked.
struct Occluder {
    bool blocked = false;
    float depth = 0.0f;
    float units_per_pixel_ = 0.0f;
    float pixels = 0.0f;
};
// The node under (x, y): the nearest within `radius` pixels (points first: a point within the radius beats a segment;
// equals go to the nearer to the camera), not behind `occluder` (a segment's depth where it passes nearest to (x, y)).
// -1 if none.
int pick_nearest(const std::vector<Pickable>& pickables, maths::Vec2 at, float radius, const Occluder& occluder = {});

} // namespace frontend::level_editor::nodes
