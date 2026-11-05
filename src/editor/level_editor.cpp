// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's). See
// level_editor.h. This file holds the editor's state, its events, start and stop, the frame's entry and the drawing
// hook; the pieces (geometry / selection / undo / drag / draw / panels / place / requests / commands / keys / pointer / frame / api) are level_editor_<name>.cpp, sharing level_editor_internal.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include "engine/pad_input/input_source.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_fields.h"
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_actions.h"
#include "base/core/format.h"
#include "editor/level_editor_arcs.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_grind_preview.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_objects.h"
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
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
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

constexpr uint32_t kSegmentColour = 0xFF40E0FF;   // ARGB

constexpr std::uint32_t kPos = 0x7F261953U; // Pos

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

namespace internal {

OcclusionSwitch& occlusion_switch()
{
    static core::NoDestroy<OcclusionSwitch> state;
    return state.get();
}

} // namespace internal

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

} // namespace frontend::level_editor
