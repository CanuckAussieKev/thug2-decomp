// NOT ORIGINAL - the level editor's pointer: what is under the cursor, the drag, and the clicks that select and
// place (ours). See level_editor.h.

#include "editor/level_editor.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>
#include "editor/level_editor_actions.h"
#include "editor/level_editor_arrange.h"
#include "base/core/format.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_ruler.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_view.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "platform/sdl_time.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr double kResizeHintSeconds = 3.0;

constexpr int kDebugLogFrames = 60;         // the debug log reports every this many frames
constexpr float kDragStartPixels = 4.0F;    // a press that moved this far is a drag
constexpr float kHandlePickPixels = 8.0F;   // handles are picked within this many pixels at the reference view height

} // namespace
namespace internal {

// ([features] debug_editor: once a second, what the mouse is seen as)
void log_mouse_debug(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    static int g_debug_frames = 0;
    if (debug_enabled() && ++g_debug_frames % kDebugLogFrames == 0) {
        get_log("level editor: mouse (%.0f, %.0f) in %dx%d, ray %d (%.3f, %.3f, %.3f), arrow %d", input.x, input.y,
                input.window_width_, input.window_height_, have_ray ? 1 : 0, dir.x, dir.y, dir.z, drag_session().hover_axis_);
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
    if (alt && !ctrl && !shift && (drag_session().dragging_ || fly_camera().key_moving_) && carry::total_of() > 0) {
        carry::get_leave();
        hud::get_message("Not carrying objects");
    }
    // Shift with the collision shown: the invisible wall a Shift+click would take, highlighted (level_editor_view.h).
    view::set_hover_wall(!drag_session().dragging_ && have_ray && shift && !alt && view::show_collision() && ui::underway()
                             ? view::pick_wall(fly_camera().pos_, make_unit(dir))
                             : 0);
    faces::get_hover(fly_camera().pos_, make_unit(dir), have_ray && !drag_session().dragging_ && drag_session().hover_axis_ < 0); // (map3: Faces mode's hover)
    if (ruler::get_armed() && have_ray && !drag_session().dragging_) {
        ruler::get_hover(fly_camera().pos_, make_unit(dir), back_x, back_y, alt); // (the ruler's next end - level_editor_ruler.h)
    }
    ux9::get_hover(fly_camera().pos_, make_unit(dir), back_x, back_y, have_ray && !drag_session().dragging_); // (UX9: the tools' next pick)
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
    if (drag_session().dragging_ && have_selection() && have_ray) {
        if (!drag_session().drag_moved_ && (transform_session().modal_ || std::fabs(input.x - drag_session().press_x_) +
                                                                         std::fabs(input.y - drag_session().press_y_) >=
                                                                     kDragStartPixels)) {
            drag_session().drag_moved_ = true;
        }
        // (an axis typed - X / Y / Z, level_editor_transform.h: a move starts again from where it began, along it)
        if (transform_session().typed_.axis_changed_) {
            transform_session().typed_.axis_changed_ = false;
            if (transform_session().drag_mode_ == xform::Mode::kShiftBy) {
                place_selection(drag_session().drag_start_);
                drag_session().axis_ = transform_session().typed_.axis;
                start_drag(fly_camera().pos_, dir, false, frame, input.x, input.y);
                drag_session().drag_moved_ = true;
            }
        }
        // (Shift: up / down - not with Ctrl or Alt, which pick what it snaps to)
        const bool vertical = shift && !ctrl && !alt;
        if (transform_session().drag_mode_ == xform::Mode::kShiftBy && drag_session().axis_ < 0 &&
            vertical != drag_session().drag_vertical_) {
            start_drag(fly_camera().pos_, dir, vertical, frame, input.x, input.y);
        }
        if (drag_session().drag_moved_ || transform_session().typed_apply_ || !transform_session().typed_.text.empty()) {
            get_drag(fly_camera().pos_, dir, frame, input.x, input.y, snapping_now(ctrl, shift, alt));
        }
    }
    // Enter (typed values) or - Alt+G's modal transform - a click: applied.
    if (drag_session().dragging_ && (transform_session().typed_apply_ || (transform_session().modal_ && (input.left_presses_ != 0)))) {
        end_drag();
        focus_on_selection();
        input.left_presses_ = 0;
        input.left_releases_ = 0;
    }
    transform_session().typed_apply_ = false;
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
    if (!drag_session().dragging_) {
        drag_session().hover_axis_ = have_ray && have_selection() ? arrow_at(frame, {.x = back_x, .y = back_y}) : -1;
        // (a selected trigger volume's resize handle - after the arrows: level_editor_triggers.h)
        if (!triggers::resizing()) {
            drag_session().hover_handle_ = have_ray && drag_session().hover_axis_ < 0 && ui::underway()
                                 ? triggers::handle_at({.x = back_x, .y = back_y}, kHandlePickPixels * pixel_scale())
                                 : -1;
        }
        triggers::set_hover_handle(drag_session().hover_handle_);
        objects::GameEntity* hover = nullptr;
        uint32_t hover_sector = 0;
        // What a click would take (level_editor_picking.h): the cycle's next entry while a click-to-cycle holds; else
        // a node's gizmo under the cursor (what the last frame drew), then the nearer of an object and scenery -
        // structure and see-through pieces passed unless Alt (or a middle click) - as the pick filter allows.
        picked_set().hover_node_ = -1;
        picked_set().hover_passed_ = 0;
        // (UX8: a drill-through - Alt+wheel - holds with Alt still down: its entry is the hover)
        const bool drilled = stored_drilled() && cycle_holds(input.x, input.y, false, false);
        stored_drilled() = drilled;
        click_cycle().hover_cycle_ =
            drilled || cycle_holds(input.x, input.y, shift, alt || (input.middle_presses_ != 0))
                ? (click_cycle().cycle_.index + 1) % static_cast<int>(click_cycle().cycle_.order.size())
                : -1;
        if (have_ray && drag_session().hover_axis_ < 0 && click_cycle().hover_cycle_ >= 0) {
            const picking::TableRow& next = core::at(click_cycle().cycle_.order, click_cycle().hover_cycle_);
            if (next.kind == picking::Choice::kNode) {
                picked_set().hover_node_ = static_cast<int>(next.id);
            } else if (next.kind == picking::Choice::kObject) {
                hover = get_live({.object = core::down_cast<objects::GameEntity>(objects::find_object(next.id)), .id = next.id});
            } else {
                hover_sector = next.id;
            }
        } else if (have_ray && drag_session().hover_axis_ < 0) {
            const bool everything = alt || (input.middle_presses_ != 0);
            picking::Candidates candidates;
            scenery::Hit hit;
            scenery::get_pick(fly_camera().pos_, make_unit(dir), kMaxPickDistance, &hit);
            // (a gizmo behind the first occluding surface under the cursor isn't taken by a plain click)
            candidates.node = ui::underway() ? nodes::pick_nearest(drawn_view().pickables_, {.x = back_x, .y = back_y},
                                                                   kHandlePickPixels * pixel_scale(),
                                                                   gizmo_occluder(frame, dir, hit, everything))
                                             : -1;
            float object_t = kMaxPickDistance;
            objects::GameEntity* object = object_edit::get_pick(fly_camera().pos_, dir, kMaxPickDistance, &object_t);
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
            switch (picking::get_choose(candidates, tool_options().pick_filter_, everything)) {
            case picking::Choice::kNode:
                picked_set().hover_node_ = candidates.node;
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
                if ((tool_options().pick_filter_ & picking::kScenery) != 0U) {
                    picked_set().hover_passed_ = hit.passed;
                }
                break;
            }
        }
        if (debug_enabled() && hover_sector != picked_set().hover_sector_ && (hover_sector != 0U)) {
            get_log("level editor: hovering %s", scenery::describe_hover(hover_sector).c_str());
        }
        picked_set().hover_ = hover ? Picked{.object = hover, .id = hover->ident()} : Picked{};
        picked_set().hover_sector_ = hover_sector;
    }
}

} // namespace internal
namespace {

// A click on something: selected (Shift: added / removed; Alt or a middle click: one piece of scenery in / out)
// and dragged; on nothing, deselected.
// A click on scenery: one piece in / out (Alt, a middle click), Shift adding / dropping its group, else its group
// selected; whether it's dragged.
bool click_scenery(bool one, bool shift)
{
    bool drag = false;
    picked_set().objects_.clear();
    if (one) {
        scenery::flip_over(picked_set().hover_sector_);
    } else if (shift) {
        scenery::get_selected(picked_set().hover_sector_) ? scenery::flip_over(picked_set().hover_sector_)
                                                          : scenery::add_group(picked_set().hover_sector_);
    } else if (!scenery::get_selected(picked_set().hover_sector_)) {
        scenery::select_group(picked_set().hover_sector_);
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
    picked_set().node_selected_ = false;
    if (shift) {
        if (is_picked(hover)) {
            std::erase_if(picked_set().objects_, [&](const Picked& picked) { return picked.object == hover; });
        } else {
            picked_set().objects_.push_back({.object = hover, .id = hover->ident()});
        }
    } else {
        if (!is_picked(hover)) {
            select_objects({hover});
        } else if (get_primary() != hover) {
            // (the clicked one first: the camera and the arrows go by it)
            auto it = std::ranges::find_if(picked_set().objects_,
                                           [&](const Picked& picked) { return picked.object == hover; });
            std::rotate(picked_set().objects_.begin(), it, it + 1);
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
    objects::GameEntity* hover = get_live(picked_set().hover_);
    const bool one = alt || ((input.middle_presses_ != 0) && (input.left_presses_ == 0));
    bool drag = false;
    if (drag_session().hover_axis_ < 0) {
        note_click_for_cycle({.x = input.x, .y = input.y}, dir, (input.left_presses_ != 0) && !shift && !one);
    }
    // (with the collision shown, Shift+click on an invisible wall's face: that wall - level_editor_view.h)
    const uint32_t wall =
        (input.left_presses_ != 0) && shift && !alt && drag_session().hover_axis_ < 0 && view::show_collision()
            ? view::pick_wall(fly_camera().pos_, make_unit(dir))
            : 0;
    // (a plain click on the selected wall's face: dragging it - level_editor_walls.h)
    const bool on_wall = (wall == 0U) && (input.left_presses_ != 0) && !shift && !one && drag_session().hover_axis_ < 0 &&
                         (wall_only() != 0U) && view::show_collision() &&
                         view::pick_wall(fly_camera().pos_, make_unit(dir)) == wall_only();
    if ((wall == 0U) && !on_wall && drag_session().hover_axis_ < 0) {
        view::clear_wall();
    }
    // (with the trigger volumes shown, a plain click on one - nearer than the rest: its node; Alt passes them -
    // level_editor_triggers.h)
    int trigger_node = -1;
    uint32_t trigger_sector = 0;
    const bool trigger = (wall == 0U) && (input.left_presses_ != 0) && !alt && drag_session().hover_axis_ < 0 &&
                         picked_set().hover_node_ < 0 && ui::underway() &&
                         triggers::get_pick(fly_camera().pos_, make_unit(dir), &trigger_node, &trigger_sector,
                                            trigger_reach(dir));
    if (wall != 0U) {
        unpick();
        view::select_wall(wall);
        hud::get_message("Invisible wall " + object_edit::caption(wall));
    } else if (on_wall) {
        drag = true;
    } else if (drag_session().hover_axis_ >= 0 && (input.left_presses_ != 0)) {
        drag_session().axis_ = drag_session().hover_axis_;
        drag = true;
    } else if (picked_set().hover_node_ >= 0 && (input.left_presses_ != 0)) {
        select_node(picked_set().hover_node_);
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
    } else if (picked_set().hover_sector_ != 0U) {
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
        picked_set().node_selected_ = false;
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
        transform_session().drag_mode_ = drag_session().axis_ >= 0 ? gizmo_mode() : xform::Mode::kShiftBy;
        transform_session().modal_ = false;
        transform_session().typed_ = {};
        drag_session().dragging_ = true;
        drag_session().drag_start_ = selection_pos();
        drag_session().press_x_ = input.x;
        drag_session().press_y_ = input.y;
        drag_session().drag_moved_ = false;
        begin_change();
        start_drag(fly_camera().pos_, dir, shift && !ctrl && !alt && drag_session().axis_ < 0, frame, input.x,
                   input.y);
    }
}

// An armed Link / path assignment's click: the node under the cursor taken (none: still waiting).
void pick_for_chain()
{
    if (picked_set().hover_node_ >= 0) {
        const chains::State before = chains::at_hand();
        std::string message;
        uint32_t select = 0;
        chains::get_picked(core::at(nodes::get_all(), picked_set().hover_node_).name, &message, &select);
        after_chain_change(before, select, message);
    } else {
        hud::get_message(chains::get_waiting());
    }
}

} // namespace
namespace internal {

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
            triggers::rescale(fly_camera().pos_, dir, ctrl || tool_options().snap_always_);
        }
        if (!input.left) {
            triggers::end_resize();
            note_action("resized a trigger volume: %s", describe_selection().c_str());
        }
    }
    // Box select armed (Ctrl+B): the press starts a box wherever it is (nothing else takes it).
    if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && organize::box_armed()) {
        organize::box_press({.x = back_x, .y = back_y},
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
        input.left_presses_ = 0;
    }
    // A press where nothing is (no arrow, gizmo, object or scenery under the cursor): a box if it's dragged (Shift
    // adds, Ctrl removes - the click itself does what it did: deselects when plain).
    bool box_candidate = (input.left_presses_ != 0) && have_ray && !drag_session().dragging_ &&
                         drag_session().hover_axis_ < 0 && drag_session().hover_handle_ < 0 &&
                         picked_set().hover_node_ < 0 && (get_live(picked_set().hover_) == nullptr) &&
                         (picked_set().hover_sector_ == 0U) && !faces::get_picking() && !chains::wants_pick() &&
                         chains::get_drawing() == chains::Blit::kNone &&
                         !(place_palette().place_armed_ >= 0 && place_palette().place_open_) && !alt;
    // (UX8: a double click on nothing a click takes - a surface - with nothing selected (the first click deselects):
    // the camera glides there - level_editor_ux8.h)
    if (box_candidate && !ruler::get_armed() && !organize::box_armed() &&
        ux8::note_empty_click(platform::ticks_ms(), input.x, input.y) && !have_selection() && !picked_set().node_selected_) {
        teleport_to(dir);
        box_candidate = false;
    }
    const bool handle_pressed = (input.left_presses_ != 0) && have_ray && !drag_session().dragging_ &&
                                drag_session().hover_axis_ < 0 && drag_session().hover_handle_ >= 0 &&
                                !triggers::resizing() &&
                                triggers::begin_resize(drag_session().hover_handle_, fly_camera().pos_, dir);
    if (handle_pressed) {
        hud::get_message("Resize: drag the face (Ctrl: snap)", kResizeHintSeconds);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && drag_session().hover_axis_ < 0 &&
               ux9::occupied() && ux9::get_click(fly_camera().pos_, make_unit(dir), back_x, back_y)) {
        // (UX9: the gap wizard's / the ground tool's pick - level_editor_ux9.h)
        note_action("ux9 tool click");
    } else if (((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && arrange::edge_picking() &&
                arrange::edge_click({.x = back_x, .y = back_y})) ||
               ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && stored_browser().open &&
                drag_session().hover_axis_ < 0 && !ctrl && !alt && arrange::place_click(shift))) {
        // (F7: a rail along the edge clicked; or the Add palette open with Click places: its piece where the ghost
        // showed it - level_editor_arrange.h)
    } else if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && ruler::get_armed() &&
               drag_session().hover_axis_ < 0) {
        // (the ruler armed: a click puts an end - level_editor_ruler.h)
        hud::get_message(ruler::get_click(), hud::kLongMessageSeconds);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && faces::get_picking() &&
               drag_session().hover_axis_ < 0) {
        // (map3: Faces mode - the collision face clicked, Shift adds / drops it: level_editor_faces.h)
        faces::click_at(fly_camera().pos_, make_unit(dir), shift);
        note_action("faces mode click: %s", faces::describe().c_str());
    } else if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && chains::wants_pick() &&
               drag_session().hover_axis_ < 0) {
        // (an armed Link / path assignment: the node clicked - level_editor_chains.h)
        pick_for_chain();
    } else if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ &&
               chains::get_drawing() != chains::Blit::kNone && drag_session().hover_axis_ < 0) {
        draw_chain_point(dir, alt);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && place_palette().place_armed_ >= 0 &&
               place_palette().place_open_ && drag_session().hover_axis_ < 0) {
        place_node(place_palette().place_armed_, cursor_point(dir, have_ray, frame));
        if (!shift) {
            place_palette().place_armed_ = -1;
        }
    } else if ((input.left_presses_ != 0) && have_ray && !drag_session().dragging_ && ctrl && !alt && !shift &&
               drag_session().hover_axis_ < 0 && drop_under_cursor(dir)) {
        // (Ctrl+click: one part of the selection dropped - no drag)
        click_cycle().cycle_.have = false;
        note_action("ctrl+click - selected: %s", describe_selection().c_str());
        if (debug_enabled()) {
            get_log("level editor: ctrl+click - selected: %s", describe_selection().c_str());
        }
    } else if (((input.left_presses_ != 0) || (input.middle_presses_ != 0)) && have_ray && !drag_session().dragging_) {
        click_to_select(editor_frame);
    }
    if (box_candidate && !drag_session().dragging_) {
        organize::box_press({.x = back_x, .y = back_y},
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
    }
}

} // namespace internal
} // namespace frontend::level_editor
