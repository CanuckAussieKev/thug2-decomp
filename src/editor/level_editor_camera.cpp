// NOT ORIGINAL - the level editor's camera tools (ours). See level_editor_camera.h.

#include "editor/level_editor_camera.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <fstream>
#include <sstream>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_prefs.h"
#include "platform/run_log.h"

namespace frontend::level_editor::view_cam {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTopPitch = 89.0f * kPi / 180.0f; // (Look's limit: the frame needs a heading)
} // namespace

// --- the pieces ---

float get_ease(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float nearest_angle(float from, float to)
{
    double d = std::fmod(static_cast<double>(to) - from, 2.0 * kPi);
    if (d > kPi)
        d -= 2.0 * kPi;
    else if (d < -kPi)
        d += 2.0 * kPi;
    return static_cast<float>(from + d);
}

Pose get_blend(const Pose& from, const Pose& to, float t)
{
    const float e = get_ease(t);
    const float yaw = nearest_angle(from.yaw, to.yaw);
    return {from.pos + (to.pos - from.pos) * e, from.yaw + (yaw - from.yaw) * e,
            from.pitch + (to.pitch - from.pitch) * e};
}

float frame_distance(float radius, float tan_half, float minimum)
{
    if (tan_half <= 0.0f)
        return std::max(minimum, radius * 3.0f);
    // (the sphere seen whole: its radius over the sine of the half-angle, a little more)
    const double half = std::atan(static_cast<double>(tan_half));
    return std::max(minimum, static_cast<float>(radius / std::sin(half) * 1.15));
}

float speed_for_radius(float radius) { return std::clamp(radius * 4.0f, 100.0f, 20000.0f); }

V3 orbit_about(V3 pos, V3 pivot, const TickCycle& before, const TickCycle& after)
{
    const V3 v = pos - pivot;
    const float r = get_dot(v, before.right);
    const float u = get_dot(v, before.up);
    const float b = get_dot(v, before.back);
    return pivot + after.right * r + after.up * u + after.back * b;
}

float fly_ramp(float held_seconds, int accel_ms)
{
    if (accel_ms <= 0)
        return 1.0f;
    const float t = held_seconds * 1000.0f / static_cast<float>(accel_ms);
    // (never quite still: the first frame moves a little)
    return std::max(0.05f, get_ease(t));
}

float eye_distance(float equivalent) { return std::clamp(equivalent * 15.0f, 15000.0f, 60000.0f); }

float ortho_angle(float base_degrees, float equivalent)
{
    const double half = static_cast<double>(base_degrees) * kPi / 360.0;
    const double narrowed = std::atan(std::tan(half) * equivalent / eye_distance(equivalent));
    return static_cast<float>(narrowed * 360.0 / kPi);
}

float ortho_near(float equivalent) { return eye_distance(equivalent) * 0.4f; }

void axis_yaw_pitch(View view, float* yaw, float* pitch)
{
    *yaw = 0.0f;
    *pitch = 0.0f;
    switch (view) {
    case View::kTop:
        *pitch = -kTopPitch;
        break; // (looking down; up the screen +z)
    case View::kBottom:
        *pitch = kTopPitch;
        break;
    case View::kMenuSystem:
        *yaw = 0.0f;
        break; // (looking along +z)
    case View::kBack:
        *yaw = kPi;
        break;
    case View::kRight:
        *yaw = -kPi * 0.5f;
        break; // (from +x, looking along -x)
    case View::kLeft:
        *yaw = kPi * 0.5f;
        break;
    default:
        break;
    }
}

const char* view_name(View view)
{
    switch (view) {
    case View::kTop:
        return "Top";
    case View::kBottom:
        return "Bottom";
    case View::kMenuSystem:
        return "Front";
    case View::kBack:
        return "Back";
    case View::kRight:
        return "Right";
    case View::kLeft:
        return "Left";
    case View::kDealloc:
        return "User";
    default:
        return "Perspective";
    }
}

std::string format_bookmarks(const Bookmark marks[kBookmarks])
{
    std::string text = "thug2-decomp camera bookmarks 1\n";
    for (int i = 0; i < kBookmarks; ++i) {
        const Bookmark& m = marks[i];
        if (!m.set)
            continue;
        char line[256];
        std::snprintf(line, sizeof(line), "%d %.9g %.9g %.9g %.9g %.9g %d %.9g\n", i + 1, m.pose.pos.x, m.pose.pos.y,
                      m.pose.pos.z, m.pose.yaw, m.pose.pitch, static_cast<int>(m.view), m.equivalent);
        text += line;
    }
    return text;
}

bool parse_bookmarks(const std::string& text, Bookmark marks[kBookmarks])
{
    for (int i = 0; i < kBookmarks; ++i)
        marks[i] = Bookmark{};
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || !line.starts_with("thug2-decomp camera bookmarks "))
        return false;
    while (std::getline(in, line)) {
        int n = 0, view = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f, pitch = 0.0f, equivalent = 0.0f;
        if (sscanf_s(line.c_str(), "%d %f %f %f %f %f %d %f", &n, &x, &y, &z, &yaw, &pitch, &view, &equivalent) != 8)
            continue;
        if (n < 1 || n > kBookmarks || view < 0 || view > static_cast<int>(View::kDealloc))
            continue;
        Bookmark& m = marks[n - 1];
        m.set = true;
        m.pose = {{x, y, z}, yaw, pitch};
        m.view = static_cast<View>(view);
        m.equivalent = equivalent;
    }
    return true;
}

std::string bookmarks_file_name(uint32_t scene)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.cameras.txt", scene);
    return name;
}

// --- the editor's state ---

namespace {

// A flight: from, to, how far along (seconds), and what it turns into at its end (an ortho view).
struct Flight {
    bool on = false;
    Pose from{}, to{};
    float t = 0.0f, duration = 0.33f;
    View then_view_ = View::kNone;
    float then_equivalent_ = 0.0f;
};
Flight g_flight;

// The ortho view: which, its heading (kept: the selection's focus would turn it), the point it looks at, and its zoom.
struct OrthoState {
    View view = View::kNone;
    float yaw = 0.0f, pitch = 0.0f;
    V3 pivot = {};
    float equivalent = 1500.0f;
    V3 last_eye_ = {};
};
OrthoState g_ortho;

// Alt+left drag.
struct OrbitState {
    bool pending = false, orbiting = false;
    float travel = 0.0f;
    float dx = 0.0f, dy = 0.0f;
    int wheel = 0;
    bool have_pivot_ = false;
    V3 pivot = {};
};
OrbitState g_orbit;

uint32_t g_scene;
Bookmark g_marks[kBookmarks];
float g_fly_held;

std::string bookmarks_path() { return game_folder_path("thug2-decomp-layouts") + "\\" + bookmarks_file_name(g_scene); }

void load_bookmarks()
{
    for (Bookmark& m : g_marks)
        m = Bookmark{};
    const std::ifstream file(bookmarks_path(), std::ios::binary);
    if (!file)
        return;
    std::stringstream text;
    text << file.rdbuf();
    if (!parse_bookmarks(text.str(), g_marks))
        get_log("level editor: %s isn't a bookmarks file - not read", bookmarks_path().c_str());
}

bool save_bookmarks()
{
    CreateDirectoryA(game_folder_path("thug2-decomp-layouts").c_str(), nullptr);
    std::ofstream file(bookmarks_path(), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << format_bookmarks(g_marks);
    return static_cast<bool>(file);
}

V3 get_forward(float yaw, float pitch) { return frame_from_yaw_pitch(yaw, pitch).back * -1.0f; }

void fly_to(const Rig& rig, const Pose& to, View then_view = View::kNone, float then_equivalent = 0.0f)
{
    g_flight.on = true;
    g_flight.from = {*rig.pos, *rig.yaw, *rig.pitch};
    g_flight.to = to;
    g_flight.t = 0.0f;
    g_flight.then_view_ = then_view;
    g_flight.then_equivalent_ = then_equivalent;
}

void enter_ortho(const Rig& rig, View view, float yaw, float pitch, V3 pivot, float equivalent)
{
    g_ortho.view = view;
    g_ortho.yaw = yaw;
    g_ortho.pitch = pitch;
    g_ortho.pivot = rig.have_selection_ ? rig.target : pivot;
    g_ortho.equivalent = std::clamp(equivalent, 50.0f, 20000.0f);
    *rig.yaw = yaw;
    *rig.pitch = pitch;
    const V3 forward = get_forward(yaw, pitch);
    *rig.pos = g_ortho.pivot - forward * eye_distance(g_ortho.equivalent);
    if (rig.have_selection_)
        *rig.orbit_distance_ = eye_distance(g_ortho.equivalent);
    g_ortho.last_eye_ = *rig.pos;
    hud::get_message(std::string(view_name(view)) + " (orthographic)", 2.5);
}

void exit_ortho(const Rig& rig)
{
    if (g_ortho.view == View::kNone)
        return;
    const V3 forward = get_forward(*rig.yaw, *rig.pitch);
    const V3 pivot = rig.have_selection_ ? rig.target : g_ortho.pivot;
    *rig.pos = pivot - forward * g_ortho.equivalent;
    if (rig.have_selection_)
        *rig.orbit_distance_ = std::clamp(g_ortho.equivalent, 20.0f, 20000.0f);
    g_ortho.view = View::kNone;
    hud::get_message("Perspective", 1.5);
}

// The point the view looks at: the selection, else the surface ahead (within 20000), else 1000 ahead.
V3 look_point(const Rig& rig)
{
    if (rig.have_selection_)
        return rig.target;
    const V3 forward = get_forward(*rig.yaw, *rig.pitch);
    V3 hit{};
    if (rig.cast && rig.cast(*rig.pos, *rig.pos + forward * 20000.0f, &hit))
        return hit;
    return *rig.pos + forward * 1000.0f;
}

} // namespace

void kickoff(uint32_t scene)
{
    g_flight = Flight{};
    g_ortho = OrthoState{};
    g_orbit = OrbitState{};
    g_fly_held = 0.0f;
    g_scene = scene;
    load_bookmarks();
}

void halt()
{
    g_flight = Flight{};
    g_ortho.view = View::kNone;
    g_orbit = OrbitState{};
}

void get_cancel()
{
    g_flight = Flight{};
    g_ortho.view = View::kNone;
    g_orbit = OrbitState{};
}

bool ortho() { return g_ortho.view != View::kNone; }
View current_view() { return g_ortho.view; }

float apparent(float distance)
{
    if (!ortho())
        return distance;
    return distance * g_ortho.equivalent / eye_distance(g_ortho.equivalent);
}

float adjust_view_angle(float base_degrees)
{
    return ortho() ? ortho_angle(base_degrees, g_ortho.equivalent) : base_degrees;
}

float get_near_plane() { return ortho() ? ortho_near(g_ortho.equivalent) : 2.0f; }

std::string get_standing()
{
    std::string s;
    if (ortho()) {
        char text[64];
        std::snprintf(text, sizeof(text), "%s ortho (%.0f)", view_name(g_ortho.view), g_ortho.equivalent);
        s = text;
    }
    if (g_orbit.orbiting)
        s += s.empty() ? "orbiting" : "  orbiting";
    return s;
}

bool get_orbiting() { return g_orbit.orbiting; }

void filter_mouse(const Rig& rig, MouseIn m, bool alt, bool have_last_ray, V3 last_ray)
{
    // The wheel with the right button held: the flying speed (Unreal's).
    if (m.right && *m.wheel && !g_orbit.orbiting) {
        *rig.speed = std::clamp(*rig.speed * std::pow(1.25f, static_cast<float>(*m.wheel)), 50.0f, 20000.0f);
        *m.wheel = 0;
        char text[64];
        std::snprintf(text, sizeof(text), "Fly speed %.0f", *rig.speed);
        hud::get_message(text, 1.2);
    }
    // Looking about ends an ortho view (Blender's auto perspective).
    if (m.right && (*m.rel_x_ != 0.0f || *m.rel_y_ != 0.0f) && ortho())
        exit_ortho(rig);
    // Alt+left: held back until it moves (an orbit) or is let go (an Alt+click).
    if (!g_orbit.pending && *m.left_presses_ && alt && !rig.dragging && !m.right) {
        g_orbit = OrbitState{};
        g_orbit.pending = true;
        *m.left_presses_ = 0;
        // (the pivot: the selection, else the surface under the cursor where the press was)
        if (!rig.have_selection_ && have_last_ray && rig.cast) {
            const V3 dir = make_unit(last_ray);
            g_orbit.have_pivot_ = rig.cast(*rig.pos, *rig.pos + dir * 30000.0f, &g_orbit.pivot);
        }
        if (!rig.have_selection_ && !g_orbit.have_pivot_) {
            g_orbit.pivot = look_point(rig);
            g_orbit.have_pivot_ = true;
        }
        return;
    }
    if (!g_orbit.pending)
        return;
    if (!g_orbit.orbiting) {
        g_orbit.travel += std::fabs(*m.rel_x_) + std::fabs(*m.rel_y_);
        if (g_orbit.travel >= 4.0f) {
            g_orbit.orbiting = true;
            g_flight.on = false;
            if (ortho())
                exit_ortho(rig);
        }
    }
    if (g_orbit.orbiting) {
        g_orbit.dx += *m.rel_x_;
        g_orbit.dy += *m.rel_y_;
        g_orbit.wheel += *m.wheel;
        *m.rel_x_ = *m.rel_y_ = 0.0f;
        *m.wheel = 0;
    }
    if (!m.left) {
        // let go: unmoved, the press given back as an Alt+click (with its release)
        if (!g_orbit.orbiting) {
            *m.left_presses_ = 1;
            *m.left_releases_ = 1; // (the button is up: a press and its release, as a quick click)
        } else
            *m.left_releases_ = 0;
        g_orbit.pending = false;
        g_orbit.orbiting = false;
    }
}

void apply_orbit(const Rig& rig)
{
    if (!g_orbit.orbiting && g_orbit.dx == 0.0f && g_orbit.dy == 0.0f && g_orbit.wheel == 0)
        return;
    const prefs::Values& prefs = prefs::get_pull();
    const float sensitivity = 0.0035f * static_cast<float>(prefs.look_pct_) / 100.0f;
    const float dy = prefs.invert_y_ ? -g_orbit.dy : g_orbit.dy;
    const TickCycle before = frame_from_yaw_pitch(*rig.yaw, *rig.pitch);
    get_look(rig.yaw, rig.pitch, g_orbit.dx, dy, sensitivity);
    g_orbit.dx = g_orbit.dy = 0.0f;
    const int wheel = g_orbit.wheel;
    g_orbit.wheel = 0;
    if (rig.have_selection_) {
        // (the editor puts the camera round it at its orbit distance: the wheel nearer / further)
        if (wheel)
            *rig.orbit_distance_ =
                std::clamp(*rig.orbit_distance_ * std::pow(0.85f, static_cast<float>(wheel)), 20.0f, 20000.0f);
        return;
    }
    if (!g_orbit.have_pivot_)
        return;
    const TickCycle after = frame_from_yaw_pitch(*rig.yaw, *rig.pitch);
    V3 pos = orbit_about(*rig.pos, g_orbit.pivot, before, after);
    if (wheel) {
        const V3 offset = pos - g_orbit.pivot;
        const float length = std::sqrt(get_dot(offset, offset));
        const float wanted = std::clamp(length * std::pow(0.85f, static_cast<float>(wheel)), 20.0f, 30000.0f);
        if (length > 0.001f)
            pos = g_orbit.pivot + offset * (wanted / length);
    }
    *rig.pos = pos;
}

void get_keys(const Rig& rig, const uint8_t* keys, bool ctrl, bool browser_open)
{
    // (the keys from the keymap - level_editor_actions.h: rebindable)
    (void)keys;
    (void)ctrl;
    // F: the selection framed (the browser has its own F).
    if (actions::squeezed(actions::kFrameSelection) && !browser_open) {
        if (!rig.have_selection_) {
            hud::get_message("Nothing selected", 2.0);
        } else {
            const float tan_half = std::min(rig.lens.tan_x_, rig.lens.tan_y_);
            if (ortho()) {
                // (an ortho view: its zoom to fit, the lens as perspective has it)
                const float base = tan_half * eye_distance(g_ortho.equivalent) / std::max(1.0f, g_ortho.equivalent);
                g_ortho.equivalent = std::clamp(frame_distance(rig.radius, base, 100.0f), 50.0f, 20000.0f);
            } else {
                const float distance = std::min(20000.0f, frame_distance(rig.radius, tan_half, 100.0f));
                const TickCycle frame = frame_from_yaw_pitch(*rig.yaw, *rig.pitch);
                fly_to(rig, {rig.target + frame.back * distance, *rig.yaw, *rig.pitch});
            }
            if (prefs::get_pull().speed_by_selection_)
                *rig.speed = speed_for_radius(rig.radius);
        }
    }
    // 1..9 recall a bookmark, Ctrl+1..9 store it. (Alt+1..4 are the view modes - level_editor_overlays.h.)
    const bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    for (int i = 0; i < kBookmarks; ++i) {
        const bool store = actions::squeezed(actions::kBookmarkStore1 + i);
        if (alt || (!store && !actions::squeezed(actions::kBookmark1 + i)))
            continue;
        char text[96];
        if (store) {
            Bookmark& m = g_marks[i];
            m.set = true;
            m.pose = {*rig.pos, *rig.yaw, *rig.pitch};
            m.view = g_ortho.view;
            m.equivalent = g_ortho.equivalent;
            if (ortho())
                m.pose.pos = rig.have_selection_ ? rig.target : g_ortho.pivot; // (an ortho bookmark keeps its pivot)
            const bool saved = save_bookmarks();
            std::snprintf(text, sizeof(text), "Camera bookmark %d stored%s", i + 1,
                          saved ? "" : " (not saved: the file)");
            hud::get_message(text, 2.0);
        } else if (g_marks[i].set) {
            const Bookmark& m = g_marks[i];
            if (rig.deselect)
                rig.deselect();
            Rig free = rig;
            free.have_selection_ = false;
            exit_ortho(free);
            if (m.view != View::kNone) {
                // (fly to the perspective place it stands for, then the ortho view)
                const V3 forward = get_forward(m.pose.yaw, m.pose.pitch);
                fly_to(free, {m.pose.pos - forward * m.equivalent, m.pose.yaw, m.pose.pitch}, m.view, m.equivalent);
            } else {
                fly_to(free, m.pose);
            }
            std::snprintf(text, sizeof(text), "Camera bookmark %d", i + 1);
            hud::get_message(text, 1.5);
        } else {
            std::snprintf(text, sizeof(text), "No camera bookmark %d yet - Ctrl+%d stores one", i + 1, i + 1);
            hud::get_message(text, 2.0);
        }
    }
    // The numpad: 7 top, 1 front, 3 right (Ctrl: the opposite), 5 perspective / ortho.
    static const int kPad[4][2] = {{actions::kViewTop, actions::kViewBottom},
                                   {actions::kCamFrontView, actions::kViewBack},
                                   {actions::kPanRightAction, actions::kPanLeftAction},
                                   {actions::kViewOrtho, actions::kViewOrtho}};
    for (int i = 0; i < 4; ++i) {
        const bool opposite = actions::squeezed(kPad[i][1]) && i != 3;
        if (!opposite && !actions::squeezed(kPad[i][0]))
            continue;
        g_flight.on = false;
        if (i == 3) {
            if (ortho()) {
                exit_ortho(rig);
            } else {
                const V3 pivot = look_point(rig);
                const V3 d = pivot - *rig.pos;
                enter_ortho(rig, View::kDealloc, *rig.yaw, *rig.pitch, pivot, std::sqrt(get_dot(d, d)));
            }
            continue;
        }
        static const View kViews[3][2] = {
            {View::kTop, View::kBottom}, {View::kMenuSystem, View::kBack}, {View::kRight, View::kLeft}};
        const View view = kViews[i][opposite ? 1 : 0];
        const V3 pivot = ortho() ? (rig.have_selection_ ? rig.target : g_ortho.pivot) : look_point(rig);
        const V3 d = pivot - *rig.pos;
        const float equivalent = ortho() ? g_ortho.equivalent : std::clamp(std::sqrt(get_dot(d, d)), 200.0f, 20000.0f);
        float yaw = 0.0f, pitch = 0.0f;
        axis_yaw_pitch(view, &yaw, &pitch);
        enter_ortho(rig, view, yaw, pitch, pivot, equivalent);
    }
}

void after_update(const Rig& rig, int wheel, bool interrupted)
{
    if (g_flight.on && interrupted)
        g_flight.on = false;
    if (g_flight.on) {
        // (the frame's time from SDL's clock: the editor caps a frame at a tenth of a second)
        static uint64_t last;
        const uint64_t now = SDL_GetPerformanceCounter();
        const double seconds = g_flight.t == 0.0f || !last ? 1.0 / 60.0
                                                           : static_cast<double>(now - last) /
                                                                 static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;
        g_flight.t += static_cast<float>(std::min(seconds, 0.1)) / g_flight.duration;
        Pose to = g_flight.to;
        if (rig.have_selection_) {
            // (the selection may move meanwhile: the place is kept relative to it)
            const TickCycle frame = frame_from_yaw_pitch(to.yaw, to.pitch);
            const V3 d = g_flight.to.pos - rig.target;
            to.pos = rig.target + frame.back * std::sqrt(get_dot(d, d));
        }
        const Pose pose = get_blend(g_flight.from, to, std::min(1.0f, g_flight.t));
        *rig.pos = pose.pos;
        *rig.yaw = pose.yaw;
        *rig.pitch = pose.pitch;
        if (rig.have_selection_) {
            const V3 d = *rig.pos - rig.target;
            *rig.orbit_distance_ = std::clamp(std::sqrt(get_dot(d, d)), 20.0f, 20000.0f);
        }
        if (g_flight.t >= 1.0f) {
            g_flight.on = false;
            if (g_flight.then_view_ != View::kNone) {
                const V3 forward = get_forward(to.yaw, to.pitch);
                enter_ortho(rig, g_flight.then_view_, to.yaw, to.pitch, to.pos + forward * g_flight.then_equivalent_,
                            g_flight.then_equivalent_);
            } else if (rig.have_selection_ && rig.focus) {
                rig.focus();
            }
        }
        return;
    }
    if (!ortho())
        return;
    // The ortho view: its heading kept, the camera far back along it.
    *rig.yaw = g_ortho.yaw;
    *rig.pitch = g_ortho.pitch;
    const V3 forward = get_forward(g_ortho.yaw, g_ortho.pitch);
    if (rig.have_selection_) {
        if (wheel && !rig.dragging)
            g_ortho.equivalent =
                std::clamp(g_ortho.equivalent * std::pow(0.85f, static_cast<float>(wheel)), 50.0f, 20000.0f);
        g_ortho.pivot = rig.target;
    } else {
        // (the editor flew the camera: along the view a zoom, across it a pan)
        const V3 moved = *rig.pos - g_ortho.last_eye_;
        const float along = get_dot(moved, forward);
        g_ortho.equivalent = std::clamp(g_ortho.equivalent - along, 50.0f, 20000.0f);
        g_ortho.pivot = g_ortho.pivot + (moved - forward * along);
    }
    const float eye = eye_distance(g_ortho.equivalent);
    *rig.pos = g_ortho.pivot - forward * eye;
    if (rig.have_selection_)
        *rig.orbit_distance_ = eye;
    g_ortho.last_eye_ = *rig.pos;
}

float fly_factor(bool held, double seconds)
{
    g_fly_held = held ? g_fly_held + static_cast<float>(seconds) : 0.0f;
    return fly_ramp(g_fly_held, prefs::get_pull().fly_accel_ms_);
}

} // namespace frontend::level_editor::view_cam
