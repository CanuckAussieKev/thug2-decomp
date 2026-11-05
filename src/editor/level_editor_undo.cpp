// NOT ORIGINAL - the level editor's undo and redo: the steps, what each restores, and the history the panels show
// (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
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
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "base/maths/vec4.h"
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

UndoSteps& undo_steps()
{
    static core::NoDestroy<UndoSteps> state;
    return state.get();
}

} // namespace internal
namespace {

constexpr size_t kMaxUndo = 64;

} // namespace
namespace internal {

// The selection as it is now (before a change; the objects noted as changed, for layouts and Home).
UndoStep get_snapshot()
{
    UndoStep step;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (picked_node_index() >= 0) {
        step.kind = UndoStep::Kind::kNodes;
        // (a rail point: the points that move with it too)
        const std::vector<uint32_t> names = node_edit::moved_with(drag_state().node_name_, pick_cycle().move_entire_rail_);
        for (const uint32_t name : names) {
            node_edit::note_origin(name);
        }
        step.nodes = node_edit::get_capture(names, node_edit::get_take_in);
    } else if (const uint32_t wall = objects.empty() ? wall_only() : 0) {
        step.kind = UndoStep::Kind::kWalls;
        step.wall = wall;
        step.walls = walls::store_away({wall});
    } else if (!objects.empty()) {
        step.kind = UndoStep::Kind::kObjects;
        for (objects::GameEntity* object : objects) {
            object_edit::note_origin(object);
            object_edit::touch(object->ident());
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
        if (index >= 0) {
            note_online_edit(index);
        }
    });
}

} // namespace internal
namespace {

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

} // namespace
namespace internal {

void push_undo(UndoStep step, const char* label)
{
    step.label = label && (*label != '\0') ? label : kind_label(step.kind);
    step.detail = describe_selection();
    step.serial = ++undo_steps().undo_serial_;
    undo_steps().undo_.push_back(std::move(step));
    if (undo_steps().undo_.size() > kMaxUndo) {
        undo_steps().undo_.erase(undo_steps().undo_.begin());
    }
    undo_steps().redo_.clear();
    mark_unsaved();
    ++unsaved_changes().moves_;
}

// The locks as they are now, as an undo step (before L / Unlock / Unlock all change them).
void push_locks_undo(const char* label)
{
    UndoStep step;
    step.kind = UndoStep::Kind::kLocks;
    step.locks = picking::get_locks();
    step.where = selection_pos();
    push_undo(step, label);
}

// A change starting (a drag, the keys moving it) and ending: kept when the selection did move.
void begin_change()
{
    undo_steps().has_pending_ = have_selection();
    drag_state().pending_forced_ = false;
    if (undo_steps().has_pending_) {
        undo_steps().pending_ = get_snapshot();
    }
}
void end_change()
{
    if (!undo_steps().has_pending_) {
        return;
    }
    undo_steps().has_pending_ = false;
    const V3 now = selection_pos();
    const bool changed = undo_steps().pending_.kind == UndoStep::Kind::kNodes
                             ? node_edit::differs(undo_steps().pending_.nodes, node_edit::get_take_in) // (moved or turned)
                             : undo_steps().pending_.where.x != now.x || undo_steps().pending_.where.y != now.y || undo_steps().pending_.where.z != now.z;
    // (a turn or a scale by a drag - level_editor_transform.h: a step though the position may be where it was)
    if (changed || drag_state().pending_forced_) {
        push_undo(undo_steps().pending_, "Move");
    }
    drag_state().pending_forced_ = false;
}

} // namespace internal
namespace {

// Copies made or taken away: shown / made again, or hidden / deleted; selected when they're there.
void set_exist(UndoStep* step, bool exist)
{
    if (!step->sectors.empty()) {
        std::vector<scenery::SectorState> states;
        for (const uint32_t checksum : step->sectors) {
            Placement placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
            scenery::placement_now(checksum, &placement);
            states.push_back({.checksum = checksum, .placement = placement, .hidden = !exist});
        }
        scenery::apply_states(states, {});
        if (exist) {
            drag_state().objects_.clear();
            scenery::set_selection(step->sectors);
        } else {
            scenery::clear_selection();
        }
    }
    if (!step->copies.empty()) {
        std::vector<objects::GameEntity*> made;
        for (MadeCopy& copy : step->copies) {
            auto* object = core::down_cast<objects::GameEntity>(objects::find_object(copy.name));
            const bool there = (object != nullptr) && ((object->object_flags() & objects::kObjectFlagDead) == 0U);
            if (exist && !there) {
                const V3 pos = {.x = copy.state.pos.x, .y = copy.state.pos.y, .z = copy.state.pos.z};
                object = object_edit::make_copy(copy.source, pos, copy.name);
                if (object) {
                    object_edit::get_restore(copy.state);
                }
            } else if (!exist && there) {
                copy.state = object_edit::store_away(object);
                object_edit::remove_copy(copy.name);
                object = nullptr;
            }
            if (exist && object) {
                made.push_back(object);
            }
        }
        if (exist) {
            select_objects(made);
        } else {
            drag_state().objects_.clear();
        }
    }
}

// Added nodes put back: made again where they were in the overlay (lowest index first, the first selected), or
// taken out.
void restore_added(const UndoStep& step)
{
    std::string message;
    if (step.exist) {
        // (made again where they were in the overlay: lowest index first)
        std::vector<size_t> by_index(step.added.size());
        for (size_t i = 0; i < by_index.size(); ++i) {
            core::at(by_index, i) = i;
        }
        std::ranges::sort(
            by_index, [&](size_t a, size_t b) { return core::at(step.added_at_, a) < core::at(step.added_at_, b); });
        for (const size_t index : by_index) {
            node_create::fabricate(core::at(step.added, index), core::at(step.added_at_, index), &message);
        }
        const int index = step.added.empty() ? -1 : nodes::get_find(node_create::name_hash(step.added.front().name));
        if (index >= 0) {
            select_node_only(index);
        }
    } else {
        for (const node_create::Spec& spec : step.added) {
            node_create::Spec removed;
            int at = 0;
            node_create::drop_out(node_create::name_hash(spec.name), &removed, &at);
        }
        drag_state().node_selected_ = false;
    }
}

} // namespace
namespace internal {

// `step` put back; what it puts back is selected.
void get_restore(UndoStep& step)
{
    switch (step.kind) {
    case UndoStep::Kind::kObjects: {
        std::vector<objects::GameEntity*> objects;
        for (const object_edit::State& state : step.objects) {
            if (object_edit::get_restore(state)) {
                objects.push_back(core::down_cast<objects::GameEntity>(objects::find_object(state.id)));
            }
        }
        select_objects(objects);
        break;
    }
    case UndoStep::Kind::kScenery:
        drag_state().objects_.clear();
        scenery::restore_state(step.scenery);
        // (what it carried: back where it was - level_editor_carry.h)
        for (const object_edit::State& state : step.carried_objects_) {
            object_edit::get_restore(state);
        }
        node_edit::place_in(step.carried_nodes_, [](uint32_t name, const node_edit::Values& values) {
            node_edit::Values now;
            if (node_edit::get_take_in(name, &now) && !node_edit::get_same(now, values)) {
                node_edit::assign_value(name, values);
            }
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
        if (index >= 0) {
            select_node_only(index);
        }
        break;
    }
    case UndoStep::Kind::kAdded: {
        restore_added(step);
        break;
    }
    case UndoStep::Kind::kHidden: {
        for (const uint32_t name : step.hidden_names_) {
            node_create::conceal(name, step.hidden);
        }
        const int index = step.hidden_names_.empty() ? -1 : nodes::get_find(step.hidden_names_.front());
        if (index >= 0) {
            select_node_only(index);
        }
        break;
    }
    case UndoStep::Kind::kOrder:
        node_create::set_order(step.order);
        break;
    case UndoStep::Kind::kLevel: {
        std::string message;
        std::string before;
        drag_state().objects_.clear();
        scenery::clear_selection();
        drag_state().node_selected_ = false;
        if (step.reset) {
            layout::reset_level({.before = &before, .message = &message});
        } else {
            layout::put_back(step.layout, &message);
        }
        break;
    }
    case UndoStep::Kind::kWalls:
        walls::apply_states(step.walls);
        if (step.wall != 0U) {
            unpick();
            view::select_wall(step.wall);
        } else if (!step.sectors.empty()) {
            drag_state().objects_.clear();
            drag_state().node_selected_ = false;
            view::clear_wall();
            scenery::set_selection(step.sectors);
        }
        break;
    case UndoStep::Kind::kChains:
        drag_state().node_selected_ = drag_state().node_selected_ && nodes::get_find(drag_state().node_name_) >= 0;
        chains::change_phase(step.chains);
        if (drag_state().node_selected_ && nodes::get_find(drag_state().node_name_) < 0) {
            drag_state().node_selected_ = false;
        }
        break;

    case UndoStep::Kind::kFields: {
        for (const FieldUndo& field : step.fields) {
            fields::place_in(field.node, field.key, field.own);
        }
        const int index = step.fields.empty() ? -1 : nodes::get_find(step.fields.front().node);
        if (index >= 0 && drag_state().node_name_ != step.fields.front().node) {
            select_node_only(index);
        }
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
        if (step.module) {
            step.module->get_restore();
        }
        break;
    case UndoStep::Kind::kGroup:
        for (size_t i = step.group.size(); i-- > 0;) {
            get_restore(core::at(step.group, i));
        }
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
            if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(state.id))) {
                now.objects.push_back(object_edit::store_away(object));
            }
        }
        break;
    case UndoStep::Kind::kScenery: {
        scenery::set_selection(step.scenery.selection);
        now.scenery = scenery::save_state();
        now.carried_objects_.clear();
        for (const object_edit::State& state : step.carried_objects_) {
            if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(state.id))) {
                now.carried_objects_.push_back(object_edit::store_away(object));
            }
        }
        std::vector<uint32_t> names;
        names.reserve(step.carried_nodes_.nodes.size());
        for (const auto& node : step.carried_nodes_.nodes) {
            names.push_back(node.first);
        }
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
        for (const auto& node : step.nodes.nodes) {
            names.push_back(node.first);
        }
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
        if (!now.reset) {
            layout::capture_text(&now.layout);
        }
        break;
    case UndoStep::Kind::kChains:
        now.chains = chains::at_hand();
        break;
    case UndoStep::Kind::kFields:
        for (FieldUndo& field : now.fields) {
            fields::get_capture(field.node, field.key, &field.own);
        }
        break;
    case UndoStep::Kind::kProps:
        now.props = level_props::at_hand();
        break;
    case UndoStep::Kind::kWalls: {
        std::vector<uint32_t> checksums;
        checksums.reserve(step.walls.size());
        for (const walls::WallState& state : step.walls) {
            checksums.push_back(state.checksum);
        }
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
        for (size_t i = 0; i < step.group.size(); ++i) {
            core::at(now.group, i) = at_hand(core::at(step.group, i));
        }
        break;
    }
    return now;
}

// Undo (`from` the undo stack, what it replaced onto redo) or redo (the other way).
void step_history(std::vector<UndoStep>* from, std::vector<UndoStep>* to, const char* what)
{
    if (from->empty()) {
        hud::get_message(from == &undo_steps().undo_ ? "Nothing to undo" : "Nothing to redo");
        return;
    }
    entities::stop_preview(); // (the previewed object back where it stands before anything is put back)
    UndoStep step = from->back();
    from->pop_back();
    carry::disarm(); // (the steps put places back themselves: nothing follows them)
    to->push_back(at_hand(step));
    to->back().serial = ++undo_steps().undo_serial_;
    get_restore(step);
    mark_unsaved();
    note_action("%s", what);
    hud::get_message(what);
    if (debug_enabled()) {
        get_log("level editor: %s (%u to undo, %u to redo)", what, static_cast<unsigned>(undo_steps().undo_.size()),
                static_cast<unsigned>(undo_steps().redo_.size()));
    }
}

} // namespace internal

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
    out.reserve(undo_steps().undo_.size());
    for (const UndoStep& step : undo_steps().undo_) {
        out.push_back({.label = step.label, .detail = step.detail});
    }
    return out;
}

std::vector<EditorUndoRecord> redo_history()
{
    std::vector<EditorUndoRecord> out;
    for (auto it = undo_steps().redo_.rbegin(); it != undo_steps().redo_.rend(); ++it) {
        out.push_back({.label = it->label, .detail = it->detail});
    }
    return out;
}

bool change_under_way() { return drag_state().dragging_ || fly_camera().key_moving_ || drag_state().field_editing_ || triggers::resizing(); }

void jump_history(int steps)
{
    if (change_under_way() || !editor_session().active_) {
        return;
    }
    for (; steps < 0 && !undo_steps().undo_.empty(); ++steps) {
        step_history(&undo_steps().undo_, &undo_steps().redo_, "Undone");
    }
    for (; steps > 0 && !undo_steps().redo_.empty(); --steps) {
        step_history(&undo_steps().redo_, &undo_steps().undo_, "Redone");
    }
}

void push_locks_undo_labelled(const char* label) { push_locks_undo(label); }

bool apply_layout_step(const std::string& text, const char* label, std::string* message)
{
    if (!editor_session().active_ || change_under_way()) {
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
    if (!layout::put_back(text, message)) {
        return false;
    }
    push_undo(step, label);
    note_action("%s (the layout put in place again)", label);
    return true;
}

namespace edit {

size_t deepness() { return undo_steps().undo_.size(); }

void fold(size_t depth)
{
    depth = std::min(depth, undo_steps().undo_.size());
    if (undo_steps().undo_.size() - depth < 2) {
        return;
    }
    UndoStep group;
    group.kind = UndoStep::Kind::kGroup;
    const std::string label = undo_steps().undo_.back().label;
    group.group.assign(undo_steps().undo_.begin() + static_cast<long>(depth), undo_steps().undo_.end());
    undo_steps().undo_.erase(undo_steps().undo_.begin() + static_cast<long>(depth), undo_steps().undo_.end());
    group.where = selection_pos();
    push_undo(group, label.c_str());
}

bool take_back()
{
    if (undo_steps().undo_.empty()) {
        return false;
    }
    entities::stop_preview();
    UndoStep step = undo_steps().undo_.back();
    undo_steps().undo_.pop_back();
    carry::disarm();
    get_restore(step);
    mark_unsaved();
    return true;
}

void push_made(MadeThings made)
{
    const auto objects = made.objects;
    const auto sectors = made.sectors;
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    for (const uint32_t id : objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (!object) {
            continue;
        }
        uint32_t source = 0;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == id) {
                source = copy.source;
            }
        }
        step.copies.push_back({.name = id, .source = source, .state = object_edit::store_away(object)});
    }
    step.sectors.assign(sectors.begin(), sectors.end());
    if (step.copies.empty() && step.sectors.empty()) {
        return;
    }
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
        if (!object || !object_edit::is_copy(id)) {
            continue;
        }
        uint32_t source = 0;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == id) {
                source = copy.source;
            }
        }
        step.copies.push_back({.name = id, .source = source, .state = object_edit::store_away(object)});
    }
    if (step.copies.empty()) {
        return;
    }
    for (const MadeCopy& copy : step.copies) {
        object_edit::remove_copy(copy.name);
    }
    drag_state().objects_.clear();
    push_undo(step, "Remove copies");
}

} // namespace edit
} // namespace frontend::level_editor
