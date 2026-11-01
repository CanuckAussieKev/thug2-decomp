#pragma once

#include <string>

namespace gfx {
class ViewCam;
}

// NOT ORIGINAL - test views (ours, added at the user's request: "add an engine function that lets you teleport to a
// given position, while standing (not skating) so you don't move, then you can screenshot every frame from that
// position"; not the game's and not PartyMod's). Used by the test-automation channel's `teleport` and `camera`
// commands (frontend/automation.h) and the script command below; the frame series that goes with them is the
// screenshot code's (platform/screenshot.h, start_series).
//
// - Teleport: the local skater put at a point STANDING (walking, no momentum), on the ground below it (the game's
//   collision ray from 12 units above the point to 4000 below; the point as given if nothing is hit), facing a
//   heading (degrees, 0 = +z, 90 = +x - as the screenshot sidecar gives it) or the way it faced. The game's own way:
//   the skater's custom restart (SetCustomRestart / SkipToCustomRestart walking: Rider::SkipToCustomRestart -
//   jump_to_spawn walking puts it in the walk state, momentum gone, the camera reset behind it), with the skater's own
//   custom restart put back afterwards. Nobody presses anything in a test, so it stands still.
// - Camera lock: viewport 0's camera drawn at an exact pose (a position and a view direction, level horizon) every
//   frame until unlocked - the game's camera keeps running underneath. Frames from the same pose are comparable
//   pixel for pixel. Wins over the level editor's camera while set. With a drift, the position moves by it every game
//   frame (from the frame it was locked): a slow, exact camera move - surfaces barely change from frame to frame,
//   z-fighting ones flip (thug2-level-converter-core/lib/frame_diff.ts).
// - Script command (for level scripts and the console): TeleportSkaterStanding pos=(x, y, z) [heading=<degrees>] -
//   false when there's no local skater.
namespace frontend::test_view {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

struct Vec3 {
    float x, y, z;
};
// A camera's rows for a view direction (game convention: forward is -back; right = up(0,1,0) x back, up = back x
// right). False for a zero direction or one straight up / down (no horizon).
bool camera_rows(Vec3 direction, Vec3* right, Vec3* up, Vec3* back);
// A standing skater's rows for a heading in degrees (0 = +z, 90 = +x): right, up (0, 1, 0), at.
void standing_rows(float heading_degrees, Vec3* right, Vec3* up, Vec3* at);

// --- the game ---------------------------------------------------------------------------------------------------

// The local skater to `position` standing; `out_position` where it ended (after the ground snap). "" or why not.
// `on_board`: on its board instead (the custom restart's skating form - still, no momentum; a test that skates on from
// there: automation's `teleport ... board`).
std::string jump_to(Vec3 position, bool has_heading, float heading_degrees, Vec3* out_position, bool on_board = false);
// Viewport 0's camera held at `position` looking along `direction`, moved by `drift` a game frame (false: a direction
// camera_rows refuses).
bool lock_camera(Vec3 position, Vec3 direction, Vec3 drift = {0.0f, 0.0f, 0.0f});
void unlock_camera();
bool camera_locked();
// The renderer, a viewport's camera before it's drawn (engine/gfx/draw_frame.cpp): placed while locked.
void place_camera(gfx::ViewCam* camera, int viewport);
// TeleportSkaterStanding (sys/main.cpp, after the game's own commands).
void register_script_functions();

} // namespace frontend::test_view
