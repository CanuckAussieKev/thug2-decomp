#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "base/core/api.h"

namespace render {
class Geom;
}

// NOT ORIGINAL - see-through copies of geometry a tool draws in the 3D view (the level editor's drag preview: the piece
// being dragged in from the Content Browser shown where it would land, before anything is placed).
//
// A UI hands its ghosts over once a frame (set_ghosts), in the frame they are drawn in (before the 3D passes - the
// level editor's frame runs then); the renderer draws them in that frame's 3D view after the world (render_pc/
// ghost_draw), in every view of a split view, with each view's camera: each ghost's meshes as the world pass draws them
// (its material, textures and vertex colours) under a world matrix of its own, blended over what is behind at a fixed
// opacity, then they are dropped. Nothing of the geometry changes: its vertices, its collision and its own drawing stay
// as they are (a geometry hidden in the level draws as a ghost all the same), so a ghost is never met by a ray or a
// skater.
namespace render::ghost_draws {

inline constexpr std::size_t kMatrixFloats = 16;
inline constexpr uint8_t kHalfOpacity = 0x40;
struct Ghost {
    Geom* geom = nullptr;
    // The matrix its vertices (as they are now) are drawn with: rows are where the x, y and z axes go, the fourth the
    // move (the renderer's row-vector order); the last column 0, 0, 0, 1.
    std::array<float, kMatrixFloats> world{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    // How much of it shows over what is behind (0x80 all of it; the game's fixed-alpha blend - its modes' top byte).
    uint8_t opacity = kHalfOpacity;
};

// --- the UI's side (the plugin API) ---

// The ghosts of this frame (in place of those handed over before). The geometries must stay until the frame is drawn.
THUG2_API void set_ghosts(std::span<const Ghost> ghosts);

// --- the renderer's side ---

std::span<const Ghost> pending();
void clear();

} // namespace render::ghost_draws
