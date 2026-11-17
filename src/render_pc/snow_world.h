#pragma once

// NOT ORIGINAL - snow (engine/gfx/snow.h; docs/design/vulkan.md "Snow"): render_pc reads the loaded world for the snow -
// where it settles (the collision seen from above: snow::make_sky_map, again whenever the loaded scenes change) and,
// once a simulation step, what presses into it (the instances drawn with a skeleton - skaters and peds - and the wheels
// of those drawn with a few bones - vehicles). Read only: nothing of the game's is written. Whether the level is outdoors
// (a sky scene loaded) is told to the settings (config::set_level_outdoor: snow's rule) whenever the loaded scenes
// change, snow or not - a walk of the few scene nodes a step; with snow off (Direct3D 9, snow=0) nothing else is done.
// The surface trails' other user, the paint (engine/gfx/paint_trails.h), is stepped here too, on the same steps: the
// game's brushes (GameHooks::paint_brushes), the loaded scenes' key its level session - while paint_trails is on.
#include <vector>

#include "engine/gfx/snow.h"

namespace render::snow_world {

// Every collidable face of the loaded world's sectors (the skies' left out), an indoor sector's marked; `has_sky` whether
// a sky scene is loaded. (Also the water bodies' collision - render_pc/water_bodies.h.)
[[nodiscard]] std::vector<snow::Face> collision_faces(bool* has_sky);

// Device::step_frame (every simulation step, drawn or not): the world's sky map made when the loaded scenes changed (an
// indoor level - one without a sky scene - gets none, logged), then the pressers handed to snow::step with the step's
// time. Everything on the simulation's steps: the same positions make the same trails, however the frames are drawn.
void step();

} // namespace render::snow_world
