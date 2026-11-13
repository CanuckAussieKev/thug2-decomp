// NOT ORIGINAL - the demo viewer (demo_viewer.h): its transport, cameras and keys. The panels are demo_viewer_ui.cpp.

#include "devtools/demo_viewer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <istream>
#include <sstream>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/core/parse_number.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "devtools/compare_split.h"
#include "devtools/demo_camera_path.h"
#include "devtools/demo_viewer_internal.h"
#include "engine/ext/extension.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/view_cam.h"
#include "engine/objects/entity.h"
#include "engine/pad_input/input_event.h"
#include "engine/pad_input/input_source.h"
#include "platform/run_log.h"

namespace devtools::demo_viewer {

namespace {

using camera_path::Pose;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr double kFramesPerSecond = 60.0;
constexpr double kProgressSeconds = 0.1;  // a seek draws a frame (its progress) this often
constexpr double kSaveSeconds = 0.5;      // the camera path saved this long after its last change
constexpr double kLongestTick = 0.1;      // (a camera's step: at most this much time at once)
constexpr float kLookDegreesPerPixel = 0.15F;
constexpr float kPadLookDegrees = 120.0F; // a second, the stick pushed fully
constexpr float kFastFactor = 4.0F;       // Shift
constexpr float kSlowFactor = 0.25F;      // Ctrl
constexpr float kWheelStep = 1.2F;
constexpr float kLeastSpeed = 25.0F;
constexpr float kMostSpeed = 20000.0F;
constexpr float kOrbitHeight = 45.0F;     // above the skater's origin (its feet)
constexpr float kLeastDistance = 40.0F;
constexpr float kMostDistance = 3000.0F;
constexpr float kPadDeadZone = 0.15F;
constexpr int16_t kPadMost = 32767;
constexpr int kSecondFrames = 60;
constexpr float kHalfTurn = 180.0F;
// (SDL's gamepad axes: the sticks' x and y, the triggers)
constexpr std::size_t kPadLeftX = 0;
constexpr std::size_t kPadLeftY = 1;
constexpr std::size_t kPadRightX = 2;
constexpr std::size_t kPadRightY = 3;
constexpr std::size_t kPadLeftTrigger = 4;
constexpr std::size_t kPadRightTrigger = 5;
constexpr float kTurn = 360.0F;
constexpr float kOneX = 1.0F;

State& the_state()
{
    static core::NoDestroy<State> value;
    return value.get();
}

double seconds_since(Clock::time_point then) { return std::chrono::duration<double>(Clock::now() - then).count(); }

uint32_t frames_done()
{
    const State& view = the_state();
    return view.host.frames_done != nullptr ? view.host.frames_done() : 0;
}

std::string sidecar_path()
{
    const State& view = the_state();
    return (view.host.demos_folder != nullptr ? view.host.demos_folder() : std::string()) + view.playback.name + ".camera.txt";
}

void load_path()
{
    State& view = the_state();
    view.path = {};
    view.selected_key = -1;
    std::string text;
    if (!fs::read_text(sidecar_path(), &text)) {
        return;
    }
    std::string why;
    if (!camera_path::read_text(text, &view.path, &why)) {
        get_log("viewer: %s not read - %s (a new path started)", sidecar_path().c_str(), why.c_str());
        view.path = {};
        return;
    }
    view.loop = view.path.loop_in >= 0 && view.path.loop_out > view.path.loop_in;
    get_log("viewer: %s - %zu key(s), %zu fixed camera(s)", sidecar_path().c_str(), view.path.keys.size(),
            view.path.fixed.size());
}

// The game's camera as a pose (its field of view the game's).
Pose game_pose()
{
    const State& view = the_state();
    Pose pose;
    pose.position = view.game_position;
    const camera_path::Angles angles = camera_path::angles_toward(view.game_forward);
    pose.yaw = angles.yaw;
    pose.pitch = angles.pitch;
    pose.fov = view.game_angle;
    return pose;
}

camera_path::Vec3 orbit_target()
{
    const State& view = the_state();
    return {.x = view.skater.x, .y = view.skater.y + kOrbitHeight, .z = view.skater.z};
}

Pose orbit_pose()
{
    const State& view = the_state();
    Pose pose;
    pose.yaw = view.orbit_yaw + (view.orbit_follow ? view.skater_heading : 0.0F);
    pose.pitch = -view.orbit_pitch;
    const camera_path::Vec3 forward = camera_path::forward_of(pose);
    const camera_path::Vec3 target = orbit_target();
    pose.position = {.x = target.x - (forward.x * view.orbit_distance),
                     .y = target.y - (forward.y * view.orbit_distance),
                     .z = target.z - (forward.z * view.orbit_distance)};
    pose.fov = view.fov;
    return pose;
}

// The pose the viewer draws now (none: the game's camera). `fraction`: how far toward the next logic frame (slow motion).
std::optional<Pose> viewer_pose(double fraction)
{
    const State& view = the_state();
    switch (view.mode) {
    case CameraMode::kFree: {
        Pose pose = view.free;
        pose.fov = view.fov;
        return pose;
    }
    case CameraMode::kOrbit:
        if (!view.skater_seen) {
            return std::nullopt;
        }
        return orbit_pose();
    case CameraMode::kFixed:
        if (view.fixed < 0 || static_cast<std::size_t>(view.fixed) >= view.path.fixed.size()) {
            return std::nullopt;
        }
        return core::at(view.path.fixed, static_cast<std::size_t>(view.fixed)).pose;
    case CameraMode::kPath: {
        Pose pose;
        if (!camera_path::pose_at(view.path, static_cast<double>(frame_shown()) + fraction, &pose)) {
            return std::nullopt;
        }
        return pose;
    }
    case CameraMode::kGame:
    default:
        return std::nullopt;
    }
}

bool key_held(const input::KeyState& keys, SDL_Scancode key)
{
    return static_cast<std::size_t>(key) < keys.size() && core::at(keys, static_cast<std::size_t>(key)) != 0U;
}

float pad_axis(std::size_t axis)
{
    const float value = core::at(the_state().pad, axis);
    return std::abs(value) < kPadDeadZone ? 0.0F : value;
}

// The cameras moved by the keys, the mouse and a pad since the last loop (real time: the same at any playback speed).
void move_cameras()
{
    State& view = the_state();
    const Clock::time_point now = Clock::now();
    const auto dt = static_cast<float>(std::min(kLongestTick, std::chrono::duration<double>(now - view.last_tick).count()));
    view.last_tick = now;
    const float look_x = view.look_x;
    const float look_y = view.look_y;
    const float wheel = view.wheel;
    view.look_x = 0.0F;
    view.look_y = 0.0F;
    view.wheel = 0.0F;
    if (view.mode == CameraMode::kFree) {
        view.free.yaw += (look_x * kLookDegreesPerPixel) + (pad_axis(kPadRightX) * kPadLookDegrees * dt);
        view.free.pitch = std::clamp(view.free.pitch - (look_y * kLookDegreesPerPixel) - (pad_axis(kPadRightY) * kPadLookDegrees * dt),
                                  -camera_path::kMostPitch, camera_path::kMostPitch);
        if (view.free.yaw > kHalfTurn) {
            view.free.yaw -= kTurn;
        } else if (view.free.yaw < -kHalfTurn) {
            view.free.yaw += kTurn;
        }
        if (wheel != 0.0F) {
            view.free_speed = std::clamp(view.free_speed * std::pow(kWheelStep, wheel), kLeastSpeed, kMostSpeed);
        }
        float right = pad_axis(kPadLeftX);
        float ahead = -pad_axis(kPadLeftY);
        float rise = core::at(view.pad, kPadRightTrigger) - core::at(view.pad, kPadLeftTrigger);
        if (!panels_want_keyboard()) {
            const input::KeyState keys = input::keys();
            right += (key_held(keys, SDL_SCANCODE_D) ? kOneX : 0.0F) - (key_held(keys, SDL_SCANCODE_A) ? kOneX : 0.0F);
            ahead += (key_held(keys, SDL_SCANCODE_W) ? kOneX : 0.0F) - (key_held(keys, SDL_SCANCODE_S) ? kOneX : 0.0F);
            rise += (key_held(keys, SDL_SCANCODE_E) ? kOneX : 0.0F) - (key_held(keys, SDL_SCANCODE_Q) ? kOneX : 0.0F);
            float factor = 1.0F;
            if (key_held(keys, SDL_SCANCODE_LSHIFT) || key_held(keys, SDL_SCANCODE_RSHIFT)) {
                factor = kFastFactor;
            } else if (key_held(keys, SDL_SCANCODE_LCTRL) || key_held(keys, SDL_SCANCODE_RCTRL)) {
                factor = kSlowFactor;
            }
            right *= factor;
            ahead *= factor;
            rise *= factor;
        }
        const camera_path::Rows rows = camera_path::rows_of(view.free);
        const float step = view.free_speed * dt;
        view.free.position.x += ((rows.right.x * right) - (rows.back.x * ahead)) * step;
        view.free.position.y += (((rows.right.y * right) - (rows.back.y * ahead)) + rise) * step;
        view.free.position.z += ((rows.right.z * right) - (rows.back.z * ahead)) * step;
    } else if (view.mode == CameraMode::kOrbit) {
        view.orbit_yaw += (look_x * kLookDegreesPerPixel) + (pad_axis(kPadRightX) * kPadLookDegrees * dt);
        view.orbit_pitch = std::clamp(view.orbit_pitch + (look_y * kLookDegreesPerPixel) + (pad_axis(kPadRightY) * kPadLookDegrees * dt),
                                   -camera_path::kMostPitch, camera_path::kMostPitch);
        if (wheel != 0.0F) {
            view.orbit_distance = std::clamp(view.orbit_distance / std::pow(kWheelStep, wheel), kLeastDistance, kMostDistance);
        }
    }
}

// The seek reached its frame: how fast it ran logged, paused or playing again as it was.
void finish_seek(uint32_t done)
{
    State& view = the_state();
    view.last_seek_seconds = seconds_since(view.seek_started);
    const auto run = static_cast<double>(done) - static_cast<double>(view.seek_started_frame);
    view.last_seek_rate = view.last_seek_seconds > 0.0 ? run / view.last_seek_seconds : 0.0;
    get_log("viewer: at frame %u - the seek ran %.0f frame(s) in %.2f s (%.0f a second, %.1fx real time)%s", done - 1,
            run, view.last_seek_seconds, view.last_seek_rate, view.last_seek_rate / kFramesPerSecond,
            view.seek_started_frame == 0 ? ", the level loaded again" : "");
    view.target = -1;
    view.paused = !view.resume;
    view.slow = 0.0;
}

// A loop of a seek or a video's run-up: flat out, a frame drawn now and then to show the progress.
void run_flat_out(ext::LoopPace* pace)
{
    State& view = the_state();
    pace->flat_out = true;
    pace->skip_draw = true;
    if (seconds_since(view.progress_drawn) >= kProgressSeconds) {
        view.progress_drawn = Clock::now();
        pace->skip_draw = false;
    }
}

void save_if_due()
{
    const State& view = the_state();
    if (view.path_dirty && seconds_since(view.path_changed_at) >= kSaveSeconds) {
        save_path();
    }
}

// The viewer's tools' keys (C, K, Delete, I, O, L, U, V, Esc): true when the key was one.
bool tool_key(int key)
{
    State& view = the_state();
    switch (key) {
    case SDL_SCANCODE_C:
        set_mode(static_cast<CameraMode>((static_cast<int>(view.mode) + 1) % (static_cast<int>(CameraMode::kPath) + 1)));
        return true;
    case SDL_SCANCODE_K:
        add_key_here();
        return true;
    case SDL_SCANCODE_DELETE:
        delete_selected_key();
        return true;
    case SDL_SCANCODE_I:
        set_loop_in();
        return true;
    case SDL_SCANCODE_O:
        set_loop_out();
        return true;
    case SDL_SCANCODE_L:
        view.loop = !view.loop;
        return true;
    case SDL_SCANCODE_U:
        view.hud = !view.hud;
        return true;
    case SDL_SCANCODE_V:
        compare_split::set_on(!compare_split::is_on());
        return true;
    case SDL_SCANCODE_ESCAPE:
        leave();
        return true;
    default:
        return false;
    }
}

// A key pressed: the transport's (Space, the arrows, Home / End, [ / ]), H, the tools'; during a video only Esc (it
// stops it).
void handle_key(const input::Event& event)
{
    State& view = the_state();
    if (view.exporting) {
        if (event.key == SDL_SCANCODE_ESCAPE) {
            if (view.host.cancel_export != nullptr) {
                view.host.cancel_export();
            }
            video_done();
            get_log("viewer: the video stopped (Esc)");
        }
        return;
    }
    if (view.ui_visible && panels_want_keyboard()) {
        return;
    }
    const bool shift = (event.modifiers & input::kModifierShift) != 0U;
    const bool alt = (event.modifiers & input::kModifierAlt) != 0U;
    if ((event.key == SDL_SCANCODE_LEFT || event.key == SDL_SCANCODE_RIGHT) && !alt) {
        const int frames = shift ? kSecondFrames : 1;
        step_frames(event.key == SDL_SCANCODE_LEFT ? -frames : frames);
        return;
    }
    if (event.repeat) {
        return;
    }
    switch (event.key) {
    case SDL_SCANCODE_H:
        view.ui_visible = !view.ui_visible;
        break;
    case SDL_SCANCODE_SPACE:
        toggle_play();
        break;
    case SDL_SCANCODE_HOME:
        seek(view.playback.up_frame, !view.paused);
        break;
    case SDL_SCANCODE_END:
        seek(last_frame(), false);
        break;
    case SDL_SCANCODE_LEFTBRACKET:
    case SDL_SCANCODE_RIGHTBRACKET: {
        const auto faster = std::ranges::find_if(kSpeeds, [&view](float speed) { return speed >= view.speed; });
        auto index = static_cast<std::ptrdiff_t>(faster - kSpeeds.begin());
        index += event.key == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1;
        set_speed(core::at(kSpeeds, std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(kSpeeds.size()) - 1)));
        break;
    }
    default:
        tool_key(event.key);
        break;
    }
}

void handle_event(const input::Event& event)
{
    State& view = the_state();
    if (!view.active) {
        return;
    }
    const bool mouse_event = event.type == input::EventType::kMouseMotion ||
                             event.type == input::EventType::kMouseButtonDown ||
                             event.type == input::EventType::kMouseButtonUp || event.type == input::EventType::kMouseWheel;
    if (view.ui_visible && !view.exporting && event.native != nullptr) {
        panel_event(event.native, mouse_event, !mouse_event);
    }
    const bool panel_mouse = view.ui_visible && !view.looking && panels_want_mouse();
    switch (event.type) {
    case input::EventType::kMouseButtonDown:
        if (event.button == input::kMouseRight && !panel_mouse && !view.exporting) {
            view.looking = true;
            input::set_relative_mouse(true);
        }
        return;
    case input::EventType::kMouseButtonUp:
        if (event.button == input::kMouseRight && view.looking) {
            view.looking = false;
            input::set_relative_mouse(false);
        }
        return;
    case input::EventType::kMouseMotion:
        if (view.looking) {
            view.look_x += static_cast<float>(event.dx);
            view.look_y += static_cast<float>(event.dy);
        }
        return;
    case input::EventType::kMouseWheel:
        if (!panel_mouse) {
            view.wheel += static_cast<float>(event.wheel_flipped ? -event.wheel : event.wheel);
        }
        return;
    case input::EventType::kPadAxis:
        if (event.pad_axis < kPadAxes) {
            core::at(view.pad, event.pad_axis) = static_cast<float>(event.pad_value) / static_cast<float>(kPadMost);
        }
        return;
    case input::EventType::kPadButtonDown:
        if (event.pad_button == SDL_GAMEPAD_BUTTON_SOUTH || event.pad_button == SDL_GAMEPAD_BUTTON_START) {
            toggle_play();
        } else if (event.pad_button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
            step_frames(-1);
        } else if (event.pad_button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
            step_frames(1);
        }
        return;
    case input::EventType::kKeyDown:
        handle_key(event);
        return;
    default:
        return;
    }
}

} // namespace

State& state() { return the_state(); }

uint32_t frame_shown()
{
    const uint32_t done = frames_done();
    return done > 0 ? done - 1 : 0;
}

uint32_t last_frame()
{
    const State& view = the_state();
    return view.playback.frames > 0 ? view.playback.frames - 1 : 0;
}

void prepare_input(const Host& host)
{
    the_state().host = host;
    input::add_event_handler(handle_event);
}

void begin(const Playback& playback)
{
    State& view = the_state();
    if (view.active && view.restarting) {
        // (a seek back or a video's run-up: the level loaded again - the seek goes on)
        view.restarting = false;
        view.playback.frames = playback.frames;
        view.playback.up_frame = playback.up_frame;
        return;
    }
    const Host host = view.host;
    const bool ui_visible = view.ui_visible;
    view = State{};
    view.host = host;
    view.ui_visible = ui_visible;
    view.playback = playback;
    view.active = true;
    view.last_tick = Clock::now();
    view.progress_drawn = Clock::now();
    view.cursor_before = input::show_cursor(-1);
    input::show_cursor(1);
    load_path();
    get_log("viewer: %s - %u frames (%.1f s), the level up at frame %u; H hides the panels, Esc leaves",
            playback.name.c_str(), playback.frames, static_cast<double>(playback.frames) / kFramesPerSecond,
            playback.up_frame);
    if (!playback.export_video.empty()) {
        get_log("viewer: %s", start_export(playback.export_video).c_str());
    }
}

void end()
{
    State& view = the_state();
    if (!view.active) {
        return;
    }
    if (view.path_dirty) {
        save_path();
    }
    frame_presented();
    if (view.looking) {
        input::set_relative_mouse(false);
    }
    if (view.cursor_before == 0) {
        input::show_cursor(0);
    }
    view.active = false;
    view.exporting = false;
    get_log("viewer: closed - the controls are the player's");
}

bool active() { return the_state().active; }

void video_done()
{
    State& view = the_state();
    if (!view.exporting) {
        return;
    }
    view.exporting = false;
    view.mode = view.mode_before_export;
    view.paused = true;
    get_log("viewer: the video is done");
}

bool loop_pace(ext::LoopPace* pace)
{
    State& view = the_state();
    if (!view.active) {
        return false;
    }
    move_cameras();
    save_if_due();
    const uint32_t done = frames_done();
    if (view.restarting) {
        run_flat_out(pace);
        return true;
    }
    if (view.exporting) {
        // (the video's frames drawn every one - the demos' keeps_every_frame -, the run-up to them flat out)
        if (done < view.export_from) {
            run_flat_out(pace);
        }
        return true;
    }
    if (view.target >= 0) {
        if (static_cast<int64_t>(done) < view.target + 1) {
            run_flat_out(pace);
            return true;
        }
        finish_seek(done);
    }
    // (the level loading: it runs until it's up)
    if (done <= view.playback.up_frame) {
        return true;
    }
    if (view.step) {
        view.step = false;
        return true;
    }
    if (done >= view.playback.frames) {
        view.paused = true;
    }
    if (view.paused) {
        pace->hold = true;
        return true;
    }
    const camera_path::Path& path = view.path;
    if (view.loop && path.loop_in >= 0 && path.loop_out > path.loop_in && std::cmp_greater(done, path.loop_out)) {
        seek(path.loop_in, true);
        run_flat_out(pace);
        return true;
    }
    if (view.speed < 1.0F) {
        view.slow += static_cast<double>(view.speed);
        if (view.slow < 1.0) {
            pace->hold = true;
        } else {
            view.slow -= 1.0;
        }
    } else if (view.speed > 1.0F) {
        // (n logic frames a drawn one: the n - 1 before it flat out, undrawn)
        const auto every = static_cast<uint32_t>(std::lround(view.speed));
        view.fast_count = (view.fast_count + 1) % std::max(1U, every);
        if (view.fast_count != 0) {
            pace->flat_out = true;
            pace->skip_draw = true;
        }
    }
    return true;
}

void place_camera(gfx::ViewCam* camera, int viewport)
{
    State& view = the_state();
    if (!view.active || viewport != 0 || camera == nullptr) {
        return;
    }
    if (view.camera == nullptr) {
        // (the frame's first view: the game's camera noted - the check track's -, the skater for the orbit)
        const maths::Vec4& at = camera->at_point();
        const maths::Vec4& back = core::at(camera->basis_frame().row, 2);
        view.game_position = {.x = at.x, .y = at.y, .z = at.z};
        view.game_forward = {.x = -back.x, .y = -back.y, .z = -back.z};
        view.game_noted = true;
        view.skater_seen = false;
        if (auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater))) {
            const maths::Vec4& position = skater->at_point();
            const maths::Vec4& ahead = core::at(skater->basis_frame().row, 2);
            view.skater = {.x = position.x, .y = position.y, .z = position.z};
            view.skater_heading = camera_path::angles_toward({.x = ahead.x, .y = 0.0F, .z = ahead.z}).yaw;
            view.skater_seen = true;
        }
    }
    const double fraction = view.paused || view.speed >= 1.0F ? 0.0 : std::min(view.slow, 1.0);
    std::optional<Pose> pose = viewer_pose(fraction);
    if (!pose) {
        view.shown = game_pose();
        return;
    }
    if (view.mode == CameraMode::kFixed && view.path.fixed.size() > static_cast<std::size_t>(view.fixed) && view.skater_seen &&
        core::at(view.path.fixed, static_cast<std::size_t>(view.fixed)).name.starts_with("track")) {
        camera_path::look_at(&*pose, orbit_target()); // (a fixed camera named "track...": it follows the skater)
    }
    if (view.camera == nullptr) {
        view.camera = camera;
        view.saved_position = camera->at_point();
        view.saved_matrix = camera->basis_frame();
    }
    const camera_path::Rows rows = camera_path::rows_of(*pose);
    camera->set_at_point({.x = pose->position.x, .y = pose->position.y, .z = pose->position.z, .w = 1.0F});
    const auto row = [](const camera_path::Vec3& axis) { return maths::Vec4{.x = axis.x, .y = axis.y, .z = axis.z, .w = 0.0F}; };
    core::at(camera->basis_frame().row, 0) = row(rows.right);
    core::at(camera->basis_frame().row, 1) = row(rows.up);
    core::at(camera->basis_frame().row, 2) = row(rows.back);
    view.shown = *pose;
}

float view_angle(float angle)
{
    State& view = the_state();
    if (!view.active) {
        return angle;
    }
    if (view.camera == nullptr) {
        view.game_angle = angle;
    }
    float wanted = view.mode == CameraMode::kGame ? view.fov : view.shown.fov;
    if (wanted <= 1.0F) {
        wanted = view.fov;
    }
    const float used = wanted > 1.0F ? wanted : angle;
    view.shown.fov = used;
    return used;
}

void frame_presented()
{
    State& view = the_state();
    if (view.camera != nullptr) {
        view.camera->set_at_point(view.saved_position);
        view.camera->set_basis_frame(view.saved_matrix);
        view.camera = nullptr;
    }
    view.game_noted = false;
}

GameView game_view()
{
    const State& view = the_state();
    if (!view.active || view.camera == nullptr || !view.game_noted) {
        return {};
    }
    return {.position = {view.game_position.x, view.game_position.y, view.game_position.z},
            .forward = {view.game_forward.x, view.game_forward.y, view.game_forward.z},
            .moved = true};
}

bool draws_hud() { return the_state().hud; }

void draw()
{
    if (the_state().active) {
        draw_panels();
    }
}

void device_lost() { panels_device_lost(); }

void toggle_play()
{
    State& view = the_state();
    if (view.target >= 0 || view.restarting) {
        view.resume = !view.resume;
        return;
    }
    if (view.paused && frames_done() >= view.playback.frames) {
        seek(view.playback.up_frame, true); // (at the end: from the start again)
        return;
    }
    view.paused = !view.paused;
    view.slow = 0.0;
}

void seek(int64_t frame, bool keep_playing)
{
    State& view = the_state();
    frame = std::clamp<int64_t>(frame, view.playback.up_frame, last_frame());
    view.resume = keep_playing;
    if (view.restarting) {
        view.target = frame; // (the level is loading again: the seek runs on to here)
        return;
    }
    const uint32_t done = frames_done();
    if (frame + 1 == static_cast<int64_t>(done)) {
        view.target = -1;
        view.paused = !keep_playing;
        return;
    }
    view.target = frame;
    view.seek_started = Clock::now();
    view.progress_drawn = Clock::now();
    view.seek_started_frame = done;
    if (frame + 1 < static_cast<int64_t>(done)) {
        // (back: the level loaded again, then run to the frame)
        view.restarting = true;
        view.seek_started_frame = 0;
        if (view.host.restart == nullptr || !view.host.restart()) {
            view.restarting = false;
            view.target = -1;
            get_log("viewer: the seek back to frame %lld failed - the level didn't load again", static_cast<long long>(frame));
        }
    }
}

void step_frames(int frames)
{
    State& view = the_state();
    if (frames == 1 && view.paused && view.target < 0 && !view.restarting && frames_done() < view.playback.frames) {
        view.step = true;
        return;
    }
    seek(static_cast<int64_t>(frame_shown()) + frames, false);
}

void set_speed(float speed)
{
    State& view = the_state();
    view.speed = std::clamp(speed, kSpeeds.front(), kSpeeds.back());
    view.slow = 0.0;
    view.fast_count = 0;
}

void set_loop_in()
{
    State& view = the_state();
    view.path.loop_in = static_cast<int32_t>(frame_shown());
    if (view.path.loop_out <= view.path.loop_in) {
        view.path.loop_out = static_cast<int32_t>(last_frame());
    }
    view.loop = true;
    path_changed();
}

void set_loop_out()
{
    State& view = the_state();
    view.path.loop_out = static_cast<int32_t>(frame_shown());
    if (view.path.loop_in < 0 || view.path.loop_in >= view.path.loop_out) {
        view.path.loop_in = static_cast<int32_t>(view.playback.up_frame);
    }
    view.loop = true;
    path_changed();
}

int add_key_here()
{
    State& view = the_state();
    camera_path::Key key;
    key.frame = static_cast<int32_t>(frame_shown());
    key.pose = view.shown;
    if (key.pose.fov <= 1.0F) {
        key.pose.fov = view.game_angle;
    }
    const int existing = camera_path::key_at(view.path, key.frame);
    if (existing >= 0) {
        key.ease = core::at(view.path.keys, static_cast<std::size_t>(existing)).ease;
    }
    view.selected_key = static_cast<int>(camera_path::add_key(&view.path, key));
    path_changed();
    get_log("viewer: key %d at frame %d - (%.1f, %.1f, %.1f), yaw %.1f, pitch %.1f, fov %.1f", view.selected_key, key.frame,
            static_cast<double>(key.pose.position.x), static_cast<double>(key.pose.position.y),
            static_cast<double>(key.pose.position.z), static_cast<double>(key.pose.yaw),
            static_cast<double>(key.pose.pitch), static_cast<double>(key.pose.fov));
    return view.selected_key;
}

void delete_selected_key()
{
    State& view = the_state();
    if (view.selected_key < 0 || static_cast<std::size_t>(view.selected_key) >= view.path.keys.size()) {
        return;
    }
    camera_path::delete_key(&view.path, static_cast<std::size_t>(view.selected_key));
    view.selected_key = std::min(view.selected_key, static_cast<int>(view.path.keys.size()) - 1);
    path_changed();
}

void path_changed()
{
    State& view = the_state();
    view.path_dirty = true;
    view.path_changed_at = Clock::now();
}

void save_path()
{
    State& view = the_state();
    view.path_dirty = false;
    const fs::Written written = fs::write_text(sidecar_path(), camera_path::write_text(view.path));
    if (!written.written || !written.closed) {
        get_log("viewer: couldn't write %s", sidecar_path().c_str());
    }
}

std::string camera_name(CameraMode mode)
{
    switch (mode) {
    case CameraMode::kFree:
        return "free";
    case CameraMode::kOrbit:
        return "orbit";
    case CameraMode::kFixed:
        return "fixed";
    case CameraMode::kPath:
        return "path";
    case CameraMode::kGame:
    default:
        return "game";
    }
}

void set_mode(CameraMode mode)
{
    State& view = the_state();
    if (mode == CameraMode::kFree && view.mode != CameraMode::kFree) {
        view.free = view.shown; // (the free camera starts where the camera is)
        view.free.roll = 0.0F;
    }
    view.mode = mode;
}

void add_fixed_here()
{
    State& view = the_state();
    camera_path::FixedCamera camera;
    camera.pose = view.shown;
    camera.name = "Camera " + std::to_string(view.path.fixed.size() + 1);
    view.path.fixed.push_back(camera);
    view.fixed = static_cast<int>(view.path.fixed.size()) - 1;
    path_changed();
}

std::string start_export(const std::string& path)
{
    State& view = the_state();
    if (view.exporting || view.host.export_video == nullptr) {
        return "a video is being saved";
    }
    const camera_path::Path& camera = view.path;
    const bool region = camera.loop_in >= 0 && camera.loop_out > camera.loop_in;
    const uint32_t from = region ? static_cast<uint32_t>(camera.loop_in) : view.playback.up_frame;
    const uint32_t to = region ? static_cast<uint32_t>(camera.loop_out) + 1 : view.playback.frames;
    const std::string file = path.empty() && view.host.video_path != nullptr ? view.host.video_path(view.playback.name + "_viewer")
                                                                          : path;
    if (!view.host.export_video(file, from, to)) {
        return "the video couldn't start";
    }
    view.exporting = true;
    view.export_from = from;
    view.export_to = to;
    view.export_path = file;
    view.mode_before_export = view.mode;
    if (!camera.keys.empty()) {
        view.mode = CameraMode::kPath;
    }
    view.target = -1;
    view.paused = false;
    view.step = false;
    view.speed = 1.0F;
    if (frames_done() > from) {
        view.restarting = true;
        if (view.host.restart == nullptr || !view.host.restart()) {
            view.restarting = false;
        }
    }
    return text::format("saving frames %u - %u with the %s camera as %s", from, to, camera_name(view.mode).c_str(),
                        file.c_str());
}

void leave()
{
    const State& view = the_state();
    if (view.host.stop != nullptr) {
        view.host.stop();
    }
}

namespace {

constexpr const char* kConsoleHelp =
    "viewer play | pause | seek <frame | seconds s> | step [n] | speed <x> | camera game | free | orbit | fixed [n] | "
    "path | free <x> <y> <z> <yaw> <pitch> | fov <deg> | hud 0|1 | ui 0|1 | tab camera|keys|compare | key add | "
    "key delete <n> | key clear | loop <in> <out> | loop off | export [file] | save | exit | status";

// The console's transport words (true: one of them).
bool transport_command(const std::string& action, std::istringstream& words)
{
    const State& view = the_state();
    if (action == "play" || action == "pause") {
        if (view.paused == (action == "play")) {
            toggle_play();
        }
    } else if (action == "seek") {
        std::string where;
        words >> where;
        const int64_t frame = where.ends_with('s')
                                  ? static_cast<int64_t>(std::lround(text::to_double(where.c_str()) * kFramesPerSecond))
                                  : text::to_int(where.c_str());
        seek(frame, !view.paused);
    } else if (action == "step") {
        int frames = 1;
        words >> frames;
        step_frames(frames);
    } else if (action == "speed") {
        float speed = 1.0F;
        words >> speed;
        set_speed(speed);
    } else {
        return false;
    }
    return true;
}

// The console's camera and view words (true: one of them).
bool camera_command(const std::string& action, std::istringstream& words)
{
    State& view = the_state();
    if (action == "camera") {
        std::string mode;
        words >> mode;
        for (int i = 0; i <= static_cast<int>(CameraMode::kPath); ++i) {
            if (camera_name(static_cast<CameraMode>(i)) == mode) {
                set_mode(static_cast<CameraMode>(i));
            }
        }
        if (view.mode == CameraMode::kFixed) {
            words >> view.fixed;
        }
    } else if (action == "free") {
        Pose pose;
        words >> pose.position.x >> pose.position.y >> pose.position.z >> pose.yaw >> pose.pitch;
        view.free = pose;
        view.mode = CameraMode::kFree;
    } else if (action == "fov") {
        words >> view.fov;
    } else if (action == "hud" || action == "ui") {
        int on = 1;
        words >> on;
        (action == "hud" ? view.hud : view.ui_visible) = on != 0;
    } else if (action == "tab") {
        std::string tab;
        words >> tab;
        view.show_tab = 0;
        if (tab == "keys") {
            view.show_tab = 1;
        } else if (tab == "compare") {
            view.show_tab = 2;
        }
    } else {
        return false;
    }
    return true;
}

// The console's path words: keys and the loop region (true: one of them).
bool path_command(const std::string& action, std::istringstream& words)
{
    State& view = the_state();
    if (action == "key") {
        std::string what;
        words >> what;
        if (what == "add") {
            add_key_here();
        } else if (what == "delete") {
            words >> view.selected_key;
            delete_selected_key();
        } else if (what == "clear") {
            view.path.keys.clear();
            view.selected_key = -1;
            path_changed();
        }
    } else if (action == "loop") {
        std::string from;
        int32_t to = -1;
        words >> from >> to;
        if (from == "off") {
            view.loop = false;
        } else {
            view.path.loop_in = static_cast<int32_t>(text::to_int(from.c_str()));
            view.path.loop_out = to;
            view.loop = view.path.loop_out > view.path.loop_in;
            path_changed();
        }
    } else {
        return false;
    }
    return true;
}

std::string status_line()
{
    const State& view = the_state();
    const uint32_t done = frames_done();
    std::string state_text = view.paused ? "paused" : text::format("playing %.2fx", static_cast<double>(view.speed));
    if (view.restarting) {
        state_text = "loading again";
    } else if (view.target >= 0) {
        state_text = text::format("seeking to %lld", static_cast<long long>(view.target));
    } else if (view.exporting) {
        state_text = "saving a video";
    }
    return text::format("frame %u of %u (done %u), %s, camera %s, %zu key(s), loop %s %d-%d, last seek %.2f s at %.0f "
                        "frames/s",
                        done > 0 ? done - 1 : 0, view.playback.frames, done, state_text.c_str(),
                        camera_name(view.mode).c_str(), view.path.keys.size(), view.loop ? "on" : "off",
                        view.path.loop_in, view.path.loop_out, view.last_seek_seconds, view.last_seek_rate);
}

} // namespace

std::string console_command(const std::string& args)
{
    if (!the_state().active) {
        return "the viewer isn't up (demo view <name>)";
    }
    std::istringstream words(args);
    std::string action;
    words >> action;
    if (transport_command(action, words) || camera_command(action, words) || path_command(action, words)) {
        return status_line();
    }
    if (action == "export") {
        std::string file;
        std::getline(words >> std::ws, file);
        return start_export(file.empty() ? std::string() : game_folder_path(file.c_str()));
    }
    if (action == "save") {
        save_path();
        return "saved " + sidecar_path();
    }
    if (action == "exit") {
        leave();
        return "closed";
    }
    if (!action.empty() && action != "status") {
        return kConsoleHelp;
    }
    return status_line();
}

} // namespace devtools::demo_viewer
