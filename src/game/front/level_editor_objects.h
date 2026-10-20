#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_scenery.h"
#include "game/mth/matrix.h"

namespace Obj {
class CCompositeObject;
}

// NOT ORIGINAL - the level editor's objects (ours; see level_editor.h): the composite objects with a model (level
// objects, game objects, peds, cars - not the skater, not cameras) - picking them by their model's box, moving and
// turning them, tinting them, and copies of them made from their nodes.
namespace Front::LevelEditor::Objects {

// A name for the HUD: the node's name from the .qb files (Script::KeptQbName), else the game's checksum names, else
// null.
const char* NameOf(uint32_t checksum);
// "<name> <checksum>" / "<checksum>".
std::string Label(uint32_t checksum);

// The model's box where it is (false: no model that follows the object - not pickable).
bool ModelBox(Obj::CCompositeObject* object, OrientedBox* box);
// The object under the ray (nearest box hit within `maxT` along the normalised ray: `t`).
// Locked objects (level_editor_picking.h) are passed.
Obj::CCompositeObject* Pick(V3 origin, V3 dir, float maxT, float* t);
// Every object the ray meets within `maxT`, nearest first (click-to-cycle).
std::vector<std::pair<Obj::CCompositeObject*, float>> PickAll(V3 origin, V3 dir, float maxT);
// [features] debug_editor: the objects and which can be picked.
void LogCensus();

V3 PosOf(const Obj::CCompositeObject* object);
// The middle of its box (or above its position).
V3 Centre(Obj::CCompositeObject* object);
// Put at `pos` (its components follow: teleported).
void Place(Obj::CCompositeObject* object, V3 pos);
// Turned by `angle` about the up axis through `pivot` (its position moved too, unless the pivot is it).
void TurnAbout(Obj::CCompositeObject* object, V3 pivot, float angle);
// "<class> <name>  at (x, y, z)  heading h" for the HUD.
std::string Describe(Obj::CCompositeObject* object);
// The class of its node: "LevelObject", "GameObject", "Pedestrian", "Vehicle"...
const char* ClassOf(uint32_t id);

// Where an object is and how it's turned (undo, layouts).
struct State {
    uint32_t id;
    Mth::Vector pos;
    Mth::Matrix matrix, displayMatrix;
};
State Save(Obj::CCompositeObject* object);
// Put back (false: no such object now).
bool Restore(const State& state);

// Highlighting: each geometry of its model tinted (Scenery::Tint; false: none could be - draw its box instead).
bool Tint(Obj::CCompositeObject* object, uint32_t rgba);
// For Scenery::EndTints: an owner's geometry now (sectors and objects).
void* ResolveTint(Scenery::TintOwner owner);

// Copies: made from the source object's node (its structure copied, with a new name and position) through the
// game's CreateFromStructure (0x005A2E50) - static props only (LevelObject, GameObject): peds and cars follow paths
// and run scripts that don't expect a second one.
bool CanCopy(uint32_t source);
// A copy of `source` at `pos` (`name` 0: a new name). Null when it couldn't be made.
Obj::CCompositeObject* MakeCopy(uint32_t source, V3 pos, uint32_t name = 0);
// A copy removed (MarkAsDead); false when it isn't one of ours or is gone.
bool RemoveCopy(uint32_t name);
// The copies made this level (made and not removed), with their sources.
struct Copy {
    uint32_t name;
    uint32_t source;
};
const std::vector<Copy>& Copies();
bool IsCopy(uint32_t id);
// Objects the editor has moved this level (their ids; layouts save them).
void Touch(uint32_t id);
const std::vector<uint32_t>& Touched();
// Where an object was when the editor first changed it this level (Home puts it back); noted before each change.
void NoteOrigin(Obj::CCompositeObject* object);
const State* Origin(uint32_t id);
// A new level: copies, touched objects and first places forgotten.
void ForgetLevel();

// The browser's objects: the level's nodes that can be copied ("<class> <name or model>").
struct Entry {
    uint32_t name;
    std::string label;
};
std::vector<Entry> Copyable();

} // namespace Front::LevelEditor::Objects
