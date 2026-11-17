#pragma once

#include <string>

// NOT ORIGINAL - the time of day's clock in the game ([Graphics] tod, tod_cycle, tod_cycle_minutes, tod_levels;
// engine/gfx/rt_daylight.h is its maths; docs/design/vulkan.md "Time of day"). A part of the simulation: once a step
// (engine::render_frame, every step - drawn or not -, before the sun's step) it moves the time on by the step's game
// time (tod_cycle_minutes counts game time: slow motion with it, stopped while the game is paused - as the original's own
// day), works out the light at that time (rt_daylight.h day_light_at) and publishes it (rt::set_day_light) for the effects
// that light by it - the scene's sun (rt_scene.h: kClock), relit, the sky, the shadows, the lamps, the exposure. The
// drawing only reads that state. A run - a demo's playback, at any display rate, with late steps or without - has the
// same time and sun on every step (tools/game/sim_check.ts compares them). It gives the original game's side its look at
// that time:
//   - the original's own time of day (engine/gfx/tod_manager.h): its four states' parameters (each level's
//     TODManager_SetParams - the default morning, afternoon, evening and night of tod_manager.qb, AU's own sun) blended
//     by the sun's elevation (rt_daylight.h tod_blend) and sent to the engine as the manager does - the skater's and the
//     objects' lights, the fog, the level's and the sky dome's colour (spread over steps, a part of the sectors a step,
//     as the manager's own transitions are). The state nearest the time is set with set_tod_instant as the game's own
//     time-of-day menu does, so the level's own scripts for it run (tod_manager_state_transition: the night geometry
//     and fake lights of BO, BA, BE, AU, NO and ST, the cars' headlights - the night geometry is level geometry with
//     collision and trigger scripts: simulation state). Not in a network session: there the settings model limits the
//     time of day to its lighting (settings_model.h limits; rt::TodSettings::lighting_only) - the level keeps the state
//     every player has, as the original never changes it in a multiplayer game. Where the relit mode relights the
//     level (rt_scene.h sun_relight) the scenes' colour is left at the level's own and the light is one path
//     (docs/design/vulkan.md "One lighting path"): the relit level from the lighting state, and what it doesn't relight
//     vertex by vertex lit by the same light where it is (engine/gfx/rt_light_field.h) - the objects' models the level's
//     own colour times the light where each stands, the objects and characters the level's own state's lights (the key
//     light times the direct light, the fill and ambient times the field's indirect light there - pc_lights.cpp), the
//     particles the level's own colour times the level's light as a whole. Where the backend draws the sky (rt_sky) the
//     dome's colour is the level's too.
//   - the original's sun sprite and lens flare (sun::set_sun) at the clock's sun, hidden while it is down (the sun's
//     step - lens_flare.h sun::step - reads it next).
// The level's own day comes from its relit data's baked sun (the start of its file, read when the level arrives - the
// simulation never waits on the drawing for it), else the game's sun. That file's verdict is the level's fact the time
// of day needs (config::set_level_light).
// It leaves everything as it is - nothing called, nothing published but `on` false - while tod=level with no cycle
// (the default: today's look exactly), in the front end (load_mainmenu), with no level loaded, and while the original runs its
// own day (CtodManager::runs_its_day: a career game's cycling time of day, or a cutscene's temporary state): the original
// wins then, and the scene's sun follows it as before (rt_sun_source). A script that sets the level's state
// (tod_manager.h tod_state_sets: the park editor's lighting, THUGPRO's peer) pauses it until tod changes. Turned off
// again (the Options screen), it puts the level's own state back (set_tod_instant, where it moved it) and its sun.
namespace render::day_clock {

// The clock's step (engine::render_frame, once each step the loading screen isn't up, before the views' sun steps).
void step();
// The clock's state in a line (the console's `rt tod`).
[[nodiscard]] const char* status();
// Whether the frame's lighting state is settled: the clock publishes it in the step before the frame is drawn, and the
// level's own scripts for the state it set (tod_manager.h tod_transition_running: the night geometry and fake lights,
// shown and hidden over their frames) have ended (render_pc's first light hold reads it: rt_relit_world.h
// hold_first_light).
[[nodiscard]] bool light_ready();
// (the proof - devtools/demo.cpp) The clock's time and sun now as a line ("" while it doesn't run): a playback writes
// it beside each check point's state, so two runs of a demo can be compared.
[[nodiscard]] std::string state_line();

} // namespace render::day_clock
