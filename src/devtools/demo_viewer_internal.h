#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "devtools/demo_camera_path.h"
#include "devtools/demo_viewer.h"

namespace gfx {
class ViewCam;
}

// NOT ORIGINAL - the demo viewer's state, shared by its logic (demo_viewer.cpp) and its panels (demo_viewer_ui.cpp).
namespace devtools::demo_viewer {

enum class CameraMode : uint8_t { kGame, kFree, kOrbit, kFixed, kPath };

inline constexpr std::array<float, 8> kSpeeds = {0.1F, 0.25F, 0.5F, 0.75F, 1.0F, 1.5F, 2.0F, 4.0F};
inline constexpr float kFreeSpeed = 600.0F;    // the free camera's, game units a second
inline constexpr float kOrbitPitch = 15.0F;    // the orbit camera's: degrees down onto the skater
inline constexpr float kOrbitDistance = 220.0F; // and its distance
inline constexpr std::size_t kPadAxes = 6;     // (SDL's gamepad axes: two sticks' x and y, two triggers)

// (the members by size: the clock's times and pointers first, the floats, the flags last)
struct State {
    Host host;
    Playback playback;
    camera_path::Path path;
    std::string export_path;

    // --- the times (the seek's, the progress drawn, the path's change, the last loop) ---
    std::chrono::steady_clock::time_point seek_started;
    std::chrono::steady_clock::time_point progress_drawn;
    std::chrono::steady_clock::time_point path_changed_at;
    std::chrono::steady_clock::time_point last_tick;

    // --- the transport ---
    double slow = 0.0;              // (slow motion: the logic frames owed, a fraction)
    int64_t target = -1;            // seeking: the frame to show (-1: none)
    int64_t seek_started_frame = 0;
    double last_seek_seconds = 0.0;
    double last_seek_rate = 0.0;    // logic frames a second while seeking

    // --- the frame's camera: the one put elsewhere this frame (null: none), as it was ---
    gfx::ViewCam* camera = nullptr;
    maths::Mat4 saved_matrix{};
    maths::Vec4 saved_position{};

    // --- the cameras ---
    camera_path::Pose free;         // the free camera
    camera_path::Pose shown;        // the pose drawn (the game's when the game's is drawn)
    camera_path::Vec3 game_position;
    camera_path::Vec3 game_forward;
    camera_path::Vec3 skater;       // where the skater was drawn
    std::array<float, kPadAxes> pad{}; // a pad's sticks (-1..1) and triggers (0..1)
    float speed = 1.0F;
    float free_speed = kFreeSpeed;
    float orbit_yaw = 0.0F;         // degrees, around the skater (with `orbit_follow`: from behind it)
    float orbit_pitch = kOrbitPitch;
    float orbit_distance = kOrbitDistance;
    float fov = 0.0F;               // the field of view asked for (0: the game's / the pose's)
    float game_angle = 0.0F;
    float skater_heading = 0.0F;
    float look_x = 0.0F;            // the mouse's motion since the last loop (pixels)
    float look_y = 0.0F;
    float wheel = 0.0F;
    uint32_t fast_count = 0;        // (fast: the loops since the last drawn)
    uint32_t export_from = 0;
    uint32_t export_to = 0;
    int fixed = 0;                  // the fixed camera looked through
    int selected_key = -1;
    int cursor_before = -1;
    int show_tab = -1;              // the right panel's tab to bring up (0 Camera, 1 Keys, 2 Compare; -1: as it is)

    // --- the flags ---
    CameraMode mode = CameraMode::kGame;
    CameraMode mode_before_export = CameraMode::kGame;
    bool active = false;
    bool paused = false;
    bool step = false;              // one logic frame, then paused again
    bool restarting = false;        // a seek back: the level loading again (its frames not counted yet)
    bool resume = false;            // playing again once the seek is done
    bool loop = false;
    bool orbit_follow = true;
    bool path_dirty = false;
    bool hud = true;
    bool game_noted = false;
    bool skater_seen = false;
    bool ui_visible = true;
    bool looking = false;           // the right mouse button held (the free / orbit camera turns)
    bool exporting = false;
};

State& state();

// The frame shown (the last logic frame run) and the demo's last.
uint32_t frame_shown();
uint32_t last_frame();
// The transport's steps (the panels' and the keys').
void toggle_play();
void seek(int64_t frame, bool keep_playing);
void step_frames(int frames);
void set_speed(float speed);
void set_loop_in();
void set_loop_out();
// The camera path's edits: a key at the frame shown from the camera drawn (its index), deleted, saved.
int add_key_here();
void delete_selected_key();
void path_changed();
void save_path();
std::string camera_name(CameraMode mode);
void set_mode(CameraMode mode);
// The fixed cameras: one placed where the camera drawn is.
void add_fixed_here();
// SAVE AS VIDEO (its result).
std::string start_export(const std::string& path);
void leave();

// The panels (demo_viewer_ui.cpp): drawn over the frame; an event for them (true: theirs); whether the mouse is over a
// panel, a field being typed in; the device's objects dropped.
void draw_panels();
bool panel_event(const void* native_event, bool mouse, bool keyboard);
bool panels_want_mouse();
bool panels_want_keyboard();
void panels_device_lost();

} // namespace devtools::demo_viewer
