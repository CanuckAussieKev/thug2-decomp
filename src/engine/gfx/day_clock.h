#pragma once

// NOT ORIGINAL - the time of day's clock in the game ([Graphics] tod, tod_cycle, tod_cycle_minutes, tod_levels;
// engine/gfx/rt_daylight.h is its maths; docs/design/vulkan.md "Time of day"). Once a frame (render_pc's draw_world,
// before the views) it works out the time and the light (rt_daylight.h day_light_at) and publishes them
// (rt::set_day_light) for the effects that light by them - the scene's sun (rt_scene.h: kClock), relit, the sky, the
// shadows, the lamps, the exposure - and gives the original game's side its look at that time:
//   - the original's own time of day (engine/gfx/tod_manager.h): its four states' parameters (each level's
//     TODManager_SetParams - the default morning, afternoon, evening and night of tod_manager.qb, AU's own sun) blended
//     by the sun's elevation (rt_daylight.h tod_blend) and sent to the engine as the manager does - the skater's and the
//     objects' lights, the fog, the level's and the sky dome's colour (spread over frames, a part of the sectors a
//     frame, as the manager's own transitions are). The state nearest the time is set with set_tod_instant as the
//     game's own time-of-day menu does, so the level's own scripts for it run (tod_manager_state_transition: the night
//     geometry and fake lights of BO, BA, BE, AU and NO, the cars' headlights). Where the relit mode relights the level
//     (rt_scene.h sun_relight) it already dims the bake: the level's colour is left at its own; where the backend draws
//     the sky (rt_sky) the dome's colour is too.
//   - the original's sun sprite and lens flare (sun::set_sun) at the clock's sun, hidden while it is down.
// It leaves everything as it is - nothing called, nothing published but `on` false - while tod=level with no cycle
// (the default: today's look exactly), in the front end (load_mainmenu), with no level loaded, and while the original runs its
// own day (CtodManager::runs_its_day: a career game's cycling time of day, or a cutscene's temporary state): the original
// wins then, and the scene's sun follows it as before (rt_sun_source). Turned off again (the Options screen), it puts
// the level's own state back (set_tod_instant) and its sun. Online it is local: each player sees their own time.
namespace render::day_clock {

// The frame's clock (render_pc's draw_world, once a frame before the views are drawn).
void frame();
// The clock's state in a line (the console's `rt tod`).
[[nodiscard]] const char* status();

} // namespace render::day_clock
