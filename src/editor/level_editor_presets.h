#pragma once

// NOT ORIGINAL - the level editor's moving-object presets (ours; the Triggers window's Car / Ped on a path - the
// trigger presets themselves are level_editor_triggers.h's add_booster, add_teleport_pair, add_two_way_gap...).
//
// A car or a ped on a path, in one click: a copy of one of the level's own cars / peds placed where the view looks (as
// the Place palette's "Car: ..." / "Ped: ..." entries place it - level_editor_entities.h; a ped whose source walks a
// path preferred), then a closed loop of four waypoints (a 1000-unit square ahead and to the right, each on the ground)
// drawn as the Path tool draws them (level_editor_chains.h: Waypoint points of the editor's, copying the fields of the
// source's own first waypoint - a ped's PedType / Continue... - when it has one) and given to the copy as its path (its
// links= set to the first waypoint, as the Inspector's Assign Path does). Two undo steps: the copy, then the path.
// Saved as the copy's `copy` record and the chains' `point` / `links` records.
namespace frontend::level_editor::presets {

// `vehicle`: a car, else a ped. Queued by the UI (run in the editor's update).
void add_on_path(bool vehicle);

} // namespace frontend::level_editor::presets
