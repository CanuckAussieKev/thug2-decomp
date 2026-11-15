#pragma once

#include <vector>

#include "engine/gfx/paint_trails.h"

// NOT ORIGINAL - the paint trails' brushes (engine/gfx/paint_trails.h; GameHooks::paint_brushes): what each skater does
// this step, read from the skaters' own state and never written - its place and orientation (the skater's up is the
// surface's normal on the ground, a ramp, a rail and a wall), its state (the skater state component: on the ground, a
// rail or lip, a wall; walking, driving or bailing - nothing), its combo's running score (its score's pot times the
// multiplier: a local skater's - a remote player's combo isn't sent between players, so its line stays slim and bursts
// where its total rises as the game is sent the scores).
//
// Only in the modes that paint: free skate (one or two players - is_freeskate), single session's free skate
// (is_singlesession) and the online lobby's free skate (netlobby); none anywhere else (the career, the scored games).
namespace skater::paint_brushes {

void gather(std::vector<render::paint::Brush>* brushes);

// The script command PaintTrailsClear: the session's paint wiped (the console's, a menu's). Registered at start-up with
// the other added commands (app/extensions.cpp register_added_script_commands).
void register_script_functions();

} // namespace skater::paint_brushes
