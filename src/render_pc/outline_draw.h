#pragma once

#include <cstdint>

// NOT ORIGINAL - the outlines a UI handed over (engine/gfx/outline_draws.h) drawn in the 3D view. render::draw_world
// calls draw after each view's world passes and ghosts (before the depth lines, the full-screen effects and the 2D),
// into the target and depth the world was drawn into, with that view's camera; and drop on a frame without the 3D view.
//
// How: a mask the target's size (an A8R8G8B8 render-target texture) is drawn with the target's own depth - each marked
// geometry's meshes in a flat colour (the texture factor through stage 0, no pixel shader, no blending, no depth
// written): first where they are hidden (depth test off, the outline's hidden colour), then where they show (depth
// tested, its colour, nudged towards the camera so the world's own depth doesn't hide it). Then over the target: for
// each offset within the stroke's radius, a quad over the view taking the mask's colour at that offset, its alpha
// times "not covered here" (stage 1: 1 - the mask's alpha here, times 4 so a dim hidden colour still counts as
// covered), blended over the frame. No pixel shader (both Gpus' fixed-function stages), no stencil.
namespace render::outline_draw {

// The outlines handed over, in the view being drawn. Then they are dropped - unless `keep` (a split view's pass: the
// same outlines again in the next view; outline_draws::clear after the last). Notes whether they could be drawn
// (outline_draws::outlines_drawn): not into a multisampled target, nor without a mask.
void draw(bool keep = false);
void drop();
// The mask let go (it is in the default pool: before a device reset).
void release();

// --- for the meshes' drawing (MeshRef::render, the scenes' passes) ---

// Whether the mask is being drawn: a mesh is drawn in flat_colour (flat_states after its own), its material not handed
// in, no shadows.
bool flat_on();
// The flat colour's states, over what the mesh set: no pixel shader, stage 0 the texture factor, no blending, no
// alpha test, no culling, solid, no depth written; the depth test and bias of the pass.
void flat_states();

} // namespace render::outline_draw
