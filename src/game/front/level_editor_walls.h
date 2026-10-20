#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

namespace Nx {
struct CCollObjTriData;
}

// NOT ORIGINAL - the level editor's collision switches and invisible walls (ours; see level_editor.h): collision with
// nothing drawn for it (a sector whose geometry is null - level_editor_view.h shows it magenta, Shift+click selects it)
// switched off and on, moved and turned; and a drawn piece's collision switched off by itself.
//
//   - The switch is the game's own: CSector::SetActive (0x0049E9B0, thiscall, RET 4) sets / clears flag 4 of the
//     sector's collision (CCollStatic +0x08) and the geometry's active flag (when it has one) - what a LevelGeometry
//     node's Kill does (0x005A32C0) and what the editor's Delete (hiding scenery) already uses. The world's collision
//     query (the grid lists, 0x004188C0) skips collision objects with flag 2 or 4, so the skater's feelers, the camera's
//     and every other one pass a wall switched off. For an invisible wall (no geometry) SetActive touches only that
//     flag; for a drawn piece the editor sets / clears the flag by itself (the collision half of SetActive), keeping
//     the piece drawn - and sets it again whenever something shows the piece (SetActive(true) clears it: the editor's
//     Home / undo of a hidden piece), every frame while editing (Tick) and after a layout goes in place.
//   - Moving and quarter turns: the collision geometry's own methods (CCollObjTriData::RotateY 0x004164F0 and
//     Translate 0x00416400 - what CCollStatic's slot 22 and SetWorldPosition 0x00410C20 hand on to), then the sector
//     flagged and the scene's super sectors updated (Scenery::NoteCollisionMoved / Commit), as scenery moves do.
//     CSector::SetYRotation (0x0049E760) isn't used: with no geometry it turns the collision about the shared origin
//     (0x00691914), not the wall. The BSP tree's splits are whole sixteenths and Translate rounds each towards zero
//     (0x00415D80), so a wall's placement is kept on sixteenths (OnGrid): every step is then exact and moving it back
//     gives the level's bytes again (no drift - self-tested).
//   - Undo: the walls' states (WallState) as the editor's undo step kWalls. Layouts: `wall <checksum> <quarters> <x>
//     <y> <z> <off>` records (level_editor_layout.h; a drawn piece's has no move - its own `sector` record moves it);
//     older readers skip them. Applied with the layout (never in a network session), put back by Reset level.
namespace Front::LevelEditor::Walls {

// A wall's (or a drawn piece's collision's) state: where its collision has been put relative to where the level has it
// (Apply(placement, x): quarter turns about the world's up axis, then the offset; scale always 1) and whether it's off.
struct WallState {
    uint32_t checksum;
    Placement placement;
    bool off;
};

// --- the pieces (pure, self-tested: src/hook/selftests_editor_walls.cpp) ----------------------------------------------

// To the nearest sixteenth of a unit (the BSP tree's grid).
float ToSixteenth(float value);
V3 OnGrid(V3 v);
// The step from one placement to another as the geometry's methods take it: turned `turns` (0..3) quarter turns about
// `centre`, then moved by `delta` - Apply(to, x) == QuarterTurn(Apply(from, x) - centre, turns) + centre + delta.
struct Step {
    int turns;
    V3 centre;
    V3 delta;
};
Step StepBetween(const Placement& from, const Placement& to);
// That step done to a collision geometry (RotateY, then Translate; nothing for a zero step).
void StepCollision(Nx::CCollObjTriData* tri, const Placement& from, const Placement& to);
// A placement moved so that the point `pivot` goes to `to`, on sixteenths; turned `quarters` about `pivot` (put on
// sixteenths first).
Placement MovedTo(const Placement& placement, V3 pivot, V3 to);
Placement TurnedAbout(const Placement& placement, V3 pivot, int quarters);
// Whether a placement is none (as the level has it).
bool IsHome(const Placement& placement);
// The Inspector's Collision checkbox over several pieces: -1 none, 0 all off, 1 all on, 2 mixed.
int Combined(const std::vector<bool>& offs);

// --- the game side --------------------------------------------------------------------------------------------------

// An invisible wall: a sector with level collision (CCollStatic) and no geometry.
bool IsWall(uint32_t checksum);
// Its collision's box now (whether off or on); false: none.
bool Box(uint32_t checksum, V3* min, V3* max);
// The middle of its box, and its bottom.
V3 Pivot(uint32_t checksum);
float Bottom(uint32_t checksum);
// Moved so that its pivot is at `pivot` / turned `quarters` about its pivot (collision only; Commit after).
bool MoveTo(uint32_t checksum, V3 pivot);
bool Turn(uint32_t checksum, int quarters);
// The collision switched off / on (a wall: CSector::SetActive; a drawn piece: its collision's flag 4 only).
bool SetOff(uint32_t checksum, bool off);
// Whether its collision is off now (a drawn piece: off by this switch - not hidden).
bool Off(uint32_t checksum);
// Where the editor has put it (false: not moved).
bool Moved(uint32_t checksum, Placement* placement);
// The super sectors brought up to date after moves (Scenery::Commit).
void Commit();

// Undo / layouts: these pieces' states now; states put in place (a drawn piece: only `off`); what the editor has
// changed in this level (copies of scenery left out); everything put back as the level has it.
std::vector<WallState> Save(const std::vector<uint32_t>& checksums);
void ApplyStates(const std::vector<WallState>& states);
std::vector<WallState> Changed();
void ResetAll();
// Once a frame while editing (and after a layout goes in place): drawn pieces switched off kept off (something that
// showed them - SetActive(true) - turned their collision on with them).
void Tick();
// The Inspector's lines about it: off or on, moved or not.
std::string DescribeState(uint32_t checksum);

} // namespace Front::LevelEditor::Walls
