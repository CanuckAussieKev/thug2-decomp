// NOT ORIGINAL - the level editor's frame: its steps in order (systems, input, panels, the ray, tools, the
// finish) (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/pad_input/input_source.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <array>
#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_arrange.h"
#include "editor/level_editor_budget.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_links.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_ux9.h"
#include "frontend/mouse_menus.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "base/core/checked_index.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr double kAutosaveNoticeSeconds = 20.0;

constexpr uint32_t kMillisecondsPerSecond = 1000U;

constexpr double kMaxFrameSeconds = 0.1;    // a frame counts for at most this long
constexpr double kFastFlyFactor = 4.0;      // Shift flies four times as fast

} // namespace
namespace internal {

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

} // namespace internal
} // namespace frontend::level_editor
