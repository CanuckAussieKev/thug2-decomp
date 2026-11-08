// NOT ORIGINAL - the level editor's keys: edit, view and command keys, nudging, turning and scaling the selection,
// flying and walking (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/pad_input/input_source.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_nav.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "base/maths/vec2.h"
#include "base/core/format.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_ruler.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/screen.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "platform/sdl_time.h"
#include "base/core/checked_index.h"
#include "base/maths/trig.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr double kBriefSeconds = 1.5;
constexpr double kPairSeconds = 2.0;

constexpr int kKeyRepeatDelay = 400;        // milliseconds before a held key repeats ...
constexpr int kKeyRepeatStep = 83;          // ... the first repeat comes after this many of them

constexpr float kMinWheelStep = 20.0F;
constexpr float kWheelStepFraction = 0.1F;  // a wheel notch moves a tenth of the distance
constexpr double kWheelZoomStep = 0.85;     // an orbit wheel notch zooms by this factor
constexpr double kSpeedStepFactor = 1.5;    // a fly speed step changes the speed by this factor

constexpr double kObjectSpeed = 150.0; // units a second (Shift: 4x)
constexpr double kWheelStep = 120.0;   // units a notch while flying (Shift: 4x)

} // namespace
namespace internal {

// A key's press (true once per press).
bool squeezed(input::KeyState keys, SDL_Scancode key, bool* was)
{
    const bool down = core::at(keys, key) != 0;
    const bool pressed = down && !*was;
    *was = down;
    return pressed;
}

} // namespace internal
namespace {

// A key's press and, held, its repeats (after 0.4 s, 12 a second): the browser's list.
bool get_repeated(input::KeyState keys, SDL_Scancode key, uint32_t* since)
{
    if (core::at(keys, key) == 0U) {
        *since = 0;
        return false;
    }
    const uint32_t now = platform::ticks_ms();
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

} // namespace
namespace internal {

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
        shown = (click_cycle().cycle_.index + 1) % static_cast<int>(click_cycle().cycle_.order.size());
    } else {
        std::vector<picking::Layered> objects;
        std::vector<picking::Layered> scenery;
        for (const auto& hit : object_edit::pick_all(fly_camera().pos_, dir, kMaxPickDistance)) {
            objects.push_back({.id = hit.first->ident(), .t = hit.second, .passed = false});
        }
        scenery::Hit first;
        std::vector<scenery::Layer> layers;
        scenery::get_pick(fly_camera().pos_, make_unit(dir), kMaxPickDistance, &first, &layers);
        scenery.reserve(layers.size());
        for (const scenery::Layer& layer : layers) {
            scenery.push_back({.id = layer.checksum, .t = layer.t, .passed = layer.passed});
        }
        click_cycle().cycle_ = Cycle{};
        click_cycle().cycle_.have = true;
        click_cycle().cycle_.x = x;
        click_cycle().cycle_.y = y;
        click_cycle().cycle_.pos = fly_camera().pos_;
        click_cycle().cycle_.changes = unsaved_changes().changes_;
        click_cycle().cycle_.filter = tool_options().pick_filter_;
        click_cycle().cycle_.order = picking::cycle_order(picked_set().hover_node_, objects, scenery, tool_options().pick_filter_);
        const objects::GameEntity* object = get_live(picked_set().hover_);
        shown = hover_cycle_index(object);
    }
    const int count = static_cast<int>(click_cycle().cycle_.order.size());
    if (count < 2) {
        click_cycle().cycle_.have = false;
        stored_drilled() = false;
        hud::get_message((count != 0) ? "Only one item under cursor" : "Nothing under cursor", kBriefSeconds);
        return;
    }
    const int at = nav::drill_index(shown, steps, count);
    click_cycle().cycle_.index = nav::cycle_index_showing(at, count);
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
    if (!get_cast(fly_camera().pos_, fly_camera().pos_ + ray * kMaxPickDistance, &hit)) {
        hud::get_message("No surface under cursor");
        return;
    }
    if (have_selection() || picked_set().node_selected_) {
        unpick(); // (the camera would orbit it)
    }
    view_cam::get_cancel();
    ux8::start_glide(fly_camera().pos_, nav::teleport_target(fly_camera().pos_, hit));
    note_action("teleported to (%.0f, %.0f, %.0f)", hit.x, hit.y, hit.z);
}

// Walk mode's frame: the mouse (captured) and the right stick look, the fly keys / left stick walk, the fly-up key /
// A jump, Shift / L3 run; Escape (or B) stops.
void walk_frame(const Mouse& input, input::KeyState keys, const ux8::Pad& pad, double seconds)
{
    if (editor_session().cancel_requested_ || (pad.active && pad.b)) {
        editor_session().cancel_requested_ = false;
        ux8::get_cancel();
        set_relative(false);
        return;
    }
    set_relative(true);
    const prefs::Values& prefs = prefs::get_pull();
    const float dx = input.rel_x_ + (pad.look_x_ * kPadLookPixelsPerSecond * static_cast<float>(seconds));
    const float dy = input.rel_y_ - (pad.look_y_ * kPadLookPixelsPerSecond * static_cast<float>(seconds));
    get_look({.yaw = &fly_camera().yaw_, .pitch = &fly_camera().pitch_}, dx, prefs.invert_y_ ? -dy : dy,
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
    nav::get_step(&walker, fly_camera().yaw_, in, static_cast<float>(seconds), get_cast);
    fly_camera().pos_ = nav::eye_of(walker);
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
    if (drag_session().dragging_ || stored_browser().open || ctrl || picked_set().field_editing_) {
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
        fly_camera().free_camera_ = false;
    }
    // Alt+G: the tool's transform without holding the mouse (Blender's G / R / S).
    if (modal_key && have_selection() && have_ray) {
        drag_session().axis_ = -1;
        transform_session().drag_mode_ = gizmo_mode();
        transform_session().modal_ = true;
        transform_session().typed_ = {};
        drag_session().dragging_ = true;
        drag_session().drag_start_ = selection_pos();
        drag_session().press_x_ = input.x;
        drag_session().press_y_ = input.y;
        drag_session().drag_moved_ = true;
        begin_change();
        start_drag(fly_camera().pos_, dir, false, frame, input.x, input.y);
        hud::get_message(std::string(xform::mode_name(transform_session().drag_mode_)) + ": X/Y/Z constrain, type a value, Enter applies",
                         hud::kShortMessageSeconds);
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
            repeat_duplicate(fly_camera().frame());
        } else if (actions::squeezed(actions::kDuplicate)) {
            get_duplicate(fly_camera().frame());
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
    if (l_pressed && !rail_point && !drag_session().dragging_) {
        toggle_lock();
    }
    if (l_pressed && rail_point && !no_change) {
        tool_options().move_entire_rail_ = !tool_options().move_entire_rail_;
        hud::get_message(tool_options().move_entire_rail_ ? "Move whole chain" : "Move single point");
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
            !drag_session().dragging_) { // (while dragging digits are typed values)
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
    if ((actions::squeezed(actions::kRuler) || ruler_clear) && !stored_browser().open && !drag_session().dragging_) {
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
        const TickCycle view = fly_camera().frame();
        triggers::tick(fly_camera().pos_, view.back * -1.0F, fly_camera().yaw_);
        // (an editor trigger selected by itself: its resize handles)
        triggers::set_selected(picked_node_index() >= 0 && triggers::has_handles(picked_set().node_name_) ? picked_set().node_name_ : 0);
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
        if (select >= 0 && !drag_session().dragging_) {
            drawn_view().pickables_.clear();
            picked_set().hover_node_ = -1;
            select_node_only(select);
        }
        if (const uint32_t wall = triggers::take_wall_select(); (wall != 0U) && !drag_session().dragging_) {
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
        const TickCycle view = fly_camera().frame();
        const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
        get_log("level editor: camera in level %d (scene %s), yaw %.1f, pitch %.1f - as a kViews entry:",
                render::current_level(), object_edit::caption(scene).c_str(), fly_camera().yaw_ * kDegreesPerHalfTurn / maths::kPi,
                fly_camera().pitch_ * kDegreesPerHalfTurn / maths::kPi);
        get_log("    {{%.3ff, %.3ff, %.3ff}, {%.6ff, %.6ff, %.6ff}, {%.6ff, %.6ff, %.6ff}},", fly_camera().pos_.x, fly_camera().pos_.y, fly_camera().pos_.z,
                view.back.x, view.back.y, view.back.z, view.up.x, view.up.y, view.up.z);
        hud::get_message("Camera position logged");
    }
    if (actions::squeezed(actions::kDeselect) && (have_selection() || picked_set().node_selected_) &&
        !drag_session().dragging_) {
        unpick();
    }
    // (Home and Delete: not on what the Outliner's lock holds - level_editor_organize.h)
    if (actions::squeezed(actions::kHome) && !no_change && !stored_browser().open &&
        !organize::selection_locked(true)) {
        get_reset();
    }
    if (!no_change && !stored_browser().open && (confirmations().confirm_hide_ == 0U) && !confirmations().confirm_reset_) {
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
    if (picked_set().node_selected_ && !no_change && !stored_browser().open && chains::is_chain_node(picked_set().node_name_)) {
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
                place_palette().place_armed_ = -1;
                unpick();
            }
        }
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

} // namespace internal
namespace {

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
    if (input.right && !ctrl && !drag_session().dragging_) {
        const FlyInput fly = {.forward = (actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                         (actions::get_held(keys, actions::kBack) ? 1 : 0),
                              .right = (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                       (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                              .up = (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
        if ((fly.forward != 0) || (fly.right != 0) || (fly.up != 0)) {
            fly_camera().free_camera_ = true;
            fly_camera().pos_ =
                get_fly_to(fly_camera().pos_, get_fly(fly_camera().pos_, frame, fly,
                                                      static_cast<float>(fly_camera().speed_ * boost * seconds)));
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
    if (((forward_step != 0) || (right_step != 0) || (u != 0)) && !drag_session().dragging_ &&
        !picked_set().field_editing_) {
        if (!fly_camera().key_moving_) {
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
        fly_camera().key_moving_ = true;
    } else {
        if (fly_camera().key_moving_) {
            const uint64_t serial = undo_steps().undo_.empty() ? 0 : undo_steps().undo_.back().serial;
            end_change();
            get_settle();
            if (!undo_steps().undo_.empty() && undo_steps().undo_.back().serial != serial) { // (level_editor_adjust.h)
                note_for_adjust(xform::Mode::kShiftBy, selection_pos() - undo_steps().pending_.where, true);
            }
        }
        fly_camera().key_moving_ = false;
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
    if ((turn != 0) && !drag_session().dragging_ && picked_node_index() >= 0 &&
        chains::is_ladder(picked_set().node_name_)) {
        run_chain_op(chains::ChainOp::kTurn, static_cast<uint32_t>(turn)); // (a ladder: its facing, whole)
    } else if ((turn != 0) && !drag_session().dragging_ &&
               !(picked_node_index() >= 0 &&
                 node_edit::is_chain(picked_set().node_name_))) { // (chain points don't turn)
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
    if ((scaling != 0) && !drag_session().dragging_ && !picked_set().field_editing_ && !ctrl) {
        const float step = shift ? scale_tool::kFineStep : scale_tool::kStep;
        const float factor = scaling > 0 ? step : 1.0F / step;
        if (alt) {
            const TickCycle view = fly_camera().frame();
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
    if ((input.wheel != 0) && drag_session().dragging_) {
        const V3 pos = selection_pos();
        drag_session().drag_push_ +=
            static_cast<float>(input.wheel) *
            std::max(kMinWheelStep,
                     std::sqrt(get_dot(pos - fly_camera().pos_, pos - fly_camera().pos_)) *
                         kWheelStepFraction);
    } else if ((input.wheel != 0) && fly_camera().free_camera_) {
        fly_camera().pos_ = get_fly_to(fly_camera().pos_,
                                       fly_camera().pos_ - frame.back * static_cast<float>(kWheelStep * boost * input.wheel));
    } else if (input.wheel != 0) {
        fly_camera().orbit_distance_ = static_cast<float>(
            std::max(static_cast<double>(kMinOrbit),
                     std::min(static_cast<double>(kMaxOrbit),
                              fly_camera().orbit_distance_ * std::pow(kWheelZoomStep, input.wheel))));
    }
    // The camera at that distance from it (held still while the mouse drags it or a trigger's face).
    if (!drag_session().dragging_ && !triggers::resizing() && !fly_camera().free_camera_) {
        fly_camera().pos_ = target_of() + frame.back * fly_camera().orbit_distance_;
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
    fly_camera().key_moving_ = false;
    if (!stored_browser().open && !ctrl && (xform::fly_always() || input.right)) {
        const FlyInput fly = {.forward = (actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                         (actions::get_held(keys, actions::kBack) ? 1 : 0),
                              .right = (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                       (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                              .up = (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
        // ([editor] fly_accel_ms: the speed reached over that time - level_editor_camera.h)
        const float ramp = view_cam::fly_factor((fly.forward != 0) || (fly.right != 0) || (fly.up != 0), seconds);
        fly_camera().pos_ =
            get_fly_to(fly_camera().pos_, get_fly(fly_camera().pos_, frame, fly,
                                                  static_cast<float>(fly_camera().speed_ * boost * seconds * ramp)));
    }
    if (input.wheel != 0) {
        auto step = static_cast<float>(kWheelStep * boost * input.wheel);
        fly_camera().pos_ = get_fly_to(fly_camera().pos_, fly_camera().pos_ - frame.back * step);
    }
    const int speed = (actions::squeezed(actions::kFaster) ? 1 : 0) - (actions::squeezed(actions::kSlower) ? 1 : 0);
    if (speed != 0) {
        fly_camera().speed_ = static_cast<float>(
            std::max(kMinFlySpeedDouble,
                     std::min(kMaxFlySpeedDouble, fly_camera().speed_ * std::pow(kSpeedStepFactor, speed))));
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
    const bool active = ux8::get_following() && have_selection() && !drag_session().dragging_ && !triggers::resizing() &&
                        key == g_follow_key;
    g_follow_key = key;
    const V3 delta =
        nav::follow_step(&ux8::get_follower(), active, active ? target_of() : V3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    if (active && fly_camera().free_camera_) {
        fly_camera().pos_ = fly_camera().pos_ + delta;
    }
}

} // namespace
namespace internal {

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
        if (actions::squeezed(actions::kDrop) && !drag_session().dragging_ && !organize::selection_locked(true)) {
            drop_selection();
        }
        wheel_with_selection(editor_frame);
    } else if (!selection_held) {
        fly_without_selection(editor_frame);
    }
    follow_selection();
    if (stored_browser().open && selection_held && !drag_session().dragging_ && !fly_camera().free_camera_) {
        fly_camera().pos_ = target_of() + frame.back * fly_camera().orbit_distance_;
    }
}

} // namespace internal
} // namespace frontend::level_editor
