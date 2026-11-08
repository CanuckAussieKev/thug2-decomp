#pragma once

// NOT ORIGINAL - the ghosts a UI handed over (engine/gfx/ghost_draws.h) drawn in the 3D view. render::draw_world calls
// draw after each view's world passes (before the depth lines, the full-screen effects and the 2D), into the target and
// depth the world was drawn into, with that view's camera; and drop on a frame without the 3D view.
namespace render::ghost_draw {

// Each ghost's shown and hidden meshes (not the ones kept out of every view) as the world pass draws them - its
// opaque ones first, then its transparent - under its own world matrix; the opaque ones blended over what is behind at
// the ghost's opacity (the game's fixed-alpha blend forced over their own), the transparent with their own blend.
// Depth tested and written; the world matrix and the override put back. Then the ghosts are dropped - unless `keep` (a
// split view's pass: the same ghosts are drawn again in the next view; ghost_draws::clear after the last).
void draw(bool keep = false);
void drop();

} // namespace render::ghost_draw
