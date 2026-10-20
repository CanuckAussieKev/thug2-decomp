#pragma once

#include <cstdint>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_objects.h"

namespace Obj {
class CCompositeObject;
}

// NOT ORIGINAL - the level editor's carrying (ours, added at the user's request; see level_editor.h): when a scenery
// group moves, turns or scales, the objects and point nodes standing on it or attached to it come along.
//
//   - What is carried (gathered when a change of the scenery selection starts - a drag, the keys, a turn, a scale, Home,
//     G): composite objects with a model (level objects, game objects, cars - not peds, which walk their paths, not
//     skaters or cameras, not a flag's object - its node moves it) and point nodes the editor moves (spawns, crowns,
//     flags, lights, sounds, proximity nodes, triggers, cameras, other nodes - not rail / ladder / path points, which go
//     with their scenery or their chain already). One is attached when
//       1. its node's Cluster= (the TrickObject cluster) names a member of the selection or a member's cluster, or
//       2. one of its node's links names a member, or
//       3. it stands on the group: it is over a member's footprint (its box's x / z grown by kMargin, from kMargin below
//          the bottom to kAbove over the top) and the game's collision ray straight down from kRayUp above it meets a
//          member first (kRayDown at most - the ground beside the group, or a floor under a table, isn't the group).
//   - How: the selection's first piece is the reference; after each change its placement's step since the last one
//     (Between) is applied to each carried thing - its position, and for quarter turns its heading (objects' matrices,
//     nodes' angles). A uniform scale moves their places (they keep their own size).
//   - The toolbar's "Carry objects" (on by default, [editor] carry=0/1 in thug2-decomp.ini) and Alt held while dragging
//     or moving with the keys (Shift is already up / down): the carried things are put back where they were when the
//     change started and left there for that change.
//   - Undo / Home / layouts: each kind records its own moves - objects as the editor's moved objects (Touch, their first
//     place noted), nodes through NodeEdit::Set (their `set` records); the scenery's undo step carries their places
//     before the change (level_editor.cpp's UndoStep).
namespace Front::LevelEditor::Carry {

constexpr float kMargin = 10.0f;  // (units round a member's footprint)
constexpr float kAbove = 400.0f;  // (how far over a member's top something can be and still stand on it)
constexpr float kRayUp = 50.0f;   // (the ground ray starts this far above the thing: its feet may be in the surface)
constexpr float kRayDown = 400.0f;

// --- the pure parts (self-tested: src/hook/selftests_editor_carry.cpp) ------------------------------------------

// The placement undoing `placement`: Apply(Inverse(p), Apply(p, x)) == x (a scale of 0 is taken as 1).
Placement Inverse(const Placement& placement);
// The step from one placement of a piece to another: Apply(Between(from, to), Apply(from, x)) == Apply(to, x).
Placement Between(const Placement& from, const Placement& to);
// Whether a step changes nothing.
bool IsNone(const Placement& step);
// Whether `p` is over a box's footprint: along the box's own x / z axes within min..max grown by `margin`, and along
// its y axis from min - margin to max + `above` (axes needn't be unit: a scaled piece's box).
bool OverFootprint(const OrientedBox& box, V3 p, float margin, float above);
// Why something is attached to the selection (in this order), from its node's Cluster= (0: none), the names its links
// name, and what the ground ray below it met first (0: nothing, or not over a footprint).
enum class Attachment { kNone, kCluster, kLink, kStanding };
Attachment Attached(uint32_t cluster, const std::vector<uint32_t>& links, uint32_t groundHit,
                    const std::vector<uint32_t>& members, const std::vector<uint32_t>& memberClusters);
// A heading turned by a step's quarter turns (radians; the node's angles' y - positive +z towards +x, as QuarterTurn).
float TurnedHeading(float heading, int quarters);

// --- the game side --------------------------------------------------------------------------------------------------

// The toolbar's "Carry objects" ([editor] carry, default 1).
bool Enabled();
void SetEnabled(bool on);

// What a change took along: the carried objects' and nodes' places before it (the undo step's).
struct Taken {
    std::vector<Objects::State> objects;
    NodeEdit::Step nodes;
};
// A change of the scenery selection starting: what is attached to it now gathered (nothing when off or nothing is
// selected) and their places returned.
Taken Arm();
// After the selection moved, turned or scaled: the reference piece's step since the last call applied to what was
// gathered. `onNode`: called for each node moved (the online warning). Nothing when not armed.
void Follow(void (*onNode)(uint32_t name) = nullptr);
// Alt: what was gathered put back where Arm found it, and not carried until the next Arm.
void Leave();
// Nothing carried until the next Arm (undo / redo, a layout put in place, the editor stopping).
void Disarm();
bool Armed();
// How many objects / nodes are carried now (the status bar).
int Count();

} // namespace Front::LevelEditor::Carry
