#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_ui.h"

// NOT ORIGINAL - the level editor's trigger volumes, kill zones, teleports and gaps (ours; see level_editor.h - phase 5
// of the plan). Edited levels only, offline: everything here is put in place by the editor or a layout, and neither runs
// in a network session (level_editor_layout.h).
//
// How the game's triggers work (THUG2PM.exe):
//   - A trigger face is a collision face with flag 0x40; with 0x10 too (not collidable) the skater passes through it -
//     a trigger volume (kill zones, water, gap planes). The trigger component's update (0x00434D90: an object that
//     moved since its last frame) casts a feeler from where it was to where it is with SetIgnore(0, 0x50) - only faces
//     with both flags - static and movable collision alike (GetCollision(1, 0)), its callback 0x00434D70 handing each
//     face hit to 0x00434BF0: the hit's collision object's checksum (CollisionHit +0x34, the feeler's +0x58) is a
//     node's name, and CTriggerComponent::TriggerNode (0x00434A60) runs that node's TriggerScript on the object. A
//     level's trigger sectors are LevelGeometry nodes (usually collision only) named like their sector.
//   - A kill zone is a trigger whose script kills: SK3_KillSkater / SK3_KillSkater_Water / ..._Pungee nodename=<a
//     restart> (sk4_scripts.qb: the panel message, then SK3_Killskater_Finish -> KillSkater node=); a teleport's calls
//     TeleportSkaterToNode nodename=<a node> (Obj_MoveToNode); gaps are StartGap GapID= flags=[...] on one trigger and
//     EndGap GapID= score= text= on another (a level's <level>_scripts.qb).
//   - The gap checklist (the pause menu's gap list) is built at the end of CFunc_ParseNodeArray (0x005A34E0, unless the
//     script integer 0xF7ECD937 is set): the skate module's checklist (0x005656C0 - the career's for the level) Build
//     (0x00578AC0): every EndGap any node's TriggerScript reaches (SkateScript::ForEachNodeScriptCall - every node
//     array, the editor's overlay too) with a score and a text.
//
// What the editor adds:
//   - A trigger volume is a box: a node in the editor's overlay array (level_editor_node_create.h: class GenericNode,
//     type ed_trigger - the Triggers family - with its TriggerScript), centred on the node's pos and turned by its
//     heading, so it moves, turns, drops, undoes and goes Home as other point nodes do (level_editor_node_edit.h); and
//     its collision: a movable box (Nx::CCollMovBBox, its checksum the node's name, every face 0x50) on the movable
//     collision list (Nx::RegisterMovable) - the trigger component's feeler meets it as it meets a level's trigger
//     faces (no collision cache is current then: the skater's physics sets one only for its own feelers), and a feeler
//     ignoring 0x10 (every other one but the graffiti feelers') never does. A box is hit where a line enters it, so it
//     fires once on the way in.
//   - Its script is made here: compiled .qb tokens (Body) defined through the game's own loader (Script::LoadQBData,
//     the file name "editor\thug2_decomp_triggers.qb") as <node>Script - except "Run a script", whose TriggerScript is
//     the script chosen.
//   - Kinds: Kill (SK3_KillSkater, or SK3_KillSkater_Water: the splash) back at a restart; Teleport (TeleportSkaterToNode
//     to a restart / node, an arrow drawn to it); Run a script (one of the level's: those its nodes' TriggerScripts
//     name, or any typed); Gap start / Gap end (made as a pair, one GapID: StartGap with its flags, EndGap with the
//     gap's name and score). After each change the gap checklist is built again as the level's load does (Build: new
//     gaps added) and the editor's gaps no longer there taken out of it (not a level's own gap of the same name).
//   - A level's own trigger volumes are found from their collision (faces with 0x40), their node's TriggerScript walked
//     (SkateScript::ForEachScriptCall) for the calls above - drawn coloured by kind, an arrow from a teleport to its
//     target. They can be turned off (their collision object's flag 4, which the world queries skip: layout record
//     `trigger_off`) or given an editable copy: an editor trigger with the same TriggerScript over the faces' box,
//     the original off while the copy is there (its `replaces`). Moving or resizing a level's own volume in place
//     would mean moving collision-only sectors, which the editor's scenery code can't yet (level_editor_view.h).
//   - Saved as layout records (still version 2; older readers skip them):
//       trigger <name> <kind> pos=x,y,z yaw=r size=x,y,z [target=<node>] [water=1] [script=<script>] [gap=<GapID>]
//               [flags=A,B] [score=n] [replaces=<checksum>] [text="..."]
//       trigger_off <checksum>
//   - The level's node array leaving takes everything with it (NodeCreate's listener): the boxes unregistered, the
//     editor's gaps taken out of that level's checklist.
namespace Front::LevelEditor::Triggers {

// --- the pure parts (self-tested: src/hook/selftests_editor_triggers.cpp) -----------------------------------------

enum class Kind : uint8_t { Kill, Teleport, Script, GapStart, GapEnd, Other };
constexpr int kKinds = 6;
const char* KindName(Kind kind); // "Kill zone"...
const char* KindWord(Kind kind); // the record's: kill, teleport, script, gapstart, gapend, other
bool KindFromWord(const std::string& word, Kind* kind);
uint32_t KindColour(Kind kind); // ARGB

// What a trigger's script calls (the calls found walking it and the scripts it calls).
struct Calls {
    bool kill = false, water = false, teleport = false, startGap = false, endGap = false;
    uint32_t target = 0; // a kill's / teleport's nodename=
    uint32_t gapId = 0;  // StartGap / EndGap GapID=
    std::string gapText;
    int gapScore = 0;
};
// The kind a script's calls make it (in this order: kill, teleport, gap end, gap start; else Script when it has one,
// Other when it has none).
Kind KindOfCalls(const Calls& calls, bool hasScript);

// The gap flags StartGap is given in THUG2's levels (the checkboxes offered).
const std::vector<const char*>& GapFlags();

// A trigger volume the editor added (its node's place - pos, heading - is the node's).
struct Spec {
    std::string name;                // its node in the overlay ("ed_trigger01")
    Kind kind = Kind::Kill;
    V3 size = {400.0f, 200.0f, 400.0f}; // the box's extents, centred on the node, turned by its heading
    bool water = false;              // Kill: SK3_KillSkater_Water (the splash)
    std::string target;              // Kill: the restart; Teleport: the node it sends the skater to
    std::string script;              // Script: the script it runs
    std::string gap;                 // GapStart / GapEnd: the GapID (the two ends share it)
    std::vector<std::string> flags;  // the gap's flags (StartGap's)
    std::string text;                // the gap's name (EndGap's text - the gap list's)
    int score = 100;                 // the gap's score
    uint32_t replaces = 0;           // a level trigger this one stands in for (off while this one is there)
};
bool SameSpec(const Spec& a, const Spec& b);
// A name's checksum: "0x" and 8 hex digits are a checksum without a known name, anything else is a name.
uint32_t ChecksumOf(const std::string& name);
// The name written for a checksum (`known`: its name, if any) - the name, else "0x" and 8 hex digits.
std::string NameFor(uint32_t checksum, const char* known);
// The script the editor makes for it and its node's TriggerScript names: "<name>Script" (Run a script: one that
// calls the script chosen - its TriggerScript never changes).
std::string ScriptName(const Spec& spec);
// Why it does nothing yet (null: it works): what it lacks.
const char* Problem(const Spec& spec);
// A gap's name as the gap list shows it: printable, no quotes or '#' (the layout's comment mark), at most 60 characters.
std::string CleanText(const std::string& text);
// Sizes kept within 10 .. 50000 units.
V3 ClampSize(V3 size);
// The box placed: centred on `pos`, turned by `yaw` (radians about the world's up axis, as a node's heading).
OrientedBox BoxOf(V3 pos, float yaw, V3 size);

// The compiled script: its statements (each ending with an end of line, 0x01) - e.g. StartGap GapID=x flags=[a, b]:
// 16 <StartGap> 16 <GapID> 07 16 <x> 16 <flags> 07 05 16 <a> 09 16 <b> 06 01. Empty while Problem says why.
std::vector<uint8_t> Body(const Spec& spec);
// A .qb file (the loader's old format) defining every spec's script ("script <name> ... endscript": 23 16 <name> 01
// <body> 24 01), the checksums' names after them (2B <checksum> <text> 00), then the end (00).
std::vector<uint8_t> QbFile(const std::vector<Spec>& specs);

// A trigger placed (the layout's record: the spec and its node's place).
struct Placed {
    Spec spec;
    V3 pos = {};
    float yaw = 0.0f;
};
// The record's text after "trigger " and back (false: not a record this reads - a bad value, a repeated key, no place).
std::string Record(const Placed& placed);
bool ReadRecord(const std::string& text, Placed* placed);

// The other end of a gap: the index in `specs` of a trigger with the same GapID and the other end's kind (-1: none).
int Partner(const std::vector<Spec>& specs, int index);

// --- the game side ------------------------------------------------------------------------------------------------

// A level's own trigger volume (its collision's faces with 0x40).
struct LevelTrigger {
    uint32_t sector = 0;   // its sector's checksum (its node's name)
    V3 min = {}, max = {}; // the trigger faces' box
    std::vector<V3> faces; // their corners (3 a face; the first 64 faces)
    int faceCount = 0;
    bool hasNode = false;
    uint32_t script = 0;   // its node's TriggerScript (0: none)
    Calls calls;
    Kind kind = Kind::Other;
    bool off = false;      // turned off by the editor (or while an editable copy stands in for it)
};
const std::vector<LevelTrigger>& LevelTriggers();

// The editor's triggers (alive: their node is in the overlay).
struct Added {
    Spec spec;
    bool alive = false;
    V3 pos = {};
    float yaw = 0.0f;
    const char* problem = nullptr;
    Calls calls;  // (Run a script: what the chosen script calls)
};
const std::vector<Added>& AddedTriggers();
// Whether `name` is one of the editor's trigger nodes.
bool IsTrigger(uint32_t name);

// The editor starting / stopping; once a frame while it runs (the camera's place and heading: where new triggers go).
void Start();
void Stop();
void Tick(V3 camera, V3 forward, float yaw);
// Every frame, the editor on or off: in a network session (level_editor_layout.h's NetworkActive) the boxes are taken
// out at once - edited levels are offline only (a layout never applies there; this is a second guard).
void Guard();
// T: the volumes shown (and the Triggers window); [editor] triggers= in the ini.
bool Shown();
void SetShown(bool shown);

// The volumes drawn over the view (lines, labels).
void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Text>* texts);
// A click while they're shown: the nearest volume under the ray - an editor trigger's node, or a level trigger's node
// (Nodes::All() index; -1: none, or a level trigger without a node - then it's the one inspected, `sector`).
bool Pick(V3 origin, V3 unitDir, int* node, uint32_t* sector);
// The level trigger shown in the Triggers window (0: none) - picked, or chosen in its list.
uint32_t InspectedSector();
void Inspect(uint32_t sector);

// The UI (level_editor_triggers_ui.cpp, ImGui): the Triggers window (T) and the Inspector's section for a trigger
// node (`node`: Nodes::All() index).
void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale, int selectedNode);
void InspectorSection(int node);
// What the UI asks for, done in the next Tick (the UI draws after the editor's update).
void Queue(std::function<void()> action);
// The UI's actions (queued): a trigger added where the view looks - a kill zone (water: SK3_KillSkater_Water) back at
// the nearest restart, a teleport to the nearest restart, one that runs a script (none chosen yet), a gap (its start
// there, its end 600 units further on) - and selected; one undo step each.
void AddKill(bool water);
void AddTeleport();
void AddScript();
void AddGap();
// A trigger's settings changed (`undo`: a step of its own - else BeginEdit started one, e.g. a field being dragged).
// A gap's name, score and flags go to both its ends.
void SetSpec(const std::string& name, const Spec& spec, bool undo);
void BeginEdit();
// A level trigger turned off / on (layout `trigger_off`), or given an editable copy (off while the copy is there).
void TurnOff(uint32_t sector, bool off);
void MakeEditable(uint32_t sector);
// The other end of a gap selected; a node selected (by name).
void SelectPartner(const std::string& name);
void SelectNodeNamed(uint32_t name);
// A level trigger described ("Kill zone (water) -> TRG_Restart01").
std::string DescribeLevelTrigger(const LevelTrigger& trigger);

// Undo: the whole trigger state (the editor's triggers - their specs and nodes - and the level triggers turned off).
struct State {
    std::vector<Spec> specs;
    std::vector<NodeCreate::Spec> nodes;
    std::vector<uint32_t> off;
};
State Save();
void Restore(const State& state);
// A change made since the last call: the state before it (the editor pushes it as one undo step).
bool TakeUndo(State* before);
// A trigger to select (the one just added; -1: none) - a Nodes::All() index.
int TakeSelect();

// The layout (level_editor_layout.h): the triggers as they are now, put in place (how many), everything removed.
std::vector<Placed> CaptureAdded();
std::vector<uint32_t> CaptureOff();
int Apply(const std::vector<Placed>& placed, const std::vector<uint32_t>& off, std::string* message);
void ResetAll();

// For the Inspector, the help and the screenshot sidecar: a line about the triggers.
std::string Describe();

} // namespace Front::LevelEditor::Triggers
