// NOT ORIGINAL - the level editor's commands: duplicate, erase, reset, hide / show, lock, focus, go to, save and
// resume (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
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
#include "editor/level_editor_adjust.h"
#include "base/core/format.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/maths/trig.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr float kPasteGap = 20.0F;     // a pasted piece goes beside the original, this far apart
constexpr float kRepeatOffset = 64.0F; // a duplicate with no repeat step goes this far to the right

constexpr std::size_t kMessageTextSize = 128;

constexpr float kRailLookDistance = 500.0F; // how far from a rail point the camera stops
constexpr float kLookDistance = 400.0F;
constexpr int kResetConfirmMilliseconds = 5000; // Reset level must be asked again within this time
constexpr float kMinLookDistance = 300.0F;
constexpr float kLookMargin = 1.5F; // the camera stops this many sizes away
constexpr float kMinBoxLookDistance = 200.0F;
constexpr float kMinFrameRadius = 50.0F;

// Ctrl+D: the selection copied, the copies set beside it (as far across the view as it is wide) and selected.
// `repeat` (Ctrl+Shift+D - level_editor_transform.h): the copies go where the last copies' move and turn put them from
// their source (each copy turned as much), else beside it. One undo step either way.
bool duplicate_node(const TickCycle& frame, const xform::RepeatStep* repeat);

} // namespace
namespace internal {

// (the last Duplicate's sources and copies, in pairs - level_editor_links.h's linked copies)
std::vector<std::pair<uint32_t, uint32_t>>& duplicate_pairs()
{
    static core::NoDestroy<std::vector<std::pair<uint32_t, uint32_t>>> duplicate_pairs;
    return duplicate_pairs.get();
}

} // namespace internal
namespace {

// The selected objects copied (that can be: a copy's copy made from the same node) beside them, or where a repeat
// says, and selected; noted in `step`. False when none could be.
bool duplicate_objects(const std::vector<objects::GameEntity*>& objects, V3 right, const xform::RepeatStep* repeat,
                       UndoStep& step)
{
    std::vector<objects::GameEntity*> made;
    std::vector<uint32_t> sources;
    for (objects::GameEntity* object : objects) {
        if (!object_edit::can_copy(object->ident()) && !object_edit::is_copy(object->ident())) {
            continue;
        }
        uint32_t source = object->ident();
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == object->ident()) {
                source = copy.source; // (a copy's copy: made from the same node)
            }
        }
        OrientedBox box{};
        const float width =
            object_edit::model_box(object, &box) ? std::max(box.max.x - box.min.x, box.max.z - box.min.z) : 50.0F;
        const V3 at = repeat ? xform::repeat_target(object_edit::pos_of(object), *repeat)
                             : object_edit::pos_of(object) + right * (width + 20.0F);
        objects::GameEntity* copy = object_edit::make_copy(source, at);
        if (!copy) {
            continue;
        }
        object_edit::State state = object_edit::store_away(object);
        state.id = copy->ident();
        state.pos = copy->at_point();
        object_edit::get_restore(state);
        made.emplace_back(copy);
        sources.push_back(source);
        duplicate_pairs().emplace_back(object->ident(), copy->ident());
    }
    // (repeated: the copies turned as the last ones were, about the first copy - then their places noted)
    if (repeat && repeat->turn != 0.0F && !made.empty()) {
        const V3 pivot = object_edit::pos_of(made.front());
        for (objects::GameEntity* copy : made) {
            object_edit::turn_about(copy, pivot, repeat->turn);
        }
    }
    for (size_t i = 0; i < made.size(); ++i) {
        step.copies.push_back({.name = core::at(made, i)->ident(),
                               .source = core::at(sources, i),
                               .state = object_edit::store_away(core::at(made, i))});
    }
    if (made.empty()) {
        hud::get_message("This object type can't be copied");
        return false;
    }
    select_objects(made);
    return true;
}

// The selected scenery copied beside it (by its width across the view), or where a repeat says, and selected;
// noted in `step`. False when none could be.
bool duplicate_scenery(V3 right, const xform::RepeatStep* repeat, UndoStep& step)
{
    OrientedBox box{};
    V3 min = {};
    V3 max = {};
    bool have = false;
    for (const uint32_t checksum : scenery::get_selection()) {
        V3 point_a{};
        V3 point_b{};
        if (scenery::cuboid(checksum, &box)) {
            bounds_of(box, &point_a, &point_b);
            grow_box(&min, &max, have, point_a, point_b);
            have = true;
        }
    }
    std::vector<uint32_t> copies;
    for (const uint32_t checksum : scenery::get_selection()) {
        if (const uint32_t copy = scenery::get_duplicate(checksum)) {
            copies.push_back(copy);
            duplicate_pairs().emplace_back(checksum, copy);
        }
    }
    if (copies.empty()) {
        hud::get_message("Couldn't copy");
        return false;
    }
    scenery::set_selection(copies);
    const float width = std::fabs((max.x - min.x) * right.x) + std::fabs((max.z - min.z) * right.z);
    if (repeat) {
        scenery::move_to(xform::repeat_target(scenery::get_pivot(), *repeat));
        if (repeat->quarters != 0) {
            scenery::get_turn(repeat->quarters);
        }
    } else {
        scenery::move_to(scenery::get_pivot() + right * (width + kPasteGap));
    }
    scenery::finalise();
    step.sectors = copies;
    return true;
}

} // namespace
namespace internal {

void get_duplicate(const TickCycle& frame, const xform::RepeatStep* repeat)
{
    duplicate_pairs().clear();
    const edit::Selected adjust_source = edit::get_selection(); // (level_editor_adjust.h: what it copied)
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    const V3 right = make_unit({.x = frame.right.x, .y = 0.0F, .z = frame.right.z});
    const std::vector<objects::GameEntity*> objects = selected_objects();
    const V3 source_pos = selection_pos();
    const float source_heading = selection_heading();
    if (picked_node_index() >= 0) {
        duplicate_node(frame, repeat);
        return;
    }
    if (!objects.empty()) {
        if (!duplicate_objects(objects, right, repeat, step)) {
            return;
        }
    } else if (scenery::has_selection()) {
        if (!duplicate_scenery(right, repeat, step)) {
            return;
        }
    } else {
        if (wall_only() != 0U) {
            hud::get_message("Invisible walls can't be copied");
        }
        return;
    }
    push_undo(step, repeat ? "Repeat duplicate" : "Duplicate");
    std::vector<uint32_t> made;
    made.reserve(step.copies.size());
    for (const MadeCopy& copy : step.copies) {
        made.push_back(copy.name);
    }
    for (const uint32_t sector : step.sectors) {
        made.push_back(sector);
    }
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
                action.quarters ? static_cast<float>(repeat->quarters) * kQuarterTurnDegrees : repeat->turn * kDegreesPerHalfTurn / maths::kPi;
        } else {
            action.offset = selection_pos() - source_pos;
        }
        adjust::note_duplicate(adjust_source, action);
    }
    note_action("duplicated (%s): %s", repeat ? "Ctrl+Shift+D" : "Ctrl+D", describe_selection().c_str());
}

} // namespace internal
namespace {

// A node the editor added (a spawn, a team spawn, a crown, a light, a sound emitter, a proximity node) copied with its
// settings and a fresh name, beside it or - repeated - where the last copy's move and turn say; one undo step. A level's
// own nodes, chain points, flag sets (named by the game) and trigger volumes (their own module's) aren't copied yet.
bool duplicate_node(const TickCycle& frame, const xform::RepeatStep* repeat)
{
    node_edit::Values values;
    if (!node_values(&values)) {
        return false;
    }
    node_create::LinkChain list = node_create::at_hand();
    int at = -1;
    for (int i = 0; std::cmp_less(i, list.size()); ++i) {
        if (node_create::name_hash(core::at(list, i).name) == picked_set().node_name_) {
            at = i;
        }
    }
    node_create::Kind kind{};
    if (at < 0 || core::at(list, at).owned || (core::at(list, at).like != 0U) || !core::at(list, at).links.empty() ||
        !node_create::kind_of(core::at(list, at), &kind) || kind == node_create::Kind::kFlagSet) {
        hud::get_message("Only added nodes can be duplicated");
        return false;
    }
    if (const char* why = node_create::get_why_not(kind, node_create::Team::kRed)) {
        hud::get_message(why, hud::kMessageSeconds);
        return false;
    }
    node_create::Spec spec = core::at(list, at);
    const V3 right = make_unit({.x = frame.right.x, .y = 0.0F, .z = frame.right.z});
    spec.name = node_create::fresh_name(node_create::name_stem(kind), node_create::name_taken);
    spec.pos = repeat ? xform::repeat_target(values.pos, *repeat) : values.pos + right * kRepeatOffset;
    spec.angles = values.angles;
    if (repeat && repeat->turn != 0.0F) {
        spec.angles = node_edit::with_heading(spec.angles, node_edit::get_heading_degrees(spec.angles) +
                                                               (repeat->turn * kDegreesPerHalfTurn / maths::kPi));
    }
    std::string message;
    if (!node_create::fabricate(spec, -1, &message)) {
        hud::get_message(message, hud::kMessageSeconds);
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kAdded;
    step.exist = false;
    step.added.push_back(spec);
    step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), spec.name));
    push_undo(step);
    pick_cycle().pickables_.clear();
    picked_set().hover_node_ = -1;
    const int index = nodes::get_find(node_create::name_hash(spec.name));
    if (index >= 0) {
        select_node_only(index);
    }
    xform::note_duplicate(&xform::last_duplicate(), xform::Kind::kElem, {node_create::name_hash(spec.name)}, values.pos,
                          values.angles.y);
    hud::get_message("Duplicated as " + spec.name);
    note_action("duplicated node %s", spec.name.c_str());
    return true;
}

} // namespace
namespace internal {

// Ctrl+Shift+D: the last Ctrl+D (or repeat) done again from its copies, still selected - as far again and turned as
// much (level_editor_transform.h).
void repeat_duplicate(const TickCycle& frame)
{
    xform::Kind kind = xform::Kind::kNone;
    std::vector<uint32_t> ids;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (picked_node_index() >= 0) {
        kind = xform::Kind::kElem;
        ids.push_back(picked_set().node_name_);
    } else if (!objects.empty()) {
        kind = xform::Kind::kObject;
        for (const objects::GameEntity* object : objects) {
            ids.push_back(object->ident());
        }
    } else if (scenery::has_selection()) {
        kind = xform::Kind::kScenery;
        ids = scenery::get_selection();
    }
    xform::RepeatStep repeat;
    if (!xform::repeat_for(xform::last_duplicate(), kind, ids, selection_pos(), selection_heading(), &repeat)) {
        hud::get_message("Nothing to repeat: duplicate first (Ctrl+D)", hud::kShortMessageSeconds);
        return;
    }
    get_duplicate(frame, &repeat);
}

// An invisible wall's collision switched off / on (level_editor_walls.h: the game's WorldCell::change_live) - Delete, Delete
// again, the Inspector's Delete wall / Restore; one undo step.
void switch_wall(uint32_t wall, bool off)
{
    if (walls::disengaged(wall) == off) {
        return;
    }
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
    if (get_primary() || sectors.empty()) {
        return;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kWalls;
    step.walls = walls::store_away(sectors);
    step.sectors = sectors;
    int changed = 0;
    for (const uint32_t checksum : sectors) {
        if (walls::disengaged(checksum) != off && walls::set_off(checksum, off)) {
            ++changed;
        }
    }
    if (changed == 0) {
        return;
    }
    push_undo(step, "Collision on / off");
    std::array<char, kStatusTextSize> text{};
    text::format_to(text, "Collision %s (%d piece%s)", off ? "off" : "on", changed, changed == 1 ? "" : "s");
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
            if (!object_edit::is_copy(object->ident())) {
                continue;
            }
            uint32_t source = 0;
            for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
                if (copy.name == object->ident()) {
                    source = copy.source;
                }
            }
            step.copies.push_back({.name = object->ident(), .source = source, .state = object_edit::store_away(object)});
        }
        if (step.copies.empty()) {
            hud::get_message("Only object copies can be deleted");
            return;
        }
        for (const MadeCopy& copy : step.copies) {
            object_edit::remove_copy(copy.name);
        }
        picked_set().objects_.clear();
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
    if (faces::has_selection() && faces::home_selection()) {
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    node_edit::Values node;
    if (show_hidden_node()) {
        // (a hidden node: shown first - Home again puts it back where the level has it)
    } else if (node_values(&node)) {
        // (a rail point: with the points that move with it - its whole rail while Whole rail is on)
        const UndoStep before = get_snapshot();
        bool any = false;
        for (const uint32_t name : node_edit::moved_with(picked_set().node_name_, pick_cycle().move_entire_rail_)) {
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
        if (chains::is_ladder(picked_set().node_name_)) {
            run_chain_op(chains::ChainOp::kFacingHome, 0); // (a level ladder turned: its facing too - its own undo step)
        }
        if (chains::view_of(picked_set().node_name_).speed > 0.0F) {
            run_chain_op(chains::ChainOp::kSpeed, 0); // (a rail point's speed-up taken off: its own undo step)
        }
        if (triggers::proxim_of(picked_set().node_name_)) {
            triggers::set_proxim(triggers::ProximEdit{.node = picked_set().node_name_}, true); // (a proximity node's scripts / shape)
        }
    } else if (!objects.empty()) {
        const UndoStep before = get_snapshot();
        bool any = false;
        for (const objects::GameEntity* object : objects) {
            if (const object_edit::State* origin = object_edit::get_origin(object->ident())) {
                any |= object_edit::get_restore(*origin);
            }
        }
        if (any) {
            push_undo(before, "Home");
            hud::get_message("Reverted");
        }
    } else if (const uint32_t wall = objects.empty() ? wall_only() : 0) {
        // (an invisible wall: where the level has it, its collision on)
        Placement placement;
        if (walls::get_moved(wall, &placement) || walls::disengaged(wall)) {
            push_undo(get_snapshot(), "Home");
            walls::apply_states({{.checksum = wall,
                                  .placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}},
                                  .off = false}});
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
    if (!have_selection()) {
        return;
    }
    fly_camera().free_camera_ = false; // (the camera follows the selection again)
    const V3 target = target_of();
    const V3 back = fly_camera().pos_ - target;
    const float distance = std::sqrt(get_dot(back, back));
    if (distance < 1.0F) {
        return;
    }
    fly_camera().orbit_distance_ = std::max(kMinOrbit, std::min(kMaxOrbit, distance));
    yaw_pitch_from_back(make_unit(back), {.yaw = &fly_camera().yaw_, .pitch = &fly_camera().pitch_});
}

// The view options (level_editor_view.h). H / the Inspector's Hide: the selection hidden while editing (from the view
// and from clicks) and deselected; I or / (Isolate): everything else hidden, the selection kept. Never saved.
void view_hide(bool isolate)
{
    std::vector<uint32_t> sectors;
    std::vector<uint32_t> objects;
    for (const picking::Lock& piece : selection_locks()) {
        (piece.kind == picking::kObjects ? objects : sectors).push_back(piece.id);
    }
    if (sectors.empty() && objects.empty()) {
        hud::get_message(isolate ? "Nothing selected to isolate" : "Nothing selected to hide");
        return;
    }
    const int count = isolate ? view::get_isolate(sectors, objects) : view::conceal(sectors, objects);
    if (!isolate) {
        unpick();
    }
    note_action("view: %s (%d hidden)", isolate ? "isolated" : "hid", count);
    std::array<char, kMessageTextSize> text{};
    text::format_to(text, "%s %d in editor", isolate ? "Isolated: hid" : "Hid", count);
    hud::get_message(text.data());
}

// Alt+H / Show all.
void view_show_all()
{
    const int count = view::hidden_count();
    view::show_all();
    note_action("view: showed all (%d)", count);
    hud::get_message((count != 0) ? "All shown" : "Nothing hidden");
}

// C / the toolbar's Collision.
void show_collision(bool on)
{
    view::set_show_collision(on);
    if (!on) {
        view::clear_wall();
    }
    if (on && !ui::underway()) {
        hud::get_message("Requires the editor UI (editor_ui=1)");
    } else {
        hud::get_message(on ? "Collision shown" : "Collision hidden");
    }
}

// L / the Inspector's Lock: the selection locked and deselected (every click passes it from now - the Inspector lists
// it to unlock); a selection all locked already (picked in the Outliner) unlocked; nothing selected: every lock dropped.
void toggle_lock()
{
    const std::vector<picking::Lock> locks = selection_locks();
    if (locks.empty() && !picked_set().node_selected_ && !picking::get_locks().empty()) {
        // (nothing selected: every lock dropped - the old HUD has no Inspector to unlock one)
        std::array<char, kPickTextSize> text{};
        text::format_to(text, "Unlocked %d", static_cast<int>(picking::get_locks().size()));
        push_locks_undo(); // (one undo step - unsaved with it)
        picking::clear_locks();
        hud::get_message(text.data());
        return;
    }
    if (locks.empty()) {
        hud::get_message(picked_set().node_selected_ ? "Nodes can't be locked" : "Nothing selected");
        return;
    }
    bool all = true;
    for (const picking::Lock& lock : locks) {
        all = all && picking::latched(lock.kind, lock.id);
    }
    push_locks_undo(); // (one undo step - unsaved with it)
    for (const picking::Lock& lock : locks) {
        picking::set_locked(lock.kind, lock.id, !all);
    }
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
    const TickCycle frame =
        frame_from_yaw_pitch(fly_camera().yaw_, std::min(fly_camera().pitch_, -0.35F));
    fly_camera().pos_ = target + frame.back * distance;
    yaw_pitch_from_back(make_unit(fly_camera().pos_ - target), {.yaw = &fly_camera().yaw_, .pitch = &fly_camera().pitch_});
}

// --- the nodes (level_editor_nodes.h) ---

// The node the Inspector shows: the one selected by itself, else the selected object's or scenery's own node; -1 if
// none.
int inspected_node()
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (picked_set().node_selected_) {
        if (std::cmp_less(picked_set().node_index_, nodes.size()) &&
            core::at(nodes, picked_set().node_index_).name == picked_set().node_name_) {
            return picked_set().node_index_;
        }
        const int found = (picked_set().node_name_ != 0U) ? nodes::get_find(picked_set().node_name_) : -1;
        if (found < 0) {
            picked_set().node_selected_ = false; // (another level)
            return -1;
        }
        picked_set().node_index_ = found;
        return found;
    }
    if (const objects::GameEntity* object = get_primary()) {
        return nodes::get_find(object->ident());
    }
    if (scenery::has_selection()) {
        return nodes::get_find(scenery::get_selection().front());
    }
    return -1;
}

// A node picked (a gizmo or the Outliner): an object's node selects the object, a movable scenery piece's the piece,
// anything else the node itself.
void select_node(int index)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()) || drag_session().dragging_) {
        return;
    }
    const nodes::Elem& node = core::at(nodes, index);
    // (a flag is moved as its node - its object with it - so the place the game makes it at changes too)
    if (node.family == nodes::Family::kObject) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(node.name));
        OrientedBox box{};
        if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) &&
            object_edit::model_box(object, &box)) {
            picked_set().node_selected_ = false;
            select_objects({object});
            note_action("selected %s (its node)", nodes::caption(node.name).c_str());
            return;
        }
    }
    if (node.family == nodes::Family::kScenery && !scenery::why_not_movable(node.name)) {
        picked_set().node_selected_ = false;
        picked_set().objects_.clear();
        scenery::set_selection({node.name});
        note_action("selected %s (its node)", nodes::caption(node.name).c_str());
        return;
    }
    select_node_only(index);
    note_action("selected the node %s", nodes::caption(node.name).c_str());
    if (debug_enabled()) {
        get_log("level editor: node %s (%s) at (%.0f, %.0f, %.0f)", nodes::caption(node.name).c_str(),
                nodes::family_name(node.family), node.pos.x, node.pos.y, node.pos.z);
    }
}

// The flag node of a flag's object (-1: not a flag's).
int flag_node_of(objects::GameEntity* object)
{
    const int index = object ? nodes::get_find(object->ident()) : -1;
    return index >= 0 && core::at(nodes::get_all(), index).family == nodes::Family::kBitMark ? index : -1;
}

// The camera taken to a node (its object or scenery: as go_to_selection).
void go_to_node(int index)
{
    select_node(index);
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()) || !core::at(nodes, index).has_pos_) {
        return;
    }
    if (!have_selection()) {
        get_look_at(core::at(nodes, index).pos, core::at(nodes, index).family == nodes::Family::kRail ? kRailLookDistance : kLookDistance);
    }
}

// G: down onto the ground below it (the collision ray from just under the scenery's bottom - not to hit itself - or
// from above an object's feet).
void drop_selection()
{
    if (!have_selection() || drag_session().dragging_) {
        return;
    }
    const V3 pos = selection_pos();
    const float base = get_selection_base();
    V3 ground{};
    const V3 from = ground_ray_start(pos, base, (get_primary() != nullptr) || picked_node_index() >= 0);
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
        unsaved_changes().unsaved_ = false;
        unsaved_changes().offer_pending_ = false; // (the autosave is gone with the save)
    }
    hud::get_message(message, hud::kShortMessageSeconds);
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
            undo_steps().undo_.clear();
            undo_steps().redo_.clear();
            mark_unsaved();
        }
        note_action("restored the autosave (Ctrl+R)");
    }
    unsaved_changes().offer_pending_ = false;
    hud::get_message(message, hud::kShortMessageSeconds);
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
    pick_cycle().confirm_reset_ = false;
    if (!layout::reset_level({.before = &step.layout, .message = &message})) {
        hud::get_message(message, hud::kShortMessageSeconds);
        return;
    }
    undo_steps().undo_.clear();
    undo_steps().redo_.clear();
    push_undo(step, "Reset level");
    unsaved_changes().unsaved_ = false; // (nothing to save: the level as shipped, no layout)
    unsaved_changes().offer_pending_ = false;
    note_action("reset the level");
    hud::get_message(message, hud::kLongMessageSeconds);
}

// Ctrl+Shift+Delete / the toolbar: the UI asks in a window; the old HUD asks for the keys again within 5 seconds.
void ask_reset_level()
{
    if (ui::underway()) {
        pick_cycle().confirm_reset_ = true;
        return;
    }
    const uint32_t now = SDL_GetTicks();
    if ((pick_cycle().reset_asked_at_ != 0U) && now - pick_cycle().reset_asked_at_ < kResetConfirmMilliseconds) {
        pick_cycle().reset_asked_at_ = 0;
        reset_level_now();
        return;
    }
    pick_cycle().reset_asked_at_ = now;
    hud::get_message("Reset level? Press Ctrl+Shift+Delete again to confirm", hud::kMessageSeconds);
}

void toggle_help()
{
    tool_options().help_ = !tool_options().help_;
    stored_browser().open = false;
    if (tool_options().help_) {
        show_help();
    } else {
        hud::hide_panel();
    }
}

void toggle_browser()
{
    if (drag_session().dragging_) {
        return;
    }
    stored_browser().open = !stored_browser().open;
    tool_options().help_ = false;
    if (stored_browser().open) {
        fill_browser();
    } else {
        hud::hide_panel();
    }
}

// F in the browser: the camera to its entry.
void focus_browser_item()
{
    if (std::cmp_greater_equal(stored_browser().index, stored_browser().items.size())) {
        return;
    }
    const uint32_t id = core::at(stored_browser().items, stored_browser().index).id;
    OrientedBox box{};
    if (stored_browser().tab == 0 && scenery::cuboid(id, &box)) {
        V3 point_a{};
        V3 point_b{};
        bounds_of(box, &point_a, &point_b);
        get_look_at((point_a + point_b) * kHalf,
                    std::max(kMinLookDistance, std::sqrt(get_dot(point_b - point_a, point_b - point_a)) * kLookMargin));
    } else if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id))) {
        get_look_at(object_edit::current_centre(object), kMinLookDistance);
    }
}

// The camera to the selection, from the side it looks from now, far enough to see it.
void go_to_selection()
{
    if (!have_selection()) {
        return;
    }
    float distance = kMinLookDistance;
    OrientedBox box{};
    if (objects::GameEntity* object = get_primary()) {
        if (object_edit::model_box(object, &box)) {
            distance = std::max(kMinBoxLookDistance, std::sqrt(get_dot(box.max - box.min, box.max - box.min)) * kLookMargin);
        }
    } else if (!scenery::get_selection().empty() && scenery::cuboid(scenery::get_selection().front(), &box)) {
        V3 point_a{};
        V3 point_b{};
        bounds_of(box, &point_a, &point_b);
        distance = std::max(kMinLookDistance, std::sqrt(get_dot(point_b - point_a, point_b - point_a)) * kLookMargin);
    }
    get_look_at(target_of(), std::min(distance, kMaxOrbit));
    focus_on_selection();
}

} // namespace internal
namespace {

// --- the camera tools (level_editor_camera.h) and play from here (level_editor_play.h) ---

// The selection's size: a sphere round its box (an object's model, the scenery's first piece; 100 otherwise).
float selection_radius()
{
    OrientedBox box{};
    if (objects::GameEntity* object = get_primary()) {
        if (object_edit::model_box(object, &box)) {
            const V3 size = box_size(box);
            return std::max(kMinFrameRadius, kHalf * std::sqrt(get_dot(size, size)));
        }
    } else if (!scenery::get_selection().empty()) {
        V3 lo = {};
        V3 hi = {};
        bool have = false;
        for (const uint32_t sector : scenery::get_selection()) {
            if (!scenery::cuboid(sector, &box)) {
                continue;
            }
            V3 point_a{};
            V3 point_b{};
            bounds_of(box, &point_a, &point_b);
            grow_box(&lo, &hi, have, point_a, point_b);
            have = true;
        }
        if (have) {
            return std::max(kMinFrameRadius, kHalf * std::sqrt(get_dot(hi - lo, hi - lo)));
        }
    }
    return 100.0F;
}

void focus_now() { focus_on_selection(); }
void deselect_now() { unpick(); }

} // namespace
namespace internal {

view_cam::Rig rig_now()
{
    view_cam::Rig rig = {};
    rig.pos = &fly_camera().pos_;
    rig.yaw = &fly_camera().yaw_;
    rig.pitch = &fly_camera().pitch_;
    rig.orbit_distance_ = &fly_camera().orbit_distance_;
    rig.speed = &fly_camera().speed_;
    rig.have_selection_ = have_selection();
    rig.target = rig.have_selection_ ? target_of() : fly_camera().pos_;
    rig.radius = rig.have_selection_ ? selection_radius() : 100.0F;
    rig.dragging = drag_session().dragging_ || fly_camera().key_moving_ || triggers::resizing();
    rig.lens = drawn_view().lens_;
    rig.cast = &get_cast;
    rig.focus = &focus_now;
    rig.deselect = &deselect_now;
    return rig;
}

perform::Resume resume_now()
{
    perform::Resume resume;
    resume.pos = fly_camera().pos_;
    resume.yaw = fly_camera().yaw_;
    resume.pitch = fly_camera().pitch_;
    resume.speed = fly_camera().speed_;
    for (const Picked& picked : picked_set().objects_) {
        resume.objects.push_back(picked.id);
    }
    resume.sectors = scenery::get_selection();
    if (picked_set().node_selected_ && picked_set().objects_.empty() && resume.sectors.empty()) {
        resume.node = picked_set().node_name_;
    }
    return resume;
}

void apply_resume(const perform::Resume& resume)
{
    fly_camera().pos_ = resume.pos;
    fly_camera().yaw_ = resume.yaw;
    fly_camera().pitch_ = resume.pitch;
    fly_camera().speed_ = resume.speed;
    std::vector<objects::GameEntity*> objects;
    for (const uint32_t id : resume.objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if ((object != nullptr) && (get_live({.object = object, .id = id}) != nullptr)) {
            objects.push_back(object);
        }
    }
    if (!objects.empty()) {
        select_objects(objects);
    } else if (!resume.sectors.empty()) {
        scenery::set_selection(resume.sectors);
    } else if (resume.node != 0U) {
        const std::vector<nodes::Elem>& nodes = nodes::get_all();
        for (int i = 0; std::cmp_less(i, nodes.size()); ++i) {
            if (core::at(nodes, i).name == resume.node) {
                select_node_only(i);
                break;
            }
        }
    }
    // (the camera where it was: the selection orbited from there)
    if (have_selection()) {
        const V3 back = fly_camera().pos_ - target_of();
        fly_camera().orbit_distance_ = std::max(kMinOrbit, std::min(kMaxOrbit, std::sqrt(get_dot(back, back))));
    }
    get_log("level editor: back from play from here - the camera at (%.0f, %.0f, %.0f), %s", fly_camera().pos_.x, fly_camera().pos_.y, fly_camera().pos_.z,
            describe_selection().c_str());
}

} // namespace internal
} // namespace frontend::level_editor
