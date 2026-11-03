#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_scenery.h"
#include "engine/maths/mat4.h"

namespace objects {
class GameEntity;
}

// NOT ORIGINAL - the level editor's objects (ours; see level_editor.h): the composite objects with a model (level
// objects, game objects, peds, cars - not the skater, not cameras) - picking them by their model's box, moving and
// turning them, tinting them, and copies of them made from their nodes.
namespace frontend::level_editor::object_edit {

// A name for the HUD: the node's name from the .qb files (Scripts::kept_qb_name), else the game's checksum names, else
// null.
const char* get_name_of(uint32_t checksum);
// "<name> <checksum>" / "<checksum>".
std::string caption(uint32_t checksum);

// The model's box where it is (false: no model that follows the object - not pickable).
bool model_box(objects::GameEntity* object, OrientedBox* out);
// The object under the ray (nearest box hit within `max_t` along the normalised ray: `t`).
// Locked objects (level_editor_picking.h) are passed.
objects::GameEntity* get_pick(V3 origin, V3 dir, float max_t, float* t);
// Every object the ray meets within `max_t`, nearest first (click-to-cycle).
std::vector<std::pair<objects::GameEntity*, float>> pick_all(V3 origin, V3 dir, float max_t);
// [features] debug_editor: the objects and which can be picked.
void log_census();

V3 pos_of(const objects::GameEntity* object);
// The middle of its box (or above its position).
V3 current_centre(objects::GameEntity* object);
// Put at `pos` (its components follow: teleported).
void get_place(objects::GameEntity* object, V3 pos);
// Turned by `angle` about the up axis through `pivot` (its position moved too, unless the pivot is it).
void turn_about(objects::GameEntity* object, V3 pivot, float angle);
// "<class> <name>  at (x, y, z)  heading h" for the HUD.
std::string describe(objects::GameEntity* object);
// The class of its node: "LevelObject", "GameObject", "Pedestrian", "Vehicle"...
const char* class_of(uint32_t id);

// Where an object is, how it's turned and scaled (undo, layouts; the scale along its own axes - level_editor_scale.h).
struct State {
    uint32_t id{};
    maths::Vec4 pos{};
    maths::Mat4 matrix{}, draw_matrix_{};
    V3 scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f};
};
State store_away(objects::GameEntity* object);
// Put back (false: no such object now).
bool get_restore(const State& state);

// Its model's geometry `index` (0..15; null past the last - the model component's model +0x208, 0x00494640) - the
// Add palette's preview draws them (level_editor_preview.h).
void* get_geometry(objects::GameEntity* object, int index);
// The model file its node names (the file's name without its folders; empty: none).
std::string get_model_file(uint32_t name);

// Highlighting: each geometry of its model tinted (Scenery::get_tint; false: none could be - draw its box instead).
bool get_tint(objects::GameEntity* object, uint32_t rgba);
// For Scenery::end_tints: an owner's geometry now (sectors and objects).
void* resolve_tint(scenery::TintOwner owner);

// Copies: made from the source object's node (its structure copied, with a new name and position) through the
// game's CreateFromStructure (0x005A2E50) - level objects and game objects; since the feature map's second round
// (level_editor_entities.h) peds and cars too (a copy keeps its source's links - its path - and TriggerScript: it
// behaves as its source) and particle systems (their MidPosition / EndPosition moved with the copy's pos=).
bool can_copy(uint32_t source);
// A copy of `source` at `pos` (`name` 0: a new name). Null when it couldn't be made.
objects::GameEntity* make_copy(uint32_t source, V3 pos, uint32_t name = 0);
// A copy removed (flag_as_dead); false when it isn't one of ours or is gone.
bool remove_copy(uint32_t name);
// The copies made this level (made and not removed), with their sources.
struct DupFrom {
    uint32_t name;
    uint32_t source;
};
const std::vector<DupFrom>& get_copies();
bool is_copy(uint32_t id);
// Objects the editor has moved this level (their ids; layouts save them).
void touch(uint32_t id);
const std::vector<uint32_t>& get_touched();
// Where an object was when the editor first changed it this level (Home puts it back); noted before each change.
void note_origin(objects::GameEntity* object);
const State* get_origin(uint32_t id);
// A new level: copies, touched objects and first places forgotten.
void forget_level();

// The browser's objects: the level's nodes that can be copied ("<class> <name or model>").
struct TableRow {
    uint32_t name;
    std::string label;
};
std::vector<TableRow> copyable();

} // namespace frontend::level_editor::object_edit
