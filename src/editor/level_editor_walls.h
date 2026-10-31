#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/level_editor.h"

namespace render {
struct TriMeshSource;
}

// NOT ORIGINAL - the level editor's collision switches and invisible walls (ours; see level_editor.h): collision with
// nothing drawn for it (a sector whose geometry is null - level_editor_view.h shows it magenta, Shift+click selects it)
// switched off and on, moved and turned; and a drawn piece's collision switched off by itself.
//
//   - The switch is the game's own: WorldCell::change_live (0x0049E9B0, thiscall, RET 4) sets / clears flag 4 of the
//     sector's collision (StaticCollider +0x08) and the geometry's active flag (when it has one) - what a LevelGeometry
//     node's Kill does (0x005A32C0) and what the editor's Delete (hiding scenery) already uses. The world's collision
//     query (the grid lists, 0x004188C0) skips collision objects with flag 2 or 4, so the skater's feelers, the camera's
//     and every other one pass a wall switched off. For an invisible wall (no geometry) change_live touches only that
//     flag; for a drawn piece the editor sets / clears the flag by itself (the collision half of change_live), keeping
//     the piece drawn - and sets it again whenever something shows the piece (change_live(true) clears it: the editor's
//     Home / undo of a hidden piece), every frame while editing (Tick) and after a layout goes in place.
//   - Moving and quarter turns: the collision geometry's own methods (TriMeshSource::RotateY 0x004164F0 and
//     Translate 0x00416400 - what StaticCollider's slot 22 and place_at_absolute_point 0x00410C20 hand on to), then the sector
//     flagged and the scene's super sectors updated (Scenery::note_collision_moved / Commit), as scenery moves do.
//     WorldCell::spin_around_up (0x0049E760) isn't used: with no geometry it turns the collision about the shared origin
//     (0x00691914), not the wall. The BSP tree's splits are whole sixteenths and Translate rounds each towards zero
//     (0x00415D80), so a wall's placement is kept on sixteenths (on_grid): every step is then exact and moving it back
//     gives the level's bytes again (no drift - self-tested).
//   - Undo: the walls' states (WallState) as the editor's undo step kWalls. Layouts: `wall <checksum> <quarters> <x>
//     <y> <z> <off>` records (level_editor_layout.h; a drawn piece's has no move - its own `sector` record moves it);
//     older readers skip them. Applied with the layout (never in a network session), put back by Reset level.
namespace frontend::level_editor::walls {

// A wall's (or a drawn piece's collision's) state: where its collision has been put relative to where the level has it
// (Apply(placement, x): quarter turns about the world's up axis, then the offset; scale always 1) and whether it's off.
struct WallState {
    uint32_t checksum;
    Placement placement;
    bool off;
};

// --- the pieces (pure) ------------------------------------------------------------------------------------------------

// To the nearest sixteenth of a unit (the BSP tree's grid).
float to_sixteenth(float value);
V3 on_grid(V3 v);
// The step from one placement to another as the geometry's methods take it: turned `turns` (0..3) quarter turns about
// `centre`, then moved by `delta` - Apply(to, x) == get_quarter_turn(Apply(from, x) - centre, turns) + centre + delta.
struct Step {
    int turns;
    V3 centre;
    V3 delta;
};
Step step_between(const Placement& from, const Placement& to);
// That step done to a collision geometry (RotateY, then Translate; nothing for a zero step).
void step_collision(render::TriMeshSource* tri, const Placement& from, const Placement& to);
// A placement moved so that the point `pivot` goes to `to`, on sixteenths; turned `quarters` about `pivot` (put on
// sixteenths first).
Placement moved_to(const Placement& placement, V3 pivot, V3 to);
Placement turned_about(const Placement& placement, V3 pivot, int quarters);
// Whether a placement is none (as the level has it).
bool is_home(const Placement& placement);
// The Inspector's Collision checkbox over several pieces: -1 none, 0 all off, 1 all on, 2 mixed.
int get_combined(const std::vector<bool>& offs);

// --- the game side --------------------------------------------------------------------------------------------------

// An invisible wall: a sector with level collision (StaticCollider) and no geometry.
bool is_wall(uint32_t checksum);
// Its collision's box now (whether off or on); false: none.
bool cuboid(uint32_t checksum, V3* min, V3* max);
// The middle of its box, and its bottom.
V3 get_pivot(uint32_t checksum);
float get_bottom(uint32_t checksum);
// Moved so that its pivot is at `pivot` / turned `quarters` about its pivot (collision only; Commit after).
bool move_to(uint32_t checksum, V3 pivot);
bool get_turn(uint32_t checksum, int quarters);
// The collision switched off / on (a wall: WorldCell::change_live; a drawn piece: its collision's flag 4 only).
bool set_off(uint32_t checksum, bool off);
// Whether its collision is off now (a drawn piece: off by this switch - not hidden).
bool disengaged(uint32_t checksum);
// Where the editor has put it (false: not moved).
bool get_moved(uint32_t checksum, Placement* placement);
// The super sectors brought up to date after moves (Scenery::finalise).
void finalise();

// Undo / layouts: these pieces' states now; states put in place (a drawn piece: only `off`); what the editor has
// changed in this level (copies of scenery left out); everything put back as the level has it.
std::vector<WallState> store_away(const std::vector<uint32_t>& checksums);
void apply_states(const std::vector<WallState>& states);
std::vector<WallState> get_changed();
void reset_all();
// Once a frame while editing (and after a layout goes in place): drawn pieces switched off kept off (something that
// showed them - change_live(true) - turned their collision on with them).
void tick();
// The Inspector's lines about it: off or on, moved or not.
std::string describe_state(uint32_t checksum);

} // namespace frontend::level_editor::walls
