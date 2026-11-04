// NOT ORIGINAL - the level editor's camera tools (ours). See level_editor_camera.h.

#include "editor/level_editor_camera.h"
#include "engine/pad_input/input_source.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <span>
#include <windows.h>
#include <fstream>
#include <sstream>
#include <array>
#include <string>

#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_prefs.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/field_reader.h"
#include "base/core/no_destroy.h"
#include "base/maths/trig.h"

namespace frontend::level_editor::view_cam {

namespace {

constexpr float kSmoothstepCubic = 3.0F; // smoothstep: t * t * (3 - 2 t)
constexpr float kSmoothstepQuadratic = 2.0F;
constexpr double kFullTurnPerPi = 2.0;       // a full turn is two pi radians
constexpr float kRadiusMarginFactor = 3.0F;  // the frame distance of a sphere: at least this many radii
constexpr double kFrameMargin = 1.15;        // a framed sphere fills 15 % less of the view
constexpr float kSpeedPerRadius = 4.0F;      // fly speed for a framed radius: this many radii a second
constexpr float kMinEase = 0.05F;            // a flight's eased progress never drops below this
constexpr float kEyeDistancePerEquivalent = 15.0F; // an orthographic view's eye sits this many equivalents away
constexpr float kMinEyeDistance = 15000.0F;
constexpr float kMaxEyeDistance = 60000.0F;
constexpr double kFullTurnDegrees = 360.0;
constexpr float kOrthoNearFraction = 0.4F; // the orthographic near plane, a share of the eye distance
constexpr std::size_t kLineTextSize = 256;
constexpr std::size_t kNameTextSize = 32;
constexpr float kDefaultFlightSeconds = 0.33F;
constexpr float kDefaultOrthoEquivalent = 1500.0F; // the orthographic views' starting zoom
constexpr float kMinOrthoEquivalent = 50.0F;
constexpr float kMaxOrthoEquivalent = 20000.0F;
constexpr double kOrthoMessageSeconds = 2.5;
constexpr float kMinOrbitDistance = 20.0F;
constexpr float kMaxOrbitDistance = 20000.0F;
constexpr double kBriefMessageSeconds = 1.5;
constexpr float kForwardCastReach = 20000.0F;     // how far ahead the view looks for a surface
constexpr float kFallbackPivotDistance = 1000.0F; // the pivot when nothing was hit
constexpr float kPerspectiveNearPlane = 2.0F;
constexpr std::size_t kSpeedTextSize = 64;
constexpr float kWheelSpeedStep = 1.25F; // fly speed change a wheel notch makes
constexpr double kSpeedMessageSeconds = 1.2;
constexpr float kPivotCastReach = 30000.0F; // how far an orbit looks for its pivot
constexpr float kOrbitDragTravel = 4.0F;    // pixels of drag that turn a click into an orbit
constexpr float kWheelZoomStep = 0.85F;     // zoom change a wheel notch makes
constexpr float kMinDirectionLength = 0.001F;
constexpr double kStatusMessageSeconds = 2.0;
constexpr std::size_t kFrameTextSize = 96;
constexpr double kMaxFlightStepSeconds = 0.1; // a flight advances by at most this each frame

constexpr float kHalf = 0.5F; // half

} // namespace

namespace {
constexpr float kTopPitch = 89.0F * maths::kPi / 180.0F; // (Look's limit: the frame needs a heading)
} // namespace

// --- the pieces ---

float get_ease(float fraction)
{
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    return fraction * fraction * (kSmoothstepCubic - (kSmoothstepQuadratic * fraction));
}

float nearest_angle(float from, float to)
{
    double difference = std::fmod(static_cast<double>(to) - from, kFullTurnPerPi * maths::kPi);
    if (difference > maths::kPi) {
        difference -= kFullTurnPerPi * maths::kPi;
    } else if (difference < -maths::kPi) {
        difference += kFullTurnPerPi * maths::kPi;
    }
    return static_cast<float>(from + difference);
}

Pose get_blend(const Pose& from, const Pose& to, float fraction)
{
    const float eased = get_ease(fraction);
    const float yaw = nearest_angle(from.yaw, to.yaw);
    return {.pos = from.pos + (to.pos - from.pos) * eased,
            .yaw = from.yaw + ((yaw - from.yaw) * eased),
            .pitch = from.pitch + ((to.pitch - from.pitch) * eased)};
}

float frame_distance(FrameFit frame_fit)
{
    const auto radius = frame_fit.radius;
    const auto tan_half = frame_fit.tan_half_;
    const auto minimum = frame_fit.minimum;
    if (tan_half <= 0.0F) {
        return std::max(minimum, radius * kRadiusMarginFactor);
    }
    // (the sphere seen whole: its radius over the sine of the half-angle, a little more)
    const double half = std::atan(static_cast<double>(tan_half));
    return std::max(minimum, static_cast<float>(radius / std::sin(half) * kFrameMargin));
}

float speed_for_radius(float radius) { return std::clamp(radius * kSpeedPerRadius, 100.0F, kMaxFlySpeed); }

V3 orbit_about(V3 pos, V3 pivot, const TickCycle& before, const TickCycle& after)
{
    const V3 v = pos - pivot;
    const float right_part = get_dot(v, before.right);
    const float u = get_dot(v, before.up);
    const float back_part = get_dot(v, before.back);
    return pivot + after.right * right_part + after.up * u + after.back * back_part;
}

float fly_ramp(float held_seconds, int accel_ms)
{
    if (accel_ms <= 0) {
        return 1.0F;
    }
    const float fraction = held_seconds * 1000.0F / static_cast<float>(accel_ms);
    // (never quite still: the first frame moves a little)
    return std::max(kMinEase, get_ease(fraction));
}

float eye_distance(float equivalent) { return std::clamp(equivalent * kEyeDistancePerEquivalent, kMinEyeDistance, kMaxEyeDistance); }

float ortho_angle(OrthoAngle ortho)
{
    const auto base_degrees = ortho.base_degrees_;
    const auto equivalent = ortho.equivalent;
    const double half = static_cast<double>(base_degrees) * maths::kPi / 360.0;
    const double narrowed = std::atan(std::tan(half) * equivalent / eye_distance(equivalent));
    return static_cast<float>(narrowed * kFullTurnDegrees / maths::kPi);
}

float ortho_near(float equivalent) { return eye_distance(equivalent) * kOrthoNearFraction; }

void axis_yaw_pitch(View view, YawPitchOut out_view)
{
    auto* const yaw = out_view.yaw;
    auto* const pitch = out_view.pitch;
    *yaw = 0.0F;
    *pitch = 0.0F;
    switch (view) {
    case View::kTop:
        *pitch = -kTopPitch;
        break; // (looking down; up the screen +z)
    case View::kBottom:
        *pitch = kTopPitch;
        break;
    case View::kMenuSystem:
        *yaw = 0.0F;
        break; // (looking along +z)
    case View::kBack:
        *yaw = maths::kPi;
        break;
    case View::kRight:
        *yaw = -maths::kPi * kHalf;
        break; // (from +x, looking along -x)
    case View::kLeft:
        *yaw = maths::kPi * kHalf;
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

std::string format_bookmarks(std::span<const Bookmark, kBookmarks> marks)
{
    std::string text = "thug2-decomp camera bookmarks 1\n";
    for (int i = 0; i < kBookmarks; ++i) {
        const Bookmark& bookmark = core::at(marks, i);
        if (!bookmark.set) {
            continue;
        }
        std::array<char, kLineTextSize> line{};
        text::format_to(line, "%d %.9g %.9g %.9g %.9g %.9g %d %.9g\n", i + 1, bookmark.pose.pos.x, bookmark.pose.pos.y,
                        bookmark.pose.pos.z, bookmark.pose.yaw, bookmark.pose.pitch, static_cast<int>(bookmark.view),
                        bookmark.equivalent);
        text += line.data();
    }
    return text;
}

bool parse_bookmarks(const std::string& text, std::span<Bookmark, kBookmarks> marks)
{
    for (int i = 0; i < kBookmarks; ++i) {
        core::at(marks, i) = Bookmark{};
    }
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || !line.starts_with("thug2-decomp camera bookmarks ")) {
        return false;
    }
    while (std::getline(in, line)) {
        int count = 0;
        int view = 0;
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        float yaw = 0.0F;
        float pitch = 0.0F;
        float equivalent = 0.0F;
        text::FieldReader fields(line); // "%d %f %f %f %f %f %d %f"
        if (!(fields.read(count) && fields.read(x) && fields.read(y) && fields.read(z) && fields.read(yaw) &&
              fields.read(pitch) && fields.read(view) && fields.read(equivalent))) {
            continue;
        }
        if (count < 1 || count > kBookmarks || view < 0 || view > static_cast<int>(View::kDealloc)) {
            continue;
        }
        Bookmark& bookmark = core::at(marks, count - 1);
        bookmark.set = true;
        bookmark.pose = {.pos = {.x = x, .y = y, .z = z}, .yaw = yaw, .pitch = pitch};
        bookmark.view = static_cast<View>(view);
        bookmark.equivalent = equivalent;
    }
    return true;
}

std::string bookmarks_file_name(uint32_t scene)
{
    std::array<char, kNameTextSize> name{};
    text::format_to(name, "%08X.cameras.txt", scene);
    return name.data();
}

// --- the editor's state ---

namespace {

// A flight: from, to, how far along (seconds), and what it turns into at its end (an ortho view).
struct Flight {
    bool on = false;
    Pose from{}, to{};
    float t = 0.0F, duration = kDefaultFlightSeconds;
    View then_view_ = View::kNone;
    float then_equivalent_ = 0.0F;
};
Flight& stored_flight()
{
    static core::NoDestroy<Flight> flight;
    return flight.get();
}

// The ortho view: which, its heading (kept: the selection's focus would turn it), the point it looks at, and its zoom.
struct OrthoState {
    View view = View::kNone;
    float yaw = 0.0F, pitch = 0.0F;
    V3 pivot = {};
    float equivalent = kDefaultOrthoEquivalent;
    V3 last_eye_ = {};
};
OrthoState& stored_ortho()
{
    static core::NoDestroy<OrthoState> ortho;
    return ortho.get();
}

// Alt+left drag.
struct OrbitState {
    bool pending = false, orbiting = false;
    float travel = 0.0F;
    float dx = 0.0F, dy = 0.0F;
    int wheel = 0;
    bool have_pivot_ = false;
    V3 pivot = {};
};
// NOT ORIGINAL grouping - level_editor_camera.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_camera_state()).
struct LevelEditorCameraState {
    OrbitState orbit_{};
    uint32_t scene_{};
    std::array<Bookmark, kBookmarks> marks_{};
    float fly_held_{};
};

LevelEditorCameraState& level_editor_camera_state()
{
    static core::NoDestroy<LevelEditorCameraState> state;
    return state.get();
}

std::string bookmarks_path() { return game_folder_path("thug2-decomp-layouts") + "\\" + bookmarks_file_name(level_editor_camera_state().scene_); }

void load_bookmarks()
{
    for (Bookmark& bookmark : level_editor_camera_state().marks_) {
        bookmark = Bookmark{};
    }
    const std::ifstream file(bookmarks_path(), std::ios::binary);
    if (!file) {
        return;
    }
    std::stringstream text;
    text << file.rdbuf();
    if (!parse_bookmarks(text.str(), level_editor_camera_state().marks_)) {
        get_log("level editor: %s isn't a bookmarks file - not read", bookmarks_path().c_str());
    }
}

bool save_bookmarks()
{
    CreateDirectoryA(game_folder_path("thug2-decomp-layouts").c_str(), nullptr);
    std::ofstream file(bookmarks_path(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << format_bookmarks(level_editor_camera_state().marks_);
    return static_cast<bool>(file);
}

V3 get_forward(float yaw, float pitch) { return frame_from_yaw_pitch(yaw, pitch).back * -1.0F; }

void fly_to(const Rig& rig, const Pose& to, View then_view = View::kNone, float then_equivalent = 0.0F)
{
    stored_flight().on = true;
    stored_flight().from = {.pos = *rig.pos, .yaw = *rig.yaw, .pitch = *rig.pitch};
    stored_flight().to = to;
    stored_flight().t = 0.0F;
    stored_flight().then_view_ = then_view;
    stored_flight().then_equivalent_ = then_equivalent;
}

void enter_ortho(const Rig& rig, View view, float yaw, float pitch, V3 pivot, float equivalent)
{
    stored_ortho().view = view;
    stored_ortho().yaw = yaw;
    stored_ortho().pitch = pitch;
    stored_ortho().pivot = rig.have_selection_ ? rig.target : pivot;
    stored_ortho().equivalent = std::clamp(equivalent, kMinOrthoEquivalent, kMaxOrthoEquivalent);
    *rig.yaw = yaw;
    *rig.pitch = pitch;
    const V3 forward = get_forward(yaw, pitch);
    *rig.pos = stored_ortho().pivot - forward * eye_distance(stored_ortho().equivalent);
    if (rig.have_selection_) {
        *rig.orbit_distance_ = eye_distance(stored_ortho().equivalent);
    }
    stored_ortho().last_eye_ = *rig.pos;
    hud::get_message(std::string(view_name(view)) + " (orthographic)", kOrthoMessageSeconds);
}

void exit_ortho(const Rig& rig)
{
    if (stored_ortho().view == View::kNone) {
        return;
    }
    const V3 forward = get_forward(*rig.yaw, *rig.pitch);
    const V3 pivot = rig.have_selection_ ? rig.target : stored_ortho().pivot;
    *rig.pos = pivot - forward * stored_ortho().equivalent;
    if (rig.have_selection_) {
        *rig.orbit_distance_ = std::clamp(stored_ortho().equivalent, kMinOrbitDistance, kMaxOrbitDistance);
    }
    stored_ortho().view = View::kNone;
    hud::get_message("Perspective", kBriefMessageSeconds);
}

// The point the view looks at: the selection, else the surface ahead (within 20000), else 1000 ahead.
V3 look_point(const Rig& rig)
{
    if (rig.have_selection_) {
        return rig.target;
    }
    const V3 forward = get_forward(*rig.yaw, *rig.pitch);
    V3 hit{};
    if (rig.cast && rig.cast(*rig.pos, *rig.pos + forward * kForwardCastReach, &hit)) {
        return hit;
    }
    return *rig.pos + forward * kFallbackPivotDistance;
}

} // namespace

void kickoff(uint32_t scene)
{
    stored_flight() = Flight{};
    stored_ortho() = OrthoState{};
    level_editor_camera_state().orbit_ = OrbitState{};
    level_editor_camera_state().fly_held_ = 0.0F;
    level_editor_camera_state().scene_ = scene;
    load_bookmarks();
}

void halt()
{
    stored_flight() = Flight{};
    stored_ortho().view = View::kNone;
    level_editor_camera_state().orbit_ = OrbitState{};
}

void get_cancel()
{
    stored_flight() = Flight{};
    stored_ortho().view = View::kNone;
    level_editor_camera_state().orbit_ = OrbitState{};
}

bool ortho() { return stored_ortho().view != View::kNone; }
View current_view() { return stored_ortho().view; }

float apparent(float distance)
{
    if (!ortho()) {
        return distance;
    }
    return distance * stored_ortho().equivalent / eye_distance(stored_ortho().equivalent);
}

float adjust_view_angle(float base_degrees)
{
    return ortho() ? ortho_angle({.base_degrees_ = base_degrees, .equivalent = stored_ortho().equivalent}) : base_degrees;
}

float get_near_plane() { return ortho() ? ortho_near(stored_ortho().equivalent) : kPerspectiveNearPlane; }

std::string get_standing()
{
    std::string report;
    if (ortho()) {
        std::array<char, kSpeedTextSize> text{};
        text::format_to(text, "%s ortho (%.0f)", view_name(stored_ortho().view), stored_ortho().equivalent);
        report = text.data();
    }
    if (level_editor_camera_state().orbit_.orbiting) {
        report += report.empty() ? "orbiting" : "  orbiting";
    }
    return report;
}

bool get_orbiting() { return level_editor_camera_state().orbit_.orbiting; }

void filter_mouse(const Rig& rig, MouseIn mouse_in, bool alt, bool have_last_ray, V3 last_ray)
{
    // The wheel with the right button held: the flying speed (Unreal's).
    if (mouse_in.right && (*mouse_in.wheel != 0) && !level_editor_camera_state().orbit_.orbiting) {
        *rig.speed = std::clamp(*rig.speed * std::pow(kWheelSpeedStep, static_cast<float>(*mouse_in.wheel)),
                                kMinFlySpeed, kMaxFlySpeed);
        *mouse_in.wheel = 0;
        std::array<char, kSpeedTextSize> text{};
        text::format_to(text, "Fly speed %.0f", *rig.speed);
        hud::get_message(text.data(), kSpeedMessageSeconds);
    }
    // Looking about ends an ortho view (Blender's auto perspective).
    if (mouse_in.right && (*mouse_in.rel_x_ != 0.0F || *mouse_in.rel_y_ != 0.0F) && ortho()) {
        exit_ortho(rig);
    }
    // Alt+left: held back until it moves (an orbit) or is let go (an Alt+click).
    if (!level_editor_camera_state().orbit_.pending && (*mouse_in.left_presses_ != 0) && alt && !rig.dragging &&
        !mouse_in.right) {
        level_editor_camera_state().orbit_ = OrbitState{};
        level_editor_camera_state().orbit_.pending = true;
        *mouse_in.left_presses_ = 0;
        // (the pivot: the selection, else the surface under the cursor where the press was)
        if (!rig.have_selection_ && have_last_ray && rig.cast) {
            const V3 dir = make_unit(last_ray);
            level_editor_camera_state().orbit_.have_pivot_ = rig.cast(*rig.pos, *rig.pos + dir * kPivotCastReach, &level_editor_camera_state().orbit_.pivot);
        }
        if (!rig.have_selection_ && !level_editor_camera_state().orbit_.have_pivot_) {
            level_editor_camera_state().orbit_.pivot = look_point(rig);
            level_editor_camera_state().orbit_.have_pivot_ = true;
        }
        return;
    }
    if (!level_editor_camera_state().orbit_.pending) {
        return;
    }
    if (!level_editor_camera_state().orbit_.orbiting) {
        level_editor_camera_state().orbit_.travel += std::fabs(*mouse_in.rel_x_) + std::fabs(*mouse_in.rel_y_);
        if (level_editor_camera_state().orbit_.travel >= kOrbitDragTravel) {
            level_editor_camera_state().orbit_.orbiting = true;
            stored_flight().on = false;
            if (ortho()) {
                exit_ortho(rig);
            }
        }
    }
    if (level_editor_camera_state().orbit_.orbiting) {
        level_editor_camera_state().orbit_.dx += *mouse_in.rel_x_;
        level_editor_camera_state().orbit_.dy += *mouse_in.rel_y_;
        level_editor_camera_state().orbit_.wheel += *mouse_in.wheel;
        *mouse_in.rel_x_ = *mouse_in.rel_y_ = 0.0F;
        *mouse_in.wheel = 0;
    }
    if (!mouse_in.left) {
        // let go: unmoved, the press given back as an Alt+click (with its release)
        if (!level_editor_camera_state().orbit_.orbiting) {
            *mouse_in.left_presses_ = 1;
            *mouse_in.left_releases_ = 1; // (the button is up: a press and its release, as a quick click)
        } else {
            *mouse_in.left_releases_ = 0;
        }
        level_editor_camera_state().orbit_.pending = false;
        level_editor_camera_state().orbit_.orbiting = false;
    }
}

void apply_orbit(const Rig& rig)
{
    if (!level_editor_camera_state().orbit_.orbiting && level_editor_camera_state().orbit_.dx == 0.0F &&
        level_editor_camera_state().orbit_.dy == 0.0F && level_editor_camera_state().orbit_.wheel == 0) {
        return;
    }
    const prefs::Values& prefs = prefs::get_pull();
    const float sensitivity = 0.0035F * static_cast<float>(prefs.look_pct_) / 100.0F;
    const float dy = prefs.invert_y_ ? -level_editor_camera_state().orbit_.dy : level_editor_camera_state().orbit_.dy;
    const TickCycle before = frame_from_yaw_pitch(*rig.yaw, *rig.pitch);
    get_look({.yaw = rig.yaw, .pitch = rig.pitch}, level_editor_camera_state().orbit_.dx, dy, sensitivity);
    level_editor_camera_state().orbit_.dx = level_editor_camera_state().orbit_.dy = 0.0F;
    const int wheel = level_editor_camera_state().orbit_.wheel;
    level_editor_camera_state().orbit_.wheel = 0;
    if (rig.have_selection_) {
        // (the editor puts the camera round it at its orbit distance: the wheel nearer / further)
        if (wheel != 0) {
            *rig.orbit_distance_ =
                std::clamp(*rig.orbit_distance_ * std::pow(kWheelZoomStep, static_cast<float>(wheel)), kMinOrbitDistance, kMaxOrbitDistance);
        }
        return;
    }
    if (!level_editor_camera_state().orbit_.have_pivot_) {
        return;
    }
    const TickCycle after = frame_from_yaw_pitch(*rig.yaw, *rig.pitch);
    V3 pos = orbit_about(*rig.pos, level_editor_camera_state().orbit_.pivot, before, after);
    if (wheel != 0) {
        const V3 offset = pos - level_editor_camera_state().orbit_.pivot;
        const float length = std::sqrt(get_dot(offset, offset));
        const float wanted = std::clamp(length * std::pow(0.85F, static_cast<float>(wheel)), 20.0F, 30000.0F);
        if (length > kMinDirectionLength) {
            pos = level_editor_camera_state().orbit_.pivot + offset * (wanted / length);
        }
    }
    *rig.pos = pos;
}

namespace {

// F: the selection framed - an ortho view zoomed to fit, else the camera flown back to see it.
void frame_selection(const Rig& rig)
{
    if (!rig.have_selection_) {
        hud::get_message("Nothing selected", kStatusMessageSeconds);
    } else {
        const float tan_half = std::min(rig.lens.tan_x_, rig.lens.tan_y_);
        if (ortho()) {
            // (an ortho view: its zoom to fit, the lens as perspective has it)
            const float base =
                tan_half * eye_distance(stored_ortho().equivalent) / std::max(1.0F, stored_ortho().equivalent);
            stored_ortho().equivalent =
                std::clamp(frame_distance({.radius = rig.radius, .tan_half_ = base, .minimum = 100.0F}),
                           kMinOrthoEquivalent, kMaxOrthoEquivalent);
        } else {
            const float distance =
                std::min(20000.0F, frame_distance({.radius = rig.radius, .tan_half_ = tan_half, .minimum = 100.0F}));
            const TickCycle frame = frame_from_yaw_pitch(*rig.yaw, *rig.pitch);
            fly_to(rig, {.pos = rig.target + frame.back * distance, .yaw = *rig.yaw, .pitch = *rig.pitch});
        }
        if (prefs::get_pull().speed_by_selection_) {
            *rig.speed = speed_for_radius(rig.radius);
        }
    }
}

// Camera bookmark `index` stored (Ctrl+n), or recalled (n): flown to, an ortho one in its view.
void use_bookmark(const Rig& rig, int index, bool store)
{
    std::array<char, kFrameTextSize> text{};
    if (store) {
        Bookmark& bookmark = core::at(level_editor_camera_state().marks_, index);
        bookmark.set = true;
        bookmark.pose = {.pos = *rig.pos, .yaw = *rig.yaw, .pitch = *rig.pitch};
        bookmark.view = stored_ortho().view;
        bookmark.equivalent = stored_ortho().equivalent;
        if (ortho()) {
            bookmark.pose.pos =
                rig.have_selection_ ? rig.target : stored_ortho().pivot; // (an ortho bookmark keeps its pivot)
        }
        const bool saved = save_bookmarks();
        text::format_to(text, "Camera bookmark %d stored%s", index + 1, saved ? "" : " (not saved: the file)");
        hud::get_message(text.data(), kStatusMessageSeconds);
    } else if (core::at(level_editor_camera_state().marks_, index).set) {
        const Bookmark& bookmark = core::at(level_editor_camera_state().marks_, index);
        if (rig.deselect) {
            rig.deselect();
        }
        Rig free = rig;
        free.have_selection_ = false;
        exit_ortho(free);
        if (bookmark.view != View::kNone) {
            // (fly to the perspective place it stands for, then the ortho view)
            const V3 forward = get_forward(bookmark.pose.yaw, bookmark.pose.pitch);
            fly_to(free,
                   {.pos = bookmark.pose.pos - forward * bookmark.equivalent,
                    .yaw = bookmark.pose.yaw,
                    .pitch = bookmark.pose.pitch},
                   bookmark.view, bookmark.equivalent);
        } else {
            fly_to(free, bookmark.pose);
        }
        text::format_to(text, "Camera bookmark %d", index + 1);
        hud::get_message(text.data(), kBriefMessageSeconds);
    } else {
        text::format_to(text, "No camera bookmark %d yet - Ctrl+%d stores one", index + 1, index + 1);
        hud::get_message(text.data(), kStatusMessageSeconds);
    }
}

} // namespace

void get_keys(const Rig& rig, input::KeyState keys, bool ctrl, bool browser_open)
{
    // (the keys from the keymap - level_editor_actions.h: rebindable)
    (void)keys;
    (void)ctrl;
    // F: the selection framed (the browser has its own F).
    if (actions::squeezed(actions::kFrameSelection) && !browser_open) {
        frame_selection(rig);
    }
    // 1..9 recall a bookmark, Ctrl+1..9 store it. (Alt+1..4 are the view modes - level_editor_overlays.h.)
    const bool alt = (core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U);
    for (int i = 0; i < kBookmarks; ++i) {
        const bool store = actions::squeezed(actions::kBookmarkStore1 + i);
        if (alt || (!store && !actions::squeezed(actions::kBookmark1 + i))) {
            continue;
        }
        use_bookmark(rig, i, store);
    }
    // The numpad: 7 top, 1 front, 3 right (Ctrl: the opposite), 5 perspective / ortho.
    static const std::array<std::array<int, 2>, 4> kPad = {{{actions::kViewTop, actions::kViewBottom},
                                   {actions::kCamFrontView, actions::kViewBack},
                                   {actions::kPanRightAction, actions::kPanLeftAction},
                                   {actions::kViewOrtho, actions::kViewOrtho}}};
    for (int i = 0; i < 4; ++i) {
        const bool opposite = actions::squeezed(core::at(core::at(kPad, i), 1)) && i != 3;
        if (!opposite && !actions::squeezed(core::at(core::at(kPad, i), 0))) {
            continue;
        }
        stored_flight().on = false;
        if (i == 3) {
            if (ortho()) {
                exit_ortho(rig);
            } else {
                const V3 pivot = look_point(rig);
                const V3 delta = pivot - *rig.pos;
                enter_ortho(rig, View::kDealloc, *rig.yaw, *rig.pitch, pivot, std::sqrt(get_dot(delta, delta)));
            }
            continue;
        }
        static const std::array<std::array<View, 2>, 3> kViews = {{
            {View::kTop, View::kBottom}, {View::kMenuSystem, View::kBack}, {View::kRight, View::kLeft}}};
        const View view = core::at(core::at(kViews, i), opposite ? 1 : 0);
        V3 pivot{};
        if (!ortho()) {
            pivot = look_point(rig);
        } else if (rig.have_selection_) {
            pivot = rig.target;
        } else {
            pivot = stored_ortho().pivot;
        }
        const V3 delta = pivot - *rig.pos;
        const float equivalent =
            ortho() ? stored_ortho().equivalent : std::clamp(std::sqrt(get_dot(delta, delta)), 200.0F, 20000.0F);
        float yaw = 0.0F;
        float pitch = 0.0F;
        axis_yaw_pitch(view, {.yaw = &yaw, .pitch = &pitch});
        enter_ortho(rig, view, yaw, pitch, pivot, equivalent);
    }
}

void after_update(const Rig& rig, int wheel, bool interrupted)
{
    if (stored_flight().on && interrupted) {
        stored_flight().on = false;
    }
    if (stored_flight().on) {
        // (the frame's time from SDL's clock: the editor caps a frame at a tenth of a second)
        static uint64_t last;
        const uint64_t now = SDL_GetPerformanceCounter();
        const double seconds =
            stored_flight().t == 0.0F || (last == 0U)
                ? 1.0 / 60.0
                : static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;
        stored_flight().t += static_cast<float>(std::min(seconds, kMaxFlightStepSeconds)) / stored_flight().duration;
        Pose to = stored_flight().to;
        if (rig.have_selection_) {
            // (the selection may move meanwhile: the place is kept relative to it)
            const TickCycle frame = frame_from_yaw_pitch(to.yaw, to.pitch);
            const V3 delta = stored_flight().to.pos - rig.target;
            to.pos = rig.target + frame.back * std::sqrt(get_dot(delta, delta));
        }
        const Pose pose = get_blend(stored_flight().from, to, std::min(1.0F, stored_flight().t));
        *rig.pos = pose.pos;
        *rig.yaw = pose.yaw;
        *rig.pitch = pose.pitch;
        if (rig.have_selection_) {
            const V3 delta = *rig.pos - rig.target;
            *rig.orbit_distance_ = std::clamp(std::sqrt(get_dot(delta, delta)), kMinOrbitDistance, kMaxOrbitDistance);
        }
        if (stored_flight().t >= 1.0F) {
            stored_flight().on = false;
            if (stored_flight().then_view_ != View::kNone) {
                const V3 forward = get_forward(to.yaw, to.pitch);
                enter_ortho(rig, stored_flight().then_view_, to.yaw, to.pitch, to.pos + forward * stored_flight().then_equivalent_,
                            stored_flight().then_equivalent_);
            } else if (rig.have_selection_ && rig.focus) {
                rig.focus();
            }
        }
        return;
    }
    if (!ortho()) {
        return;
    }
    // The ortho view: its heading kept, the camera far back along it.
    *rig.yaw = stored_ortho().yaw;
    *rig.pitch = stored_ortho().pitch;
    const V3 forward = get_forward(stored_ortho().yaw, stored_ortho().pitch);
    if (rig.have_selection_) {
        if ((wheel != 0) && !rig.dragging) {
            stored_ortho().equivalent =
                std::clamp(stored_ortho().equivalent * std::pow(kWheelZoomStep, static_cast<float>(wheel)), kMinOrthoEquivalent, kMaxOrthoEquivalent);
        }
        stored_ortho().pivot = rig.target;
    } else {
        // (the editor flew the camera: along the view a zoom, across it a pan)
        const V3 moved = *rig.pos - stored_ortho().last_eye_;
        const float along = get_dot(moved, forward);
        stored_ortho().equivalent = std::clamp(stored_ortho().equivalent - along, kMinOrthoEquivalent, kMaxOrthoEquivalent);
        stored_ortho().pivot = stored_ortho().pivot + (moved - forward * along);
    }
    const float eye = eye_distance(stored_ortho().equivalent);
    *rig.pos = stored_ortho().pivot - forward * eye;
    if (rig.have_selection_) {
        *rig.orbit_distance_ = eye;
    }
    stored_ortho().last_eye_ = *rig.pos;
}

float fly_factor(bool held, double seconds)
{
    level_editor_camera_state().fly_held_ =
        held ? level_editor_camera_state().fly_held_ + static_cast<float>(seconds) : 0.0F;
    return fly_ramp(level_editor_camera_state().fly_held_, prefs::get_pull().fly_accel_ms_);
}

} // namespace frontend::level_editor::view_cam
