// NOT ORIGINAL - the level editor's API for its tools: the selection, the view, fields, transforms and the
// edit:: calls (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
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
#include "base/maths/vec2.h"
#include "base/core/format.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_view.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/maths/trig.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {

SelectionIds current_selection()
{
    SelectionIds ids;
    for (const objects::GameEntity* object : selected_objects()) {
        ids.objects.push_back(object->ident());
    }
    if (ids.objects.empty()) {
        ids.sectors = scenery::get_selection();
    }
    if (ids.objects.empty() && ids.sectors.empty() && picked_set().node_selected_ && inspected_node() >= 0) {
        ids.node = picked_set().node_name_;
    }
    return ids;
}

void set_selection(const SelectionIds& ids)
{
    if (drag_session().dragging_) {
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
    if (!editor_session().active_ || (id == 0U) || drag_session().dragging_) {
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
    if (!editor_session().active_ || !drawn_view().have_view_) {
        return false;
    }
    float nx = 0.0F;
    float ny = 0.0F;
    if (!cast_onto(fly_camera().pos_,
                   fly_camera().frame(),
                   drawn_view().lens_, point, kNearZ, &nx, &ny)) {
        return false;
    }
    ndc_to_pixel(drawn_view().viewport_, nx, ny, x, y);
    return true;
}

uint32_t pick_filter_now() { return tool_options().pick_filter_; }

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
    if (!editor_session().active_ || !drawn_view().have_view_) {
        return false;
    }
    view->pos = fly_camera().pos_;
    view->yaw = fly_camera().yaw_;
    view->pitch = fly_camera().pitch_;
    view->lens = drawn_view().lens_;
    std::memcpy(view->viewport.data(), drawn_view().viewport_.data(), sizeof(view->viewport));
    view->near_z_ = kNearZ;
    return true;
}

void look_from(V3 pos, YawPitch view)
{
    const auto yaw = view.yaw;
    const auto pitch = view.pitch;
    if (!editor_session().active_) {
        return;
    }
    unpick();
    drag_session().dragging_ = false;
    fly_camera().pos_ = pos;
    fly_camera().yaw_ = yaw;
    fly_camera().pitch_ = std::clamp(pitch, -kMaxPitchDegrees * maths::kPi / kDegreesPerHalfTurn, kMaxPitchDegrees * maths::kPi / kDegreesPerHalfTurn);
}

// (UX5 - level_editor_modules.h) the Problems window's and the notes' Go to.
bool go_to_node_named(uint32_t name)
{
    const int index =
        editor_session().active_ && !drag_session().dragging_ && (name != 0U) ? nodes::get_find(name) : -1;
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
    if (!editor_session().active_ || drag_session().dragging_ || (sector == 0U) || !scenery::sector_of(sector) ||
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
    if (!editor_session().active_ || drag_session().dragging_) {
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
        out.ids.push_back(picked_set().node_name_);
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
        picked_set().objects_.clear();
        picked_set().node_selected_ = false;
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
    transform_session().pending_forced_ = true;
    end_change();
    get_settle();
}

void place_at(V3 pos) { place_selection(pos); }

void turn_by(float radians)
{
    turn_selection_by(radians * kDegreesPerHalfTurn / maths::kPi);
}

bool scale_by(V3 factor, bool uniform) { return scale_selection_by(factor, uniform); }

std::vector<std::pair<uint32_t, uint32_t>> get_duplicate(const void* repeat)
{
    duplicate_pairs().clear();
    level_editor::get_duplicate(fly_camera().frame(), static_cast<const xform::RepeatStep*>(repeat));
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
bool get_dragging() { return editor_session().active_ && drag_session().dragging_; }
int drag_mode() { return static_cast<int>(transform_session().drag_mode_); }
maths::Vec2 get_cursor() { return {.x = transform_session().cursor_x_, .y = transform_session().cursor_y_}; }
bool get_ray(RayOut ray)
{
    auto* const origin = ray.origin;
    auto* const dir = ray.dir;
    if (!editor_session().active_ || !drawn_view().last_have_ray_) {
        return false;
    }
    *origin = fly_camera().pos_;
    *dir = drawn_view().last_ray_;
    return true;
}
bool ground_mark(GroundMark mark)
{
    auto* const base = mark.base;
    auto* const ground = mark.ground;
    if (!picked_set().have_mark_ || !have_selection()) {
        return false;
    }
    *base = picked_set().mark_base_;
    *ground = picked_set().mark_ground_;
    return true;
}
float get_yaw() { return fly_camera().yaw_; }

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
    if (!place_palette().place_open_ || place_palette().place_armed_ < 0) {
        return -1;
    }
    if (std::cmp_less(place_palette().place_armed_, place_entries().size())) {
        *colour = core::at(place_entries(), place_palette().place_armed_).colour;
    }
    return place_palette().place_armed_;
}

V3 cursor_point()
{
    return level_editor::cursor_point(drawn_view().last_ray_, drawn_view().last_have_ray_, fly_camera().frame());
}

} // namespace edit
} // namespace frontend::level_editor
