#pragma once

// NOT ORIGINAL - the lines a UI handed over (engine/gfx/depth_lines.h) drawn in the 3D view. render::draw_world calls
// draw after the viewports' world passes (before the full-screen effects and the 2D), into the target and depth the
// world was drawn into, with the camera of the last viewport drawn (the level editor has one); and drop on a frame
// without the 3D view (the loading screen up), so lines handed over before it aren't drawn after it.
namespace render::depth_lines_draw {

// Each line as a strip facing the camera, its width in pixels at each end; tested against the depth as the line asks
// (depth_lines::Depth), alpha-blended, no depth written, no fog, solid even in wireframe; the renderer's state put back
// as the frame had it. Then the lines are dropped.
void draw();
void drop();

} // namespace render::depth_lines_draw
