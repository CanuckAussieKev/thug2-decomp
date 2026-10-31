#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_ui.h"
#include "game/front/level_editor_walls.h"

namespace Game {
class CGapChecklist;
}

// NOT ORIGINAL - the level editor's trigger volumes, kill zones, teleports and gaps (ours; see level_editor.h - phase 5
// of the plan). Edited levels only, offline: everything here is put in place by the editor or a layout, and neither runs
// in a network session (level_editor_layout.h).
//
// How the game's triggers work (the reference build):
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
//     target. Their collision is switched off / on and (collision only - an invisible wall) moved and turned through
//     level_editor_walls.h (the game's own switch, CSector::SetActive; their `wall` records) - only when every face of
//     it is a trigger's or it's a wall, so nothing solid goes with it. Or they get an editable copy: an editor trigger
//     with the same TriggerScript over the faces' box (resizable, any kind), the original switched off while the copy
//     is there (its `replaces`).
//   - Saved as layout records (still version 2; older readers skip them):
//       trigger <name> <kind> pos=x,y,z yaw=r size=x,y,z [target=<node>] [water=1] [script=<script>] [gap=<GapID>]
//               [flags=A,B] [score=n] [replaces=<checksum>] [seconds=s] [text="..."]
//     (map4: kind `camera` - target= an editor camera, seconds= the shot: LevelEditor_PlayCamera, level_editor_cameras.h)
//     (a level trigger switched off or moved: its `wall` record - level_editor_walls.h)
//   - The level's node array leaving takes everything with it (NodeCreate's listener): the boxes unregistered, the
//     editor's gaps taken out of that level's checklist.
namespace Front::LevelEditor::Triggers {

// --- the pure parts -----------------------------------------------------------------------------------------------

// (the feature map added Boost and Break after Other: the record words of the older kinds keep their places)
enum class Kind : uint8_t { Kill, Teleport, Script, GapStart, GapEnd, Other, Boost, Break, Camera };
constexpr int kKinds = 9;
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
    // (the feature map) trigger_jump_boost (its speed= or multi=), Shatter / ShatterAndDie (a breakable), THAW's
    // RailAccelerate (a rail's speed-up: its rate=, else THAW's default 0.05)
    bool boost = false, shatter = false, railSpeed = false;
    float boostSpeed = 0.0f, boostMulti = 0.0f, railRate = 0.0f;
    // (map4) LevelEditor_PlayCamera camera=<node> (level_editor_cameras.h): the camera played
    bool camera = false;
    uint32_t cameraNode = 0;
};
// The kind a script's calls make it (in this order: kill, teleport, gap end, gap start, a jump boost, a breakable;
// else Script when it has one, Other when it has none).
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
    // Fires on the way out as well (the record's exit=1): the game's trigger component only reports a box where a line
    // enters it (a CCollMovBBox is met on the way in), so leaving is seen by Play's inside test on the skater each
    // frame - its TriggerScript run on the skater as the trigger component runs it (0x00434A60).
    bool exit = false;
    // Boost (a jump boost / launch pad - THUG2's own skutils.qb trigger_jump_boost, as THAW's Ruins and BE's fans use
    // it: `trigger_jump_boost speed=<n>` sets the skater's upward velocity to n, its sideways kept): the strength.
    int strength = 900;
    // Break (a breakable - "breaks when hit"): `target` the scenery piece (its sector's name); the box is its box (where
    // it is now, grown a little) and it stands in for its collision (`replaces` - the piece's collision off while it's
    // breakable, so the skater goes through as through THUG2's glass, whose faces are 0x50 too); the script plays
    // `sound` (none: "") and shatters the piece as BO_ShatteringGlass does (Shatter name= area= ... then Kill name=).
    std::string sound;
    // (map4) Camera: `target` an editor camera node (level_editor_cameras.h - Type=ed_camera; any node with
    // targetOffset / positionOffset, or Pos / Angles, will do); the script plays it for `seconds` - LevelEditor_PlayCamera
    // camera=<target> time=<seconds>, the game's own virtual_cam (PlaySkaterCamAnim) - then the skater's camera is back.
    float seconds = 3.0f;
};
// Camera shots kept within these (seconds).
constexpr float kCameraMinSeconds = 0.5f, kCameraMaxSeconds = 60.0f;
float ClampSeconds(float seconds); // (not a number: 3)
// Break's sounds offered (THUG2's shared hits most levels load: Shared\Hits\...; any other may be typed).
const std::vector<const char*>& BreakSounds();
// Jump boost strengths kept within these.
constexpr int kBoostMin = 100, kBoostMax = 5000;
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
// Whether `p` is inside a placed box (its faces included).
bool Inside(const OrientedBox& box, V3 p);
// The leaving test, a frame at a time: whether a skater inside last frame (`was`) and not now fires the exit; `known`
// false (the first frame seen - the editor just closed, the trigger just made) never fires.
inline bool Left(bool known, bool was, bool now) { return known && was && !now; }

// Resize handles: one on the middle of each face of a box - handle 0 / 1 the box's -x / +x face (its own axes), 2 / 3
// -y / +y, 4 / 5 -z / +z. Where a handle is, and its face's outward direction (unit).
V3 HandlePoint(V3 pos, float yaw, V3 size, int handle);
V3 HandleDirection(float yaw, int handle);
// A handle dragged `along` units outwards (negative: inwards): the opposite face stays where it is - the size along
// that axis grows by `along` (kept within ClampSize's limits) and the centre moves out by half of what it grew.
void ResizeFace(V3 pos, float yaw, V3 size, int handle, float along, V3* newPos, V3* newSize);

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

// --- proximity nodes (the feature map) ---
//
// A ProximNode (the editor's or a level's) runs ONE script - its TriggerScript, kept by the game in its ProximNode
// (+0xA0, read at Proxim_AddNode) - both when a skater comes in and when it goes out; the script tells them apart
// with `if Inside` (the level's own: BO's TRG_Proxim_In_LibraryScript). The editor gives a node an enter script and /
// or an exit script: a script of its own, ed_proxim_<node> = `if Inside { <enter> } else { <exit> }`, where a branch
// not chosen calls the node's own script (its arguments passed on - `<script> <...>`), if it has one, so the other half
// keeps working; it is put in the ProximNode's script (+0xA0), the node's structure untouched. Its shape: a sphere
// (Radius: +0x70 / +0x74) or a box - the game's PROXIM_SHAPE_BOX (1: an axis-aligned box in the world, +0x7C - which
// the game otherwise takes from the sector named like the node), here `size` about the node's place plus `offset`
// (kept with the node as it moves). Put back (script, shape, radius, box) as the node had them when the edit goes, in a
// network session and with Reset level. Records (version 2; older readers skip them):
//   proxim <node> [enter=<script>] [exit=<script>] [shape=sphere|box] [radius=r] [size=x,y,z] [offset=x,y,z]
struct ProximEdit {
    uint32_t node = 0;
    std::string enter, exit; // the scripts ("": the node's own, if any)
    bool shape = false;      // the shape is the editor's (else as the node has it)
    bool box = false;
    float radius = 0.0f;     // (a sphere's)
    V3 size = {}, offset = {}; // (a box's: its extents and its middle from the node's place)
};
bool SameProxim(const ProximEdit& a, const ProximEdit& b);
// Whether it changes anything (a script or a shape).
bool ProximEdited(const ProximEdit& edit);
std::string ProximRecord(const ProximEdit& edit, const char* known); // (after "proxim "; `known`: the node's name)
bool ReadProximRecord(const std::string& text, ProximEdit* edit);
// "ed_proxim_" and the node's checksum in 8 hex digits.
std::string ProximScriptName(uint32_t node);
// The body (empty: neither script chosen): 47 <to else> 16 <Inside> 01 [16 <enter> 01 | 16 <own> 2C 01] 48 <to end>
// 01 [16 <exit> 01 | 16 <own> 2C 01] 28 01 - as NeverScript compiles `if Inside {...} else {...}` (the jumps' offsets
// counted from the offset's own first byte).
std::vector<uint8_t> ProximBody(uint32_t enter, uint32_t exit, uint32_t own);
struct ProximScript {
    uint32_t node, enter, exit, own;
};
std::vector<uint8_t> ProximQbFile(const std::vector<ProximScript>& scripts);
// Radius and box sizes kept within these.
constexpr float kProximMin = 10.0f, kProximMax = 20000.0f;

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
    bool off = false;      // its collision switched off (by the editor, or while an editable copy stands in for it)
    bool wall = false;     // collision only (no geometry): an invisible wall to level_editor_walls.h - movable there
    bool allTrigger = false; // every face of its collision a trigger's (switching it off takes nothing else)
    V3 boxMin = {}, boxMax = {}; // (its collision's box when read: moved since - read again)
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
    bool broken = false; // (a breakable whose piece is gone - broken in play, hidden: no box until it's back)
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

// The volumes drawn over the view (lines, labels) - and the selected editor trigger's resize handles (`selected`: its
// node's name, 0 none; the handle under the cursor or being dragged lit).
void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Text>* texts);
void SetSelected(uint32_t node);
// The resize handles (drawn by the last AddOverlay): the one within `radius` back buffer pixels of (x, y) (-1: none),
// and dragging one - started with the mouse ray, followed each frame (`snap`: sizes on 25s), ended (one undo step).
// The handles' circles go in `circles` (AddOverlay's caller passes the overlay's).
void AddHandles(std::vector<Ui::Overlay::Circle>* circles);
int HandleAt(float x, float y, float radius);
void SetHoverHandle(int handle);
bool BeginResize(int handle, V3 origin, V3 dir);
void Resize(V3 origin, V3 dir, bool snap);
void EndResize();
bool Resizing();
// Every frame, the editor running or not (`editing`): the skater's leaving of the editor's exit triggers (Spec::exit)
// seen, and their scripts run - in play only (not while editing, not in a network session).
void Play(bool editing);
// A click while they're shown: the nearest volume under the ray - an editor trigger's node, or a level trigger's node
// (Nodes::All() index; -1: none, or a level trigger without a node - then it's the one inspected, `sector`).
// Only volumes met before `maxT` along the ray (a plain click: the first occluding surface under the cursor and some
// slack - a volume behind a wall isn't taken; level_editor_picking.h).
bool Pick(V3 origin, V3 unitDir, int* node, uint32_t* sector, float maxT = 1e30f);
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
// (UX9: the gap wizard) A gap made of two ends placed as given (their specs' size, text, score and the start's flags;
// names and the GapID made fresh) - the start selected; one undo step. Queued like the others.
void AddGapPair(Placed start, Placed end);
// A trigger's settings changed (`undo`: a step of its own - else BeginEdit started one, e.g. a field being dragged).
// A gap's name, score and flags go to both its ends.
void SetSpec(const std::string& name, const Spec& spec, bool undo);
void BeginEdit();
// A level trigger switched off / on (through level_editor_walls.h - its collision switch, saved as its `wall` record;
// only when its collision is a wall or all trigger faces), selected as an invisible wall (to move / turn it there), or
// given an editable copy (switched off while the copy is there).
void TurnOff(uint32_t sector, bool off);
void SelectWall(uint32_t sector);
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
    std::vector<Walls::WallState> walls; // (the level triggers switched here)
    std::vector<ProximEdit> proxims;     // (proximity nodes' scripts / shapes)
};
State Save();
void Restore(const State& state);
// A change made since the last call: the state before it (the editor pushes it as one undo step).
bool TakeUndo(State* before);
// A trigger to select (the one just added; -1: none) - a Nodes::All() index; a level trigger to select as an invisible
// wall (0: none).
int TakeSelect();
uint32_t TakeWallSelect();

// The layout (level_editor_layout.h): the triggers as they are now, put in place (how many), everything removed.
std::vector<Placed> CaptureAdded();
int Apply(const std::vector<Placed>& placed, std::string* message);
void ResetAll();
// The proximity nodes' edits likewise (put in place after the nodes: how many found).
std::vector<ProximEdit> CaptureProxims();
int ApplyProxims(const std::vector<ProximEdit>& edits);

// --- the feature map's game side ---
// What a script calls (walked once a level) - the chains' speed-ups read RailAccelerate from it.
Calls ScriptCalls(uint32_t script);
// A jump boost added where the view looks (the Triggers window); one undo step.
void AddBoost();
// (map4) A camera trigger added where the view looks, playing the nearest editor camera (level_editor_cameras.h); one
// undo step.
void AddCamera();
// A scenery piece made breakable (a Break trigger over it, its collision off while it is) or not (the trigger removed,
// its collision back; shown again if a test run broke it); one undo step. The Break trigger standing for it (null:
// none).
void SetBreakable(uint32_t sector, bool on);
const Added* BreakableOf(uint32_t sector);
// The Inspector's section for one scenery piece (level_editor_triggers_ui.cpp): Breaks when hit, its sound.
void ScenerySection(uint32_t sector);
// A proximity node's edit (null: none) and changed (`undo`: a step of its own); its runtime state put in place every
// frame (Guard). Whether the node is a proximity node the game has (its ProximNode).
const ProximEdit* ProximOf(uint32_t node);
void SetProxim(const ProximEdit& edit, bool undo);
bool IsProxim(uint32_t node);
// The selected node has resize handles (an editor trigger, a proximity node).
bool HasHandles(uint32_t node);
// The feature map's lines over the view, the trigger window shown or not: rail speed-ups' chevrons
// (level_editor_chains.h), the selected / edited proximity nodes' shapes, breakables' cracks, boosts' arrows.
void AddExtras(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ, int selectedNode,
               std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Text>* texts);

// For the Inspector, the help and the screenshot sidecar: a line about the triggers.
std::string Describe();

// The career save (level_editor_career.h): the names of the gaps the editor put in `checklist` (false, nothing: it
// isn't the checklist they went into, or there are none) and the level's own gap names (a level gap of the same name
// is the level's, not the editor's).
bool EditorGaps(const Game::CGapChecklist* checklist, std::vector<std::string>* names,
                std::vector<std::string>* levelNames);

} // namespace Front::LevelEditor::Triggers
