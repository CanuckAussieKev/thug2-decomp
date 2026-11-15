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
//     (rt_scene.h sun_relight) the scenes' colour is left at the level's own and the light is one path (docs/design/
//     vulkan.md "One lighting path"): the relit level from the lighting state, and what it doesn't relight vertex by
//     vertex lit by the same light where it is (engine/gfx/rt_light_field.h) - the objects' models the level's own
//     colour times the light where each stands, the objects and characters the level's own state's lights (the key
//     light times the direct light, the fill and ambient times the field's indirect light there - pc_lights.cpp), the
//     particles the level's own colour times the level's light as a whole. Where the backend draws the sky (rt_sky)
//     the dome's colour is the level's too.
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
// Whether the frame's lighting state is settled: the clock isn't wanted (tod=level, the front end, the original's own
// day) or it has published the light at its time - false while it waits for the level's baked sun (render_pc's first
// light hold: rt_relit_world.h hold_first_light).
[[nodiscard]] bool light_ready();

} // namespace render::day_clock
