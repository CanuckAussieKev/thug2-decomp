#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "editor/level_editor.h"
#include "engine/core/checksums.h"

// NOT ORIGINAL - the level editor's snapping while dragging (ours; see level_editor.h and level_editor_transform.h). Held while dragging (or always, with the toolbar's Snap on and its Snap to):
//   - Ctrl: the grid (the toolbar's size: 1, 5, 10, 25, 50, 100) relative to where the drag started - it moves by whole
//     steps from where it was; Ctrl+Shift: the world's grid (absolute: on the grid's lines). Rotate drags: the rotation
//     snap's steps; Scale drags: tenths.
//   - Alt+Shift: the surface under the cursor - the selection's base slides along whatever the collision ray meets
//     first (the selection's own pieces passed), an object optionally stood square to its slope (the toolbar's Align);
//     scenery keeps its quarter turns, nodes their angles.
//   - Ctrl+Alt: a vertex - the selection's box corner nearest the cursor (chosen when snapping starts) goes onto the
//     nearest vertex of the piece under the cursor (its drawn meshes', or an object's box corner).
// Alt alone still leaves what scenery carries (level_editor_carry.h); Shift alone still drags up / down.
namespace frontend::level_editor::snap {

// --- the pure parts --------------------------------------------------------------------------------------------------

// The index of the point nearest `to` (-1: none).
int get_nearest(const std::vector<V3>& points, V3 to);
// The index of the point whose projection is nearest (x, y) in pixels (-1: none in front).
int nearest_on_screen(const std::vector<V3>& points, const std::function<bool(V3, float*, float*)>& project, float x,
                      float y);
// An object's rows (right, up, at - their lengths kept) stood square to a surface's `normal`: up along it, at keeping
// its heading as nearly as it can (false: the normal is no direction, or at lies along it - unchanged).
bool align_rows(std::span<V3, 3> rows, V3 normal);
// The selection's place for its base to lie on `point`: `pos` (where it is) moved so that its base (`base`: its height
// there) is on the point.
inline V3 on_surface(V3 pos, float base, V3 point) { return {.x = point.x, .y = point.y + (pos.y - base), .z = point.z}; }

// --- the game side ------------------------------------------------------------------------------------------------

// The first static collision the ray meets within `max_t` (unit `dir`), passing the sectors in `skip` (the selection's
// own): its point and normal, and the sector's checksum. False: none.
struct SurfaceHit {
    V3 point = {}, normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    uint32_t checksum = 0;
};
bool get_surface(V3 origin, V3 dir, float max_t, const std::vector<uint32_t>& skip, SurfaceHit* hit);
// The vertices of a sector's drawn meshes where it is now (at most `limit`; none for an unknown sector).
std::vector<V3> sector_vertices(checksums::Name sector, size_t limit);

} // namespace frontend::level_editor::snap
