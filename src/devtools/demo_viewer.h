#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ext {
struct LoopPace;
}
namespace gfx {
class ViewCam;
}

// NOT ORIGINAL - the demo viewer (ours, added at the user's request: "a replay viewer / movie editor for demos ... with a
// split-screen settings comparison"; the idea of a racing game's replay editor, our own UI): a demo (devtools/demo.h)
// played in the level with a timeline, cameras and keyframes over it, its panels drawn with Dear ImGui (devtools/
// demo_viewer_ui.cpp). Opened from the pause menu's DEMOS > WATCH REPLAY, `-demo <name> -viewer`, or the console's
// `demo view <name>`. docs/demos.md "The viewer" has the keys.
//
//   The timeline: play / pause, scrub, a frame back / forward, the speed (0.1x - 4x: slow motion draws every loop, a
//     logic frame only every few - the main loop's pace, ext::LoopPace::hold), a loop region (in / out).
//   Seeking: the game's logic only runs forward, so a frame before the one shown is reached by loading the level again
//     the way the demo started (its random numbers, clocks and input) and running the logic flat out, undrawn, to it;
//     a frame after it by running on. No snapshot of the game is kept (its state is spread over the script heap, the
//     objects, the physics and the sound - nothing restores all of it exactly), so the check track's drift stays 0.
//   Cameras: the game's (as recorded), a free one (WASD + Q / E, the right mouse button to look, the wheel for speed;
//     a pad's sticks), one orbiting the skater (the right mouse button to turn, the wheel for distance; it can follow
//     the skater's heading), fixed ones placed in the level, and the path - keyframes on the timeline (position,
//     rotation, field of view; smooth, linear, ease or hold between keys - devtools/demo_camera_path.h), kept beside
//     the demo as <demo>.camera.txt. The game's camera keeps running underneath: the one drawn is put back after the
//     frame, and the check track records the game's.
//   The HUD on / off, the field of view; the split comparison's panel (devtools/compare_split.h).
//   SAVE AS VIDEO: the loop region (or the whole demo from the level up) rendered at [demo] video_fps with the camera
//     path (the keyframes; with none the camera chosen), the graphics as set and the split if it's on - the panels
//     never in it: thug2-decomp-videos\<demo>_viewer.mp4.
// While the viewer is up the game reads no key or pad (the demo's input drives the skater); Esc (or F6) leaves it - the
// controls are then the player's from the frame shown.
namespace devtools::demo_viewer {

// What the demos' side does for the viewer (demo.cpp).
struct Host {
    bool (*restart)() = nullptr;          // the playback started again from the level's load (false: it couldn't)
    uint32_t (*frames_done)() = nullptr;  // frames run since the level's load (0: none yet / loading again)
    bool (*export_video)(const std::string& path, uint32_t from, uint32_t to) = nullptr; // frames from..to as a video
    void (*cancel_export)() = nullptr;
    void (*stop)() = nullptr;             // the playback stopped: the controls the player's
    std::string (*video_path)(const std::string& name) = nullptr;
    std::string (*demos_folder)() = nullptr;
};

struct Playback {
    std::string name;
    uint32_t frames = 0;   // the demo's
    uint32_t up_frame = 0; // the level up (the check track's first frame)
    std::string export_video; // (-demo-video with -viewer: saved with the camera path at once)
};

// Start-up: the event handler, the host.
void prepare_input(const Host& host);
// A viewer's playback began (its level loaded: the first time, or again for a seek back) / ended.
void begin(const Playback& playback);
void end();
bool active();
// The viewer's video of its range is done (closed).
void video_done();

// The main loop's pace this loop (false: not the viewer's - it isn't up).
bool loop_pace(ext::LoopPace* pace);
// Viewport 0's camera placed (the viewer's, the game's noted first), its field of view, and the game's camera put back
// once the frame is drawn.
void place_camera(gfx::ViewCam* camera, int viewport);
float view_angle(float angle);
void frame_presented();
// The game's own camera this frame when the viewer drew another (`moved` false: the game's was drawn).
struct GameView {
    std::array<float, 3> position{};
    std::array<float, 3> forward{};
    bool moved = false;
};
GameView game_view();
bool draws_hud();
// The panels over the frame (after a video's picture).
void draw();
void device_lost();

// The console's `viewer ...`: what happened.
std::string console_command(const std::string& args);

} // namespace devtools::demo_viewer
