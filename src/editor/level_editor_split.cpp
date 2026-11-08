// NOT ORIGINAL - the level editor's split view (ours). See level_editor_split.h.

#include "editor/level_editor_split.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/core/parse_number.h"
#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_gizmo_lines.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_internal.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_app.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_view.h"
#include "engine/ext/extension.h"
#include "engine/gfx/view_modes.h"
#include "platform/run_log.h"

namespace frontend::level_editor::split_view {

using namespace internal;
using view_cam::View;

namespace {

constexpr int kModeShift = 4; // the ini's number: type + 16 x mode
constexpr int kTypeMask = 0xF;
constexpr int kCollisionOnly = static_cast<int>(render::view_mode::Mode::kCollisionOnly);
constexpr float kHalf = 0.5F;
constexpr float kLookReach = 20000.0F;       // how far ahead the point a view is set to look at is looked for
constexpr float kFallbackLook = 1500.0F;     // the point ahead when nothing is there
constexpr float kMinStartEquivalent = 200.0F; // an ortho view's zoom when it starts (the distance to that point)
constexpr float kMaxStartEquivalent = 20000.0F;
constexpr float kDefaultEquivalent = 3000.0F; // the ortho views of a new quad layout
constexpr float kPerspectivePitch = -0.5F;    // a view made Perspective from an ortho one: looking down a little
constexpr float kGridMargin = 1.05F;          // the grid a little past the view's sides
constexpr int kMostGridLines = 400;           // each way
constexpr int kMajorEvery = 5;
constexpr uint32_t kGridMinor = 0x1CFFFFFFU;
constexpr uint32_t kGridMajor = 0x40FFFFFFU;
constexpr uint32_t kGridAxis = 0x90E0A040U;
constexpr float kGridNear = 0.1F;
constexpr std::array<float, 3> kGridSteps = {{1.0F, 2.0F, 5.0F}};
constexpr float kDecade = 10.0F;
constexpr float kSmallestSpacing = 1.0F;
constexpr double kSwitchMessageSeconds = 1.5;

// A view's state while it isn't the active one (the active one's is the editor's own: fly_camera, view_cam, the view
// mode, drawn_view).
struct ViewSlot {
    V3 pos{};
    float yaw{};
    float pitch{};
    float orbit_distance_ = kDefaultOrbitDistance;
    bool free_camera_ = true;
    view_cam::Slot ortho{};
    int mode{};
    DrawnView drawn{};
    ui::Overlay overlay{};
};

struct SplitState {
    bool read{};
    Layout layout = Layout::kSingle;
    bool maximised{};
    // (the ini's: each slot's type and mode when the views are made)
    std::array<View, kViews> types = {{View::kTop, View::kMenuSystem, View::kLeft, View::kNone}};
    std::array<int, kViews> modes{};
    std::array<ViewSlot, kViews> slots{};
    int active = kPerspectiveSlot;
    uint32_t scene{};
    bool ready{}; // (the slots made from the editor's camera in this level)
    bool kept{};  // (stopped: the active view's state in its slot, put back at the next start)
    // The frame's passes: the slots in drawing order (the active one last) and the one being drawn (-1: the active).
    std::array<int, kViews> order{};
    int passes{};
    int pass_slot = -1;
    // The keymap's actions.
    int maximise_action = -1;
};

SplitState& split_state()
{
    static core::NoDestroy<SplitState> state;
    return state.get();
}

ViewSlot& slot_of(int index) { return core::at(split_state().slots, index); }

V3 forward_of(float yaw, float pitch) { return frame_from_yaw_pitch(yaw, pitch).back * -1.0F; }

// The editor's camera state as a slot, and put back.
ViewSlot capture()
{
    ViewSlot slot;
    slot.pos = fly_camera().pos_;
    slot.yaw = fly_camera().yaw_;
    slot.pitch = fly_camera().pitch_;
    slot.orbit_distance_ = fly_camera().orbit_distance_;
    slot.free_camera_ = fly_camera().free_camera_;
    slot.ortho = view_cam::save_slot();
    slot.mode = overlays::view_mode();
    return slot;
}

void restore(const ViewSlot& slot)
{
    fly_camera().pos_ = slot.pos;
    fly_camera().yaw_ = slot.yaw;
    fly_camera().pitch_ = slot.pitch;
    fly_camera().orbit_distance_ = slot.orbit_distance_;
    fly_camera().free_camera_ = slot.free_camera_;
    view_cam::load_slot(slot.ortho);
    view_cam::stop_motion();
}

// The editor's camera state and a slot's exchanged (both ways the same: a second call puts them back). The view mode
// stays the editor's.
void exchange(int index)
{
    ViewSlot& slot = slot_of(index);
    std::swap(fly_camera().pos_, slot.pos);
    std::swap(fly_camera().yaw_, slot.yaw);
    std::swap(fly_camera().pitch_, slot.pitch);
    std::swap(fly_camera().orbit_distance_, slot.orbit_distance_);
    std::swap(fly_camera().free_camera_, slot.free_camera_);
    const view_cam::Slot ortho = view_cam::save_slot();
    view_cam::load_slot(slot.ortho);
    slot.ortho = ortho;
    std::swap(drawn_view(), slot.drawn);
}

// The point a view looks at: its ortho view's pivot, else the surface ahead, else a way ahead.
V3 look_point(const ViewSlot& slot)
{
    if (slot.ortho.view != View::kNone) {
        return slot.ortho.pivot;
    }
    const V3 forward = forward_of(slot.yaw, slot.pitch);
    V3 hit{};
    if (get_cast(slot.pos, slot.pos + forward * kLookReach, &hit)) {
        return hit;
    }
    return slot.pos + forward * kFallbackLook;
}

// A slot given a view type, looking at `pivot` with ortho zoom `equivalent`.
void set_type_of(ViewSlot* slot, View type, V3 pivot, float equivalent)
{
    if (axis_view(type)) {
        float yaw = 0.0F;
        float pitch = 0.0F;
        view_cam::axis_yaw_pitch(type, {.yaw = &yaw, .pitch = &pitch});
        const V3 eye = pivot - forward_of(yaw, pitch) * view_cam::eye_distance(equivalent);
        slot->ortho = {.view = type, .yaw = yaw, .pitch = pitch, .pivot = pivot, .equivalent = equivalent, .last_eye = eye};
        slot->pos = eye;
        slot->yaw = yaw;
        slot->pitch = pitch;
        slot->orbit_distance_ = view_cam::eye_distance(equivalent);
        slot->free_camera_ = true;
        return;
    }
    if (slot->ortho.view == View::kNone) {
        return;
    }
    // (from an ortho view: looking down at the same point from its zoom's distance)
    slot->ortho.view = View::kNone;
    slot->pitch = kPerspectivePitch;
    slot->pos = pivot - forward_of(slot->yaw, slot->pitch) * equivalent;
    slot->orbit_distance_ = equivalent;
    slot->free_camera_ = true;
}

void write_settings()
{
    const SplitState& state = split_state();
    config_set_int("editor", "split_layout", static_cast<int>(state.layout));
    config_set_int("editor", "split_maximised", state.maximised ? 1 : 0);
    for (int i = 0; i < kViews; ++i) {
        const std::string key = "split_view" + std::to_string(i + 1);
        config_set_int("editor", key.c_str(), pack_view(core::at(state.types, i), core::at(state.modes, i)));
    }
}

// Each slot's type and mode as they are now (the active one's: the editor's).
void note_settings()
{
    SplitState& state = split_state();
    for (int i = 0; i < kViews; ++i) {
        const bool is_active = i == state.active;
        core::at(state.types, i) = is_active ? view_cam::current_view() : slot_of(i).ortho.view;
        core::at(state.modes, i) = is_active ? overlays::view_mode() : slot_of(i).mode;
    }
}

void read_settings()
{
    SplitState& state = split_state();
    if (state.read) {
        return;
    }
    state.read = true;
    state.layout = static_cast<Layout>(std::clamp(config_int("editor", "split_layout", 0), 0, kLayouts - 1));
    state.maximised = config_int("editor", "split_maximised", 0) != 0;
    for (int i = 0; i < kViews; ++i) {
        const std::string key = "split_view" + std::to_string(i + 1);
        View type = View::kNone;
        int mode = 0;
        if (unpack_view(config_int("editor", key.c_str(), -1), &type, &mode)) {
            core::at(state.types, i) = type;
            core::at(state.modes, i) = mode;
        }
    }
}

// The renderer's mode follows the editor's while one view is shown (the original path draws with it).
void sync_renderer_mode()
{
    if (!several()) {
        overlays::swap_view_mode(overlays::view_mode(), true);
    }
}

// The slots made from the editor's camera: each looking at what the camera looks at, as its type says.
void make_slots()
{
    SplitState& state = split_state();
    state.ready = true;
    const ViewSlot here = capture();
    const V3 pivot = look_point(here);
    for (int i = 0; i < kViews; ++i) {
        ViewSlot& slot = slot_of(i);
        slot = here;
        slot.free_camera_ = true;
        slot.mode = core::at(state.modes, i);
        set_type_of(&slot, core::at(state.types, i), pivot, kDefaultEquivalent);
    }
    // (the active one is the editor's: its type and mode applied to the editor's state)
    const ViewSlot active = slot_of(state.active);
    if (active.ortho.view != View::kNone) {
        restore(active);
    }
    overlays::swap_view_mode(active.mode, true);
    get_log("level editor: split view %s, view %d active", layout_name(shown_layout()), state.active + 1);
}

// Another slot made the active one: the editor's state kept in the old one's slot, the new one's taken.
void activate(int index)
{
    SplitState& state = split_state();
    if (index == state.active || index < 0 || index >= kViews) {
        return;
    }
    ViewSlot& old_slot = slot_of(state.active);
    ViewSlot& new_slot = slot_of(index);
    old_slot.pos = fly_camera().pos_;
    old_slot.yaw = fly_camera().yaw_;
    old_slot.pitch = fly_camera().pitch_;
    old_slot.orbit_distance_ = fly_camera().orbit_distance_;
    old_slot.free_camera_ = fly_camera().free_camera_;
    old_slot.ortho = view_cam::save_slot();
    old_slot.mode = overlays::view_mode();
    std::swap(drawn_view(), new_slot.drawn);
    old_slot.drawn = std::move(new_slot.drawn);
    new_slot.drawn = {};
    drawn_view().last_have_ray_ = false;
    restore(new_slot);
    overlays::swap_view_mode(new_slot.mode, !several());
    state.active = index;
    if (debug_enabled()) {
        get_log("level editor: split view %d active (%s)", index + 1, view_cam::view_name(view_cam::current_view()));
    }
}

// The slot the active view becomes when the layout doesn't show the one it was.
void keep_active_shown()
{
    const Layout shown = shown_layout();
    const int count = shown_count(shown);
    for (int place = 0; place < count; ++place) {
        if (slot_at(shown, place) == split_state().active) {
            return;
        }
    }
    activate(slot_at(shown, 0));
}

} // namespace

Layout shown_layout() { return split_state().maximised ? Layout::kSingle : split_state().layout; }

// --- the pieces ----------------------------------------------------------------------------------------------------

const char* layout_name(Layout layout)
{
    switch (layout) {
    case Layout::kTwoSideBySide:
        return "Two Side by Side";
    case Layout::kTwoStacked:
        return "Two Stacked";
    case Layout::kFour:
        return "Four";
    default:
        return "Single";
    }
}

int shown_count(Layout layout)
{
    switch (layout) {
    case Layout::kTwoSideBySide:
    case Layout::kTwoStacked:
        return 2;
    case Layout::kFour:
        return kViews;
    default:
        return 1;
    }
}

int slot_at(Layout layout, int place)
{
    switch (layout) {
    case Layout::kTwoSideBySide:
    case Layout::kTwoStacked:
        // (the perspective view first, the top view beside it)
        return place == 0 ? kPerspectiveSlot : 0;
    case Layout::kFour:
        return std::clamp(place, 0, kViews - 1); // (3ds Max's: Top, Front / Left, Perspective)
    default:
        return split_state().active;
    }
}

Area area_at(Layout layout, int place)
{
    switch (layout) {
    case Layout::kTwoSideBySide:
        return {.x = place == 0 ? 0.0F : kHalf, .y = 0.0F, .width = kHalf, .height = 1.0F};
    case Layout::kTwoStacked:
        return {.x = 0.0F, .y = place == 0 ? 0.0F : kHalf, .width = 1.0F, .height = kHalf};
    case Layout::kFour:
        return {.x = (place % 2) == 0 ? 0.0F : kHalf, .y = place < 2 ? 0.0F : kHalf, .width = kHalf, .height = kHalf};
    default:
        return {};
    }
}

int place_holding(std::span<const Area> areas, float x, float y)
{
    for (int i = 0; std::cmp_less(i, areas.size()); ++i) {
        const Area& area = core::at(areas, i);
        if (x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height) {
            return i;
        }
    }
    return -1;
}

int pack_view(View type, int mode) { return static_cast<int>(type) + (mode << kModeShift); }

bool unpack_view(int value, View* type, int* mode)
{
    if (value < 0) {
        return false;
    }
    const int type_number = value & kTypeMask;
    const int mode_number = value >> kModeShift;
    if (type_number > static_cast<int>(View::kDealloc) || mode_number >= overlays::kViewModes) {
        return false;
    }
    *type = static_cast<View>(type_number);
    *mode = mode_number;
    return true;
}

float grid_spacing(float units_per_pixel, float min_pixels)
{
    const float least = std::max(units_per_pixel * min_pixels, kSmallestSpacing);
    float decade = kSmallestSpacing;
    while (decade * kGridSteps.back() < least) {
        decade *= kDecade;
    }
    for (const float step : kGridSteps) {
        if (decade * step >= least) {
            return decade * step;
        }
    }
    return decade * kDecade;
}

bool axis_view(View view) { return view != View::kNone && view != View::kDealloc; }

// --- the editor's state ----------------------------------------------------------------------------------------------

void kickoff(uint32_t scene)
{
    read_settings();
    SplitState& state = split_state();
    if (scene != state.scene) {
        state.scene = scene;
        state.ready = false;
        state.kept = false;
        state.active = kPerspectiveSlot;
    }
    state.pass_slot = -1;
}

void halt()
{
    SplitState& state = split_state();
    if (state.ready && fly_camera().camera_) {
        note_settings();
        write_settings();
        ViewSlot& slot = slot_of(state.active);
        DrawnView drawn = std::move(slot.drawn);
        slot = capture();
        slot.drawn = std::move(drawn);
        state.kept = true;
    }
    state.pass_slot = -1;
    gizmo_lines::set_all_views(false);
    gizmo_lines::set_muted(false);
    view::set_collision_forced(false);
}

void prepare_all()
{
    actions::ActStep maximise;
    maximise.id = "view_maximise";
    maximise.name = "Maximise / Restore View";
    maximise.category = "Camera";
    maximise.kind = actions::Kind::kPress;
    maximise.defaults = {{.scancode = SDL_SCANCODE_F11, .mods = 0}};
    maximise.keys = maximise.defaults;
    maximise.tip = "The view under the mouse fills the Scene panel; again: the views back";
    maximise.run = [] { toggle_maximise(); };
    maximise.checked = [] { return maximised() ? 1 : 0; };
    split_state().maximise_action = actions::enrol(maximise);
    static constexpr std::array<const char*, kLayouts> kIds = {
        {"layout_single", "layout_two_side", "layout_two_stacked", "layout_four"}};
    for (int i = 0; i < kLayouts; ++i) {
        actions::ActStep step;
        step.id = core::at(kIds, i);
        step.name = std::string("Layout: ") + layout_name(static_cast<Layout>(i));
        step.category = "Camera";
        step.kind = actions::Kind::kPress;
        step.tip = "The Scene panel's views";
        step.run = [i] { set_layout(static_cast<Layout>(i)); };
        step.checked = [i] { return split_state().layout == static_cast<Layout>(i) ? 1 : 0; };
        actions::enrol(step);
    }
}

Layout layout() { return split_state().layout; }

void set_layout(Layout layout)
{
    SplitState& state = split_state();
    if (layout >= Layout::kCount || (layout == state.layout && !state.maximised)) {
        return;
    }
    state.layout = layout;
    state.maximised = false;
    if (state.ready) {
        keep_active_shown();
    }
    sync_renderer_mode();
    note_settings();
    write_settings();
    hud::get_message(std::string("Layout: ") + layout_name(layout), kSwitchMessageSeconds);
    get_log("level editor: split view %s", layout_name(layout));
}

bool maximised() { return split_state().maximised; }

void toggle_maximise()
{
    SplitState& state = split_state();
    if (state.layout == Layout::kSingle) {
        hud::get_message("One view: nothing to maximise", kSwitchMessageSeconds);
        return;
    }
    state.maximised = !state.maximised;
    sync_renderer_mode();
    note_settings();
    write_settings();
    hud::get_message(state.maximised ? "View maximised" : "Views restored", kSwitchMessageSeconds);
}

bool several() { return shown_count(shown_layout()) > 1; }

int active() { return split_state().active; }

void make_active(int slot)
{
    if (split_state().ready) {
        activate(slot);
    }
}

void tick_frame()
{
    SplitState& state = split_state();
    if (!fly_camera().camera_) {
        return;
    }
    if (!state.ready) {
        make_slots();
    } else if (state.kept && several()) {
        // (back from play from here, or the editor opened again in the level: the active view as it was, its ortho
        // view too - one view alone starts where the game's camera is, as ever)
        DrawnView drawn = std::move(slot_of(state.active).drawn);
        restore(slot_of(state.active));
        overlays::swap_view_mode(slot_of(state.active).mode, false);
        slot_of(state.active).drawn = std::move(drawn);
    }
    state.kept = false;
    if (!several()) {
        return;
    }
    // (an ortho view of an axis keeps its place when something is selected - tick_keys: F centres it)
    if (axis_view(view_cam::current_view())) {
        fly_camera().free_camera_ = true;
    }
    // The view under the mouse made the active one (not while a button, a drag, an orbit or a key move is under way).
    const Mouse& mouse = stored_mouse();
    if (mouse.left || mouse.right || drag_session().dragging_ || view_cam::get_orbiting() || fly_camera().key_moving_ ||
        triggers::resizing() || !ui::app::scene_hovered()) {
        return;
    }
    const ui_layout::Rect2 rect = ui::app::scene_rect();
    if (rect.width <= 0.0F || rect.height <= 0.0F) {
        return;
    }
    const Layout shown = shown_layout();
    std::array<Area, kViews> areas{};
    const int count = shown_count(shown);
    for (int place = 0; place < count; ++place) {
        core::at(areas, place) = area_at(shown, place);
    }
    const int place = place_holding(std::span<const Area>(areas).first(static_cast<std::size_t>(count)),
                                    (mouse.x - rect.x) / rect.width, (mouse.y - rect.y) / rect.height);
    if (place >= 0) {
        activate(slot_at(shown, place));
    }
}

void tick_keys()
{
    const SplitState& state = split_state();
    if (!state.ready || !fly_camera().camera_) {
        return;
    }
    if (state.maximise_action >= 0 && actions::squeezed(state.maximise_action)) {
        toggle_maximise();
    }
    // F in an ortho view of an axis: centred on the selection (level_editor_camera.h's F zooms it to fit)
    if (several() && axis_view(view_cam::current_view()) && actions::squeezed(actions::kFrameSelection) &&
        have_selection()) {
        view_cam::Slot ortho = view_cam::save_slot();
        ortho.pivot = target_of();
        ortho.last_eye = ortho.pivot - forward_of(ortho.yaw, ortho.pitch) * view_cam::eye_distance(ortho.equivalent);
        view_cam::load_slot(ortho);
        fly_camera().pos_ = ortho.last_eye;
    }
}

void pan_ortho(float* rel_x, float* rel_y, bool right)
{
    if (!right || (*rel_x == 0.0F && *rel_y == 0.0F) || !several() || !axis_view(view_cam::current_view()) ||
        drag_session().dragging_ || !drawn_view().have_view_) {
        return;
    }
    const auto height = static_cast<float>(core::at(drawn_view().viewport_, 3));
    if (height <= 0.0F) {
        return;
    }
    const float eye = view_cam::eye_distance(view_cam::save_slot().equivalent);
    const float units_per_pixel = 2.0F * eye * std::fabs(drawn_view().lens_.tan_y_) / height;
    const TickCycle frame = fly_camera().frame();
    fly_camera().pos_ = fly_camera().pos_ - frame.right * (*rel_x * units_per_pixel) + frame.up * (*rel_y * units_per_pixel);
    fly_camera().free_camera_ = true;
    *rel_x = 0.0F;
    *rel_y = 0.0F;
}

// --- the renderer's side ---------------------------------------------------------------------------------------------

int view_count()
{
    SplitState& state = split_state();
    state.passes = 0;
    // (one view: the original's path - unless it is an ortho one, drawn without the level's fog)
    if (!state.ready || !fly_camera().camera_ || (!several() && !view_cam::ortho())) {
        return 0;
    }
    const Layout shown = shown_layout();
    const int count = shown_count(shown);
    for (int place = 0; place < count; ++place) {
        const int slot = slot_at(shown, place);
        if (slot != state.active) {
            core::at(state.order, state.passes++) = slot;
        }
    }
    core::at(state.order, state.passes++) = state.active; // (last: the frame's camera is the active view's after)
    return state.passes;
}

bool begin_view(int index, ext::ViewPass* pass)
{
    SplitState& state = split_state();
    if (index < 0 || index >= state.passes) {
        return false;
    }
    const int slot = core::at(state.order, index);
    const Layout shown = shown_layout();
    const int count = shown_count(shown);
    for (int place = 0; place < count; ++place) {
        if (slot_at(shown, place) == slot) {
            const Area area = area_at(shown, place);
            pass->rect = {{area.x, area.y, area.width, area.height}};
        }
    }
    const bool is_active = slot == state.active;
    pass->mode = is_active ? overlays::view_mode() : slot_of(slot).mode;
    pass->fog = (is_active ? view_cam::current_view() : slot_of(slot).ortho.view) == View::kNone;
    state.pass_slot = is_active ? -1 : slot;
    return true;
}

void end_views() { split_state().pass_slot = -1; }

bool pass_pose(V3* pos, TickCycle* frame)
{
    const int slot = split_state().pass_slot;
    if (slot < 0) {
        return false;
    }
    *pos = slot_of(slot).pos;
    *frame = frame_from_yaw_pitch(slot_of(slot).yaw, slot_of(slot).pitch);
    return true;
}

float pass_view_angle(float base_degrees)
{
    const int slot = split_state().pass_slot;
    return slot < 0 ? view_cam::adjust_view_angle(base_degrees)
                    : view_cam::slot_view_angle(slot_of(slot).ortho, base_degrees);
}

float pass_near_plane()
{
    const int slot = split_state().pass_slot;
    return slot < 0 ? view_cam::get_near_plane() : view_cam::slot_near_plane(slot_of(slot).ortho);
}

bool note_pass(const Lens& lens, std::span<const uint32_t, 4> viewport)
{
    const int slot = split_state().pass_slot;
    if (slot < 0) {
        return false;
    }
    DrawnView& drawn = slot_of(slot).drawn;
    drawn.lens_ = lens;
    std::ranges::copy(viewport, drawn.viewport_.begin());
    drawn.have_view_ = true;
    return true;
}

// --- the overlays ------------------------------------------------------------------------------------------------------

void gather_others(ui::Overlay* overlay, const std::function<void()>& gather)
{
    const SplitState& state = split_state();
    if (state.ready && several()) {
        const Layout shown = shown_layout();
        const int count = shown_count(shown);
        gizmo_lines::set_muted(true);
        for (int place = 0; place < count; ++place) {
            const int slot = slot_at(shown, place);
            if (slot == state.active || !slot_of(slot).drawn.have_view_) {
                continue;
            }
            view::set_collision_forced(slot_of(slot).mode == kCollisionOnly);
            exchange(slot);
            ui::get_clear(*overlay);
            gather();
            exchange(slot);
            std::swap(*overlay, slot_of(slot).overlay);
        }
        gizmo_lines::set_muted(false);
    }
    ui::get_clear(*overlay);
    view::set_collision_forced(several() && overlays::view_mode() == kCollisionOnly);
}

void add_grid(ui::Overlay* overlay)
{
    const view_cam::Slot ortho = view_cam::save_slot();
    const DrawnView& drawn = drawn_view();
    if (!axis_view(ortho.view) || !drawn.have_view_ || core::at(drawn.viewport_, 2) == 0U ||
        core::at(drawn.viewport_, 3) == 0U) {
        return;
    }
    const auto width = static_cast<float>(core::at(drawn.viewport_, 2));
    const auto height = static_cast<float>(core::at(drawn.viewport_, 3));
    const float eye = view_cam::eye_distance(ortho.equivalent);
    const float units_per_pixel = 2.0F * eye * std::fabs(drawn.lens_.tan_y_) / height;
    if (units_per_pixel <= 0.0F) {
        return;
    }
    const float spacing = grid_spacing(units_per_pixel, kGridPixels);
    const TickCycle frame = fly_camera().frame();
    // (the world's axes most along the view's right and up: the grid's lines run along them)
    const auto axis_along = [](V3 direction) {
        const float ax = std::fabs(direction.x);
        const float ay = std::fabs(direction.y);
        const float az = std::fabs(direction.z);
        if (ax >= ay && ax >= az) {
            return V3{.x = direction.x < 0.0F ? -1.0F : 1.0F, .y = 0.0F, .z = 0.0F};
        }
        if (ay >= az) {
            return V3{.x = 0.0F, .y = direction.y < 0.0F ? -1.0F : 1.0F, .z = 0.0F};
        }
        return V3{.x = 0.0F, .y = 0.0F, .z = direction.z < 0.0F ? -1.0F : 1.0F};
    };
    const V3 across = axis_along(frame.right);
    const V3 upward = axis_along(frame.up);
    const float half_across = units_per_pixel * width * kHalf * kGridMargin;
    const float half_up = units_per_pixel * height * kHalf * kGridMargin;
    std::vector<ui::Seg> lines;
    const auto project = [&](V3 point, float* px, float* py) {
        float nx = 0.0F;
        float ny = 0.0F;
        if (!cast_onto(fly_camera().pos_, frame, drawn.lens_, point, kGridNear, &nx, &ny)) {
            return false;
        }
        ndc_to_pixel(drawn.viewport_, nx, ny, px, py);
        return true;
    };
    // Lines across `along` (each at a multiple of the spacing along it), as long as the view is along `other`.
    struct Family {
        V3 along;
        V3 other;
        float half_along;
        float half_other;
    };
    const auto add_family = [&](const Family& family) {
        const auto& [along, other, half_along, half_other] = family;
        const float centre = get_dot(ortho.pivot, along);
        const auto first = static_cast<int>(std::floor((centre - half_along) / spacing));
        const auto last = static_cast<int>(std::ceil((centre + half_along) / spacing));
        if (last - first > kMostGridLines) {
            return;
        }
        for (int step = first; step <= last; ++step) {
            const float at = static_cast<float>(step) * spacing;
            const V3 middle = ortho.pivot + along * (at - centre);
            float ax = 0.0F;
            float ay = 0.0F;
            float bx = 0.0F;
            float by = 0.0F;
            if (!project(middle - other * half_other, &ax, &ay) || !project(middle + other * half_other, &bx, &by)) {
                continue;
            }
            uint32_t colour = kGridMinor;
            if (step == 0) {
                colour = kGridAxis;
            } else if (step % kMajorEvery == 0) {
                colour = kGridMajor;
            }
            lines.push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = colour, .thickness = 1.0F});
        }
    };
    add_family({.along = across, .other = upward, .half_along = half_across, .half_other = half_up});
    add_family({.along = upward, .other = across, .half_along = half_up, .half_other = half_across});
    overlay->lines.insert(overlay->lines.begin(), lines.begin(), lines.end());
}

// --- what the Scene panel shows and changes ---------------------------------------------------------------------------

const ui::Overlay* overlay_of(int slot)
{
    if (slot < 0 || slot >= kViews || slot == split_state().active) {
        return nullptr;
    }
    return &slot_of(slot).overlay;
}

View type_of(int slot)
{
    if (slot == split_state().active) {
        return view_cam::current_view();
    }
    return slot >= 0 && slot < kViews ? slot_of(slot).ortho.view : View::kNone;
}

int mode_of(int slot)
{
    if (slot == split_state().active) {
        return overlays::view_mode();
    }
    return slot >= 0 && slot < kViews ? slot_of(slot).mode : 0;
}

void set_type(int slot, View type)
{
    const SplitState& state = split_state();
    if (slot < 0 || slot >= kViews || !state.ready) {
        return;
    }
    const bool is_active = slot == state.active;
    ViewSlot here = is_active ? capture() : slot_of(slot);
    const V3 pivot = look_point(here);
    float equivalent = here.ortho.equivalent;
    if (here.ortho.view == View::kNone) {
        const V3 delta = pivot - here.pos;
        equivalent = std::clamp(std::sqrt(get_dot(delta, delta)), kMinStartEquivalent, kMaxStartEquivalent);
    }
    set_type_of(&here, type, pivot, equivalent);
    if (is_active) {
        restore(here);
    } else {
        DrawnView drawn = std::move(slot_of(slot).drawn);
        ui::Overlay overlay = std::move(slot_of(slot).overlay);
        slot_of(slot) = here;
        slot_of(slot).drawn = std::move(drawn);
        slot_of(slot).overlay = std::move(overlay);
    }
    note_settings();
    write_settings();
}

void set_mode(int slot, int mode)
{
    const SplitState& state = split_state();
    if (slot < 0 || slot >= kViews || mode < 0 || mode >= overlays::kViewModes) {
        return;
    }
    if (slot == state.active) {
        if (several()) {
            overlays::swap_view_mode(mode, false);
        } else {
            overlays::change_view_mode(mode);
        }
    } else {
        slot_of(slot).mode = mode;
    }
    note_settings();
    write_settings();
}

// --- the automation channel --------------------------------------------------------------------------------------------

namespace {

constexpr std::array<const char*, kLayouts> kLayoutWords = {{"single", "side", "stacked", "four"}};
constexpr float kAxisReach = 100000.0F; // the straight ray's half length (a check, not a pick)

bool read_float(const std::string& text, float* value)
{
    *value = static_cast<float>(text::to_double(text.c_str()));
    return !text.empty();
}

// The world axis most along a direction.
V3 nearest_axis(V3 direction)
{
    const float ax = std::fabs(direction.x);
    const float ay = std::fabs(direction.y);
    const float az = std::fabs(direction.z);
    if (ax >= ay && ax >= az) {
        return V3{.x = direction.x < 0.0F ? -1.0F : 1.0F, .y = 0.0F, .z = 0.0F};
    }
    if (ay >= az) {
        return V3{.x = 0.0F, .y = direction.y < 0.0F ? -1.0F : 1.0F, .z = 0.0F};
    }
    return V3{.x = 0.0F, .y = 0.0F, .z = direction.z < 0.0F ? -1.0F : 1.0F};
}

// A point of a view: parts of its width and height.
struct ViewPoint {
    float x = 0.0F;
    float y = 0.0F;
};

std::string pick_in_view(int slot, ViewPoint point)
{
    const float part_x = point.x;
    const float part_y = point.y;
    const SplitState& state = split_state();
    const bool is_active = slot == state.active;
    const DrawnView& drawn = is_active ? drawn_view() : slot_of(slot).drawn;
    if (!state.ready || !drawn.have_view_) {
        return "error: that view isn't drawn (yet)";
    }
    const V3 pos = is_active ? fly_camera().pos_ : slot_of(slot).pos;
    const TickCycle frame = is_active ? fly_camera().frame() : frame_from_yaw_pitch(slot_of(slot).yaw, slot_of(slot).pitch);
    const view_cam::Slot ortho = is_active ? view_cam::save_slot() : slot_of(slot).ortho;
    const float px = static_cast<float>(core::at(drawn.viewport_, 0)) + (part_x * static_cast<float>(core::at(drawn.viewport_, 2)));
    const float py = static_cast<float>(core::at(drawn.viewport_, 1)) + (part_y * static_cast<float>(core::at(drawn.viewport_, 3)));
    float nx = 0.0F;
    float ny = 0.0F;
    if (!pixel_to_ndc(drawn.viewport_, px, py, &nx, &ny)) {
        return "error: no view there";
    }
    const V3 dir = ray_direction(frame, drawn.lens_, nx, ny);
    const V3 unit = make_unit(dir);
    scenery::Hit hit;
    const bool any = scenery::get_pick(pos, unit, kMaxPickDistance, &hit);
    // (a plain click's piece, else the structure it passes - level_editor_picking.h)
    const bool picked = any && (hit.checksum != 0U || hit.nearest != 0U);
    const uint32_t piece = hit.checksum != 0U ? hit.checksum : hit.nearest;
    const V3 at = pos + unit * (hit.checksum != 0U ? hit.t : hit.nearest_t_);
    std::string text = text::format("view %d (%s) at pixel (%.0f, %.0f): ", slot + 1, view_cam::view_name(ortho.view), px, py);
    if (picked) {
        text::append_format(text, "%s %s at (%.0f, %.0f, %.0f)", hit.checksum != 0U ? "picks" : "passes over", nodes::caption(piece).c_str(), at.x, at.y, at.z);
        if (hit.checksum != 0U) {
            text += "; " + scenery::describe_units(piece); // (what a click there selects - level_editor_scenery.h)
        }
    } else {
        text += "picks nothing";
    }
    if (axis_view(ortho.view)) {
        // (the same point on the pivot's plane, a ray straight along the view's axis through it: where it meets the
        // level's collision)
        const V3 axis = nearest_axis(frame.back * -1.0F);
        const V3 on_plane = pos + dir * view_cam::eye_distance(ortho.equivalent);
        V3 straight{};
        if (get_cast(on_plane - axis * kAxisReach, on_plane + axis * kAxisReach, &straight)) {
            const V3 apart = straight - at;
            text::append_format(text, "; straight along the axis: (%.0f, %.0f, %.0f)%s", straight.x, straight.y, straight.z,
                                picked ? text::format(", %.1f apart", std::sqrt(get_dot(apart, apart))).c_str() : "");
        } else {
            text += "; straight along the axis: nothing";
        }
    }
    get_log("level editor: split view pick - %s", text.c_str());
    return "ok " + text;
}

} // namespace

std::string automation(const std::vector<std::string>& words)
{
    const std::string verb = words.size() > 1 ? words.at(1) : std::string();
    if (verb == "split" && words.size() > 2) {
        if (words.at(2) == "maximise") {
            toggle_maximise();
            return maximised() ? "ok maximised" : "ok restored";
        }
        // (`split active <view>`: as the mouse going to it)
        float view = 0.0F;
        if (words.at(2) == "active" && words.size() > 3 && read_float(words.at(3), &view) && view >= 1.0F &&
            view <= static_cast<float>(kViews)) {
            make_active(static_cast<int>(view) - 1);
            return text::format("ok view %d active (%s)", active() + 1, view_cam::view_name(view_cam::current_view()));
        }
        for (int i = 0; i < kLayouts; ++i) {
            if (words.at(2) == core::at(kLayoutWords, i)) {
                set_layout(static_cast<Layout>(i));
                return std::string("ok ") + layout_name(static_cast<Layout>(i));
            }
        }
    } else if (verb == "pick" && words.size() > 4) {
        float view = 0.0F;
        float part_x = 0.0F;
        float part_y = 0.0F;
        if (read_float(words.at(2), &view) && read_float(words.at(3), &part_x) && read_float(words.at(4), &part_y) &&
            view >= 1.0F && view <= static_cast<float>(kViews)) {
            return pick_in_view(static_cast<int>(view) - 1, {.x = std::clamp(part_x, 0.0F, 1.0F), .y = std::clamp(part_y, 0.0F, 1.0F)});
        }
    }
    return "error: fe editor split single | side | stacked | four | maximise | active <view 1-4> <timeout>; fe editor pick <view 1-4> <x 0-1> <y 0-1>";
}

} // namespace frontend::level_editor::split_view