#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_nodes.h"

// NOT ORIGINAL - the level editor's added and hidden nodes (ours; see level_editor.h - phase 3 of the plan).
//
// Added nodes live in a node array of their own, the overlay ("thug2_decomp_editor_nodes", a global array symbol made
// once), registered with the node manager after the level's array (CNodeManager::AddNodeArray, not owned). So:
//   - the level's node indexes, its restarts' and crowns' order stay as they are: walks (GameObjects::get_restart_nodes
//     0x0046B6C0, CFunc_SpawnCrown 0x00538170) go array by array, the overlay last;
//   - the game never parses the overlay (cfunc_parse_node_array 0x005A34E0 parses the node manager owner's array, the
//     level's): what it would make for a node is made here - a proximity node (nearby_zone_add_node), a sound emitter
//     (emitter_add_node: silent until a script plays a sound on it), a level light (0x00490570, as the cutscenes do);
//     restarts, crowns and flags are read by name or by the walks when used;
//   - get_last_node_array / get_last_node_array_name pass over the overlay (level_nodes::set_editor_node_array), and it is
//     taken out with the level's array (the node manager's removal hook);
//   - each change re-registers the array (remove_node_array, its elements set again, AddNodeArray).
// Names: fresh `ed_<kind>NN` names, except the name-driven CTF nodes (TRG_CTF_<Colour>, _Base, TRG_CTF_Restart_<Colour>:
// the game finds them by name - refused when the level has them already). Links can only point at named nodes (none of
// the kinds made here has links).
//
// Hidden ("deleted" originals): the node isn't removed - the level's indexes stay - but its class and type become the
// marker `ed_hidden` (the originals kept on the node as ed_hidden_class / ed_hidden_type): the restart walks skip it
// (class), SpawnCrown skips it (type), Create makes nothing of it (0x005A2E50: an unknown class); a proximity node and a
// sound emitter are also made inactive, a light taken out of the light lists (0x0048FCA0), a live flag object killed.
// Home puts it back. A hidden crown isn't counted by the game: the crowns after it each move up one number (their
// order kept).
namespace frontend::level_editor::node_create {

// --- the pure parts -----------------------------------------------------------------------------------------------

enum class Kind : uint8_t { kSpawn, kTeamSpawn, kCrown, kFlagSet, kLight, kSound, kProximityNode };
constexpr int kKinds = 7;
enum class Team : uint8_t { kRed, kBlue, kGreen, kYellow };
constexpr int kTeams = 4;
const char* kind_name(Kind kind);     // "Multiplayer spawn"...
const char* get_team_name(Team team); // "Red"...

// One component of a node: a flag (no key), a name, a string, an integer, a float, a vector, an array of names or of
// integers.
struct Prop {
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): values fixed by a file, a script, the wire or the SDK, used as integers
    enum KindTag : uint8_t { kFlag, kName, kString, kInteger, kFloat, kVector, kNames, kIntegers } type;
    std::string key;
    std::string text;              // kFlag, kName, kString
    std::array<float, 3> value = {};           // kFloat (value[0]), kVector
    int integer = 0;               // kInteger
    std::vector<std::string> list; // kNames
    std::vector<int> integers;     // kIntegers
};
// A node the editor adds, as the layout's `node` record keeps it (the rest of the node follows from these: Fields).
struct Spec {
    std::string name; // "ed_restart01", "TRG_CTF_Red"...
    std::string cls;  // Restart, GenericNode, GameObject, LevelLight, EmitterObject, ProximNode
    std::string type; // MultiPlayer, CTF, Crown, Flag_Red, Flag_Red_Base... ("": none)
    V3 pos = {}, angles = {};
    std::vector<std::string> types;                      // a restart's restart_types
    std::array<int, 3> colour = {{255, 255, 255}};                     // a light's Color
    float brightness = 0.0f, inner = 0.0f, outer = 0.0f; // a light's
    float radius = 0.0f;                                 // a proximity node's / an emitter's sphere
    // A node another of the editor's modules makes in the overlay (level_editor_triggers.h: a trigger volume's
    // node): `extra` fields after the kind's own (its TriggerScript...), and `owned` - its module writes its own
    // layout records, so it isn't a `node` record.
    std::vector<Prop> extra;
    bool owned = false;
    // Chain points (level_editor_chains.h - RailNode, Waypoint, a ladder's ClimbingNode of `type` Ladder): the node
    // whose fields it copies (0: none), a rail's TerrainType ("": the default) and its links= (names: kept out of the
    // node manager's resolve_links - Sync).
    uint32_t like = 0;
    std::string terrain;
    std::vector<uint32_t> links;
};
bool same_spec(const Spec& a, const Spec& b);

// The kind of a node `spec` describes (false: not one the editor makes).
bool kind_of(const Spec& spec, Kind* kind);
// A new node of `kind` at `pos` facing `yaw` (radians: angles (0, yaw, 0)), named `name` (the flag set's base: Base).
Spec default_spec(Kind kind, Team team, V3 pos, float yaw, const std::string& name);
// The flag set: its flag and its base (named-driven).
Spec current_base(const Spec& flag);
// The fixed names of the name-driven kinds ("" for the others): TRG_CTF_Restart_<Colour>, TRG_CTF_<Colour>.
std::string pinned_node_name(Kind kind, Team team);
// A team lobby flag (the feature map's second round): a kFlagSet node (GameObject Flag_<Colour>, its flag model) named
// TRG_Flag_<Colour> - the name SK4_Scripts' Create_Team_Flags makes in a lobby (NodeExists / create name=) - whose
// TriggerScript is its level's TRG_Flag_<Colour>Script (Team_Flag <colour>, as BO's and TR's); no base (the shipped
// levels' lobby flags have none). is_lobby_flag: a kFlagSet spec named so.
std::string lobby_flag_name(Team team);
bool is_lobby_flag(const std::string& name);
// The stem fresh names start with: ed_restart, ed_crown, ed_light, ed_sound, ed_proxim.
const char* name_stem(Kind kind);
// The first of stem01, stem02... (three digits past 99) that `taken` says is free.
std::string fresh_name(const std::string& stem, const std::function<bool(const std::string&)>& taken);
// Whether a name may be written in a record (letters, digits, '_', 1-63 characters).
bool valid_name(const std::string& name);

// The whole node, in THUG2's own levels' form (as the converters make them): name, pos, angles, class, type and what
// each kind has (CreatedAtStart, RestartName, restart_types, a flag's model and trigger script, a light's colour and
// radii, a sphere's Shape and Radius).
std::vector<Prop> get_fields(const Spec& spec);

// The game's limits, enforced before adding: CFunc_SpawnCrown collects the crowns into two 10-entry stack arrays,
// get_spawn_node a 256-name one (both unchecked); the light lists hold 256 dynamic and 150 level lights.
constexpr int kMaxCrowns = 10, kMaxRestarts = 256, kMaxDynamicLights = 256, kMaxLevelLights = 150;
struct Counts {
    int crowns = 0, restarts = 0;               // counted as the game counts them (hidden ones not)
    int dynamic_lights_ = 0, level_lights_ = 0; // the lists' counts now
};
// Why `kind` can't be added now (null: it can).
const char* limit_message(Kind kind, const Counts& counts);
// Hiding a restart of `types` (Nodes::SpawnPointKind bits) leaves none of Player1 / Multiplayer when `others` (the bits of
// the other restarts counted) lacks one of those it has.
bool hide_leaves_no_restart(uint32_t types, uint32_t others);

// The overlay's nodes in array order, and what undo / redo do to them.
using LinkChain = std::vector<Spec>;
int get_index_of(const LinkChain& list, const std::string& name);
// Added at the end (its index).
int append_one(LinkChain* list, const Spec& spec);
// Taken out (false: not there): the spec and where it was.
bool get_take(LinkChain* list, const std::string& name, Spec* spec, int* index);
// Put back where it was (clamped to the list).
void put_back(LinkChain* list, const Spec& spec, int index);
// The crowns' names in order.
std::vector<std::string> crown_order(const LinkChain& list);
// Crown `name` swapped with the crown before (delta -1) / after (+1) it (false: none there, or not a crown).
bool move_crown(LinkChain* list, const std::string& name, int delta);
// The crowns put in `order` in the slots the crowns hold (those `order` doesn't name after, as they were; names that
// aren't crowns of the list skipped).
void apply_crown_order(LinkChain* list, const std::vector<std::string>& order);

// --- the game side ------------------------------------------------------------------------------------------------

uint32_t name_hash(const std::string& name);
// The overlay's nodes now (their places as the nodes have them).
LinkChain at_hand();
bool is_added(uint32_t name);
// A node of this name in any loaded node array, or an object of it.
bool name_taken(const std::string& name);
// The counts for limit_message now.
Counts counts_now();
// Why `kind` / `team` can't be placed now (null: it can): the limits, a name-driven node already in the level.
const char* get_why_not(Kind kind, Team team);
// Why a team lobby flag of `team` can't be placed now (null: it can) - the level has one by that name.
const char* why_not_lobby_flag(Team team);
// Added (`index`: in the overlay, -1 at the end) - its structure made, the overlay re-registered, the game's
// proximity node / emitter / light made. False (and `message`): a name already there, not in a level.
bool fabricate(const Spec& spec, int index, std::string* message);
// An added node's links= (names) set: its spec and its structure (chain points - level_editor_chains.h). False: not
// an added node.
bool set_links(uint32_t name, const std::vector<uint32_t>& links);
// Several at once (a layout: one re-registration). How many were made.
int create_all(const LinkChain& specs, std::string* message);
// An added node removed (its spec as it is now and its index: undo puts it back).
bool drop_out(uint32_t name, Spec* spec, int* index);
// The overlay's order (by names) set again (undo of a reorder); crowns moved (delta -1 / +1).
std::vector<std::string> get_order();
void set_order(const std::vector<std::string>& names);
bool move_crown(uint32_t name, int delta, std::string* message);
void apply_crown_order(const std::vector<std::string>& order);

// Hiding a level's node (not an added one): whether it can be (`why` when not), and done / undone.
bool can_hide(const nodes::Elem& node, const char** why);
bool conceal(uint32_t name, bool hide);
bool get_hidden(uint32_t name);
std::vector<uint32_t> hidden_names();

// Another module told of every node array the node manager takes out (before the overlay's own handling - the level's
// array going takes the overlay with it): level_editor_triggers.h drops its trigger volumes with their level. Sets the
// node manager's hook (this module's).
void set_array_removed_listener(void (*listener)(uint32_t name));

// Everything added removed and everything hidden shown (a layout put in place of another).
void reset_all();
// A new level: forgotten (the overlay's array went with the level's; hidden nodes' structures with it).
void forget_level();

} // namespace frontend::level_editor::node_create
