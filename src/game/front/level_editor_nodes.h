#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

namespace Script {
class CStruct;
}

// NOT ORIGINAL - the level editor's node layer (ours; see level_editor.h): every node of the level's node arrays (the
// node manager's, game/script/node_manager.h - what the game parsed at the level's load), sorted into families by
// class and type, with what the gizmos need (position, heading, links, colour, radius), and the pure parts of
// classifying and picking them (self-tested: src/hook/selftests_editor_ui.cpp). Read here; point nodes are moved and
// turned by level_editor_node_edit.h (phase 2 of the plan).
namespace Front::LevelEditor::Nodes {

// The families, as the Outliner groups them and the chips filter them.
enum class Family : uint8_t {
    Spawn,    // Restart
    Crown,    // GenericNode Type=Crown (King of the Hill: the n-th crown in node order)
    Flag,     // GameObject Type=Flag_<colour>[_Base] (CTF and team flags)
    Rail,     // RailNode (chains by Links)
    Ladder,   // ClimbingNode
    Path,     // Waypoint (peds' and cars' paths)
    Object,   // LevelObject, GameObject, Pedestrian, Vehicle
    Scenery,  // LevelGeometry
    Light,    // LevelLight
    Sound,    // EmitterObject (sound emitters)
    Particle, // ParticleObject
    Trigger,  // ProximNode
    Other,    // anything else (GenericNode, unknown classes)
};
constexpr int kFamilies = 13;
const char* FamilyName(Family family); // plural, for the Outliner ("Spawns")
inline uint32_t Bit(Family family) { return 1u << static_cast<int>(family); }
constexpr uint32_t kAllFamilies = (1u << kFamilies) - 1;

// The family of a node's class= and type= (checksums).
Family Classify(uint32_t cls, uint32_t type);
// How a family is drawn: a point (an icon and a heading), a chain (lines along its links), a volume (as a point, with
// a sphere when it has a radius) or nothing (scenery: its own geometry shows it).
enum class Shape : uint8_t { Point, Chain, Volume, None };
Shape ShapeOf(Family family);

// A restart's restart_types as bits.
enum RestartType : uint32_t {
    kPlayer1 = 1,
    kPlayer2 = 2,
    kMultiplayer = 4,
    kHorse = 8,
    kCtf = 16,
    kTeam = 32,
    kKing = 64,
    kGeneric = 128,
};
uint32_t RestartTypeBit(uint32_t typeChecksum);
// A team colour named in a node's name or type (_Red, _Blue, _Green, _Yellow; ARGB), 0 if none.
uint32_t TeamColour(const char* text);
// A spawn's gizmo colour: its team's (CTF / team restarts), else by type - multiplayer blue, player 1 green, player 2
// lime, horse orange, others grey.
uint32_t SpawnColour(uint32_t types, uint32_t team);

// --- the level's nodes ---

struct Node {
    int array = 0, index = 0; // the node manager's array and the node's index in it
    uint32_t name = 0, cls = 0, type = 0;
    Family family = Family::Other;
    bool hasPos = false;
    V3 pos = {}, angles = {};
    V3 forward = {0.0f, 0.0f, 1.0f}; // its heading (the matrix the game makes of its angles: its at row)
    std::vector<int> links;          // linked nodes (indices into All())
    uint32_t colour = 0xFFC0C0C0;    // ARGB
    int order = -1;                  // crowns: the index among the crowns; spawns: among the restarts
    float radius = 0.0f, innerRadius = 0.0f; // a light's outer / inner radius, a proximity node's radius
    uint32_t restartTypes = 0;
    uint32_t terrain = 0;
    bool trickObject = false;
    bool triggerScript = false;
    bool added = false;  // in the editor's overlay array (level_editor_node_create.h)
    bool hidden = false; // hidden by the editor: class / type are the marker's, cls / type here its own
};

// The editor's overlay array and the hidden marker (level_editor_node_create.h): its class and type, and the
// components that keep a hidden node's own.
extern const char* const kOverlayArrayName; // "thug2_decomp_editor_nodes"
uint32_t OverlayArray();
uint32_t HiddenMarker();      // ed_hidden
uint32_t HiddenClassKey();    // ed_hidden_class
uint32_t HiddenTypeKey();     // ed_hidden_type

// The nodes (scanned again when the node arrays change: their names, sizes or count) - empty outside a level.
const std::vector<Node>& All();
// The count of each family (hidden ones too).
const int* Counts();
// How many of a family the game counts (spawns, crowns: hidden ones not).
int Walked(Family family);
// A number that changes whenever the nodes are scanned again (the UI's caches key on it).
uint32_t Generation();
// The next All() scans again (a node hidden or shown: the arrays' sizes didn't change).
void Invalidate();
// The node named `name` (its index in All(), -1 if none).
int Find(uint32_t name);
// The node's structure (null if the arrays changed).
Script::CStruct* StructOf(const Node& node);
// Node `index`'s place read again from its structure (pos, angles, heading) - after the editor moved it: the scan is
// made again only when the arrays' names or sizes change.
void Reread(int index);
// The node array's name (for the Inspector).
uint32_t ArrayName(const Node& node);
// "TRG_Restart01" / its checksum in hex.
std::string Label(uint32_t checksum);

// A node's fields for the Inspector: each component (the included structures - ncomp_<class>_n - expanded), names
// resolved where they're known.
struct Field {
    std::string name, value;
    bool included; // (from an included structure)
};
std::vector<Field> Fields(const Node& node);

// Whether the level loaded is one of the ported levels (tools/menu_level's lists, level_select_menu_<game>_level_info),
// which are shared with THUGPRO: their spawns, crowns and flags are what net games use. `game`: "THPS3", "THPS4",
// "THUG1" or "THAW".
bool LevelIsShared(const char** game);
// Whether a family matters online (spawns, crowns, flags).
inline bool Online(Family family) { return family == Family::Spawn || family == Family::Crown || family == Family::Flag; }

// --- picking (pure) ---

// Something drawn, in back buffer pixels: a point (a..a) or a segment (a..b) of node `node`, `depth` along the view
// (a segment's: at its middle; `depthA` / `depthB` at its ends - none given: `depth` all along).
struct Pickable {
    int node;
    float ax, ay, bx, by;
    bool segment;
    float depth;
    float depthA = -1.0f, depthB = -1.0f;
};
// The first occluding surface under the cursor, along the view (level_editor_picking.h's GizmoInFront): gizmos further
// than `depth` and the slack (GizmoSlack: `unitsPerPixel` at depth 1, `pixels` of reach) aren't picked.
struct Occluder {
    bool blocked = false;
    float depth = 0.0f;
    float unitsPerPixel = 0.0f;
    float pixels = 0.0f;
};
// The node under (x, y): the nearest within `radius` pixels (points first: a point within the radius beats a segment;
// equals go to the nearer to the camera), not behind `occluder` (a segment's depth where it passes nearest to (x, y)).
// -1 if none.
int PickNearest(const std::vector<Pickable>& pickables, float x, float y, float radius, const Occluder& occluder = {});

} // namespace Front::LevelEditor::Nodes
