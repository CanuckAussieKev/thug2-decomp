#pragma once

#include <string>
#include <vector>

#include "editor/level_editor_chains.h"
#include "editor/level_editor_rail_tools.h"

namespace frontend::level_editor::overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's Auto Rails (ours; Experimental menu - see level_editor_rail_tools.h): the level's
// collision scanned for edges a skater could grind, shown as dashed rails with their limits on sliders, and made into
// rails in one undo step.
//
// An edge is grindable when one of its faces looks up (normal y >= kTopNormalY - its terrain and piece are the rail's)
// and:
//   - its other face (none: an open edge, taken as 90 degrees) turns away from it by at least the angle limit, and
//     downwards (the other face's third corner not above the upper face's plane: a ledge's top, a kerb - not the
//     corner where a floor meets a wall);
//   - it slopes no more than the slope limit;
//   - nothing is just above it (the collision ray up kClearance from it meets nothing);
//   - the ground falls away beside it (the ray down beside it, on the side away from the upper face, meets nothing
//     within kMinDrop - not a seam between two floor pieces);
//   - no rail runs along it already (one within kRailedReach, parallel).
// The edges that pass are joined as Rail from Edges joins picked ones (join_edges), runs shorter than the least length
// left out. Scanned: the collision objects of the level's sectors that are on (a moved piece's shared collision -
// level_editor_shared_coll.h - isn't), faces that are collidable and not triggers; around the camera (a radius), of
// the selected scenery, or the whole level (at most kMaxFaces faces).
namespace frontend::level_editor::rail_tools::auto_rails {

constexpr float kTopNormalY = 0.7F;
constexpr float kClearance = 48.0F;
constexpr float kMinDrop = 4.0F;
constexpr float kRailedReach = 8.0F;
constexpr int kMaxFaces = 300000;
constexpr int kMaxRails = 400;

// The window (open / closed: the Experimental menu's Auto Rails...).
bool get_open();
void set_open(bool open);
void halt();
// Once a frame: the window's requests.
void tick_frame();
// The candidates over the view (dashed).
void add_overlay(const overlays::Painter& painter);
void window(float view_x, float view_y, float view_width, float scale);
std::string describe();

} // namespace frontend::level_editor::rail_tools::auto_rails

namespace frontend::level_editor::rail_tools {

// The rails runs of `edges` make: their points kRailLift above the edges (straight stretches one segment), the terrain
// matching their faces' commonest, a trick object of their commonest piece's cluster when it is one.
std::vector<chains::NewRail> rails_from(const std::vector<Edge>& edges, const std::vector<Run>& runs);

} // namespace frontend::level_editor::rail_tools
