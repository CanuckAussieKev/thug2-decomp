#pragma once

// NOT ORIGINAL - the Scene panel's tool strip (ours, at the user's request: Unity's Tools overlay in the Scene view's
// top-left corner): small buttons down the left edge, under the view's corner menu - Move (W), Rotate (E), Scale (R),
// then World / Local (`) - the active tool lit in the accent colour, a tooltip on each with its key (the keys as the
// action registry has them now: level_editor_actions.h). They set what the keys set (xform::Settings' mode and space -
// level_editor_transform.h): the toolbar's Move / Rotate / Scale and World / Local buttons do the same.
// Its icons are drawn with lines (no icon font): four arrows, a turning arrow, a box pulled out from a corner; a globe
// for world axes, a box for local ones.
namespace frontend::level_editor::scene_tools {

// The strip drawn into the current window (the Scene panel) with its top-left corner at (x, y) - screen pixels; `scale`
// the UI's. True while the mouse is over it (the Scene's clicks aren't the view's then).
bool draw(float x, float y, float scale);

} // namespace frontend::level_editor::scene_tools
