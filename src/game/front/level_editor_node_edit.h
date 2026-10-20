#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_nodes.h"

// NOT ORIGINAL - the level editor's node edits (ours; see level_editor.h): point nodes (spawns / restarts, crowns, CTF
// and team flags, lights, sound emitters, proximity nodes, cameras, generic and other nodes) moved and turned - their
// structure's pos= and angles= written, and whatever the game keeps of them brought up to date so the change shows
// without reloading the level (phase 2 of the plan). Undo / redo and the layouts' `set` records go through here.
//
// What applies when (found in the decompiled code - docs/pending/editor-ui.md has the table):
//   - restarts: read from the node each time one is used (Obj::GetRestartNodes 0x0046B6C0, the skate module's lookup
//     0x00566100, CSkater::MoveToNode 0x005815C0): the next respawn / F8 (restart_tester) puts the skater there;
//   - crowns: CFunc_SpawnCrown 0x00538170 walks the nodes and makes the crown from the n-th Crown node's place each
//     time: the next crown spawned is there (one already in the level stays where it is);
//   - flags: their object (when a CTF / team game has made it) is moved with the node, as the editor moves objects;
//   - proximity nodes and sound emitters: the managers keep a copy of the node's place (ProximNode / EmitterNode
//     m_pos, made at the level's load): updated - a sphere (Radius) follows at once; a box shape comes from the level
//     sector of the same name, which the node doesn't move;
//   - lights (LevelLight): the light lists keep the place too (0x00490570 makes them: the dynamic lights at
//     0x006FAAF0, the level lights at 0x006FAF80): updated; what a light has already lit in the scene's colours
//     follows only after the level is loaded again;
//   - cameras, generic nodes and the rest: read from the node when used.
namespace Front::LevelEditor::NodeEdit {

// A node's place: pos= and angles= (radians, as the node arrays hold them).
struct Values {
    V3 pos = {};
    V3 angles = {};
};
bool Same(const Values& a, const Values& b);

// --- the pure parts (self-tested: src/hook/selftests_editor_nodes.cpp) ----------------------------------------------

// An angle in radians brought into (-pi, pi].
float WrapAngle(float radians);
// Turned by `notches` steps of 15 degrees about the up axis: the Y angle (positive: +z towards +x, as the editor turns
// objects - the game's angles-to-matrix 0x0041A730 makes a node of angles (0, a, 0) face (sin a, 0, cos a)).
V3 Turned(V3 angles, int notches);
// The Y angle as a heading in degrees (-180, 180], and set from one.
float HeadingDegrees(V3 angles);
V3 WithHeading(V3 angles, float degrees);

// An undo step's nodes: each node's place before the change.
struct Step {
    std::vector<std::pair<uint32_t, Values>> nodes;
};
using Reader = std::function<bool(uint32_t name, Values* values)>;
using Writer = std::function<void(uint32_t name, const Values& values)>;
// The named nodes' places now (those `read` knows).
Step Capture(const std::vector<uint32_t>& names, const Reader& read);
// The step's places written back.
void Put(const Step& step, const Writer& write);
// Whether any of the step's nodes is somewhere else now.
bool Differs(const Step& step, const Reader& read);

// --- the game side ------------------------------------------------------------------------------------------------

// Whether the editor moves and turns this node itself (a point node: not a chain, an object or scenery - those move
// through their own layers - and not particles yet).
bool Movable(const Nodes::Node& node);
// When a change to it shows in the game (the Inspector's note).
const char* WhenApplied(const Nodes::Node& node);

// The node named `name`'s place now (false: no such node).
bool Read(uint32_t name, Values* values);
// The node's place changed: its structure, what the game keeps of it, its object if it has one, and the node layer's
// copy (its gizmo). Its place as the level had it is noted first (Origin). False: no such node.
bool Set(uint32_t name, const Values& values);
// The node's place as the level had it, noted the first time the editor changes it in this level.
void NoteOrigin(uint32_t name);
const Values* Origin(uint32_t name);
// The nodes the editor has changed in this level and where the level had them.
struct Record {
    uint32_t name;
    Values values;
};
// Those changed and not back where the level had them, as they are now (layouts save them).
std::vector<Record> Changed();
// Every node the editor has changed put back where the level had it (a layout put in place of another).
void ResetAll();
// A new level: the changes forgotten (put back first if the last level's node array is still the one loaded).
void ForgetLevel();

} // namespace Front::LevelEditor::NodeEdit
