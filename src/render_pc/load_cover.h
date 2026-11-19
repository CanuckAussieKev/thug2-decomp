#pragma once

// NOT ORIGINAL - (round LS) the loading screen kept over a level's first frames until the level is ready to be seen.
//
// The original takes its loading screen down when the level's game flow says so (HideLoadingScreen: GameFlow_PlayRun,
// a few frames after the level's scripts have run and the skaters are in - the moment the player has control), and
// the next frame drawn is the level's first. Here the renderer still has work left at that moment that only the
// level's first frames do: the ray tracing scene built, the relit light written for the level's light, the time of
// day's state set by the level's own scripts, the pipelines the first views need made. The original's flag
// (engine_parts loading_screen_up) still goes down at the original's moment - the simulation and the scripts are the
// original's, held from there (below) -; what is shown differs: those frames are drawn in full behind the loading
// screen's picture, bar and console (the main loop draws them now - the loading screen's own timers are stopped), and
// the cover lifts on the first frame that is ready.
//
// Ready - one rule, read from the renderer's own state each frame (no count of frames, no per-level wait):
//   - the level's first light: rt_relit_world.h hold_first_light (the clock's light published, the level's own scripts
//     for its time of day ended, the level relit for that light - or relighting not in play: ready at once), and
//   - the frame drew only from what was ready: no pipeline made since the previous frame's cover (Gpu::pipelines_made -
//     the passes' pipelines are made the first time they draw or trace; Direct3D 9 makes none, so it is ready at once),
//   - (round LS2) the lighting's cut shown (rt_daylight.h cut_settling: the stores written for the light the clock
//     published - the eye follows the picture without adapting until then; at most kCutMostSeconds),
//   - (round LS2) the camera's exposure settled (Gpu::exposure_settled - the auto exposure's eye at its target for this
//     picture and the frame exposed there; each covered frame starts the eye at its target, as a new view: no
//     adaptation after the reveal; nothing metered - Direct3D 9, a fixed or hand-set camera - is settled at once).
// (round LS2) Meanwhile the simulation is held (engine/gfx/level_cover.h - set here: from the hide until the first ready
// frame): the player gets control on the first frame shown, and the simulation's next step after the hide runs on the
// loop after it - the original's state at its hide moment is where it waits.
// A movie playing is its own picture: ready. With no loading screen before it (a frozen frame kept - a cutscene's -, the
// editor mode's loading dialog) nothing is covered.
//
// Never stuck: each term is ready when it doesn't apply (no ray tracing, relit off, no time of day, another backend); a
// renderer that keeps making pipelines every frame is a defect, and the cover still lifts after kMostSeconds - logged
// "not ready" so it is seen.
namespace render::load_cover {

inline constexpr double kMostSeconds = 10.0;

// show_loading_screen: a loading screen up - `kept`: the last frame kept (no picture: nothing will be covered), `blank`:
// black instead of the picture; `dialog`: the editor mode's loading dialog instead (nothing covered either).
void screen_shown(bool kept, bool blank, bool dialog);
// hide_loading_screen: the original's moment - the cover starts if a loading screen was shown since the last one.
void screen_hidden();
// draw_world, while the loading screen is up (the next level's first light held).
void loading_frame();
// draw_world, after the frame's world and 2D: whether this frame stays covered (called once a frame - it reads the
// readiness and ends the cover, logging how long the level's first frames took).
[[nodiscard]] bool frame_covered();
// The cover over the frame (after frame_covered said so, the viewport the whole screen): the loading screen's picture
// (or black), its bar by the time since it began, the loading console - as the loading screen's own frame draws them.
void draw_cover();

} // namespace render::load_cover
