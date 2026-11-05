// NOT ORIGINAL - the level editor's selection: what is picked, moving and turning it, and the click cycle under the
// cursor (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include "base/maths/vec2.h"
#include "base/core/format.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_shadows.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
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

constexpr double kSharedWarningSeconds = 8.0;

constexpr double kCountMessageSeconds = 2.5;

constexpr float kOccluderPixels = 8.0F;  // pixels at the reference view height
constexpr float kClickSlopPixels = 4.0F; // a click that moved less than this is the same spot

} // namespace
namespace internal {

// --- the selection ---

objects::GameEntity* get_live(const Picked& picked)
{
    if (!picked.object) {
        return nullptr;
    }
    if (objects::find_object(picked.id) != picked.object ||
        ((picked.object->object_flags() & objects::kObjectFlagDead) != 0U)) {
        return nullptr;
    }
    return picked.object;
}

// The selected objects still there (the others dropped).
std::vector<objects::GameEntity*> selected_objects()
{
    std::vector<objects::GameEntity*> live;
    std::vector<Picked> kept;
    for (const Picked& picked : drag_state().objects_) {
        if (objects::GameEntity* object = get_live(picked)) {
            live.emplace_back(object);
            kept.push_back(picked);
        }
    }
    drag_state().objects_ = kept;
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
    if (!drag_state().node_selected_) {
        return -1;
    }
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (drag_state().node_index_ < 0 || std::cmp_greater_equal(drag_state().node_index_, nodes.size()) ||
        core::at(nodes, drag_state().node_index_).name != drag_state().node_name_) {
        const int found = (drag_state().node_name_ != 0U) ? nodes::get_find(drag_state().node_name_) : -1;
        if (found < 0) {
            return -1;
        }
        drag_state().node_index_ = found;
    }
    return node_edit::get_movable(core::at(nodes, drag_state().node_index_)) ? drag_state().node_index_ : -1;
}
// That node's place now (false: none).
bool node_values(node_edit::Values* values)
{
    return picked_node_index() >= 0 && node_edit::get_take_in(drag_state().node_name_, values);
}
// An invisible wall selected by itself (Shift+click with the collision shown - level_editor_view.h): moved, turned and
// switched off / on as level_editor_walls.h does; 0 when none (or something else is selected).
uint32_t wall_only()
{
    const uint32_t wall = view::selected_wall();
    return (wall != 0U) && !drag_state().node_selected_ && !scenery::has_selection() && !get_primary() &&
                   walls::is_wall(wall)
               ? wall
               : 0;
}
bool have_selection()
{
    return (get_primary() != nullptr) || scenery::has_selection() || picked_node_index() >= 0 || (wall_only() != 0U);
}

// A spawn, crown or flag of a level shared with THUGPRO changed by the editor (moved, added, hidden, reordered): the
// warning, once a run (the edit is made - layouts aren't used in net games, but a layout baked into the level's files
// would be).
void note_online_change(nodes::Family family, const char* what, const char* verb)
{
    if (drag_state().online_warned_ || !nodes::get_online(family)) {
        return;
    }
    const char* game = "";
    if (!nodes::level_is_shared(&game)) {
        return;
    }
    drag_state().online_warned_ = true;
    drag_state().online_warning_shown_ = true;
    get_log("level editor: %s %s in a level shared with THUGPRO (%s) - warned", what, verb, game);
    hud::get_message("Shared level: spawn, crown and flag changes differ from THUGPRO players' copies", kSharedWarningSeconds);
}
void note_online_edit(int index)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size())) {
        return;
    }
    note_online_change(core::at(nodes, index).family, nodes::caption(core::at(nodes, index).name).c_str(), "moved");
}

// The selected node put at `values` (an edit of the editor's: the online warning first time). A rail point takes the
// rest of its rail with it, by as much, while Whole rail is on (L).
void set_selected_node(const node_edit::Values& values)
{
    const int index = picked_node_index();
    if (index < 0) {
        return;
    }
    note_online_edit(index);
    node_edit::Values now;
    if (node_edit::is_chain(drag_state().node_name_) && node_edit::get_take_in(drag_state().node_name_, &now)) {
        const V3 delta = values.pos - now.pos;
        for (const uint32_t name : node_edit::moved_with(drag_state().node_name_, pick_cycle().move_entire_rail_)) {
            node_edit::Values point;
            if (node_edit::get_take_in(name, &point)) {
                point.pos = point.pos + delta;
                node_edit::assign_value(name, point);
            }
        }
        return;
    }
    node_edit::assign_value(drag_state().node_name_, values);
}

// The node `index` selected by itself (nothing else).
void select_node_only(int index)
{
    drag_state().objects_.clear();
    scenery::clear_selection();
    drag_state().node_selected_ = true;
    drag_state().node_index_ = index;
    drag_state().node_name_ = core::at(nodes::get_all(), index).name;
}
bool is_picked(objects::GameEntity* object)
{
    return std::ranges::any_of(drag_state().objects_, [&](const Picked& picked) { return picked.object == object; });
}
void select_objects(const std::vector<objects::GameEntity*>& objects)
{
    drag_state().objects_.clear();
    for (objects::GameEntity* object : objects) {
        drag_state().objects_.push_back({.object = object, .id = object->ident()});
    }
    if (!objects.empty()) {
        scenery::clear_selection();
    }
}
void unpick()
{
    drag_state().free_camera_ = false; // (the next selection is framed again - level_editor_transform.h)
    drag_state().objects_.clear();
    scenery::clear_selection();
    drag_state().node_selected_ = false;
    view::clear_wall();
    faces::clear_selection(); // (map3: the collision faces selected - level_editor_faces.h)
}

// Where the selection is: the first object's position, else the scenery's pivot (the middle of its boxes); its base:
// that object's feet, the scenery's bottom.
// (a node selected by itself: its pos=, which is its base too)
V3 selection_pos()
{
    if (const objects::GameEntity* object = get_primary()) {
        return object_edit::pos_of(object);
    }
    node_edit::Values node;
    if (node_values(&node)) {
        return node.pos;
    }
    if (const uint32_t wall = wall_only()) {
        return walls::get_pivot(wall);
    }
    return scenery::get_pivot();
}
float get_selection_base()
{
    if (const objects::GameEntity* object = get_primary()) {
        return object->at_point().y;
    }
    node_edit::Values node;
    if (node_values(&node)) {
        return node.pos.y;
    }
    if (const uint32_t wall = wall_only()) {
        return walls::get_bottom(wall);
    }
    return scenery::get_bottom();
}
// The point the camera looks at: the first object's middle, a node's place, or the scenery's pivot.
V3 target_of()
{
    if (objects::GameEntity* object = get_primary()) {
        return object_edit::current_centre(object);
    }
    node_edit::Values node;
    if (node_values(&node)) {
        return node.pos;
    }
    if (const uint32_t wall = wall_only()) {
        return walls::get_pivot(wall);
    }
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
    for (objects::GameEntity* object : objects) {
        object_edit::get_place(object, object_edit::pos_of(object) + delta);
    }
}
// The middle of the selection's box: the objects' model boxes (or places), the scenery's boxes; a node's or a wall's
// place (the Centre pivot - level_editor_transform.h).
V3 selection_centre()
{
    const std::vector<objects::GameEntity*> objects = selected_objects();
    V3 min = {};
    V3 max = {};
    bool have = false;
    OrientedBox box{};
    if (!objects.empty()) {
        for (objects::GameEntity* object : objects) {
            V3 point_a{};
            V3 point_b{};
            if (object_edit::model_box(object, &box)) {
                bounds_of(box, &point_a, &point_b);
            } else {
                point_a = point_b = object_edit::pos_of(object);
            }
            grow_box(&min, &max, have, point_a, point_b);
            have = true;
        }
        return (min + max) * kHalf;
    }
    if (picked_node_index() >= 0 || (wall_only() != 0U)) {
        return selection_pos();
    }
    for (const uint32_t checksum : scenery::get_selection()) {
        V3 point_a{};
        V3 point_b{};
        if (scenery::cuboid(checksum, &box)) {
            bounds_of(box, &point_a, &point_b);
            grow_box(&min, &max, have, point_a, point_b);
            have = true;
        }
    }
    return have ? (min + max) * kHalf : selection_pos();
}
// Objects turned by `radians` about the toolbar's Pivot (Base: the first one's position; Centre: `centre`; Own: each
// about its own).
void turn_objects_by(const std::vector<objects::GameEntity*>& objects, float radians, V3 centre)
{
    if (objects.empty() || radians == 0.0F) {
        return;
    }
    const xform::Pivot pivot = xform::at_hand().pivot;
    const V3 base = object_edit::pos_of(objects.front());
    for (objects::GameEntity* object : objects) {
        V3 about = base;
        if (pivot == xform::Pivot::kOwn) {
            about = object_edit::pos_of(object);
        } else if (pivot == xform::Pivot::kCentre) {
            about = centre;
        }
        object_edit::turn_about(object, about, radians);
    }
}

} // namespace internal
namespace {

// Turned a notch (an object or a node by the rotation snap's step - 15 degrees unless the toolbar says otherwise -
// about the Pivot / its own place; scenery a quarter about its pivot).
float notch_radians() { return xform::angle_step() * maths::kPi / kDegreesPerHalfTurn; }

} // namespace
namespace internal {

void turn_selection(int notches)
{
    node_edit::Values node;
    if (node_values(&node)) {
        node.angles = node_edit::with_heading(node.angles, node_edit::get_heading_degrees(node.angles) +
                                                               (static_cast<float>(notches) * xform::angle_step()));
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
    if (!get_primary() && scenery::has_selection()) {
        scenery::finalise();
    }
    if (wall_only() != 0U) {
        walls::finalise();
    }
}

// Occlusion for gizmos (level_editor_picking.h's gizmo_in_front): the first occluding surface under the cursor as a depth
// along the view, and what the pick radius spans there (the slack). Alt (or a middle click) reaches past: none.
nodes::Occluder gizmo_occluder(const TickCycle& frame, V3 dir, const scenery::Hit& hit, bool everything)
{
    nodes::Occluder occluder;
    if (everything || !hit.blocked) {
        return occluder;
    }
    occluder.blocked = true;
    occluder.depth = hit.block_t_ * -get_dot(make_unit(dir), frame.back);
    occluder.units_per_pixel_ = core::at(level_editor_state().viewport_, 2) > 0
                                    ? kScreenSpanFactor * level_editor_state().lens_.tan_x_ /
                                          static_cast<float>(core::at(level_editor_state().viewport_, 2))
                                    : 0.0F;
    occluder.pixels = kOccluderPixels * pixel_scale();
    return occluder;
}

// How far along the ray a plain click may take a trigger volume (Triggers::Pick): to the first occluding surface under
// the cursor and kGizmoSlack past it - a volume standing on the floor still picks, one behind a wall doesn't.
float trigger_reach(V3 dir)
{
    scenery::Hit hit;
    scenery::get_pick(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, &hit);
    return hit.blocked ? hit.block_t_ + picking::kGizmoSlack : kFarDistance;
}

// Click-to-cycle (level_editor_picking.h): a plain click on the last click's spot (within 4 pixels at 720 lines), the
// camera where it was (a click turns it to the selection, it doesn't move it), nothing changed or dragged since and the
// filter the same - and that click took something with more under it.
bool cycle_holds(float x, float y, bool shift, bool everything)
{
    const V3 moved = level_editor_state().pos_ - pick_cycle().cycle_.pos;
    return pick_cycle().cycle_.have && !shift && !everything && pick_cycle().cycle_.index >= 0 &&
           pick_cycle().cycle_.order.size() > 1 &&
           picking::same_spot(x, y, pick_cycle().cycle_.x, pick_cycle().cycle_.y, kClickSlopPixels * pixel_scale()) &&
           get_dot(moved, moved) < 1.0F && pick_cycle().cycle_.changes == level_editor_state().changes_ &&
           pick_cycle().cycle_.filter == drag_state().pick_filter_;
}

// The hover's entry in the click cycle's order - its node, else `object`, else its scenery (-1: none of them).
int hover_cycle_index(const objects::GameEntity* object)
{
    if (drag_state().hover_node_ >= 0) {
        return picking::get_index_of(pick_cycle().cycle_.order, picking::Choice::kNode, drag_state().hover_node_);
    }
    if (object) {
        return picking::get_index_of(pick_cycle().cycle_.order, picking::Choice::kObject, object->ident());
    }
    if (drag_state().hover_sector_ != 0U) {
        return picking::get_index_of(pick_cycle().cycle_.order, picking::Choice::kScenery, drag_state().hover_sector_);
    }
    return -1;
}

// A plain click: the cycle's entry it takes (the hover's), or a new cycle from everything under the cursor now.
void note_click_for_cycle(maths::Vec2 at, V3 dir, bool plain)
{
    const float x = at.x;
    const float y = at.y;
    if (!plain) {
        pick_cycle().cycle_.have = false;
        return;
    }
    if (pick_cycle().hover_cycle_ >= 0) {
        pick_cycle().cycle_.index = pick_cycle().hover_cycle_;
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
        pick_cycle().cycle_.filter = drag_state().pick_filter_;
        pick_cycle().cycle_.order = picking::cycle_order(drag_state().hover_node_, objects, scenery, drag_state().pick_filter_);
        const objects::GameEntity* object = get_live(drag_state().hover_);
        pick_cycle().cycle_.index = hover_cycle_index(object);
    }
    if (pick_cycle().cycle_.index >= 0 && pick_cycle().cycle_.order.size() > 1) {
        std::array<char, kPickTextSize> text{};
        text::format_to(text, "%d of %d under cursor", pick_cycle().cycle_.index + 1, static_cast<int>(pick_cycle().cycle_.order.size()));
        hud::get_message(text.data(), kCountMessageSeconds);
    }
}

// Ctrl+click: a part of the scenery selection dropped from it - the rail point whose gizmo is under the cursor (from the
// points carried along), else the nearest selected piece no further than what a plain click takes (a selected shadow
// or floor in front of it too: level_editor_shadows.h's drop_target). False: nothing selected there (the click goes on
// as a plain one).
bool drop_under_cursor(V3 dir)
{
    if (!scenery::has_selection()) {
        return false;
    }
    if (drag_state().hover_node_ >= 0 && std::cmp_less(drag_state().hover_node_, nodes::get_all().size())) {
        const uint32_t name = core::at(nodes::get_all(), drag_state().hover_node_).name;
        if (!scenery::carries_rail(name)) {
            return false;
        }
        scenery::drop_rail(name);
        hud::get_message("Removed rail point " + nodes::caption(name));
        return true;
    }
    scenery::Hit first;
    std::vector<scenery::Layer> layers;
    scenery::get_pick(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, &first, &layers);
    std::vector<bool> passed;
    std::vector<bool> selected;
    for (const scenery::Layer& layer : layers) {
        passed.push_back(layer.passed);
        selected.push_back(scenery::get_selected(layer.checksum));
    }
    const int index = members::drop_target(passed, selected);
    if (index < 0) {
        return false;
    }
    const uint32_t checksum = core::at(layers, index).checksum;
    scenery::flip_over(checksum);
    hud::get_message("Removed " + object_edit::caption(checksum) + " from selection");
    return true;
}

// The selection as locks: the selected objects, else the selected scenery pieces (a node by itself: none).
std::vector<picking::Lock> selection_locks()
{
    std::vector<picking::Lock> locks;
    for (const objects::GameEntity* object : selected_objects()) {
        locks.push_back({.kind = picking::kObjects, .id = object->ident()});
    }
    if (locks.empty()) {
        for (const uint32_t sector : scenery::get_selection()) {
            locks.push_back({.kind = picking::kScenery, .id = sector});
        }
    }
    return locks;
}

} // namespace internal
} // namespace frontend::level_editor
