// NOT ORIGINAL - the level editor's panel requests: what the UI asks for (set a field, pick a member, place, chain
// operations) and how each is carried out (ours). See level_editor.h.

#include "editor/level_editor_transform.h"
#include "editor/level_editor_rotate.h"
#include "base/maths/trig.h"
#include "editor/level_editor.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <utility>
#include "editor/level_editor_fields.h"
#include "editor/level_editor_level_props.h"
#include "base/core/format.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_preview.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_view.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace internal {

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
        drawn_view().pickables_.clear();
        picked_set().hover_node_ = -1;
    }
    if (select != 0U) {
        const int index = nodes::get_find(select);
        if (index >= 0) {
            select_node_only(index);
        }
    } else if (picked_set().node_selected_ && nodes::get_find(picked_set().node_name_) < 0) {
        picked_set().node_selected_ = false;
    }
    if (!message.empty()) {
        hud::get_message(message, hud::kMessageSeconds);
    }
    note_action("chains: %s", message.c_str());
}

// What the chain operations work on: the node selected by itself, else the selected object's node (a ped's / car's
// path); 0: none.
uint32_t chain_target()
{
    if (picked_set().node_selected_ && (picked_set().node_name_ != 0U)) {
        return picked_set().node_name_;
    }
    if (const objects::GameEntity* object = get_primary()) {
        return nodes::get_find(object->ident()) >= 0 ? object->ident() : 0;
    }
    return 0;
}

// An Inspector / key operation on the selected node's chain.
void run_chain_op(chains::ChainOp op, uint32_t arg)
{
    const uint32_t target = chain_target();
    if (target == 0U) {
        return;
    }
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    chains::apply_op(op, target, arg, &message, &select);
    if (select == target && !picked_set().node_selected_) {
        select = 0; // (a ped / car stays selected as the object)
    }
    after_chain_change(before, select, message);
}

} // namespace internal
namespace {

// --- the full Inspector and the level properties (phase 6: level_editor_fields.h, level_editor_level_props.h) ---

// A node's links= typed in the Inspector (`home`: back as the level has them): the chains' `links` record, as relinking
// a chain does (one undo step, live).
void set_links_from_inspector(uint32_t node, const std::string& text, bool home)
{
    const chains::State before = chains::at_hand();
    chains::State state = before;
    auto it = std::ranges::find_if(state.links, [&](const chains::LinkEdit& link) { return link.name == node; });
    if (home) {
        if (it != state.links.end()) {
            state.links.erase(it);
        }
    } else {
        fields::Value value;
        std::string error;
        if (!fields::parse(fields::Kind::kNames, text, &value, &error) ||
            !fields::valid_links(
                value.list, node, [](uint32_t node_name) { return nodes::get_find(node_name) >= 0; },
                fields::get_name_of, &error)) {
            hud::get_message("Links: " + error, hud::kMessageSeconds);
            return;
        }
        if (it != state.links.end()) {
            it->links = value.list;
        } else {
            state.links.push_back({.name = node, .links = value.list});
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

} // namespace
namespace internal {

// The Inspector's field requests: set, removed, put back.
void field_request(const ui::Request& request)
{
    uint32_t node = request.node;
    if ((node == 0U) || nodes::get_find(node) < 0) {
        return;
    }
    if (request.id == fields::key_links() && request.kind != ui::Request::Kind::kFieldRemove) {
        set_links_from_inspector(node, request.text, request.kind == ui::Request::Kind::kFieldHome);
        return;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kFields;
    std::string message;
    if (request.kind == ui::Request::Kind::kFieldHome) {
        const std::vector<uint32_t> keys =
            (request.id != 0U) ? std::vector<uint32_t>{request.id} : fields::touched_keys(node);
        for (const uint32_t key : keys) {
            fields::Own before;
            fields::Own origin;
            if (fields::get_capture(node, key, &before) && fields::get_origin(node, key, &origin)) {
                step.fields.push_back({.node = node, .key = key, .own = before});
                fields::place_in(node, key, origin);
            }
        }
        // (and the links, when every field goes back)
        if (request.id == 0U) {
            chains::State chains = chains::at_hand();
            const bool relinked =
                std::ranges::any_of(chains.links, [&](const chains::LinkEdit& link) { return link.name == node; });
            if (relinked) {
                set_links_from_inspector(node, "", true);
            }
        }
        message = (request.id != 0U) ? "Field reverted" : "All fields reverted";
    } else {
        fields::Own before;
        fields::get_capture(node, request.id, &before);
        fields::Value value;
        std::string error;
        const bool removed = request.kind == ui::Request::Kind::kFieldRemove;
        if (!removed && !fields::parse(static_cast<fields::Kind>(request.value), request.text, &value, &error)) {
            hud::get_message("Not set: " + error, hud::kMessageSeconds);
            return;
        }
        if (!request.label.empty()) {
            fields::remember(request.label);
        }
        if (!fields::assign_value(node, request.id, value, removed, &error)) {
            hud::get_message("Not set: " + error, hud::kMessageSeconds);
            return;
        }
        step.fields.push_back({.node = node, .key = request.id, .own = before});
        const char* note = nullptr;
        const nodes::Elem& elem = core::at(nodes::get_all(), nodes::get_find(node));
        const fields::When when = fields::when_applied(elem.family, request.id, &note);
        message = std::string(removed ? "Field removed" : "Field set") +
                  (when == fields::When::kLive ? "" : " (applies on reload)");
    }
    if (!step.fields.empty()) {
        push_undo(step, message.c_str());
        hud::get_message(message, hud::kShortMessageSeconds);
        note_action("inspector: %s - %s.%s", message.c_str(), object_edit::caption(node).c_str(),
                    fields::get_name_text(request.id, fields::get_name_of).c_str());
    }
}

} // namespace internal
namespace {

// NOT ORIGINAL grouping - the properties before an edit and whether one is going on (were globals). Made on first use,
// never destroyed (props_edit()).
struct PropsEdit {
    // The level properties' panel: each change shown at once (the time of day) and kept; one undo step a field.
    level_props::Props props_before_{};
    bool props_editing_{};
};

PropsEdit& props_edit()
{
    static core::NoDestroy<PropsEdit> state;
    return state.get();
}

void level_props_request(const ui::Request& request)
{
    if (request.kind == ui::Request::Kind::kLevelProps) {
        if (!props_edit().props_editing_) {
            props_edit().props_before_ = level_props::at_hand();
            props_edit().props_editing_ = true;
        }
        level_props::assign_value(request.props);
        return;
    }
    if (!props_edit().props_editing_) {
        return;
    }
    props_edit().props_editing_ = false;
    if (!level_props::get_same(props_edit().props_before_, level_props::at_hand())) {
        UndoStep step;
        step.kind = UndoStep::Kind::kProps;
        step.props = props_edit().props_before_;
        push_undo(step, "Level properties");
        note_action("level properties changed");
    }
}

// The frame's view for the UI's requests: where the mouse ray points, and whether a change may start now.
struct RequestFrame {
    V3 dir{};
    bool have_ray_{};
    const TickCycle* frame{};
    bool no_change_{};
};

void browser_tab_request(const ui::Request& request)
{
    if (stored_browser().open && stored_browser().tab != request.value) {
        stored_browser().tab = (request.value != 0) ? 1 : 0;
        stored_browser().index = stored_browser().top = 0;
        fill_browser();
    }
}

// The Inspector's list of the scenery selection's parts: x / Keep only this.
void member_request(const ui::Request& request)
{
    if (drag_session().dragging_ || !scenery::has_selection()) {
        return;
    }
    if (request.value != 0) {
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
        if (request.kind == ui::Request::Kind::kMemberRemove) {
            scenery::flip_over(request.id);
        } else {
            scenery::keep_only(request.id);
        }
    }
    note_action("Inspector - selected: %s", describe_selection().c_str());
}

void select_scenery_request(const ui::Request& request)
{
    if (!drag_session().dragging_ && !scenery::why_not_movable(request.id)) {
        picked_set().objects_.clear();
        scenery::set_selection({request.id});
        go_to_selection();
        note_action("selected in the Outliner: %s", describe_selection().c_str());
    }
}

void unlock_request(const ui::Request& request, bool no_change)
{
    if (!no_change && picking::latched(static_cast<uint32_t>(request.value), request.id)) {
        push_locks_undo(); // (one undo step - unsaved with it)
        picking::set_locked(static_cast<uint32_t>(request.value), request.id, false);
        hud::get_message("Unlocked");
    }
}

// The Inspector's fields (typed, or dragged to scrub): one undo step until they're let go.
void node_set_request(const ui::Request& request)
{
    if (picked_node_index() >= 0 && !drag_session().dragging_ && !fly_camera().key_moving_) {
        if (!picked_set().field_editing_) {
            begin_change();
            picked_set().field_editing_ = true;
        }
        node_edit::Values values;
        values.pos = {.x = core::at(request.a, 0), .y = core::at(request.a, 1), .z = core::at(request.a, 2)};
        values.angles = {.x = core::at(request.b, 0), .y = core::at(request.b, 1), .z = core::at(request.b, 2)};
        set_selected_node(values);
        note_action("set in the Inspector: %s", describe_selection().c_str());
    }
}

// The Inspector's Scale X / Y / Z (level_editor_scale.h): scenery or objects scaled to them (along their own axes)
// while dragged or typed; one undo step until let go.
void scale_set_request(const ui::Request& request)
{
    if ((get_primary() || scenery::has_selection()) && picked_node_index() < 0 && !drag_session().dragging_ &&
        !fly_camera().key_moving_) {
        if (!picked_set().field_editing_) {
            begin_change();
            picked_set().field_editing_ = true;
            scale_at_field_start() = selection_scale_now();
        }
        scale_selection_to({.x = core::at(request.a, 0), .y = core::at(request.a, 1), .z = core::at(request.a, 2)});
        if (!get_primary()) {
            follow_scenery();
        }
    }
}

void scale_done_request()
{
    if (!picked_set().field_editing_) {
        return;
    }
    picked_set().field_editing_ = false;
    if (!get_primary()) {
        scenery::finalise();
    }
    const V3 now = selection_scale_now();
    if (undo_steps().has_pending_ &&
        (now.x != scale_at_field_start().x || now.y != scale_at_field_start().y || now.z != scale_at_field_start().z)) {
        push_undo(undo_steps().pending_, "Scale (Inspector)");
        note_action("scaled in the Inspector: %s", describe_selection().c_str());
    }
    undo_steps().has_pending_ = false;
}

// The Inspector's Rotation (level_editor_rotate.h): objects' rows made from the three angles outright (nothing drifts),
// scenery turned about its pivot to the heading asked for.
void rotation_set_request(const ui::Request& request)
{
    if (!(get_primary() || scenery::has_selection()) || picked_node_index() >= 0 || drag_session().dragging_ ||
        fly_camera().key_moving_) {
        return;
    }
    if (!picked_set().field_editing_) {
        begin_change();
        picked_set().field_editing_ = true;
    }
    const V3 degrees = {.x = core::at(request.a, 0), .y = core::at(request.a, 1), .z = core::at(request.a, 2)};
    if (get_primary()) {
        std::array<V3, 3> rows{};
        rotate_tool::rows_of(degrees, rows);
        for (objects::GameEntity* object : selected_objects()) {
            object_edit::set_rows(object, rows);
        }
        return;
    }
    const float now = scenery::selection_heading();
    const float wanted = degrees.y * maths::kPi / kDegreesPerHalfTurn;
    scenery::turn_by(xform::get_wrap(wanted - now));
    follow_scenery();
}

void rotation_done_request()
{
    if (!picked_set().field_editing_) {
        return;
    }
    picked_set().field_editing_ = false;
    if (!get_primary()) {
        scenery::finalise();
    }
    if (undo_steps().has_pending_) {
        push_undo(undo_steps().pending_, "Rotate (Inspector)");
        note_action("turned in the Inspector: %s", describe_selection().c_str());
    }
    undo_steps().has_pending_ = false;
}

void place_toggle_request()
{
    place_palette().place_open_ = !place_palette().place_open_;
    if (place_palette().place_open_) {
        entities::refresh_palette();
    }
    if (chain_kind_of(place_palette().place_armed_) != chains::Blit::kNone) {
        stop_chain_drawing(nullptr);
    }
    place_palette().place_armed_ = -1;
}

void place_arm_request(const ui::Request& request)
{
    if (chain_kind_of(place_palette().place_armed_) != chains::Blit::kNone) {
        stop_chain_drawing(nullptr);
    }
    place_palette().place_armed_ = request.value;
    if (chain_kind_of(place_palette().place_armed_) != chains::Blit::kNone) {
        // (a rail / a path: drawn point by point - each click a point, Enter finishes)
        chains::start_drawing(chain_kind_of(place_palette().place_armed_));
        hud::get_message(chains::get_waiting(), hud::kLongMessageSeconds);
    }
}

void chain_op_request(const ui::Request& request, bool no_change)
{
    if (!no_change) {
        const auto op = static_cast<chains::ChainOp>(request.value);
        run_chain_op(op, request.id);
        if (op == chains::ChainOp::kExtend && chains::get_drawing() != chains::Blit::kNone) {
            place_palette().place_armed_ = -1; // (drawing on from the selected end: the palette's entry isn't armed)
            unpick();                       // (the camera flies while drawing)
        }
    }
}

void place_here_request(const ui::Request& request, const RequestFrame& request_frame)
{
    if (chain_kind_of(request.value) != chains::Blit::kNone) {
        hud::get_message("Click points in the view");
        return;
    }
    if (!request_frame.no_change_) {
        // (where the view looks: the panel has the mouse)
        const TickCycle& frame = *request_frame.frame;
        place_node(request.value, cursor_point(frame.back * -1.0F, false, frame));
        place_palette().place_armed_ = -1;
    }
}

void hide_confirmed_request(bool no_change)
{
    const uint32_t name = confirmations().confirm_hide_;
    confirmations().confirm_hide_ = 0;
    if (!no_change && (name != 0U) && name == picked_set().node_name_) {
        unlink_list_node(true);
    }
}

void select_object_request(const ui::Request& request)
{
    if (!drag_session().dragging_) {
        if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(request.id))) {
            select_objects({object});
            go_to_selection();
            note_action("selected in the Outliner: %s", describe_selection().c_str());
        }
    }
}

// One request: the history, the browser, the selection and the view.
void apply_request(const ui::Request& request, const RequestFrame& request_frame)
{
    const bool no_change = request_frame.no_change_;
    switch (request.kind) {
    case ui::Request::Kind::kUndo:
        if (!no_change) {
            step_history(&undo_steps().undo_, &undo_steps().redo_, "Undone");
        }
        break;
    case ui::Request::Kind::kRedo:
        if (!no_change) {
            step_history(&undo_steps().redo_, &undo_steps().undo_, "Redone");
        }
        break;
    case ui::Request::Kind::kSave:
        if (!no_change) {
            save_layout();
        }
        break;
    case ui::Request::Kind::kRestoreAutosave:
    case ui::Request::Kind::kDiscardAutosave:
        if (!no_change) {
            answer_autosave(request.kind == ui::Request::Kind::kDiscardAutosave);
        }
        break;
    case ui::Request::Kind::kToggleHelp:
        toggle_help();
        break;
    case ui::Request::Kind::kToggleBrowser:
        toggle_browser();
        break;
    case ui::Request::Kind::kBrowserTab:
        browser_tab_request(request);
        break;
    case ui::Request::Kind::kBrowserIndex:
        if (stored_browser().open && request.value >= 0 &&
            std::cmp_less(request.value, stored_browser().items.size())) {
            stored_browser().index = request.value;
        }
        break;
    case ui::Request::Kind::kBrowserPlace:
        if (stored_browser().open && !drag_session().dragging_) {
            place_from_browser(cursor_point(request_frame.dir, request_frame.have_ray_, *request_frame.frame));
        }
        break;
    case ui::Request::Kind::kBrowserFocus:
        if (stored_browser().open) {
            focus_browser_item();
        }
        break;
    case ui::Request::Kind::kDrop:
        drop_selection();
        break;
    case ui::Request::Kind::kDeselect:
        if (!drag_session().dragging_) {
            unpick();
        }
        break;
    case ui::Request::Kind::kSnapAlways:
        tool_options().snap_always_ = request.value != 0;
        break;
    case ui::Request::Kind::kMemberRemove:
    case ui::Request::Kind::kMemberKeep:
        member_request(request);
        break;
    case ui::Request::Kind::kSelectScenery:
        select_scenery_request(request);
        break;
    case ui::Request::Kind::kFamilies:
        tool_options().families_ = request.id & nodes::kAllFamilies;
        config_set_int("editor", "families", static_cast<int>(tool_options().families_));
        break;
    case ui::Request::Kind::kPickFilter:
        tool_options().pick_filter_ = request.id & picking::kAll;
        config_set_int("editor", "pick", static_cast<int>(tool_options().pick_filter_));
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
        if (const uint32_t wall = wall_only(); (wall != 0U) && !no_change) {
            switch_wall(wall, request.value == 0);
        }
        break;
    case ui::Request::Kind::kSceneryCollision:
        if (!no_change) {
            switch_scenery_collision(request.value == 0);
        }
        break;
    case ui::Request::Kind::kUnlock:
        unlock_request(request, no_change);
        break;
    case ui::Request::Kind::kCarry:
        carry::set_enabled(request.value != 0);
        hud::get_message((request.value != 0) ? "Carry Objects on" : "Carry Objects off");
        break;
    case ui::Request::Kind::kSelectNode:
        select_node(request.value);
        if (picked_node_index() >= 0) {
            focus_on_selection(); // (the camera turns to it, at the distance it is - and orbits it)
        }
        break;
    case ui::Request::Kind::kNodeSet:
        node_set_request(request);
        break;
    case ui::Request::Kind::kNodeSetDone:
        if (picked_set().field_editing_) {
            picked_set().field_editing_ = false;
            end_change();
        }
        break;
    case ui::Request::Kind::kScaleSet:
        scale_set_request(request);
        break;
    case ui::Request::Kind::kScaleDone:
        scale_done_request();
        break;
    case ui::Request::Kind::kRotationSet:
        rotation_set_request(request);
        break;
    case ui::Request::Kind::kRotationDone:
        rotation_done_request();
        break;
    case ui::Request::Kind::kReset:
        if (!no_change) {
            get_reset();
        }
        break;
    case ui::Request::Kind::kOnlineWarningSeen:
        picked_set().online_warning_shown_ = false;
        break;
    case ui::Request::Kind::kGoToNode:
        go_to_node(request.value);
        if (have_selection()) {
            go_to_selection();
        }
        break;
    case ui::Request::Kind::kPlaceToggle:
        place_toggle_request();
        break;
    case ui::Request::Kind::kPlaceArm:
        place_arm_request(request);
        break;
    case ui::Request::Kind::kChainOp:
        chain_op_request(request, no_change);
        break;
    case ui::Request::Kind::kPlaceHere:
        place_here_request(request, request_frame);
        break;
    case ui::Request::Kind::kRemoveNodeAction:
        if (!no_change) {
            unlink_list_node(false);
        }
        break;
    case ui::Request::Kind::kHideConfirmed:
        hide_confirmed_request(no_change);
        break;
    case ui::Request::Kind::kHideCancelled:
        confirmations().confirm_hide_ = 0;
        break;
    case ui::Request::Kind::kResetLevel:
        if (!no_change) {
            ask_reset_level();
        }
        break;
    case ui::Request::Kind::kResetConfirmed:
        if (!no_change && confirmations().confirm_reset_) {
            reset_level_now();
        }
        break;
    case ui::Request::Kind::kResetCancelled:
        confirmations().confirm_reset_ = false;
        break;
    case ui::Request::Kind::kReqEntireRail:
        tool_options().move_entire_rail_ = request.value != 0;
        break;
    case ui::Request::Kind::kFieldSet:
    case ui::Request::Kind::kFieldRemove:
    case ui::Request::Kind::kFieldHome:
        if (!no_change) {
            field_request(request);
        }
        break;
    case ui::Request::Kind::kLevelProps:
    case ui::Request::Kind::kLevelPropsDone:
        if (!drag_session().dragging_ && !fly_camera().key_moving_) {
            level_props_request(request);
        }
        break;
    case ui::Request::Kind::kCrownMove:
        if (!no_change) {
            move_selected_crown(request.value);
        }
        break;
    case ui::Request::Kind::kBrowserPreview:
        stored_browser().preview = request.value;
        break;
    case ui::Request::Kind::kPreviewOrbit:
        preview::intake({.x = core::at(request.a, 0), .y = core::at(request.a, 1)}, core::at(request.a, 2));
        break;
    case ui::Request::Kind::kSelectObject:
        select_object_request(request);
        break;
    }
}

} // namespace
namespace internal {

// What was asked for on the UI's panels (level_editor_ui.h), done as the keys would.
void apply_ui_requests(V3 dir, bool have_ray, const TickCycle& frame)
{
    const bool no_change = drag_session().dragging_ || fly_camera().key_moving_ || picked_set().field_editing_ ||
                           triggers::resizing();
    const RequestFrame request_frame{.dir = dir, .have_ray_ = have_ray, .frame = &frame, .no_change_ = no_change};
    for (const ui::Request& request : ui::take_requests()) {
        apply_request(request, request_frame);
    }
}

} // namespace internal
} // namespace frontend::level_editor
