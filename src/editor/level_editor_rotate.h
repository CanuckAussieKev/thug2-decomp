#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_transform.h"

// NOT ORIGINAL - the level editor's rotation tools (ours, at the user's request: rotation as Unity / Unreal / Blender
// do it) and the keys that came with them:
//   - the Rotate gizmo's rings (level_editor_transform.h): about X / Y / Z (red / green / blue - the world's, or Local the
//     selection's), a view ring about the line of sight (white, larger) and a free trackball inside them (the sphere's
//     area: drag in any direction). Objects turn about any of them; scenery and nodes only about up (the Y ring - the
//     engine's level geometry turns about up at any angle: level_editor_scenery.h's turn_by).
//   - Alt+R: Blender's R - a rotate without holding the mouse (whatever the tool); X / Y / Z then turn it about that axis
//     only, digits type the degrees, Enter / a click applies, Escape puts it back. (Alt+G stays the tool's own.)
//   - While a turn is dragged: the angle so far beside the pivot.
//   - The Inspector's Rotation: X / Y / Z in degrees (objects: R = Ry Rx Rz, set exactly - the object's rows made from
//     the three angles each time, so nothing drifts; scenery: Y only - its heading).
//   - Home frames the whole level (Blender's Home); Revert to Original is the Edit menu's, asked first (no key).
//   - V held while moving: vertex snap (Unity / Unreal) - as Ctrl+Alt.
//   - Alt+drag on a Move arrow: a duplicate dragged, the source left (Unreal / Maya) - one undo step.
namespace frontend::level_editor::rotate_tool {

// Gizmo handles beyond the axes' 0 / 1 / 2 (Rotate): the view ring and the trackball.
constexpr int kViewRing = 4;
constexpr int kTrackball = 5;

// --- the pure parts ---------------------------------------------------------------------------------------------

// `v` turned by `radians` about the unit `axis` (right-handed: about up, +z towards +x - as turn_about_up).
V3 turn_about_axis(V3 v, V3 axis, float radians);
// Two unit directions square to the unit `axis` and to each other (v = axis x u).
struct Square {
    V3 u, v;
};
Square square_to(V3 axis);
// The angle of `point` about the line `axis` (its origin the pivot, its unit direction the axis): atan2 of its parts
// along v and u - for up: turn_around_axis's.
float angle_about(Ray axis, V3 point);
// `ring.count` points round the line's origin at `ring.radius`, square to its direction.
std::vector<V3> ring_about(Ray axis, xform::RingShape ring);
// Euler angles in degrees (x pitch, y heading, z roll: rows = columns of Ry(y) Rx(x) Rz(z)) of a frame's unit rows
// (right, up, at), and the rows of those angles.
V3 euler_of(std::span<const V3, 3> rows);
void rows_of(V3 degrees, std::span<V3, 3> rows);

// --- the game side ----------------------------------------------------------------------------------------------

// Once: the actions (Home's Frame Level, Revert to Original, V's vertex snap, Alt+R, the hints).
void prepare_all();
int frame_level_action();
int revert_action();
int vertex_snap_action();
int modal_rotate_action();
// Revert to Original asked for (the Edit menu / the command search): the question shown; true once when it was answered
// yes (the editor's frame then reverts the selection).
void ask_revert();
bool asking();
bool take_revert();
// The question (inside the editor's ImGui frame).
void dialogs(float scale);
// Alt+drag on a Move arrow made a copy (the undo history `depth` deep before it): the drag's end folds the copy and the
// move into one undo step.
void note_copy_drag(std::size_t depth);
bool take_copy_drag(std::size_t* depth);
// The angle so far of a turn being dragged ("37.0°"), for beside the pivot.
std::string readout(float radians);

} // namespace frontend::level_editor::rotate_tool
