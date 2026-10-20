#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_nodes.h"

// NOT ORIGINAL - the level editor's added and hidden nodes (ours; see level_editor.h - phase 3 of the plan).
//
// Added nodes live in a node array of their own, the overlay ("thug2_decomp_editor_nodes", a global array symbol made
// once), registered with the node manager after the level's array (CNodeManager::AddNodeArray, not owned). So:
//   - the level's node indexes, its restarts' and crowns' order stay as they are: walks (Obj::GetRestartNodes
//     0x0046B6C0, CFunc_SpawnCrown 0x00538170) go array by array, the overlay last;
//   - the game never parses the overlay (CFunc_ParseNodeArray 0x005A34E0 parses the node manager owner's array, the
//     level's): what it would make for a node is made here - a proximity node (Proxim_AddNode), a sound emitter
//     (Emitter_AddNode: silent until a script plays a sound on it), a level light (0x00490570, as the cutscenes do);
//     restarts, crowns and flags are read by name or by the walks when used;
//   - GetLastNodeArray / GetLastNodeArrayName pass over the overlay (SkateScript::SetEditorNodeArray), and it is
//     taken out with the level's array (the node manager's removal hook);
//   - each change re-registers the array (RemoveNodeArray, its elements set again, AddNodeArray).
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
namespace Front::LevelEditor::NodeCreate {

// --- the pure parts (self-tested: src/hook/selftests_editor_create.cpp) -------------------------------------------

enum class Kind : uint8_t { Spawn, TeamSpawn, Crown, FlagSet, Light, Sound, Proxim };
constexpr int kKinds = 7;
enum class Team : uint8_t { Red, Blue, Green, Yellow };
constexpr int kTeams = 4;
const char* KindName(Kind kind); // "Multiplayer spawn"...
const char* TeamName(Team team); // "Red"...

// A node the editor adds, as the layout's `node` record keeps it (the rest of the node follows from these: Fields).
struct Spec {
    std::string name;  // "ed_restart01", "TRG_CTF_Red"...
    std::string cls;   // Restart, GenericNode, GameObject, LevelLight, EmitterObject, ProximNode
    std::string type;  // MultiPlayer, CTF, Crown, Flag_Red, Flag_Red_Base... ("": none)
    V3 pos = {}, angles = {};
    std::vector<std::string> types; // a restart's restart_types
    int colour[3] = {255, 255, 255}; // a light's Color
    float brightness = 0.0f, inner = 0.0f, outer = 0.0f; // a light's
    float radius = 0.0f; // a proximity node's / an emitter's sphere
};
bool SameSpec(const Spec& a, const Spec& b);

// The kind of a node `spec` describes (false: not one the editor makes).
bool KindOf(const Spec& spec, Kind* kind);
// A new node of `kind` at `pos` facing `yaw` (radians: angles (0, yaw, 0)), named `name` (the flag set's base: Base).
Spec Default(Kind kind, Team team, V3 pos, float yaw, const std::string& name);
// The flag set: its flag and its base (named-driven).
Spec Base(const Spec& flag);
// The fixed names of the name-driven kinds ("" for the others): TRG_CTF_Restart_<Colour>, TRG_CTF_<Colour>.
std::string FixedName(Kind kind, Team team);
// The stem fresh names start with: ed_restart, ed_crown, ed_light, ed_sound, ed_proxim.
const char* NameStem(Kind kind);
// The first of stem01, stem02... (three digits past 99) that `taken` says is free.
std::string FreshName(const std::string& stem, const std::function<bool(const std::string&)>& taken);
// Whether a name may be written in a record (letters, digits, '_', 1-63 characters).
bool ValidName(const std::string& name);

// One component of a node: a flag (no key), a name, a string, an integer, a float, a vector, an array of names or of
// integers.
struct Field {
    enum Type : uint8_t { kFlag, kName, kString, kInteger, kFloat, kVector, kNames, kIntegers } type;
    std::string key;
    std::string text;              // kFlag, kName, kString
    float value[3] = {};           // kFloat (value[0]), kVector
    int integer = 0;               // kInteger
    std::vector<std::string> list; // kNames
    std::vector<int> integers;     // kIntegers
};
// The whole node, in THUG2's own levels' form (as the converters make them): name, pos, angles, class, type and what
// each kind has (CreatedAtStart, RestartName, restart_types, a flag's model and trigger script, a light's colour and
// radii, a sphere's Shape and Radius).
std::vector<Field> Fields(const Spec& spec);

// The game's limits, enforced before adding: CFunc_SpawnCrown collects the crowns into two 10-entry stack arrays,
// GetRestartNode a 256-name one (both unchecked); the light lists hold 256 dynamic and 150 level lights.
constexpr int kMaxCrowns = 10, kMaxRestarts = 256, kMaxDynamicLights = 256, kMaxLevelLights = 150;
struct Counts {
    int crowns = 0, restarts = 0;           // counted as the game counts them (hidden ones not)
    int dynamicLights = 0, levelLights = 0; // the lists' counts now
};
// Why `kind` can't be added now (null: it can).
const char* LimitMessage(Kind kind, const Counts& counts);
// Hiding a restart of `types` (Nodes::RestartType bits) leaves none of Player1 / Multiplayer when `others` (the bits of
// the other restarts counted) lacks one of those it has.
bool HideLeavesNoRestart(uint32_t types, uint32_t others);

// The overlay's nodes in array order, and what undo / redo do to them.
using List = std::vector<Spec>;
int IndexOf(const List& list, const std::string& name);
// Added at the end (its index).
int Add(List* list, const Spec& spec);
// Taken out (false: not there): the spec and where it was.
bool Take(List* list, const std::string& name, Spec* spec, int* index);
// Put back where it was (clamped to the list).
void PutBack(List* list, const Spec& spec, int index);
// The crowns' names in order.
std::vector<std::string> CrownOrder(const List& list);
// Crown `name` swapped with the crown before (delta -1) / after (+1) it (false: none there, or not a crown).
bool MoveCrown(List* list, const std::string& name, int delta);
// The crowns put in `order` in the slots the crowns hold (those `order` doesn't name after, as they were; names that
// aren't crowns of the list skipped).
void ApplyCrownOrder(List* list, const std::vector<std::string>& order);

// --- the game side ------------------------------------------------------------------------------------------------

uint32_t Checksum(const std::string& name);
// The overlay's nodes now (their places as the nodes have them).
List Current();
bool IsAdded(uint32_t name);
// A node of this name in any loaded node array, or an object of it.
bool NameTaken(const std::string& name);
// The counts for LimitMessage now.
Counts CountsNow();
// Why `kind` / `team` can't be placed now (null: it can): the limits, a name-driven node already in the level.
const char* WhyNot(Kind kind, Team team);
// Added (`index`: in the overlay, -1 at the end) - its structure made, the overlay re-registered, the game's
// proximity node / emitter / light made. False (and `message`): a name already there, not in a level.
bool Create(const Spec& spec, int index, std::string* message);
// Several at once (a layout: one re-registration). How many were made.
int CreateAll(const List& specs, std::string* message);
// An added node removed (its spec as it is now and its index: undo puts it back).
bool Remove(uint32_t name, Spec* spec, int* index);
// The overlay's order (by names) set again (undo of a reorder); crowns moved (delta -1 / +1).
std::vector<std::string> Order();
void SetOrder(const std::vector<std::string>& names);
bool MoveCrown(uint32_t name, int delta, std::string* message);
void ApplyCrownOrder(const std::vector<std::string>& order);

// Hiding a level's node (not an added one): whether it can be (`why` when not), and done / undone.
bool CanHide(const Nodes::Node& node, const char** why);
bool Hide(uint32_t name, bool hide);
bool Hidden(uint32_t name);
std::vector<uint32_t> HiddenNames();

// Everything added removed and everything hidden shown (a layout put in place of another).
void ResetAll();
// A new level: forgotten (the overlay's array went with the level's; hidden nodes' structures with it).
void ForgetLevel();

} // namespace Front::LevelEditor::NodeCreate
