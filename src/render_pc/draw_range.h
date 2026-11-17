#pragma once

// NOT ORIGINAL - the drawing's distance cull for objects with a suspend component (their lod_dist2), the part of the
// original's dormancy that the drawing keeps. The original hid an object's model in the simulation beyond lod_dist2 or
// out of the view (objects::DormancyModule::refresh_shown_state); outside a net game the simulation now leaves the
// model on (objects::dormancy_by_distance), and the drawing culls instead:
//   - the view: the renderer's own culling of what is out of the frustum or occluded;
//   - the distance: begin_frame switches off the instances of every model further from viewport 0's camera (as the game
//     left it: an extension's camera, placed later, isn't asked) than its lod_dist2 x [Graphics] lod_distance_scale; end_frame switches them back on,
//     so the simulation reads nothing of it. Not in split screen (the original drew every model in more than one
//     viewport), not for a model without LOD or a level object's (DormancyModule::draw_range 0).
// In a net game the simulation still hides models as the original (its lod_dist2, unscaled): there the cull only
// takes off more with a scale below 1.
namespace render::draw_range {

// draw_world's first step (before frame_interp's): the models out of range switched off for this frame.
void begin_frame();
// draw_world's last step (after frame_interp's): switched back on.
void end_frame();
// The instances' models switched off by the last begin_frame (for the log / the check).
int last_culled();

} // namespace render::draw_range
