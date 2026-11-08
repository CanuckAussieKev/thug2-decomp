#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "editor/level_editor.h"

// NOT ORIGINAL - the level editor's ghosts (ours): scenery pieces drawn see-through where something would land - the
// Content Browser's drag (a level's piece, a prop pack, a prefab's pieces) shows the real piece following the surface
// under the cursor, as Unreal's / Unity's drag-placement previews do.
//
// A ghost is a sector's own meshes drawn a second time under a matrix of its own (render::ghost_draws: the renderer
// draws them after the world, in every view, at a fixed opacity). Nothing of the sector changes - not its vertices,
// its collision, its placement or whether it is hidden - so a ghost leaves nothing in the layout or the undo history,
// and neither the cursor's ray nor the skater can meet it (it can't climb onto itself). The pieces asked for are handed
// to the renderer as the editor's frame ends (before the 3D view is drawn): their sectors are looked up then, so a level
// gone in between leaves no stale geometry to draw.
namespace frontend::level_editor::ghost {

// --- the pure parts -------------------------------------------------------------------------------------------------

// A matrix in the renderer's row-vector order: rows where the x, y and z axes go, the fourth the move.
inline constexpr std::size_t kMatrixFloats = 16;
using Matrix = std::array<float, kMatrixFloats>;
inline constexpr Matrix kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
// A placement's matrix: get_apply(placement, x) == x times it.
Matrix matrix_of(const Placement& placement);
// A move by `delta`.
Matrix moved(V3 delta);
// `first`, then `then` (x times first, times then).
Matrix after(const Matrix& first, const Matrix& then);
// The matrix undoing an affine one (false: it squashes space flat - a scale of 0).
bool inverse_of(const Matrix& matrix, Matrix* inverse);
// A point times the matrix.
V3 transformed(const Matrix& matrix, V3 point);

// --- the game side ------------------------------------------------------------------------------------------------

// A sector drawn as a ghost: its meshes as they are now, times `world`.
struct Part {
    uint32_t sector = 0;
    Matrix world = kIdentity;
};
// How much of a ghost shows (0x80 all of it).
inline constexpr uint8_t kOpacity = 0x38;
// This frame's ghosts (added to those asked for already this frame).
void show(std::span<const Part> parts);
// The editor's frame starting (before the 3D view is drawn): the ghosts asked for since the last one handed to the
// renderer for this frame, and forgotten - a frame that asks for none draws none.
void hand_over();

} // namespace frontend::level_editor::ghost
