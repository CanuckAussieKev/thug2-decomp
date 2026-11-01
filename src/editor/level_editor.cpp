// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's). See
// level_editor.h.

#include "editor/level_editor.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_arrange.h"
#include "editor/level_editor_arcs.h"
#include "editor/level_editor_budget.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_grind_preview.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_links.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_preview.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_ruler.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_shadows.h"
#include "editor/level_editor_snap.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "frontend/mouse_menus.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/screen.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "platform/sdl_events.h"
#include "platform/run_log.h"
#include "engine/gfx/device.h"
#include "platform/code_address.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor {

// --- the pieces -------------------------------------------------------------------------------------------------

V3 make_unit(V3 v)
{
    const double length =
        std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
    if (length <= 0.0)
        return v;
    return {static_cast<float>(v.x / length), static_cast<float>(v.y / length), static_cast<float>(v.z / length)};
}

TickCycle frame_from_yaw_pitch(float yaw, float pitch)
{
    const double cp = std::cos(static_cast<double>(pitch));
    const double sp = std::sin(static_cast<double>(pitch));
    const double cy = std::cos(static_cast<double>(yaw));
    const double sy = std::sin(static_cast<double>(yaw));
    const V3 forward = {static_cast<float>(sy * cp), static_cast<float>(sp), static_cast<float>(cy * cp)};
    TickCycle frame{};
    frame.back = forward * -1.0f;
    frame.right = make_unit(perpendicular({0.0f, 1.0f, 0.0f}, frame.back));
    frame.up = make_unit(perpendicular(frame.back, frame.right));
    return frame;
}

void yaw_pitch_from_back(V3 back, float* yaw, float* pitch)
{
    const V3 forward = make_unit(back * -1.0f);
    *yaw = static_cast<float>(std::atan2(static_cast<double>(forward.x), static_cast<double>(forward.z)));
    *pitch = static_cast<float>(std::asin(std::max(-1.0, std::min(1.0, static_cast<double>(forward.y)))));
}

V3 get_fly(V3 pos, const TickCycle& frame, const FlyInput& input, float distance)
{
    const V3 forward = frame.back * -1.0f;
    const V3 move = forward * static_cast<float>(input.forward) + frame.right * static_cast<float>(input.right) +
                    V3{0.0f, static_cast<float>(input.up), 0.0f};
    if (move.x == 0.0f && move.y == 0.0f && move.z == 0.0f)
        return pos;
    return pos + make_unit(move) * distance;
}

void get_look(float* yaw, float* pitch, float dx, float dy, float radians_per_pixel)
{
    constexpr float kLimit = 89.0f * 3.14159265f / 180.0f;
    // (yaw turns +z towards +x, which is to the left of a view along +z: right is -yaw)
    *yaw -= dx * radians_per_pixel;
    *pitch = std::max(-kLimit, std::min(kLimit, *pitch - dy * radians_per_pixel));
    constexpr float kTurn = 2.0f * 3.14159265f;
    if (*yaw > kTurn || *yaw < -kTurn)
        *yaw = std::fmod(*yaw, kTurn);
}

Lens lens_for(float angle_degrees, float aspect)
{
    const double half_angle = static_cast<double>(angle_degrees) * 0.008726646f;
    const double tan_x = std::tan(half_angle);
    const double height_factor = 1.0 / (static_cast<double>(aspect) * 0.90909094f);
    return {static_cast<float>(tan_x), static_cast<float>(tan_x * height_factor)};
}

bool pixel_to_ndc(const uint32_t viewport[4], float px, float py, float* nx, float* ny)
{
    if (viewport[2] == 0 || viewport[3] == 0)
        return false;
    *nx = static_cast<float>((static_cast<double>(px) - viewport[0]) / viewport[2] * 2.0 - 1.0);
    *ny = static_cast<float>(1.0 - (static_cast<double>(py) - viewport[1]) / viewport[3] * 2.0);
    return true;
}

void ndc_to_pixel(const uint32_t viewport[4], float nx, float ny, float* px, float* py)
{
    *px = static_cast<float>(viewport[0] + (static_cast<double>(nx) + 1.0) * 0.5 * viewport[2]);
    *py = static_cast<float>(viewport[1] + (1.0 - static_cast<double>(ny)) * 0.5 * viewport[3]);
}

V3 ray_direction(const TickCycle& frame, const Lens& lens, float nx, float ny)
{
    return frame.back * -1.0f + frame.right * (nx * lens.tan_x_) + frame.up * (ny * lens.tan_y_);
}

bool cast_onto(V3 camera_pos, const TickCycle& frame, const Lens& lens, V3 point, float near_z, float* nx, float* ny)
{
    const V3 d = point - camera_pos;
    const float depth = -get_dot(d, frame.back);
    if (depth < near_z || lens.tan_x_ == 0.0f || lens.tan_y_ == 0.0f)
        return false;
    *nx = get_dot(d, frame.right) / (depth * lens.tan_x_);
    *ny = get_dot(d, frame.up) / (depth * lens.tan_y_);
    return true;
}

bool ray_sphere(V3 origin, V3 dir, V3 centre, float radius, float* t)
{
    const V3 oc = origin - centre;
    const double a = get_dot(dir, dir);
    const double b = 2.0 * get_dot(oc, dir);
    const double c = static_cast<double>(get_dot(oc, oc)) - radius * radius;
    if (a <= 0.0)
        return false;
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
        return false;
    const double root = std::sqrt(discriminant);
    const double t_near = (-b - root) / (2.0 * a);
    const double t_far = (-b + root) / (2.0 * a);
    if (t_far < 0.0)
        return false;
    *t = static_cast<float>(t_near >= 0.0 ? t_near : 0.0); // (inside: at once)
    return true;
}

bool ray_box(V3 origin, V3 dir, V3 box_origin, const V3 axes[3], V3 min, V3 max, float* t)
{
    // The ray in the box's own coordinates (p = local.x * axes[0] + local.y * axes[1] + local.z * axes[2] + origin;
    // the rows are taken as orthogonal).
    const V3 rel = origin - box_origin;
    double o[3], d[3];
    for (int i = 0; i < 3; ++i) {
        const double length_squared = get_dot(axes[i], axes[i]);
        if (length_squared <= 0.0)
            return false;
        core::at(o, i) = get_dot(rel, axes[i]) / length_squared;
        core::at(d, i) = get_dot(dir, axes[i]) / length_squared;
    }
    const double lo[3] = {min.x, min.y, min.z}, hi[3] = {max.x, max.y, max.z};
    double enter = -1e30, leave = 1e30;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(core::at(d, i)) < 1e-12) {
            if (core::at(o, i) < core::at(lo, i) || core::at(o, i) > core::at(hi, i))
                return false;
            continue;
        }
        double t0 = (core::at(lo, i) - core::at(o, i)) / core::at(d, i), t1 = (core::at(hi, i) - core::at(o, i)) / core::at(d, i);
        if (t0 > t1)
            std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave)
            return false;
    }
    if (leave < 0.0)
        return false;
    *t = static_cast<float>(enter >= 0.0 ? enter : 0.0);
    return true;
}

bool ray_plane(V3 origin, V3 dir, V3 point, V3 normal, float* t)
{
    const double denominator = get_dot(dir, normal);
    if (std::fabs(denominator) < 1e-9)
        return false;
    const double hit = get_dot(point - origin, normal) / denominator;
    if (hit < 0.0)
        return false;
    *t = static_cast<float>(hit);
    return true;
}

void box_corners(V3 box_origin, const V3 axes[3], V3 min, V3 max, V3 out[8])
{
    for (int i = 0; i < 8; ++i) {
        const float x = (i & 1) ? max.x : min.x;
        const float y = (i & 2) ? max.y : min.y;
        const float z = (i & 4) ? max.z : min.z;
        out[i] = box_origin + axes[0] * x + axes[1] * y + axes[2] * z;
    }
}

bool box_looks_world_space(V3 min, V3 max, V3 object_pos)
{
    auto holds = [&](V3 p, float margin) {
        return p.x >= min.x - margin && p.x <= max.x + margin && p.y >= min.y - margin && p.y <= max.y + margin &&
               p.z >= min.z - margin && p.z <= max.z + margin;
    };
    const float size = std::max({max.x - min.x, max.y - min.y, max.z - min.z, 1.0f});
    return holds(object_pos, size * 0.05f) && !holds({0.0f, 0.0f, 0.0f}, size * 0.05f);
}

V3 turn_about_up(V3 v, float angle)
{
    const double c = std::cos(static_cast<double>(angle));
    const double s = std::sin(static_cast<double>(angle));
    return {static_cast<float>(v.x * c + v.z * s), v.y, static_cast<float>(-v.x * s + v.z * c)};
}

bool closest_on_axis(V3 p, V3 u, V3 o, V3 d, float* s)
{
    // Minimise |(p + u s) - (o + d t)|: with w = p - o, a = u.u, b = u.d, c = d.d, e = u.w, f = d.w,
    // s = (b f - c e) / (a c - b b).
    const V3 w = p - o;
    const double a = get_dot(u, u);
    const double b = get_dot(u, d);
    const double c = get_dot(d, d);
    const double e = get_dot(u, w);
    const double f = get_dot(d, w);
    const double denominator = a * c - b * b;
    if (std::fabs(denominator) < 1e-9 * a * c || a <= 0.0 || c <= 0.0)
        return false;
    *s = static_cast<float>((b * f - c * e) / denominator);
    return true;
}

float distance_to_segment(float x, float y, float ax, float ay, float bx, float by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = dx * dx + dy * dy;
    double t = length > 0.0 ? ((x - ax) * dx + (y - ay) * dy) / length : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    const double px = ax + dx * t - x;
    const double py = ay + dy * t - y;
    return static_cast<float>(std::sqrt(px * px + py * py));
}

float snap_to_grid(float value, float grid) { return grid > 0.0f ? std::round(value / grid) * grid : value; }

V3 ground_ray_start(V3 pos, float base, bool from_above)
{
    return from_above ? V3{pos.x, base + 50.0f, pos.z} : V3{pos.x, base - 1.0f, pos.z};
}

V3 dropped_to(V3 pos, float base, float ground_y) { return {pos.x, pos.y + (ground_y - base), pos.z}; }

bool clip_to_near(V3 camera_pos, const TickCycle& frame, float near_z, V3* a, V3* b)
{
    const float da = -get_dot(*a - camera_pos, frame.back);
    const float db = -get_dot(*b - camera_pos, frame.back);
    if (da < near_z && db < near_z)
        return false;
    if (da < near_z)
        *a = *a + (*b - *a) * ((near_z - da) / (db - da));
    else if (db < near_z)
        *b = *b + (*a - *b) * ((near_z - db) / (da - db));
    return true;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr float kNearZ = 2.0f; // (render::choose_camera's near plane)
constexpr float kPi = 3.14159265f;
constexpr float kLookRadiansPerPixel = 0.0035f;
constexpr float kMaxPickDistance = 30000.0f;
constexpr float kMinOrbit = 20.0f, kMaxOrbit = 20000.0f;
constexpr double kObjectSpeed = 150.0; // units a second (Shift: 4x)
constexpr double kWheelStep = 120.0;   // units a notch while flying (Shift: 4x)
constexpr float kArrowGrab = 8.0f;     // (pixels from a move arrow that grab it)

bool g_active;
// The camera we fly (the first viewport's, when the editor started) and the pose the game had given it.
gfx::ViewCam* g_camera;
maths::Vec4 g_saved_pos;
maths::Mat4 g_saved_matrix;
// Our pose.
V3 g_pos;
float g_yaw, g_pitch;
float g_speed = 800.0f; // units a second (Shift: 4x)
// With something selected: the camera's distance from it, and whether the keys are moving it.
float g_orbit_distance = 300.0f;
bool g_key_moving;
// The view as last drawn (note_view).
bool g_have_view;
Lens g_lens;
std::array<uint32_t, 4> g_viewport;
uint64_t g_last_counter;
bool g_pause_key_released;
bool g_exit_requested;
int g_saved_cursor = -1;
bool g_relative;
int g_moves;
bool g_unsaved; // (changes since the layout was last saved)
// The autosave (level_editor_layout.h): which level load the unsaved changes belong to, a count of changes (the
// snapshot made again when it moves), when it was last written, and the offer of an older one.
int g_unsaved_load = -1;
int g_changes, g_snapshot_changes;
uint32_t g_last_autosave;
bool g_offer_show;    // (shown on the first frame the HUD is up)
bool g_offer_pending; // (Ctrl+R / Ctrl+Shift+R)
// What the editor last did, for the log if the game crashes (plain memory: the crash hook reads it).
std::array<char, 128> g_last_action = {"none"};
std::string describe_selection();             // (the HUD's)
int inspected_node();                         // (the node layer's)
std::vector<picking::Lock> selection_locks(); // (picking)
void follow_scenery();                        // (carrying - level_editor_carry.h)
// The camera tools and play from here (level_editor_camera.h, level_editor_play.h): the last frame's ray under the
// cursor (an orbit's pivot), the play key (held when the editor starts: it counts once let go).
bool g_last_have_ray;
V3 g_last_ray;
view_cam::Rig rig_now();
perform::Resume resume_now();
void apply_resume(const perform::Resume& resume);

template <typename... Args>
void note_action(const char* format, Args... args)
{
    std::snprintf(g_last_action.data(), sizeof(g_last_action), format, args...);
}

// A change not saved yet (undo / redo steps, layouts restored).
void mark_unsaved()
{
    g_unsaved = true;
    g_unsaved_load = layout::level_loads();
    ++g_changes;
}

// The crash logger's hook (platform/run_log.h): what the editor was doing, and its unsaved changes autosaved.
void on_crash()
{
    static int logged = 0;
    if (!g_active && !g_unsaved)
        return;
    if (logged < 4) {
        ++logged;
        get_log("level editor: %s at the exception; last action: %s", g_active ? "running" : "not running",
                g_last_action.data());
    }
    layout::write_autosave_from_crash();
}

// The mouse, from the events since the last Update.
struct Mouse {
    float x = 0.0f, y = 0.0f; // window pixels
    int window_width_ = 0, window_height_ = 0;
    float rel_x_ = 0.0f, rel_y_ = 0.0f;
    int wheel = 0;
    int left_presses_ = 0, left_releases_ = 0;
    int middle_presses_ = 0; // (as Alt+left click: one scenery piece in / out)
    bool left = false, right = false;
};
Mouse g_mouse;

// What is picked: the pointer (only used after checking it's still the object found under its id).
struct Picked {
    objects::GameEntity* object = nullptr;
    uint32_t id = 0;
};
Picked g_hover;
// The selected objects (the first is the one the camera and the arrows go by); or the selected scenery
// (Scenery::Selection) - never both.
std::vector<Picked> g_objects;
// The scene sector under the cursor (its checksum; 0: none).
uint32_t g_hover_sector;

// Dragging: free (the plane / the view's terms) or along one axis (an arrow).
bool g_dragging, g_drag_vertical;
int g_axis = -1;         // (0 / 1 / 2: dragging along x / y / z; -1: free)
int g_hover_axis = -1;   // (the arrow under the cursor)
int g_hover_handle = -1; // (a selected trigger volume's resize handle under the cursor - level_editor_triggers.h)
bool g_cancel_requested; // (Escape: a drag put back, the browser or the help closed - before leaving)
V3 g_drag_start;         // where the selection was when the drag started
V3 g_drag_offset, g_drag_plane_normal, g_drag_plane_offset;
float g_press_x, g_press_y;
bool g_drag_moved;
float g_drag_reach;
bool g_drag_screen;
V3 g_drag_from, g_drag_right, g_drag_forward;
float g_drag_mouse_x, g_drag_mouse_y, g_drag_units, g_drag_push, g_axis_t0;
// The transform tools (level_editor_transform.h): what the drag does (Move unless it started on a Rotate / Scale handle
// or as Alt+G's modal transform - no button held: a click or Enter applies it), what's typed, whether a turn or a scale
// was made (an undo step even when the position stayed), the turn and scale so far, where the cursor is (back buffer
// pixels), vertex snapping's corner, and the camera let go of the selection (wasd_flies=0: flying with RMB - F frames
// it again).
xform::Mode g_drag_mode = xform::Mode::kShiftBy;
bool g_modal, g_typed_apply, g_pending_forced;
xform::Typed g_typed;
V3 g_transform_pivot;
float g_rot_total, g_rot_applied, g_rot_last, g_rot_sign = 1.0f;
bool g_rot_by_plane;
int g_quarters_applied;
float g_scale_s0;
V3 g_scale_start = {1.0f, 1.0f, 1.0f};
float g_cursor_x, g_cursor_y;
bool g_vertex_have;
V3 g_vertex_corner;
bool g_free_camera;

// The ground marker: the selection's base and the ground below it.
bool g_have_mark;
V3 g_mark_base, g_mark_ground;

// The help panel (F1) and the browser (B).
bool g_help;
bool g_snap_always; // (the toolbar's Snap 25: moves snap without Ctrl)
// The node layer (level_editor_nodes.h): the families shown ([editor] families in the ini), a node selected by itself
// (a gizmo or an Outliner row that isn't an object or scenery - a point node moves and turns as objects do,
// level_editor_node_edit.h; chains and the rest are read-only), the gizmo under the cursor, and what the last frame
// drew of them (picked by screen distance).
uint32_t g_families = nodes::kAllFamilies;
bool g_node_selected;
int g_node_index;
uint32_t g_node_name;
bool g_field_editing; // (the Inspector's position / heading fields are being changed: one undo step)
bool g_online_warned; // (the warning for a spawn, crown or flag moved in a level shared with THUGPRO: once a run)
bool g_online_warning_shown; // (its window is up)
int g_hover_node = -1;
// Picking (level_editor_picking.h): what a click in the view may take ([editor] pick=), the piece a plain click passes
// under the cursor (for the status bar: Alt+click takes it), and click-to-cycle - the last click's spot, what was
// under it in the cycle's order and which of those it took (the hover shows the next one while it holds).
uint32_t g_pick_filter = picking::kAll;
uint32_t g_hover_passed;
struct Cycle {
    bool have = false;
    float x = 0.0f, y = 0.0f;
    V3 pos = {};
    int changes = 0;
    uint32_t filter = 0;
    std::vector<picking::TableRow> order;
    int index = -1;
};
Cycle g_cycle;
int g_hover_cycle = -1; // (the hover is the cycle's next entry: its index)
// Phase 3 (level_editor_node_create.h): the Place palette (N) and its armed entry (a click in the view places it), a
// level's node waiting for the confirm window before it's hidden.
bool g_place_open;
int g_place_armed = -1;
uint32_t g_confirm_hide;
// Reset level (Ctrl+Shift+Delete, the toolbar): the confirm window is up; the old HUD's second press (its time).
bool g_confirm_reset;
uint32_t g_reset_asked_at;
// Rails (level_editor_node_edit.h): a rail node moves with its whole rail (L toggles: one point).
bool g_move_entire_rail = true;
std::vector<nodes::Pickable> g_pickables;
ui::Overlay g_overlay;
int g_hud_taken_down; // (the UI runs: frames the HUD's screen elements have been taken down)
struct Browser {
    bool open = false;
    int tab = 0; // 0: scenery pieces, 1: objects
    int index = 0;
    int top = 0;
    int preview = -1; // (the entry the preview shows when not the highlighted one: the one hovered in the list)
    struct Item {
        uint32_t id;
        std::string label;
    };
    std::vector<Item> items;
};
Browser g_browser;
constexpr int kBrowserRows = 22;

void read_window_size(uint32_t window_id)
{
    if (SDL_Window* window = SDL_GetWindowFromID(window_id))
        SDL_GetWindowSize(window, &g_mouse.window_width_, &g_mouse.window_height_);
}

void handle_event(SDL_Event* event)
{
    if (!g_active)
        return;
    // (the UI first: over a panel, or while it wants the keys, the event is its own - level_editor_ui.h)
    if (ui::underway() && !ui::route_event(event, g_dragging || g_mouse.right || triggers::resizing()))
        return;
    switch (event->type) {
    case SDL_MOUSEMOTION:
        g_mouse.x = static_cast<float>(event->motion.x);
        g_mouse.y = static_cast<float>(event->motion.y);
        g_mouse.rel_x_ += static_cast<float>(event->motion.xrel);
        g_mouse.rel_y_ += static_cast<float>(event->motion.yrel);
        read_window_size(event->motion.windowID);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        const bool down = event->type == SDL_MOUSEBUTTONDOWN;
        if (!g_relative) {
            g_mouse.x = static_cast<float>(event->button.x);
            g_mouse.y = static_cast<float>(event->button.y);
        }
        read_window_size(event->button.windowID);
        if (event->button.button == SDL_BUTTON_LEFT) {
            g_mouse.left = down;
            ++(down ? g_mouse.left_presses_ : g_mouse.left_releases_);
        } else if (event->button.button == SDL_BUTTON_RIGHT) {
            g_mouse.right = down;
        } else if (event->button.button == SDL_BUTTON_MIDDLE && down) {
            ++g_mouse.middle_presses_;
        }
        break;
    }
    case SDL_MOUSEWHEEL:
        g_mouse.wheel += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        break;
    case SDL_KEYDOWN:
        // While dragging: X / Y / Z, digits, - . and Backspace typed (level_editor_transform.h), Enter applies.
        if (g_dragging) {
            const SDL_Scancode code = event->key.keysym.scancode;
            char c = 0;
            if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
                c = static_cast<char>('1' + (code - SDL_SCANCODE_1));
            else if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9)
                c = static_cast<char>('1' + (code - SDL_SCANCODE_KP_1));
            else if (code == SDL_SCANCODE_0 || code == SDL_SCANCODE_KP_0)
                c = '0';
            else if (code == SDL_SCANCODE_MINUS || code == SDL_SCANCODE_KP_MINUS)
                c = '-';
            else if (code == SDL_SCANCODE_PERIOD || code == SDL_SCANCODE_KP_PERIOD)
                c = '.';
            else if (code == SDL_SCANCODE_BACKSPACE)
                c = '\b';
            else if (!event->key.repeat && code >= SDL_SCANCODE_X && code <= SDL_SCANCODE_Z)
                c = static_cast<char>('x' + (code - SDL_SCANCODE_X));
            if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
                g_typed_apply = true;
                break;
            }
            if (c && xform::type_key(&g_typed, c))
                break;
        }
        // Escape: first what's open (a drag put back, the browser, the help), then the selection, then leave.
        if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE && !event->key.repeat && !actions::get_capturing()) {
            if (g_dragging || g_browser.open || g_help || g_place_open || g_confirm_hide || g_confirm_reset ||
                chains::get_drawing() != chains::Blit::kNone || chains::wants_pick() || !g_objects.empty() ||
                scenery::has_selection() || g_node_selected || ux8::wants_escape() || // (UX8: a pie, walking)
                ux9::occupied())                                                      // (UX9: a tool armed)
                g_cancel_requested = true;
            else
                g_exit_requested = true;
        }
        break;
    default:
        break;
    }
}

// --- the selection ---

objects::GameEntity* get_live(const Picked& picked)
{
    if (!picked.object)
        return nullptr;
    if (objects::find_object(picked.id) != picked.object || (picked.object->object_flags_ & objects::kObjectFlagDead))
        return nullptr;
    return picked.object;
}

// The selected objects still there (the others dropped).
std::vector<objects::GameEntity*> selected_objects()
{
    std::vector<objects::GameEntity*> live;
    std::vector<Picked> kept;
    for (const Picked& picked : g_objects) {
        if (objects::GameEntity* object = get_live(picked)) {
            live.emplace_back(object);
            kept.push_back(picked);
        }
    }
    g_objects = kept;
    return live;
}
objects::GameEntity* get_primary()
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    return objects.empty() ? nullptr : objects.front();
}
// The node selected by itself, if the editor moves it (a point node): its index in Nodes::All(), else -1.
int picked_node_index()
{
    if (!g_node_selected)
        return -1;
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (g_node_index < 0 || std::cmp_greater_equal(g_node_index, nodes.size()) ||
        core::at(nodes, g_node_index).name != g_node_name) {
        const int found = g_node_name ? nodes::get_find(g_node_name) : -1;
        if (found < 0)
            return -1;
        g_node_index = found;
    }
    return node_edit::get_movable(core::at(nodes, g_node_index)) ? g_node_index : -1;
}
// That node's place now (false: none).
bool node_values(node_edit::Values* values)
{
    return picked_node_index() >= 0 && node_edit::get_take_in(g_node_name, values);
}
// An invisible wall selected by itself (Shift+click with the collision shown - level_editor_view.h): moved, turned and
// switched off / on as level_editor_walls.h does; 0 when none (or something else is selected).
uint32_t wall_only()
{
    const uint32_t wall = view::selected_wall();
    return wall && !g_node_selected && !scenery::has_selection() && !get_primary() && walls::is_wall(wall) ? wall : 0;
}
bool have_selection() { return get_primary() || scenery::has_selection() || picked_node_index() >= 0 || wall_only(); }

// A spawn, crown or flag of a level shared with THUGPRO changed by the editor (moved, added, hidden, reordered): the
// warning, once a run (the edit is made - layouts aren't used in net games, but a layout baked into the level's files
// would be).
void note_online_change(nodes::Family family, const char* what, const char* verb)
{
    if (g_online_warned || !nodes::get_online(family))
        return;
    const char* game = "";
    if (!nodes::level_is_shared(&game))
        return;
    g_online_warned = true;
    g_online_warning_shown = true;
    get_log("level editor: %s %s in a level shared with THUGPRO (%s) - warned", what, verb, game);
    hud::get_message("Shared level: spawn, crown and flag changes differ from THUGPRO players' copies", 8.0);
}
void note_online_edit(int index)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()))
        return;
    note_online_change(core::at(nodes, index).family, nodes::caption(core::at(nodes, index).name).c_str(), "moved");
}

// The selected node put at `values` (an edit of the editor's: the online warning first time). A rail point takes the
// rest of its rail with it, by as much, while Whole rail is on (L).
void set_selected_node(const node_edit::Values& values)
{
    const int index = picked_node_index();
    if (index < 0)
        return;
    note_online_edit(index);
    node_edit::Values now;
    if (node_edit::is_chain(g_node_name) && node_edit::get_take_in(g_node_name, &now)) {
        const V3 delta = values.pos - now.pos;
        for (const uint32_t name : node_edit::moved_with(g_node_name, g_move_entire_rail)) {
            node_edit::Values point;
            if (node_edit::get_take_in(name, &point)) {
                point.pos = point.pos + delta;
                node_edit::assign_value(name, point);
            }
        }
        return;
    }
    node_edit::assign_value(g_node_name, values);
}

// The node `index` selected by itself (nothing else).
void select_node_only(int index)
{
    g_objects.clear();
    scenery::clear_selection();
    g_node_selected = true;
    g_node_index = index;
    g_node_name = core::at(nodes::get_all(), index).name;
}
bool is_picked(objects::GameEntity* object)
{
    return std::ranges::any_of(g_objects, [&](const Picked& p) { return p.object == object; });
}
void select_objects(const std::vector<objects::GameEntity*>& objects)
{
    g_objects.clear();
    for (objects::GameEntity* object : objects)
        g_objects.push_back({object, object->ident_});
    if (!objects.empty())
        scenery::clear_selection();
}
void unpick()
{
    g_free_camera = false; // (the next selection is framed again - level_editor_transform.h)
    g_objects.clear();
    scenery::clear_selection();
    g_node_selected = false;
    view::clear_wall();
    faces::clear_selection(); // (map3: the collision faces selected - level_editor_faces.h)
}

// Where the selection is: the first object's position, else the scenery's pivot (the middle of its boxes); its base:
// that object's feet, the scenery's bottom.
// (a node selected by itself: its pos=, which is its base too)
V3 selection_pos()
{
    if (const objects::GameEntity* object = get_primary())
        return object_edit::pos_of(object);
    node_edit::Values node;
    if (node_values(&node))
        return node.pos;
    if (const uint32_t wall = wall_only())
        return walls::get_pivot(wall);
    return scenery::get_pivot();
}
float get_selection_base()
{
    if (const objects::GameEntity* object = get_primary())
        return object->at_point_.y;
    node_edit::Values node;
    if (node_values(&node))
        return node.pos.y;
    if (const uint32_t wall = wall_only())
        return walls::get_bottom(wall);
    return scenery::get_bottom();
}
// The point the camera looks at: the first object's middle, a node's place, or the scenery's pivot.
V3 target_of()
{
    if (objects::GameEntity* object = get_primary())
        return object_edit::current_centre(object);
    node_edit::Values node;
    if (node_values(&node))
        return node.pos;
    if (const uint32_t wall = wall_only())
        return walls::get_pivot(wall);
    return scenery::get_pivot();
}
// The selection moved so that selection_pos is `pos` (the other objects by as much).
void place_selection(V3 pos)
{
    node_edit::Values node;
    if (node_values(&node)) {
        node.pos = pos;
        set_selected_node(node);
        return;
    }
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (const uint32_t wall = objects.empty() ? wall_only() : 0) {
        walls::move_to(wall, pos);
        return;
    }
    if (objects.empty()) {
        scenery::move_to(pos);
        follow_scenery();
        return;
    }
    const V3 delta = pos - object_edit::pos_of(objects.front());
    for (objects::GameEntity* object : objects)
        object_edit::get_place(object, object_edit::pos_of(object) + delta);
}
// The middle of the selection's box: the objects' model boxes (or places), the scenery's boxes; a node's or a wall's
// place (the Centre pivot - level_editor_transform.h).
V3 selection_centre()
{
    const std::vector<objects::GameEntity*> objects = selected_objects();
    V3 min = {}, max = {};
    bool have = false;
    OrientedBox box{};
    if (!objects.empty()) {
        for (objects::GameEntity* object : objects) {
            V3 a{}, b{};
            if (object_edit::model_box(object, &box))
                bounds_of(box, &a, &b);
            else
                a = b = object_edit::pos_of(object);
            grow_box(&min, &max, have, a, b);
            have = true;
        }
        return (min + max) * 0.5f;
    }
    if (picked_node_index() >= 0 || wall_only())
        return selection_pos();
    for (const uint32_t checksum : scenery::get_selection()) {
        V3 a{}, b{};
        if (scenery::cuboid(checksum, &box)) {
            bounds_of(box, &a, &b);
            grow_box(&min, &max, have, a, b);
            have = true;
        }
    }
    return have ? (min + max) * 0.5f : selection_pos();
}
// Objects turned by `radians` about the toolbar's Pivot (Base: the first one's position; Centre: `centre`; Own: each
// about its own).
void turn_objects_by(const std::vector<objects::GameEntity*>& objects, float radians, V3 centre)
{
    if (objects.empty() || radians == 0.0f)
        return;
    const xform::Pivot pivot = xform::at_hand().pivot;
    const V3 base = object_edit::pos_of(objects.front());
    for (objects::GameEntity* object : objects)
        object_edit::turn_about(object,
                                pivot == xform::Pivot::kOwn      ? object_edit::pos_of(object)
                                : pivot == xform::Pivot::kCentre ? centre
                                                                 : base,
                                radians);
}
// Turned a notch (an object or a node by the rotation snap's step - 15 degrees unless the toolbar says otherwise -
// about the Pivot / its own place; scenery a quarter about its pivot).
float notch_radians() { return xform::angle_step() * kPi / 180.0f; }
void turn_selection(int notches)
{
    node_edit::Values node;
    if (node_values(&node)) {
        node.angles = node_edit::with_heading(node.angles, node_edit::get_heading_degrees(node.angles) +
                                                               static_cast<float>(notches) * xform::angle_step());
        set_selected_node(node);
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (const uint32_t wall = objects.empty() ? wall_only() : 0) {
        walls::get_turn(wall, notches);
        walls::finalise();
        return;
    }
    if (objects.empty()) {
        scenery::get_turn(notches);
        follow_scenery();
        scenery::finalise();
        return;
    }
    turn_objects_by(objects, static_cast<float>(notches) * notch_radians(), selection_centre());
}
// After the keys or a drag: the scenery's collision lookups brought up to date.
void get_settle()
{
    if (!get_primary() && scenery::has_selection())
        scenery::finalise();
    if (wall_only())
        walls::finalise();
}

// --- undo / redo (Ctrl+Z / Ctrl+Y) ---
//
// A step is how things were before a change: the selected objects' places, or the scenery's state (Scenery::State:
// its pieces' placements, hidden or not, its rails), or - for copies made or deleted - whether those copies exist.
// Undoing puts that back and keeps what it replaced for redo.

struct MadeCopy {
    uint32_t name, source;
    object_edit::State state;
};
struct FieldUndo {
    uint32_t node, key;
    fields::Own own;
};
struct UndoStep {
    enum class Kind : uint8_t {
        kObjects,
        kScenery,
        kMade,
        kNodes,
        kAdded,
        kHidden,
        kOrder,
        kLevel,
        kChains,
        kWalls,
        kTriggers,
        kFields,
        kProps,
        kLocks,
        kEntities,
        kModule,
        kGroup
    } kind = Kind::kObjects;
    // kGroup (UX7 - level_editor_modules.h's Edit::fold): several steps made as one action, put back last first.
    std::vector<UndoStep> group;
    uint64_t serial = 0; // (a number for the step: the adjust-last panel knows its step is still the newest)
    // kEntities: path speeds and emitters' sounds to put back (level_editor_entities.h).
    entities::State entities;
    // kModule: a module's own step (level_editor_modules.h - face flags, clusters, cameras).
    std::shared_ptr<ModuleStep> module;
    // kLocks: the locks to put back (level_editor_picking.h - L, Unlock, Unlock all).
    std::vector<picking::Lock> locks;
    // kScenery: what the scenery carried along (level_editor_carry.h) - their places before the change.
    std::vector<object_edit::State> carried_objects_;
    node_edit::Step carried_nodes_;
    // kFields (phase 6): nodes' own fields to put back (level_editor_fields.h); kProps: the level's properties.
    std::vector<FieldUndo> fields;
    level_props::Props props;
    // kWalls (level_editor_walls.h): collision states to put back - an invisible wall's (wall: selected again) or the
    // selected scenery's switches (sectors: selected again).
    std::vector<walls::WallState> walls;
    uint32_t wall = 0;
    // kChains (phase 4): the chains' state to put back (level_editor_chains.h).
    chains::State chains;
    std::vector<object_edit::State> objects;
    scenery::State scenery;
    node_edit::Step nodes; // (kNodes: the nodes' places - level_editor_node_edit.h)
    // kMade: the scenery copies and object copies, and whether they exist once this step is put back.
    std::vector<uint32_t> sectors;
    std::vector<MadeCopy> copies;
    bool exist = false;
    V3 where = {}; // (the selection's position then: whether a drag moved it)
    // kAdded: nodes the editor added and where in the overlay (exist: whether they're there once put back); kHidden:
    // a level's nodes and whether they're hidden once put back; kOrder: the overlay's order (names) to put back.
    std::vector<node_create::Spec> added;
    std::vector<int> added_at_;
    std::vector<uint32_t> hidden_names_;
    bool hidden = false;
    std::vector<std::string> order;
    // kLevel (Reset level): put back, the level is reset again (`reset`) or the layout `layout` (Write's text) put in
    // place again.
    bool reset = false;
    std::string layout;
    // kTriggers: the trigger volumes' state to put back (level_editor_triggers.h).
    triggers::State triggers;
    // (the undo history panel - level_editor_ux3.h: a short label given where the step is made, and what was selected)
    std::string label, detail;
};
std::vector<UndoStep> g_undo, g_redo;
uint64_t g_undo_serial;
UndoStep g_pending;
bool g_has_pending;
constexpr size_t kMaxUndo = 64;

// The selection as it is now (before a change; the objects noted as changed, for layouts and Home).
UndoStep get_snapshot()
{
    UndoStep step;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (picked_node_index() >= 0) {
        step.kind = UndoStep::Kind::kNodes;
        // (a rail point: the points that move with it too)
        const std::vector<uint32_t> names = node_edit::moved_with(g_node_name, g_move_entire_rail);
        for (const uint32_t name : names)
            node_edit::note_origin(name);
        step.nodes = node_edit::get_capture(names, node_edit::get_take_in);
    } else if (const uint32_t wall = objects.empty() ? wall_only() : 0) {
        step.kind = UndoStep::Kind::kWalls;
        step.wall = wall;
        step.walls = walls::store_away({wall});
    } else if (!objects.empty()) {
        step.kind = UndoStep::Kind::kObjects;
        for (objects::GameEntity* object : objects) {
            object_edit::note_origin(object);
            object_edit::touch(object->ident_);
            step.objects.push_back(object_edit::store_away(object));
        }
    } else {
        step.kind = UndoStep::Kind::kScenery;
        step.scenery = scenery::save_state();
        // (what stands on it or is attached to it: carried by the change starting - level_editor_carry.h)
        carry::Taken taken = carry::arm();
        step.carried_objects_ = std::move(taken.objects);
        step.carried_nodes_ = std::move(taken.nodes);
    }
    step.where = selection_pos();
    return step;
}

// The scenery moved, turned or scaled: what it carries follows (a node carried in a level shared with THUGPRO: the
// online warning).
void follow_scenery()
{
    carry::get_follow([](uint32_t name) {
        const int index = nodes::get_find(name);
        if (index >= 0)
            note_online_edit(index);
    });
}

// A step's label when its maker gave none (by its kind).
const char* kind_label(UndoStep::Kind kind)
{
    switch (kind) {
    case UndoStep::Kind::kObjects:
        return "Move objects";
    case UndoStep::Kind::kScenery:
        return "Move scenery";
    case UndoStep::Kind::kMade:
        return "Copies";
    case UndoStep::Kind::kNodes:
        return "Move node";
    case UndoStep::Kind::kAdded:
        return "Add / remove node";
    case UndoStep::Kind::kHidden:
        return "Hide / show node";
    case UndoStep::Kind::kOrder:
        return "Crown order";
    case UndoStep::Kind::kLevel:
        return "Level layout";
    case UndoStep::Kind::kChains:
        return "Chain edit";
    case UndoStep::Kind::kWalls:
        return "Walls";
    case UndoStep::Kind::kTriggers:
        return "Trigger volume";
    case UndoStep::Kind::kFields:
        return "Fields";
    case UndoStep::Kind::kProps:
        return "Level properties";
    case UndoStep::Kind::kLocks:
        return "Locks";
    case UndoStep::Kind::kEntities:
        return "Path speed / sound";
    case UndoStep::Kind::kModule:
        return "Faces / clusters / cameras";
    case UndoStep::Kind::kGroup:
        return "Several changes";
    }
    return "Change";
}

void push_undo(UndoStep step, const char* label = nullptr)
{
    step.label = label && *label ? label : kind_label(step.kind);
    step.detail = describe_selection();
    step.serial = ++g_undo_serial;
    g_undo.push_back(std::move(step));
    if (g_undo.size() > kMaxUndo)
        g_undo.erase(g_undo.begin());
    g_redo.clear();
    mark_unsaved();
    ++g_moves;
}

// The locks as they are now, as an undo step (before L / Unlock / Unlock all change them).
void push_locks_undo(const char* label = "Lock / unlock")
{
    UndoStep step;
    step.kind = UndoStep::Kind::kLocks;
    step.locks = picking::get_locks();
    step.where = selection_pos();
    push_undo(step, label);
}

// The selected objects scaled by `factor` (along the world's axes - each object takes it along its own axis nearest
// each; `uniform`: the same on all three) about their positions. Whether any changed.
bool scale_objects(const std::vector<objects::GameEntity*>& objects, V3 factor, bool uniform)
{
    bool any = false;
    for (objects::GameEntity* object : objects) {
        if (!scale_tool::object_scalable(object))
            continue;
        V3 local = factor;
        if (!uniform) {
            std::array<V3, 3> rows{};
            for (int i = 0; i < 3; ++i)
                core::at(rows, i) = {core::at(object->basis_frame_.row, i).x, core::at(object->basis_frame_.row, i).y, core::at(object->basis_frame_.row, i).z};
            local = scale_tool::get_uniform(1.0f);
            const std::array<float, 3> along = {{factor.x, factor.y, factor.z}};
            for (int a = 0; a < 3; ++a) {
                if (core::at(along, a) == 1.0f)
                    continue;
                const int axis = scale_tool::nearest_axis(rows.data(), a);
                (axis == 0 ? local.x : axis == 1 ? local.y : local.z) = core::at(along, a);
            }
        }
        const V3 now = scale_tool::get_object_scale(object);
        const V3 allowed = scale_tool::allowed_factor(local, {now}, {scale_tool::object_limit(object)}, uniform);
        if (is_unit_scale(allowed))
            continue;
        if (scale_tool::set_object_scale(object, per_axis(now, allowed)))
            any = true;
    }
    return any;
}

// [ / ] and Alt+[ / ] (level_editor_scale.h): the selected scenery scaled by `factor` (along the world's axes) about
// the middle of its bottom, or the selected objects about their positions; one undo step. False when nothing changed
// (nodes, or at its limit - said on the message line).
bool scale_selection_by(V3 factor, bool uniform)
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (wall_only()) {
        // (level_editor_walls.h: a wall's collision is moved by the geometry's own Translate / RotateY on the BSP
        // tree's sixteenths; a scale would need its collision kept and rebuilt as level_editor_scale.h does scenery's)
        hud::get_message("Invisible walls can't be scaled");
        return false;
    }
    if (picked_node_index() >= 0 || (objects.empty() && !scenery::has_selection())) {
        hud::get_message("Nodes can't be scaled");
        return false;
    }
    if (!objects.empty() && std::ranges::none_of(objects, scale_tool::object_scalable)) {
        hud::get_message("Object has no model to scale");
        return false;
    }
    const UndoStep before = get_snapshot();
    const bool changed = objects.empty() ? !is_unit_scale(scenery::scale_selection(factor, uniform))
                                         : scale_objects(objects, factor, uniform);
    if (!changed) {
        hud::get_message("Scale limit reached");
        return false;
    }
    if (objects.empty()) {
        follow_scenery();
        scenery::finalise();
    }
    push_undo(before, "Scale");
    return true;
}

// The selection's scale (scenery: its first piece's, along its axes as first touched; an object: along its own).
V3 selection_scale_now()
{
    if (objects::GameEntity* object = get_primary())
        return scale_tool::get_object_scale(object);
    return scenery::selection_scale();
}
// The selection scaled to `scale` (along its own axes, as selection_scale_now): the Inspector's fields.
void scale_selection_to(V3 scale)
{
    const V3 now = selection_scale_now();
    const V3 local = {scale_tool::confine(scale.x) / now.x, scale_tool::confine(scale.y) / now.y,
                      scale_tool::confine(scale.z) / now.z};
    if (get_primary()) {
        for (objects::GameEntity* object : selected_objects()) {
            if (scale_tool::object_scalable(object))
                scale_tool::set_object_scale(object, per_axis(scale_tool::get_object_scale(object), local));
        }
        return;
    }
    // (the first piece's own axes to the world's: its quarter turns)
    scenery::scale_selection(turned_scale(local, scenery::selection_quarters()), is_uniform_scale(local));
}
V3 g_scale_at_field_start = {1.0f, 1.0f, 1.0f}; // (the Inspector's Scale fields: the scale when taken - its undo step)

// A change starting (a drag, the keys moving it) and ending: kept when the selection did move.
void begin_change()
{
    g_has_pending = have_selection();
    g_pending_forced = false;
    if (g_has_pending)
        g_pending = get_snapshot();
}
void end_change()
{
    if (!g_has_pending)
        return;
    g_has_pending = false;
    const V3 now = selection_pos();
    const bool changed = g_pending.kind == UndoStep::Kind::kNodes
                             ? node_edit::differs(g_pending.nodes, node_edit::get_take_in) // (moved or turned)
                             : g_pending.where.x != now.x || g_pending.where.y != now.y || g_pending.where.z != now.z;
    // (a turn or a scale by a drag - level_editor_transform.h: a step though the position may be where it was)
    if (changed || g_pending_forced)
        push_undo(g_pending, "Move");
    g_pending_forced = false;
}

// Copies made or taken away: shown / made again, or hidden / deleted; selected when they're there.
void set_exist(UndoStep* step, bool exist)
{
    if (!step->sectors.empty()) {
        std::vector<scenery::SectorState> states;
        for (const uint32_t checksum : step->sectors) {
            Placement placement = {0, {0.0f, 0.0f, 0.0f}};
            scenery::placement_now(checksum, &placement);
            states.push_back({checksum, placement, !exist});
        }
        scenery::apply_states(states, {});
        if (exist) {
            g_objects.clear();
            scenery::set_selection(step->sectors);
        } else {
            scenery::clear_selection();
        }
    }
    if (!step->copies.empty()) {
        std::vector<objects::GameEntity*> made;
        for (MadeCopy& copy : step->copies) {
            auto* object = core::down_cast<objects::GameEntity>(objects::find_object(copy.name));
            const bool there = object && !(object->object_flags_ & objects::kObjectFlagDead);
            if (exist && !there) {
                const V3 pos = {copy.state.pos.x, copy.state.pos.y, copy.state.pos.z};
                object = object_edit::make_copy(copy.source, pos, copy.name);
                if (object)
                    object_edit::get_restore(copy.state);
            } else if (!exist && there) {
                copy.state = object_edit::store_away(object);
                object_edit::remove_copy(copy.name);
                object = nullptr;
            }
            if (exist && object)
                made.push_back(object);
        }
        if (exist)
            select_objects(made);
        else
            g_objects.clear();
    }
}

// `step` put back; what it puts back is selected.
void get_restore(UndoStep& step)
{
    switch (step.kind) {
    case UndoStep::Kind::kObjects: {
        std::vector<objects::GameEntity*> objects;
        for (const object_edit::State& state : step.objects) {
            if (object_edit::get_restore(state))
                objects.push_back(core::down_cast<objects::GameEntity>(objects::find_object(state.id)));
        }
        select_objects(objects);
        break;
    }
    case UndoStep::Kind::kScenery:
        g_objects.clear();
        scenery::restore_state(step.scenery);
        // (what it carried: back where it was - level_editor_carry.h)
        for (const object_edit::State& state : step.carried_objects_)
            object_edit::get_restore(state);
        node_edit::place_in(step.carried_nodes_, [](uint32_t name, const node_edit::Values& values) {
            node_edit::Values now;
            if (node_edit::get_take_in(name, &now) && !node_edit::get_same(now, values))
                node_edit::assign_value(name, values);
        });
        break;
    case UndoStep::Kind::kLocks:
        picking::set_locks(step.locks);
        break;
    case UndoStep::Kind::kMade:
        set_exist(&step, step.exist);
        break;
    case UndoStep::Kind::kNodes: {
        node_edit::place_in(
            step.nodes, [](uint32_t name, const node_edit::Values& values) { node_edit::assign_value(name, values); });
        const int index = step.nodes.nodes.empty() ? -1 : nodes::get_find(step.nodes.nodes.front().first);
        if (index >= 0)
            select_node_only(index);
        break;
    }
    case UndoStep::Kind::kAdded: {
        std::string message;
        if (step.exist) {
            // (made again where they were in the overlay: lowest index first)
            std::vector<size_t> by_index(step.added.size());
            for (size_t i = 0; i < by_index.size(); ++i)
                core::at(by_index, i) = i;
            std::ranges::sort(by_index, [&](size_t a, size_t b) { return core::at(step.added_at_, a) < core::at(step.added_at_, b); });
            for (const size_t i : by_index)
                node_create::fabricate(core::at(step.added, i), core::at(step.added_at_, i), &message);
            const int index =
                step.added.empty() ? -1 : nodes::get_find(node_create::name_hash(step.added.front().name));
            if (index >= 0)
                select_node_only(index);
        } else {
            for (const node_create::Spec& spec : step.added) {
                node_create::Spec removed;
                int at = 0;
                node_create::drop_out(node_create::name_hash(spec.name), &removed, &at);
            }
            g_node_selected = false;
        }
        break;
    }
    case UndoStep::Kind::kHidden: {
        for (const uint32_t name : step.hidden_names_)
            node_create::conceal(name, step.hidden);
        const int index = step.hidden_names_.empty() ? -1 : nodes::get_find(step.hidden_names_.front());
        if (index >= 0)
            select_node_only(index);
        break;
    }
    case UndoStep::Kind::kOrder:
        node_create::set_order(step.order);
        break;
    case UndoStep::Kind::kLevel: {
        std::string message, before;
        g_objects.clear();
        scenery::clear_selection();
        g_node_selected = false;
        if (step.reset)
            layout::reset_level(&before, &message);
        else
            layout::put_back(step.layout, &message);
        break;
    }
    case UndoStep::Kind::kWalls:
        walls::apply_states(step.walls);
        if (step.wall) {
            unpick();
            view::select_wall(step.wall);
        } else if (!step.sectors.empty()) {
            g_objects.clear();
            g_node_selected = false;
            view::clear_wall();
            scenery::set_selection(step.sectors);
        }
        break;
    case UndoStep::Kind::kChains:
        g_node_selected = g_node_selected && nodes::get_find(g_node_name) >= 0;
        chains::change_phase(step.chains);
        if (g_node_selected && nodes::get_find(g_node_name) < 0)
            g_node_selected = false;
        break;

    case UndoStep::Kind::kFields: {
        for (const FieldUndo& f : step.fields)
            fields::place_in(f.node, f.key, f.own);
        const int index = step.fields.empty() ? -1 : nodes::get_find(step.fields.front().node);
        if (index >= 0 && g_node_name != step.fields.front().node)
            select_node_only(index);
        break;
    }
    case UndoStep::Kind::kProps:
        level_props::assign_value(step.props);
        break;
    case UndoStep::Kind::kTriggers:
        triggers::get_restore(step.triggers);
        break;
    case UndoStep::Kind::kEntities:
        entities::change_phase(step.entities);
        break;
    case UndoStep::Kind::kModule:
        if (step.module)
            step.module->get_restore();
        break;
    case UndoStep::Kind::kGroup:
        for (size_t i = step.group.size(); i-- > 0;)
            get_restore(core::at(step.group, i));
        break;
    }
}

// The same things as `step` as they are now (for the other stack).
UndoStep at_hand(const UndoStep& step)
{
    UndoStep now = step;
    switch (step.kind) {
    case UndoStep::Kind::kObjects:
        now.objects.clear();
        for (const object_edit::State& state : step.objects) {
            if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(state.id)))
                now.objects.push_back(object_edit::store_away(object));
        }
        break;
    case UndoStep::Kind::kScenery: {
        scenery::set_selection(step.scenery.selection);
        now.scenery = scenery::save_state();
        now.carried_objects_.clear();
        for (const object_edit::State& state : step.carried_objects_) {
            if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(state.id)))
                now.carried_objects_.push_back(object_edit::store_away(object));
        }
        std::vector<uint32_t> names;
        names.reserve(step.carried_nodes_.nodes.size());
        for (const auto& node : step.carried_nodes_.nodes)
            names.push_back(node.first);
        now.carried_nodes_ = node_edit::get_capture(names, node_edit::get_take_in);
        break;
    }
    case UndoStep::Kind::kLocks:
        now.locks = picking::get_locks();
        break;
    case UndoStep::Kind::kMade:
        now.exist = !step.exist;
        break;
    case UndoStep::Kind::kNodes: {
        std::vector<uint32_t> names;
        names.reserve(step.nodes.nodes.size());
        for (const auto& node : step.nodes.nodes)
            names.push_back(node.first);
        now.nodes = node_edit::get_capture(names, node_edit::get_take_in);
        break;
    }
    case UndoStep::Kind::kAdded: {
        now.exist = !step.exist;
        if (now.exist) {
            // (there now: as they are now - moved since - and where)
            node_create::LinkChain current = node_create::at_hand();
            for (size_t i = 0; i < now.added.size(); ++i) {
                const int at = node_create::get_index_of(current, core::at(now.added, i).name);
                if (at >= 0) {
                    core::at(now.added, i) = core::at(current, at);
                    core::at(now.added_at_, i) = at;
                }
            }
        }
        break;
    }
    case UndoStep::Kind::kHidden:
        now.hidden = !step.hidden;
        break;
    case UndoStep::Kind::kOrder:
        now.order = node_create::get_order();
        break;
    case UndoStep::Kind::kLevel:
        now.reset = !step.reset;
        now.layout.clear();
        if (!now.reset)
            layout::capture_text(&now.layout);
        break;
    case UndoStep::Kind::kChains:
        now.chains = chains::at_hand();
        break;
    case UndoStep::Kind::kFields:
        for (FieldUndo& f : now.fields)
            fields::get_capture(f.node, f.key, &f.own);
        break;
    case UndoStep::Kind::kProps:
        now.props = level_props::at_hand();
        break;
    case UndoStep::Kind::kWalls: {
        std::vector<uint32_t> checksums;
        checksums.reserve(step.walls.size());
        for (const walls::WallState& state : step.walls)
            checksums.push_back(state.checksum);
        now.walls = walls::store_away(checksums);
        break;
    }
    case UndoStep::Kind::kTriggers:
        now.triggers = triggers::store_away();
        break;
    case UndoStep::Kind::kEntities:
        now.entities = entities::at_hand();
        break;
    case UndoStep::Kind::kModule:
        now.module = step.module ? step.module->get_now() : nullptr;
        break;
    case UndoStep::Kind::kGroup:
        for (size_t i = 0; i < step.group.size(); ++i)
            core::at(now.group, i) = at_hand(core::at(step.group, i));
        break;
    }
    return now;
}

// Undo (`from` the undo stack, what it replaced onto redo) or redo (the other way).
void step_history(std::vector<UndoStep>* from, std::vector<UndoStep>* to, const char* what)
{
    if (from->empty()) {
        hud::get_message(from == &g_undo ? "Nothing to undo" : "Nothing to redo");
        return;
    }
    entities::stop_preview(); // (the previewed object back where it stands before anything is put back)
    UndoStep step = from->back();
    from->pop_back();
    carry::disarm(); // (the steps put places back themselves: nothing follows them)
    to->push_back(at_hand(step));
    to->back().serial = ++g_undo_serial;
    get_restore(step);
    mark_unsaved();
    note_action("%s", what);
    hud::get_message(what);
    if (debug_enabled())
        get_log("level editor: %s (%u to undo, %u to redo)", what, static_cast<unsigned>(g_undo.size()),
                static_cast<unsigned>(g_redo.size()));
}

// --- dragging ---
//
// Free: looking down on it, the selection follows the point under the cursor on the horizontal plane at its base;
// with the camera about level with that (a ray meeting it at under ~20 degrees), the mouse moves it in the view's
// terms instead - across for left / right, away / back along the ground for up / down, as far as a pixel spans at
// its distance. Shift: up / down only (an upright plane facing the camera). The wheel pushes it away / pulls it back;
// Ctrl snaps it to a 25-unit grid. Along an arrow: the point of that axis nearest the mouse ray. It moves only once
// the cursor has gone a few pixels from where it was pressed (a click doesn't nudge it).

V3 drag_plane_normal(bool vertical, const TickCycle& frame)
{
    if (!vertical)
        return {0.0f, 1.0f, 0.0f};
    const V3 flat = {frame.back.x, 0.0f, frame.back.z};
    if (get_dot(flat, flat) < 1e-6f)
        return frame.back;
    return make_unit(flat);
}

const std::array<V3, 3> kAxes = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};

// The gizmo's size: a fifth of the camera's distance from the selection (20 to 1500 units)
// (as the picture shows the distance - an ortho view's far eye scaled down: level_editor_camera.h; [editor] gizmo_pct)
float arrow_length()
{
    const float distance = view_cam::apparent(std::sqrt(get_dot(selection_pos() - g_pos, selection_pos() - g_pos)));
    const float length =
        std::max(20.0f, std::min(1500.0f, distance * 0.2f)) * static_cast<float>(prefs::get_pull().gizmo_pct_) / 100.0f;
    return view_cam::ortho() ? length * std::sqrt(get_dot(selection_pos() - g_pos, selection_pos() - g_pos)) /
                                   std::max(1.0f, distance)
                             : length;
}

// A world point in back-buffer pixels (false: behind the camera).
bool get_to_pixel(const TickCycle& frame, V3 point, float* px, float* py)
{
    float nx = 0.0f, ny = 0.0f;
    if (!cast_onto(g_pos, frame, g_lens, point, kNearZ, &nx, &ny))
        return false;
    ndc_to_pixel(g_viewport.data(), nx, ny, px, py);
    return true;
}

// --- the transform tools (level_editor_transform.h): Move / Rotate / Scale, World / Local, the pivot ---

// What the selection can do: a chain's point doesn't turn (a ladder turns whole with Z / X); only scenery and objects
// with a model scale.
bool selection_turns() { return !(picked_node_index() >= 0 && node_edit::is_chain(g_node_name)); }
bool selection_scales()
{
    if (picked_node_index() >= 0 || wall_only())
        return false;
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (objects.empty())
        return scenery::has_selection();
    return std::ranges::any_of(objects, scale_tool::object_scalable);
}
// The tool the gizmo shows: the toolbar's, or Move when the selection can't do that.
xform::Mode gizmo_mode()
{
    const xform::Mode mode = xform::at_hand().mode;
    if ((mode == xform::Mode::kSpinBy && !selection_turns()) || (mode == xform::Mode::kScale && !selection_scales()))
        return xform::Mode::kShiftBy;
    return mode;
}
// The gizmo's axes: World the world's; Local the first object's own (its matrix's rows), a node's heading, the scenery's
// quarter turns (its first piece's) - an invisible wall the world's.
void selection_axes(V3 out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = core::at(kAxes, i);
    if (xform::at_hand().space != xform::Space::kLocal)
        return;
    if (const objects::GameEntity* object = get_primary()) {
        std::array<V3, 3> rows{};
        for (int i = 0; i < 3; ++i)
            core::at(rows, i) = {core::at(object->basis_frame_.row, i).x, core::at(object->basis_frame_.row, i).y, core::at(object->basis_frame_.row, i).z};
        xform::unit_axes(rows.data(), out);
        return;
    }
    node_edit::Values node;
    if (node_values(&node)) {
        xform::heading_axes(node.angles.y, out);
        return;
    }
    if (!wall_only() && scenery::has_selection()) {
        const int quarters = scenery::selection_quarters();
        for (int i = 0; i < 3; ++i)
            out[i] = get_quarter_turn(core::at(kAxes, i), quarters);
    }
}
V3 axis_vector(int axis)
{
    std::array<V3, 3> axes{};
    selection_axes(axes.data());
    return core::at(axes, axis >= 0 && axis < 3 ? axis : 0);
}
// The selection's heading (the repeat of a duplicate): the first object's (its at row), a node's, the scenery's quarter
// turns.
float selection_heading()
{
    if (const objects::GameEntity* object = get_primary())
        return xform::get_heading_of(
            {core::at(object->basis_frame_.row, 2).x, core::at(object->basis_frame_.row, 2).y, core::at(object->basis_frame_.row, 2).z});
    node_edit::Values node;
    if (node_values(&node))
        return node.angles.y;
    return scenery::has_selection() ? static_cast<float>(scenery::selection_quarters()) * kPi * 0.5f : 0.0f;
}
// Where Rotate / Scale turn and scale about (the toolbar's Pivot): Centre the selection's middle; Base / Own the first
// object's position, a node's place, the scenery's pivot - for Scale the middle of the scenery's bottom (as [ / ]).
V3 transform_pivot(xform::Mode mode)
{
    if (xform::at_hand().pivot == xform::Pivot::kCentre)
        return selection_centre();
    V3 pos = selection_pos();
    if (mode == xform::Mode::kScale && !get_primary() && scenery::has_selection())
        return {pos.x, scenery::get_bottom(), pos.z};
    return pos;
}
xform::Gizmo gizmo_now()
{
    xform::Gizmo gizmo;
    gizmo.mode = g_dragging ? g_drag_mode : gizmo_mode();
    gizmo.pivot = gizmo.mode == xform::Mode::kShiftBy ? selection_pos()
                  : g_dragging                        ? g_transform_pivot
                                                      : transform_pivot(gizmo.mode);
    selection_axes(gizmo.axes.data());
    gizmo.length = arrow_length();
    gizmo.rotates = selection_turns();
    return gizmo;
}

// Turned to `target` radians from where the drag started: objects about the Pivot, a node's heading, scenery and
// invisible walls by whole quarter turns (the engine turns level geometry by quarters - level_editor_scenery.h).
void apply_turn_to(float target)
{
    const std::vector<objects::GameEntity*> objects = selected_objects();
    node_edit::Values node;
    if (!objects.empty()) {
        const float delta = target - g_rot_applied;
        if (std::fabs(delta) < 1e-6f)
            return;
        turn_objects_by(objects, delta, g_transform_pivot);
    } else if (node_values(&node)) {
        const float delta = target - g_rot_applied;
        if (std::fabs(delta) < 1e-6f)
            return;
        node.angles =
            node_edit::with_heading(node.angles, node_edit::get_heading_degrees(node.angles) + delta * 180.0f / kPi);
        set_selected_node(node);
    } else {
        const int quarters = xform::get_quarters(target);
        if (quarters == g_quarters_applied)
            return;
        turn_selection(quarters - g_quarters_applied);
        g_quarters_applied = quarters;
    }
    g_rot_applied = target;
    g_pending_forced = true;
}

// The scale made since the drag started, along the gizmo's axes: the selection's scale (along its own axes) over its
// scale then - an object's own axis nearest each world axis, the scenery's turned (odd quarter turns swap x and z).
V3 scale_applied_now()
{
    const V3 now = selection_scale_now();
    V3 r = {now.x / g_scale_start.x, now.y / g_scale_start.y, now.z / g_scale_start.z};
    if (xform::at_hand().space == xform::Space::kLocal)
        return r;
    if (const objects::GameEntity* object = get_primary()) {
        std::array<V3, 3> rows{};
        for (int i = 0; i < 3; ++i)
            core::at(rows, i) = {core::at(object->basis_frame_.row, i).x, core::at(object->basis_frame_.row, i).y, core::at(object->basis_frame_.row, i).z};
        const std::array<float, 3> own = {{r.x, r.y, r.z}};
        return {core::at(own, scale_tool::nearest_axis(rows.data(), 0)), core::at(own, scale_tool::nearest_axis(rows.data(), 1)),
                core::at(own, scale_tool::nearest_axis(rows.data(), 2))};
    }
    return turned_scale(r, scenery::selection_quarters());
}

// Scaled to `wanted` (along the gizmo's axes) from where the drag started (within the limits - level_editor_scale.h):
// objects each about its position (Centre: their places spread from the middle too), scenery about the middle of its
// bottom (Centre: about its middle).
void apply_scale_to(V3 wanted, bool uniform)
{
    const V3 applied = scale_applied_now();
    const V3 ratio = {wanted.x / applied.x, wanted.y / applied.y, wanted.z / applied.z};
    if (std::fabs(ratio.x - 1.0f) < 1e-4f && std::fabs(ratio.y - 1.0f) < 1e-4f && std::fabs(ratio.z - 1.0f) < 1e-4f)
        return;
    const bool local = xform::at_hand().space == xform::Space::kLocal;
    const bool centre = xform::at_hand().pivot == xform::Pivot::kCentre;
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        std::vector<V3> before;
        before.reserve(objects.size());
        for (const objects::GameEntity* object : objects)
            before.push_back(object_edit::pos_of(object));
        if (local) {
            for (objects::GameEntity* object : objects) {
                if (!scale_tool::object_scalable(object))
                    continue;
                const V3 now = scale_tool::get_object_scale(object);
                const V3 allowed =
                    scale_tool::allowed_factor(ratio, {now}, {scale_tool::object_limit(object)}, uniform);
                if (!is_unit_scale(allowed))
                    scale_tool::set_object_scale(object, per_axis(now, allowed));
            }
        } else {
            scale_objects(objects, ratio, uniform);
        }
        if (centre) {
            std::array<V3, 3> axes{};
            selection_axes(axes.data());
            const std::array<float, 3> r = {{ratio.x, ratio.y, ratio.z}};
            for (size_t i = 0; i < objects.size(); ++i) {
                const V3 d = core::at(before, i) - g_transform_pivot;
                V3 placed = g_transform_pivot;
                for (int a = 0; a < 3; ++a)
                    placed = placed + core::at(axes, a) * (get_dot(d, core::at(axes, a)) * core::at(r, a));
                object_edit::get_place(core::at(objects, i), placed);
            }
        }
    } else {
        const V3 centre_before = selection_centre();
        scenery::scale_selection(local ? turned_scale(ratio, scenery::selection_quarters()) : ratio, uniform);
        if (centre)
            scenery::move_to(scenery::get_pivot() + (centre_before - selection_centre()));
        follow_scenery();
    }
    g_pending_forced = true;
}

// A Rotate / Scale drag starting (on a handle, or Alt+G's modal one): its pivot; the cursor's angle about it (on the
// level plane through it when looking down on it, else on the screen - which way round from two of the ring's points),
// or its distance from it (a handle's axis, else in pixels from the pivot's pixel).
void start_transform_drag(V3 origin, V3 dir, const TickCycle& frame)
{
    g_transform_pivot = transform_pivot(g_drag_mode);
    if (g_drag_mode == xform::Mode::kSpinBy) {
        g_rot_total = g_rot_applied = 0.0f;
        g_quarters_applied = 0;
        g_rot_sign = 1.0f;
        g_rot_last = 0.0f;
        float t = 0.0f;
        g_rot_by_plane = std::fabs(make_unit(dir).y) > 0.25f &&
                         ray_plane(origin, dir, g_transform_pivot, {0.0f, 1.0f, 0.0f}, &t) && t < kMaxPickDistance;
        if (g_rot_by_plane) {
            g_rot_last = xform::turn_around_axis(g_transform_pivot, origin + dir * t);
            return;
        }
        float px = 0.0f, py = 0.0f, ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f, r = arrow_length();
        if (!get_to_pixel(frame, g_transform_pivot, &px, &py))
            return;
        g_rot_last = std::atan2(g_cursor_x - px, g_cursor_y - py);
        if (get_to_pixel(frame, g_transform_pivot + V3{0.0f, 0.0f, r}, &ax, &ay) &&
            get_to_pixel(frame, g_transform_pivot + V3{r, 0.0f, 0.0f}, &bx, &by))
            g_rot_sign =
                xform::get_wrap(std::atan2(bx - px, by - py) - std::atan2(ax - px, ay - py)) < 0.0f ? -1.0f : 1.0f;
        return;
    }
    g_scale_start = selection_scale_now();
    g_scale_s0 = 0.0f;
    if (g_axis >= 0 && g_axis < 3) {
        closest_on_axis(g_transform_pivot, axis_vector(g_axis), origin, dir, &g_scale_s0);
    } else {
        float px = 0.0f, py = 0.0f;
        if (get_to_pixel(frame, g_transform_pivot, &px, &py))
            g_scale_s0 = std::hypot(g_cursor_x - px, g_cursor_y - py);
    }
}

void rotate_drag(V3 origin, V3 dir, const TickCycle& frame, bool snap)
{
    float typed = 0.0f, target = 0.0f;
    if (xform::typed_value(g_typed, &typed)) {
        target = typed * kPi / 180.0f;
    } else {
        float angle = 0.0f;
        bool have = false;
        float t = 0.0f, px = 0.0f, py = 0.0f;
        if (g_rot_by_plane) {
            if (ray_plane(origin, dir, g_transform_pivot, {0.0f, 1.0f, 0.0f}, &t) && t < kMaxPickDistance) {
                angle = xform::turn_around_axis(g_transform_pivot, origin + dir * t);
                have = true;
            }
        } else if (get_to_pixel(frame, g_transform_pivot, &px, &py) &&
                   std::hypot(g_cursor_x - px, g_cursor_y - py) > 2.0f) {
            angle = std::atan2(g_cursor_x - px, g_cursor_y - py);
            have = true;
        }
        if (have) {
            const float delta = xform::get_wrap(angle - g_rot_last);
            g_rot_last = angle;
            g_rot_total += g_rot_by_plane ? delta : delta * g_rot_sign;
        }
        target = snap ? xform::snap_angle(g_rot_total, xform::angle_step()) : g_rot_total;
    }
    apply_turn_to(target);
}

void scale_drag(V3 origin, V3 dir, const TickCycle& frame, bool snap)
{
    const int axis = g_typed.axis >= 0 ? g_typed.axis : g_axis >= 0 && g_axis < 3 ? g_axis : -1;
    float factor = 1.0f, typed = 0.0f;
    if (xform::typed_value(g_typed, &typed)) {
        if (typed <= 0.0f)
            return;
        factor = typed;
    } else {
        float s = 0.0f, px = 0.0f, py = 0.0f;
        if (g_axis >= 0 && g_axis < 3) {
            if (!closest_on_axis(g_transform_pivot, axis_vector(g_axis), origin, dir, &s))
                return;
            factor = xform::factor_along(g_scale_s0, s);
        } else {
            if (!get_to_pixel(frame, g_transform_pivot, &px, &py))
                return;
            factor = xform::factor_from_pixels(g_scale_s0, std::hypot(g_cursor_x - px, g_cursor_y - py));
        }
        if (snap)
            factor = xform::snap_factor(factor);
    }
    const V3 wanted = axis < 0 ? V3{factor, factor, factor}
                               : V3{axis == 0 ? factor : 1.0f, axis == 1 ? factor : 1.0f, axis == 2 ? factor : 1.0f};
    apply_scale_to(wanted, axis < 0);
}

// --- dragging (Move) ---

void start_drag(V3 origin, V3 dir, bool vertical, const TickCycle& frame, float mouse_x, float mouse_y)
{
    if (g_drag_mode != xform::Mode::kShiftBy) {
        start_transform_drag(origin, dir, frame);
        return;
    }
    const V3 pos = selection_pos();
    g_drag_vertical = vertical;
    g_drag_from = pos;
    g_drag_mouse_x = mouse_x;
    g_drag_mouse_y = mouse_y;
    g_drag_push = 0.0f;
    if (g_axis >= 0) {
        g_axis_t0 = 0.0f;
        closest_on_axis(pos, axis_vector(g_axis), origin, dir, &g_axis_t0);
        return;
    }
    g_drag_plane_normal = drag_plane_normal(vertical, frame);
    g_drag_plane_offset = vertical ? V3{0.0f, 0.0f, 0.0f} : V3{0.0f, get_selection_base() - pos.y, 0.0f};
    g_drag_right = make_unit({frame.right.x, 0.0f, frame.right.z});
    g_drag_forward = make_unit({-frame.back.x, 0.0f, -frame.back.z});
    const float depth = std::max(50.0f, -get_dot(pos - origin, frame.back));
    g_drag_units = 2.0f * depth * g_lens.tan_x_ / static_cast<float>(std::max(1, g_mouse.window_width_));
    float t = 0.0f;
    g_drag_screen = !vertical && (std::fabs(make_unit(dir).y) < 0.34f ||
                                  !ray_plane(origin, dir, pos + g_drag_plane_offset, g_drag_plane_normal, &t));
    const V3 point = pos + g_drag_plane_offset;
    g_drag_offset = !g_drag_screen && ray_plane(origin, dir, point, g_drag_plane_normal, &t)
                        ? point - (origin + dir * t)
                        : V3{0.0f, 0.0f, 0.0f};
    g_drag_reach = std::max(2000.0f, 3.0f * t);
    g_drag_offset = g_drag_offset - g_drag_plane_offset;
}

// What a drag snaps to (level_editor_snap.h): held keys, or the toolbar's Snap and its Snap to.
struct Snapping {
    bool on = false;
    xform::SnapTarget target = xform::SnapTarget::kGrid;
};
Snapping snapping_now(bool ctrl, bool shift, bool alt)
{
    Snapping s;
    s.on = true;
    if (ctrl && alt)
        s.target = xform::SnapTarget::kVertex;
    else if (alt && shift)
        s.target = xform::SnapTarget::kSurface;
    else if (ctrl && shift)
        s.target = xform::SnapTarget::kWorldGrid;
    else if (ctrl)
        s.target = xform::SnapTarget::kGrid;
    else if (g_snap_always)
        s.target = xform::at_hand().target;
    else
        s.on = false;
    return s;
}

// The selection's box corners (vertex snapping's own corner): its objects' model boxes, its scenery's boxes, else its
// place.
std::vector<V3> selection_corners()
{
    std::vector<V3> out;
    OrientedBox box{};
    std::array<V3, 8> corners{};
    auto add = [&](const OrientedBox& b) {
        box_corners(b.origin, b.axes.data(), b.min, b.max, corners.data());
        out.insert(out.end(), corners.data(), corners.data() + 8);
    };
    const std::vector<objects::GameEntity*> objects = selected_objects();
    for (objects::GameEntity* object : objects) {
        if (object_edit::model_box(object, &box))
            add(box);
    }
    if (objects.empty() && picked_node_index() < 0 && !wall_only()) {
        for (const uint32_t checksum : scenery::get_selection()) {
            if (scenery::cuboid(checksum, &box))
                add(box);
        }
    }
    if (out.empty())
        out.push_back(selection_pos());
    return out;
}

// Surface snapping: the selection's base onto what the collision ray meets first under the cursor (its own pieces
// passed); an object stood square to the slope with the toolbar's Align.
void surface_drag(V3 origin, V3 dir)
{
    std::vector<uint32_t> skip;
    if (!get_primary() && picked_node_index() < 0) {
        skip = scenery::get_selection();
        if (const uint32_t wall = wall_only())
            skip.push_back(wall);
    }
    snap::SurfaceHit hit;
    if (!snap::get_surface(origin, make_unit(dir), kMaxPickDistance, skip, &hit))
        return;
    const V3 pos = selection_pos();
    const V3 wanted = snap::on_surface(pos, get_selection_base(), hit.point);
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
        place_selection(wanted);
    if (!xform::at_hand().align_to_slope_)
        return;
    for (objects::GameEntity* object : selected_objects()) {
        for (maths::Mat4* m : {&object->basis_frame_, &object->display_matrix_}) {
            std::array<V3, 3> rows{};
            for (int i = 0; i < 3; ++i)
                core::at(rows, i) = {core::at(m->row, i).x, core::at(m->row, i).y, core::at(m->row, i).z};
            if (!snap::align_rows(rows.data(), hit.normal))
                continue;
            for (int i = 0; i < 3; ++i) {
                core::at(m->row, i).x = core::at(rows, i).x;
                core::at(m->row, i).y = core::at(rows, i).y;
                core::at(m->row, i).z = core::at(rows, i).z;
            }
            g_pending_forced = true;
        }
    }
}

// Vertex snapping: the selection's corner nearest the cursor (chosen when it starts) onto the nearest vertex of what's
// under the cursor - an object's box corner, or a scenery piece's drawn vertex (the selection's own passed).
void vertex_drag(V3 origin, V3 dir, const TickCycle& frame)
{
    const V3 unit = make_unit(dir);
    const V3 pos = selection_pos();
    if (!g_vertex_have) {
        std::vector<V3> corners = selection_corners();
        const int i = snap::nearest_on_screen(
            corners, [&](V3 p, float* x, float* y) { return get_to_pixel(frame, p, x, y); }, g_cursor_x, g_cursor_y);
        if (i < 0)
            return;
        g_vertex_corner = core::at(corners, i) - pos;
        g_vertex_have = true;
    }
    std::vector<V3> targets;
    V3 at = {};
    float reach = kMaxPickDistance;
    for (const auto& picked : object_edit::pick_all(origin, unit, kMaxPickDistance)) {
        if (is_picked(picked.first))
            continue;
        OrientedBox box{};
        if (object_edit::model_box(picked.first, &box)) {
            std::array<V3, 8> corners{};
            box_corners(box.origin, box.axes.data(), box.min, box.max, corners.data());
            targets.assign(corners.data(), corners.data() + 8);
            at = origin + unit * picked.second;
            reach = picked.second;
        }
        break;
    }
    scenery::Hit hit;
    std::vector<scenery::Layer> layers;
    scenery::get_pick(origin, unit, reach, &hit, &layers);
    for (const scenery::Layer& layer : layers) {
        if (scenery::get_selected(layer.checksum) || layer.t >= reach)
            continue;
        std::vector<V3> vertices = snap::sector_vertices(layer.checksum, 65536);
        if (!vertices.empty()) {
            targets = std::move(vertices);
            at = origin + unit * layer.t;
        }
        break;
    }
    const int nearest = snap::get_nearest(targets, at);
    if (nearest < 0)
        return;
    const V3 wanted = core::at(targets, nearest) - g_vertex_corner;
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
        place_selection(wanted);
}

void get_drag(V3 origin, V3 dir, const TickCycle& frame, float mouse_x, float mouse_y, Snapping snap)
{
    if (g_drag_mode == xform::Mode::kSpinBy) {
        rotate_drag(origin, dir, frame, snap.on);
        return;
    }
    if (g_drag_mode == xform::Mode::kScale) {
        scale_drag(origin, dir, frame, snap.on);
        return;
    }
    const V3 pos = selection_pos();
    // (a typed amount: that far along the typed axis - else the arrow's, else X - from where the drag started)
    float typed = 0.0f;
    if (xform::typed_value(g_typed, &typed)) {
        const int axis = g_typed.axis >= 0 ? g_typed.axis : g_axis >= 0 ? g_axis : 0;
        const V3 wanted = g_drag_start + axis_vector(axis) * typed;
        if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
            place_selection(wanted);
        return;
    }
    if (!snap.on || snap.target != xform::SnapTarget::kVertex)
        g_vertex_have = false;
    if (snap.on && snap.target == xform::SnapTarget::kSurface) {
        surface_drag(origin, dir);
        return;
    }
    if (snap.on && snap.target == xform::SnapTarget::kVertex) {
        vertex_drag(origin, dir, frame);
        return;
    }
    // Grid snapping: relative (whole steps from where the drag started), or the world's grid (absolute).
    const bool absolute = snap.target == xform::SnapTarget::kWorldGrid;
    float grid = xform::get_grid();
    auto relative = [&](float d) { return xform::snap_relative(d, grid); };
    V3 wanted{};
    if (g_axis >= 0) {
        const V3 u = axis_vector(g_axis);
        float s = 0.0f;
        if (!closest_on_axis(g_drag_from, u, origin, dir, &s))
            return;
        const float along = s - g_axis_t0;
        if (std::fabs(along) > g_orbit_distance * 20.0f)
            return; // (the ray nearly along the axis: far off)
        wanted = g_drag_from + u * along;
        if (snap.on && absolute && xform::at_hand().space == xform::Space::kWorld) {
            float* c = g_axis == 0 ? &wanted.x : g_axis == 1 ? &wanted.y : &wanted.z;
            *c = snap_to_grid(*c, grid);
        } else if (snap.on) {
            const float total = get_dot(wanted - g_drag_start, u);
            wanted = wanted + u * (relative(total) - total);
        }
    } else if (g_drag_screen) {
        // (away for the cursor going up: 2.5 times a pixel's span, the ground being seen end-on)
        const float right = (mouse_x - g_drag_mouse_x) * g_drag_units;
        const float away = (g_drag_mouse_y - mouse_y) * g_drag_units * 2.5f;
        wanted = g_drag_from + g_drag_right * right + g_drag_forward * away;
        wanted.y = pos.y;
    } else {
        float t = 0.0f;
        if (!ray_plane(origin, dir, pos + g_drag_plane_offset, g_drag_plane_normal, &t) || t > g_drag_reach)
            return; // (towards the horizon)
        wanted = origin + dir * t + g_drag_offset;
        if (g_drag_vertical)
            wanted = {pos.x, wanted.y, pos.z};
        else
            wanted.y = pos.y;
    }
    if (g_axis < 0) {
        if (!g_drag_vertical)
            wanted = wanted + g_drag_forward * g_drag_push;
        if (snap.on && absolute) {
            wanted = g_drag_vertical ? V3{wanted.x, snap_to_grid(wanted.y, grid), wanted.z}
                                     : V3{snap_to_grid(wanted.x, grid), wanted.y, snap_to_grid(wanted.z, grid)};
        } else if (snap.on) {
            V3 d = wanted - g_drag_start;
            d = g_drag_vertical ? V3{d.x, relative(d.y), d.z} : V3{relative(d.x), d.y, relative(d.z)};
            wanted = g_drag_start + d;
        }
    }
    // (no snapping held: a scenery piece's box faces flush with its neighbours' - level_editor_arrange.h)
    if (!snap.on && !get_primary() && picked_node_index() < 0 && !wall_only()) {
        const int axes = g_axis >= 0       ? (xform::at_hand().space == xform::Space::kWorld ? 1 << g_axis : 0)
                         : g_drag_vertical ? 2
                                           : 5;
        if (axes)
            wanted = arrange::edge_snapped(pos, wanted, axes);
    }
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
        place_selection(wanted);
}

// A move, turn or scale made (its undo step just pushed): the last action for its panel (level_editor_adjust.h).
void note_for_adjust(xform::Mode mode, V3 moved, bool uniform)
{
    adjust::ActStep action;
    if (mode == xform::Mode::kSpinBy) {
        action.kind = adjust::Kind::kTurn;
        action.quarters = !get_primary() && picked_node_index() < 0;
        action.degrees =
            action.quarters ? static_cast<float>(g_quarters_applied) * 90.0f : g_rot_applied * 180.0f / kPi;
    } else if (mode == xform::Mode::kScale) {
        action.kind = adjust::Kind::kScale;
        action.factor = scale_applied_now();
        action.uniform = uniform;
    } else {
        action.kind = adjust::Kind::kShiftBy;
        action.offset = moved;
    }
    adjust::get_note(action);
}

void end_drag()
{
    if (!g_dragging)
        return;
    g_dragging = false;
    const bool uniform = g_axis < 0 && g_typed.axis < 0;
    g_axis = -1;
    const uint64_t serial = g_undo.empty() ? 0 : g_undo.back().serial;
    end_change();
    get_settle();
    if (!g_undo.empty() && g_undo.back().serial != serial)
        note_for_adjust(g_drag_mode, selection_pos() - g_drag_start, uniform);
    const V3 at = selection_pos();
    note_action("%s to (%.0f, %.0f, %.0f): %s",
                g_drag_mode == xform::Mode::kSpinBy  ? "turned"
                : g_drag_mode == xform::Mode::kScale ? "scaled"
                                                     : "dragged",
                at.x, at.y, at.z, describe_selection().c_str());
    g_drag_mode = xform::Mode::kShiftBy;
    g_modal = false;
    g_typed = {};
    g_vertex_have = false;
}

void set_relative(bool on)
{
    if (on == g_relative)
        return;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    g_relative = on;
}

// --- the gizmo's handles ---
//
// Move: three arrows from the selection's position along x (red), y (green) and z (blue) - the world's or, Local, the
// selection's own; Rotate: the heading ring about the pivot; Scale: a cube at each axis' end and one at the pivot. A
// fifth of the camera's distance long; the handle under the cursor (within 8 pixels) lights up, and dragging it moves
// the selection along that axis only, turns it or scales it.

// The handle under the back-buffer pixel (x, y): -1 for none.
int arrow_at(const TickCycle& frame, float x, float y)
{
    return xform::handle_at(
        gizmo_now(), [&](V3 p, float* px, float* py) { return get_to_pixel(frame, p, px, py); }, x, y, kArrowGrab);
}

// --- drawing (the renderer's cached state, then the device directly) ---

uint32_t get_stage_state(int type)
{
    DWORD value = 0;
    render::g_d3d_device->GetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(type), &value);
    return value;
}
void set_stage_state(int type, uint32_t value)
{
    render::g_d3d_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

struct LineVertex {
    float x, y, z, rhw;
    uint32_t color;
    float u, v;
};

void add_pixel_line(float ax, float ay, float bx, float by, uint32_t color, std::vector<LineVertex>* out)
{
    out->push_back({ax, ay, 0.0f, 1.0f, color, 0.0f, 0.0f});
    out->push_back({bx, by, 0.0f, 1.0f, color, 0.0f, 0.0f});
}

// A world line; `thickness` 3: drawn three times a pixel apart across it.
void add_line(V3 a, V3 b, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out, int thickness = 1)
{
    if (!clip_to_near(g_pos, frame, kNearZ + 0.01f, &a, &b))
        return;
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    if (!get_to_pixel(frame, a, &ax, &ay) || !get_to_pixel(frame, b, &bx, &by))
        return;
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float nx = length > 0.0f ? -dy / length : 0.0f;
    const float ny = length > 0.0f ? dx / length : 0.0f;
    for (int k = -(thickness / 2); k <= thickness / 2; ++k)
        add_pixel_line(ax + nx * static_cast<float>(k), ay + ny * static_cast<float>(k),
                       bx + nx * static_cast<float>(k), by + ny * static_cast<float>(k), color, out);
}

void add_box(const OrientedBox& box, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out)
{
    std::array<V3, 8> corners{};
    box_corners(box.origin, box.axes.data(), box.min, box.max, corners.data());
    for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit < 8; bit <<= 1) {
            if (!(i & bit))
                add_line(core::at(corners, i), core::at(corners, i | bit), frame, color, out);
        }
    }
}

// An arrow: its shaft (three pixels wide) and a head of two strokes 12 pixels long.
void add_arrow(V3 from, V3 to, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out)
{
    add_line(from, to, frame, color, out, 3);
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    if (!get_to_pixel(frame, from, &ax, &ay) || !get_to_pixel(frame, to, &bx, &by))
        return;
    float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
        return;
    dx /= length;
    dy /= length;
    for (const float side : {-1.0f, 1.0f}) {
        const float hx = -dx * 0.9f + -dy * 0.45f * side;
        const float hy = -dy * 0.9f + dx * 0.45f * side;
        add_pixel_line(bx, by, bx + hx * 12.0f, by + hy * 12.0f, color, out);
    }
}

// --- the nodes' gizmos (level_editor_nodes.h): drawn through the UI's overlay, picked by screen distance ---
//
// A point (spawns, crowns, flags, sounds, particles, objects, others) is an icon in its colour - a spawn with its
// heading -, a chain (rails, ladders, paths) lines along its links, a volume (lights, proximity nodes) an icon with its
// radius as rings when it's near, hovered or selected. Only what's in front of the camera, within a distance (6000 for
// points, 4000 for chains, 2500 for objects and particles) unless selected.

// Back buffer pixels per pixel at 720 lines (the icons' size).
float pixel_scale()
{
    const float gizmo = static_cast<float>(prefs::get_pull().gizmo_pct_) / 100.0f; // ([editor] gizmo_pct)
    return (core::at(g_viewport, 3) > 0 ? static_cast<float>(core::at(g_viewport, 3)) / 720.0f : 1.0f) * gizmo;
}

constexpr uint32_t kSelectedGizmo = 0xFFFFFFFF, kHoverGizmo = 0xFFFFE070;

void add_node_gizmos(const TickCycle& frame, int selected)
{
    g_pickables.clear();
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    const float k = pixel_scale();
    ui::Overlay& o = g_overlay;
    auto line = [&](float ax, float ay, float bx, float by, uint32_t c, float t) {
        o.lines.push_back({ax, ay, bx, by, c, t});
    };
    auto world_line = [&](V3 a, V3 b, uint32_t c, float t) {
        if (!clip_to_near(g_pos, frame, kNearZ + 0.01f, &a, &b))
            return;
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (get_to_pixel(frame, a, &ax, &ay) && get_to_pixel(frame, b, &bx, &by))
            line(ax, ay, bx, by, c, t);
    };
    // A ring about `centre` in the plane of `u` and `v` (unit, at right angles).
    auto ring = [&](V3 centre, float radius, V3 u, V3 v, uint32_t c) {
        constexpr int kSides = 32;
        V3 previous = centre + u * radius;
        for (int i = 1; i <= kSides; ++i) {
            const float angle = 2.0f * kPi * static_cast<float>(i) / kSides;
            const V3 next = centre + u * (radius * std::cos(angle)) + v * (radius * std::sin(angle));
            world_line(previous, next, c, 1.0f);
            previous = next;
        }
    };
    const V3 x_axis = {1.0f, 0.0f, 0.0f}, y_axis = {0.0f, 1.0f, 0.0f}, z_axis = {0.0f, 0.0f, 1.0f};
    const float left = static_cast<float>(core::at(g_viewport, 0)) - 40.0f;
    const float top = static_cast<float>(core::at(g_viewport, 1)) - 40.0f;
    const float right = static_cast<float>(core::at(g_viewport, 0) + core::at(g_viewport, 2)) + 40.0f;
    const float bottom = static_cast<float>(core::at(g_viewport, 1) + core::at(g_viewport, 3)) + 40.0f;
    for (int i = 0; std::cmp_less(i, nodes.size()); ++i) {
        const nodes::Elem& n = core::at(nodes, i);
        if (!n.has_pos_ || !(g_families & nodes::get_bit(n.family)) || organize::gizmo_hidden(n.name))
            continue;
        const nodes::Shape shape = nodes::shape_of(n.family);
        if (shape == nodes::Shape::kNone)
            continue;
        const bool is_selected = i == selected;
        const bool is_hovered = i == g_hover_node;
        const V3 d = n.pos - g_pos;
        const float distance = view_cam::apparent(std::sqrt(get_dot(d, d))); // (an ortho view: level_editor_camera.h)
        if (shape == nodes::Shape::kChain) {
            constexpr float kChainCutoff = 4000.0f;
            for (const int link : n.links) {
                const nodes::Elem& m = core::at(nodes, link);
                if (!m.has_pos_)
                    continue;
                const V3 e = m.pos - g_pos;
                if (!is_selected && !is_hovered && distance > kChainCutoff &&
                    view_cam::apparent(std::sqrt(get_dot(e, e))) > kChainCutoff)
                    continue;
                V3 a = n.pos, b = m.pos;
                if (!clip_to_near(g_pos, frame, kNearZ + 0.01f, &a, &b))
                    continue;
                float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
                if (!get_to_pixel(frame, a, &ax, &ay) || !get_to_pixel(frame, b, &bx, &by))
                    continue;
                if ((ax < left && bx < left) || (ax > right && bx > right) || (ay < top && by < top) ||
                    (ay > bottom && by > bottom))
                    continue;
                const uint32_t c = is_selected ? kSelectedGizmo : is_hovered ? kHoverGizmo : n.colour;
                const float t = is_selected || is_hovered ? 3.0f : n.family == nodes::Family::kRail ? 2.0f : 1.5f;
                if (n.family == nodes::Family::kLadder) {
                    for (int piece = 0; piece < 8; piece += 2) // (dashed)
                        line(ax + (bx - ax) * static_cast<float>(piece) / 8.0f,
                             ay + (by - ay) * static_cast<float>(piece) / 8.0f,
                             ax + (bx - ax) * static_cast<float>(piece + 1) / 8.0f,
                             ay + (by - ay) * static_cast<float>(piece + 1) / 8.0f, c, t);
                } else {
                    line(ax, ay, bx, by, c, t);
                }
                if (n.family == nodes::Family::kRouteWay) {
                    // (the way it goes: an arrowhead at the middle)
                    float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
                    if (length > 30.0f * k) {
                        dx /= length;
                        dy /= length;
                        const float mx = (ax + bx) * 0.5f;
                        const float my = (ay + by) * 0.5f;
                        const float h = 7.0f * k;
                        o.triangles.push_back({mx + dx * h, my + dy * h, mx - dx * h - dy * h * 0.6f,
                                               my - dy * h + dx * h * 0.6f, mx - dx * h + dy * h * 0.6f,
                                               my - dy * h - dx * h * 0.6f, c});
                    }
                }
                const V3 middle = (a + b) * 0.5f;
                g_pickables.push_back({i, ax, ay, bx, by, true, -get_dot(middle - g_pos, frame.back),
                                       -get_dot(a - g_pos, frame.back), -get_dot(b - g_pos, frame.back)});
            }
            if (is_selected || is_hovered) {
                float px = 0.0f, py = 0.0f;
                if (get_to_pixel(frame, n.pos, &px, &py)) {
                    o.circles.push_back({px, py, 4.0f * k, is_selected ? kSelectedGizmo : kHoverGizmo, true, 1.0f});
                    o.texts.push_back({px + 8.0f * k, py - 8.0f * k, 0xFFFFFFFF, nodes::caption(n.name)});
                }
            }
            if (n.family == nodes::Family::kRail && (distance < 1500.0f || is_selected)) {
                // (a rail's points, near: a dot each, picked before its segments - a point moves by itself or with
                // its whole rail)
                float px = 0.0f, py = 0.0f;
                const V3 front = n.pos - g_pos;
                if (-get_dot(front, frame.back) > kNearZ && get_to_pixel(frame, n.pos, &px, &py) && px >= left &&
                    px <= right && py >= top && py <= bottom) {
                    if (!is_selected && !is_hovered)
                        o.circles.push_back({px, py, 2.5f * k, n.colour, true, 1.0f});
                    g_pickables.push_back({i, px, py, px, py, false, -get_dot(front, frame.back)});
                }
            }
            continue;
        }
        const float cutoff =
            n.family == nodes::Family::kObject || n.family == nodes::Family::kParticle ? 2500.0f : 6000.0f;
        if (!is_selected && !is_hovered && distance > cutoff)
            continue;
        float px = 0.0f, py = 0.0f;
        if (!get_to_pixel(frame, n.pos, &px, &py) || px < left || px > right || py < top || py > bottom)
            continue;
        const float depth = -get_dot(d, frame.back);
        const float r = 6.0f * k;
        const uint32_t c = n.hidden ? 0x90A0A4A8 : n.colour; // (hidden: greyed)
        const uint32_t outline = 0xC0101012;
        switch (n.family) {
        case nodes::Family::kSpawn: {
            // A disc and its heading (an arrow along the ground).
            const float length = std::max(30.0f, std::min(200.0f, depth * 0.08f));
            const V3 tip = n.pos + n.forward * length;
            float tx = 0.0f, ty = 0.0f;
            if (get_to_pixel(frame, tip, &tx, &ty)) {
                line(px, py, tx, ty, c, 2.5f);
                float dx = tx - px, dy = ty - py, l = std::sqrt(dx * dx + dy * dy);
                if (l > 4.0f) {
                    dx /= l;
                    dy /= l;
                    const float h = 8.0f * k;
                    o.triangles.push_back({tx + dx * h * 0.5f, ty + dy * h * 0.5f, tx - dx * h - dy * h * 0.55f,
                                           ty - dy * h + dx * h * 0.55f, tx - dx * h + dy * h * 0.55f,
                                           ty - dy * h - dx * h * 0.55f, c});
                }
            }
            o.circles.push_back({px, py, r + 1.5f * k, outline, true, 1.0f});
            o.circles.push_back({px, py, r, c, true, 1.0f});
            if (n.restart_types_ & nodes::kHorse) // (a HORSE spot: restart_types has Horse - the feature map)
                o.texts.push_back({px + r + 3.0f * k, py - r - 12.0f * k, 0xFFF0943C, "H"});
            break;
        }
        case nodes::Family::kCrown: {
            // A gold diamond and its index.
            o.triangles.push_back({px, py - r * 1.4f, px + r, py, px - r, py, c});
            o.triangles.push_back({px - r, py, px + r, py, px, py + r * 1.4f, c});
            std::array<char, 16> text{};
            if (n.order >= 0)
                std::snprintf(text.data(), sizeof(text), "#%d", n.order);
            else
                std::snprintf(text.data(), sizeof(text), "hidden");
            o.texts.push_back({px + r + 4.0f * k, py - r * 1.4f, c, text.data()});
            break;
        }
        case nodes::Family::kBitMark:
            // A pole and a pennant in its team's colour.
            line(px, py, px, py - r * 3.2f, 0xFFE0E0E0, 2.0f);
            o.triangles.push_back({px, py - r * 3.2f, px + r * 2.2f, py - r * 2.6f, px, py - r * 2.0f, c});
            o.circles.push_back({px, py, r * 0.5f, c, true, 1.0f});
            break;
        case nodes::Family::kLight: {
            // A bulb in its colour; rings at its radii when near.
            o.circles.push_back({px, py, r + 1.5f * k, outline, true, 1.0f});
            o.circles.push_back({px, py, r, c, true, 1.0f});
            for (int ray = 0; ray < 4; ++ray) {
                const float angle = kPi * 0.25f + kPi * 0.5f * static_cast<float>(ray);
                const float cx = std::cos(angle);
                const float cy = std::sin(angle);
                line(px + cx * r * 1.4f, py + cy * r * 1.4f, px + cx * r * 2.1f, py + cy * r * 2.1f, c, 1.5f);
            }
            if ((is_selected || is_hovered) && n.radius > 0.0f) {
                ring(n.pos, n.radius, x_axis, z_axis, c);
                if (n.inner_radius_ > 0.0f && n.inner_radius_ < n.radius)
                    ring(n.pos, n.inner_radius_, x_axis, z_axis, (c & 0x00FFFFFF) | 0x80000000);
            }
            break;
        }
        case nodes::Family::kSound:
            // A speaker: a box and its cone.
            line(px - r, py - r * 0.5f, px - r * 0.3f, py - r * 0.5f, c, 2.0f);
            line(px - r, py + r * 0.5f, px - r * 0.3f, py + r * 0.5f, c, 2.0f);
            line(px - r, py - r * 0.5f, px - r, py + r * 0.5f, c, 2.0f);
            o.triangles.push_back({px - r * 0.3f, py - r * 0.5f, px + r, py - r * 1.1f, px + r, py + r * 1.1f, c});
            o.triangles.push_back(
                {px - r * 0.3f, py - r * 0.5f, px + r, py + r * 1.1f, px - r * 0.3f, py + r * 0.5f, c});
            break;
        case nodes::Family::kParticle:
            // A spark: three strokes.
            for (int stroke = 0; stroke < 3; ++stroke) {
                const float angle = kPi * static_cast<float>(stroke) / 3.0f;
                const float cx = std::cos(angle) * r;
                const float cy = std::sin(angle) * r;
                line(px - cx, py - cy, px + cx, py + cy, c, 2.0f);
            }
            break;
        case nodes::Family::kFireOff:
            // A square; a radius as three rings when near.
            line(px - r, py - r, px + r, py - r, c, 2.0f);
            line(px + r, py - r, px + r, py + r, c, 2.0f);
            line(px + r, py + r, px - r, py + r, c, 2.0f);
            line(px - r, py + r, px - r, py - r, c, 2.0f);
            if (n.radius > 0.0f && (is_selected || is_hovered || distance < 2000.0f)) {
                const uint32_t faint = (c & 0x00FFFFFF) | 0x90000000;
                ring(n.pos, n.radius, x_axis, z_axis, faint);
                ring(n.pos, n.radius, x_axis, y_axis, faint);
                ring(n.pos, n.radius, z_axis, y_axis, faint);
            }
            break;
        case nodes::Family::kObject:
            // A small open diamond (the object itself shows).
            line(px, py - r * 0.8f, px + r * 0.8f, py, c, 1.5f);
            line(px + r * 0.8f, py, px, py + r * 0.8f, c, 1.5f);
            line(px, py + r * 0.8f, px - r * 0.8f, py, c, 1.5f);
            line(px - r * 0.8f, py, px, py - r * 0.8f, c, 1.5f);
            break;
        default:
            o.circles.push_back({px, py, r * 0.55f, c, true, 1.0f});
            break;
        }
        if (is_selected || is_hovered) {
            o.circles.push_back({px, py, r + 5.0f * k, is_selected ? kSelectedGizmo : kHoverGizmo, false, 2.0f});
            o.texts.push_back({px + r + 8.0f * k, py + 2.0f * k, 0xFFFFFFFF, nodes::caption(n.name)});
        }
        g_pickables.push_back({i, px, py, px, py, false, depth});
    }
    // (the Outliner's selectable off: drawn, not picked - level_editor_organize.h)
    std::erase_if(g_pickables, [&](const nodes::Pickable& p) {
        return p.node >= 0 && std::cmp_less(p.node, nodes.size()) && !organize::gizmo_pickable(core::at(nodes, p.node).name);
    });
    // (a box being dragged - level_editor_organize.h: its outline, solid inside / dashed crossing)
    organize::Rect2 box{};
    bool crossing = false;
    if (organize::box_outline(&box, &crossing)) {
        uint32_t c = crossing ? 0xFF8FD18Fu : 0xFFFFC870u;
        const std::array<ui::Seg, 4> edges = {{{box.x0, box.y0, box.x1, box.y0, c, 1.5f},
                                  {box.x1, box.y0, box.x1, box.y1, c, 1.5f},
                                  {box.x1, box.y1, box.x0, box.y1, c, 1.5f},
                                  {box.x0, box.y1, box.x0, box.y0, c, 1.5f}}};
        for (const ui::Seg& edge : edges) {
            if (crossing)
                view::add_dashed(&o.lines, edge, 6.0f * k, 4.0f * k);
            else
                o.lines.push_back(edge);
        }
    }
}

// --- highlighting (the things' own colour tinted: Scenery::get_tint) ---

constexpr uint32_t kHoverTint = 0x8098C8C8;       // RGBA: warmer and brighter
constexpr uint32_t kSelectedTintLow = 0x80D0A060; // a cyan pulse
constexpr uint32_t kSelectedTintHigh = 0x80FFD850;
constexpr uint32_t kBrowseTint = 0x8060E0E0; // the browser's entry: yellow
bool g_hover_tinted, g_selected_tinted;      // (else their boxes are drawn)

bool tint_sectors(const std::vector<uint32_t>& sectors, uint32_t rgba)
{
    bool all = true;
    for (const uint32_t checksum : sectors) {
        void* geom = scenery::sector_geometry(checksum);
        if (scenery::tintable(geom))
            scenery::get_tint({checksum, -1}, geom, rgba);
        else
            all = false;
    }
    return all;
}

void update_highlights()
{
    const uint32_t pulse = get_pulse(kSelectedTintLow, kSelectedTintHigh, SDL_GetTicks() / 1000.0);
    g_selected_tinted = true;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    for (objects::GameEntity* object : objects)
        g_selected_tinted &= object_edit::get_tint(object, pulse);
    if (objects.empty() && scenery::has_selection())
        g_selected_tinted = tint_sectors(scenery::get_selection(), pulse);
    g_hover_tinted = true;
    objects::GameEntity* hover = g_dragging ? nullptr : get_live(g_hover);
    if (hover && !is_picked(hover))
        g_hover_tinted = object_edit::get_tint(hover, kHoverTint);
    else if (!g_dragging && g_hover_sector && !scenery::get_selected(g_hover_sector) &&
             !scenery::why_not_movable(g_hover_sector))
        g_hover_tinted = tint_sectors(SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_LALT] ||
                                              SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_RALT]
                                          ? std::vector<uint32_t>{g_hover_sector}
                                          : scenery::hover_group(g_hover_sector),
                                      kHoverTint);
    // (the browser's entry where it is in the level)
    if (g_browser.open && std::cmp_less(g_browser.index, g_browser.items.size())) {
        const uint32_t id = core::at(g_browser.items, g_browser.index).id;
        if (g_browser.tab == 0)
            tint_sectors({id}, kBrowseTint);
        else if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id)))
            object_edit::get_tint(object, kBrowseTint);
    }
    scenery::end_tints(object_edit::resolve_tint, false);
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "level_editor", 1) != 0;
    return on;
}

bool& debug_enabled()
{
    static bool on = config_int("features", "debug_editor", 0) != 0;
    return on;
}

bool live_now() { return g_active; }
namespace {
// Occlusion culling switched off while editing (O; render::occlusion_off).
bool g_occlusion_off = true;
bool g_occlusion_in_play; // (render::occlusion_off as it was when the editor opened)
} // namespace

bool pass_key(bool* released, bool down)
{
    if (!down) {
        *released = true;
        return false;
    }
    return *released;
}

bool pass_pause_key(bool down)
{
    // (while dragging, browsing, reading the help or with something selected the key closes / deselects that instead - handle_event - and counts again
    // only once it has been up)
    // (drawing a chain: Enter finishes it - level_editor_chains.h)
    if (g_dragging || g_cancel_requested || g_browser.open || g_help || have_selection() || g_node_selected ||
        chains::get_drawing() != chains::Blit::kNone || chains::wants_pick() || !ui::keys_to_editor()) {
        g_pause_key_released = false;
        return false;
    }
    return pass_key(&g_pause_key_released, down);
}

// (offline only, outside career-type modes: no LEVEL EDITOR in a network session or in story / classic... -
// level_editor_layout.h)
bool cfunc_available(script::Struct*, script::Script*)
{
    return is_switched_on() && render::current_level() != 0 && !layout::get_refused();
}

bool cfunc_start(script::Struct*, script::Script*)
{
    if (!is_switched_on()) {
        get_log("level editor: off ([features] level_editor=0)");
        return false;
    }
    if (g_active)
        return true;
    // The level becomes its edited copy (its saved layout put in place if it wasn't) - never in a network session or a
    // career-type mode (the player told why on screen: the pause menu stays).
    std::string edited;
    if (!layout::enter_edited_copy(&edited)) {
        get_log("level editor: not started - %s", edited.c_str());
        layout::panel_message(edited.c_str());
        return false;
    }
    g_active = true;
    g_camera = nullptr;
    g_have_view = false;
    g_last_counter = 0;
    g_pause_key_released = false;
    g_exit_requested = false;
    g_cancel_requested = false;
    g_hover = {};
    g_objects.clear();
    g_hover_sector = 0;
    scenery::get_reset();
    g_dragging = false;
    g_axis = g_hover_axis = -1;
    g_undo.clear();
    g_redo.clear();
    g_has_pending = false;
    g_moves = 0;
    g_help = false;
    g_browser = Browser{};
    // The UI (Dear ImGui) - else the HUD of screen elements.
    const bool ui = ui::kickoff();
    view::kickoff();
    overlays::kickoff();  // (the view menus: Lit to start with - level_editor_overlays.h)
    ux8::kickoff();       // (no pie, not walking, not following - level_editor_ux8.h)
    ux9::kickoff();       // (UX9: gap wizard, skateability, ground tool - level_editor_ux9.h)
    faces::kickoff();     // (map3: Faces mode off, nothing selected)
    triggers::kickoff();  // (the level's trigger volumes found)
    goal_edit::kickoff(); // (the goals out of play while editing - level_editor_goals.h)
    entities::kickoff();
    // (occlusion off while flying the editor's camera: O turns it back on - kept for the next time; the setting from
    // [Graphics] OcclusionCulling comes back when the editor closes)
    g_occlusion_in_play = render::occlusion_off();
    // (the camera tools and the preferences - level_editor_camera.h, level_editor_prefs.h)
    g_speed = static_cast<float>(prefs::get_pull().fly_speed_);
    view_cam::kickoff(scenery::world_scene_key(scenery::main_scene()));
    play_test::on_editor_start(); // (the trail's run ends; the level's trails file read - level_editor_playtest.h)
    actions::seed_keys(SDL_GetKeyboardState(nullptr)); // (the key that opened the editor doesn't act at once)
    render::occlusion_off() = g_occlusion_off;
    g_hud_taken_down = 0;
    g_families = static_cast<uint32_t>(config_int("editor", "families", static_cast<int>(nodes::kAllFamilies))) &
                 nodes::kAllFamilies;
    g_pick_filter =
        static_cast<uint32_t>(config_int("editor", "pick", static_cast<int>(picking::kAll))) & picking::kAll;
    g_cycle = Cycle{};
    g_hover_cycle = -1;
    g_hover_passed = 0;
    g_node_selected = false;
    g_field_editing = false;
    g_hover_node = -1;
    g_pickables.clear();
    hud::get_reset(!ui);
    g_mouse = Mouse{};
    g_saved_cursor = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);
    // (unsaved changes of an earlier level - its autosave was written when the editor was left - aren't this level's)
    if (g_unsaved_load != layout::level_loads())
        g_unsaved = false;
    g_snapshot_changes = g_changes - 1; // (a snapshot on the first frame, if unsaved)
    g_last_autosave = SDL_GetTicks();
    g_offer_pending = g_offer_show = layout::autosave_offered();
    note_action("entered the editor");
    get_log("level editor: on");
    hud::get_message(edited.empty() ? "Level Editor - F1: Keyboard Shortcuts" : edited, edited.empty() ? 3.0 : 5.0);
    if (debug_enabled()) {
        object_edit::log_census();
        scenery::log_census();
        // (rail points away from their nodes' pos=: where the node layer shows them - the rail manager's)
        int away = 0;
        for (const nodes::Elem& n : nodes::get_all()) {
            script::Struct* s = n.family == nodes::Family::kRail ? nodes::struct_of(n) : nullptr;
            script::VectorValue v = {};
            V3 rail{};
            if (!s || !s->fetch_vec(0x7F261953, &v, 0) || !scenery::get_rail_point(n.name, &rail) ||
                (rail.x == v.x && rail.y == v.y && rail.z == v.z))
                continue;
            if (++away <= 8)
                get_log(
                    "level editor: rail point %s at (%.1f, %.1f, %.1f) - its node's pos= (%.1f, %.1f, %.1f), shown at "
                    "(%.1f, %.1f, %.1f)",
                    nodes::caption(n.name).c_str(), rail.x, rail.y, rail.z, v.x, v.y, v.z, n.pos.x, n.pos.y, n.pos.z);
        }
        get_log("level editor: %d rail points away from their nodes' pos=", away);
    }
    return true;
}

namespace {

// The ground below a point: the game's collision ray (a ProbeRay: constructor 0x00417760; 0x00417710 (start, end,
// movables) casts it - thiscall, RET 0xC, true on a hit; the hit's normal at +0x24, its point at +0x34), static
// collision only, from `from` along `to - from`.
bool get_cast(V3 from, V3 to, V3* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {from.x, from.y, from.z, 1.0f};
    const maths::Vec4 end = {to.x, to.y, to.z, 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const float* point = &ray->hit_.point.x;
    *hit = {point[0], point[1], point[2]};
    return true;
}
bool ground_below(V3 from, V3* ground) { return get_cast(from, {from.x, from.y - 5000.0f, from.z}, ground); }

// Leaving the editor: the skater (hidden while editing) put on the ground below the camera, facing the way the camera
// looked, so play goes on where the editor was left (as Obj_MoveToNode does: position, orientation, teleported).
void place_skater_below_camera()
{
    constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater)
        return;
    // (below the camera; else from high above it - the camera may have gone through the floor)
    V3 ground{};
    const bool found = ground_below(g_pos, &ground) || ground_below({g_pos.x, g_pos.y + 3000.0f, g_pos.z}, &ground);
    if (!found)
        ground = g_pos;
    const TickCycle frame = frame_from_yaw_pitch(g_yaw, 0.0f);
    V3 at = {-frame.back.x, 0.0f, -frame.back.z};
    const float length = std::sqrt(at.x * at.x + at.z * at.z);
    if (length > 0.0001f)
        at = {at.x / length, 0.0f, at.z / length};
    else
        at = {0.0f, 0.0f, 1.0f};
    // (rows: right = up x at, up, at)
    const maths::Vec4 right = {at.z, 0.0f, -at.x, 0.0f};
    const maths::Vec4 up = {0.0f, 1.0f, 0.0f, 0.0f};
    const maths::Vec4 forward = {at.x, 0.0f, at.z, 0.0f};
    for (maths::Mat4* m : {&skater->basis_frame_, &skater->display_matrix_}) {
        core::at(m->row, 0) = right;
        core::at(m->row, 1) = up;
        core::at(m->row, 2) = forward;
    }
    object_edit::get_place(skater, ground);
    get_log("level editor: the skater put at (%.0f, %.0f, %.0f)%s", ground.x, ground.y, ground.z,
            found ? " (the ground below the camera)" : " (the camera's place: no ground found below)");
}

} // namespace

bool cfunc_stop(script::Struct*, script::Script*)
{
    if (!g_active)
        return true;
    play_test::on_editor_stop(); // (a simulation stopped, the world put back - level_editor_playtest.h)
    end_drag();
    scenery::finalise(); // (the moved scenery's collision where it now is, before the skater is put down)
    // (unsaved changes autosaved - unless an older autosave is still offered)
    if (g_unsaved && layout::get_snapshot())
        layout::write_autosave();
    note_action("left the editor");
    scenery::end_tints(object_edit::resolve_tint, true);
    hud::hide_panel();
    preview::conceal();
    view::halt();     // (everything hidden in the editor shown again)
    overlays::halt(); // (the view mode back to Lit)
    ux8::halt();
    ux9::halt();
    thumbs::halt(); // (the Add palette's thumbnails freed)
    ruler::set_armed(false);
    faces::halt();
    carry::disarm();
    triggers::halt();
    goal_edit::halt(); // (the goals put in play again, with their changes)
    entities::halt();  // (the previewed object put back)
    render::occlusion_off() = g_occlusion_in_play;
    view_cam::halt();
    ui::halt();
    set_relative(false);
    place_skater_below_camera();
    if (g_camera) {
        g_camera->at_point_ = g_saved_pos;
        g_camera->basis_frame_ = g_saved_matrix;
    }
    if (g_saved_cursor >= 0)
        SDL_ShowCursor(g_saved_cursor);
    g_active = false;
    g_camera = nullptr;
    g_hover = {};
    g_objects.clear();
    g_hover_sector = 0;
    scenery::clear_selection();
    get_log("level editor: off (%d changes, %d scenery pieces moved since the level loaded%s)", g_moves,
            scenery::get_moved_count(), g_unsaved ? "; not saved" : "");
    return true;
}

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {"LevelEditor_Available", code_address(&cfunc_available)},
        {"LevelEditor_Start", code_address(&cfunc_start)},
        {"LevelEditor_Stop", code_address(&cfunc_stop)},
        {"LevelEditor_PickLevel", code_address(&layout::cfunc_pick_level)},
        {"LevelEditor_EditedLevels", code_address(&layout::cfunc_edited_levels)},
        {"LevelEditor_PlayCamera", code_address(&cameras::cfunc_play_camera)}, // (map4)
    });
    script::install_script_functions(kFunctions.data(), sizeof(kFunctions) / sizeof(core::at(kFunctions, 0)));
}

void prepare_all()
{
    os_layer::register_event_handler(handle_event);
    set_crash_hook(on_crash);
    script::keep_qb_names(is_switched_on()); // (node names for the HUD: Fire_Tender rather than 61EF3AD4)
    ux3::prepare_all(); // (the Outliner's records in the layouts, the actions' callbacks - level_editor_ux3.h)
    ux8::
        prepare_all(); // (pies, quick sets, walk / follow / teleport, controller mode: their actions - level_editor_ux8.h)
    ux9::prepare_all(); // (UX9: its actions and the ground pieces' layout records - level_editor_ux9.h)
}

void input_logic()
{
    if (!g_active)
        return;
    // Play from here (F5, Ctrl+P - level_editor_play.h): the editor's camera and selection kept for the way back, then
    // left as Escape leaves it (Play closes the pause menu that brings back).
    if (perform::take_request()) {
        perform::get_keep(resume_now(), layout::level_loads());
        g_exit_requested = true;
        get_log("level editor: play from here");
    }
    if (!g_exit_requested)
        return;
    g_exit_requested = false;
    if (debug_enabled())
        get_log("level editor: Escape - running level_editor_exit");
    script::launch_script("level_editor_exit", nullptr, nullptr, false);
}

void place_camera(gfx::ViewCam* camera)
{
    if (!g_active || !camera)
        return;
    if (!g_camera) {
        g_camera = camera;
        g_saved_pos = camera->at_point_;
        g_saved_matrix = camera->basis_frame_;
        g_pos = {camera->at_point_.x, camera->at_point_.y, camera->at_point_.z};
        const maths::Vec4& back = core::at(camera->basis_frame_.row, 2);
        yaw_pitch_from_back({back.x, back.y, back.z}, &g_yaw, &g_pitch);
        if (debug_enabled())
            get_log("level editor: camera %p at (%.1f, %.1f, %.1f), yaw %.1f, pitch %.1f degrees",
                    static_cast<void*>(camera), g_pos.x, g_pos.y, g_pos.z, g_yaw * 180.0f / kPi,
                    g_pitch * 180.0f / kPi);
        // (back from play from here: the camera and the selection as they were - level_editor_play.h)
        perform::Resume resume;
        if (perform::take_resume(&resume, layout::level_loads()))
            apply_resume(resume);
    }
    if (camera != g_camera)
        return;
    const TickCycle frame = frame_from_yaw_pitch(g_yaw, g_pitch);
    camera->at_point_ = {g_pos.x, g_pos.y, g_pos.z, 1.0f};
    core::at(camera->basis_frame_.row, 0) = {frame.right.x, frame.right.y, frame.right.z, 0.0f};
    core::at(camera->basis_frame_.row, 1) = {frame.up.x, frame.up.y, frame.up.z, 0.0f};
    core::at(camera->basis_frame_.row, 2) = {frame.back.x, frame.back.y, frame.back.z, 0.0f};
}

float adjust_view_angle(gfx::ViewCam* camera, float angle)
{
    return g_active && camera && camera == g_camera ? view_cam::adjust_view_angle(angle) : angle;
}

float get_near_plane(gfx::ViewCam* camera)
{
    return g_active && camera && camera == g_camera ? view_cam::get_near_plane() : 2.0f;
}

void note_view(gfx::ViewCam* camera, float angle, float aspect, const uint32_t viewport[4])
{
    if (!g_active || camera != g_camera)
        return;
    const Lens lens = lens_for(angle, aspect);
    if (debug_enabled() && !g_have_view)
        get_log("level editor: view %u,%u %ux%u, angle %.2f, aspect %.4f", viewport[0], viewport[1], viewport[2],
                viewport[3], angle, aspect);
    g_lens = lens;
    std::memcpy(g_viewport.data(), viewport, sizeof(g_viewport));
    g_have_view = true;
}

namespace {

// --- the HUD (level_editor_hud.h): what you're doing, what's selected and under the cursor, and the keys for it ---

using hud::get_key;

std::string describe_selection()
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        std::string text = object_edit::describe(objects.front());
        if (objects.size() > 1)
            text += "  + " + std::to_string(objects.size() - 1) + " more";
        return text;
    }
    if (scenery::has_selection())
        return scenery::describe_selection();
    if (const uint32_t wall = wall_only())
        return "invisible wall " + object_edit::caption(wall) + " - " + walls::describe_state(wall);
    if (g_node_selected && std::cmp_less(g_node_index, nodes::get_all().size())) {
        const nodes::Elem& node = core::at(nodes::get_all(), g_node_index);
        return "node " + nodes::caption(node.name) + " (" + nodes::caption(node.cls) + ")";
    }
    return "Nothing";
}

const char* mode_name()
{
    if (g_browser.open)
        return "ADD";
    if (g_dragging && g_drag_mode == xform::Mode::kSpinBy)
        return "ROTATING";
    if (g_dragging && g_drag_mode == xform::Mode::kScale)
        return "SCALING";
    if (g_dragging && g_axis >= 0)
        return g_axis == 0 ? "MOVING ALONG X" : g_axis == 1 ? "MOVING ALONG Y" : "MOVING ALONG Z";
    if (g_dragging)
        return "MOVING";
    if (have_selection() || g_node_selected)
        return "SELECTED";
    return "FLYING";
}

// What's under the cursor - what a click would take (level_editor_picking.h): the next of a click-to-cycle with its
// place, or the piece a plain click passes (Alt+click takes it).
// The gizmo's handle under the cursor, in words (level_editor_transform.h).
std::string handle_hover_text(int handle)
{
    const char* axis = handle == 0 ? "X" : handle == 1 ? "Y" : handle == 2 ? "Z" : "";
    switch (gizmo_mode()) {
    case xform::Mode::kSpinBy:
        return "Rotate ring (Ctrl: snap)";
    case xform::Mode::kScale:
        return handle == xform::kCentre ? std::string("Uniform scale handle")
                                        : std::string("the ") + axis + " handle - drag to scale it along that axis";
    default:
        return std::string("the ") + axis + " arrow - drag to move along it";
    }
}
std::string hover_text()
{
    if (const uint32_t wall = view::hover_wall(); wall && g_hover_axis < 0)
        return "Invisible wall " + object_edit::caption(wall) + " (Shift+click)";
    if (g_hover_handle >= 0 && g_hover_axis < 0)
        return std::string("the trigger volume's ") + "-+"[g_hover_handle & 1] + "XYZ"[g_hover_handle >> 1] +
               " face - drag to resize it";
    std::string text = g_hover_axis >= 0   ? handle_hover_text(g_hover_axis)
                       : g_hover_sector    ? scenery::describe_hover(g_hover_sector)
                       : get_live(g_hover) ? object_edit::describe(get_live(g_hover))
                                           : "-";
    if (g_hover_axis < 0 && g_hover_cycle >= 0) {
        std::array<char, 64> place{};
        std::snprintf(place.data(), sizeof(place), "  (%d of %d under the cursor: click)", g_hover_cycle + 1,
                      static_cast<int>(g_cycle.order.size()));
        text += place.data();
    } else if (g_hover_axis < 0 && !g_hover_sector && !get_live(g_hover) && g_hover_passed) {
        text = "-  (passing " + scenery::describe_hover(g_hover_passed) + " - Alt+click takes it)";
    }
    return text;
}

void update_hud()
{
    static int frames = 0;
    hud::tick();
    if (frames++ % 6 != 0)
        return;
    std::array<char, 200> text{};
    // (which level: always its edited copy - never the level as shipped)
    std::snprintf(text.data(), sizeof(text), "EDITING %.24s (EDITED COPY)   %s%s%s", layout::level_title().c_str(),
                  mode_name(), g_unsaved ? "   (unsaved)" : "", g_offer_pending ? "   (autosave: Ctrl+R)" : "");
    hud::change_strand(1, text.data());
    hud::change_strand(2, "Selected: " + describe_selection());
    hud::change_strand(3, "Under the cursor: " + hover_text());
    std::snprintf(text.data(), sizeof(text), "Camera (%.0f, %.0f, %.0f)  speed %.0f    %d changes    %d pieces moved", g_pos.x,
                  g_pos.y, g_pos.z, g_speed, g_moves, scenery::get_moved_count());
    hud::change_strand(4, text.data());
    // (each line at most 90 characters with its colour codes - 6 a key)
    if (g_browser.open) {
        hud::change_strand(5, get_key("Up/Down") + " choose   " + get_key("Left/Right") + " scenery / objects   " +
                                  get_key("Enter") + " place it");
        hud::change_strand(6, get_key("F") + " show me the original   " + get_key("B") + " or " + get_key("Esc") +
                                  " close");
    } else if (g_dragging) {
        if (g_axis >= 0)
            hud::change_strand(5, get_key("Mouse") + " moves it along the arrow   " + get_key("Ctrl") + " snap");
        else
            hud::change_strand(5, get_key("Mouse") + " move   " + get_key("Shift") + " up / down   " +
                                      get_key("Wheel") + " push / pull   " + get_key("Ctrl") + " snap");
        hud::change_strand(6, get_key("Esc") + " put it back where it was");
    } else if (have_selection()) {
        hud::change_strand(5, get_key("Drag") + " move  " + get_key("Arrows") + " one axis  " + get_key("WASDQE") +
                                  " nudge  " + get_key("ZX") + " turn  " + get_key("[ ]") + " scale  " + get_key("G") +
                                  " drop");
        hud::change_strand(6, (get_primary() ? std::string() : get_key("Ctrl+click") + " remove one  ") +
                                  get_key("Ctrl+D") + " copy  " + get_key("Del") + " hide  " + get_key("Home") +
                                  " reset  " + get_key("Esc") + " deselect  " + get_key("F1") + " help");
    } else {
        hud::change_strand(5, get_key("WASD") + " fly  " + get_key("QE") + " down / up  " + get_key("Shift") +
                                  " fast  " + get_key("RMB") + " look  " + get_key("Wheel") + " dolly");
        hud::change_strand(6, get_key("Click") + " select  " + get_key("B") + " add  " + get_key("Ctrl+S") + " save  " +
                                  get_key("Ctrl+Z/Y") + " undo  " + get_key("F1") + " help");
    }
}

// --- adding and deleting nodes (level_editor_node_create.h) ---

struct PlaceEntry {
    node_create::Kind kind;
    node_create::Team team;
    std::string label;
    uint32_t colour;
    bool lobby = false; // (a team lobby flag, TRG_Flag_<Colour>: node_create::lobby_flag_name)
};
const std::vector<PlaceEntry>& place_entries()
{
    static std::vector<PlaceEntry> entries;
    if (!entries.empty())
        return entries;
    using node_create::Kind;
    using node_create::Team;
    const auto teams = std::to_array<Team>({Team::kRed, Team::kBlue, Team::kGreen, Team::kYellow});
    entries.push_back({Kind::kSpawn, Team::kRed, "Multiplayer Spawn", nodes::spawn_colour(nodes::kMultiplayer, 0)});
    for (Team t : teams) {
        const std::string colour = node_create::get_team_name(t);
        entries.push_back(
            {Kind::kTeamSpawn, t, "Team spawn (" + colour + ")", nodes::team_colour(("_" + colour).c_str())});
    }
    entries.push_back({Kind::kCrown, Team::kRed, "Crown", 0xFFFFC233});
    for (Team t : teams) {
        const std::string colour = node_create::get_team_name(t);
        entries.push_back({Kind::kFlagSet, t, "CTF Flag (" + colour + ")", nodes::team_colour(("_" + colour).c_str())});
    }
    for (Team t : teams) {
        const std::string colour = node_create::get_team_name(t);
        entries.push_back(
            {Kind::kFlagSet, t, "Team Flag (" + colour + ")", nodes::team_colour(("_" + colour).c_str()), true});
    }
    entries.push_back({Kind::kLight, Team::kRed, "Level light", 0xFFFFE9A8});
    entries.push_back({Kind::kSound, Team::kRed, "Sound Emitter", 0xFF4FD1C5});
    entries.push_back({Kind::kProximityNode, Team::kRed, "Proximity Node", 0xFFF08A3C});
    return entries;
}

nodes::Family family_of(node_create::Kind kind)
{
    switch (kind) {
    case node_create::Kind::kSpawn:
    case node_create::Kind::kTeamSpawn:
        return nodes::Family::kSpawn;
    case node_create::Kind::kCrown:
        return nodes::Family::kCrown;
    case node_create::Kind::kFlagSet:
        return nodes::Family::kBitMark;
    case node_create::Kind::kLight:
        return nodes::Family::kLight;
    case node_create::Kind::kSound:
        return nodes::Family::kSound;
    case node_create::Kind::kProximityNode:
        return nodes::Family::kFireOff;
    }
    return nodes::Family::kOther;
}

// Palette entry `entry` placed at `at` (a point the cursor or the view found): dropped to the ground below it as G
// drops a node (a light raised 150 units), facing the camera's heading; selected; one undo step.
bool place_entity(int entry, V3 at);
void place_node(int entry, V3 at)
{
    if (place_entity(entry, at))
        return; // (the entities' entries - after the chains': level_editor_entities.h)
    const std::vector<PlaceEntry>& entries = place_entries();
    if (entry < 0 || std::cmp_greater_equal(entry, entries.size()))
        return;
    const PlaceEntry& e = core::at(entries, entry);
    if (const char* why =
            e.lobby ? node_create::why_not_lobby_flag(e.team) : node_create::get_why_not(e.kind, e.team)) {
        hud::get_message(why, 5.0);
        return;
    }
    V3 ground{};
    if (ground_below(ground_ray_start(at, at.y, true), &ground))
        at = dropped_to(at, at.y, ground.y);
    if (e.kind == node_create::Kind::kLight)
        at.y += 150.0f;
    std::string name = e.lobby ? node_create::lobby_flag_name(e.team) : node_create::pinned_node_name(e.kind, e.team);
    if (name.empty())
        name = node_create::fresh_name(node_create::name_stem(e.kind), node_create::name_taken);
    std::vector<node_create::Spec> specs = {node_create::default_spec(e.kind, e.team, at, g_yaw, name)};
    if (e.kind == node_create::Kind::kFlagSet && !e.lobby) // (a lobby flag has no base - as the levels' own)
        specs.push_back(node_create::current_base(specs.front()));
    UndoStep step;
    step.kind = UndoStep::Kind::kAdded;
    step.exist = false; // (undone: they're removed)
    std::string message;
    for (const node_create::Spec& spec : specs) {
        if (!node_create::fabricate(spec, -1, &message))
            break;
        step.added.push_back(spec);
        step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), spec.name));
    }
    if (step.added.empty()) {
        hud::get_message(message, 5.0);
        return;
    }
    push_undo(step, "Place node");
    g_pickables.clear();
    g_hover_node = -1;
    const int index = nodes::get_find(node_create::name_hash(specs.front().name));
    if (index >= 0)
        select_node_only(index);
    note_online_change(family_of(e.kind), specs.front().name.c_str(), "added");
    hud::get_message("Added " + specs.front().name);
    adjust::ActStep placed; // (level_editor_adjust.h)
    placed.kind = adjust::Kind::kPlace;
    adjust::get_note(placed);
    note_action("added %s (%s)", specs.front().name.c_str(), e.label.c_str());
}

// --- chains: rails, ladders, paths (level_editor_chains.h) ---

// A chain change made (`before`: the chains' state then): one undo step when it changed anything; `select` (a node's
// name; 0: none) selected; the message shown.
void after_chain_change(const chains::State& before, uint32_t select, const std::string& message)
{
    if (!chains::same_state(before, chains::at_hand())) {
        UndoStep step;
        step.kind = UndoStep::Kind::kChains;
        step.chains = before;
        push_undo(step, "Chain edit");
        g_pickables.clear();
        g_hover_node = -1;
    }
    if (select) {
        const int index = nodes::get_find(select);
        if (index >= 0)
            select_node_only(index);
    } else if (g_node_selected && nodes::get_find(g_node_name) < 0) {
        g_node_selected = false;
    }
    if (!message.empty())
        hud::get_message(message, 5.0);
    note_action("chains: %s", message.c_str());
}

// What the chain operations work on: the node selected by itself, else the selected object's node (a ped's / car's
// path); 0: none.
uint32_t chain_target()
{
    if (g_node_selected && g_node_name)
        return g_node_name;
    if (const objects::GameEntity* object = get_primary())
        return nodes::get_find(object->ident_) >= 0 ? object->ident_ : 0;
    return 0;
}

// An Inspector / key operation on the selected node's chain.
void run_chain_op(int op, uint32_t arg)
{
    const uint32_t target = chain_target();
    if (!target)
        return;
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    chains::apply_op(static_cast<chains::Op>(op), target, arg, &message, &select);
    if (select == target && !g_node_selected)
        select = 0; // (a ped / car stays selected as the object)
    after_chain_change(before, select, message);
}

// --- the full Inspector and the level properties (phase 6: level_editor_fields.h, level_editor_level_props.h) ---

// A node's links= typed in the Inspector (`home`: back as the level has them): the chains' `links` record, as relinking
// a chain does (one undo step, live).
void set_links_from_inspector(uint32_t node, const std::string& text, bool home)
{
    const chains::State before = chains::at_hand();
    chains::State state = before;
    auto it = std::ranges::find_if(state.links, [&](const chains::LinkEdit& l) { return l.name == node; });
    if (home) {
        if (it != state.links.end())
            state.links.erase(it);
    } else {
        fields::Value value;
        std::string error;
        if (!fields::parse(fields::Kind::kNames, text, &value, &error) ||
            !fields::valid_links(
                value.list, node, [](uint32_t c) { return nodes::get_find(c) >= 0; }, fields::get_name_of, &error)) {
            hud::get_message("Links: " + error, 5.0);
            return;
        }
        if (it != state.links.end()) {
            it->links = value.list;
        } else {
            state.links.push_back({node, value.list});
            std::ranges::sort(state.links,
                              [](const chains::LinkEdit& a, const chains::LinkEdit& b) { return a.name < b.name; });
        }
    }
    if (!chains::change_phase(state)) {
        hud::get_message("No level loaded");
        return;
    }
    after_chain_change(before, 0, home ? "Links reverted" : "Links set");
}

// The Inspector's field requests: set, removed, put back.
void field_request(const ui::Request& request)
{
    uint32_t node = request.node;
    if (!node || nodes::get_find(node) < 0)
        return;
    if (request.id == fields::key_links() && request.kind != ui::Request::Kind::kFieldRemove) {
        set_links_from_inspector(node, request.text, request.kind == ui::Request::Kind::kFieldHome);
        return;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kFields;
    std::string message;
    if (request.kind == ui::Request::Kind::kFieldHome) {
        const std::vector<uint32_t> keys = request.id ? std::vector<uint32_t>{request.id} : fields::touched_keys(node);
        for (const uint32_t key : keys) {
            fields::Own before, origin;
            if (fields::get_capture(node, key, &before) && fields::get_origin(node, key, &origin)) {
                step.fields.push_back({node, key, before});
                fields::place_in(node, key, origin);
            }
        }
        // (and the links, when every field goes back)
        if (!request.id) {
            chains::State chains = chains::at_hand();
            const bool relinked =
                std::ranges::any_of(chains.links, [&](const chains::LinkEdit& l) { return l.name == node; });
            if (relinked)
                set_links_from_inspector(node, "", true);
        }
        message = request.id ? "Field reverted" : "All fields reverted";
    } else {
        fields::Own before;
        fields::get_capture(node, request.id, &before);
        fields::Value value;
        std::string error;
        const bool removed = request.kind == ui::Request::Kind::kFieldRemove;
        if (!removed && !fields::parse(static_cast<fields::Kind>(request.value), request.text, &value, &error)) {
            hud::get_message("Not set: " + error, 5.0);
            return;
        }
        if (!request.label.empty())
            fields::remember(request.label);
        if (!fields::assign_value(node, request.id, value, removed, &error)) {
            hud::get_message("Not set: " + error, 5.0);
            return;
        }
        step.fields.push_back({node, request.id, before});
        const char* note = nullptr;
        const nodes::Elem& n = core::at(nodes::get_all(), nodes::get_find(node));
        const fields::When when = fields::when_applied(n.family, request.id, &note);
        message = std::string(removed ? "Field removed" : "Field set") +
                  (when == fields::When::kLive ? "" : " (applies on reload)");
    }
    if (!step.fields.empty()) {
        push_undo(step, message.c_str());
        hud::get_message(message, 4.0);
        note_action("inspector: %s - %s.%s", message.c_str(), object_edit::caption(node).c_str(),
                    fields::get_name_text(request.id, fields::get_name_of).c_str());
    }
}

// The level properties' panel: each change shown at once (the time of day) and kept; one undo step a field.
level_props::Props g_props_before;
bool g_props_editing;
void level_props_request(const ui::Request& request)
{
    if (request.kind == ui::Request::Kind::kLevelProps) {
        if (!g_props_editing) {
            g_props_before = level_props::at_hand();
            g_props_editing = true;
        }
        level_props::assign_value(request.props);
        return;
    }
    if (!g_props_editing)
        return;
    g_props_editing = false;
    if (!level_props::get_same(g_props_before, level_props::at_hand())) {
        UndoStep step;
        step.kind = UndoStep::Kind::kProps;
        step.props = g_props_before;
        push_undo(step, "Level properties");
        note_action("level properties changed");
    }
}

// The Place palette's chain entries (after the node kinds): a new rail, a new path, a new ladder.
constexpr int kChainEntries = 4; // (the feature map's ledge the fourth)
int chain_entry(int entry)
{
    const int base = static_cast<int>(place_entries().size());
    return entry >= base && entry < base + kChainEntries ? entry - base : -1;
}
// The Place palette's entities (after the chains): peds, cars, bouncy sets, particle systems, sound emitters
// (level_editor_entities.h). Placed at `at` (dropped to the ground below it): copies (one undo step, as the browser's)
// or a sound emitter with its sound (an added node: its undo step). False: not one of those entries.
bool place_entity(int entry, V3 at)
{
    const int index = entry - static_cast<int>(place_entries().size()) - kChainEntries;
    if (entry < 0 || index < 0 || std::cmp_greater_equal(index, entities::get_palette().size()))
        return false;
    const entities::PaletteEntry& e = core::at(entities::get_palette(), index);
    if (!e.why.empty()) {
        hud::get_message(e.why, 5.0);
        return true;
    }
    V3 ground{};
    if (ground_below(ground_ray_start(at, at.y, true), &ground))
        at = dropped_to(at, at.y, ground.y);
    entities::Placed placed = entities::get_place(index, at, g_yaw);
    if (placed.emitter) {
        const std::string name =
            node_create::fresh_name(node_create::name_stem(node_create::Kind::kSound), node_create::name_taken);
        const node_create::Spec spec =
            node_create::default_spec(node_create::Kind::kSound, node_create::Team::kRed, at, g_yaw, name);
        std::string message;
        if (!node_create::fabricate(spec, -1, &message)) {
            hud::get_message(message, 5.0);
            return true;
        }
        entities::attach_sound(node_create::name_hash(name), placed.sound);
        UndoStep step;
        step.kind = UndoStep::Kind::kAdded;
        step.exist = false;
        step.added.push_back(spec);
        step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), name));
        push_undo(step, "Place");
        g_pickables.clear();
        g_hover_node = -1;
        const int node = nodes::get_find(node_create::name_hash(name));
        if (node >= 0)
            select_node_only(node);
        hud::get_message("Added " + name + " (" + nodes::caption(placed.sound) + ")");
        note_action("added the sound emitter %s (%s)", name.c_str(), nodes::caption(placed.sound).c_str());
        return true;
    }
    if (placed.copies.empty()) {
        hud::get_message(placed.message.empty() ? std::string("Could not place") : placed.message, 5.0);
        return true;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    std::vector<objects::GameEntity*> made;
    for (const uint32_t name : placed.copies) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
        if (!object)
            continue;
        uint32_t source = 0;
        for (const object_edit::DupFrom& c : object_edit::get_copies()) {
            if (c.name == name)
                source = c.source;
        }
        step.copies.push_back({name, source, object_edit::store_away(object)});
        OrientedBox box{};
        if (object_edit::model_box(object, &box))
            made.push_back(object);
    }
    push_undo(step, "Place");
    if (!made.empty()) {
        select_objects(made);
    } else if (const int node = nodes::get_find(placed.copies.front()); node >= 0) {
        g_pickables.clear();
        select_node_only(node); // (a particle system: its node)
    }
    hud::get_message(placed.message);
    note_action("%s", placed.message.c_str());
    adjust::ActStep adjust; // (level_editor_adjust.h)
    adjust.kind = adjust::Kind::kPlace;
    adjust::get_note(adjust);
    return true;
}

chains::Blit chain_kind_of(int entry)
{
    const int chain = chain_entry(entry);
    return chain == 0   ? chains::Blit::kRail
           : chain == 1 ? chains::Blit::kRouteWay
           : chain == 2 ? chains::Blit::kLadder
           : chain == 3 ? chains::Blit::kLedge
                        : chains::Blit::kNone;
}

// A click while drawing: a point where the cursor's ray meets the level (edges snapped unless Alt; a free end near it
// taken), linked on from the last.
void draw_chain_point(V3 dir, bool alt)
{
    if (chains::get_drawing() == chains::Blit::kLadder) {
        // (a ladder: its foot on a wall, then its top - level_editor_chains.h)
        const chains::State before = chains::at_hand();
        std::string message;
        uint32_t select = 0;
        const bool made = chains::ladder_click(g_pos, make_unit(dir), kMaxPickDistance, &message, &select);
        after_chain_change(before, made ? select : 0, message);
        if (made && chain_kind_of(g_place_armed) != chains::Blit::kNone)
            g_place_armed = -1;
        return;
    }
    V3 point{};
    uint32_t end = 0;
    if (!chains::magnet_point(g_pos, make_unit(dir), kMaxPickDistance, alt, &point, &end)) {
        hud::get_message("No surface under the cursor");
        return;
    }
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    chains::draw_at(point, end, &message, &select);
    // (nothing selected while drawing: the camera keeps flying - a selection would be orbited)
    unpick();
    after_chain_change(before, 0, message);
    if (chains::get_drawing() == chains::Blit::kNone && chain_kind_of(g_place_armed) != chains::Blit::kNone)
        g_place_armed = -1; // (linked onto an end: done)
}

// Drawing finished (Enter, a right click) or an armed pick / drawing cancelled (Esc).
void stop_chain_drawing(const char* message)
{
    std::string text;
    uint32_t select = 0;
    chains::apply_op(chains::kOpCancel, 0, 0, &text, &select);
    if (chain_kind_of(g_place_armed) != chains::Blit::kNone)
        g_place_armed = -1;
    if (message)
        hud::get_message(message);
}

// The selected node deleted: an added one removed (undoable); a level's own hidden - after the confirm window when the
// UI runs (`confirmed`), at once with the old HUD.
void unlink_list_node(bool confirmed)
{
    const int index = picked_node_index();
    if (index < 0)
        return;
    const nodes::Elem node = core::at(nodes::get_all(), index);
    // A rail or path point: deleted from its chain, its neighbours linked (a level's own hidden - level_editor_chains.h).
    if (node.family == nodes::Family::kRail || node.family == nodes::Family::kRouteWay ||
        node.family == nodes::Family::kLadder) { // (a ladder: whole)
        run_chain_op(chains::kOpDelete, 0);
        return;
    }
    // A copy selected by its node (a particle system's - level_editor_entities.h): deleted as the object copies are.
    if (object_edit::is_copy(node.name)) {
        UndoStep step;
        step.kind = UndoStep::Kind::kMade;
        step.exist = true; // (undone: made again)
        uint32_t source = 0;
        for (const object_edit::DupFrom& c : object_edit::get_copies()) {
            if (c.name == node.name)
                source = c.source;
        }
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(node.name));
        if (!object || (object->object_flags_ & objects::kObjectFlagDead))
            return;
        step.copies.push_back({node.name, source, object_edit::store_away(object)});
        object_edit::remove_copy(node.name);
        push_undo(step, "Delete copy");
        g_node_selected = false;
        g_pickables.clear();
        g_hover_node = -1;
        hud::get_message("Deleted");
        note_action("deleted the copy %s", nodes::caption(node.name).c_str());
        return;
    }
    if (node.added) {
        UndoStep step;
        step.kind = UndoStep::Kind::kAdded;
        step.exist = true; // (undone: made again)
        node_create::Spec spec;
        int at = 0;
        if (!node_create::drop_out(node.name, &spec, &at))
            return;
        step.added.push_back(spec);
        step.added_at_.push_back(at);
        push_undo(step, "Delete node");
        g_node_selected = false;
        g_pickables.clear();
        g_hover_node = -1;
        note_online_change(node.family, spec.name.c_str(), "deleted");
        hud::get_message("Deleted " + spec.name);
        note_action("deleted the added node %s", spec.name.c_str());
        return;
    }
    if (node.hidden) {
        hud::get_message("Already hidden");
        return;
    }
    const char* why = nullptr;
    if (!node_create::can_hide(node, &why)) {
        hud::get_message(why ? why : "Can't be hidden", 5.0);
        return;
    }
    if (ui::underway() && !confirmed) {
        g_confirm_hide = node.name;
        return;
    }
    if (!node_create::conceal(node.name, true))
        return;
    UndoStep step;
    step.kind = UndoStep::Kind::kHidden;
    step.hidden_names_ = {node.name};
    step.hidden = false; // (undone: shown)
    push_undo(step, "Hide node");
    note_online_change(node.family, nodes::caption(node.name).c_str(), "hidden");
    hud::get_message("Hidden " + nodes::caption(node.name));
    note_action("hid the level's node %s", nodes::caption(node.name).c_str());
}

// Home on a hidden node: shown again (undoable).
bool show_hidden_node()
{
    const int index = picked_node_index();
    if (index < 0 || !core::at(nodes::get_all(), index).hidden)
        return false;
    const uint32_t name = core::at(nodes::get_all(), index).name;
    if (chains::is_chain_node(name)) {
        // (a deleted rail / path point: its neighbours were linked past it - the chains' undo puts both back)
        hud::get_message("Deleted points can be restored with Undo");
        return true;
    }
    if (!node_create::conceal(name, false))
        return false;
    UndoStep step;
    step.kind = UndoStep::Kind::kHidden;
    step.hidden_names_ = {name};
    step.hidden = true; // (undone: hidden again)
    push_undo(step, "Show node");
    hud::get_message("Shown " + nodes::caption(name));
    note_action("showed the level's node %s again", nodes::caption(name).c_str());
    return true;
}

// An added crown earlier / later among the added crowns (one undo step).
void move_selected_crown(int delta)
{
    const int index = picked_node_index();
    if (index < 0)
        return;
    const nodes::Elem& node = core::at(nodes::get_all(), index);
    UndoStep step;
    step.kind = UndoStep::Kind::kOrder;
    step.order = node_create::get_order();
    std::string message;
    const uint32_t name = node.name;
    const nodes::Family family = node.family;
    if (!node_create::move_crown(name, delta, &message)) {
        hud::get_message(message, 5.0);
        return;
    }
    push_undo(step, "Crown order");
    const int now = nodes::get_find(name);
    if (now >= 0) {
        select_node_only(now);
        std::array<char, 96> text{};
        std::snprintf(text.data(), sizeof(text), "Now crown #%d", core::at(nodes::get_all(), now).order);
        hud::get_message(text.data());
    }
    note_online_change(family, nodes::caption(name).c_str(), "reordered");
}

// --- the UI (level_editor_ui.h): what it shows ---

std::vector<ui::BrowserItem> g_ui_browser_items;

// The keys for what you're doing now (the status bar - UX8's context hints, level_editor_hints.h: from the action
// registry's keys as bound now and the editor's state; the old HUD keeps its own lines).
std::vector<std::pair<std::string, std::string>> keys_now()
{
    hints::CtxInfo c;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (ui::keys_to_editor()) {
        if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])
            c.mods |= actions::kCtrl;
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
            c.mods |= actions::kShift;
        if (keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT])
            c.mods |= actions::kAlt;
    }
    c.rmb = g_mouse.right;
    c.fly_always_ = xform::fly_always();
    if (picked_node_index() >= 0)
        c.selection = wall_only() ? hints::Selection::kWall : hints::Selection::kPointNode;
    else if (get_primary())
        c.selection = hints::Selection::kObject;
    else if (wall_only())
        c.selection = hints::Selection::kWall;
    else if (scenery::has_selection())
        c.selection = hints::Selection::kScenery;
    else if (g_node_selected)
        c.selection = hints::Selection::kOtherNode;
    c.free_camera_ = g_free_camera;
    c.tool = static_cast<int>(xform::at_hand().mode);
    if (g_dragging)
        c.drag = g_drag_mode == xform::Mode::kSpinBy  ? hints::Drag::kSpinBy
                 : g_drag_mode == xform::Mode::kScale ? hints::Drag::kScale
                 : g_axis >= 0                        ? hints::Drag::kMoveAxis
                                                      : hints::Drag::kShiftBy;
    c.modal = g_modal;
    if (g_dragging && (!g_typed.text.empty() || g_typed.axis >= 0))
        c.typed = xform::describe(g_drag_mode, g_typed, g_axis >= 0 && g_axis < 3 ? g_axis : -1);
    c.carried = g_dragging ? carry::total_of() : 0;
    c.browser = g_browser.open;
    c.place_armed_ = g_place_armed >= 0;
    c.ladder_drawing_ = chains::get_drawing() == chains::Blit::kLadder;
    c.chain_drawing_ = chains::get_drawing() != chains::Blit::kNone && !c.ladder_drawing_;
    c.chain_pick_ = chains::wants_pick();
    if (c.ladder_drawing_ || c.chain_pick_)
        c.chain_waiting_ = chains::get_waiting();
    c.ruler_armed_ = ruler::get_armed();
    c.ux9_tool_ = ux9::picking_tool();
    c.faces_mode_ = faces::get_picking();
    c.box_armed_ = organize::box_armed();
    c.chain_point_ = picked_node_index() >= 0 && node_edit::is_chain(g_node_name);
    c.following = ux8::get_following();
    c.walking = ux8::on_foot();
    c.pie_open_ = ux8::pie_open();
    c.pie_sticky_ = ux8::pie_sticky();
    c.pad = ux8::pad_mode() && ux8::pad_owns_cursor();
    c.simulating = play_test::get_simulating();
    if (c.mods)
        c.modded = hints::modifier_hints(actions::get_all(), c.mods, 9);
    std::vector<std::pair<std::string, std::string>> out;
    for (const hints::Hint& h :
         hints::get_build(c, [](const char* id) { return actions::get_keys_text(actions::get_find(id)); }))
        out.emplace_back(h.keys, h.what);
    return out;
}

// The Outliner's markers (level_editor_ui.h's kMark*): by node name, what the editor has done to it, its object or its
// scenery - read from each module's record of its changes.
std::unordered_map<uint32_t, uint8_t> change_marks()
{
    std::unordered_map<uint32_t, uint8_t> marks;
    auto mark = [&](uint32_t name, uint8_t bits) {
        if (name)
            marks[name] |= bits;
    };
    for (const scenery::SectorState& s : scenery::changed_sectors()) {
        const bool moved = s.placement.quarters != 0 || !is_unit_scale(s.placement.scale) ||
                           s.placement.offset.x != 0.0f || s.placement.offset.y != 0.0f || s.placement.offset.z != 0.0f;
        mark(s.checksum, static_cast<uint8_t>((moved ? ui::kMarkMoved : 0) | (s.hidden ? ui::kMarkHidden : 0)));
    }
    for (const scenery::RailState& r : scenery::changed_rails())
        mark(r.name, static_cast<uint8_t>(r.hidden ? ui::kMarkHidden : ui::kMarkMoved));
    for (const walls::WallState& w : walls::get_changed()) {
        const bool moved = !walls::is_home(w.placement);
        mark(w.checksum, static_cast<uint8_t>((moved ? ui::kMarkMoved : 0) | (w.off ? ui::kMarkCollisionOff : 0)));
    }
    for (const uint32_t id : object_edit::get_touched()) {
        const object_edit::State* origin = object_edit::get_origin(id);
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (!origin || !object)
            continue;
        const maths::Vec4 &a = core::at(origin->matrix.row, 2), &b = core::at(object->basis_frame_.row, 2);
        if (origin->pos.x != object->at_point_.x || origin->pos.y != object->at_point_.y ||
            origin->pos.z != object->at_point_.z || a.x != b.x || a.z != b.z)
            mark(id, ui::kMarkMoved);
    }
    for (const node_edit::LinkRecord& r : node_edit::get_changed())
        mark(r.name, ui::kMarkMoved);
    for (const nodes::Elem& n : nodes::get_all()) {
        if (n.added)
            mark(n.name, ui::kMarkAdded);
        if (n.hidden)
            mark(n.name, ui::kMarkHidden);
    }
    for (const uint32_t id : view::hidden_ids())
        mark(id, ui::kMarkViewHidden);
    for (const picking::Lock& lock : picking::get_locks())
        mark(lock.id, ui::kMarkClickThrough);
    return marks;
}

ui::ShapeMesh assemble_model()
{
    ui::ShapeMesh model = {};
    model.carry = carry::is_switched_on();
    model.carried = g_dragging || g_key_moving || g_field_editing ? carry::total_of() : 0; // (a move under way)
    model.marks = change_marks();
    const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
    model.level = scene ? object_edit::caption(scene) : "";
    model.level_title_ = layout::level_title();
    model.edited_saved_ = layout::has_saved_layout();
    model.confirm_reset_ = g_confirm_reset;
    model.mode = mode_name();
    model.selected = describe_selection();
    model.hover = hover_text();
    model.message = hud::current_message();
    model.keys = keys_now();
    model.camera = g_pos;
    model.speed = g_speed;
    model.changes = g_moves;
    model.moved = scenery::get_moved_count();
    model.unsaved = g_unsaved;
    model.offer = g_offer_pending;
    model.can_undo_ = !g_undo.empty();
    model.can_redo_ = !g_redo.empty();
    model.snap_always_ = g_snap_always;
    model.pick_filter_ = g_pick_filter;
    for (const picking::Lock& lock : picking::get_locks())
        model.locks.push_back(
            {lock.kind, lock.id,
             std::string(lock.kind == picking::kObjects ? "object " : "scenery ") + object_edit::caption(lock.id)});
    {
        bool all = true;
        const std::vector<picking::Lock> selected = selection_locks();
        for (const picking::Lock& lock : selected)
            all = all && picking::latched(lock.kind, lock.id);
        model.selection_lockable_ = !selected.empty();
        model.selection_locked_ = !selected.empty() && all;
        model.view.can_hide_ = !selected.empty();
    }
    {
        const view::Attributes stats = view::last_stats();
        model.view.collision = view::show_collision();
        model.view.triangles = stats.triangles;
        model.view.sectors = stats.sectors;
        model.view.walls = stats.walls;
        model.view.companions = stats.companions;
        model.view.taken = stats.taken;
        model.view.triggers = stats.triggers;
        model.view.capped = stats.capped;
        model.view.hidden = view::hidden_count();
        if (const uint32_t wall = view::selected_wall()) {
            model.view.wall = wall;
            model.view.wall_name_ = object_edit::caption(wall);
            model.view.wall_info_ = view::describe_wall(wall);
            model.view.wall_state_ = walls::describe_state(wall);
            model.view.wall_off_ = walls::disengaged(wall);
            Placement placement;
            model.view.wall_moved_ = walls::get_moved(wall, &placement);
        }
        model.view.off = stats.off;
        // (the Inspector's Collision checkbox: the selected scenery's switches - level_editor_walls.h)
        std::vector<bool> offs;
        if (!get_primary())
            for (const uint32_t checksum : scenery::get_selection())
                offs.push_back(walls::disengaged(checksum));
        model.view.scenery_collision_ = walls::get_combined(offs);
    }
    std::vector<objects::GameEntity*> objects = selected_objects();
    ui::ShapeMesh::Selection& s = model.selection;
    if (!objects.empty()) {
        objects::GameEntity* object = objects.front();
        s.objects = static_cast<int>(objects.size());
        const char* cls = object_edit::class_of(object->ident_);
        s.kind = cls ? cls : "Object";
        s.name = object_edit::caption(object->ident_);
        s.checksum = object->ident_;
        s.pos = object_edit::pos_of(object);
        const maths::Vec4& at = core::at(object->basis_frame_.row, 2);
        s.scalable = scale_tool::object_scalable(object);
        s.scale = scale_tool::get_object_scale(object);
        s.has_heading_ = true;
        s.heading_degrees_ =
            static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * 180.0 / kPi);
    } else if (scenery::has_selection()) {
        s.sectors = static_cast<int>(scenery::get_selection().size());
        s.kind = s.sectors > 1 ? "Scenery group" : "Scenery";
        s.checksum = scenery::get_selection().front();
        s.name = object_edit::caption(s.checksum);
        s.pos = scenery::get_pivot();
        s.scalable = true;
        s.scale = scenery::selection_scale();
        if (s.sectors > 1 || scenery::selected_rail_count() > 0) {
            for (const uint32_t checksum : scenery::get_selection()) {
                ui::ShapeMesh::Selection::Member member;
                member.id = checksum;
                member.name = object_edit::caption(checksum);
                member.shadow = scenery::is_shadow(checksum);
                OrientedBox box{};
                if (scenery::cuboid(checksum, &box)) {
                    std::array<char, 48> size{};
                    const V3 extent = box_size(box);
                    std::snprintf(size.data(), sizeof(size), "%.0f x %.0f x %.0f", extent.x, extent.y, extent.z);
                    member.size = size.data();
                }
                s.members.push_back(member);
            }
            for (const uint32_t name : scenery::selected_rail_names()) {
                ui::ShapeMesh::Selection::Member member;
                member.rail = true;
                member.id = name;
                member.name = nodes::caption(name);
                s.members.push_back(member);
            }
        }
    }
    model.help = g_help;
    model.browser = g_browser.open;
    model.browser_tab_ = g_browser.tab;
    model.browser_index_ = g_browser.index;
    g_ui_browser_items.clear();
    for (const Browser::Item& item : g_browser.items)
        g_ui_browser_items.push_back({item.id, item.label});
    model.browser_items_ = &g_ui_browser_items;
    model.preview.index = !g_browser.open || g_browser.items.empty() ? -1
                          : g_browser.preview >= 0 && std::cmp_less(g_browser.preview, g_browser.items.size())
                              ? g_browser.preview
                              : g_browser.index;
    model.nodes.families = g_families;
    model.nodes.selected = inspected_node();
    model.nodes.hovered = g_hover_node;
    model.nodes.node_only_ = g_node_selected && model.nodes.selected >= 0;
    model.nodes.level_shared_ = nodes::level_is_shared(&model.nodes.shared_game_);
    node_edit::Values node;
    if (model.nodes.node_only_ && node_values(&node)) {
        model.nodes.movable = true;
        model.nodes.pos = node.pos;
        model.nodes.angles = node.angles;
        const node_edit::Values* origin = node_edit::get_origin(g_node_name);
        model.nodes.moved = origin && !node_edit::get_same(node, *origin);
        model.nodes.when_applied_ = node_edit::when_applied(core::at(nodes::get_all(), model.nodes.selected));
        if (node_edit::is_chain(g_node_name)) {
            model.nodes.rail = true; // (a rail's, a ladder's or a path's point: moved alone or with its chain)
            model.nodes.chain_noun_ = chains::is_path(g_node_name)     ? "path"
                                      : chains::is_ladder(g_node_name) ? "ladder"
                                                                       : "rail";
            model.nodes.entire_rail_ = g_move_entire_rail;
            model.nodes.rail_points_ = static_cast<int>(node_edit::moved_with(g_node_name, g_move_entire_rail).size());
        }
    }
    model.nodes.online_warning_ = g_online_warning_shown;
    if (model.nodes.node_only_ && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& n = core::at(nodes::get_all(), model.nodes.selected);
        model.nodes.added = n.added;
        model.nodes.hidden = n.hidden;
        const char* why = nullptr;
        model.nodes.can_hide_ = !n.added && node_create::can_hide(n, &why);
        model.nodes.why_no_hide_ = why ? why : "";
        if (n.added && n.family == nodes::Family::kCrown) {
            const node_create::LinkChain list = node_create::at_hand();
            std::string name;
            for (const node_create::Spec& spec : list) {
                if (node_create::name_hash(spec.name) == n.name)
                    name = spec.name;
            }
            node_create::LinkChain up = list, down = list;
            model.nodes.crown_up_ = node_create::move_crown(&up, name, -1);
            model.nodes.crown_down_ = node_create::move_crown(&down, name, 1);
        }
    }
    {
        // Phase 6: the level's properties, its own values (read once a level: its structure is walked for them) and
        // its restarts.
        static int g_defaults_for = -1;
        static level_props::Props defaults;
        static bool g_have_defaults = false;
        if (g_defaults_for != layout::level_loads()) {
            g_defaults_for = layout::level_loads();
            g_have_defaults = level_props::level_defaults(&defaults);
        }
        model.level_props_.in_level_ = render::current_level() != 0 && scene != 0;
        model.level_props_.props = level_props::at_hand();
        model.level_props_.defaults = defaults;
        model.level_props_.have_defaults_ = g_have_defaults;
        for (const nodes::Elem& n : nodes::get_all()) {
            if (n.family == nodes::Family::kSpawn && !n.hidden && n.name)
                model.level_props_.restarts.emplace_back(n.name, nodes::caption(n.name));
        }
        if (model.nodes.node_only_ && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
            const nodes::Elem& n = core::at(nodes::get_all(), model.nodes.selected);
            if (n.family == nodes::Family::kSpawn && !n.hidden)
                model.level_props_.selected_restart_ = n.name;
        }
    }
    model.place.open = g_place_open;
    model.place.armed = g_place_armed;
    if (g_place_open) {
        for (const PlaceEntry& e : place_entries()) {
            const char* why =
                e.lobby ? node_create::why_not_lobby_flag(e.team) : node_create::get_why_not(e.kind, e.team);
            model.place.entries.push_back({e.label, e.colour, why ? why : ""});
        }
        // (phase 4: chains drawn point by point - level_editor_chains.h)
        model.place.entries.push_back({"Rail", 0xFF7FB2D9, ""});
        model.place.entries.push_back({"Path", 0xFF8FD18F, ""});
        model.place.entries.push_back({"Ladder", 0xFFC88FD9, ""});
        model.place.entries.push_back({"Ledge", 0xFFB98CFF, ""});
        // (peds, cars, bouncy sets, particle systems, sound emitters - level_editor_entities.h; place_entity)
        for (const entities::PaletteEntry& e : entities::get_palette())
            model.place.entries.push_back({e.label, e.colour, e.why});
    }
    model.chain = chain_target() ? chains::view_of(chain_target()) : chains::View{};
    if (!model.chain.waiting)
        model.chain.waiting = chains::get_waiting();
    if (g_confirm_hide) {
        const int index = nodes::get_find(g_confirm_hide);
        if (index >= 0) {
            const nodes::Elem& n = core::at(nodes::get_all(), index);
            model.confirm.open = true;
            model.confirm.name = nodes::caption(n.name);
            std::string family = nodes::family_name(n.family);
            if (!family.empty() && family.back() == 's')
                family.pop_back(); // ("Crowns": "Crown")
            for (char& ch : family)
                ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            model.confirm.family = family;
            model.confirm.crown = n.family == nodes::Family::kCrown;
        } else {
            g_confirm_hide = 0;
        }
    }
    return model;
}

void show_help()
{
    const std::vector<std::string> lines = {
        "LEVEL EDITOR - KEYS    (F1 or Esc closes this)",
        "Flying       W A S D move   Q / E down / up   Shift faster   right mouse looks",
        "             wheel forward / back   - / = flying speed",
        "Selecting    click an object or scenery (a piece of scenery takes its group)",
        "             floors, ceilings, rooms around you and decals are passed by a click",
        "             click again on the same spot: the next thing under the cursor",
        "             Shift+click adds / removes   Alt+click (middle click) one piece, any",
        "             Ctrl+click removes one piece from the selection (a group takes its shadows too)",
        "             L locks the selection (clicks pass it); L with nothing selected unlocks all",
        "             what clicks take: [editor] pick= in the ini (1 nodes 2 objects 4 scenery)",
        "             Esc or Backspace deselects",
        "Moving       hold and drag (Shift: up / down, wheel: push / pull, Ctrl: snap)",
        "             drag an arrow to move along that axis only",
        "             W A S D / Q E nudge   Z / X turn   G drops it to the ground",
        "             [ / ] scale scenery / an object down / up 10% (Shift: 1%), 0.25x to 4x",
        "             Alt+[ / ] stretch it along the axis the camera faces most (x, y or z)",
        "             a rail / ladder / path point moves with its whole chain - L: by itself",
        "             scenery carries what stands on it ([editor] carry=0 off; Alt while moving leaves it)",
        "Chains       N > Rail / Path: click points (Alt: no edge snap), Enter / right click finishes",
        "             on a point: Insert adds one after   Del deletes   End draws on   J links   U unlinks",
        "Editing      Ctrl+D duplicates   Del hides (a copy / added node: deletes)   Home puts back",
        "             Ctrl+Z undo   Ctrl+Y redo   Ctrl+Shift+Del twice: reset the level",
        "Adding       B lists the level's pieces and objects: Enter places a copy",
        "             N: new spawns, crowns, flags, lights... - pick one, click in the view",
        "Saving       Ctrl+S saves the edited copy (the level select's EDITED entry)",
        "             unsaved changes are autosaved: Ctrl+R restores, Ctrl+Shift+R discards",
        "Leaving      Esc (first it cancels a drag, closes a panel or deselects)",
        "View         H hides the selection while editing   I or / isolates it   Alt+H shows all",
        "             (never saved, never in play; the collision view, C, needs the UI)",
        "Triggers     T: kill zones, teleports, gaps - shown and added with the UI only",
        "Camera       P writes where the camera is to the log (position, back, up)",
    };
    hud::show_panel(lines, 96.0f, 104.0f, 448.0f);
}

// --- the browser (B): the level's pieces and objects, a copy placed where the cursor points ---

void fill_browser()
{
    g_browser.items.clear();
    if (g_browser.tab == 0) {
        for (const uint32_t checksum : scenery::movable_pieces()) {
            OrientedBox box{};
            std::array<char, 48> size = {""};
            if (scenery::cuboid(checksum, &box))
                std::snprintf(size.data(), sizeof(size), "  %.0f x %.0f x %.0f", box.max.x - box.min.x, box.max.y - box.min.y,
                              box.max.z - box.min.z);
            g_browser.items.push_back({checksum, object_edit::caption(checksum) + size.data()});
        }
        std::ranges::sort(g_browser.items,
                          [](const Browser::Item& a, const Browser::Item& b) { return a.label < b.label; });
    } else {
        for (const object_edit::TableRow& entry : object_edit::copyable())
            g_browser.items.push_back({entry.name, entry.label});
    }
    g_browser.index = std::min(g_browser.index, std::max(0, static_cast<int>(g_browser.items.size()) - 1));
    g_browser.preview = -1;
}

void show_browser()
{
    std::vector<std::string> lines;
    std::array<char, 96> title{};
    std::snprintf(title.data(), sizeof(title), "ADD   %s / %s   (%d)", g_browser.tab == 0 ? "SCENERY" : "scenery",
                  g_browser.tab == 1 ? "OBJECTS" : "objects", static_cast<int>(g_browser.items.size()));
    lines.emplace_back(title.data());
    const int count = static_cast<int>(g_browser.items.size());
    if (g_browser.index < g_browser.top)
        g_browser.top = g_browser.index;
    if (g_browser.index >= g_browser.top + kBrowserRows)
        g_browser.top = g_browser.index - kBrowserRows + 1;
    for (int row = 0; row < kBrowserRows; ++row) {
        const int i = g_browser.top + row;
        if (i >= count) {
            lines.emplace_back(
                row == 0 ? (g_browser.tab == 0 ? "(no pieces that can be moved)" : "(no objects that can be copied)")
                         : "");
            continue;
        }
        const std::string label = core::at(g_browser.items, i).label.substr(0, 40);
        lines.push_back(i == g_browser.index ? get_key(("> " + label).c_str()) : "  " + label);
    }
    hud::show_panel(lines, 380.0f, 34.0f, 254.0f);
}

// Where the cursor points: the ground below the point its ray first meets the level (a wall: the floor at its foot;
// a roof: the roof) - none: 600 units ahead, on the ground below that if any.
V3 cursor_point(V3 dir, bool have_ray, const TickCycle& frame)
{
    V3 hit{}, ground{};
    const V3 ray = have_ray ? make_unit(dir) : frame.back * -1.0f;
    if (get_cast(g_pos, g_pos + ray * kMaxPickDistance, &hit)) {
        const V3 back = hit - ray * 5.0f + V3{0.0f, 5.0f, 0.0f};
        return ground_below(back, &ground) ? ground : hit;
    }
    V3 ahead = g_pos + ray * 600.0f;
    if (ground_below(ahead, &hit))
        return hit;
    return ahead;
}

void place_from_browser(V3 at, bool keep_open = false)
{
    if (std::cmp_greater_equal(g_browser.index, g_browser.items.size()))
        return;
    const uint32_t source = core::at(g_browser.items, g_browser.index).id;
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    if (g_browser.tab == 0) {
        const uint32_t copy = scenery::get_duplicate(source);
        if (!copy) {
            hud::get_message("Couldn't copy the piece");
            return;
        }
        g_objects.clear();
        scenery::set_selection({copy});
        const V3 pivot = scenery::get_pivot();
        scenery::move_to({at.x, at.y + (pivot.y - scenery::get_bottom()), at.z});
        scenery::finalise();
        step.sectors.push_back(copy);
    } else {
        objects::GameEntity* object = object_edit::make_copy(source, at);
        if (!object) {
            hud::get_message("Couldn't copy the object");
            return;
        }
        select_objects({object});
        step.copies.push_back({object->ident_, source, object_edit::store_away(object)});
    }
    push_undo(step, "Place copy");
    thumbs::note_used(g_browser.tab, source); // (the palette's Recently used - level_editor_thumbs.h)
    adjust::ActStep placed;                   // (level_editor_adjust.h: where it went, its turn)
    placed.kind = adjust::Kind::kPlace;
    placed.quarters = g_browser.tab == 0;
    adjust::get_note(placed);
    if (!keep_open) { // (Shift+click with the arrange tools' Click places: more of it - level_editor_arrange.h)
        g_browser.open = false;
        hud::hide_panel();
    }
    hud::get_message("Placed " + object_edit::caption(source));
    note_action("placed a copy of %s (the browser)", object_edit::caption(source).c_str());
}

// Ctrl+D: the selection copied, the copies set beside it (as far across the view as it is wide) and selected.
// `repeat` (Ctrl+Shift+D - level_editor_transform.h): the copies go where the last copies' move and turn put them from
// their source (each copy turned as much), else beside it. One undo step either way.
bool duplicate_node(const TickCycle& frame, const xform::RepeatStep* repeat);
// (the last Duplicate's sources and copies, in pairs - level_editor_links.h's linked copies)
std::vector<std::pair<uint32_t, uint32_t>> g_duplicate_pairs;
void get_duplicate(const TickCycle& frame, const xform::RepeatStep* repeat = nullptr)
{
    g_duplicate_pairs.clear();
    const edit::Selected adjust_source = edit::get_selection(); // (level_editor_adjust.h: what it copied)
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    const V3 right = make_unit({frame.right.x, 0.0f, frame.right.z});
    const std::vector<objects::GameEntity*> objects = selected_objects();
    const V3 source_pos = selection_pos();
    const float source_heading = selection_heading();
    if (picked_node_index() >= 0) {
        duplicate_node(frame, repeat);
        return;
    }
    if (!objects.empty()) {
        std::vector<objects::GameEntity*> made;
        std::vector<uint32_t> sources;
        for (objects::GameEntity* object : objects) {
            if (!object_edit::can_copy(object->ident_) && !object_edit::is_copy(object->ident_))
                continue;
            uint32_t source = object->ident_;
            for (const object_edit::DupFrom& c : object_edit::get_copies()) {
                if (c.name == object->ident_)
                    source = c.source; // (a copy's copy: made from the same node)
            }
            OrientedBox box{};
            const float width =
                object_edit::model_box(object, &box) ? std::max(box.max.x - box.min.x, box.max.z - box.min.z) : 50.0f;
            const V3 at = repeat ? xform::repeat_target(object_edit::pos_of(object), *repeat)
                                 : object_edit::pos_of(object) + right * (width + 20.0f);
            objects::GameEntity* copy = object_edit::make_copy(source, at);
            if (!copy)
                continue;
            object_edit::State state = object_edit::store_away(object);
            state.id = copy->ident_;
            state.pos = copy->at_point_;
            object_edit::get_restore(state);
            made.emplace_back(copy);
            sources.push_back(source);
            g_duplicate_pairs.emplace_back(object->ident_, copy->ident_);
        }
        // (repeated: the copies turned as the last ones were, about the first copy - then their places noted)
        if (repeat && repeat->turn != 0.0f && !made.empty()) {
            const V3 pivot = object_edit::pos_of(made.front());
            for (objects::GameEntity* copy : made)
                object_edit::turn_about(copy, pivot, repeat->turn);
        }
        for (size_t i = 0; i < made.size(); ++i)
            step.copies.push_back({core::at(made, i)->ident_, core::at(sources, i), object_edit::store_away(core::at(made, i))});
        if (made.empty()) {
            hud::get_message("This object type can't be copied");
            return;
        }
        select_objects(made);
    } else if (scenery::has_selection()) {
        OrientedBox box{};
        V3 min = {}, max = {};
        bool have = false;
        for (const uint32_t checksum : scenery::get_selection()) {
            V3 a{}, b{};
            if (scenery::cuboid(checksum, &box)) {
                bounds_of(box, &a, &b);
                grow_box(&min, &max, have, a, b);
                have = true;
            }
        }
        std::vector<uint32_t> copies;
        for (const uint32_t checksum : scenery::get_selection()) {
            if (const uint32_t copy = scenery::get_duplicate(checksum)) {
                copies.push_back(copy);
                g_duplicate_pairs.emplace_back(checksum, copy);
            }
        }
        if (copies.empty()) {
            hud::get_message("Couldn't copy");
            return;
        }
        scenery::set_selection(copies);
        const float width = std::fabs((max.x - min.x) * right.x) + std::fabs((max.z - min.z) * right.z);
        if (repeat) {
            scenery::move_to(xform::repeat_target(scenery::get_pivot(), *repeat));
            if (repeat->quarters)
                scenery::get_turn(repeat->quarters);
        } else {
            scenery::move_to(scenery::get_pivot() + right * (width + 20.0f));
        }
        scenery::finalise();
        step.sectors = copies;
    } else {
        if (wall_only())
            hud::get_message("Invisible walls can't be copied");
        return;
    }
    push_undo(step, repeat ? "Repeat duplicate" : "Duplicate");
    std::vector<uint32_t> made;
    made.reserve(step.copies.size());
    for (const MadeCopy& copy : step.copies)
        made.push_back(copy.name);
    for (const uint32_t sector : step.sectors)
        made.push_back(sector);
    xform::note_duplicate(&xform::last_duplicate(), step.sectors.empty() ? xform::Kind::kObject : xform::Kind::kScenery,
                          made, source_pos, source_heading);
    hud::get_message(repeat ? "Duplicated again" : "Duplicated");
    {
        // (the panel's numbers - level_editor_adjust.h: each copy's offset from the one before, its turn, how many)
        adjust::ActStep action;
        action.kind = adjust::Kind::kDuplicate;
        action.quarters = !step.sectors.empty();
        if (repeat) {
            action.offset = repeat->offset;
            action.degrees =
                action.quarters ? static_cast<float>(repeat->quarters) * 90.0f : repeat->turn * 180.0f / kPi;
        } else {
            action.offset = selection_pos() - source_pos;
        }
        adjust::note_duplicate(adjust_source, action);
    }
    note_action("duplicated (%s): %s", repeat ? "Ctrl+Shift+D" : "Ctrl+D", describe_selection().c_str());
}

// A node the editor added (a spawn, a team spawn, a crown, a light, a sound emitter, a proximity node) copied with its
// settings and a fresh name, beside it or - repeated - where the last copy's move and turn say; one undo step. A level's
// own nodes, chain points, flag sets (named by the game) and trigger volumes (their own module's) aren't copied yet.
bool duplicate_node(const TickCycle& frame, const xform::RepeatStep* repeat)
{
    node_edit::Values values;
    if (!node_values(&values))
        return false;
    node_create::LinkChain list = node_create::at_hand();
    int at = -1;
    for (int i = 0; std::cmp_less(i, list.size()); ++i) {
        if (node_create::name_hash(core::at(list, i).name) == g_node_name)
            at = i;
    }
    node_create::Kind kind{};
    if (at < 0 || core::at(list, at).owned || core::at(list, at).like || !core::at(list, at).links.empty() ||
        !node_create::kind_of(core::at(list, at), &kind) || kind == node_create::Kind::kFlagSet) {
        hud::get_message("Only added nodes can be duplicated");
        return false;
    }
    if (const char* why = node_create::get_why_not(kind, node_create::Team::kRed)) {
        hud::get_message(why, 5.0);
        return false;
    }
    node_create::Spec spec = core::at(list, at);
    const V3 right = make_unit({frame.right.x, 0.0f, frame.right.z});
    spec.name = node_create::fresh_name(node_create::name_stem(kind), node_create::name_taken);
    spec.pos = repeat ? xform::repeat_target(values.pos, *repeat) : values.pos + right * 64.0f;
    spec.angles = values.angles;
    if (repeat && repeat->turn != 0.0f)
        spec.angles = node_edit::with_heading(spec.angles, node_edit::get_heading_degrees(spec.angles) +
                                                               repeat->turn * 180.0f / kPi);
    std::string message;
    if (!node_create::fabricate(spec, -1, &message)) {
        hud::get_message(message, 5.0);
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kAdded;
    step.exist = false;
    step.added.push_back(spec);
    step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), spec.name));
    push_undo(step);
    g_pickables.clear();
    g_hover_node = -1;
    const int index = nodes::get_find(node_create::name_hash(spec.name));
    if (index >= 0)
        select_node_only(index);
    xform::note_duplicate(&xform::last_duplicate(), xform::Kind::kElem, {node_create::name_hash(spec.name)}, values.pos,
                          values.angles.y);
    hud::get_message("Duplicated as " + spec.name);
    note_action("duplicated node %s", spec.name.c_str());
    return true;
}

// Ctrl+Shift+D: the last Ctrl+D (or repeat) done again from its copies, still selected - as far again and turned as
// much (level_editor_transform.h).
void repeat_duplicate(const TickCycle& frame)
{
    xform::Kind kind = xform::Kind::kNone;
    std::vector<uint32_t> ids;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (picked_node_index() >= 0) {
        kind = xform::Kind::kElem;
        ids.push_back(g_node_name);
    } else if (!objects.empty()) {
        kind = xform::Kind::kObject;
        for (const objects::GameEntity* object : objects)
            ids.push_back(object->ident_);
    } else if (scenery::has_selection()) {
        kind = xform::Kind::kScenery;
        ids = scenery::get_selection();
    }
    xform::RepeatStep repeat;
    if (!xform::repeat_for(xform::last_duplicate(), kind, ids, selection_pos(), selection_heading(), &repeat)) {
        hud::get_message("Nothing to repeat: duplicate first (Ctrl+D)", 4.0);
        return;
    }
    get_duplicate(frame, &repeat);
}

// An invisible wall's collision switched off / on (level_editor_walls.h: the game's WorldCell::change_live) - Delete, Delete
// again, the Inspector's Delete wall / Restore; one undo step.
void switch_wall(uint32_t wall, bool off)
{
    if (walls::disengaged(wall) == off)
        return;
    push_undo(get_snapshot(), "Wall collision");
    if (!walls::set_off(wall, off)) {
        hud::get_message("Couldn't change the wall's collision");
        return;
    }
    hud::get_message(off ? "Wall collision off" : "Wall collision on");
    note_action("invisible wall %08X: collision %s", wall, off ? "off" : "on");
}

// The Inspector's Collision checkbox on selected scenery: the pieces' collision switched off / on by itself (they stay
// drawn - level_editor_walls.h); one undo step.
void switch_scenery_collision(bool off)
{
    const std::vector<uint32_t>& sectors = scenery::get_selection();
    if (get_primary() || sectors.empty())
        return;
    UndoStep step;
    step.kind = UndoStep::Kind::kWalls;
    step.walls = walls::store_away(sectors);
    step.sectors = sectors;
    int changed = 0;
    for (const uint32_t checksum : sectors) {
        if (walls::disengaged(checksum) != off && walls::set_off(checksum, off))
            ++changed;
    }
    if (!changed)
        return;
    push_undo(step, "Collision on / off");
    std::array<char, 160> text{};
    std::snprintf(text.data(), sizeof(text), off ? "Collision off (%d piece%s)" : "Collision on (%d piece%s)", changed,
                  changed == 1 ? "" : "s");
    hud::get_message(text.data());
    note_action("scenery collision %s (%d pieces)", off ? "off" : "on", changed);
}

// Delete: scenery hidden (undoable); object copies deleted (undoable); other objects can't be.
void erase_one()
{
    if (picked_node_index() >= 0) {
        unlink_list_node(false);
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        UndoStep step;
        step.kind = UndoStep::Kind::kMade;
        step.exist = true; // (undone: they're made again)
        for (objects::GameEntity* object : objects) {
            if (!object_edit::is_copy(object->ident_))
                continue;
            uint32_t source = 0;
            for (const object_edit::DupFrom& c : object_edit::get_copies()) {
                if (c.name == object->ident_)
                    source = c.source;
            }
            step.copies.push_back({object->ident_, source, object_edit::store_away(object)});
        }
        if (step.copies.empty()) {
            hud::get_message("Only object copies can be deleted");
            return;
        }
        for (const MadeCopy& copy : step.copies)
            object_edit::remove_copy(copy.name);
        g_objects.clear();
        push_undo(step, "Delete copies");
        hud::get_message("Deleted");
        note_action("deleted object copies (Delete)");
        return;
    }
    if (const uint32_t wall = wall_only()) {
        switch_wall(wall, !walls::disengaged(wall)); // (Delete again: back on)
        return;
    }
    if (scenery::has_selection()) {
        push_undo(get_snapshot(), "Hide scenery");
        scenery::hide_selection();
        hud::get_message("Hidden");
        note_action("hid scenery (Delete)");
    }
}

// Home: the selection back where the level had it.
void get_reset()
{
    // (map3: faces selected - their flags back as the level has them, level_editor_faces.h)
    if (faces::has_selection() && faces::home_selection())
        return;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    node_edit::Values node;
    if (show_hidden_node()) {
        // (a hidden node: shown first - Home again puts it back where the level has it)
    } else if (node_values(&node)) {
        // (a rail point: with the points that move with it - its whole rail while Whole rail is on)
        const UndoStep before = get_snapshot();
        bool any = false;
        for (const uint32_t name : node_edit::moved_with(g_node_name, g_move_entire_rail)) {
            node_edit::Values now;
            const node_edit::Values* origin = node_edit::get_origin(name);
            if (origin && node_edit::get_take_in(name, &now) && !node_edit::get_same(now, *origin)) {
                node_edit::assign_value(name, *origin);
                any = true;
            }
        }
        if (any) {
            push_undo(before, "Home");
            hud::get_message("Reverted");
        } else {
            hud::get_message("Already original");
        }
        if (chains::is_ladder(g_node_name))
            run_chain_op(chains::kOpFacingHome, 0); // (a level ladder turned: its facing too - its own undo step)
        if (chains::view_of(g_node_name).speed > 0.0f)
            run_chain_op(chains::kOpSpeed, 0); // (a rail point's speed-up taken off: its own undo step)
        if (triggers::proxim_of(g_node_name))
            triggers::set_proxim(triggers::ProximEdit{g_node_name}, true); // (a proximity node's scripts / shape)
    } else if (!objects.empty()) {
        const UndoStep before = get_snapshot();
        bool any = false;
        for (const objects::GameEntity* object : objects) {
            if (const object_edit::State* origin = object_edit::get_origin(object->ident_))
                any |= object_edit::get_restore(*origin);
        }
        if (any) {
            push_undo(before, "Home");
            hud::get_message("Reverted");
        }
    } else if (uint32_t wall = objects.empty() ? wall_only() : 0) {
        // (an invisible wall: where the level has it, its collision on)
        Placement placement;
        if (walls::get_moved(wall, &placement) || walls::disengaged(wall)) {
            push_undo(get_snapshot(), "Home");
            walls::apply_states({{wall, {0, {0.0f, 0.0f, 0.0f}}, false}});
            hud::get_message("Wall reverted");
        } else {
            hud::get_message("Already original");
        }
    } else if (scenery::has_selection()) {
        push_undo(get_snapshot(), "Home");
        scenery::reset_selection();
        follow_scenery(); // (what it carries goes with it - level_editor_carry.h)
        hud::get_message("Reverted");
    }
    note_action("reset (Home): %s", describe_selection().c_str());
}

// The selection as the frame of reference: the camera turned to look at it from where it is, at that distance
// (kept while the selection moves - as the skater camera keeps the skater's size).
void focus_on_selection()
{
    if (!have_selection())
        return;
    g_free_camera = false; // (the camera follows the selection again)
    const V3 target = target_of();
    const V3 back = g_pos - target;
    const float distance = std::sqrt(get_dot(back, back));
    if (distance < 1.0f)
        return;
    g_orbit_distance = std::max(kMinOrbit, std::min(kMaxOrbit, distance));
    yaw_pitch_from_back(make_unit(back), &g_yaw, &g_pitch);
}

// Occlusion for gizmos (level_editor_picking.h's gizmo_in_front): the first occluding surface under the cursor as a depth
// along the view, and what the pick radius spans there (the slack). Alt (or a middle click) reaches past: none.
nodes::Occluder gizmo_occluder(const TickCycle& frame, V3 dir, const scenery::Hit& hit, bool everything)
{
    nodes::Occluder occluder;
    if (everything || !hit.blocked)
        return occluder;
    occluder.blocked = true;
    occluder.depth = hit.block_t_ * -get_dot(make_unit(dir), frame.back);
    occluder.units_per_pixel_ = core::at(g_viewport, 2) > 0 ? 2.0f * g_lens.tan_x_ / static_cast<float>(core::at(g_viewport, 2)) : 0.0f;
    occluder.pixels = 8.0f * pixel_scale();
    return occluder;
}

// How far along the ray a plain click may take a trigger volume (Triggers::Pick): to the first occluding surface under
// the cursor and kGizmoSlack past it - a volume standing on the floor still picks, one behind a wall doesn't.
float trigger_reach(V3 dir)
{
    scenery::Hit hit;
    scenery::get_pick(g_pos, make_unit(dir), kMaxPickDistance, &hit);
    return hit.blocked ? hit.block_t_ + picking::kGizmoSlack : 1e30f;
}

// Click-to-cycle (level_editor_picking.h): a plain click on the last click's spot (within 4 pixels at 720 lines), the
// camera where it was (a click turns it to the selection, it doesn't move it), nothing changed or dragged since and the
// filter the same - and that click took something with more under it.
bool cycle_holds(float x, float y, bool shift, bool everything)
{
    const V3 moved = g_pos - g_cycle.pos;
    return g_cycle.have && !shift && !everything && g_cycle.index >= 0 && g_cycle.order.size() > 1 &&
           picking::same_spot(x, y, g_cycle.x, g_cycle.y, 4.0f * pixel_scale()) && get_dot(moved, moved) < 1.0f &&
           g_cycle.changes == g_changes && g_cycle.filter == g_pick_filter;
}

// A plain click: the cycle's entry it takes (the hover's), or a new cycle from everything under the cursor now.
void note_click_for_cycle(float x, float y, V3 dir, bool plain)
{
    if (!plain) {
        g_cycle.have = false;
        return;
    }
    if (g_hover_cycle >= 0) {
        g_cycle.index = g_hover_cycle;
    } else {
        std::vector<picking::Layered> objects, scenery;
        for (const auto& hit : object_edit::pick_all(g_pos, dir, kMaxPickDistance))
            objects.push_back({hit.first->ident_, hit.second, false});
        scenery::Hit first;
        std::vector<scenery::Layer> layers;
        scenery::get_pick(g_pos, make_unit(dir), kMaxPickDistance, &first, &layers);
        for (const scenery::Layer& layer : layers)
            scenery.push_back({layer.checksum, layer.t, layer.passed});
        g_cycle = Cycle{};
        g_cycle.have = true;
        g_cycle.x = x;
        g_cycle.y = y;
        g_cycle.pos = g_pos;
        g_cycle.filter = g_pick_filter;
        g_cycle.order = picking::cycle_order(g_hover_node, objects, scenery, g_pick_filter);
        const objects::GameEntity* object = get_live(g_hover);
        g_cycle.index = g_hover_node >= 0 ? picking::get_index_of(g_cycle.order, picking::Choice::kNode, g_hover_node)
                        : object ? picking::get_index_of(g_cycle.order, picking::Choice::kObject, object->ident_)
                        : g_hover_sector
                            ? picking::get_index_of(g_cycle.order, picking::Choice::kScenery, g_hover_sector)
                            : -1;
    }
    if (g_cycle.index >= 0 && g_cycle.order.size() > 1) {
        std::array<char, 64> text{};
        std::snprintf(text.data(), sizeof(text), "%d of %d under cursor", g_cycle.index + 1,
                      static_cast<int>(g_cycle.order.size()));
        hud::get_message(text.data(), 2.5);
    }
}

// Ctrl+click: a part of the scenery selection dropped from it - the rail point whose gizmo is under the cursor (from the
// points carried along), else the nearest selected piece no further than what a plain click takes (a selected shadow
// or floor in front of it too: level_editor_shadows.h's drop_target). False: nothing selected there (the click goes on
// as a plain one).
bool drop_under_cursor(V3 dir)
{
    if (!scenery::has_selection())
        return false;
    if (g_hover_node >= 0 && std::cmp_less(g_hover_node, nodes::get_all().size())) {
        const uint32_t name = core::at(nodes::get_all(), g_hover_node).name;
        if (!scenery::carries_rail(name))
            return false;
        scenery::drop_rail(name);
        hud::get_message("Removed rail point " + nodes::caption(name));
        return true;
    }
    scenery::Hit first;
    std::vector<scenery::Layer> layers;
    scenery::get_pick(g_pos, make_unit(dir), kMaxPickDistance, &first, &layers);
    std::vector<bool> passed, selected;
    for (const scenery::Layer& layer : layers) {
        passed.push_back(layer.passed);
        selected.push_back(scenery::get_selected(layer.checksum));
    }
    const int index = members::drop_target(passed, selected);
    if (index < 0)
        return false;
    const uint32_t checksum = core::at(layers, index).checksum;
    scenery::flip_over(checksum);
    hud::get_message("Removed " + object_edit::caption(checksum) + " from selection");
    return true;
}

// The selection as locks: the selected objects, else the selected scenery pieces (a node by itself: none).
std::vector<picking::Lock> selection_locks()
{
    std::vector<picking::Lock> locks;
    for (const objects::GameEntity* object : selected_objects())
        locks.push_back({picking::kObjects, object->ident_});
    if (locks.empty())
        for (const uint32_t sector : scenery::get_selection())
            locks.push_back({picking::kScenery, sector});
    return locks;
}

// The view options (level_editor_view.h). H / the Inspector's Hide: the selection hidden while editing (from the view
// and from clicks) and deselected; I or / (Isolate): everything else hidden, the selection kept. Never saved.
void view_hide(bool isolate)
{
    std::vector<uint32_t> sectors, objects;
    for (const picking::Lock& piece : selection_locks())
        (piece.kind == picking::kObjects ? objects : sectors).push_back(piece.id);
    if (sectors.empty() && objects.empty()) {
        hud::get_message(isolate ? "Nothing selected to isolate" : "Nothing selected to hide");
        return;
    }
    const int count = isolate ? view::get_isolate(sectors, objects) : view::conceal(sectors, objects);
    if (!isolate)
        unpick();
    note_action("view: %s (%d hidden)", isolate ? "isolated" : "hid", count);
    std::array<char, 128> text{};
    std::snprintf(text.data(), sizeof(text), "%s %d in editor", isolate ? "Isolated: hid" : "Hid", count);
    hud::get_message(text.data());
}

// Alt+H / Show all.
void view_show_all()
{
    const int count = view::hidden_count();
    view::show_all();
    note_action("view: showed all (%d)", count);
    hud::get_message(count ? "All shown" : "Nothing hidden");
}

// C / the toolbar's Collision.
void show_collision(bool on)
{
    view::set_show_collision(on);
    if (!on)
        view::clear_wall();
    if (on && !ui::underway())
        hud::get_message("Requires the editor UI (editor_ui=1)");
    else
        hud::get_message(on ? "Collision shown" : "Collision hidden");
}

// L / the Inspector's Lock: the selection locked and deselected (every click passes it from now - the Inspector lists
// it to unlock); a selection all locked already (picked in the Outliner) unlocked; nothing selected: every lock dropped.
void toggle_lock()
{
    const std::vector<picking::Lock> locks = selection_locks();
    if (locks.empty() && !g_node_selected && !picking::get_locks().empty()) {
        // (nothing selected: every lock dropped - the old HUD has no Inspector to unlock one)
        std::array<char, 64> text{};
        std::snprintf(text.data(), sizeof(text), "Unlocked %d", static_cast<int>(picking::get_locks().size()));
        push_locks_undo(); // (one undo step - unsaved with it)
        picking::clear_locks();
        hud::get_message(text.data());
        return;
    }
    if (locks.empty()) {
        hud::get_message(g_node_selected ? "Nodes can't be locked" : "Nothing selected");
        return;
    }
    bool all = true;
    for (const picking::Lock& lock : locks)
        all = all && picking::latched(lock.kind, lock.id);
    push_locks_undo(); // (one undo step - unsaved with it)
    for (const picking::Lock& lock : locks)
        picking::set_locked(lock.kind, lock.id, !all);
    note_action("%s %d", all ? "unlocked" : "locked", static_cast<int>(locks.size()));
    if (all) {
        hud::get_message("Unlocked");
    } else {
        unpick();
        hud::get_message("Locked");
    }
}

// The camera taken to look at a point from `distance` away, from the side it looks from now.
void get_look_at(V3 target, float distance)
{
    const TickCycle frame = frame_from_yaw_pitch(g_yaw, std::min(g_pitch, -0.35f));
    g_pos = target + frame.back * distance;
    yaw_pitch_from_back(make_unit(g_pos - target), &g_yaw, &g_pitch);
}

// A key's press (true once per press).
bool squeezed(const uint8_t* keys, SDL_Scancode key, bool* was)
{
    const bool down = keys[key] != 0;
    const bool pressed = down && !*was;
    *was = down;
    return pressed;
}

// A key's press and, held, its repeats (after 0.4 s, 12 a second): the browser's list.
bool get_repeated(const uint8_t* keys, SDL_Scancode key, uint32_t* since)
{
    if (!keys[key]) {
        *since = 0;
        return false;
    }
    const uint32_t now = SDL_GetTicks();
    if (!*since) {
        *since = now;
        return true;
    }
    if (now - *since < 400)
        return false;
    *since = now - 400 + 83;
    return true;
}

// --- the nodes (level_editor_nodes.h) ---

// The node the Inspector shows: the one selected by itself, else the selected object's or scenery's own node; -1 if
// none.
int inspected_node()
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (g_node_selected) {
        if (std::cmp_less(g_node_index, nodes.size()) && core::at(nodes, g_node_index).name == g_node_name)
            return g_node_index;
        const int found = g_node_name ? nodes::get_find(g_node_name) : -1;
        if (found < 0) {
            g_node_selected = false; // (another level)
            return -1;
        }
        g_node_index = found;
        return found;
    }
    if (const objects::GameEntity* object = get_primary())
        return nodes::get_find(object->ident_);
    if (scenery::has_selection())
        return nodes::get_find(scenery::get_selection().front());
    return -1;
}

// A node picked (a gizmo or the Outliner): an object's node selects the object, a movable scenery piece's the piece,
// anything else the node itself.
void select_node(int index)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()) || g_dragging)
        return;
    const nodes::Elem& node = core::at(nodes, index);
    // (a flag is moved as its node - its object with it - so the place the game makes it at changes too)
    if (node.family == nodes::Family::kObject) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(node.name));
        OrientedBox box{};
        if (object && !(object->object_flags_ & objects::kObjectFlagDead) && object_edit::model_box(object, &box)) {
            g_node_selected = false;
            select_objects({object});
            note_action("selected %s (its node)", nodes::caption(node.name).c_str());
            return;
        }
    }
    if (node.family == nodes::Family::kScenery && !scenery::why_not_movable(node.name)) {
        g_node_selected = false;
        g_objects.clear();
        scenery::set_selection({node.name});
        note_action("selected %s (its node)", nodes::caption(node.name).c_str());
        return;
    }
    select_node_only(index);
    note_action("selected the node %s", nodes::caption(node.name).c_str());
    if (debug_enabled())
        get_log("level editor: node %s (%s) at (%.0f, %.0f, %.0f)", nodes::caption(node.name).c_str(),
                nodes::family_name(node.family), node.pos.x, node.pos.y, node.pos.z);
}

// The flag node of a flag's object (-1: not a flag's).
int flag_node_of(objects::GameEntity* object)
{
    const int index = object ? nodes::get_find(object->ident_) : -1;
    return index >= 0 && core::at(nodes::get_all(), index).family == nodes::Family::kBitMark ? index : -1;
}

// The camera taken to a node (its object or scenery: as go_to_selection).
void go_to_node(int index)
{
    select_node(index);
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()) || !core::at(nodes, index).has_pos_)
        return;
    if (!have_selection())
        get_look_at(core::at(nodes, index).pos, core::at(nodes, index).family == nodes::Family::kRail ? 500.0f : 400.0f);
}

// G: down onto the ground below it (the collision ray from just under the scenery's bottom - not to hit itself - or
// from above an object's feet).
void drop_selection()
{
    if (!have_selection() || g_dragging)
        return;
    const V3 pos = selection_pos();
    const float base = get_selection_base();
    V3 ground{};
    const V3 from = ground_ray_start(pos, base, get_primary() || picked_node_index() >= 0);
    if (ground_below(from, &ground) && ground.y != base) {
        push_undo(get_snapshot(), "Drop to ground");
        place_selection(dropped_to(pos, base, ground.y));
        note_action("dropped to the ground (G): %s", describe_selection().c_str());
        get_settle();
    }
}

// Ctrl+S: the layout saved.
void save_layout()
{
    std::string message;
    if (layout::store_away(&message)) {
        g_unsaved = false;
        g_offer_pending = false; // (the autosave is gone with the save)
    }
    hud::get_message(message, 4.0);
    note_action("saved (Ctrl+S)");
}

// Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R (`discard`): it's discarded.
void answer_autosave(bool discard)
{
    std::string message;
    if (discard) {
        layout::discard_autosave(&message);
        note_action("discarded the autosave (Ctrl+Shift+R)");
    } else {
        unpick();
        if (layout::restore_autosave(&message)) {
            g_undo.clear();
            g_redo.clear();
            mark_unsaved();
        }
        note_action("restored the autosave (Ctrl+R)");
    }
    g_offer_pending = false;
    hud::get_message(message, 4.0);
}

// Reset level: everything in the level put back as the level has it and its edited copy's files deleted
// (Layout::ResetLevel). One undo step puts it all back (this session; the steps before it are dropped - they were made
// on what the reset replaced); the files come back only with Ctrl+S.
void reset_level_now()
{
    end_drag();
    unpick();
    UndoStep step;
    step.kind = UndoStep::Kind::kLevel;
    std::string message;
    g_confirm_reset = false;
    if (!layout::reset_level(&step.layout, &message)) {
        hud::get_message(message, 4.0);
        return;
    }
    g_undo.clear();
    g_redo.clear();
    push_undo(step, "Reset level");
    g_unsaved = false; // (nothing to save: the level as shipped, no layout)
    g_offer_pending = false;
    note_action("reset the level");
    hud::get_message(message, 6.0);
}

// Ctrl+Shift+Delete / the toolbar: the UI asks in a window; the old HUD asks for the keys again within 5 seconds.
void ask_reset_level()
{
    if (ui::underway()) {
        g_confirm_reset = true;
        return;
    }
    const uint32_t now = SDL_GetTicks();
    if (g_reset_asked_at && now - g_reset_asked_at < 5000) {
        g_reset_asked_at = 0;
        reset_level_now();
        return;
    }
    g_reset_asked_at = now;
    hud::get_message("Reset level? Press Ctrl+Shift+Delete again to confirm", 5.0);
}

void toggle_help()
{
    g_help = !g_help;
    g_browser.open = false;
    if (g_help)
        show_help();
    else
        hud::hide_panel();
}

void toggle_browser()
{
    if (g_dragging)
        return;
    g_browser.open = !g_browser.open;
    g_help = false;
    if (g_browser.open)
        fill_browser();
    else
        hud::hide_panel();
}

// F in the browser: the camera to its entry.
void focus_browser_item()
{
    if (std::cmp_greater_equal(g_browser.index, g_browser.items.size()))
        return;
    const uint32_t id = core::at(g_browser.items, g_browser.index).id;
    OrientedBox box{};
    if (g_browser.tab == 0 && scenery::cuboid(id, &box)) {
        V3 a{}, b{};
        bounds_of(box, &a, &b);
        get_look_at((a + b) * 0.5f, std::max(300.0f, std::sqrt(get_dot(b - a, b - a)) * 1.5f));
    } else if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id))) {
        get_look_at(object_edit::current_centre(object), 300.0f);
    }
}

// The camera to the selection, from the side it looks from now, far enough to see it.
void go_to_selection()
{
    if (!have_selection())
        return;
    float distance = 300.0f;
    OrientedBox box{};
    if (objects::GameEntity* object = get_primary()) {
        if (object_edit::model_box(object, &box))
            distance = std::max(200.0f, std::sqrt(get_dot(box.max - box.min, box.max - box.min)) * 1.5f);
    } else if (!scenery::get_selection().empty() && scenery::cuboid(scenery::get_selection().front(), &box)) {
        V3 a{}, b{};
        bounds_of(box, &a, &b);
        distance = std::max(300.0f, std::sqrt(get_dot(b - a, b - a)) * 1.5f);
    }
    get_look_at(target_of(), std::min(distance, kMaxOrbit));
    focus_on_selection();
}

// --- the camera tools (level_editor_camera.h) and play from here (level_editor_play.h) ---

// The selection's size: a sphere round its box (an object's model, the scenery's first piece; 100 otherwise).
float selection_radius()
{
    OrientedBox box{};
    if (objects::GameEntity* object = get_primary()) {
        if (object_edit::model_box(object, &box)) {
            const V3 size = box_size(box);
            return std::max(50.0f, 0.5f * std::sqrt(get_dot(size, size)));
        }
    } else if (!scenery::get_selection().empty()) {
        V3 lo = {}, hi = {};
        bool have = false;
        for (const uint32_t sector : scenery::get_selection()) {
            if (!scenery::cuboid(sector, &box))
                continue;
            V3 a{}, b{};
            bounds_of(box, &a, &b);
            grow_box(&lo, &hi, have, a, b);
            have = true;
        }
        if (have)
            return std::max(50.0f, 0.5f * std::sqrt(get_dot(hi - lo, hi - lo)));
    }
    return 100.0f;
}

void focus_now() { focus_on_selection(); }
void deselect_now() { unpick(); }

view_cam::Rig rig_now()
{
    view_cam::Rig rig = {};
    rig.pos = &g_pos;
    rig.yaw = &g_yaw;
    rig.pitch = &g_pitch;
    rig.orbit_distance_ = &g_orbit_distance;
    rig.speed = &g_speed;
    rig.have_selection_ = have_selection();
    rig.target = rig.have_selection_ ? target_of() : g_pos;
    rig.radius = rig.have_selection_ ? selection_radius() : 100.0f;
    rig.dragging = g_dragging || g_key_moving || triggers::resizing();
    rig.lens = g_lens;
    rig.cast = &get_cast;
    rig.focus = &focus_now;
    rig.deselect = &deselect_now;
    return rig;
}

perform::Resume resume_now()
{
    perform::Resume r;
    r.pos = g_pos;
    r.yaw = g_yaw;
    r.pitch = g_pitch;
    r.speed = g_speed;
    for (const Picked& p : g_objects)
        r.objects.push_back(p.id);
    r.sectors = scenery::get_selection();
    if (g_node_selected && g_objects.empty() && r.sectors.empty())
        r.node = g_node_name;
    return r;
}

void apply_resume(const perform::Resume& r)
{
    g_pos = r.pos;
    g_yaw = r.yaw;
    g_pitch = r.pitch;
    g_speed = r.speed;
    std::vector<objects::GameEntity*> objects;
    for (const uint32_t id : r.objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (object && get_live({object, id}))
            objects.push_back(object);
    }
    if (!objects.empty()) {
        select_objects(objects);
    } else if (!r.sectors.empty()) {
        scenery::set_selection(r.sectors);
    } else if (r.node) {
        const std::vector<nodes::Elem>& nodes = nodes::get_all();
        for (int i = 0; std::cmp_less(i, nodes.size()); ++i) {
            if (core::at(nodes, i).name == r.node) {
                select_node_only(i);
                break;
            }
        }
    }
    // (the camera where it was: the selection orbited from there)
    if (have_selection()) {
        const V3 back = g_pos - target_of();
        g_orbit_distance = std::max(kMinOrbit, std::min(kMaxOrbit, std::sqrt(get_dot(back, back))));
    }
    get_log("level editor: back from play from here - the camera at (%.0f, %.0f, %.0f), %s", g_pos.x, g_pos.y, g_pos.z,
            describe_selection().c_str());
}

// What was asked for on the UI's panels (level_editor_ui.h), done as the keys would.
void apply_ui_requests(V3 dir, bool have_ray, const TickCycle& frame)
{
    const bool no_change = g_dragging || g_key_moving || g_field_editing || triggers::resizing();
    for (const ui::Request& request : ui::take_requests()) {
        switch (request.kind) {
        case ui::Request::Kind::kUndo:
            if (!no_change)
                step_history(&g_undo, &g_redo, "Undone");
            break;
        case ui::Request::Kind::kRedo:
            if (!no_change)
                step_history(&g_redo, &g_undo, "Redone");
            break;
        case ui::Request::Kind::kSave:
            if (!no_change)
                save_layout();
            break;
        case ui::Request::Kind::kRestoreAutosave:
        case ui::Request::Kind::kDiscardAutosave:
            if (!no_change)
                answer_autosave(request.kind == ui::Request::Kind::kDiscardAutosave);
            break;
        case ui::Request::Kind::kToggleHelp:
            toggle_help();
            break;
        case ui::Request::Kind::kToggleBrowser:
            toggle_browser();
            break;
        case ui::Request::Kind::kBrowserTab:
            if (g_browser.open && g_browser.tab != request.value) {
                g_browser.tab = request.value ? 1 : 0;
                g_browser.index = g_browser.top = 0;
                fill_browser();
            }
            break;
        case ui::Request::Kind::kBrowserIndex:
            if (g_browser.open && request.value >= 0 && std::cmp_less(request.value, g_browser.items.size()))
                g_browser.index = request.value;
            break;
        case ui::Request::Kind::kBrowserPlace:
            if (g_browser.open && !g_dragging)
                place_from_browser(cursor_point(dir, have_ray, frame));
            break;
        case ui::Request::Kind::kBrowserFocus:
            if (g_browser.open)
                focus_browser_item();
            break;
        case ui::Request::Kind::kDrop:
            drop_selection();
            break;
        case ui::Request::Kind::kDeselect:
            if (!g_dragging)
                unpick();
            break;
        case ui::Request::Kind::kSnapAlways:
            g_snap_always = request.value != 0;
            break;
        case ui::Request::Kind::kMemberRemove:
        case ui::Request::Kind::kMemberKeep:
            // (the Inspector's list of the scenery selection's parts: x / Keep only this)
            if (g_dragging || !scenery::has_selection())
                break;
            if (request.value) {
                if (request.kind == ui::Request::Kind::kMemberRemove) {
                    scenery::drop_rail(request.id);
                } else {
                    // (a rail point by itself: the node selected, as its gizmo would)
                    const std::vector<nodes::Elem>& nodes = nodes::get_all();
                    for (size_t i = 0; i < nodes.size(); ++i) {
                        if (core::at(nodes, i).name == request.id) {
                            unpick();
                            select_node(static_cast<int>(i));
                            break;
                        }
                    }
                }
            } else if (scenery::get_selected(request.id)) {
                if (request.kind == ui::Request::Kind::kMemberRemove)
                    scenery::flip_over(request.id);
                else
                    scenery::keep_only(request.id);
            }
            note_action("Inspector - selected: %s", describe_selection().c_str());
            break;
        case ui::Request::Kind::kSelectScenery:
            if (!g_dragging && !scenery::why_not_movable(request.id)) {
                g_objects.clear();
                scenery::set_selection({request.id});
                go_to_selection();
                note_action("selected in the Outliner: %s", describe_selection().c_str());
            }
            break;
        case ui::Request::Kind::kFamilies:
            g_families = request.id & nodes::kAllFamilies;
            config_set_int("editor", "families", static_cast<int>(g_families));
            break;
        case ui::Request::Kind::kPickFilter:
            g_pick_filter = request.id & picking::kAll;
            config_set_int("editor", "pick", static_cast<int>(g_pick_filter));
            break;
        case ui::Request::Kind::kToggleLock:
            toggle_lock();
            break;
        case ui::Request::Kind::kCollision:
            show_collision(request.value != 0);
            break;
        case ui::Request::Kind::kViewHide:
            view_hide(false);
            break;
        case ui::Request::Kind::kViewIsolate:
            view_hide(true);
            break;
        case ui::Request::Kind::kViewShowAll:
            view_show_all();
            break;
        case ui::Request::Kind::kClearWall:
            view::clear_wall();
            break;
        case ui::Request::Kind::kWallCollision:
            if (const uint32_t wall = wall_only(); wall && !no_change)
                switch_wall(wall, request.value == 0);
            break;
        case ui::Request::Kind::kSceneryCollision:
            if (!no_change)
                switch_scenery_collision(request.value == 0);
            break;
        case ui::Request::Kind::kUnlock:
            if (!no_change && picking::latched(static_cast<uint32_t>(request.value), request.id)) {
                push_locks_undo(); // (one undo step - unsaved with it)
                picking::set_locked(static_cast<uint32_t>(request.value), request.id, false);
                hud::get_message("Unlocked");
            }
            break;
        case ui::Request::Kind::kCarry:
            carry::set_enabled(request.value != 0);
            hud::get_message(request.value ? "Carry Objects on" : "Carry Objects off");
            break;
        case ui::Request::Kind::kSelectNode:
            select_node(request.value);
            if (picked_node_index() >= 0)
                focus_on_selection(); // (the camera turns to it, at the distance it is - and orbits it)
            break;
        case ui::Request::Kind::kNodeSet:
            // The Inspector's fields (typed, or dragged to scrub): one undo step until they're let go.
            if (picked_node_index() >= 0 && !g_dragging && !g_key_moving) {
                if (!g_field_editing) {
                    begin_change();
                    g_field_editing = true;
                }
                node_edit::Values values;
                values.pos = {request.a[0], request.a[1], request.a[2]};
                values.angles = {request.b[0], request.b[1], request.b[2]};
                set_selected_node(values);
                note_action("set in the Inspector: %s", describe_selection().c_str());
            }
            break;
        case ui::Request::Kind::kNodeSetDone:
            if (g_field_editing) {
                g_field_editing = false;
                end_change();
            }
            break;
        case ui::Request::Kind::kScaleSet:
            // The Inspector's Scale X / Y / Z (level_editor_scale.h): scenery or objects scaled to them (along their own
            // axes) while dragged or typed; one undo step until let go.
            if ((get_primary() || scenery::has_selection()) && picked_node_index() < 0 && !g_dragging &&
                !g_key_moving) {
                if (!g_field_editing) {
                    begin_change();
                    g_field_editing = true;
                    g_scale_at_field_start = selection_scale_now();
                }
                scale_selection_to({request.a[0], request.a[1], request.a[2]});
                if (!get_primary())
                    follow_scenery();
            }
            break;
        case ui::Request::Kind::kScaleDone:
            if (g_field_editing) {
                g_field_editing = false;
                if (!get_primary())
                    scenery::finalise();
                const V3 now = selection_scale_now();
                if (g_has_pending && (now.x != g_scale_at_field_start.x || now.y != g_scale_at_field_start.y ||
                                      now.z != g_scale_at_field_start.z)) {
                    push_undo(g_pending, "Scale (Inspector)");
                    note_action("scaled in the Inspector: %s", describe_selection().c_str());
                }
                g_has_pending = false;
            }
            break;
        case ui::Request::Kind::kReset:
            if (!no_change)
                get_reset();
            break;
        case ui::Request::Kind::kOnlineWarningSeen:
            g_online_warning_shown = false;
            break;
        case ui::Request::Kind::kGoToNode:
            go_to_node(request.value);
            if (have_selection())
                go_to_selection();
            break;
        case ui::Request::Kind::kPlaceToggle:
            g_place_open = !g_place_open;
            if (g_place_open)
                entities::refresh_palette();
            if (chain_kind_of(g_place_armed) != chains::Blit::kNone)
                stop_chain_drawing(nullptr);
            g_place_armed = -1;
            break;
        case ui::Request::Kind::kPlaceArm:
            if (chain_kind_of(g_place_armed) != chains::Blit::kNone)
                stop_chain_drawing(nullptr);
            g_place_armed = request.value;
            if (chain_kind_of(g_place_armed) != chains::Blit::kNone) {
                // (a rail / a path: drawn point by point - each click a point, Enter finishes)
                chains::start_drawing(chain_kind_of(g_place_armed));
                hud::get_message(chains::get_waiting(), 6.0);
            }
            break;
        case ui::Request::Kind::kChainOp:
            if (!no_change) {
                run_chain_op(request.value, request.id);
                if (request.value == chains::kOpExtend && chains::get_drawing() != chains::Blit::kNone) {
                    g_place_armed = -1; // (drawing on from the selected end: the palette's entry isn't armed)
                    unpick();           // (the camera flies while drawing)
                }
            }
            break;
        case ui::Request::Kind::kPlaceHere:
            if (chain_kind_of(request.value) != chains::Blit::kNone) {
                hud::get_message("Click points in the view");
                break;
            }
            if (!no_change) {
                // (where the view looks: the panel has the mouse)
                place_node(request.value, cursor_point(frame.back * -1.0f, false, frame));
                g_place_armed = -1;
            }
            break;
        case ui::Request::Kind::kRemoveNodeAction:
            if (!no_change)
                unlink_list_node(false);
            break;
        case ui::Request::Kind::kHideConfirmed: {
            const uint32_t name = g_confirm_hide;
            g_confirm_hide = 0;
            if (!no_change && name && name == g_node_name)
                unlink_list_node(true);
            break;
        }
        case ui::Request::Kind::kHideCancelled:
            g_confirm_hide = 0;
            break;
        case ui::Request::Kind::kResetLevel:
            if (!no_change)
                ask_reset_level();
            break;
        case ui::Request::Kind::kResetConfirmed:
            if (!no_change && g_confirm_reset)
                reset_level_now();
            break;
        case ui::Request::Kind::kResetCancelled:
            g_confirm_reset = false;
            break;
        case ui::Request::Kind::kReqEntireRail:
            g_move_entire_rail = request.value != 0;
            break;
        case ui::Request::Kind::kFieldSet:
        case ui::Request::Kind::kFieldRemove:
        case ui::Request::Kind::kFieldHome:
            if (!no_change)
                field_request(request);
            break;
        case ui::Request::Kind::kLevelProps:
        case ui::Request::Kind::kLevelPropsDone:
            if (!g_dragging && !g_key_moving)
                level_props_request(request);
            break;
        case ui::Request::Kind::kCrownMove:
            if (!no_change)
                move_selected_crown(request.value);
            break;
        case ui::Request::Kind::kBrowserPreview:
            g_browser.preview = request.value;
            break;
        case ui::Request::Kind::kPreviewOrbit:
            preview::intake(request.a[0], request.a[1], request.a[2]);
            break;
        case ui::Request::Kind::kSelectObject:
            if (!g_dragging) {
                if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(request.id))) {
                    select_objects({object});
                    go_to_selection();
                    note_action("selected in the Outliner: %s", describe_selection().c_str());
                }
            }
            break;
        }
    }
}

// --- UX8 (level_editor_ux8.h): wheel drill-through, click-to-teleport, walk mode ---

// The controller's right stick at full tilt: as a mouse moved this many pixels a second.
constexpr float kPadLookPixelsPerSecond = 700.0f;
bool g_drilled; // (the click-to-cycle's next entry was chosen by Alt+wheel: it stays the hover while Alt is held)

// Alt+wheel: the hover stepped `steps` deeper (negative: nearer) through everything under the cursor - the
// click-to-cycle's list (made now as a click would make it, nothing taken), its next entry set so the hover shows the
// one stepped to; a click then takes it.
void drill_through(int steps, float x, float y, V3 dir)
{
    int shown = -1;
    if (cycle_holds(x, y, false, false)) {
        shown = (g_cycle.index + 1) % static_cast<int>(g_cycle.order.size());
    } else {
        std::vector<picking::Layered> objects, scenery;
        for (const auto& hit : object_edit::pick_all(g_pos, dir, kMaxPickDistance))
            objects.push_back({hit.first->ident_, hit.second, false});
        scenery::Hit first;
        std::vector<scenery::Layer> layers;
        scenery::get_pick(g_pos, make_unit(dir), kMaxPickDistance, &first, &layers);
        for (const scenery::Layer& layer : layers)
            scenery.push_back({layer.checksum, layer.t, layer.passed});
        g_cycle = Cycle{};
        g_cycle.have = true;
        g_cycle.x = x;
        g_cycle.y = y;
        g_cycle.pos = g_pos;
        g_cycle.changes = g_changes;
        g_cycle.filter = g_pick_filter;
        g_cycle.order = picking::cycle_order(g_hover_node, objects, scenery, g_pick_filter);
        const objects::GameEntity* object = get_live(g_hover);
        shown = g_hover_node >= 0 ? picking::get_index_of(g_cycle.order, picking::Choice::kNode, g_hover_node)
                : object          ? picking::get_index_of(g_cycle.order, picking::Choice::kObject, object->ident_)
                : g_hover_sector  ? picking::get_index_of(g_cycle.order, picking::Choice::kScenery, g_hover_sector)
                                  : -1;
    }
    const int count = static_cast<int>(g_cycle.order.size());
    if (count < 2) {
        g_cycle.have = false;
        g_drilled = false;
        hud::get_message(count ? "Only one item under cursor" : "Nothing under cursor", 1.5);
        return;
    }
    const int at = nav::drill_index(shown, steps, count);
    g_cycle.index = nav::cycle_index_showing(at, count);
    g_drilled = true;
    std::array<char, 96> text{};
    std::snprintf(text.data(), sizeof(text), "%d of %d under cursor", at + 1, count);
    hud::get_message(text.data(), 2.0);
}

// Click-to-teleport: the camera glides to the surface under the cursor (nav::teleport_target), looking the same way.
void teleport_to(V3 dir)
{
    V3 hit{};
    const V3 ray = make_unit(dir);
    if (!get_cast(g_pos, g_pos + ray * kMaxPickDistance, &hit)) {
        hud::get_message("No surface under cursor");
        return;
    }
    if (have_selection() || g_node_selected)
        unpick(); // (the camera would orbit it)
    view_cam::get_cancel();
    ux8::start_glide(g_pos, nav::teleport_target(g_pos, hit));
    note_action("teleported to (%.0f, %.0f, %.0f)", hit.x, hit.y, hit.z);
}

// Walk mode's frame: the mouse (captured) and the right stick look, the fly keys / left stick walk, the fly-up key /
// A jump, Shift / L3 run; Escape (or B) stops.
void walk_frame(const Mouse& input, const uint8_t* keys, const ux8::Pad& pad, double seconds)
{
    if (g_cancel_requested || (pad.active && pad.b)) {
        g_cancel_requested = false;
        ux8::get_cancel();
        set_relative(false);
        return;
    }
    set_relative(true);
    const prefs::Values& prefs = prefs::get_pull();
    const float dx = input.rel_x_ + pad.look_x_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
    const float dy = input.rel_y_ - pad.look_y_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
    get_look(&g_yaw, &g_pitch, dx, prefs.invert_y_ ? -dy : dy,
             kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0f);
    static bool g_jump_was;
    const bool jump_key = actions::get_held(keys, actions::kFlyUp);
    nav::WalkInput in;
    in.forward = static_cast<float>((actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kBack) ? 1 : 0)) +
                 pad.move_y_;
    in.right = static_cast<float>((actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                  (actions::get_held(keys, actions::kLeft) ? 1 : 0)) +
               pad.move_x_;
    in.jump = (jump_key && !g_jump_was) || pad.a_presses_ > 0;
    in.run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] || pad.fast;
    g_jump_was = jump_key;
    nav::Walker& walker = ux8::walker_state();
    nav::get_step(&walker, g_yaw, in, static_cast<float>(seconds), get_cast);
    g_pos = nav::eye_of(walker);
    view_cam::after_update(rig_now(), 0, true);
}

// The transform tools' keys (level_editor_transform.h): the modes (wasd_flies=0: W / E / R while the right button isn't
// held; both: Space cycles - with wasd_flies=1 only with a selection, Space flying up without one), ` World / Local,
// Alt+G the modal transform; F (UX1's frame) lets the camera follow the selection again.
void transform_keys(const uint8_t* keys, bool ctrl, bool shift, bool alt, const Mouse& input, V3 dir, bool have_ray,
                    const TickCycle& frame)
{
    // (the keys from the keymap - level_editor_actions.h: W / E / R, Space, `, Alt+G, F)
    (void)keys;
    const bool w = actions::squeezed(actions::kToolMove);
    const bool e = actions::squeezed(actions::kToolRotate);
    const bool r = actions::squeezed(actions::kToolScale);
    const bool space = actions::squeezed(actions::kToolCycle);
    const bool grave = actions::squeezed(actions::kToolSpace);
    const bool g = actions::squeezed(actions::kModalTransform);
    const bool f = actions::squeezed(actions::kFrameSelection);
    if (g_dragging || g_browser.open || ctrl || g_field_editing)
        return;
    xform::Settings& settings = xform::at_hand();
    const xform::Mode before = settings.mode;
    const bool always = xform::fly_always();
    (void)shift, (void)alt;
    if (!always && !input.right) {
        if (w)
            settings.mode = xform::Mode::kShiftBy;
        if (e)
            settings.mode = xform::Mode::kSpinBy;
        if (r)
            settings.mode = xform::Mode::kScale;
    }
    if (space && !input.right && (!always || have_selection()))
        settings.mode = xform::next_mode(settings.mode);
    if (settings.mode != before) {
        const char* why = settings.mode == xform::Mode::kSpinBy && have_selection() && gizmo_mode() != settings.mode
                              ? " (selection can't rotate)"
                          : settings.mode == xform::Mode::kScale && have_selection() && gizmo_mode() != settings.mode
                              ? " (selection can't scale)"
                              : "";
        hud::get_message(std::string(xform::mode_name(settings.mode)) + " Tool" + why);
    }
    if (grave) {
        settings.space = settings.space == xform::Space::kWorld ? xform::Space::kLocal : xform::Space::kWorld;
        hud::get_message(settings.space == xform::Space::kWorld ? "World Space" : "Local Space");
    }
    // (F - UX1's frame, level_editor_camera.h - flies the camera to the selection: it follows it again after)
    if (f && have_selection())
        g_free_camera = false;
    // Alt+G: the tool's transform without holding the mouse (Blender's G / R / S).
    if (g && have_selection() && have_ray) {
        g_axis = -1;
        g_drag_mode = gizmo_mode();
        g_modal = true;
        g_typed = {};
        g_dragging = true;
        g_drag_start = selection_pos();
        g_press_x = input.x;
        g_press_y = input.y;
        g_drag_moved = true;
        begin_change();
        start_drag(g_pos, dir, false, frame, input.x, input.y);
        hud::get_message(std::string(xform::mode_name(g_drag_mode)) + ": X/Y/Z constrain, type a value, Enter applies",
                         4.0);
    }
}

} // namespace

void tick_frame()
{
    budget::get_observe(); // (UX5: the budget's baseline - the level up, before its edited copy's layout goes in)
    layout::tick();
    triggers::get_guard();       // (no editor trigger volumes in a network session)
    triggers::perform(g_active); // (exit triggers: the skater leaving one, in play)
    // (emitters' sounds, path speeds, objects made again, the path preview - level_editor_entities.h)
    {
        const objects::GameEntity* primary = g_active ? get_primary() : nullptr;
        entities::tick(g_active, primary ? primary->ident_ : 0, g_dragging);
    }
    chains::tick(); // (peds / cars made again after their path changed; old rail node arrays freed)
    if (!g_active)
        return;
    // (the UI runs: the HUD's elements - made by level_editor_enter after LevelEditor_Start - taken down, the first
    // few frames)
    if (ui::underway() && g_hud_taken_down < 4) {
        ++g_hud_taken_down;
        script::launch_script("level_editor_hud_destroy", nullptr, nullptr, false);
    }
    update_hud();
    // The autosave: the snapshot made again after each change (the frame after: the change is complete), written once a
    // minute while there are unsaved changes; an older autosave offered once the HUD is up.
    if (g_unsaved && g_changes != g_snapshot_changes && layout::get_snapshot())
        g_snapshot_changes = g_changes;
    if (g_unsaved && SDL_GetTicks() - g_last_autosave >=
                         static_cast<uint32_t>(prefs::get_pull().autosave_seconds_) * 1000u) { // ([editor] autosave_s)
        g_last_autosave = SDL_GetTicks();
        layout::write_autosave();
    }
    if (g_offer_show) {
        g_offer_show = false;
        hud::get_message("Autosave found: Ctrl+R restores, Ctrl+Shift+R discards", 20.0);
    }
    const uint64_t now = SDL_GetPerformanceCounter();
    double seconds =
        g_last_counter ? static_cast<double>(now - g_last_counter) / static_cast<double>(SDL_GetPerformanceFrequency())
                       : 0.0;
    g_last_counter = now;
    seconds = std::min(seconds, 0.1);
    Mouse input = g_mouse;
    g_mouse.rel_x_ = g_mouse.rel_y_ = 0.0f;
    g_mouse.wheel = 0;
    g_mouse.left_presses_ = g_mouse.left_releases_ = 0;
    g_mouse.middle_presses_ = 0;
    if (!g_camera)
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    // (the UI is typing: the editor's keys rest - level_editor_ui.h)
    static const std::array<uint8_t, SDL_NUM_SCANCODES> kNoKeys = {};
    if (!ui::keys_to_editor())
        keys = kNoKeys.data();
    // (UX8: a game controller in controller mode - level_editor_ux8.h: A as the left button at the crosshair, the
    // D-pad as the nudge keys, L3 as Shift, B as Escape; the sticks below)
    const ux8::Pad pad = ux8::read_pad();
    bool pad_cursor = false;
    if (pad.active) {
        keys = ux8::pad_keys(keys, pad);
        input.left = input.left || pad.a_down_;
        input.left_presses_ += pad.a_presses_;
        input.left_releases_ += pad.a_releases_;
        float cx = 0.0f, cy = 0.0f;
        pad_cursor = ux8::get_pad_cursor(pad, input.rel_x_ != 0.0f || input.rel_y_ != 0.0f, &cx, &cy) && !input.right;
        if (pad_cursor) {
            input.x = cx;
            input.y = cy;
        }
        if (ux8::pad_cancel(pad))
            g_cancel_requested = true;
    }
    const bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    const bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    const bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    const double boost = shift ? 4.0 : 1.0;
    if (!have_selection())
        g_dragging = false;
    // The camera tools (level_editor_camera.h): Alt+drag orbits, the wheel with the right button the flying speed.
    view_cam::filter_mouse(rig_now(),
                           {&input.left_presses_, &input.left_releases_, input.left, input.right, &input.rel_x_,
                            &input.rel_y_, &input.wheel},
                           SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_LALT] ||
                               SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_RALT],
                           g_last_have_ray, g_last_ray);
    // The keys from the keymap (level_editor_actions.h: rebindable, [editor_keys] in the ini; the command search runs
    // them the same way).
    actions::begin_frame(keys);
    // Play from here (F5, Ctrl+P - level_editor_play.h); the preferences (Ctrl+, - level_editor_prefs.h).
    if (actions::squeezed(actions::kPlay) && !g_dragging)
        perform::get_request();
    if (actions::squeezed(actions::kPreferences) && ui::underway())
        prefs::unseal() = !prefs::unseal();
    // Simulate (F6) and the Trail window (F7) - level_editor_playtest.h.
    play_test::tick(g_dragging);
    // Walk mode (UX8 - level_editor_ux8.h: Shift+`): the camera at a skater's eye height; the editor's other keys and
    // clicks rest meanwhile.
    if (ux8::take_walk_toggle() && !g_dragging) {
        const bool on = !ux8::on_foot();
        if (on)
            unpick();
        ux8::set_walking(on, g_pos);
    }
    if (ux8::on_foot()) {
        walk_frame(input, keys, pad, seconds);
        return;
    }

    // Escape: a drag put back, else the browser or the help closed, else the selection dropped.
    if (g_cancel_requested) {
        g_cancel_requested = false;
        // NOLINTNEXTLINE(bugprone-branch-clone): the cases kept apart, one per kind
        if (ux8::get_cancel()) {
            // (UX8: a pie menu closed)
        } else if (!g_dragging && ux9::get_cancel()) {
            // (UX9: the gap wizard / the ground tool given up first - level_editor_ux9.h)
        } else if (triggers::resizing()) {
            triggers::end_resize(); // (kept as far as it went - Ctrl+Z puts the size back)
        } else if (g_dragging) {
            // (a turn, a scale or an object stood on a slope: the step taken when it started put back -
            // level_editor_transform.h)
            if (g_has_pending && (g_drag_mode != xform::Mode::kShiftBy || g_pending_forced)) {
                carry::disarm();
                get_restore(g_pending);
            } else {
                place_selection(g_drag_start);
            }
            get_settle();
            g_dragging = false;
            g_axis = -1;
            g_has_pending = false;
            g_pending_forced = false;
            g_drag_mode = xform::Mode::kShiftBy;
            g_modal = false;
            g_typed = {};
            g_vertex_have = false;
            focus_on_selection();
            hud::get_message("Cancelled");
        } else if (g_confirm_reset) {
            g_confirm_reset = false;
        } else if (g_confirm_hide) {
            g_confirm_hide = 0;
        } else if (chains::get_drawing() != chains::Blit::kNone || chains::wants_pick()) {
            stop_chain_drawing(chains::wants_pick() ? "Cancelled" : "Drawing finished");
        } else if (g_place_armed >= 0) {
            g_place_armed = -1;
        } else if (g_place_open) {
            g_place_open = false;
        } else if (g_browser.open) {
            g_browser.open = false;
            hud::hide_panel();
        } else if (g_help) {
            g_help = false;
            hud::hide_panel();
        } else if (have_selection() || g_node_selected) {
            unpick();
        } else if (faces::has_selection()) {
            faces::clear_selection(); // (map3)
        } else if (faces::get_picking()) {
            faces::set_picking(false);
            hud::get_message("Face Select off");
        }
    }

    static bool g_enter_key, g_focus_key, g_left_key, g_right_key;
    static uint32_t g_up_since, g_down_since, g_page_up_since, g_page_down_since;
    const bool no_change = g_dragging || g_key_moving || g_field_editing || triggers::resizing();
    // (the history, changes, search, selection tools and clipboards - level_editor_ux3.h)
    ux3::tick_frame();
    // (UX8: quick selection sets, the follow camera's key, the pie menus - a pie up takes the clicks)
    {
        ux8::TickCycle ux8;
        ux8.mouse_x_ = input.x;
        ux8.mouse_y_ = input.y;
        ux8.left_presses_ = &input.left_presses_;
        ux8.left_releases_ = &input.left_releases_;
        ux8.right = input.right;
        ux8.dragging = g_dragging;
        ux8.browser = g_browser.open;
        ux8.keys = keys;
        ux8.pad = pad;
        ux8.have_selection_ = have_selection() || g_node_selected;
        ux8::tick_frame(ux8);
    }

    // The panels: F1 the help, B the browser.
    if (actions::squeezed(actions::kHelp))
        toggle_help();
    if (actions::squeezed(actions::kBrowser) && !g_dragging)
        toggle_browser();
    if (actions::squeezed(actions::kPlace) && !g_dragging) {
        if (ui::underway()) {
            g_place_open = !g_place_open;
            g_place_armed = -1;
            if (g_place_open)
                entities::refresh_palette(); // (objects made since show up)
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }

    // Ctrl+ keys: undo / redo, save, duplicate.
    if (!no_change) {
        if (actions::squeezed(actions::kUndo))
            step_history(&g_undo, &g_redo, "Undone");
        if (actions::squeezed(actions::kRedo))
            step_history(&g_redo, &g_undo, "Redone");
        if (actions::squeezed(actions::kSave))
            save_layout();
        // Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R: it's discarded.
        if (actions::squeezed(actions::kRestoreAutosave) || actions::squeezed(actions::kDiscardAutosave))
            answer_autosave(actions::squeezed(actions::kDiscardAutosave));
        // (Ctrl+Shift+D: the last Ctrl+D repeated - its copies' move and turn since made again from them,
        // level_editor_transform.h)
        if (actions::squeezed(actions::kRepeatDuplicate))
            repeat_duplicate(frame_from_yaw_pitch(g_yaw, g_pitch));
        else if (actions::squeezed(actions::kDuplicate))
            get_duplicate(frame_from_yaw_pitch(g_yaw, g_pitch));
    }
    if (ctrl && !no_change && have_selection())
        focus_on_selection();
    // L: with a rail point selected, it moves with its whole rail or by itself (level_editor_node_edit.h); otherwise
    // the selection locked (clicks pass it - level_editor_picking.h) or, all locked, unlocked.
    const int l_node = picked_node_index();
    // (a rail's, a ladder's or a path's point: its whole chain or itself - level_editor_chains.h)
    const bool rail_point = l_node >= 0 && node_edit::is_chain(core::at(nodes::get_all(), l_node).name);
    const bool l_pressed = actions::squeezed(actions::kLock);
    if (l_pressed && !rail_point && !g_dragging)
        toggle_lock();
    if (l_pressed && rail_point && !no_change) {
        g_move_entire_rail = !g_move_entire_rail;
        hud::get_message(g_move_entire_rail ? "Move whole chain" : "Move single point");
    }
    // C: the collision shown / hidden; H: the selection hidden (Alt+H: everything shown); I or /: isolated
    // (level_editor_view.h - while editing only).
    if (actions::squeezed(actions::kOcclusion) && !g_browser.open) {
        g_occlusion_off = !g_occlusion_off;
        render::occlusion_off() = g_occlusion_off;
        hud::get_message(g_occlusion_off ? "Occlusion culling off" : "Occlusion culling on");
    }
    if (actions::squeezed(actions::kCollision) && !g_browser.open)
        show_collision(!view::show_collision());
    if (actions::squeezed(actions::kShowAll) && !no_change && !g_browser.open)
        view_show_all();
    if (actions::squeezed(actions::kHide) && !no_change && !g_browser.open)
        view_hide(false);
    if (actions::squeezed(actions::kIsolate) && !no_change && !g_browser.open)
        view_hide(true);
    view::tick();
    // The view menus' keys (level_editor_overlays.h): Alt+1..4 the view mode (Lit, Unlit, Wireframe, Collision only);
    // Y the jump arcs (level_editor_arcs.h); M the ruler (level_editor_ruler.h), Shift+M clears its measurement.
    // (the keys from the keymap - level_editor_actions.h)
    for (int mode = 0; mode < std::min(overlays::kViewModes, 4); ++mode) {
        if (actions::squeezed(actions::kViewModeLit + mode) &&
            !g_dragging) { // (while dragging digits are typed values)
            overlays::change_view_mode(mode);
            hud::get_message(std::string("View: ") + overlays::view_mode_name(mode));
        }
    }
    if (actions::squeezed(actions::kJumpArcs) && !g_browser.open) {
        const bool on = !overlays::get_shown(overlays::kJumpArcs);
        overlays::assign_value(overlays::kJumpArcs, on);
        hud::get_message(on ? "Jump Arcs on" : "Jump Arcs off");
    }
    const bool ruler_clear = actions::squeezed(actions::kRulerClear);
    if ((actions::squeezed(actions::kRuler) || ruler_clear) && !g_browser.open && !g_dragging) {
        if (ruler_clear) {
            ruler::get_clear();
            hud::get_message("Ruler cleared");
        } else {
            ruler::set_armed(!ruler::get_armed());
            if (ruler::get_armed())
                overlays::assign_value(overlays::kRuler, true);
            hud::get_message(ruler::get_armed() ? "Ruler: click two points" : "Ruler off");
        }
    }
    // T: the trigger volumes shown (and the Triggers window) - level_editor_triggers.h; their changes, undo steps and
    // selections.
    if (actions::squeezed(actions::kTriggers) && !g_browser.open) {
        if (ui::underway()) {
            triggers::set_shown(!triggers::get_shown());
            hud::get_message(triggers::get_shown() ? "Trigger volumes shown" : "Trigger volumes hidden");
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
    {
        const TickCycle view = frame_from_yaw_pitch(g_yaw, g_pitch);
        triggers::tick(g_pos, view.back * -1.0f, g_yaw);
        // (an editor trigger selected by itself: its resize handles)
        triggers::set_selected(picked_node_index() >= 0 && triggers::has_handles(g_node_name) ? g_node_name : 0);
        UndoStep step;
        if (triggers::take_undo(&step.triggers)) {
            step.kind = UndoStep::Kind::kTriggers;
            push_undo(step, "Trigger volume");
        }
        UndoStep entities;
        if (entities::take_undo(&entities.entities)) {
            entities.kind = UndoStep::Kind::kEntities;
            push_undo(entities, "Path speed / sound");
        }
        int select = triggers::take_select();
        if (select < 0)
            select = goal_edit::take_select(); // (a goal just made / a point chosen in its section)
        if (select >= 0 && !g_dragging) {
            g_pickables.clear();
            g_hover_node = -1;
            select_node_only(select);
        }
        if (const uint32_t wall = triggers::take_wall_select(); wall && !g_dragging) {
            unpick();
            view::select_wall(wall); // (a level trigger volume moved as an invisible wall - level_editor_walls.h)
        }
    }
    walls::tick(); // (scenery whose collision is switched off kept off - level_editor_walls.h)
    // (map3) K: Faces mode - clicks take collision faces (level_editor_faces.h); V: a camera node made from the view
    // (level_editor_cameras.h).
    if (actions::squeezed(actions::kFaces) && !g_browser.open && !no_change) {
        if (ui::underway()) {
            faces::set_picking(!faces::get_picking());
            hud::get_message(faces::get_picking() ? "Face Select on" : "Face Select off");
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
    if (actions::squeezed(actions::kCameraNode) && !g_browser.open && !no_change)
        cameras::create_from_view();
    faces::tick_frame();
    clusters::tick_frame();
    cameras::tick_frame();
    goal_edit::tick_frame();
    checks::tick_frame(); // (UX5: the Problems window's Go to / Fix - level_editor_checks.h)
    notes::tick_frame();  // (UX5: note pins - level_editor_notes.h)
    ux9::tick_frame();    // (UX9: Alt+J / Alt+K / Alt+U and the forms - level_editor_ux9.h)
    // P: the camera written to the log (position, back, up).
    if (actions::squeezed(actions::kProblems))
        checks::toggle_window(); // (UX5) Alt+P: the Problems window (Hammer's Check for Problems key)
    if (actions::squeezed(actions::kLogCamera)) {
        const TickCycle view = frame_from_yaw_pitch(g_yaw, g_pitch);
        const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
        get_log("level editor: camera in level %d (scene %s), yaw %.1f, pitch %.1f - as a kViews entry:",
                render::current_level(), object_edit::caption(scene).c_str(), g_yaw * 180.0f / kPi,
                g_pitch * 180.0f / kPi);
        get_log("    {{%.3ff, %.3ff, %.3ff}, {%.6ff, %.6ff, %.6ff}, {%.6ff, %.6ff, %.6ff}},", g_pos.x, g_pos.y, g_pos.z,
                view.back.x, view.back.y, view.back.z, view.up.x, view.up.y, view.up.z);
        hud::get_message("Camera position logged");
    }
    if (actions::squeezed(actions::kDeselect) && (have_selection() || g_node_selected) && !g_dragging)
        unpick();
    // (Home and Delete: not on what the Outliner's lock holds - level_editor_organize.h)
    if (actions::squeezed(actions::kHome) && !no_change && !g_browser.open && !organize::selection_locked(true))
        get_reset();
    if (!no_change && !g_browser.open && !g_confirm_hide && !g_confirm_reset) {
        if (actions::squeezed(actions::kResetLevel))
            ask_reset_level(); // (Ctrl+Shift+Delete: Reset level)
        else if (actions::squeezed(actions::kDelete) && !organize::selection_locked(true))
            erase_one();
    }
    // A rail's / path's point selected (level_editor_chains.h): Insert puts a point halfway to the next, End draws on
    // from this end, J links this end to the next end clicked, U unlinks it from its next.
    const bool insert_now = actions::squeezed(actions::kChainInsert);
    const bool end_now = actions::squeezed(actions::kChainExtend);
    const bool join_now = actions::squeezed(actions::kChainLink);
    const bool unlink_now = actions::squeezed(actions::kChainUnlink);
    if (g_node_selected && !no_change && !g_browser.open && chains::is_chain_node(g_node_name)) {
        if (insert_now)
            run_chain_op(chains::kOpInsert, 0);
        if (unlink_now)
            run_chain_op(chains::kOpUnlink, 0);
        if (join_now)
            run_chain_op(chains::kOpLinkArm, 0);
        if (end_now) {
            run_chain_op(chains::kOpExtend, 0);
            if (chains::get_drawing() != chains::Blit::kNone) {
                g_place_armed = -1;
                unpick();
            }
        }
    }

    // Drawing a chain (level_editor_chains.h): Enter or a right click (pressed and let go without looking about)
    // finishes it.
    static bool g_right_was;
    static float g_right_travel;
    if (input.right) {
        if (!g_right_was)
            g_right_travel = 0.0f;
        g_right_travel += std::fabs(input.rel_x_) + std::fabs(input.rel_y_);
    }
    const bool right_click = g_right_was && !input.right && g_right_travel < 6.0f;
    g_right_was = input.right;
    const bool enter_now = actions::squeezed(actions::kChainFinish);
    if (chains::get_drawing() != chains::Blit::kNone && (enter_now || right_click) && !g_browser.open)
        stop_chain_drawing("Drawing finished");

    // Looking (right button held): about the camera, or orbiting the selection.
    set_relative(input.right || view_cam::get_orbiting());
    if (input.right) {
        const prefs::Values& prefs = prefs::get_pull(); // ([editor] look_pct, invert_y)
        get_look(&g_yaw, &g_pitch, input.rel_x_, prefs.invert_y_ ? -input.rel_y_ : input.rel_y_,
                 kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0f);
    }
    const bool pad_sticks = pad.active && !ux8::pie_open();
    if (pad_sticks && (pad.look_x_ != 0.0f || pad.look_y_ != 0.0f)) {
        // (UX8: the controller's right stick looks - orbits a selection, as the right button does)
        const prefs::Values& prefs = prefs::get_pull();
        const float px = pad.look_x_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
        const float py = -pad.look_y_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
        get_look(&g_yaw, &g_pitch, px, prefs.invert_y_ ? -py : py,
                 kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0f);
    }
    view_cam::apply_orbit(rig_now());
    const TickCycle frame = frame_from_yaw_pitch(g_yaw, g_pitch);
    if (pad_sticks && (pad.move_x_ != 0.0f || pad.move_y_ != 0.0f || pad.up_down_ != 0.0f)) {
        // (UX8: the left stick flies, the triggers down / up - with a selection the camera leaves it, as the right
        // button's flying does; F frames it again)
        const V3 move = frame.back * -pad.move_y_ + frame.right * pad.move_x_ + V3{0.0f, pad.up_down_, 0.0f};
        g_pos = g_pos + move * static_cast<float>(g_speed * (pad.fast ? 4.0 : 1.0) * seconds);
        if (have_selection())
            g_free_camera = true;
        if (g_dragging && pad_cursor)
            g_drag_moved = true; // (the crosshair stays put: the camera's move is the drag's)
        ux8::stop_glide();
    }
    // (UX8: a teleport's glide - a double click on a surface; flying or looking ends it)
    if (input.right)
        ux8::stop_glide();
    ux8::glide(&g_pos, seconds);

    // The mouse ray (once the view has been drawn): the cursor in back-buffer pixels too (the arrows).
    bool have_ray = false;
    V3 dir = frame.back * -1.0f;
    float back_x = 0.0f, back_y = 0.0f;
    if (g_have_view && !input.right && input.window_width_ > 0 && input.window_height_ > 0 &&
        (ui::mouse_to_editor() || pad_cursor)) {
        const mouse_menus::ScreenMapping mapping =
            mouse_menus::current_mapping(input.window_width_, input.window_height_);
        back_x = static_cast<float>(static_cast<double>(input.x) * mapping.back_buffer_width_ / input.window_width_);
        back_y = static_cast<float>(static_cast<double>(input.y) * mapping.back_buffer_height_ / input.window_height_);
        float nx = 0.0f, ny = 0.0f;
        if (pixel_to_ndc(g_viewport.data(), back_x, back_y, &nx, &ny)) {
            dir = ray_direction(frame, g_lens, nx, ny);
            have_ray = true;
        }
    }

    g_cursor_x = back_x;
    g_cursor_y = back_y;
    apply_ui_requests(dir, have_ray, frame);
    transform_keys(keys, ctrl, shift, alt, input, dir, have_ray, frame);
    // (UX7: the arrange tools' keys, the adjust panel's changes, the linked copies - level_editor_arrange.h,
    // level_editor_adjust.h, level_editor_links.h)
    arrange::tick_frame();
    adjust::tick();
    links::tick();
    if (!g_dragging) // (digits typed while dragging aren't bookmarks - level_editor_transform.h)
        view_cam::get_keys(rig_now(), keys, ctrl, g_browser.open); // (F, 1..9, the numpad - level_editor_camera.h)
    g_last_have_ray = have_ray;
    g_last_ray = dir;
    // (UX8: Alt+wheel steps what a click would take through everything under the cursor; the command search's
    // Teleport to the cursor)
    if (alt && input.wheel && !g_dragging && !input.right && have_ray && !view_cam::get_orbiting() && !g_browser.open) {
        drill_through(-input.wheel, input.x, input.y, dir);
        input.wheel = 0;
    }
    if (ux8::take_teleport() && have_ray && !g_dragging)
        teleport_to(dir);

    if (g_browser.open) {
        // The browser takes the arrows, Enter and F (the selection's keys rest meanwhile).
        const int count = static_cast<int>(g_browser.items.size());
        const int move = (get_repeated(keys, SDL_SCANCODE_DOWN, &g_down_since) ? 1 : 0) -
                         (get_repeated(keys, SDL_SCANCODE_UP, &g_up_since) ? 1 : 0) +
                         (get_repeated(keys, SDL_SCANCODE_PAGEDOWN, &g_page_down_since) ? kBrowserRows : 0) -
                         (get_repeated(keys, SDL_SCANCODE_PAGEUP, &g_page_up_since) ? kBrowserRows : 0);
        if (move && count)
            g_browser.index = std::max(0, std::min(count - 1, g_browser.index + move));
        const bool left = squeezed(keys, SDL_SCANCODE_LEFT, &g_left_key);
        const bool right = squeezed(keys, SDL_SCANCODE_RIGHT, &g_right_key);
        if (left || right) {
            g_browser.tab = 1 - g_browser.tab;
            g_browser.index = g_browser.top = 0;
            fill_browser();
        }
        if (squeezed(keys, SDL_SCANCODE_F, &g_focus_key) && g_browser.index < count)
            focus_browser_item();
        if (squeezed(keys, SDL_SCANCODE_RETURN, &g_enter_key) || squeezed(keys, SDL_SCANCODE_KP_ENTER, &g_enter_key))
            place_from_browser(cursor_point(dir, have_ray, frame));
        if (g_browser.open)
            show_browser();
    }

    const bool selection_held = have_selection();
    // (the Outliner's lock - level_editor_organize.h: what it holds isn't moved, turned, scaled or dropped)
    const bool change_locked = selection_held && organize::selection_locked(false);
    if (selection_held && !g_browser.open) {
        // The selection moves with the keys - W/S along the camera's heading, A/D across it, Q/E (R, Page
        // Up/Down) down/up - and the camera follows it.
        const V3 forward = make_unit({-frame.back.x, 0.0f, -frame.back.z});
        const V3 right = make_unit({frame.right.x, 0.0f, frame.right.z});
        int f = 0, r = 0, u = 0;
        if (xform::fly_always()) {
            // ([editor] wasd_flies=1: the fly keys nudge it as well as the arrows - Space aside, it cycles the tool)
            f = (actions::get_held(keys, actions::kForward) || actions::get_held(keys, actions::kNudgeForward) ? 1
                                                                                                               : 0) -
                (actions::get_held(keys, actions::kBack) || actions::get_held(keys, actions::kNudgeBack) ? 1 : 0);
            r = (actions::get_held(keys, actions::kRight) || actions::get_held(keys, actions::kNudgeRight) ? 1 : 0) -
                (actions::get_held(keys, actions::kLeft) || actions::get_held(keys, actions::kNudgeLeft) ? 1 : 0);
            u = (actions::get_held(keys, actions::kNudgeUp) ||
                         (actions::get_held(keys, actions::kFlyUp) && !keys[SDL_SCANCODE_SPACE])
                     ? 1
                     : 0) -
                (actions::get_held(keys, actions::kNudgeDown) || actions::get_held(keys, actions::kFlyDown) ? 1 : 0);
        } else {
            // ([editor] wasd_flies=0 - level_editor_transform.h: the arrow keys nudge it, Page Up / Down up / down;
            // W A S D / Q E fly the camera while the right button is held - away from the selection, F frames it again)
            f = (actions::get_held(keys, actions::kNudgeForward) ? 1 : 0) -
                (actions::get_held(keys, actions::kNudgeBack) ? 1 : 0);
            r = (actions::get_held(keys, actions::kNudgeRight) ? 1 : 0) -
                (actions::get_held(keys, actions::kNudgeLeft) ? 1 : 0);
            u = (actions::get_held(keys, actions::kNudgeUp) ? 1 : 0) -
                (actions::get_held(keys, actions::kNudgeDown) ? 1 : 0);
            if (input.right && !ctrl && !g_dragging) {
                const FlyInput fly = {(actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                          (actions::get_held(keys, actions::kBack) ? 1 : 0),
                                      (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                          (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                                      (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                          (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
                if (fly.forward || fly.right || fly.up) {
                    g_free_camera = true;
                    g_pos = get_fly(g_pos, frame, fly, static_cast<float>(g_speed * boost * seconds));
                }
            }
        }
        if (ctrl || alt || change_locked)
            f = r = u = 0; // (Ctrl+S, Ctrl+D; Alt+G / Alt+H...; the Outliner's lock)
        if ((f || r || u) && change_locked)
            organize::selection_locked(true);
        if ((f || r || u) && !g_dragging && !g_field_editing) {
            if (!g_key_moving)
                begin_change();
            auto step = static_cast<float>(kObjectSpeed * boost * seconds);
            const V3 pos = selection_pos();
            place_selection({pos.x + (forward.x * static_cast<float>(f) + right.x * static_cast<float>(r)) * step,
                             pos.y + static_cast<float>(u) * step,
                             pos.z + (forward.z * static_cast<float>(f) + right.z * static_cast<float>(r)) * step});
            g_key_moving = true;
        } else {
            if (g_key_moving) {
                const uint64_t serial = g_undo.empty() ? 0 : g_undo.back().serial;
                end_change();
                get_settle();
                if (!g_undo.empty() && g_undo.back().serial != serial) // (level_editor_adjust.h)
                    note_for_adjust(xform::Mode::kShiftBy, selection_pos() - g_pending.where, true);
            }
            g_key_moving = false;
        }
        // Z / X: turn it - an object 15 degrees, scenery a quarter turn about its centre (the engine turns level
        // geometry by quarter turns only).
        int turn = (actions::squeezed(actions::kTurnRight) ? 1 : 0) - (actions::squeezed(actions::kTurnLeft) ? 1 : 0);
        if (turn && organize::selection_locked(true))
            turn = 0;
        if (turn && !g_dragging && picked_node_index() >= 0 && chains::is_ladder(g_node_name)) {
            run_chain_op(chains::kOpTurn, static_cast<uint32_t>(turn)); // (a ladder: its facing, whole)
        } else if (turn && !g_dragging &&
                   !(picked_node_index() >= 0 && node_edit::is_chain(g_node_name))) { // (chain points don't turn)
            push_undo(get_snapshot(), "Turn");
            turn_selection(turn);
            note_action("turned (Z / X): %s", describe_selection().c_str());
            adjust::ActStep action; // (level_editor_adjust.h)
            action.kind = adjust::Kind::kTurn;
            action.quarters = !get_primary() && picked_node_index() < 0;
            action.degrees = static_cast<float>(turn) * (action.quarters ? 90.0f : xform::angle_step());
            adjust::get_note(action);
        }
        // [ / ]: scenery or objects scaled down / up a tenth (Shift: a hundredth); Alt+[ / ]: stretched so along the
        // world axis the camera faces most (looking down: y) - level_editor_scale.h.
        int scaling = (actions::squeezed(actions::kScaleUp) ? 1 : 0) - (actions::squeezed(actions::kScaleDown) ? 1 : 0);
        if (scaling && organize::selection_locked(true))
            scaling = 0;
        if (scaling && !g_dragging && !g_field_editing && !ctrl) {
            const float step = shift ? scale_tool::kFineStep : scale_tool::kStep;
            const float factor = scaling > 0 ? step : 1.0f / step;
            if (alt) {
                const TickCycle view = frame_from_yaw_pitch(g_yaw, g_pitch);
                const int axis = scale_tool::main_axis(view.back);
                if (scale_selection_by(scale_tool::along_axis(axis, factor), false)) {
                    adjust::ActStep action; // (level_editor_adjust.h)
                    action.kind = adjust::Kind::kScale;
                    action.factor = scale_tool::along_axis(axis, factor);
                    action.uniform = false;
                    adjust::get_note(action);
                    const char* axis_name = axis == 0 ? "x" : axis == 1 ? "y" : "z";
                    hud::get_message(std::string("Stretched along ") + axis_name);
                    note_action("stretched along %s (Alt+[ / ]): %s", axis_name, describe_selection().c_str());
                }
            } else if (scale_selection_by(scale_tool::get_uniform(factor), true)) {
                note_action("scaled ([ / ]): %s", describe_selection().c_str());
                adjust::ActStep action; // (level_editor_adjust.h)
                action.kind = adjust::Kind::kScale;
                action.factor = scale_tool::get_uniform(factor);
                adjust::get_note(action);
            }
        }
        // G: down onto the ground below it.
        if (actions::squeezed(actions::kDrop) && !g_dragging && !organize::selection_locked(true))
            drop_selection();
        // The wheel: nearer / further - or, while dragging, the selection pushed away / pulled back (a tenth of its
        // distance a notch).
        if (input.wheel && g_dragging) {
            const V3 pos = selection_pos();
            g_drag_push +=
                static_cast<float>(input.wheel) * std::max(20.0f, std::sqrt(get_dot(pos - g_pos, pos - g_pos)) * 0.1f);
        } else if (input.wheel && g_free_camera) {
            g_pos = g_pos - frame.back * static_cast<float>(kWheelStep * boost * input.wheel);
        } else if (input.wheel) {
            g_orbit_distance = static_cast<float>(
                std::max(static_cast<double>(kMinOrbit),
                         std::min(static_cast<double>(kMaxOrbit), g_orbit_distance * std::pow(0.85, input.wheel))));
        }
        // The camera at that distance from it (held still while the mouse drags it or a trigger's face).
        if (!g_dragging && !triggers::resizing() && !g_free_camera)
            g_pos = target_of() + frame.back * g_orbit_distance;
    } else if (!selection_held) {
        // Flying: W/A/S/D along the view, Q/E down/up, the wheel forward / back, - / = the speed (not while the
        // browser has the keys: its F, arrows and Enter).
        g_key_moving = false;
        if (!g_browser.open && !ctrl && (xform::fly_always() || input.right)) {
            const FlyInput fly = {(actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                      (actions::get_held(keys, actions::kBack) ? 1 : 0),
                                  (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                      (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                                  (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                      (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
            // ([editor] fly_accel_ms: the speed reached over that time - level_editor_camera.h)
            const float ramp = view_cam::fly_factor(fly.forward || fly.right || fly.up, seconds);
            g_pos = get_fly(g_pos, frame, fly, static_cast<float>(g_speed * boost * seconds * ramp));
        }
        if (input.wheel) {
            auto step = static_cast<float>(kWheelStep * boost * input.wheel);
            g_pos = g_pos - frame.back * step;
        }
        const int speed = (actions::squeezed(actions::kFaster) ? 1 : 0) - (actions::squeezed(actions::kSlower) ? 1 : 0);
        if (speed)
            g_speed = static_cast<float>(std::max(50.0, std::min(20000.0, g_speed * std::pow(1.5, speed))));
    }
    {
        // (UX8: the follow camera, Shift+F - the camera moved as the selection moves, keeping its offset even after
        // flying off it; not while a mouse drag places it - the drag goes by the cursor's ray. Another selection starts
        // afresh.)
        static uint32_t g_follow_key;
        SelectionIds ids = current_selection();
        const uint32_t key = (ids.objects.empty() ? 0u : ids.objects.front()) ^
                             (ids.sectors.empty() ? 0u : ids.sectors.front() * 31u) ^ (ids.node * 131u) ^
                             static_cast<uint32_t>(ids.objects.size() * 7u + ids.sectors.size() * 13u);
        const bool active =
            ux8::get_following() && have_selection() && !g_dragging && !triggers::resizing() && key == g_follow_key;
        g_follow_key = key;
        const V3 delta = nav::follow_step(&ux8::get_follower(), active, active ? target_of() : V3{0.0f, 0.0f, 0.0f});
        if (active && g_free_camera)
            g_pos = g_pos + delta;
    }
    if (g_browser.open && selection_held && !g_dragging && !g_free_camera)
        g_pos = target_of() + frame.back * g_orbit_distance;

    // ([features] debug_editor: once a second, what the mouse is seen as)
    static int g_debug_frames = 0;
    if (debug_enabled() && ++g_debug_frames % 60 == 0)
        get_log("level editor: mouse (%.0f, %.0f) in %dx%d, ray %d (%.3f, %.3f, %.3f), arrow %d", input.x, input.y,
                input.window_width_, input.window_height_, have_ray ? 1 : 0, dir.x, dir.y, dir.z, g_hover_axis);

    // Alt while moving scenery: what it carries (level_editor_carry.h) put back and left there for this move.
    // (not with Ctrl or Shift: Ctrl+Alt / Alt+Shift snap to a vertex / a surface - level_editor_snap.h)
    if (alt && !ctrl && !shift && (g_dragging || g_key_moving) && carry::total_of() > 0) {
        carry::get_leave();
        hud::get_message("Not carrying objects");
    }
    // Shift with the collision shown: the invisible wall a Shift+click would take, highlighted (level_editor_view.h).
    view::set_hover_wall(!g_dragging && have_ray && shift && !alt && view::show_collision() && ui::underway()
                             ? view::pick_wall(g_pos, make_unit(dir))
                             : 0);
    faces::get_hover(g_pos, make_unit(dir), have_ray && !g_dragging && g_hover_axis < 0); // (map3: Faces mode's hover)
    if (ruler::get_armed() && have_ray && !g_dragging)
        ruler::get_hover(g_pos, make_unit(dir), back_x, back_y, alt); // (the ruler's next end - level_editor_ruler.h)
    ux9::get_hover(g_pos, make_unit(dir), back_x, back_y, have_ray && !g_dragging); // (UX9: the tools' next pick)
    // Dragging, or what's under the cursor (an arrow first, then an object or scenery - the nearer).
    if (g_dragging && have_selection() && have_ray) {
        if (!g_drag_moved && (g_modal || std::fabs(input.x - g_press_x) + std::fabs(input.y - g_press_y) >= 4.0f))
            g_drag_moved = true;
        // (an axis typed - X / Y / Z, level_editor_transform.h: a move starts again from where it began, along it)
        if (g_typed.axis_changed_) {
            g_typed.axis_changed_ = false;
            if (g_drag_mode == xform::Mode::kShiftBy) {
                place_selection(g_drag_start);
                g_axis = g_typed.axis;
                start_drag(g_pos, dir, false, frame, input.x, input.y);
                g_drag_moved = true;
            }
        }
        // (Shift: up / down - not with Ctrl or Alt, which pick what it snaps to)
        const bool vertical = shift && !ctrl && !alt;
        if (g_drag_mode == xform::Mode::kShiftBy && g_axis < 0 && vertical != g_drag_vertical)
            start_drag(g_pos, dir, vertical, frame, input.x, input.y);
        if (g_drag_moved || g_typed_apply || !g_typed.text.empty())
            get_drag(g_pos, dir, frame, input.x, input.y, snapping_now(ctrl, shift, alt));
    }
    // Enter (typed values) or - Alt+G's modal transform - a click: applied.
    if (g_dragging && (g_typed_apply || (g_modal && input.left_presses_))) {
        end_drag();
        focus_on_selection();
        input.left_presses_ = 0;
        input.left_releases_ = 0;
    }
    g_typed_apply = false;
    if (!g_dragging) {
        g_hover_axis = have_ray && have_selection() ? arrow_at(frame, back_x, back_y) : -1;
        // (a selected trigger volume's resize handle - after the arrows: level_editor_triggers.h)
        if (!triggers::resizing())
            g_hover_handle = have_ray && g_hover_axis < 0 && ui::underway()
                                 ? triggers::handle_at(back_x, back_y, 8.0f * pixel_scale())
                                 : -1;
        triggers::set_hover_handle(g_hover_handle);
        objects::GameEntity* hover = nullptr;
        uint32_t hover_sector = 0;
        // What a click would take (level_editor_picking.h): the cycle's next entry while a click-to-cycle holds; else
        // a node's gizmo under the cursor (what the last frame drew), then the nearer of an object and scenery -
        // structure and see-through pieces passed unless Alt (or a middle click) - as the pick filter allows.
        g_hover_node = -1;
        g_hover_passed = 0;
        // (UX8: a drill-through - Alt+wheel - holds with Alt still down: its entry is the hover)
        const bool drilled = g_drilled && cycle_holds(input.x, input.y, false, false);
        g_drilled = drilled;
        g_hover_cycle = drilled || cycle_holds(input.x, input.y, shift, alt || input.middle_presses_)
                            ? (g_cycle.index + 1) % static_cast<int>(g_cycle.order.size())
                            : -1;
        if (have_ray && g_hover_axis < 0 && g_hover_cycle >= 0) {
            const picking::TableRow& next = core::at(g_cycle.order, g_hover_cycle);
            if (next.kind == picking::Choice::kNode)
                g_hover_node = static_cast<int>(next.id);
            else if (next.kind == picking::Choice::kObject)
                hover = get_live({core::down_cast<objects::GameEntity>(objects::find_object(next.id)), next.id});
            else
                hover_sector = next.id;
        } else if (have_ray && g_hover_axis < 0) {
            const bool everything = alt || input.middle_presses_;
            picking::Candidates c;
            scenery::Hit hit;
            scenery::get_pick(g_pos, make_unit(dir), kMaxPickDistance, &hit);
            // (a gizmo behind the first occluding surface under the cursor isn't taken by a plain click)
            c.node = ui::underway() ? nodes::pick_nearest(g_pickables, back_x, back_y, 8.0f * pixel_scale(),
                                                          gizmo_occluder(frame, dir, hit, everything))
                                    : -1;
            float object_t = kMaxPickDistance;
            objects::GameEntity* object = object_edit::get_pick(g_pos, dir, kMaxPickDistance, &object_t);
            c.object = object != nullptr;
            c.object_t_ = object_t;
            // (Alt: the nearest pickable piece whatever is in front of it; a plain click: the first, if nothing occludes)
            const uint32_t scenery_pick = everything ? hit.nearest : hit.checksum;
            c.scenery = scenery_pick != 0;
            c.scenery_t_ = everything ? hit.nearest_t_ : hit.t;
            c.passed = hit.passed != 0;
            c.passed_t_ = hit.passed_t_;
            c.blocked = hit.blocked;
            c.block_t_ = hit.block_t_;
            switch (picking::get_choose(c, g_pick_filter, everything)) {
            case picking::Choice::kNode:
                g_hover_node = c.node;
                break;
            case picking::Choice::kObject:
                hover = object;
                break;
            case picking::Choice::kScenery:
                hover_sector = scenery_pick;
                break;
            case picking::Choice::kPassed:
                hover_sector = hit.passed;
                break;
            case picking::Choice::kNone:
                if (g_pick_filter & picking::kScenery)
                    g_hover_passed = hit.passed;
                break;
            }
        }
        if (debug_enabled() && hover_sector != g_hover_sector && hover_sector)
            get_log("level editor: hovering %s", scenery::describe_hover(hover_sector).c_str());
        g_hover = hover ? Picked{hover, hover->ident_} : Picked{};
        g_hover_sector = hover_sector;
    }

    // A click: on an arrow, dragging along it; on something, selecting it (Shift: adding / removing it; Alt or a
    // middle click: one piece of scenery in / out; Ctrl on a part of the scenery selection: that part dropped) and
    // dragging it; on nothing, deselecting.
    // A trigger volume's resize handle (level_editor_triggers.h): pressed, its face follows the mouse along its axis
    // (Ctrl / Snap 25: sizes on 25s; the opposite face stays); let go, one undo step.
    if (triggers::resizing()) {
        if (input.left && have_ray)
            triggers::rescale(g_pos, dir, ctrl || g_snap_always);
        if (!input.left) {
            triggers::end_resize();
            note_action("resized a trigger volume: %s", describe_selection().c_str());
        }
    }
    // Box select armed (Ctrl+B): the press starts a box wherever it is (nothing else takes it).
    if (input.left_presses_ && have_ray && !g_dragging && organize::box_armed()) {
        organize::box_press(back_x, back_y,
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
        input.left_presses_ = 0;
    }
    // A press where nothing is (no arrow, gizmo, object or scenery under the cursor): a box if it's dragged (Shift
    // adds, Ctrl removes - the click itself does what it did: deselects when plain).
    bool box_candidate = input.left_presses_ && have_ray && !g_dragging && g_hover_axis < 0 && g_hover_handle < 0 &&
                         g_hover_node < 0 && !get_live(g_hover) && !g_hover_sector && !faces::get_picking() &&
                         !chains::wants_pick() && chains::get_drawing() == chains::Blit::kNone &&
                         !(g_place_armed >= 0 && g_place_open) && !alt;
    // (UX8: a double click on nothing a click takes - a surface - with nothing selected (the first click deselects):
    // the camera glides there - level_editor_ux8.h)
    if (box_candidate && !ruler::get_armed() && !organize::box_armed() &&
        ux8::note_empty_click(SDL_GetTicks(), input.x, input.y) && !have_selection() && !g_node_selected) {
        teleport_to(dir);
        box_candidate = false;
    }
    const bool handle_pressed = input.left_presses_ && have_ray && !g_dragging && g_hover_axis < 0 &&
                                g_hover_handle >= 0 && !triggers::resizing() &&
                                triggers::begin_resize(g_hover_handle, g_pos, dir);
    if (handle_pressed) {
        hud::get_message("Resize: drag the face (Ctrl: snap)", 3.0);
    } else if (input.left_presses_ && have_ray && !g_dragging && g_hover_axis < 0 && ux9::occupied() &&
               ux9::get_click(g_pos, make_unit(dir), back_x, back_y)) {
        // (UX9: the gap wizard's / the ground tool's pick - level_editor_ux9.h)
        note_action("ux9 tool click");
    } else if (input.left_presses_ && have_ray && !g_dragging && arrange::edge_picking() &&
               // NOLINTNEXTLINE(bugprone-branch-clone): the cases kept apart, one per kind
               arrange::edge_click(back_x, back_y)) {
        // (F7: a rail along the edge clicked - level_editor_arrange.h)
    } else if (input.left_presses_ && have_ray && !g_dragging && g_browser.open && g_hover_axis < 0 && !ctrl && !alt &&
               arrange::place_click(shift)) {
        // (the Add palette open with Click places: its piece where the ghost showed it - level_editor_arrange.h)
    } else if (input.left_presses_ && have_ray && !g_dragging && ruler::get_armed() && g_hover_axis < 0) {
        // (the ruler armed: a click puts an end - level_editor_ruler.h)
        hud::get_message(ruler::get_click(), 6.0);
    } else if (input.left_presses_ && have_ray && !g_dragging && faces::get_picking() && g_hover_axis < 0) {
        // (map3: Faces mode - the collision face clicked, Shift adds / drops it: level_editor_faces.h)
        faces::click_at(g_pos, make_unit(dir), shift);
        note_action("faces mode click: %s", faces::describe().c_str());
    } else if (input.left_presses_ && have_ray && !g_dragging && chains::wants_pick() && g_hover_axis < 0) {
        // (an armed Link / path assignment: the node clicked - level_editor_chains.h)
        if (g_hover_node >= 0) {
            const chains::State before = chains::at_hand();
            std::string message;
            uint32_t select = 0;
            chains::get_picked(core::at(nodes::get_all(), g_hover_node).name, &message, &select);
            after_chain_change(before, select, message);
        } else {
            hud::get_message(chains::get_waiting());
        }
    } else if (input.left_presses_ && have_ray && !g_dragging && chains::get_drawing() != chains::Blit::kNone &&
               g_hover_axis < 0) {
        draw_chain_point(dir, alt);
    } else if (input.left_presses_ && have_ray && !g_dragging && g_place_armed >= 0 && g_place_open &&
               g_hover_axis < 0) {
        place_node(g_place_armed, cursor_point(dir, have_ray, frame));
        if (!shift)
            g_place_armed = -1;
    } else if (input.left_presses_ && have_ray && !g_dragging && ctrl && !alt && !shift && g_hover_axis < 0 &&
               drop_under_cursor(dir)) {
        // (Ctrl+click: one part of the selection dropped - no drag)
        g_cycle.have = false;
        note_action("ctrl+click - selected: %s", describe_selection().c_str());
        if (debug_enabled())
            get_log("level editor: ctrl+click - selected: %s", describe_selection().c_str());
    } else if ((input.left_presses_ || input.middle_presses_) && have_ray && !g_dragging) {
        objects::GameEntity* hover = get_live(g_hover);
        const bool one = alt || (input.middle_presses_ && !input.left_presses_);
        bool drag = false;
        if (g_hover_axis < 0)
            note_click_for_cycle(input.x, input.y, dir, input.left_presses_ && !shift && !one);
        // (with the collision shown, Shift+click on an invisible wall's face: that wall - level_editor_view.h)
        const uint32_t wall = input.left_presses_ && shift && !alt && g_hover_axis < 0 && view::show_collision()
                                  ? view::pick_wall(g_pos, make_unit(dir))
                                  : 0;
        // (a plain click on the selected wall's face: dragging it - level_editor_walls.h)
        const bool on_wall = !wall && input.left_presses_ && !shift && !one && g_hover_axis < 0 && wall_only() &&
                             view::show_collision() && view::pick_wall(g_pos, make_unit(dir)) == wall_only();
        if (!wall && !on_wall && g_hover_axis < 0)
            view::clear_wall();
        // (with the trigger volumes shown, a plain click on one - nearer than the rest: its node; Alt passes them -
        // level_editor_triggers.h)
        int trigger_node = -1;
        uint32_t trigger_sector = 0;
        const bool trigger =
            !wall && input.left_presses_ && !alt && g_hover_axis < 0 && g_hover_node < 0 && ui::underway() &&
            triggers::get_pick(g_pos, make_unit(dir), &trigger_node, &trigger_sector, trigger_reach(dir));
        if (wall) {
            unpick();
            view::select_wall(wall);
            hud::get_message("Invisible wall " + object_edit::caption(wall));
        } else if (on_wall) {
            drag = true;
        } else if (g_hover_axis >= 0 && input.left_presses_) {
            g_axis = g_hover_axis;
            drag = true;
        } else if (g_hover_node >= 0 && input.left_presses_) {
            select_node(g_hover_node);
            drag = have_selection() && !shift; // (an object, or a point node: dragged as objects are)
        } else if (trigger) {
            if (trigger_node >= 0) {
                select_node_only(trigger_node);
                drag = picked_node_index() >= 0 && !shift;
            } else {
                unpick();
            }
            hud::get_message(trigger_sector ? "Trigger " + object_edit::caption(trigger_sector)
                                            : std::string("Trigger volume"));
        } else if (g_hover_sector) {
            g_objects.clear();
            if (one)
                scenery::flip_over(g_hover_sector);
            else if (shift)
                scenery::get_selected(g_hover_sector) ? scenery::flip_over(g_hover_sector)
                                                      : scenery::add_group(g_hover_sector);
            else if (!scenery::get_selected(g_hover_sector))
                scenery::select_group(g_hover_sector);
            drag = scenery::has_selection() && !one && !shift;
        } else if (input.left_presses_ && hover && flag_node_of(hover) >= 0) {
            // (a flag's object: its node is what moves - level_editor_node_edit.h)
            select_node(flag_node_of(hover));
            drag = have_selection() && !shift;
        } else if (input.left_presses_ && hover) {
            scenery::clear_selection();
            g_node_selected = false;
            if (shift) {
                if (is_picked(hover))
                    std::erase_if(g_objects, [&](const Picked& p) { return p.object == hover; });
                else
                    g_objects.push_back({hover, hover->ident_});
            } else {
                if (!is_picked(hover))
                    select_objects({hover});
                else if (get_primary() != hover) {
                    // (the clicked one first: the camera and the arrows go by it)
                    auto it = std::ranges::find_if(g_objects, [&](const Picked& p) { return p.object == hover; });
                    std::rotate(g_objects.begin(), it, it + 1);
                }
                drag = true;
            }
        } else if (input.left_presses_ && !shift && !ctrl) {
            unpick();
        }
        if (get_primary() || scenery::has_selection())
            g_node_selected = false;
        note_action("click - selected: %s", describe_selection().c_str());
        if (debug_enabled())
            get_log("level editor: click - selected: %s", describe_selection().c_str());
        if (drag && organize::selection_locked(true))
            drag = false; // (the Outliner's lock: selected, not moved)
        if (drag) {
            // (on a Rotate / Scale handle: that tool - level_editor_transform.h; else it moves)
            g_drag_mode = g_axis >= 0 ? gizmo_mode() : xform::Mode::kShiftBy;
            g_modal = false;
            g_typed = {};
            g_dragging = true;
            g_drag_start = selection_pos();
            g_press_x = input.x;
            g_press_y = input.y;
            g_drag_moved = false;
            begin_change();
            start_drag(g_pos, dir, shift && !ctrl && !alt && g_axis < 0, frame, input.x, input.y);
        }
    }
    if (box_candidate && !g_dragging)
        organize::box_press(back_x, back_y,
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
    organize::box_track(input.left, back_x, back_y);
    // Let go: the selection becomes the frame of reference (the camera turns to it, at the distance it is).
    if (input.left_releases_ && !input.left) {
        if (g_dragging && g_drag_moved)
            g_cycle.have = false; // (moved: the next click starts afresh)
        end_drag();
        focus_on_selection();
        g_cycle.changes = g_changes;
    }
    update_highlights();
    // The ground marker: the point below the selection's base.
    g_have_mark = false;
    if (have_selection()) {
        const V3 pos = selection_pos();
        g_mark_base = {pos.x, get_selection_base(), pos.z};
        const V3 from = get_primary() || picked_node_index() >= 0 ? g_mark_base + V3{0.0f, 2.0f, 0.0f}
                                                                  : g_mark_base - V3{0.0f, 1.0f, 0.0f};
        g_have_mark = ground_below(from, &g_mark_ground);
    }
    // The camera tools' end of the frame: a flight, an ortho view kept on its axis (level_editor_camera.h).
    const bool flew = actions::get_held(keys, actions::kForward) || actions::get_held(keys, actions::kBack) ||
                      actions::get_held(keys, actions::kLeft) || actions::get_held(keys, actions::kRight) ||
                      actions::get_held(keys, actions::kFlyUp) || actions::get_held(keys, actions::kFlyDown);
    view_cam::after_update(rig_now(), input.wheel,
                           input.right || input.left_presses_ || input.middle_presses_ || input.wheel || flew ||
                               view_cam::get_orbiting());
}

void blit()
{
    if (!g_active || !g_have_view || !render::g_d3d_device)
        return;
    const TickCycle frame = frame_from_yaw_pitch(g_yaw, g_pitch);
    // What's hovered and selected shows tinted (update_highlights); boxes only for what couldn't be (yellow hovered,
    // cyan selected); the selection's grind rails, the ground marker and the move arrows as lines.
    static std::vector<LineVertex> lines;
    lines.clear();
    const std::vector<objects::GameEntity*> objects = selected_objects();
    objects::GameEntity* hover = g_dragging ? nullptr : get_live(g_hover);
    OrientedBox box{};
    // (with the UI the hover's outline is always drawn too - what a click takes: the object, or the scenery's group -
    // its one piece with Alt)
    if (!g_hover_tinted || ui::underway()) {
        if (hover && !is_picked(hover) && object_edit::model_box(hover, &box)) {
            add_box(box, frame, 0xFFFFFF00, &lines);
        } else if (!g_dragging && g_hover_sector && !scenery::get_selected(g_hover_sector)) {
            const uint8_t* keys = SDL_GetKeyboardState(nullptr);
            const bool one =
                keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT] || scenery::why_not_movable(g_hover_sector);
            const std::vector<uint32_t>& group = scenery::hover_group(g_hover_sector);
            if (one || group.empty()) {
                if (scenery::cuboid(g_hover_sector, &box))
                    add_box(box, frame, 0xFFFFFF00, &lines);
            } else {
                for (size_t i = 0; i < group.size() && i < 64; ++i)
                    if (scenery::cuboid(core::at(group, i), &box))
                        add_box(box, frame, 0xFFFFFF00, &lines);
            }
        }
    }
    if (!g_selected_tinted) {
        for (objects::GameEntity* object : objects) {
            if (object_edit::model_box(object, &box))
                add_box(box, frame, 0xFF00FFFF, &lines);
        }
        for (size_t i = 0; objects.empty() && i < scenery::get_selection().size() && i < 64; ++i) {
            if (scenery::cuboid(core::at(scenery::get_selection(), i), &box))
                add_box(box, frame, 0xFF00FFFF, &lines);
        }
    }
    if (objects.empty()) {
        for (const auto& segment : scenery::selected_rails())
            add_line(segment.first, segment.second, frame, 0xFF40E0FF, &lines);
    }
    // The ground marker: a line from the selection's base down (or up) to the ground below it, and a cross there.
    if (g_have_mark && have_selection()) {
        constexpr uint32_t kMark = 0xFFFFE070;
        if (std::fabs(g_mark_base.y - g_mark_ground.y) > 1.0f)
            add_line(g_mark_base, g_mark_ground, frame, kMark, &lines);
        const float size = std::max(10.0f, std::min(60.0f, g_orbit_distance * 0.05f));
        add_line(g_mark_ground - V3{size, 0.0f, 0.0f}, g_mark_ground + V3{size, 0.0f, 0.0f}, frame, kMark, &lines);
        add_line(g_mark_ground - V3{0.0f, 0.0f, size}, g_mark_ground + V3{0.0f, 0.0f, size}, frame, kMark, &lines);
    }
    // The gizmo (level_editor_transform.h): Move's arrows, Rotate's ring, Scale's cubes - while dragging with one, that
    // one only (a modal Rotate / Scale: the whole gizmo).
    if (have_selection() && (!g_dragging || g_axis >= 0 || g_drag_mode != xform::Mode::kShiftBy)) {
        const xform::Gizmo gizmo = gizmo_now();
        for (const xform::Segment& segment : xform::gizmo_segments(gizmo)) {
            if (g_dragging && g_axis >= 0 && segment.handle != g_axis)
                continue;
            const uint32_t colour =
                xform::handle_colour(segment.handle, segment.handle == g_hover_axis || segment.handle == g_axis);
            if (segment.arrow)
                add_arrow(segment.a, segment.b, frame, colour, &lines);
            else
                add_line(segment.a, segment.b, frame, colour, &lines,
                         segment.handle == 1 && gizmo.mode == xform::Mode::kSpinBy ? 3 : 1);
        }
    }
    // The UI: the lines through ImGui's draw list, with the panels (level_editor_ui.h).
    if (ui::underway()) {
        g_overlay.get_clear();
        ui::ShapeMesh model = assemble_model();
        // The Add palette's preview drawn off-screen first (level_editor_preview.h): ImGui shows its picture.
        preview::tick_cycle(&model);
        add_node_gizmos(frame, model.nodes.selected);
        view::add_collision(g_pos, frame, g_lens, g_viewport.data(), kNearZ, &g_overlay.under, &g_overlay.lines);
        // (map3: the selected collision faces, the camera nodes' frustums)
        faces::add_overlay(g_pos, frame, g_lens, g_viewport.data(), kNearZ, &g_overlay.lines);
        cameras::add_overlay(g_pos, frame, g_lens, g_viewport.data(), kNearZ,
                             model.nodes.selected >= 0 ? core::at(nodes::get_all(), model.nodes.selected).name : 0,
                             &g_overlay.lines);
        // (UX5: note pins; the problem checker run again a moment after a change)
        notes::add_overlay(g_pos, frame, g_lens, g_viewport.data(), kNearZ, &g_overlay.lines, &g_overlay.circles,
                           &g_overlay.texts);
        checks::note_model(model.changes, model.marks);
        triggers::add_overlay(g_pos, frame, g_lens, g_viewport.data(), kNearZ, &g_overlay.lines, &g_overlay.texts);
        if (overlays::get_shown(overlays::kFeatureMap)) // (the Overlays menu - level_editor_overlays.h)
            triggers::add_extras(
                g_pos, frame, g_lens, g_viewport.data(), kNearZ, model.nodes.selected, &g_overlay.lines,
                &g_overlay.texts); // (speed-ups, proximity shapes, breakables, boosts - the feature map)
        triggers::add_handles(&g_overlay.circles); // (the selected trigger volume's resize handles)
        entities::add_overlay(g_pos, frame, g_lens, g_viewport.data(), kNearZ, &g_overlay.lines, &g_overlay.texts);
        play_test::add_overlay(
            g_pos, frame, g_lens, g_viewport.data(), kNearZ, &g_overlay.lines, &g_overlay.circles,
            &g_overlay.texts); // (the test runs' trails, Simulate's banner - level_editor_playtest.h)
        goal_edit::add_overlay(g_pos, frame, g_lens, g_viewport.data(), kNearZ,
                               model.nodes.selected >= 0 ? core::at(nodes::get_all(), model.nodes.selected).name : 0,
                               &g_overlay.lines,
                               &g_overlay.texts); // (the goals' points' labels)
        for (size_t i = 0; i + 1 < lines.size(); i += 2)
            g_overlay.lines.push_back({core::at(lines, i).x, core::at(lines, i).y, core::at(lines, i + 1).x, core::at(lines, i + 1).y, core::at(lines, i).color, 1.0f});
        // The view menus' overlays (level_editor_overlays.h): labels off - the texts so far dropped; the grid and the
        // carried things, the jump arcs, the grind preview, the ruler (their own texts kept: they are the readings).
        if (!overlays::get_shown(overlays::kLabels))
            g_overlay.texts.clear();
        overlays::Painter painter;
        if (overlays::painter_now(&g_overlay, &painter)) {
            overlays::add_overlay(painter, g_dragging || g_key_moving, g_mark_base, have_selection());
            arcs::add_overlay(painter, objects.empty() ? scenery::get_selection() : std::vector<uint32_t>{});
            grind_preview::add_overlay(painter, g_dragging ? -1 : model.nodes.hovered, model.nodes.selected);
            ruler::add_overlay(painter);
            ux9::add_overlay(painter); // (UX9: skateability colours, the gap wizard's gates, the ground tool's preview)
        }
        // (UX7: the placement ghost, the readout while dragging, the faces snapped to, the top edges for a rail -
        // level_editor_arrange.h)
        arrange::add_overlay(g_pos, frame, g_lens, g_viewport.data(), kNearZ, &g_overlay);
        thumbs::tick_cycle(model); // (the Add palette's thumbnails drawn off-screen - level_editor_thumbs.h)
        ui::tick_cycle(model, g_overlay);
        return;
    }
    // The old HUD: the lines on the device (the renderer's cached state).
    if (lines.empty())
        return;
    const int count = static_cast<int>(lines.size());
    const LineVertex* line_data = lines.data();
    // The renderer's own (cached) state: no texture, pre-transformed vertices with a colour.
    render::set_texture(0, nullptr);
    render::set_vertex_format(0x144);           // XYZRHW | DIFFUSE | TEX1
    render::set_pixel_shader(nullptr, nullptr); // no pixel shader
    // Stage 0 takes the vertex colour (put back after: the renderer doesn't know of this).
    const std::array<int, 4> states = {{1, 2, 4, 5}};      // colour op / arg 1, alpha op / arg 1
    const std::array<uint32_t, 4> values = {{2, 0, 2, 0}}; // select arg 1, diffuse
    std::array<uint32_t, 4> saved{};
    for (int i = 0; i < 4; ++i) {
        core::at(saved, i) = get_stage_state(core::at(states, i));
        set_stage_state(core::at(states, i), core::at(values, i));
    }
    render::g_d3d_device->DrawPrimitiveUP(D3DPT_LINELIST, static_cast<unsigned int>(count / 2), line_data,
                                          sizeof(LineVertex));
    for (int i = 0; i < 4; ++i)
        set_stage_state(core::at(states, i), core::at(saved, i));
}

std::vector<std::pair<std::string, std::string>> describe_for_screenshot()
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!g_active)
        return out;
    const ui::ShapeMesh model = assemble_model();
    std::array<char, 160> text{};
    out.emplace_back("mode", std::string(model.mode ? model.mode : "") +
                                 (ui::underway() ? " (Dear ImGui panels)" : " (old HUD)"));
    out.emplace_back("level", model.level);
    std::snprintf(text.data(), sizeof(text), "(%.1f, %.1f, %.1f)", g_pos.x, g_pos.y, g_pos.z);
    out.emplace_back("camera", text.data());
    std::snprintf(text.data(), sizeof(text), "yaw %.1f, pitch %.1f degrees", g_yaw * 180.0f / kPi, g_pitch * 180.0f / kPi);
    out.emplace_back("camera turn", text.data());
    out.emplace_back("selected", model.selected);
    // The view options (level_editor_view.h).
    std::snprintf(text.data(), sizeof(text),
                  "collision %s (%d faces, %d pieces, %d invisible walls%s), %d hidden in the editor",
                  model.view.collision ? "shown" : "off", model.view.triangles, model.view.sectors, model.view.walls,
                  model.view.capped ? ", capped" : "", model.view.hidden);
    out.emplace_back("view", text.data());
    out.emplace_back("triggers", std::string(triggers::get_shown() ? "shown: " : "hidden: ") + triggers::describe());
    out.emplace_back("goals", goal_edit::describe());
    if (model.view.wall)
        out.emplace_back("invisible wall", model.view.wall_name_ + ": " + model.view.wall_info_);
    const ui::ShapeMesh::Selection& sel = model.selection;
    if (!sel.kind.empty()) {
        std::snprintf(text.data(), sizeof(text), "%s %s at (%.1f, %.1f, %.1f)", sel.kind.c_str(), sel.name.c_str(), sel.pos.x,
                      sel.pos.y, sel.pos.z);
        out.emplace_back("selection", text.data());
    }
    if (model.nodes.selected >= 0 && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& node = core::at(nodes::get_all(), model.nodes.selected);
        std::snprintf(text.data(), sizeof(text), "%s (%s, %s) at (%.1f, %.1f, %.1f)%s%s", nodes::caption(node.name).c_str(),
                      nodes::caption(node.cls).c_str(), nodes::family_name(node.family), node.pos.x, node.pos.y,
                      node.pos.z, node.added ? ", added" : "", node.hidden ? ", hidden" : "");
        out.emplace_back("inspected node", text.data());
    }
    out.emplace_back("under the cursor", model.hover);
    // (the view menus - level_editor_overlays.h: the view mode, the overlays' bits, the arcs, the ruler, the rail shown)
    std::snprintf(text.data(), sizeof(text), "%s, overlays 0x%03X", overlays::view_mode_name(overlays::view_mode()),
                  overlays::at_hand());
    out.emplace_back("view mode", text.data());
    for (const std::string& line : {arcs::describe(), ruler::describe(), grind_preview::describe()})
        if (!line.empty())
            out.emplace_back("measure", line);
    if (const std::string ux9 = ux9::describe(); !ux9.empty())
        out.emplace_back("ux9", ux9);
    std::snprintf(text.data(), sizeof(text), "%d changes, %d pieces moved%s", model.changes, model.moved,
                  model.unsaved ? ", unsaved" : "");
    out.emplace_back("changes", text.data());
    // (UX5) the problem checker's last count (the level's own and ignored ones left out) and the notes
    {
        int errors = 0, warnings = 0, infos = 0;
        checks::shown_counts(&errors, &warnings, &infos);
        std::snprintf(text.data(), sizeof(text),
                      "%d errors, %d warnings, %d notes; %u layout records unresolved; %u note pins", errors, warnings,
                      infos, static_cast<unsigned>(checks::layout_issues().size()),
                      static_cast<unsigned>(notes::get_all().size()));
        out.emplace_back("problems", text.data());
    }
    if (!model.message.empty())
        out.emplace_back("message", model.message);
    return out;
}

// --- for the modules (level_editor_modules.h) ---

void push_module_undo_labelled(std::shared_ptr<ModuleStep> before, const char* label)
{
    UndoStep step;
    step.kind = UndoStep::Kind::kModule;
    step.module = std::move(before);
    step.where = selection_pos();
    push_undo(step, label);
}

void push_module_undo(std::shared_ptr<ModuleStep> before) { push_module_undo_labelled(std::move(before), nullptr); }

// --- for the history, changes, selection and clipboard modules (level_editor_modules.h, level_editor_ux3.h) ---

std::vector<EditorUndoRecord> undo_history()
{
    std::vector<EditorUndoRecord> out;
    out.reserve(g_undo.size());
    for (const UndoStep& step : g_undo)
        out.push_back({step.label, step.detail});
    return out;
}

std::vector<EditorUndoRecord> redo_history()
{
    std::vector<EditorUndoRecord> out;
    for (auto it = g_redo.rbegin(); it != g_redo.rend(); ++it)
        out.push_back({it->label, it->detail});
    return out;
}

bool change_under_way() { return g_dragging || g_key_moving || g_field_editing || triggers::resizing(); }

void jump_history(int steps)
{
    if (change_under_way() || !g_active)
        return;
    for (; steps < 0 && !g_undo.empty(); ++steps)
        step_history(&g_undo, &g_redo, "Undone");
    for (; steps > 0 && !g_redo.empty(); --steps)
        step_history(&g_redo, &g_undo, "Redone");
}

void push_locks_undo_labelled(const char* label) { push_locks_undo(label); }

bool apply_layout_step(const std::string& text, const char* label, std::string* message)
{
    if (!g_active || change_under_way()) {
        *message = "Not available while moving";
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kLevel;
    step.reset = false;
    if (!layout::capture_text(&step.layout)) {
        *message = "Not in a level";
        return false;
    }
    entities::stop_preview();
    carry::disarm();
    unpick();
    if (!layout::put_back(text, message))
        return false;
    push_undo(step, label);
    note_action("%s (the layout put in place again)", label);
    return true;
}

SelectionIds current_selection()
{
    SelectionIds ids;
    for (const objects::GameEntity* object : selected_objects())
        ids.objects.push_back(object->ident_);
    if (ids.objects.empty())
        ids.sectors = scenery::get_selection();
    if (ids.objects.empty() && ids.sectors.empty() && g_node_selected && inspected_node() >= 0)
        ids.node = g_node_name;
    return ids;
}

void set_selection(const SelectionIds& ids)
{
    if (g_dragging)
        return;
    unpick();
    std::vector<objects::GameEntity*> objects;
    for (const uint32_t id : ids.objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (object && !(object->object_flags_ & objects::kObjectFlagDead))
            objects.push_back(object);
    }
    if (!objects.empty()) {
        select_objects(objects);
    } else if (!ids.sectors.empty()) {
        scenery::set_selection(ids.sectors);
    } else if (ids.node) {
        const int index = nodes::get_find(ids.node);
        if (index >= 0)
            select_node(index);
    }
    if (have_selection())
        focus_on_selection();
    note_action("selected: %s", describe_selection().c_str());
}

bool go_to_thing(uint32_t id)
{
    if (!g_active || !id || g_dragging)
        return false;
    const int index = nodes::get_find(id);
    if (index >= 0) {
        go_to_node(index);
        if (have_selection())
            go_to_selection();
        return true;
    }
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    if (object && !(object->object_flags_ & objects::kObjectFlagDead)) {
        unpick();
        select_objects({object});
        go_to_selection();
        return true;
    }
    OrientedBox box{};
    if (scenery::cuboid(id, &box)) {
        unpick();
        scenery::set_selection({id});
        go_to_selection();
        return true;
    }
    return false;
}

bool project_to_back(V3 point, float* x, float* y)
{
    if (!g_active || !g_have_view)
        return false;
    float nx = 0.0f, ny = 0.0f;
    if (!cast_onto(g_pos, frame_from_yaw_pitch(g_yaw, g_pitch), g_lens, point, kNearZ, &nx, &ny))
        return false;
    ndc_to_pixel(g_viewport.data(), nx, ny, x, y);
    return true;
}

uint32_t pick_filter_now() { return g_pick_filter; }

int set_fields_on_nodes(const std::vector<uint32_t>& nodes, const std::vector<FieldSet>& fields, const char* label,
                        std::string* message)
{
    UndoStep step;
    step.kind = UndoStep::Kind::kFields;
    int set = 0;
    for (const uint32_t node : nodes) {
        if (nodes::get_find(node) < 0)
            continue;
        for (const FieldSet& f : fields) {
            fields::Own before;
            fields::get_capture(node, f.key, &before);
            fields::Value value;
            std::string error;
            if (!f.removed && !fields::parse(static_cast<fields::Kind>(f.kind), f.text, &value, &error)) {
                if (message->empty())
                    *message = error;
                continue;
            }
            if (!fields::assign_value(node, f.key, value, f.removed, &error)) {
                if (message->empty())
                    *message = object_edit::caption(node) + ": " + error;
                continue;
            }
            step.fields.push_back({node, f.key, before});
            ++set;
        }
    }
    if (!step.fields.empty()) {
        push_undo(step, label);
        note_action("%s: %d fields on %u nodes", label, set, static_cast<unsigned>(nodes.size()));
    }
    return set;
}

bool selection_transform(TransformValues* transform)
{
    if (!have_selection())
        return false;
    *transform = TransformValues{};
    transform->pos = selection_pos();
    node_edit::Values node;
    if (objects::GameEntity* object = get_primary()) {
        const maths::Vec4& at = core::at(object->basis_frame_.row, 2);
        transform->has_heading_ = true;
        transform->heading = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)));
        transform->has_scale_ = scale_tool::object_scalable(object);
        transform->scale = scale_tool::get_object_scale(object);
    } else if (node_values(&node)) {
        transform->has_heading_ = true;
        transform->heading = node.angles.y;
    } else if (scenery::has_selection()) {
        transform->has_scale_ = true;
        transform->scale = scenery::selection_scale();
    }
    return true;
}

bool set_selection_transform(const TransformValues& transform)
{
    if (!have_selection() || change_under_way())
        return false;
    if (organize::selection_locked(true))
        return false;
    push_undo(get_snapshot(), "Paste transform");
    place_selection(transform.pos);
    node_edit::Values node;
    if (transform.has_heading_) {
        if (const objects::GameEntity* object = get_primary()) {
            const maths::Vec4& at = core::at(object->basis_frame_.row, 2);
            auto now = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)));
            const V3 pivot = object_edit::pos_of(object);
            for (objects::GameEntity* o : selected_objects())
                object_edit::turn_about(o, pivot, transform.heading - now);
        } else if (node_values(&node)) {
            node.angles.y = transform.heading;
            set_selected_node(node);
        }
    }
    if (transform.has_scale_ && (get_primary() || scenery::has_selection()) && picked_node_index() < 0)
        scale_selection_to(transform.scale);
    if (!get_primary() && scenery::has_selection())
        follow_scenery();
    get_settle();
    note_action("pasted a transform: %s", describe_selection().c_str());
    return true;
}

void show_message(const std::string& text, double seconds) { hud::get_message(text, seconds); }

bool current_view(ViewNow* view)
{
    if (!g_active || !g_have_view)
        return false;
    view->pos = g_pos;
    view->yaw = g_yaw;
    view->pitch = g_pitch;
    view->lens = g_lens;
    std::memcpy(view->viewport.data(), g_viewport.data(), sizeof(view->viewport));
    view->near_z_ = kNearZ;
    return true;
}

void look_from(V3 pos, float yaw, float pitch)
{
    if (!g_active)
        return;
    unpick();
    g_dragging = false;
    g_pos = pos;
    g_yaw = yaw;
    g_pitch = std::clamp(pitch, -89.0f * kPi / 180.0f, 89.0f * kPi / 180.0f);
}

// (UX5 - level_editor_modules.h) the Problems window's and the notes' Go to.
bool go_to_node_named(uint32_t name)
{
    const int index = g_active && !g_dragging && name ? nodes::get_find(name) : -1;
    if (index < 0)
        return false;
    go_to_node(index);
    if (have_selection())
        go_to_selection();
    return true;
}

bool go_to_scenery(uint32_t sector)
{
    if (!g_active || g_dragging || !sector || !scenery::sector_of(sector) || scenery::why_not_movable(sector))
        return false;
    unpick();
    scenery::set_selection({sector});
    go_to_selection();
    return true;
}

void go_to_point(V3 pos, float distance)
{
    if (!g_active || g_dragging)
        return;
    unpick();
    get_look_at(pos, distance);
}

// --- for the arrange tools (UX7 - level_editor_modules.h's Edit) ---

namespace edit {

Selected get_selection()
{
    Selected out;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        out.kind = Kind::kObject;
        for (const objects::GameEntity* object : objects)
            out.ids.push_back(object->ident_);
    } else if (picked_node_index() >= 0) {
        out.kind = Kind::kElem;
        out.ids.push_back(g_node_name);
    } else if (const uint32_t wall = wall_only()) {
        out.kind = Kind::kWall;
        out.ids.push_back(wall);
    } else if (scenery::has_selection()) {
        out.kind = Kind::kScenery;
        out.ids = scenery::get_selection();
    }
    return out;
}

void choose_one(const Selected& selected)
{
    switch (selected.kind) {
    case Kind::kObject: {
        std::vector<objects::GameEntity*> objects;
        for (const uint32_t id : selected.ids) {
            auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
            if (object && !(object->object_flags_ & objects::kObjectFlagDead))
                objects.push_back(object);
        }
        unpick();
        select_objects(objects);
        break;
    }
    case Kind::kScenery:
        g_objects.clear();
        g_node_selected = false;
        view::clear_wall();
        scenery::set_selection(selected.ids);
        break;
    case Kind::kElem: {
        const int index = selected.ids.empty() ? -1 : nodes::get_find(selected.ids.front());
        if (index >= 0)
            select_node_only(index);
        break;
    }
    case Kind::kWall:
        unpick();
        if (!selected.ids.empty())
            view::select_wall(selected.ids.front());
        break;
    case Kind::kNone:
        unpick();
        break;
    }
}

V3 get_pos() { return selection_pos(); }
float current_base() { return get_selection_base(); }
V3 current_centre() { return selection_centre(); }

void get_begin() { begin_change(); }
void get_end()
{
    g_pending_forced = true;
    end_change();
    get_settle();
}

void place_at(V3 pos) { place_selection(pos); }

void turn_by(float radians)
{
    node_edit::Values node;
    if (node_values(&node)) {
        node.angles =
            node_edit::with_heading(node.angles, node_edit::get_heading_degrees(node.angles) + radians * 180.0f / kPi);
        set_selected_node(node);
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        turn_objects_by(objects, radians, selection_centre());
        return;
    }
    if (const int quarters = xform::get_quarters(radians))
        turn_selection(quarters);
}

bool scale_by(V3 factor, bool uniform) { return scale_selection_by(factor, uniform); }

std::vector<std::pair<uint32_t, uint32_t>> get_duplicate(const void* repeat)
{
    g_duplicate_pairs.clear();
    level_editor::get_duplicate(frame_from_yaw_pitch(g_yaw, g_pitch), static_cast<const xform::RepeatStep*>(repeat));
    return g_duplicate_pairs;
}

uint64_t top_serial() { return g_undo.empty() ? 0 : g_undo.back().serial; }
size_t deepness() { return g_undo.size(); }

void fold(size_t depth)
{
    depth = std::min(depth, g_undo.size());
    if (g_undo.size() - depth < 2)
        return;
    UndoStep group;
    group.kind = UndoStep::Kind::kGroup;
    const std::string label = g_undo.back().label;
    group.group.assign(g_undo.begin() + static_cast<long>(depth), g_undo.end());
    g_undo.erase(g_undo.begin() + static_cast<long>(depth), g_undo.end());
    group.where = selection_pos();
    push_undo(group, label.c_str());
}

bool take_back()
{
    if (g_undo.empty())
        return false;
    entities::stop_preview();
    UndoStep step = g_undo.back();
    g_undo.pop_back();
    carry::disarm();
    get_restore(step);
    mark_unsaved();
    return true;
}

void push_made(const std::vector<uint32_t>& objects, const std::vector<uint32_t>& sectors)
{
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    for (const uint32_t id : objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (!object)
            continue;
        uint32_t source = 0;
        for (const object_edit::DupFrom& c : object_edit::get_copies()) {
            if (c.name == id)
                source = c.source;
        }
        step.copies.push_back({id, source, object_edit::store_away(object)});
    }
    step.sectors = sectors;
    if (step.copies.empty() && step.sectors.empty())
        return;
    push_undo(step, "Copies");
}

void push_snapshot() { push_undo(get_snapshot()); }

void remove_copies(const std::vector<uint32_t>& ids)
{
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = true; // (undone: made again)
    for (const uint32_t id : ids) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (!object || !object_edit::is_copy(id))
            continue;
        uint32_t source = 0;
        for (const object_edit::DupFrom& c : object_edit::get_copies()) {
            if (c.name == id)
                source = c.source;
        }
        step.copies.push_back({id, source, object_edit::store_away(object)});
    }
    if (step.copies.empty())
        return;
    for (const MadeCopy& copy : step.copies)
        object_edit::remove_copy(copy.name);
    g_objects.clear();
    push_undo(step, "Remove copies");
}

void get_settle() { level_editor::get_settle(); }

bool set_field(uint32_t node, uint32_t key, int kind, const std::string& text)
{
    ui::Request request;
    request.kind = ui::Request::Kind::kFieldSet;
    request.node = node;
    request.id = key;
    request.value = kind;
    request.text = text;
    const size_t before = g_undo.size();
    const uint64_t serial = top_serial();
    field_request(request);
    return g_undo.size() != before || top_serial() != serial;
}

void chain_changed(const void* before, uint32_t select, const std::string& message)
{
    after_chain_change(*static_cast<const chains::State*>(before), select, message);
}

bool get_ground(V3 from, V3* ground) { return ground_below(from, ground); }
bool get_dragging() { return g_active && g_dragging; }
int drag_mode() { return static_cast<int>(g_drag_mode); }
void get_cursor(float* x, float* y)
{
    *x = g_cursor_x;
    *y = g_cursor_y;
}
bool get_ray(V3* origin, V3* dir)
{
    if (!g_active || !g_last_have_ray)
        return false;
    *origin = g_pos;
    *dir = g_last_ray;
    return true;
}
bool ground_mark(V3* base, V3* ground)
{
    if (!g_have_mark || !have_selection())
        return false;
    *base = g_mark_base;
    *ground = g_mark_ground;
    return true;
}
float get_yaw() { return g_yaw; }

bool browser_item(int* tab, uint32_t* id)
{
    if (!g_browser.open || g_browser.index < 0 || std::cmp_greater_equal(g_browser.index, g_browser.items.size()))
        return false;
    *tab = g_browser.tab;
    *id = core::at(g_browser.items, g_browser.index).id;
    return true;
}
void place_browser_item(V3 at, bool keep_open) { place_from_browser(at, keep_open); }

int place_armed(uint32_t* colour)
{
    *colour = 0;
    if (!g_place_open || g_place_armed < 0)
        return -1;
    if (std::cmp_less(g_place_armed, place_entries().size()))
        *colour = core::at(place_entries(), g_place_armed).colour;
    return g_place_armed;
}

V3 cursor_point()
{
    return level_editor::cursor_point(g_last_ray, g_last_have_ray, frame_from_yaw_pitch(g_yaw, g_pitch));
}

} // namespace edit

} // namespace frontend::level_editor
