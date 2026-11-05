// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's). See
// level_editor.h. This file holds the editor's state, its events, start and stop, the frame's entry and the drawing
// hook; the pieces (geometry / selection / undo / drag / draw / panels / place / requests / commands) are level_editor_<name>.cpp, sharing level_editor_internal.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include "engine/pad_input/input_source.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_fields.h"
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_nav.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_arrange.h"
#include "base/maths/vec2.h"
#include "base/core/format.h"
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
#include "editor/level_editor_layout.h"
#include "editor/level_editor_links.h"
#include "editor/level_editor_modules.h"
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
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "frontend/mouse_menus.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/screen.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "engine/pad_input/input_event.h"
#include "platform/run_log.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/maths/trig.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
namespace {

constexpr double kWelcomeSeconds = 3.0;
constexpr double kEditedSeconds = 5.0;
constexpr int kMaxRailLogs = 8;             // misplaced rail points logged at most
constexpr float kGroundProbeDepth = 5000.0F;
constexpr float kTinyForwardLength = 0.0001F;

constexpr double kBriefSeconds = 1.5;
constexpr double kPairSeconds = 2.0;

constexpr double kResizeHintSeconds = 3.0;
constexpr double kAutosaveNoticeSeconds = 20.0;

constexpr int kKeyRepeatDelay = 400;        // milliseconds before a held key repeats ...
constexpr int kKeyRepeatStep = 83;          // ... the first repeat comes after this many of them

constexpr uint32_t kMillisecondsPerSecond = 1000U;

constexpr double kMaxFrameSeconds = 0.1;    // a frame counts for at most this long
constexpr double kFastFlyFactor = 4.0;      // Shift flies four times as fast
constexpr float kMinWheelStep = 20.0F;
constexpr float kWheelStepFraction = 0.1F;  // a wheel notch moves a tenth of the distance
constexpr double kWheelZoomStep = 0.85;     // an orbit wheel notch zooms by this factor
constexpr double kSpeedStepFactor = 1.5;    // a fly speed step changes the speed by this factor
constexpr int kDebugLogFrames = 60;         // the debug log reports every this many frames
constexpr float kDragStartPixels = 4.0F;    // a press that moved this far is a drag
constexpr float kHandlePickPixels = 8.0F;   // handles are picked within this many pixels at the reference view height

constexpr uint32_t kSegmentColour = 0xFF40E0FF;   // ARGB

constexpr std::uint32_t kPos = 0x7F261953U; // Pos

constexpr float kLookRadiansPerPixel = 0.0035F;

constexpr double kObjectSpeed = 150.0; // units a second (Shift: 4x)
constexpr double kWheelStep = 120.0;   // units a notch while flying (Shift: 4x)

} // namespace
namespace internal {

LevelEditorState& level_editor_state()
{
    static core::NoDestroy<LevelEditorState> state;
    return state.get();
}

// A change not saved yet (undo / redo steps, layouts restored).
void mark_unsaved()
{
    level_editor_state().unsaved_ = true;
    level_editor_state().unsaved_load_ = layout::level_loads();
    ++level_editor_state().changes_;
}

} // namespace internal
namespace {

// The crash logger's hook (platform/run_log.h): what the editor was doing, and its unsaved changes autosaved.
void on_crash()
{
    static int logged = 0;
    if (!level_editor_state().active_ && !level_editor_state().unsaved_) {
        return;
    }
    if (logged < 4) {
        ++logged;
        get_log("level editor: %s at the exception; last action: %s", level_editor_state().active_ ? "running" : "not running",
                level_editor_state().last_action_.data());
    }
    layout::write_autosave_from_crash();
}

} // namespace
namespace internal {

Mouse& stored_mouse()
{
    static core::NoDestroy<Mouse> mouse;
    return mouse.get();
}

DragState& drag_state()
{
    static core::NoDestroy<DragState> state;
    return state.get();
}

PickCycle& pick_cycle()
{
    static core::NoDestroy<PickCycle> state;
    return state.get();
}

Browser& stored_browser()
{
    static core::NoDestroy<Browser> browser;
    return browser.get();
}

} // namespace internal
namespace {

void read_window_size(uint32_t window_id)
{
    input::window_size(window_id, &stored_mouse().window_width_, &stored_mouse().window_height_);
}

void handle_event(const input::Event& event)
{
    if (!level_editor_state().active_) {
        return;
    }
    // (the UI first: over a panel, or while it wants the keys, the event is its own - level_editor_ui.h)
    if (ui::underway() &&
        !ui::route_event(event, drag_state().dragging_ || stored_mouse().right || triggers::resizing())) {
        return;
    }
    switch (event.type) {
    case input::EventType::kMouseMotion:
        stored_mouse().x = static_cast<float>(event.x);
        stored_mouse().y = static_cast<float>(event.y);
        stored_mouse().rel_x_ += static_cast<float>(event.dx);
        stored_mouse().rel_y_ += static_cast<float>(event.dy);
        read_window_size(event.window);
        break;
    case input::EventType::kMouseButtonDown:
    case input::EventType::kMouseButtonUp: {
        const bool down = event.type == input::EventType::kMouseButtonDown;
        if (!level_editor_state().relative_) {
            stored_mouse().x = static_cast<float>(event.x);
            stored_mouse().y = static_cast<float>(event.y);
        }
        read_window_size(event.window);
        if (event.button == input::kMouseLeft) {
            stored_mouse().left = down;
            ++(down ? stored_mouse().left_presses_ : stored_mouse().left_releases_);
        } else if (event.button == input::kMouseRight) {
            stored_mouse().right = down;
        } else if (event.button == input::kMouseMiddle && down) {
            ++stored_mouse().middle_presses_;
        }
        break;
    }
    case input::EventType::kMouseWheel:
        stored_mouse().wheel += event.wheel_flipped ? -event.wheel : event.wheel;
        break;
    case input::EventType::kKeyDown:
        // While dragging: X / Y / Z, digits, - . and Backspace typed (level_editor_transform.h), Enter applies.
        if (drag_state().dragging_) {
            const auto code = static_cast<SDL_Scancode>(event.key);
            char character = 0;
            if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
                character = static_cast<char>('1' + (code - SDL_SCANCODE_1));
            } else if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) {
                character = static_cast<char>('1' + (code - SDL_SCANCODE_KP_1));
            } else if (code == SDL_SCANCODE_0 || code == SDL_SCANCODE_KP_0) {
                character = '0';
            } else if (code == SDL_SCANCODE_MINUS || code == SDL_SCANCODE_KP_MINUS) {
                character = '-';
            } else if (code == SDL_SCANCODE_PERIOD || code == SDL_SCANCODE_KP_PERIOD) {
                character = '.';
            } else if (code == SDL_SCANCODE_BACKSPACE) {
                character = '\b';
            } else if (!event.repeat && code >= SDL_SCANCODE_X && code <= SDL_SCANCODE_Z) {
                character = static_cast<char>('x' + (code - SDL_SCANCODE_X));
            }
            if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
                drag_state().typed_apply_ = true;
                break;
            }
            if ((character != '\0') && xform::type_key(&drag_state().typed_, character)) {
                break;
            }
        }
        // Escape: first what's open (a drag put back, the browser, the help), then the selection, then leave.
        if (event.key == SDL_SCANCODE_ESCAPE && !event.repeat &&
            !actions::get_capturing()) {
            if (drag_state().dragging_ || stored_browser().open || drag_state().help_ || pick_cycle().place_open_ ||
                (pick_cycle().confirm_hide_ != 0U) || pick_cycle().confirm_reset_ ||
                chains::get_drawing() != chains::Blit::kNone || chains::wants_pick() ||
                !drag_state().objects_.empty() || scenery::has_selection() || drag_state().node_selected_ ||
                ux8::wants_escape() || // (UX8: a pie, walking)
                ux9::occupied()) {     // (UX9: a tool armed)
                drag_state().cancel_requested_ = true;
            } else {
                level_editor_state().exit_requested_ = true;
            }
        }
        break;
    default:
        break;
    }
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

bool live_now() { return level_editor_state().active_; }

namespace {

// NOT ORIGINAL grouping - whether occlusion is off, in the editor and in play (were globals). Made on first use, never
// destroyed (occlusion_switch()).
struct OcclusionSwitch {
    // Occlusion culling switched off while editing (O; render::occlusion_off).
    bool occlusion_off_ = true;
    bool occlusion_in_play_{}; // (render::occlusion_off as it was when the editor opened)
};

OcclusionSwitch& occlusion_switch()
{
    static core::NoDestroy<OcclusionSwitch> state;
    return state.get();
}

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
    if (drag_state().dragging_ || drag_state().cancel_requested_ || stored_browser().open || drag_state().help_ || have_selection() || drag_state().node_selected_ ||
        chains::get_drawing() != chains::Blit::kNone || chains::wants_pick() || !ui::keys_to_editor()) {
        level_editor_state().pause_key_released_ = false;
        return false;
    }
    return pass_key(&level_editor_state().pause_key_released_, down);
}

// (offline only, outside career-type modes: no LEVEL EDITOR in a network session or in story / classic... -
// level_editor_layout.h)
bool cfunc_available(script::Struct* /*params*/, script::Context /*context*/)
{
    return is_switched_on() && render::current_level() != 0 && !layout::get_refused();
}

bool cfunc_start(script::Struct* /*params*/, script::Context /*context*/)
{
    if (!is_switched_on()) {
        get_log("level editor: off ([features] level_editor=0)");
        return false;
    }
    if (level_editor_state().active_) {
        return true;
    }
    // The level becomes its edited copy (its saved layout put in place if it wasn't) - never in a network session or a
    // career-type mode (the player told why on screen: the pause menu stays).
    std::string edited;
    if (!layout::enter_edited_copy(&edited)) {
        get_log("level editor: not started - %s", edited.c_str());
        layout::panel_message(edited.c_str());
        return false;
    }
    level_editor_state().active_ = true;
    level_editor_state().camera_ = nullptr;
    level_editor_state().have_view_ = false;
    level_editor_state().last_counter_ = 0;
    level_editor_state().pause_key_released_ = false;
    level_editor_state().exit_requested_ = false;
    drag_state().cancel_requested_ = false;
    drag_state().hover_ = {};
    drag_state().objects_.clear();
    drag_state().hover_sector_ = 0;
    scenery::get_reset();
    drag_state().dragging_ = false;
    drag_state().axis_ = drag_state().hover_axis_ = -1;
    undo_steps().undo_.clear();
    undo_steps().redo_.clear();
    undo_steps().has_pending_ = false;
    level_editor_state().moves_ = 0;
    drag_state().help_ = false;
    stored_browser() = Browser{};
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
    occlusion_switch().occlusion_in_play_ = render::occlusion_off();
    // (the camera tools and the preferences - level_editor_camera.h, level_editor_prefs.h)
    level_editor_state().speed_ = static_cast<float>(prefs::get_pull().fly_speed_);
    view_cam::kickoff(scenery::world_scene_key(scenery::main_scene()));
    play_test::on_editor_start(); // (the trail's run ends; the level's trails file read - level_editor_playtest.h)
    actions::seed_keys(input::keys()); // (the key that opened the editor doesn't act at once)
    render::occlusion_off() = occlusion_switch().occlusion_off_;
    pick_cycle().hud_taken_down_ = 0;
    drag_state().families_ = static_cast<uint32_t>(config_int("editor", "families", static_cast<int>(nodes::kAllFamilies))) &
                 nodes::kAllFamilies;
    drag_state().pick_filter_ =
        static_cast<uint32_t>(config_int("editor", "pick", static_cast<int>(picking::kAll))) & picking::kAll;
    pick_cycle().cycle_ = Cycle{};
    pick_cycle().hover_cycle_ = -1;
    drag_state().hover_passed_ = 0;
    drag_state().node_selected_ = false;
    drag_state().field_editing_ = false;
    drag_state().hover_node_ = -1;
    pick_cycle().pickables_.clear();
    hud::get_reset(!ui);
    stored_mouse() = Mouse{};
    level_editor_state().saved_cursor_ = input::show_cursor(input::kCursorQuery);
    input::show_cursor(input::kCursorShow);
    // (unsaved changes of an earlier level - its autosave was written when the editor was left - aren't this level's)
    if (level_editor_state().unsaved_load_ != layout::level_loads()) {
        level_editor_state().unsaved_ = false;
    }
    level_editor_state().snapshot_changes_ = level_editor_state().changes_ - 1; // (a snapshot on the first frame, if unsaved)
    level_editor_state().last_autosave_ = SDL_GetTicks();
    level_editor_state().offer_pending_ = level_editor_state().offer_show_ = layout::autosave_offered();
    note_action("entered the editor");
    get_log("level editor: on");
    hud::get_message(edited.empty() ? "Level Editor - F1: Keyboard Shortcuts" : edited, edited.empty() ? kWelcomeSeconds : kEditedSeconds);
    if (debug_enabled()) {
        object_edit::log_census();
        scenery::log_census();
        // (rail points away from their nodes' pos=: where the node layer shows them - the rail manager's)
        int away = 0;
        for (const nodes::Elem& elem : nodes::get_all()) {
            script::Struct* structure = elem.family == nodes::Family::kRail ? nodes::struct_of(elem) : nullptr;
            script::VectorValue v = {};
            V3 rail{};
            if (!structure || !structure->fetch_vec(kPos, &v, 0) || !scenery::get_rail_point(elem.name, &rail) ||
                (rail.x == v.x && rail.y == v.y && rail.z == v.z)) {
                continue;
            }
            if (++away <= kMaxRailLogs) {
                get_log(
                    "level editor: rail point %s at (%.1f, %.1f, %.1f) - its node's pos= (%.1f, %.1f, %.1f), shown at "
                    "(%.1f, %.1f, %.1f)",
                    nodes::caption(elem.name).c_str(), rail.x, rail.y, rail.z, v.x, v.y, v.z, elem.pos.x, elem.pos.y,
                    elem.pos.z);
            }
        }
        get_log("level editor: %d rail points away from their nodes' pos=", away);
    }
    return true;
}

namespace internal {

// The ground below a point: the game's collision ray (a ProbeRay: constructor 0x00417760; 0x00417710 (start, end,
// movables) casts it - thiscall, RET 0xC, true on a hit; the hit's normal at +0x24, its point at +0x34), static
// collision only, from `from` along `to - from`.
bool get_cast(V3 from, V3 to, V3* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& point = ray->hit().point;
    *hit = {.x = point[0], .y = point[1], .z = point[2]};
    return true;
}
bool ground_below(V3 from, V3* ground) { return get_cast(from, {.x = from.x, .y = from.y - kGroundProbeDepth, .z = from.z}, ground); }

} // namespace internal
namespace {

// Leaving the editor: the skater (hidden while editing) put on the ground below the camera, facing the way the camera
// looked, so play goes on where the editor was left (as Obj_MoveToNode does: position, orientation, teleported).
void place_skater_below_camera()
{
    constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater) {
        return;
    }
    // (below the camera; else from high above it - the camera may have gone through the floor)
    V3 ground{};
    const bool found =
        ground_below(level_editor_state().pos_, &ground) || ground_below({.x = level_editor_state().pos_.x,
                                                                          .y = level_editor_state().pos_.y + 3000.0F,
                                                                          .z = level_editor_state().pos_.z},
                                                                         &ground);
    if (!found) {
        ground = level_editor_state().pos_;
    }
    const TickCycle frame = frame_from_yaw_pitch(level_editor_state().yaw_, 0.0F);
    V3 at = {.x = -frame.back.x, .y = 0.0F, .z = -frame.back.z};
    const float length = std::sqrt((at.x * at.x) + (at.z * at.z));
    if (length > kTinyForwardLength) {
        at = {.x = at.x / length, .y = 0.0F, .z = at.z / length};
    } else {
        at = {.x = 0.0F, .y = 0.0F, .z = 1.0F};
    }
    // (rows: right = up x at, up, at)
    const maths::Vec4 right = {.x = at.z, .y = 0.0F, .z = -at.x, .w = 0.0F};
    const maths::Vec4 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    const maths::Vec4 forward = {.x = at.x, .y = 0.0F, .z = at.z, .w = 0.0F};
    for (maths::Mat4* matrix : {&skater->basis_frame(), &skater->display_matrix()}) {
        core::at(matrix->row, 0) = right;
        core::at(matrix->row, 1) = up;
        core::at(matrix->row, 2) = forward;
    }
    object_edit::get_place(skater, ground);
    get_log("level editor: the skater put at (%.0f, %.0f, %.0f)%s", ground.x, ground.y, ground.z,
            found ? " (the ground below the camera)" : " (the camera's place: no ground found below)");
}

} // namespace

bool cfunc_stop(script::Struct* /*params*/, script::Context /*context*/)
{
    if (!level_editor_state().active_) {
        return true;
    }
    play_test::on_editor_stop(); // (a simulation stopped, the world put back - level_editor_playtest.h)
    end_drag();
    scenery::finalise(); // (the moved scenery's collision where it now is, before the skater is put down)
    // (unsaved changes autosaved - unless an older autosave is still offered)
    if (level_editor_state().unsaved_ && layout::get_snapshot()) {
        layout::write_autosave();
    }
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
    render::occlusion_off() = occlusion_switch().occlusion_in_play_;
    view_cam::halt();
    ui::halt();
    set_relative(false);
    place_skater_below_camera();
    if (level_editor_state().camera_) {
        level_editor_state().camera_->set_at_point(level_editor_state().saved_pos_);
        level_editor_state().camera_->set_basis_frame(level_editor_state().saved_matrix_);
    }
    if (level_editor_state().saved_cursor_ >= 0) {
        input::show_cursor(level_editor_state().saved_cursor_);
    }
    level_editor_state().active_ = false;
    level_editor_state().camera_ = nullptr;
    drag_state().hover_ = {};
    drag_state().objects_.clear();
    drag_state().hover_sector_ = 0;
    scenery::clear_selection();
    get_log("level editor: off (%d changes, %d scenery pieces moved since the level loaded%s)", level_editor_state().moves_,
            scenery::get_moved_count(), level_editor_state().unsaved_ ? "; not saved" : "");
    return true;
}

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "LevelEditor_Available", .function = &cfunc_available},
        {.name = "LevelEditor_Start", .function = &cfunc_start},
        {.name = "LevelEditor_Stop", .function = &cfunc_stop},
        {.name = "LevelEditor_PickLevel", .function = &layout::cfunc_pick_level},
        {.name = "LevelEditor_EditedLevels", .function = &layout::cfunc_edited_levels},
        {.name = "LevelEditor_PlayCamera", .function = &cameras::cfunc_play_camera}, // (map4)
    });
    script::install_script_functions(kFunctions);
}

void prepare_all()
{
    input::add_event_handler(handle_event);
    set_crash_hook(on_crash);
    script::keep_qb_names(is_switched_on()); // (node names for the HUD: Fire_Tender rather than 61EF3AD4)
    ux3::prepare_all(); // (the Outliner's records in the layouts, the actions' callbacks - level_editor_ux3.h)
    ux8::
        prepare_all(); // (pies, quick sets, walk / follow / teleport, controller mode: their actions - level_editor_ux8.h)
    ux9::prepare_all(); // (UX9: its actions and the ground pieces' layout records - level_editor_ux9.h)
}

void input_logic()
{
    if (!level_editor_state().active_) {
        return;
    }
    // Play from here (F5, Ctrl+P - level_editor_play.h): the editor's camera and selection kept for the way back, then
    // left as Escape leaves it (Play closes the pause menu that brings back).
    if (perform::take_request()) {
        perform::get_keep(resume_now(), layout::level_loads());
        level_editor_state().exit_requested_ = true;
        get_log("level editor: play from here");
    }
    if (!level_editor_state().exit_requested_) {
        return;
    }
    level_editor_state().exit_requested_ = false;
    if (debug_enabled()) {
        get_log("level editor: Escape - running level_editor_exit");
    }
    script::launch_script("level_editor_exit", nullptr, nullptr, false);
}

void place_camera(gfx::ViewCam* camera)
{
    if (!level_editor_state().active_ || !camera) {
        return;
    }
    if (!level_editor_state().camera_) {
        level_editor_state().camera_ = camera;
        level_editor_state().saved_pos_ = camera->at_point();
        level_editor_state().saved_matrix_ = camera->basis_frame();
        level_editor_state().pos_ = {.x = camera->at_point().x, .y = camera->at_point().y, .z = camera->at_point().z};
        const maths::Vec4& back = core::at(camera->basis_frame().row, 2);
        yaw_pitch_from_back({.x = back.x, .y = back.y, .z = back.z}, {.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_});
        if (debug_enabled()) {
            get_log("level editor: camera %p at (%.1f, %.1f, %.1f), yaw %.1f, pitch %.1f degrees",
                    static_cast<void*>(camera), level_editor_state().pos_.x, level_editor_state().pos_.y, level_editor_state().pos_.z, level_editor_state().yaw_ * kDegreesPerHalfTurn / maths::kPi,
                    level_editor_state().pitch_ * kDegreesPerHalfTurn / maths::kPi);
        }
        // (back from play from here: the camera and the selection as they were - level_editor_play.h)
        perform::Resume resume;
        if (perform::take_resume(&resume, layout::level_loads())) {
            apply_resume(resume);
        }
    }
    if (camera != level_editor_state().camera_) {
        return;
    }
    const TickCycle frame = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
    camera->set_at_point({.x = level_editor_state().pos_.x,
                          .y = level_editor_state().pos_.y,
                          .z = level_editor_state().pos_.z,
                          .w = 1.0F});
    core::at(camera->basis_frame().row, 0) = {.x = frame.right.x, .y = frame.right.y, .z = frame.right.z, .w = 0.0F};
    core::at(camera->basis_frame().row, 1) = {.x = frame.up.x, .y = frame.up.y, .z = frame.up.z, .w = 0.0F};
    core::at(camera->basis_frame().row, 2) = {.x = frame.back.x, .y = frame.back.y, .z = frame.back.z, .w = 0.0F};
}

float adjust_view_angle(gfx::ViewCam* camera, float angle)
{
    return level_editor_state().active_ && camera && camera == level_editor_state().camera_ ? view_cam::adjust_view_angle(angle) : angle;
}

float get_near_plane(gfx::ViewCam* camera, float near_z)
{
    return level_editor_state().active_ && camera && camera == level_editor_state().camera_ ? view_cam::get_near_plane() : near_z;
}

void note_view(gfx::ViewCam* camera, float angle, float aspect, std::span<const uint32_t, 4> viewport)
{
    if (!level_editor_state().active_ || camera != level_editor_state().camera_) {
        return;
    }
    const Lens lens = lens_for({.angle_degrees_ = angle, .aspect = aspect});
    if (debug_enabled() && !level_editor_state().have_view_) {
        get_log("level editor: view %u,%u %ux%u, angle %.2f, aspect %.4f", core::at(viewport, 0), core::at(viewport, 1), core::at(viewport, 2),
                core::at(viewport, 3), angle, aspect);
    }
    level_editor_state().lens_ = lens;
    std::ranges::copy(viewport, level_editor_state().viewport_.begin());
    level_editor_state().have_view_ = true;
}

namespace {

// A key's press (true once per press).
bool squeezed(input::KeyState keys, SDL_Scancode key, bool* was)
{
    const bool down = core::at(keys, key) != 0;
    const bool pressed = down && !*was;
    *was = down;
    return pressed;
}

// A key's press and, held, its repeats (after 0.4 s, 12 a second): the browser's list.
bool get_repeated(input::KeyState keys, SDL_Scancode key, uint32_t* since)
{
    if (core::at(keys, key) == 0U) {
        *since = 0;
        return false;
    }
    const uint32_t now = SDL_GetTicks();
    if (*since == 0U) {
        *since = now;
        return true;
    }
    if (now - *since < kKeyRepeatDelay) {
        return false;
    }
    *since = now - kKeyRepeatDelay + kKeyRepeatStep;
    return true;
}

// --- UX8 (level_editor_ux8.h): wheel drill-through, click-to-teleport, walk mode ---

// The controller's right stick at full tilt: as a mouse moved this many pixels a second.
constexpr float kPadLookPixelsPerSecond = 700.0F;
bool& stored_drilled()
{
    static bool drilled{}; // (the click-to-cycle's next entry was chosen by Alt+wheel: it stays the hover while Alt is held)
    return drilled;
}

// Alt+wheel: the hover stepped `steps` deeper (negative: nearer) through everything under the cursor - the
// click-to-cycle's list (made now as a click would make it, nothing taken), its next entry set so the hover shows the
// one stepped to; a click then takes it.
void drill_through(int steps, maths::Vec2 point, V3 dir)
{
    const auto x = point.x;
    const auto y = point.y;
    int shown = -1;
    if (cycle_holds(x, y, false, false)) {
        shown = (pick_cycle().cycle_.index + 1) % static_cast<int>(pick_cycle().cycle_.order.size());
    } else {
        std::vector<picking::Layered> objects;
        std::vector<picking::Layered> scenery;
        for (const auto& hit : object_edit::pick_all(level_editor_state().pos_, dir, kMaxPickDistance)) {
            objects.push_back({.id = hit.first->ident(), .t = hit.second, .passed = false});
        }
        scenery::Hit first;
        std::vector<scenery::Layer> layers;
        scenery::get_pick(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, &first, &layers);
        scenery.reserve(layers.size());
        for (const scenery::Layer& layer : layers) {
            scenery.push_back({.id = layer.checksum, .t = layer.t, .passed = layer.passed});
        }
        pick_cycle().cycle_ = Cycle{};
        pick_cycle().cycle_.have = true;
        pick_cycle().cycle_.x = x;
        pick_cycle().cycle_.y = y;
        pick_cycle().cycle_.pos = level_editor_state().pos_;
        pick_cycle().cycle_.changes = level_editor_state().changes_;
        pick_cycle().cycle_.filter = drag_state().pick_filter_;
        pick_cycle().cycle_.order = picking::cycle_order(drag_state().hover_node_, objects, scenery, drag_state().pick_filter_);
        const objects::GameEntity* object = get_live(drag_state().hover_);
        shown = hover_cycle_index(object);
    }
    const int count = static_cast<int>(pick_cycle().cycle_.order.size());
    if (count < 2) {
        pick_cycle().cycle_.have = false;
        stored_drilled() = false;
        hud::get_message((count != 0) ? "Only one item under cursor" : "Nothing under cursor", kBriefSeconds);
        return;
    }
    const int at = nav::drill_index(shown, steps, count);
    pick_cycle().cycle_.index = nav::cycle_index_showing(at, count);
    stored_drilled() = true;
    std::array<char, kCaptionTextSize> text{};
    text::format_to(text, "%d of %d under cursor", at + 1, count);
    hud::get_message(text.data(), kPairSeconds);
}

// Click-to-teleport: the camera glides to the surface under the cursor (nav::teleport_target), looking the same way.
void teleport_to(V3 dir)
{
    V3 hit{};
    const V3 ray = make_unit(dir);
    if (!get_cast(level_editor_state().pos_, level_editor_state().pos_ + ray * kMaxPickDistance, &hit)) {
        hud::get_message("No surface under cursor");
        return;
    }
    if (have_selection() || drag_state().node_selected_) {
        unpick(); // (the camera would orbit it)
    }
    view_cam::get_cancel();
    ux8::start_glide(level_editor_state().pos_, nav::teleport_target(level_editor_state().pos_, hit));
    note_action("teleported to (%.0f, %.0f, %.0f)", hit.x, hit.y, hit.z);
}

// Walk mode's frame: the mouse (captured) and the right stick look, the fly keys / left stick walk, the fly-up key /
// A jump, Shift / L3 run; Escape (or B) stops.
void walk_frame(const Mouse& input, input::KeyState keys, const ux8::Pad& pad, double seconds)
{
    if (drag_state().cancel_requested_ || (pad.active && pad.b)) {
        drag_state().cancel_requested_ = false;
        ux8::get_cancel();
        set_relative(false);
        return;
    }
    set_relative(true);
    const prefs::Values& prefs = prefs::get_pull();
    const float dx = input.rel_x_ + (pad.look_x_ * kPadLookPixelsPerSecond * static_cast<float>(seconds));
    const float dy = input.rel_y_ - (pad.look_y_ * kPadLookPixelsPerSecond * static_cast<float>(seconds));
    get_look({.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_}, dx, prefs.invert_y_ ? -dy : dy,
             kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0F);
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
    in.run = (core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U) || pad.fast;
    g_jump_was = jump_key;
    nav::Walker& walker = ux8::walker_state();
    nav::get_step(&walker, level_editor_state().yaw_, in, static_cast<float>(seconds), get_cast);
    level_editor_state().pos_ = nav::eye_of(walker);
    view_cam::after_update(rig_now(), 0, true);
}

// The transform tools' keys (level_editor_transform.h): the modes (wasd_flies=0: W / E / R while the right button isn't
// held; both: Space cycles - with wasd_flies=1 only with a selection, Space flying up without one), ` World / Local,
// Alt+G the modal transform; F (UX1's frame) lets the camera follow the selection again.
void transform_keys(input::KeyState keys, bool ctrl, bool shift, bool alt, const Mouse& input, V3 dir, bool have_ray,
                    const TickCycle& frame)
{
    // (the keys from the keymap - level_editor_actions.h: W / E / R, Space, `, Alt+G, F)
    (void)keys;
    const bool w = actions::squeezed(actions::kToolMove);
    const bool rotate_key = actions::squeezed(actions::kToolRotate);
    const bool scale_key = actions::squeezed(actions::kToolScale);
    const bool space = actions::squeezed(actions::kToolCycle);
    const bool grave = actions::squeezed(actions::kToolSpace);
    const bool modal_key = actions::squeezed(actions::kModalTransform);
    const bool frame_key = actions::squeezed(actions::kFrameSelection);
    if (drag_state().dragging_ || stored_browser().open || ctrl || drag_state().field_editing_) {
        return;
    }
    xform::Settings& settings = xform::at_hand();
    const xform::Mode before = settings.mode;
    const bool always = xform::fly_always();
    (void)shift, (void)alt;
    if (!always && !input.right) {
        if (w) {
            settings.mode = xform::Mode::kShiftBy;
        }
        if (rotate_key) {
            settings.mode = xform::Mode::kSpinBy;
        }
        if (scale_key) {
            settings.mode = xform::Mode::kScale;
        }
    }
    if (space && !input.right && (!always || have_selection())) {
        settings.mode = xform::next_mode(settings.mode);
    }
    if (settings.mode != before) {
        const char* why = "";
        if (settings.mode == xform::Mode::kSpinBy && have_selection() && gizmo_mode() != settings.mode) {
            why = " (selection can't rotate)";
        } else if (settings.mode == xform::Mode::kScale && have_selection() && gizmo_mode() != settings.mode) {
            why = " (selection can't scale)";
        }
        hud::get_message(std::string(xform::mode_name(settings.mode)) + " Tool" + why);
    }
    if (grave) {
        settings.space = settings.space == xform::Space::kWorld ? xform::Space::kLocal : xform::Space::kWorld;
        hud::get_message(settings.space == xform::Space::kWorld ? "World Space" : "Local Space");
    }
    // (F - UX1's frame, level_editor_camera.h - flies the camera to the selection: it follows it again after)
    if (frame_key && have_selection()) {
        drag_state().free_camera_ = false;
    }
    // Alt+G: the tool's transform without holding the mouse (Blender's G / R / S).
    if (modal_key && have_selection() && have_ray) {
        drag_state().axis_ = -1;
        drag_state().drag_mode_ = gizmo_mode();
        drag_state().modal_ = true;
        drag_state().typed_ = {};
        drag_state().dragging_ = true;
        drag_state().drag_start_ = selection_pos();
        drag_state().press_x_ = input.x;
        drag_state().press_y_ = input.y;
        drag_state().drag_moved_ = true;
        begin_change();
        start_drag(level_editor_state().pos_, dir, false, frame, input.x, input.y);
        hud::get_message(std::string(xform::mode_name(drag_state().drag_mode_)) + ": X/Y/Z constrain, type a value, Enter applies",
                         hud::kShortMessageSeconds);
    }
}

// One editor frame's input and view, as tick_frame's steps share them.
struct EditorFrame {
    double seconds = 0.0;
    Mouse input;
    input::KeyState keys;
    ux8::Pad pad;
    bool pad_cursor_ = false; // the crosshair is the cursor (a game controller in controller mode)
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    double boost = 1.0;      // Shift: kFastFlyFactor
    bool no_change_ = false; // a drag, a key move, a field edit or a resize under way: no undo / save / delete keys
    TickCycle frame{};       // the camera's frame after this frame's looking
    V3 dir{};                // the mouse ray (the view's direction without one)
    bool have_ray_ = false;
    float back_x_ = 0.0F; // the cursor in back-buffer pixels
    float back_y_ = 0.0F;
};

// The level's editor systems, every frame (in play too).
void tick_level_systems()
{
    budget::get_observe(); // (UX5: the budget's baseline - the level up, before its edited copy's layout goes in)
    layout::tick();
    triggers::get_guard();       // (no editor trigger volumes in a network session)
    triggers::perform(level_editor_state().active_); // (exit triggers: the skater leaving one, in play)
    // (emitters' sounds, path speeds, objects made again, the path preview - level_editor_entities.h)
    {
        const objects::GameEntity* primary = level_editor_state().active_ ? get_primary() : nullptr;
        entities::tick(level_editor_state().active_, primary ? primary->ident() : 0, drag_state().dragging_);
    }
    chains::tick(); // (peds / cars made again after their path changed; old rail node arrays freed)
}

// The HUD and the autosave.
void tick_hud_and_autosave()
{
    // (the UI runs: the HUD's elements - made by level_editor_enter after LevelEditor_Start - taken down, the first
    // few frames)
    if (ui::underway() && pick_cycle().hud_taken_down_ < 4) {
        ++pick_cycle().hud_taken_down_;
        script::launch_script("level_editor_hud_destroy", nullptr, nullptr, false);
    }
    update_hud();
    // The autosave: the snapshot made again after each change (the frame after: the change is complete), written once a
    // minute while there are unsaved changes; an older autosave offered once the HUD is up.
    if (level_editor_state().unsaved_ && level_editor_state().changes_ != level_editor_state().snapshot_changes_ &&
        layout::get_snapshot()) {
        level_editor_state().snapshot_changes_ = level_editor_state().changes_;
    }
    if (level_editor_state().unsaved_ && SDL_GetTicks() - level_editor_state().last_autosave_ >=
                         static_cast<uint32_t>(prefs::get_pull().autosave_seconds_) * kMillisecondsPerSecond) { // ([editor] autosave_s)
        level_editor_state().last_autosave_ = SDL_GetTicks();
        layout::write_autosave();
    }
    if (level_editor_state().offer_show_) {
        level_editor_state().offer_show_ = false;
        hud::get_message("Autosave found: Ctrl+R restores, Ctrl+Shift+R discards", kAutosaveNoticeSeconds);
    }
}

// The time since the last frame (at most kMaxFrameSeconds).
double take_frame_seconds()
{
    const uint64_t now = SDL_GetPerformanceCounter();
    double seconds = (level_editor_state().last_counter_ != 0U)
                         ? static_cast<double>(now - level_editor_state().last_counter_) /
                               static_cast<double>(SDL_GetPerformanceFrequency())
                         : 0.0;
    level_editor_state().last_counter_ = now;
    seconds = std::min(seconds, kMaxFrameSeconds);
    return seconds;
}

// The mouse since the last frame (its counts taken).
Mouse take_mouse()
{
    Mouse input = stored_mouse();
    stored_mouse().rel_x_ = stored_mouse().rel_y_ = 0.0F;
    stored_mouse().wheel = 0;
    stored_mouse().left_presses_ = stored_mouse().left_releases_ = 0;
    stored_mouse().middle_presses_ = 0;
    return input;
}

// The keyboard (none while the UI types), the controller, the modifiers.
void read_keys_and_pad(EditorFrame& editor_frame)
{
    input::KeyState& keys = editor_frame.keys;
    Mouse& input = editor_frame.input;
    keys = input::keys();
    // (the UI is typing: the editor's keys rest - level_editor_ui.h)
    static const std::array<uint8_t, SDL_NUM_SCANCODES> kNoKeys = {};
    if (!ui::keys_to_editor()) {
        keys = kNoKeys;
    }
    // (UX8: a game controller in controller mode - level_editor_ux8.h: A as the left button at the crosshair, the
    // D-pad as the nudge keys, L3 as Shift, B as Escape; the sticks below)
    editor_frame.pad = ux8::read_pad();
    const ux8::Pad& pad = editor_frame.pad;
    bool& pad_cursor = editor_frame.pad_cursor_;
    if (pad.active) {
        keys = ux8::pad_keys(keys, pad);
        input.left = input.left || pad.a_down_;
        input.left_presses_ += pad.a_presses_;
        input.left_releases_ += pad.a_releases_;
        float cx = 0.0F;
        float cy = 0.0F;
        pad_cursor = ux8::get_pad_cursor(pad, input.rel_x_ != 0.0F || input.rel_y_ != 0.0F, {.x = &cx, .y = &cy}) &&
                     !input.right;
        if (pad_cursor) {
            input.x = cx;
            input.y = cy;
        }
        if (ux8::pad_cancel(pad)) {
            drag_state().cancel_requested_ = true;
        }
    }
    editor_frame.shift = (core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U);
    editor_frame.ctrl = (core::at(keys, SDL_SCANCODE_LCTRL) != 0U) || (core::at(keys, SDL_SCANCODE_RCTRL) != 0U);
    editor_frame.alt = (core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U);
    editor_frame.boost = editor_frame.shift ? kFastFlyFactor : 1.0;
    if (!have_selection()) {
        drag_state().dragging_ = false;
    }
}

// The camera tools' mouse, the keymap's frame, play / preferences / simulate; walk mode. Whether walk mode took
// the frame.
bool global_keys_and_walk(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const ux8::Pad& pad = editor_frame.pad;
    const double seconds = editor_frame.seconds;
    // The camera tools (level_editor_camera.h): Alt+drag orbits, the wheel with the right button the flying speed.
    view_cam::filter_mouse(rig_now(),
                           {.left_presses_ = &input.left_presses_,
                            .left_releases_ = &input.left_releases_,
                            .left = input.left,
                            .right = input.right,
                            .rel_x_ = &input.rel_x_,
                            .rel_y_ = &input.rel_y_,
                            .wheel = &input.wheel},
                           (core::at(input::keys(), SDL_SCANCODE_LALT) != 0U) ||
                               (core::at(input::keys(), SDL_SCANCODE_RALT) != 0U),
                           level_editor_state().last_have_ray_, level_editor_state().last_ray_);
    // The keys from the keymap (level_editor_actions.h: rebindable, [editor_keys] in the ini; the command search runs
    // them the same way).
    actions::begin_frame(keys);
    // Play from here (F5, Ctrl+P - level_editor_play.h); the preferences (Ctrl+, - level_editor_prefs.h).
    if (actions::squeezed(actions::kPlay) && !drag_state().dragging_) {
        perform::get_request();
    }
    if (actions::squeezed(actions::kPreferences) && ui::underway()) {
        prefs::unseal() = !prefs::unseal();
    }
    // Simulate (F6) and the Trail window (F7) - level_editor_playtest.h.
    play_test::tick(drag_state().dragging_);
    // Walk mode (UX8 - level_editor_ux8.h: Shift+`): the camera at a skater's eye height; the editor's other keys and
    // clicks rest meanwhile.
    if (ux8::take_walk_toggle() && !drag_state().dragging_) {
        const bool on = !ux8::on_foot();
        if (on) {
            unpick();
        }
        ux8::set_walking(on, level_editor_state().pos_);
    }
    if (ux8::on_foot()) {
        walk_frame(input, keys, pad, seconds);
        return true;
    }
    return false;
}

// Escape: a drag put back, else the browser or the help closed, else the selection dropped.
void handle_escape()
{
    if (drag_state().cancel_requested_) {
        drag_state().cancel_requested_ = false;
        if (ux8::get_cancel() || (!drag_state().dragging_ && ux9::get_cancel())) {
            // (UX8: a pie menu closed; UX9: the gap wizard / the ground tool given up first - level_editor_ux9.h)
        } else if (triggers::resizing()) {
            triggers::end_resize(); // (kept as far as it went - Ctrl+Z puts the size back)
        } else if (drag_state().dragging_) {
            // (a turn, a scale or an object stood on a slope: the step taken when it started put back -
            // level_editor_transform.h)
            if (undo_steps().has_pending_ && (drag_state().drag_mode_ != xform::Mode::kShiftBy || drag_state().pending_forced_)) {
                carry::disarm();
                get_restore(undo_steps().pending_);
            } else {
                place_selection(drag_state().drag_start_);
            }
            get_settle();
            drag_state().dragging_ = false;
            drag_state().axis_ = -1;
            undo_steps().has_pending_ = false;
            drag_state().pending_forced_ = false;
            drag_state().drag_mode_ = xform::Mode::kShiftBy;
            drag_state().modal_ = false;
            drag_state().typed_ = {};
            drag_state().vertex_have_ = false;
            focus_on_selection();
            hud::get_message("Cancelled");
        } else if (pick_cycle().confirm_reset_) {
            pick_cycle().confirm_reset_ = false;
        } else if (pick_cycle().confirm_hide_ != 0U) {
            pick_cycle().confirm_hide_ = 0;
        } else if (chains::get_drawing() != chains::Blit::kNone || chains::wants_pick()) {
            stop_chain_drawing(chains::wants_pick() ? "Cancelled" : "Drawing finished");
        } else if (pick_cycle().place_armed_ >= 0) {
            pick_cycle().place_armed_ = -1;
        } else if (pick_cycle().place_open_) {
            pick_cycle().place_open_ = false;
        } else if (stored_browser().open) {
            stored_browser().open = false;
            hud::hide_panel();
        } else if (drag_state().help_) {
            drag_state().help_ = false;
            hud::hide_panel();
        } else if (have_selection() || drag_state().node_selected_) {
            unpick();
        } else if (faces::has_selection()) {
            faces::clear_selection(); // (map3)
        } else if (faces::get_picking()) {
            faces::set_picking(false);
            hud::get_message("Face Select off");
        }
    }
}

// The history and selection tools, UX8's cycle, the panels' keys.
void tick_panels(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const ux8::Pad& pad = editor_frame.pad;
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
        ux8.dragging = drag_state().dragging_;
        ux8.browser = stored_browser().open;
        ux8.keys = keys;
        ux8.pad = pad;
        ux8.have_selection_ = have_selection() || drag_state().node_selected_;
        ux8::tick_frame(ux8);
    }

    // The panels: F1 the help, B the browser.
    if (actions::squeezed(actions::kHelp)) {
        toggle_help();
    }
    if (actions::squeezed(actions::kBrowser) && !drag_state().dragging_) {
        toggle_browser();
    }
    if (actions::squeezed(actions::kPlace) && !drag_state().dragging_) {
        if (ui::underway()) {
            pick_cycle().place_open_ = !pick_cycle().place_open_;
            pick_cycle().place_armed_ = -1;
            if (pick_cycle().place_open_) {
                entities::refresh_palette(); // (objects made since show up)
            }
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
}

// Ctrl+ keys (undo / redo, save, duplicate), L, the view's C / H / I.
void edit_keys(const EditorFrame& editor_frame)
{
    const bool ctrl = editor_frame.ctrl;
    const bool no_change = editor_frame.no_change_;
    // Ctrl+ keys: undo / redo, save, duplicate.
    if (!no_change) {
        if (actions::squeezed(actions::kUndo)) {
            step_history(&undo_steps().undo_, &undo_steps().redo_, "Undone");
        }
        if (actions::squeezed(actions::kRedo)) {
            step_history(&undo_steps().redo_, &undo_steps().undo_, "Redone");
        }
        if (actions::squeezed(actions::kSave)) {
            save_layout();
        }
        // Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R: it's discarded.
        if (actions::squeezed(actions::kRestoreAutosave) || actions::squeezed(actions::kDiscardAutosave)) {
            answer_autosave(actions::squeezed(actions::kDiscardAutosave));
        }
        // (Ctrl+Shift+D: the last Ctrl+D repeated - its copies' move and turn since made again from them,
        // level_editor_transform.h)
        if (actions::squeezed(actions::kRepeatDuplicate)) {
            repeat_duplicate(frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_));
        } else if (actions::squeezed(actions::kDuplicate)) {
            get_duplicate(frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_));
        }
    }
    if (ctrl && !no_change && have_selection()) {
        focus_on_selection();
    }
    // L: with a rail point selected, it moves with its whole rail or by itself (level_editor_node_edit.h); otherwise
    // the selection locked (clicks pass it - level_editor_picking.h) or, all locked, unlocked.
    const int l_node = picked_node_index();
    // (a rail's, a ladder's or a path's point: its whole chain or itself - level_editor_chains.h)
    const bool rail_point = l_node >= 0 && node_edit::is_chain(core::at(nodes::get_all(), l_node).name);
    const bool l_pressed = actions::squeezed(actions::kLock);
    if (l_pressed && !rail_point && !drag_state().dragging_) {
        toggle_lock();
    }
    if (l_pressed && rail_point && !no_change) {
        pick_cycle().move_entire_rail_ = !pick_cycle().move_entire_rail_;
        hud::get_message(pick_cycle().move_entire_rail_ ? "Move whole chain" : "Move single point");
    }
    // C: the collision shown / hidden; H: the selection hidden (Alt+H: everything shown); I or /: isolated
    // (level_editor_view.h - while editing only).
    if (actions::squeezed(actions::kOcclusion) && !stored_browser().open) {
        occlusion_switch().occlusion_off_ = !occlusion_switch().occlusion_off_;
        render::occlusion_off() = occlusion_switch().occlusion_off_;
        hud::get_message(occlusion_switch().occlusion_off_ ? "Occlusion culling off" : "Occlusion culling on");
    }
    if (actions::squeezed(actions::kCollision) && !stored_browser().open) {
        show_collision(!view::show_collision());
    }
    if (actions::squeezed(actions::kShowAll) && !no_change && !stored_browser().open) {
        view_show_all();
    }
    if (actions::squeezed(actions::kHide) && !no_change && !stored_browser().open) {
        view_hide(false);
    }
    if (actions::squeezed(actions::kIsolate) && !no_change && !stored_browser().open) {
        view_hide(true);
    }
}

// The view menus' keys, the ruler, the trigger volumes shown.
void view_keys()
{
    view::tick();
    // The view menus' keys (level_editor_overlays.h): Alt+1..4 the view mode (Lit, Unlit, Wireframe, Collision only);
    // Y the jump arcs (level_editor_arcs.h); M the ruler (level_editor_ruler.h), Shift+M clears its measurement.
    // (the keys from the keymap - level_editor_actions.h)
    for (int mode = 0; mode < std::min(overlays::kViewModes, 4); ++mode) {
        if (actions::squeezed(actions::kViewModeLit + mode) &&
            !drag_state().dragging_) { // (while dragging digits are typed values)
            overlays::change_view_mode(mode);
            hud::get_message(std::string("View: ") + overlays::view_mode_name(mode));
        }
    }
    if (actions::squeezed(actions::kJumpArcs) && !stored_browser().open) {
        const bool on = !overlays::get_shown(overlays::kJumpArcs);
        overlays::assign_value(overlays::kJumpArcs, on);
        hud::get_message(on ? "Jump Arcs on" : "Jump Arcs off");
    }
    const bool ruler_clear = actions::squeezed(actions::kRulerClear);
    if ((actions::squeezed(actions::kRuler) || ruler_clear) && !stored_browser().open && !drag_state().dragging_) {
        if (ruler_clear) {
            ruler::get_clear();
            hud::get_message("Ruler cleared");
        } else {
            ruler::set_armed(!ruler::get_armed());
            if (ruler::get_armed()) {
                overlays::assign_value(overlays::kRuler, true);
            }
            hud::get_message(ruler::get_armed() ? "Ruler: click two points" : "Ruler off");
        }
    }
    // T: the trigger volumes shown (and the Triggers window) - level_editor_triggers.h; their changes, undo steps and
    // selections.
    if (actions::squeezed(actions::kTriggers) && !stored_browser().open) {
        if (ui::underway()) {
            triggers::set_shown(!triggers::get_shown());
            hud::get_message(triggers::get_shown() ? "Trigger volumes shown" : "Trigger volumes hidden");
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
}

// The trigger volumes' frame, the walls, Faces mode and the tools' frames.
void tick_triggers_and_tools(const EditorFrame& editor_frame)
{
    const bool no_change = editor_frame.no_change_;
    {
        const TickCycle view = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
        triggers::tick(level_editor_state().pos_, view.back * -1.0F, level_editor_state().yaw_);
        // (an editor trigger selected by itself: its resize handles)
        triggers::set_selected(picked_node_index() >= 0 && triggers::has_handles(drag_state().node_name_) ? drag_state().node_name_ : 0);
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
        if (select < 0) {
            select = goal_edit::take_select(); // (a goal just made / a point chosen in its section)
        }
        if (select >= 0 && !drag_state().dragging_) {
            pick_cycle().pickables_.clear();
            drag_state().hover_node_ = -1;
            select_node_only(select);
        }
        if (const uint32_t wall = triggers::take_wall_select(); (wall != 0U) && !drag_state().dragging_) {
            unpick();
            view::select_wall(wall); // (a level trigger volume moved as an invisible wall - level_editor_walls.h)
        }
    }
    walls::tick(); // (scenery whose collision is switched off kept off - level_editor_walls.h)
    // (map3) K: Faces mode - clicks take collision faces (level_editor_faces.h); V: a camera node made from the view
    // (level_editor_cameras.h).
    if (actions::squeezed(actions::kFaces) && !stored_browser().open && !no_change) {
        if (ui::underway()) {
            faces::set_picking(!faces::get_picking());
            hud::get_message(faces::get_picking() ? "Face Select on" : "Face Select off");
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
    if (actions::squeezed(actions::kCameraNode) && !stored_browser().open && !no_change) {
        cameras::create_from_view();
    }
    faces::tick_frame();
    clusters::tick_frame();
    cameras::tick_frame();
    goal_edit::tick_frame();
    checks::tick_frame(); // (UX5: the Problems window's Go to / Fix - level_editor_checks.h)
    notes::tick_frame();  // (UX5: note pins - level_editor_notes.h)
    ux9::tick_frame();    // (UX9: Alt+J / Alt+K / Alt+U and the forms - level_editor_ux9.h)
}

// The Problems window, the camera logged, deselect / home / delete, a chain point's keys.
void command_keys(const EditorFrame& editor_frame)
{
    const bool no_change = editor_frame.no_change_;
    // P: the camera written to the log (position, back, up).
    if (actions::squeezed(actions::kProblems)) {
        checks::toggle_window(); // (UX5) Alt+P: the Problems window (Hammer's Check for Problems key)
    }
    if (actions::squeezed(actions::kLogCamera)) {
        const TickCycle view = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
        const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
        get_log("level editor: camera in level %d (scene %s), yaw %.1f, pitch %.1f - as a kViews entry:",
                render::current_level(), object_edit::caption(scene).c_str(), level_editor_state().yaw_ * kDegreesPerHalfTurn / maths::kPi,
                level_editor_state().pitch_ * kDegreesPerHalfTurn / maths::kPi);
        get_log("    {{%.3ff, %.3ff, %.3ff}, {%.6ff, %.6ff, %.6ff}, {%.6ff, %.6ff, %.6ff}},", level_editor_state().pos_.x, level_editor_state().pos_.y, level_editor_state().pos_.z,
                view.back.x, view.back.y, view.back.z, view.up.x, view.up.y, view.up.z);
        hud::get_message("Camera position logged");
    }
    if (actions::squeezed(actions::kDeselect) && (have_selection() || drag_state().node_selected_) &&
        !drag_state().dragging_) {
        unpick();
    }
    // (Home and Delete: not on what the Outliner's lock holds - level_editor_organize.h)
    if (actions::squeezed(actions::kHome) && !no_change && !stored_browser().open &&
        !organize::selection_locked(true)) {
        get_reset();
    }
    if (!no_change && !stored_browser().open && (pick_cycle().confirm_hide_ == 0U) && !pick_cycle().confirm_reset_) {
        if (actions::squeezed(actions::kResetLevel)) {
            ask_reset_level(); // (Ctrl+Shift+Delete: Reset level)
        } else if (actions::squeezed(actions::kDelete) && !organize::selection_locked(true)) {
            erase_one();
        }
    }
    // A rail's / path's point selected (level_editor_chains.h): Insert puts a point halfway to the next, End draws on
    // from this end, J links this end to the next end clicked, U unlinks it from its next.
    const bool insert_now = actions::squeezed(actions::kChainInsert);
    const bool end_now = actions::squeezed(actions::kChainExtend);
    const bool join_now = actions::squeezed(actions::kChainLink);
    const bool unlink_now = actions::squeezed(actions::kChainUnlink);
    if (drag_state().node_selected_ && !no_change && !stored_browser().open && chains::is_chain_node(drag_state().node_name_)) {
        if (insert_now) {
            run_chain_op(chains::ChainOp::kInsert, 0);
        }
        if (unlink_now) {
            run_chain_op(chains::ChainOp::kUnlink, 0);
        }
        if (join_now) {
            run_chain_op(chains::ChainOp::kLinkArm, 0);
        }
        if (end_now) {
            run_chain_op(chains::ChainOp::kExtend, 0);
            if (chains::get_drawing() != chains::Blit::kNone) {
                pick_cycle().place_armed_ = -1;
                unpick();
            }
        }
    }
}

// Drawing a chain (level_editor_chains.h): Enter or a right click (pressed and let go without looking about)
// finishes it.
void finish_chain_drawing(const Mouse& input)
{
    static bool g_right_was;
    static float g_right_travel;
    if (input.right) {
        if (!g_right_was) {
            g_right_travel = 0.0F;
        }
        g_right_travel += std::fabs(input.rel_x_) + std::fabs(input.rel_y_);
    }
    const bool right_click = g_right_was && !input.right && g_right_travel < 6.0F;
    g_right_was = input.right;
    const bool enter_now = actions::squeezed(actions::kChainFinish);
    if (chains::get_drawing() != chains::Blit::kNone && (enter_now || right_click) && !stored_browser().open) {
        stop_chain_drawing("Drawing finished");
    }
}

// Looking (right button held): about the camera, or orbiting the selection; the controller's sticks; a glide.
void look_and_fly(EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const ux8::Pad& pad = editor_frame.pad;
    const bool pad_cursor = editor_frame.pad_cursor_;
    const double seconds = editor_frame.seconds;
    // Looking (right button held): about the camera, or orbiting the selection.
    set_relative(input.right || view_cam::get_orbiting());
    if (input.right) {
        const prefs::Values& prefs = prefs::get_pull(); // ([editor] look_pct, invert_y)
        get_look({.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_}, input.rel_x_,
                 prefs.invert_y_ ? -input.rel_y_ : input.rel_y_,
                 kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0F);
    }
    const bool pad_sticks = pad.active && !ux8::pie_open();
    if (pad_sticks && (pad.look_x_ != 0.0F || pad.look_y_ != 0.0F)) {
        // (UX8: the controller's right stick looks - orbits a selection, as the right button does)
        const prefs::Values& prefs = prefs::get_pull();
        const float px = pad.look_x_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
        const float py = -pad.look_y_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
        get_look({.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_}, px,
                 prefs.invert_y_ ? -py : py, kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0F);
    }
    view_cam::apply_orbit(rig_now());
    editor_frame.frame = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
    const TickCycle& frame = editor_frame.frame;
    if (pad_sticks && (pad.move_x_ != 0.0F || pad.move_y_ != 0.0F || pad.up_down_ != 0.0F)) {
        // (UX8: the left stick flies, the triggers down / up - with a selection the camera leaves it, as the right
        // button's flying does; F frames it again)
        const V3 move =
            frame.back * -pad.move_y_ + frame.right * pad.move_x_ + V3{.x = 0.0F, .y = pad.up_down_, .z = 0.0F};
        level_editor_state().pos_ = level_editor_state().pos_ + move * static_cast<float>(level_editor_state().speed_ * (pad.fast ? kFastFlyFactor : 1.0) * seconds);
        if (have_selection()) {
            drag_state().free_camera_ = true;
        }
        if (drag_state().dragging_ && pad_cursor) {
            drag_state().drag_moved_ = true; // (the crosshair stays put: the camera's move is the drag's)
        }
        ux8::stop_glide();
    }
    // (UX8: a teleport's glide - a double click on a surface; flying or looking ends it)
    if (input.right) {
        ux8::stop_glide();
    }
    ux8::glide(&level_editor_state().pos_, seconds);
}

// The mouse ray (once the view has been drawn): the cursor in back-buffer pixels too (the arrows).
void cast_mouse_ray(EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const bool pad_cursor = editor_frame.pad_cursor_;
    const TickCycle& frame = editor_frame.frame;
    bool& have_ray = editor_frame.have_ray_;
    V3& dir = editor_frame.dir;
    float& back_x = editor_frame.back_x_;
    float& back_y = editor_frame.back_y_;
    have_ray = false;
    dir = frame.back * -1.0F;
    back_x = 0.0F;
    back_y = 0.0F;
    if (level_editor_state().have_view_ && !input.right && input.window_width_ > 0 && input.window_height_ > 0 &&
        (ui::mouse_to_editor() || pad_cursor)) {
        const mouse_menus::ScreenMapping mapping =
            mouse_menus::current_mapping({.width = input.window_width_, .height = input.window_height_});
        back_x = static_cast<float>(static_cast<double>(input.x) * mapping.back_buffer_width_ / input.window_width_);
        back_y = static_cast<float>(static_cast<double>(input.y) * mapping.back_buffer_height_ / input.window_height_);
        float nx = 0.0F;
        float ny = 0.0F;
        if (pixel_to_ndc(level_editor_state().viewport_, back_x, back_y, &nx, &ny)) {
            dir = ray_direction(frame, level_editor_state().lens_, nx, ny);
            have_ray = true;
        }
    }
}

// The cursor and the ray handed to the UI's requests, the transform keys, the arrange tools, the camera's
// bookmarks, Alt+wheel and the teleport.
void apply_ray_tools(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    drag_state().cursor_x_ = back_x;
    drag_state().cursor_y_ = back_y;
    apply_ui_requests(dir, have_ray, frame);
    transform_keys(keys, ctrl, shift, alt, input, dir, have_ray, frame);
    // (UX7: the arrange tools' keys, the adjust panel's changes, the linked copies - level_editor_arrange.h,
    // level_editor_adjust.h, level_editor_links.h)
    arrange::tick_frame();
    adjust::tick();
    links::tick();
    if (!drag_state().dragging_) { // (digits typed while dragging aren't bookmarks - level_editor_transform.h)
        view_cam::get_keys(rig_now(), keys, ctrl, stored_browser().open); // (F, 1..9, the numpad - level_editor_camera.h)
    }
    level_editor_state().last_have_ray_ = have_ray;
    level_editor_state().last_ray_ = dir;
    // (UX8: Alt+wheel steps what a click would take through everything under the cursor; the command search's
    // Teleport to the cursor)
    if (alt && (input.wheel != 0) && !drag_state().dragging_ && !input.right && have_ray && !view_cam::get_orbiting() &&
        !stored_browser().open) {
        drill_through(-input.wheel, {.x = input.x, .y = input.y}, dir);
        input.wheel = 0;
    }
    if (ux8::take_teleport() && have_ray && !drag_state().dragging_) {
        teleport_to(dir);
    }
}

// The browser takes the arrows, Enter and F (the selection's keys rest meanwhile).
void browser_keys(const EditorFrame& editor_frame)
{
    const input::KeyState keys = editor_frame.keys;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    static bool g_enter_key;
    static bool g_focus_key;
    static bool g_left_key;
    static bool g_right_key;
    static uint32_t g_up_since;
    static uint32_t g_down_since;
    static uint32_t g_page_up_since;
    static uint32_t g_page_down_since;
    if (stored_browser().open) {
        // The browser takes the arrows, Enter and F (the selection's keys rest meanwhile).
        const int count = static_cast<int>(stored_browser().items.size());
        const int move = (get_repeated(keys, SDL_SCANCODE_DOWN, &g_down_since) ? 1 : 0) -
                         (get_repeated(keys, SDL_SCANCODE_UP, &g_up_since) ? 1 : 0) +
                         (get_repeated(keys, SDL_SCANCODE_PAGEDOWN, &g_page_down_since) ? kBrowserRows : 0) -
                         (get_repeated(keys, SDL_SCANCODE_PAGEUP, &g_page_up_since) ? kBrowserRows : 0);
        if ((move != 0) && (count != 0)) {
            stored_browser().index = std::max(0, std::min(count - 1, stored_browser().index + move));
        }
        const bool left = squeezed(keys, SDL_SCANCODE_LEFT, &g_left_key);
        const bool right = squeezed(keys, SDL_SCANCODE_RIGHT, &g_right_key);
        if (left || right) {
            stored_browser().tab = 1 - stored_browser().tab;
            stored_browser().index = stored_browser().top = 0;
            fill_browser();
        }
        if (squeezed(keys, SDL_SCANCODE_F, &g_focus_key) && stored_browser().index < count) {
            focus_browser_item();
        }
        if (squeezed(keys, SDL_SCANCODE_RETURN, &g_enter_key) || squeezed(keys, SDL_SCANCODE_KP_ENTER, &g_enter_key)) {
            place_from_browser(cursor_point(dir, have_ray, frame));
        }
        if (stored_browser().open) {
            show_browser();
        }
    }
}

// The nudge keys' steps: forward / back, right / left, up / down (each -1, 0 or 1).
struct NudgeSteps {
    int forward = 0;
    int right = 0;
    int up = 0;
};

// The nudge with [editor] wasd_flies=1.
NudgeSteps read_fly_nudge(input::KeyState keys)
{
    NudgeSteps steps;
    // ([editor] wasd_flies=1: the fly keys nudge it as well as the arrows - Space aside, it cycles the tool)
    steps.forward =
        (actions::get_held(keys, actions::kForward) || actions::get_held(keys, actions::kNudgeForward) ? 1 : 0) -
        (actions::get_held(keys, actions::kBack) || actions::get_held(keys, actions::kNudgeBack) ? 1 : 0);
    steps.right = (actions::get_held(keys, actions::kRight) || actions::get_held(keys, actions::kNudgeRight) ? 1 : 0) -
                  (actions::get_held(keys, actions::kLeft) || actions::get_held(keys, actions::kNudgeLeft) ? 1 : 0);
    steps.up = (actions::get_held(keys, actions::kNudgeUp) ||
                        (actions::get_held(keys, actions::kFlyUp) && (core::at(keys, SDL_SCANCODE_SPACE) == 0U))
                    ? 1
                    : 0) -
               (actions::get_held(keys, actions::kNudgeDown) || actions::get_held(keys, actions::kFlyDown) ? 1 : 0);
    return steps;
}

// The nudge with [editor] wasd_flies=0 (and the camera flown with the right button held).
NudgeSteps read_arrow_nudge(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const bool ctrl = editor_frame.ctrl;
    const double boost = editor_frame.boost;
    const double seconds = editor_frame.seconds;
    const TickCycle& frame = editor_frame.frame;
    NudgeSteps steps;
    // ([editor] wasd_flies=0 - level_editor_transform.h: the arrow keys nudge it, Page Up / Down up / down;
    // W A S D / Q E fly the camera while the right button is held - away from the selection, F frames it again)
    steps.forward = (actions::get_held(keys, actions::kNudgeForward) ? 1 : 0) -
                    (actions::get_held(keys, actions::kNudgeBack) ? 1 : 0);
    steps.right = (actions::get_held(keys, actions::kNudgeRight) ? 1 : 0) -
                  (actions::get_held(keys, actions::kNudgeLeft) ? 1 : 0);
    steps.up =
        (actions::get_held(keys, actions::kNudgeUp) ? 1 : 0) - (actions::get_held(keys, actions::kNudgeDown) ? 1 : 0);
    if (input.right && !ctrl && !drag_state().dragging_) {
        const FlyInput fly = {.forward = (actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                         (actions::get_held(keys, actions::kBack) ? 1 : 0),
                              .right = (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                       (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                              .up = (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
        if ((fly.forward != 0) || (fly.right != 0) || (fly.up != 0)) {
            drag_state().free_camera_ = true;
            level_editor_state().pos_ = get_fly(level_editor_state().pos_, frame, fly,
                                                static_cast<float>(level_editor_state().speed_ * boost * seconds));
        }
    }
    return steps;
}

// The selection moves with the keys - W/S along the camera's heading, A/D across it, Q/E (R, Page Up/Down)
// down/up - and the camera follows it.
void nudge_selection(const EditorFrame& editor_frame, bool change_locked)
{
    const input::KeyState keys = editor_frame.keys;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const double boost = editor_frame.boost;
    const double seconds = editor_frame.seconds;
    const TickCycle& frame = editor_frame.frame;
    const V3 forward = make_unit({.x = -frame.back.x, .y = 0.0F, .z = -frame.back.z});
    const V3 right = make_unit({.x = frame.right.x, .y = 0.0F, .z = frame.right.z});
    const NudgeSteps steps = xform::fly_always() ? read_fly_nudge(keys) : read_arrow_nudge(editor_frame);
    int forward_step = steps.forward;
    int right_step = steps.right;
    int u = steps.up;
    if (ctrl || alt || change_locked) {
        forward_step = right_step = u = 0; // (Ctrl+S, Ctrl+D; Alt+G / Alt+H...; the Outliner's lock)
    }
    if (((forward_step != 0) || (right_step != 0) || (u != 0)) && change_locked) {
        organize::selection_locked(true);
    }
    if (((forward_step != 0) || (right_step != 0) || (u != 0)) && !drag_state().dragging_ &&
        !drag_state().field_editing_) {
        if (!level_editor_state().key_moving_) {
            begin_change();
        }
        auto step = static_cast<float>(kObjectSpeed * boost * seconds);
        const V3 pos = selection_pos();
        place_selection(
            {.x =
                 pos.x +
                 (((forward.x * static_cast<float>(forward_step)) + (right.x * static_cast<float>(right_step))) * step),
             .y = pos.y + (static_cast<float>(u) * step),
             .z = pos.z +
                  (((forward.z * static_cast<float>(forward_step)) + (right.z * static_cast<float>(right_step))) *
                   step)});
        level_editor_state().key_moving_ = true;
    } else {
        if (level_editor_state().key_moving_) {
            const uint64_t serial = undo_steps().undo_.empty() ? 0 : undo_steps().undo_.back().serial;
            end_change();
            get_settle();
            if (!undo_steps().undo_.empty() && undo_steps().undo_.back().serial != serial) { // (level_editor_adjust.h)
                note_for_adjust(xform::Mode::kShiftBy, selection_pos() - undo_steps().pending_.where, true);
            }
        }
        level_editor_state().key_moving_ = false;
    }
}

// Z / X: the selection turned.
void turn_selection_keys()
{
    // Z / X: turn it - an object 15 degrees, scenery a quarter turn about its centre (the engine turns level
    // geometry by quarter turns only).
    int turn = (actions::squeezed(actions::kTurnRight) ? 1 : 0) - (actions::squeezed(actions::kTurnLeft) ? 1 : 0);
    if ((turn != 0) && organize::selection_locked(true)) {
        turn = 0;
    }
    if ((turn != 0) && !drag_state().dragging_ && picked_node_index() >= 0 &&
        chains::is_ladder(drag_state().node_name_)) {
        run_chain_op(chains::ChainOp::kTurn, static_cast<uint32_t>(turn)); // (a ladder: its facing, whole)
    } else if ((turn != 0) && !drag_state().dragging_ &&
               !(picked_node_index() >= 0 &&
                 node_edit::is_chain(drag_state().node_name_))) { // (chain points don't turn)
        push_undo(get_snapshot(), "Turn");
        turn_selection(turn);
        note_action("turned (Z / X): %s", describe_selection().c_str());
        adjust::ActStep action; // (level_editor_adjust.h)
        action.kind = adjust::Kind::kTurn;
        action.quarters = (get_primary() == nullptr) && picked_node_index() < 0;
        action.degrees = static_cast<float>(turn) * (action.quarters ? kQuarterTurnDegrees : xform::angle_step());
        adjust::get_note(action);
    }
}

// [ / ]: the selection scaled or stretched.
void scale_selection_keys(const EditorFrame& editor_frame)
{
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    // [ / ]: scenery or objects scaled down / up a tenth (Shift: a hundredth); Alt+[ / ]: stretched so along the
    // world axis the camera faces most (looking down: y) - level_editor_scale.h.
    int scaling = (actions::squeezed(actions::kScaleUp) ? 1 : 0) - (actions::squeezed(actions::kScaleDown) ? 1 : 0);
    if ((scaling != 0) && organize::selection_locked(true)) {
        scaling = 0;
    }
    if ((scaling != 0) && !drag_state().dragging_ && !drag_state().field_editing_ && !ctrl) {
        const float step = shift ? scale_tool::kFineStep : scale_tool::kStep;
        const float factor = scaling > 0 ? step : 1.0F / step;
        if (alt) {
            const TickCycle view = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
            const int axis = scale_tool::main_axis(view.back);
            if (scale_selection_by(scale_tool::along_axis(axis, factor), false)) {
                adjust::ActStep action; // (level_editor_adjust.h)
                action.kind = adjust::Kind::kScale;
                action.factor = scale_tool::along_axis(axis, factor);
                action.uniform = false;
                adjust::get_note(action);
                char letter = 'z';
                if (axis == 0) {
                    letter = 'x';
                } else if (axis == 1) {
                    letter = 'y';
                }
                const std::string axis_name(1, letter);
                hud::get_message("Stretched along " + axis_name);
                note_action("stretched along %s (Alt+[ / ]): %s", axis_name.c_str(), describe_selection().c_str());
            }
        } else if (scale_selection_by(scale_tool::get_uniform(factor), true)) {
            note_action("scaled ([ / ]): %s", describe_selection().c_str());
            adjust::ActStep action; // (level_editor_adjust.h)
            action.kind = adjust::Kind::kScale;
            action.factor = scale_tool::get_uniform(factor);
            adjust::get_note(action);
        }
    }
}

// The wheel with a selection, and the camera at its distance from it.
void wheel_with_selection(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const double boost = editor_frame.boost;
    const TickCycle& frame = editor_frame.frame;
    // The wheel: nearer / further - or, while dragging, the selection pushed away / pulled back (a tenth of its
    // distance a notch).
    if ((input.wheel != 0) && drag_state().dragging_) {
        const V3 pos = selection_pos();
        drag_state().drag_push_ +=
            static_cast<float>(input.wheel) *
            std::max(kMinWheelStep,
                     std::sqrt(get_dot(pos - level_editor_state().pos_, pos - level_editor_state().pos_)) *
                         kWheelStepFraction);
    } else if ((input.wheel != 0) && drag_state().free_camera_) {
        level_editor_state().pos_ =
            level_editor_state().pos_ - frame.back * static_cast<float>(kWheelStep * boost * input.wheel);
    } else if (input.wheel != 0) {
        level_editor_state().orbit_distance_ = static_cast<float>(
            std::max(static_cast<double>(kMinOrbit),
                     std::min(static_cast<double>(kMaxOrbit),
                              level_editor_state().orbit_distance_ * std::pow(kWheelZoomStep, input.wheel))));
    }
    // The camera at that distance from it (held still while the mouse drags it or a trigger's face).
    if (!drag_state().dragging_ && !triggers::resizing() && !drag_state().free_camera_) {
        level_editor_state().pos_ = target_of() + frame.back * level_editor_state().orbit_distance_;
    }
}

// Flying with nothing selected.
void fly_without_selection(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const bool ctrl = editor_frame.ctrl;
    const double boost = editor_frame.boost;
    const double seconds = editor_frame.seconds;
    const TickCycle& frame = editor_frame.frame;
    // Flying: W/A/S/D along the view, Q/E down/up, the wheel forward / back, - / = the speed (not while the
    // browser has the keys: its F, arrows and Enter).
    level_editor_state().key_moving_ = false;
    if (!stored_browser().open && !ctrl && (xform::fly_always() || input.right)) {
        const FlyInput fly = {.forward = (actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                         (actions::get_held(keys, actions::kBack) ? 1 : 0),
                              .right = (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                       (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                              .up = (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
        // ([editor] fly_accel_ms: the speed reached over that time - level_editor_camera.h)
        const float ramp = view_cam::fly_factor((fly.forward != 0) || (fly.right != 0) || (fly.up != 0), seconds);
        level_editor_state().pos_ = get_fly(level_editor_state().pos_, frame, fly,
                                            static_cast<float>(level_editor_state().speed_ * boost * seconds * ramp));
    }
    if (input.wheel != 0) {
        auto step = static_cast<float>(kWheelStep * boost * input.wheel);
        level_editor_state().pos_ = level_editor_state().pos_ - frame.back * step;
    }
    const int speed = (actions::squeezed(actions::kFaster) ? 1 : 0) - (actions::squeezed(actions::kSlower) ? 1 : 0);
    if (speed != 0) {
        level_editor_state().speed_ = static_cast<float>(
            std::max(kMinFlySpeedDouble,
                     std::min(kMaxFlySpeedDouble, level_editor_state().speed_ * std::pow(kSpeedStepFactor, speed))));
    }
}

// UX8's follow camera.
void follow_selection()
{
    // (UX8: the follow camera, Shift+F - the camera moved as the selection moves, keeping its offset even after
    // flying off it; not while a mouse drag places it - the drag goes by the cursor's ray. Another selection starts
    // afresh.)
    static uint32_t g_follow_key;
    SelectionIds ids = current_selection();
    const uint32_t key = (ids.objects.empty() ? 0U : ids.objects.front()) ^
                         (ids.sectors.empty() ? 0U : ids.sectors.front() * 31U) ^ (ids.node * 131U) ^
                         static_cast<uint32_t>((ids.objects.size() * 7U) + (ids.sectors.size() * 13U));
    const bool active = ux8::get_following() && have_selection() && !drag_state().dragging_ && !triggers::resizing() &&
                        key == g_follow_key;
    g_follow_key = key;
    const V3 delta =
        nav::follow_step(&ux8::get_follower(), active, active ? target_of() : V3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    if (active && drag_state().free_camera_) {
        level_editor_state().pos_ = level_editor_state().pos_ + delta;
    }
}

// The selection moved, turned, scaled and dropped with the keys, or the camera flown; the follow camera.
void move_with_keys(const EditorFrame& editor_frame)
{
    const TickCycle& frame = editor_frame.frame;
    const bool selection_held = have_selection();
    // (the Outliner's lock - level_editor_organize.h: what it holds isn't moved, turned, scaled or dropped)
    const bool change_locked = selection_held && organize::selection_locked(false);
    if (selection_held && !stored_browser().open) {
        nudge_selection(editor_frame, change_locked);
        turn_selection_keys();
        scale_selection_keys(editor_frame);
        // G: down onto the ground below it.
        if (actions::squeezed(actions::kDrop) && !drag_state().dragging_ && !organize::selection_locked(true)) {
            drop_selection();
        }
        wheel_with_selection(editor_frame);
    } else if (!selection_held) {
        fly_without_selection(editor_frame);
    }
    follow_selection();
    if (stored_browser().open && selection_held && !drag_state().dragging_ && !drag_state().free_camera_) {
        level_editor_state().pos_ = target_of() + frame.back * level_editor_state().orbit_distance_;
    }
}

// ([features] debug_editor: once a second, what the mouse is seen as)
void log_mouse_debug(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    static int g_debug_frames = 0;
    if (debug_enabled() && ++g_debug_frames % kDebugLogFrames == 0) {
        get_log("level editor: mouse (%.0f, %.0f) in %dx%d, ray %d (%.3f, %.3f, %.3f), arrow %d", input.x, input.y,
                input.window_width_, input.window_height_, have_ray ? 1 : 0, dir.x, dir.y, dir.z, drag_state().hover_axis_);
    }
}

// Alt's carry, the tools' hovers (walls, faces, the ruler, UX9).
void update_tool_hovers(const EditorFrame& editor_frame)
{
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    // Alt while moving scenery: what it carries (level_editor_carry.h) put back and left there for this move.
    // (not with Ctrl or Shift: Ctrl+Alt / Alt+Shift snap to a vertex / a surface - level_editor_snap.h)
    if (alt && !ctrl && !shift && (drag_state().dragging_ || level_editor_state().key_moving_) && carry::total_of() > 0) {
        carry::get_leave();
        hud::get_message("Not carrying objects");
    }
    // Shift with the collision shown: the invisible wall a Shift+click would take, highlighted (level_editor_view.h).
    view::set_hover_wall(!drag_state().dragging_ && have_ray && shift && !alt && view::show_collision() && ui::underway()
                             ? view::pick_wall(level_editor_state().pos_, make_unit(dir))
                             : 0);
    faces::get_hover(level_editor_state().pos_, make_unit(dir), have_ray && !drag_state().dragging_ && drag_state().hover_axis_ < 0); // (map3: Faces mode's hover)
    if (ruler::get_armed() && have_ray && !drag_state().dragging_) {
        ruler::get_hover(level_editor_state().pos_, make_unit(dir), back_x, back_y, alt); // (the ruler's next end - level_editor_ruler.h)
    }
    ux9::get_hover(level_editor_state().pos_, make_unit(dir), back_x, back_y, have_ray && !drag_state().dragging_); // (UX9: the tools' next pick)
}

// Dragging (an arrow first, then an object or scenery); typed values or a modal click applied.
void update_drag(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    // Dragging, or what's under the cursor (an arrow first, then an object or scenery - the nearer).
    if (drag_state().dragging_ && have_selection() && have_ray) {
        if (!drag_state().drag_moved_ && (drag_state().modal_ || std::fabs(input.x - drag_state().press_x_) +
                                                                         std::fabs(input.y - drag_state().press_y_) >=
                                                                     kDragStartPixels)) {
            drag_state().drag_moved_ = true;
        }
        // (an axis typed - X / Y / Z, level_editor_transform.h: a move starts again from where it began, along it)
        if (drag_state().typed_.axis_changed_) {
            drag_state().typed_.axis_changed_ = false;
            if (drag_state().drag_mode_ == xform::Mode::kShiftBy) {
                place_selection(drag_state().drag_start_);
                drag_state().axis_ = drag_state().typed_.axis;
                start_drag(level_editor_state().pos_, dir, false, frame, input.x, input.y);
                drag_state().drag_moved_ = true;
            }
        }
        // (Shift: up / down - not with Ctrl or Alt, which pick what it snaps to)
        const bool vertical = shift && !ctrl && !alt;
        if (drag_state().drag_mode_ == xform::Mode::kShiftBy && drag_state().axis_ < 0 &&
            vertical != drag_state().drag_vertical_) {
            start_drag(level_editor_state().pos_, dir, vertical, frame, input.x, input.y);
        }
        if (drag_state().drag_moved_ || drag_state().typed_apply_ || !drag_state().typed_.text.empty()) {
            get_drag(level_editor_state().pos_, dir, frame, input.x, input.y, snapping_now(ctrl, shift, alt));
        }
    }
    // Enter (typed values) or - Alt+G's modal transform - a click: applied.
    if (drag_state().dragging_ && (drag_state().typed_apply_ || (drag_state().modal_ && (input.left_presses_ != 0)))) {
        end_drag();
        focus_on_selection();
        input.left_presses_ = 0;
        input.left_releases_ = 0;
    }
    drag_state().typed_apply_ = false;
}

// What's under the cursor (not while dragging).
void update_hover(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    if (!drag_state().dragging_) {
        drag_state().hover_axis_ = have_ray && have_selection() ? arrow_at(frame, {.x = back_x, .y = back_y}) : -1;
        // (a selected trigger volume's resize handle - after the arrows: level_editor_triggers.h)
        if (!triggers::resizing()) {
            drag_state().hover_handle_ = have_ray && drag_state().hover_axis_ < 0 && ui::underway()
                                 ? triggers::handle_at({.x = back_x, .y = back_y}, kHandlePickPixels * pixel_scale())
                                 : -1;
        }
        triggers::set_hover_handle(drag_state().hover_handle_);
        objects::GameEntity* hover = nullptr;
        uint32_t hover_sector = 0;
        // What a click would take (level_editor_picking.h): the cycle's next entry while a click-to-cycle holds; else
        // a node's gizmo under the cursor (what the last frame drew), then the nearer of an object and scenery -
        // structure and see-through pieces passed unless Alt (or a middle click) - as the pick filter allows.
        drag_state().hover_node_ = -1;
        drag_state().hover_passed_ = 0;
        // (UX8: a drill-through - Alt+wheel - holds with Alt still down: its entry is the hover)
        const bool drilled = stored_drilled() && cycle_holds(input.x, input.y, false, false);
        stored_drilled() = drilled;
        pick_cycle().hover_cycle_ =
            drilled || cycle_holds(input.x, input.y, shift, alt || (input.middle_presses_ != 0))
                ? (pick_cycle().cycle_.index + 1) % static_cast<int>(pick_cycle().cycle_.order.size())
                : -1;
        if (have_ray && drag_state().hover_axis_ < 0 && pick_cycle().hover_cycle_ >= 0) {
            const picking::TableRow& next = core::at(pick_cycle().cycle_.order, pick_cycle().hover_cycle_);
            if (next.kind == picking::Choice::kNode) {
                drag_state().hover_node_ = static_cast<int>(next.id);
            } else if (next.kind == picking::Choice::kObject) {
                hover = get_live({.object = core::down_cast<objects::GameEntity>(objects::find_object(next.id)), .id = next.id});
            } else {
                hover_sector = next.id;
            }
        } else if (have_ray && drag_state().hover_axis_ < 0) {
            const bool everything = alt || (input.middle_presses_ != 0);
            picking::Candidates candidates;
            scenery::Hit hit;
            scenery::get_pick(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, &hit);
            // (a gizmo behind the first occluding surface under the cursor isn't taken by a plain click)
            candidates.node = ui::underway() ? nodes::pick_nearest(pick_cycle().pickables_, {.x = back_x, .y = back_y},
                                                                   kHandlePickPixels * pixel_scale(),
                                                                   gizmo_occluder(frame, dir, hit, everything))
                                             : -1;
            float object_t = kMaxPickDistance;
            objects::GameEntity* object = object_edit::get_pick(level_editor_state().pos_, dir, kMaxPickDistance, &object_t);
            candidates.object = object != nullptr;
            candidates.object_t_ = object_t;
            // (Alt: the nearest pickable piece whatever is in front of it; a plain click: the first, if nothing occludes)
            const uint32_t scenery_pick = everything ? hit.nearest : hit.checksum;
            candidates.scenery = scenery_pick != 0;
            candidates.scenery_t_ = everything ? hit.nearest_t_ : hit.t;
            candidates.passed = hit.passed != 0;
            candidates.passed_t_ = hit.passed_t_;
            candidates.blocked = hit.blocked;
            candidates.block_t_ = hit.block_t_;
            switch (picking::get_choose(candidates, drag_state().pick_filter_, everything)) {
            case picking::Choice::kNode:
                drag_state().hover_node_ = candidates.node;
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
                if ((drag_state().pick_filter_ & picking::kScenery) != 0U) {
                    drag_state().hover_passed_ = hit.passed;
                }
                break;
            }
        }
        if (debug_enabled() && hover_sector != drag_state().hover_sector_ && (hover_sector != 0U)) {
            get_log("level editor: hovering %s", scenery::describe_hover(hover_sector).c_str());
        }
        drag_state().hover_ = hover ? Picked{.object = hover, .id = hover->ident()} : Picked{};
        drag_state().hover_sector_ = hover_sector;
    }
}

// A click on something: selected (Shift: added / removed; Alt or a middle click: one piece of scenery in / out)
// and dragged; on nothing, deselected.
// A click on scenery: one piece in / out (Alt, a middle click), Shift adding / dropping its group, else its group
// selected; whether it's dragged.
bool click_scenery(bool one, bool shift)
{
    bool drag = false;
    drag_state().objects_.clear();
    if (one) {
        scenery::flip_over(drag_state().hover_sector_);
    } else if (shift) {
        scenery::get_selected(drag_state().hover_sector_) ? scenery::flip_over(drag_state().hover_sector_)
                                                          : scenery::add_group(drag_state().hover_sector_);
    } else if (!scenery::get_selected(drag_state().hover_sector_)) {
        scenery::select_group(drag_state().hover_sector_);
    }
    drag = scenery::has_selection() && !one && !shift;
    return drag;
}

// A click on an object: Shift adding / dropping it, else it selected (or first of the selection); whether it's
// dragged.
bool click_object(objects::GameEntity* hover, bool shift)
{
    bool drag = false;
    scenery::clear_selection();
    drag_state().node_selected_ = false;
    if (shift) {
        if (is_picked(hover)) {
            std::erase_if(drag_state().objects_, [&](const Picked& picked) { return picked.object == hover; });
        } else {
            drag_state().objects_.push_back({.object = hover, .id = hover->ident()});
        }
    } else {
        if (!is_picked(hover)) {
            select_objects({hover});
        } else if (get_primary() != hover) {
            // (the clicked one first: the camera and the arrows go by it)
            auto it = std::ranges::find_if(drag_state().objects_,
                                           [&](const Picked& picked) { return picked.object == hover; });
            std::rotate(drag_state().objects_.begin(), it, it + 1);
        }
        drag = true;
    }
    return drag;
}

void click_to_select(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    objects::GameEntity* hover = get_live(drag_state().hover_);
    const bool one = alt || ((input.middle_presses_ != 0) && (input.left_presses_ == 0));
    bool drag = false;
    if (drag_state().hover_axis_ < 0) {
        note_click_for_cycle({.x = input.x, .y = input.y}, dir, (input.left_presses_ != 0) && !shift && !one);
    }
    // (with the collision shown, Shift+click on an invisible wall's face: that wall - level_editor_view.h)
    const uint32_t wall =
        (input.left_presses_ != 0) && shift && !alt && drag_state().hover_axis_ < 0 && view::show_collision()
            ? view::pick_wall(level_editor_state().pos_, make_unit(dir))
            : 0;
    // (a plain click on the selected wall's face: dragging it - level_editor_walls.h)
    const bool on_wall = (wall == 0U) && (input.left_presses_ != 0) && !shift && !one && drag_state().hover_axis_ < 0 &&
                         (wall_only() != 0U) && view::show_collision() &&
                         view::pick_wall(level_editor_state().pos_, make_unit(dir)) == wall_only();
    if ((wall == 0U) && !on_wall && drag_state().hover_axis_ < 0) {
        view::clear_wall();
    }
    // (with the trigger volumes shown, a plain click on one - nearer than the rest: its node; Alt passes them -
    // level_editor_triggers.h)
    int trigger_node = -1;
    uint32_t trigger_sector = 0;
    const bool trigger = (wall == 0U) && (input.left_presses_ != 0) && !alt && drag_state().hover_axis_ < 0 &&
                         drag_state().hover_node_ < 0 && ui::underway() &&
                         triggers::get_pick(level_editor_state().pos_, make_unit(dir), &trigger_node, &trigger_sector,
                                            trigger_reach(dir));
    if (wall != 0U) {
        unpick();
        view::select_wall(wall);
        hud::get_message("Invisible wall " + object_edit::caption(wall));
    } else if (on_wall) {
        drag = true;
    } else if (drag_state().hover_axis_ >= 0 && (input.left_presses_ != 0)) {
        drag_state().axis_ = drag_state().hover_axis_;
        drag = true;
    } else if (drag_state().hover_node_ >= 0 && (input.left_presses_ != 0)) {
        select_node(drag_state().hover_node_);
        drag = have_selection() && !shift; // (an object, or a point node: dragged as objects are)
    } else if (trigger) {
        if (trigger_node >= 0) {
            select_node_only(trigger_node);
            drag = picked_node_index() >= 0 && !shift;
        } else {
            unpick();
        }
        hud::get_message((trigger_sector != 0U) ? "Trigger " + object_edit::caption(trigger_sector)
                                                : std::string("Trigger volume"));
    } else if (drag_state().hover_sector_ != 0U) {
        drag = click_scenery(one, shift);
    } else if ((input.left_presses_ != 0) && hover && flag_node_of(hover) >= 0) {
        // (a flag's object: its node is what moves - level_editor_node_edit.h)
        select_node(flag_node_of(hover));
        drag = have_selection() && !shift;
    } else if ((input.left_presses_ != 0) && hover) {
        drag = click_object(hover, shift);
    } else if ((input.left_presses_ != 0) && !shift && !ctrl) {
        unpick();
    }
    if (get_primary() || scenery::has_selection()) {
        drag_state().node_selected_ = false;
    }
    note_action("click - selected: %s", describe_selection().c_str());
    if (debug_enabled()) {
        get_log("level editor: click - selected: %s", describe_selection().c_str());
    }
    if (drag && organize::selection_locked(true)) {
        drag = false; // (the Outliner's lock: selected, not moved)
    }
    if (drag) {
        // (on a Rotate / Scale handle: that tool - level_editor_transform.h; else it moves)
        drag_state().drag_mode_ = drag_state().axis_ >= 0 ? gizmo_mode() : xform::Mode::kShiftBy;
        drag_state().modal_ = false;
        drag_state().typed_ = {};
        drag_state().dragging_ = true;
        drag_state().drag_start_ = selection_pos();
        drag_state().press_x_ = input.x;
        drag_state().press_y_ = input.y;
        drag_state().drag_moved_ = false;
        begin_change();
        start_drag(level_editor_state().pos_, dir, shift && !ctrl && !alt && drag_state().axis_ < 0, frame, input.x,
                   input.y);
    }
}

// An armed Link / path assignment's click: the node under the cursor taken (none: still waiting).
void pick_for_chain()
{
    if (drag_state().hover_node_ >= 0) {
        const chains::State before = chains::at_hand();
        std::string message;
        uint32_t select = 0;
        chains::get_picked(core::at(nodes::get_all(), drag_state().hover_node_).name, &message, &select);
        after_chain_change(before, select, message);
    } else {
        hud::get_message(chains::get_waiting());
    }
}

// A click: on an arrow, dragging along it; on a tool's target, the tool's pick; else selecting.
void handle_clicks(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    // A click: on an arrow, dragging along it; on something, selecting it (Shift: adding / removing it; Alt or a
    // middle click: one piece of scenery in / out; Ctrl on a part of the scenery selection: that part dropped) and
    // dragging it; on nothing, deselecting.
    // A trigger volume's resize handle (level_editor_triggers.h): pressed, its face follows the mouse along its axis
    // (Ctrl / Snap 25: sizes on 25s; the opposite face stays); let go, one undo step.
    if (triggers::resizing()) {
        if (input.left && have_ray) {
            triggers::rescale(level_editor_state().pos_, dir, ctrl || drag_state().snap_always_);
        }
        if (!input.left) {
            triggers::end_resize();
            note_action("resized a trigger volume: %s", describe_selection().c_str());
        }
    }
    // Box select armed (Ctrl+B): the press starts a box wherever it is (nothing else takes it).
    if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && organize::box_armed()) {
        organize::box_press({.x = back_x, .y = back_y},
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
        input.left_presses_ = 0;
    }
    // A press where nothing is (no arrow, gizmo, object or scenery under the cursor): a box if it's dragged (Shift
    // adds, Ctrl removes - the click itself does what it did: deselects when plain).
    bool box_candidate = (input.left_presses_ != 0) && have_ray && !drag_state().dragging_ &&
                         drag_state().hover_axis_ < 0 && drag_state().hover_handle_ < 0 &&
                         drag_state().hover_node_ < 0 && (get_live(drag_state().hover_) == nullptr) &&
                         (drag_state().hover_sector_ == 0U) && !faces::get_picking() && !chains::wants_pick() &&
                         chains::get_drawing() == chains::Blit::kNone &&
                         !(pick_cycle().place_armed_ >= 0 && pick_cycle().place_open_) && !alt;
    // (UX8: a double click on nothing a click takes - a surface - with nothing selected (the first click deselects):
    // the camera glides there - level_editor_ux8.h)
    if (box_candidate && !ruler::get_armed() && !organize::box_armed() &&
        ux8::note_empty_click(SDL_GetTicks(), input.x, input.y) && !have_selection() && !drag_state().node_selected_) {
        teleport_to(dir);
        box_candidate = false;
    }
    const bool handle_pressed = (input.left_presses_ != 0) && have_ray && !drag_state().dragging_ &&
                                drag_state().hover_axis_ < 0 && drag_state().hover_handle_ >= 0 &&
                                !triggers::resizing() &&
                                triggers::begin_resize(drag_state().hover_handle_, level_editor_state().pos_, dir);
    if (handle_pressed) {
        hud::get_message("Resize: drag the face (Ctrl: snap)", kResizeHintSeconds);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && drag_state().hover_axis_ < 0 &&
               ux9::occupied() && ux9::get_click(level_editor_state().pos_, make_unit(dir), back_x, back_y)) {
        // (UX9: the gap wizard's / the ground tool's pick - level_editor_ux9.h)
        note_action("ux9 tool click");
    } else if (((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && arrange::edge_picking() &&
                arrange::edge_click({.x = back_x, .y = back_y})) ||
               ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && stored_browser().open &&
                drag_state().hover_axis_ < 0 && !ctrl && !alt && arrange::place_click(shift))) {
        // (F7: a rail along the edge clicked; or the Add palette open with Click places: its piece where the ghost
        // showed it - level_editor_arrange.h)
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && ruler::get_armed() &&
               drag_state().hover_axis_ < 0) {
        // (the ruler armed: a click puts an end - level_editor_ruler.h)
        hud::get_message(ruler::get_click(), hud::kLongMessageSeconds);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && faces::get_picking() &&
               drag_state().hover_axis_ < 0) {
        // (map3: Faces mode - the collision face clicked, Shift adds / drops it: level_editor_faces.h)
        faces::click_at(level_editor_state().pos_, make_unit(dir), shift);
        note_action("faces mode click: %s", faces::describe().c_str());
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && chains::wants_pick() &&
               drag_state().hover_axis_ < 0) {
        // (an armed Link / path assignment: the node clicked - level_editor_chains.h)
        pick_for_chain();
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ &&
               chains::get_drawing() != chains::Blit::kNone && drag_state().hover_axis_ < 0) {
        draw_chain_point(dir, alt);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && pick_cycle().place_armed_ >= 0 &&
               pick_cycle().place_open_ && drag_state().hover_axis_ < 0) {
        place_node(pick_cycle().place_armed_, cursor_point(dir, have_ray, frame));
        if (!shift) {
            pick_cycle().place_armed_ = -1;
        }
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && ctrl && !alt && !shift &&
               drag_state().hover_axis_ < 0 && drop_under_cursor(dir)) {
        // (Ctrl+click: one part of the selection dropped - no drag)
        pick_cycle().cycle_.have = false;
        note_action("ctrl+click - selected: %s", describe_selection().c_str());
        if (debug_enabled()) {
            get_log("level editor: ctrl+click - selected: %s", describe_selection().c_str());
        }
    } else if (((input.left_presses_ != 0) || (input.middle_presses_ != 0)) && have_ray && !drag_state().dragging_) {
        click_to_select(editor_frame);
    }
    if (box_candidate && !drag_state().dragging_) {
        organize::box_press({.x = back_x, .y = back_y},
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
    }
}

// The end of the frame: let go, the highlights, the ground marker, the camera tools.
void finish_frame(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    organize::box_track(input.left, back_x, back_y);
    // Let go: the selection becomes the frame of reference (the camera turns to it, at the distance it is).
    if ((input.left_releases_ != 0) && !input.left) {
        if (drag_state().dragging_ && drag_state().drag_moved_) {
            pick_cycle().cycle_.have = false; // (moved: the next click starts afresh)
        }
        end_drag();
        focus_on_selection();
        pick_cycle().cycle_.changes = level_editor_state().changes_;
    }
    update_highlights();
    // The ground marker: the point below the selection's base.
    drag_state().have_mark_ = false;
    if (have_selection()) {
        const V3 pos = selection_pos();
        drag_state().mark_base_ = {.x = pos.x, .y = get_selection_base(), .z = pos.z};
        const V3 from = get_primary() || picked_node_index() >= 0
                            ? drag_state().mark_base_ + V3{.x = 0.0F, .y = 2.0F, .z = 0.0F}
                            : drag_state().mark_base_ - V3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
        drag_state().have_mark_ = ground_below(from, &drag_state().mark_ground_);
    }
    // The camera tools' end of the frame: a flight, an ortho view kept on its axis (level_editor_camera.h).
    const bool flew = actions::get_held(keys, actions::kForward) || actions::get_held(keys, actions::kBack) ||
                      actions::get_held(keys, actions::kLeft) || actions::get_held(keys, actions::kRight) ||
                      actions::get_held(keys, actions::kFlyUp) || actions::get_held(keys, actions::kFlyDown);
    view_cam::after_update(rig_now(), input.wheel,
                           input.right || (input.left_presses_ != 0) || (input.middle_presses_ != 0) ||
                               (input.wheel != 0) || flew || view_cam::get_orbiting());
}

} // namespace

void tick_frame()
{
    tick_level_systems();
    if (!level_editor_state().active_) {
        return;
    }
    tick_hud_and_autosave();
    EditorFrame editor_frame;
    editor_frame.seconds = take_frame_seconds();
    editor_frame.input = take_mouse();
    if (!level_editor_state().camera_) {
        return;
    }
    read_keys_and_pad(editor_frame);
    if (global_keys_and_walk(editor_frame)) {
        return;
    }
    handle_escape();
    editor_frame.no_change_ = drag_state().dragging_ || level_editor_state().key_moving_ ||
                              drag_state().field_editing_ || triggers::resizing();
    tick_panels(editor_frame);
    edit_keys(editor_frame);
    view_keys();
    tick_triggers_and_tools(editor_frame);
    command_keys(editor_frame);
    finish_chain_drawing(editor_frame.input);
    look_and_fly(editor_frame);
    cast_mouse_ray(editor_frame);
    apply_ray_tools(editor_frame);
    browser_keys(editor_frame);
    move_with_keys(editor_frame);
    log_mouse_debug(editor_frame);
    update_tool_hovers(editor_frame);
    update_drag(editor_frame);
    update_hover(editor_frame);
    handle_clicks(editor_frame);
    finish_frame(editor_frame);
}

void blit()
{
    if (!level_editor_state().active_ || !level_editor_state().have_view_ || !render::device().device_made()) {
        return;
    }
    const TickCycle frame = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
    // What's hovered and selected shows tinted (update_highlights); boxes only for what couldn't be (yellow hovered,
    // cyan selected); the selection's grind rails, the ground marker and the move arrows as lines.
    static std::vector<LineVertex> lines;
    lines.clear();
    const std::vector<objects::GameEntity*> objects = selected_objects();
    objects::GameEntity* hover = drag_state().dragging_ ? nullptr : get_live(drag_state().hover_);
    add_hover_boxes(frame, hover, lines);
    add_selected_boxes(frame, objects, lines);
    if (objects.empty()) {
        for (const auto& segment : scenery::selected_rails()) {
            add_line(segment.first, segment.second, frame, kSegmentColour, &lines);
        }
    }
    // The ground marker: a line from the selection's base down (or up) to the ground below it, and a cross there.
    if (drag_state().have_mark_ && have_selection()) {
        constexpr uint32_t kMark = 0xFFFFE070;
        if (std::fabs(drag_state().mark_base_.y - drag_state().mark_ground_.y) > 1.0F) {
            add_line(drag_state().mark_base_, drag_state().mark_ground_, frame, kMark, &lines);
        }
        const float size = std::max(10.0F, std::min(60.0F, level_editor_state().orbit_distance_ * 0.05F));
        add_line(drag_state().mark_ground_ - V3{.x = size, .y = 0.0F, .z = 0.0F},
                 drag_state().mark_ground_ + V3{.x = size, .y = 0.0F, .z = 0.0F}, frame, kMark, &lines);
        add_line(drag_state().mark_ground_ - V3{.x = 0.0F, .y = 0.0F, .z = size},
                 drag_state().mark_ground_ + V3{.x = 0.0F, .y = 0.0F, .z = size}, frame, kMark, &lines);
    }
    // The gizmo (level_editor_transform.h): Move's arrows, Rotate's ring, Scale's cubes - while dragging with one, that
    // one only (a modal Rotate / Scale: the whole gizmo).
    add_gizmo_lines(frame, lines);
    // The UI: the lines through ImGui's draw list, with the panels (level_editor_ui.h).
    if (ui::underway()) {
        draw_with_ui(frame, objects, lines);
        return;
    }
    // The old HUD: the lines on the device (the renderer's cached state).
    if (lines.empty()) {
        return;
    }
    render::device().draw_screen_lines(lines);
}

std::vector<std::pair<std::string, std::string>> describe_for_screenshot()
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!level_editor_state().active_) {
        return out;
    }
    const ui::ShapeMesh model = assemble_model();
    std::array<char, kStatusTextSize> text{};
    out.emplace_back("mode", std::string(model.mode ? model.mode : "") +
                                 (ui::underway() ? " (Dear ImGui panels)" : " (old HUD)"));
    out.emplace_back("level", model.level);
    text::format_to(text, "(%.1f, %.1f, %.1f)", level_editor_state().pos_.x, level_editor_state().pos_.y, level_editor_state().pos_.z);
    out.emplace_back("camera", text.data());
    text::format_to(text, "yaw %.1f, pitch %.1f degrees", level_editor_state().yaw_ * kDegreesPerHalfTurn / maths::kPi, level_editor_state().pitch_ * kDegreesPerHalfTurn / maths::kPi);
    out.emplace_back("camera turn", text.data());
    out.emplace_back("selected", model.selected);
    // The view options (level_editor_view.h).
    text::format_to(text, "collision %s (%d faces, %d pieces, %d invisible walls%s), %d hidden in the editor",
                    model.view.collision ? "shown" : "off", model.view.triangles, model.view.sectors, model.view.walls,
                    model.view.capped ? ", capped" : "", model.view.hidden);
    out.emplace_back("view", text.data());
    out.emplace_back("triggers", std::string(triggers::get_shown() ? "shown: " : "hidden: ") + triggers::describe());
    out.emplace_back("goals", goal_edit::describe());
    if (model.view.wall != 0U) {
        out.emplace_back("invisible wall", model.view.wall_name_ + ": " + model.view.wall_info_);
    }
    const ui::ShapeMesh::Selection& sel = model.selection;
    if (!sel.kind.empty()) {
        text::format_to(text, "%s %s at (%.1f, %.1f, %.1f)", sel.kind.c_str(), sel.name.c_str(), sel.pos.x, sel.pos.y,
                        sel.pos.z);
        out.emplace_back("selection", text.data());
    }
    if (model.nodes.selected >= 0 && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& node = core::at(nodes::get_all(), model.nodes.selected);
        text::format_to(text, "%s (%s, %s) at (%.1f, %.1f, %.1f)%s%s", nodes::caption(node.name).c_str(),
                        nodes::caption(node.cls).c_str(), nodes::family_name(node.family), node.pos.x, node.pos.y,
                        node.pos.z, node.added ? ", added" : "", node.hidden ? ", hidden" : "");
        out.emplace_back("inspected node", text.data());
    }
    out.emplace_back("under the cursor", model.hover);
    // (the view menus - level_editor_overlays.h: the view mode, the overlays' bits, the arcs, the ruler, the rail shown)
    text::format_to(text, "%s, overlays 0x%03X", overlays::view_mode_name(overlays::view_mode()), overlays::at_hand());
    out.emplace_back("view mode", text.data());
    for (const std::string& line : {arcs::describe(), ruler::describe(), grind_preview::describe()}) {
        if (!line.empty()) {
            out.emplace_back("measure", line);
        }
    }
    if (const std::string ux9 = ux9::describe(); !ux9.empty()) {
        out.emplace_back("ux9", ux9);
    }
    text::format_to(text, "%d changes, %d pieces moved%s", model.changes, model.moved,
                    model.unsaved ? ", unsaved" : "");
    out.emplace_back("changes", text.data());
    // (UX5) the problem checker's last count (the level's own and ignored ones left out) and the notes
    {
        int errors = 0;
        int warnings = 0;
        int infos = 0;
        checks::shown_counts(&errors, &warnings, &infos);
        text::format_to(text, "%d errors, %d warnings, %d notes; %u layout records unresolved; %u note pins", errors,
                        warnings, infos, static_cast<unsigned>(checks::layout_issues().size()),
                        static_cast<unsigned>(notes::get_all().size()));
        out.emplace_back("problems", text.data());
    }
    if (!model.message.empty()) {
        out.emplace_back("message", model.message);
    }
    return out;
}

SelectionIds current_selection()
{
    SelectionIds ids;
    for (const objects::GameEntity* object : selected_objects()) {
        ids.objects.push_back(object->ident());
    }
    if (ids.objects.empty()) {
        ids.sectors = scenery::get_selection();
    }
    if (ids.objects.empty() && ids.sectors.empty() && drag_state().node_selected_ && inspected_node() >= 0) {
        ids.node = drag_state().node_name_;
    }
    return ids;
}

void set_selection(const SelectionIds& ids)
{
    if (drag_state().dragging_) {
        return;
    }
    unpick();
    std::vector<objects::GameEntity*> objects;
    for (const uint32_t id : ids.objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
            objects.push_back(object);
        }
    }
    if (!objects.empty()) {
        select_objects(objects);
    } else if (!ids.sectors.empty()) {
        scenery::set_selection(ids.sectors);
    } else if (ids.node != 0U) {
        const int index = nodes::get_find(ids.node);
        if (index >= 0) {
            select_node(index);
        }
    }
    if (have_selection()) {
        focus_on_selection();
    }
    note_action("selected: %s", describe_selection().c_str());
}

bool go_to_thing(uint32_t id)
{
    if (!level_editor_state().active_ || (id == 0U) || drag_state().dragging_) {
        return false;
    }
    const int index = nodes::get_find(id);
    if (index >= 0) {
        go_to_node(index);
        if (have_selection()) {
            go_to_selection();
        }
        return true;
    }
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
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
    if (!level_editor_state().active_ || !level_editor_state().have_view_) {
        return false;
    }
    float nx = 0.0F;
    float ny = 0.0F;
    if (!cast_onto(level_editor_state().pos_,
                   frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_),
                   level_editor_state().lens_, point, kNearZ, &nx, &ny)) {
        return false;
    }
    ndc_to_pixel(level_editor_state().viewport_, nx, ny, x, y);
    return true;
}

uint32_t pick_filter_now() { return drag_state().pick_filter_; }

int set_fields_on_nodes(const std::vector<uint32_t>& nodes, const std::vector<FieldSet>& fields, const char* label,
                        std::string* message)
{
    UndoStep step;
    step.kind = UndoStep::Kind::kFields;
    int set = 0;
    for (const uint32_t node : nodes) {
        if (nodes::get_find(node) < 0) {
            continue;
        }
        for (const FieldSet& field : fields) {
            fields::Own before;
            fields::get_capture(node, field.key, &before);
            fields::Value value;
            std::string error;
            if (!field.removed && !fields::parse(static_cast<fields::Kind>(field.kind), field.text, &value, &error)) {
                if (message->empty()) {
                    *message = error;
                }
                continue;
            }
            if (!fields::assign_value(node, field.key, value, field.removed, &error)) {
                if (message->empty()) {
                    *message = object_edit::caption(node) + ": " + error;
                }
                continue;
            }
            step.fields.push_back({.node = node, .key = field.key, .own = before});
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
    if (!have_selection()) {
        return false;
    }
    *transform = TransformValues{};
    transform->pos = selection_pos();
    node_edit::Values node;
    if (objects::GameEntity* object = get_primary()) {
        const maths::Vec4& at = core::at(object->basis_frame().row, 2);
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
    if (!have_selection() || change_under_way()) {
        return false;
    }
    if (organize::selection_locked(true)) {
        return false;
    }
    push_undo(get_snapshot(), "Paste transform");
    place_selection(transform.pos);
    node_edit::Values node;
    if (transform.has_heading_) {
        if (const objects::GameEntity* object = get_primary()) {
            const maths::Vec4& at = core::at(object->basis_frame().row, 2);
            auto now = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)));
            const V3 pivot = object_edit::pos_of(object);
            for (objects::GameEntity* selected_object : selected_objects()) {
                object_edit::turn_about(selected_object, pivot, transform.heading - now);
            }
        } else if (node_values(&node)) {
            node.angles.y = transform.heading;
            set_selected_node(node);
        }
    }
    if (transform.has_scale_ && (get_primary() || scenery::has_selection()) && picked_node_index() < 0) {
        scale_selection_to(transform.scale);
    }
    if (!get_primary() && scenery::has_selection()) {
        follow_scenery();
    }
    get_settle();
    note_action("pasted a transform: %s", describe_selection().c_str());
    return true;
}

void show_message(const std::string& text, double seconds) { hud::get_message(text, seconds); }

bool current_view(ViewNow* view)
{
    if (!level_editor_state().active_ || !level_editor_state().have_view_) {
        return false;
    }
    view->pos = level_editor_state().pos_;
    view->yaw = level_editor_state().yaw_;
    view->pitch = level_editor_state().pitch_;
    view->lens = level_editor_state().lens_;
    std::memcpy(view->viewport.data(), level_editor_state().viewport_.data(), sizeof(view->viewport));
    view->near_z_ = kNearZ;
    return true;
}

void look_from(V3 pos, YawPitch view)
{
    const auto yaw = view.yaw;
    const auto pitch = view.pitch;
    if (!level_editor_state().active_) {
        return;
    }
    unpick();
    drag_state().dragging_ = false;
    level_editor_state().pos_ = pos;
    level_editor_state().yaw_ = yaw;
    level_editor_state().pitch_ = std::clamp(pitch, -kMaxPitchDegrees * maths::kPi / kDegreesPerHalfTurn, kMaxPitchDegrees * maths::kPi / kDegreesPerHalfTurn);
}

// (UX5 - level_editor_modules.h) the Problems window's and the notes' Go to.
bool go_to_node_named(uint32_t name)
{
    const int index =
        level_editor_state().active_ && !drag_state().dragging_ && (name != 0U) ? nodes::get_find(name) : -1;
    if (index < 0) {
        return false;
    }
    go_to_node(index);
    if (have_selection()) {
        go_to_selection();
    }
    return true;
}

bool go_to_scenery(uint32_t sector)
{
    if (!level_editor_state().active_ || drag_state().dragging_ || (sector == 0U) || !scenery::sector_of(sector) ||
        scenery::why_not_movable(sector)) {
        return false;
    }
    unpick();
    scenery::set_selection({sector});
    go_to_selection();
    return true;
}

void go_to_point(V3 pos, float distance)
{
    if (!level_editor_state().active_ || drag_state().dragging_) {
        return;
    }
    unpick();
    get_look_at(pos, distance);
}

namespace edit {

Selected get_selection()
{
    Selected out;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        out.kind = Kind::kObject;
        for (const objects::GameEntity* object : objects) {
            out.ids.push_back(object->ident());
        }
    } else if (picked_node_index() >= 0) {
        out.kind = Kind::kElem;
        out.ids.push_back(drag_state().node_name_);
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
            if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
                objects.push_back(object);
            }
        }
        unpick();
        select_objects(objects);
        break;
    }
    case Kind::kScenery:
        drag_state().objects_.clear();
        drag_state().node_selected_ = false;
        view::clear_wall();
        scenery::set_selection(selected.ids);
        break;
    case Kind::kElem: {
        const int index = selected.ids.empty() ? -1 : nodes::get_find(selected.ids.front());
        if (index >= 0) {
            select_node_only(index);
        }
        break;
    }
    case Kind::kWall:
        unpick();
        if (!selected.ids.empty()) {
            view::select_wall(selected.ids.front());
        }
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
    drag_state().pending_forced_ = true;
    end_change();
    get_settle();
}

void place_at(V3 pos) { place_selection(pos); }

void turn_by(float radians)
{
    node_edit::Values node;
    if (node_values(&node)) {
        node.angles = node_edit::with_heading(node.angles, node_edit::get_heading_degrees(node.angles) +
                                                               (radians * kDegreesPerHalfTurn / maths::kPi));
        set_selected_node(node);
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        turn_objects_by(objects, radians, selection_centre());
        return;
    }
    if (const int quarters = xform::get_quarters(radians)) {
        turn_selection(quarters);
    }
}

bool scale_by(V3 factor, bool uniform) { return scale_selection_by(factor, uniform); }

std::vector<std::pair<uint32_t, uint32_t>> get_duplicate(const void* repeat)
{
    duplicate_pairs().clear();
    level_editor::get_duplicate(frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_), static_cast<const xform::RepeatStep*>(repeat));
    return duplicate_pairs();
}

uint64_t top_serial() { return undo_steps().undo_.empty() ? 0 : undo_steps().undo_.back().serial; }

void get_settle() { level_editor::get_settle(); }

bool set_field(FieldKey field_key, const std::string& text)
{
    const auto node = field_key.node;
    const auto key = field_key.key;
    const auto kind = field_key.kind;
    ui::Request request;
    request.kind = ui::Request::Kind::kFieldSet;
    request.node = node;
    request.id = key;
    request.value = kind;
    request.text = text;
    const size_t before = undo_steps().undo_.size();
    const uint64_t serial = top_serial();
    field_request(request);
    return undo_steps().undo_.size() != before || top_serial() != serial;
}

void chain_changed(const void* before, uint32_t select, const std::string& message)
{
    after_chain_change(*static_cast<const chains::State*>(before), select, message);
}

bool get_ground(V3 from, V3* ground) { return ground_below(from, ground); }
bool get_dragging() { return level_editor_state().active_ && drag_state().dragging_; }
int drag_mode() { return static_cast<int>(drag_state().drag_mode_); }
maths::Vec2 get_cursor() { return {.x = drag_state().cursor_x_, .y = drag_state().cursor_y_}; }
bool get_ray(RayOut ray)
{
    auto* const origin = ray.origin;
    auto* const dir = ray.dir;
    if (!level_editor_state().active_ || !level_editor_state().last_have_ray_) {
        return false;
    }
    *origin = level_editor_state().pos_;
    *dir = level_editor_state().last_ray_;
    return true;
}
bool ground_mark(GroundMark mark)
{
    auto* const base = mark.base;
    auto* const ground = mark.ground;
    if (!drag_state().have_mark_ || !have_selection()) {
        return false;
    }
    *base = drag_state().mark_base_;
    *ground = drag_state().mark_ground_;
    return true;
}
float get_yaw() { return level_editor_state().yaw_; }

bool browser_item(int* tab, uint32_t* id)
{
    if (!stored_browser().open || stored_browser().index < 0 ||
        std::cmp_greater_equal(stored_browser().index, stored_browser().items.size())) {
        return false;
    }
    *tab = stored_browser().tab;
    *id = core::at(stored_browser().items, stored_browser().index).id;
    return true;
}
void place_browser_item(V3 at, bool keep_open) { place_from_browser(at, keep_open); }

int place_armed(uint32_t* colour)
{
    *colour = 0;
    if (!pick_cycle().place_open_ || pick_cycle().place_armed_ < 0) {
        return -1;
    }
    if (std::cmp_less(pick_cycle().place_armed_, place_entries().size())) {
        *colour = core::at(place_entries(), pick_cycle().place_armed_).colour;
    }
    return pick_cycle().place_armed_;
}

V3 cursor_point()
{
    return level_editor::cursor_point(level_editor_state().last_ray_, level_editor_state().last_have_ray_, frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_));
}

} // namespace edit
} // namespace frontend::level_editor
