// NOT ORIGINAL - the level editor's transform gizmo and drags: moving, turning and scaling the selection with the
// mouse (ours). See level_editor.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include "engine/pad_input/input_source.h"
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
#include "editor/level_editor_arrange.h"
#include "base/core/format.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_snap.h"
#include "editor/level_editor_transform.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/maths/trig.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor {
namespace {

constexpr float kTinyLengthSquared = 1e-6F;
constexpr float kTurnEpsilon = 1e-6F;      // a turn smaller than this (radians) is none
constexpr float kRatioEpsilon = 1e-4F;     // a scale ratio this close to 1 is unchanged
constexpr float kRotPlaneMinSlope = 0.25F; // a rotation drags on the ground plane when the ray is this far from level
constexpr float kMinRotDragPixels = 2.0F;  // a rotation drag shorter than this has no direction

constexpr float kScreenDragSlope = 0.34F;  // a ray flatter than this drags in the screen plane
constexpr float kMinDragReach = 2000.0F;
constexpr float kDragReachFactor = 3.0F;
constexpr int kMaxLayerVertices = 65536;
constexpr float kAxisFarFactor = 20.0F;     // a hit further along the axis than this many orbit distances is ignored

// The selected objects scaled by `factor` (along the world's axes - each object takes it along its own axis nearest
// each; `uniform`: the same on all three) about their positions. Whether any changed.
bool scale_objects(const std::vector<objects::GameEntity*>& objects, V3 factor, bool uniform)
{
    bool any = false;
    for (objects::GameEntity* object : objects) {
        if (!scale_tool::object_scalable(object)) {
            continue;
        }
        V3 local = factor;
        if (!uniform) {
            std::array<V3, 3> rows{};
            for (int i = 0; i < 3; ++i) {
                core::at(rows, i) = {.x = core::at(object->basis_frame().row, i).x, .y = core::at(object->basis_frame().row, i).y, .z = core::at(object->basis_frame().row, i).z};
            }
            local = scale_tool::get_uniform(1.0F);
            const std::array<float, 3> along = {{factor.x, factor.y, factor.z}};
            for (int axis_index = 0; axis_index < 3; ++axis_index) {
                if (core::at(along, axis_index) == 1.0F) {
                    continue;
                }
                const int axis = scale_tool::nearest_axis(rows, axis_index);
                axis_ref(local, axis) = core::at(along, axis_index);
            }
        }
        const V3 now = scale_tool::get_object_scale(object);
        const V3 allowed = scale_tool::allowed_factor(local, {now}, {scale_tool::object_limit(object)}, uniform);
        if (is_unit_scale(allowed)) {
            continue;
        }
        if (scale_tool::set_object_scale(object, per_axis(now, allowed))) {
            any = true;
        }
    }
    return any;
}

} // namespace
namespace internal {

// [ / ] and Alt+[ / ] (level_editor_scale.h): the selected scenery scaled by `factor` (along the world's axes) about
// the middle of its bottom, or the selected objects about their positions; one undo step. False when nothing changed
// (nodes, or at its limit - said on the message line).
bool scale_selection_by(V3 factor, bool uniform)
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (wall_only() != 0U) {
        // (level_editor_walls.h: a wall's collision is moved by the geometry's own Translate / RotateY on the BSP
        // tree's sixteenths; a scale would need its collision kept and rebuilt as level_editor_scale.h does scenery's)
        hud::get_message("Invisible walls can't be scaled");
        return false;
    }
    if (picked_node_index() >= 0 || (objects.empty() && !scenery::has_selection())) {
        hud::get_message("Nodes can't be scaled");
        return false;
    }
    if (!objects.empty() && std::ranges::none_of(objects, scale_tool::object_scalable)) {
        hud::get_message("Object has no model to scale");
        return false;
    }
    const UndoStep before = get_snapshot();
    const bool changed = objects.empty() ? !is_unit_scale(scenery::scale_selection(factor, uniform))
                                         : scale_objects(objects, factor, uniform);
    if (!changed) {
        hud::get_message("Scale limit reached");
        return false;
    }
    if (objects.empty()) {
        follow_scenery();
        scenery::finalise();
    }
    push_undo(before, "Scale");
    return true;
}

// The selection's scale (scenery: its first piece's, along its axes as first touched; an object: along its own).
V3 selection_scale_now()
{
    if (objects::GameEntity* object = get_primary()) {
        return scale_tool::get_object_scale(object);
    }
    return scenery::selection_scale();
}
// The selection scaled to `scale` (along its own axes, as selection_scale_now): the Inspector's fields.
void scale_selection_to(V3 scale)
{
    const V3 now = selection_scale_now();
    const V3 local = {.x = scale_tool::confine(scale.x) / now.x, .y = scale_tool::confine(scale.y) / now.y,
                      .z = scale_tool::confine(scale.z) / now.z};
    if (get_primary()) {
        for (objects::GameEntity* object : selected_objects()) {
            if (scale_tool::object_scalable(object)) {
                scale_tool::set_object_scale(object, per_axis(scale_tool::get_object_scale(object), local));
            }
        }
        return;
    }
    // (the first piece's own axes to the world's: its quarter turns)
    scenery::scale_selection(turned_scale(local, scenery::selection_quarters()), is_uniform_scale(local));
}
V3& scale_at_field_start()
{
    static V3 scale_at_field_start{
        .x = 1.0F, .y = 1.0F, .z = 1.0F}; // (the Inspector's Scale fields: the scale when taken - its undo step)
    return scale_at_field_start;
}

} // namespace internal
namespace {

// --- dragging ---
//
// Free: looking down on it, the selection follows the point under the cursor on the horizontal plane at its base;
// with the camera about level with that (a ray meeting it at under ~20 degrees), the mouse moves it in the view's
// terms instead - across for left / right, away / back along the ground for up / down, as far as a pixel spans at
// its distance. Shift: up / down only (an upright plane facing the camera). The wheel pushes it away / pulls it back;
// Ctrl snaps it to a 25-unit grid. Along an arrow: the point of that axis nearest the mouse ray. It moves only once
// the cursor has gone a few pixels from where it was pressed (a click doesn't nudge it).

V3 drag_plane_normal(bool vertical, const TickCycle& frame)
{
    if (!vertical) {
        return {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }
    const V3 flat = {.x = frame.back.x, .y = 0.0F, .z = frame.back.z};
    if (get_dot(flat, flat) < kTinyLengthSquared) {
        return frame.back;
    }
    return make_unit(flat);
}

const std::array<V3, 3> kAxes = {
    {{.x = 1.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 1.0F, .z = 0.0F}, {.x = 0.0F, .y = 0.0F, .z = 1.0F}}};

// The gizmo's size: a fifth of the camera's distance from the selection (20 to 1500 units)
// (as the picture shows the distance - an ortho view's far eye scaled down: level_editor_camera.h; [editor] gizmo_pct)
float arrow_length()
{
    const float distance = view_cam::apparent(std::sqrt(get_dot(selection_pos() - fly_camera().pos_, selection_pos() - fly_camera().pos_)));
    const float length =
        std::max(20.0F, std::min(1500.0F, distance * 0.2F)) * static_cast<float>(prefs::get_pull().gizmo_pct_) / 100.0F;
    return view_cam::ortho() ? length *
                                   std::sqrt(get_dot(selection_pos() - fly_camera().pos_,
                                                     selection_pos() - fly_camera().pos_)) /
                                   std::max(1.0F, distance)
                             : length;
}

} // namespace
namespace internal {

// A world point in back-buffer pixels (false: behind the camera).
bool get_to_pixel(const TickCycle& frame, V3 point, float* px, float* py)
{
    float nx = 0.0F;
    float ny = 0.0F;
    if (!cast_onto(fly_camera().pos_, frame, drawn_view().lens_, point, kNearZ, &nx, &ny)) {
        return false;
    }
    ndc_to_pixel(drawn_view().viewport_, nx, ny, px, py);
    return true;
}

} // namespace internal
namespace {

// --- the transform tools (level_editor_transform.h): Move / Rotate / Scale, World / Local, the pivot ---

// What the selection can do: a chain's point doesn't turn (a ladder turns whole with Z / X); only scenery and objects
// with a model scale.
bool selection_turns() { return !(picked_node_index() >= 0 && node_edit::is_chain(drag_state().node_name_)); }
bool selection_scales()
{
    if (picked_node_index() >= 0 || (wall_only() != 0U)) {
        return false;
    }
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (objects.empty()) {
        return scenery::has_selection();
    }
    return std::ranges::any_of(objects, scale_tool::object_scalable);
}

} // namespace
namespace internal {

// The tool the gizmo shows: the toolbar's, or Move when the selection can't do that.
xform::Mode gizmo_mode()
{
    const xform::Mode mode = xform::at_hand().mode;
    if ((mode == xform::Mode::kSpinBy && !selection_turns()) || (mode == xform::Mode::kScale && !selection_scales())) {
        return xform::Mode::kShiftBy;
    }
    return mode;
}

} // namespace internal
namespace {

// The gizmo's axes: World the world's; Local the first object's own (its matrix's rows), a node's heading, the scenery's
// quarter turns (its first piece's) - an invisible wall the world's.
void selection_axes(std::span<V3, 3> out)
{
    for (int i = 0; i < 3; ++i) {
        core::at(out, i) = core::at(kAxes, i);
    }
    if (xform::at_hand().space != xform::Space::kLocal) {
        return;
    }
    if (const objects::GameEntity* object = get_primary()) {
        std::array<V3, 3> rows{};
        for (int i = 0; i < 3; ++i) {
            core::at(rows, i) = {.x = core::at(object->basis_frame().row, i).x, .y = core::at(object->basis_frame().row, i).y, .z = core::at(object->basis_frame().row, i).z};
        }
        xform::unit_axes(rows, out);
        return;
    }
    node_edit::Values node;
    if (node_values(&node)) {
        xform::heading_axes(node.angles.y, out);
        return;
    }
    if ((wall_only() == 0U) && scenery::has_selection()) {
        const int quarters = scenery::selection_quarters();
        for (int i = 0; i < 3; ++i) {
            core::at(out, i) = get_quarter_turn(core::at(kAxes, i), quarters);
        }
    }
}
V3 axis_vector(int axis)
{
    std::array<V3, 3> axes{};
    selection_axes(axes);
    return core::at(axes, axis >= 0 && axis < 3 ? axis : 0);
}

} // namespace
namespace internal {

// The selection's heading (the repeat of a duplicate): the first object's (its at row), a node's, the scenery's quarter
// turns.
float selection_heading()
{
    if (const objects::GameEntity* object = get_primary()) {
        return xform::get_heading_of(
            {.x = core::at(object->basis_frame().row, 2).x, .y = core::at(object->basis_frame().row, 2).y, .z = core::at(object->basis_frame().row, 2).z});
    }
    node_edit::Values node;
    if (node_values(&node)) {
        return node.angles.y;
    }
    return scenery::has_selection() ? static_cast<float>(scenery::selection_quarters()) * maths::kPi * kHalf : 0.0F;
}

} // namespace internal
namespace {

// Where Rotate / Scale turn and scale about (the toolbar's Pivot): Centre the selection's middle; Base / Own the first
// object's position, a node's place, the scenery's pivot - for Scale the middle of the scenery's bottom (as [ / ]).
V3 transform_pivot(xform::Mode mode)
{
    if (xform::at_hand().pivot == xform::Pivot::kCentre) {
        return selection_centre();
    }
    V3 pos = selection_pos();
    if (mode == xform::Mode::kScale && !get_primary() && scenery::has_selection()) {
        return {.x = pos.x, .y = scenery::get_bottom(), .z = pos.z};
    }
    return pos;
}

} // namespace
namespace internal {

xform::Gizmo gizmo_now()
{
    xform::Gizmo gizmo;
    gizmo.mode = drag_state().dragging_ ? drag_state().drag_mode_ : gizmo_mode();
    if (gizmo.mode == xform::Mode::kShiftBy) {
        gizmo.pivot = selection_pos();
    } else if (drag_state().dragging_) {
        gizmo.pivot = drag_state().transform_pivot_;
    } else {
        gizmo.pivot = transform_pivot(gizmo.mode);
    }
    selection_axes(gizmo.axes);
    gizmo.length = arrow_length();
    gizmo.rotates = selection_turns();
    return gizmo;
}

} // namespace internal
namespace {

// Turned to `target` radians from where the drag started: objects about the Pivot, a node's heading, scenery and
// invisible walls by whole quarter turns (the engine turns level geometry by quarters - level_editor_scenery.h).
void apply_turn_to(float target)
{
    const std::vector<objects::GameEntity*> objects = selected_objects();
    node_edit::Values node;
    if (!objects.empty()) {
        const float delta = target - drag_state().rot_applied_;
        if (std::fabs(delta) < kTurnEpsilon) {
            return;
        }
        turn_objects_by(objects, delta, drag_state().transform_pivot_);
    } else if (node_values(&node)) {
        const float delta = target - drag_state().rot_applied_;
        if (std::fabs(delta) < kTurnEpsilon) {
            return;
        }
        node.angles = node_edit::with_heading(node.angles, node_edit::get_heading_degrees(node.angles) +
                                                               (delta * kDegreesPerHalfTurn / maths::kPi));
        set_selected_node(node);
    } else {
        const int quarters = xform::get_quarters(target);
        if (quarters == drag_state().quarters_applied_) {
            return;
        }
        turn_selection(quarters - drag_state().quarters_applied_);
        drag_state().quarters_applied_ = quarters;
    }
    drag_state().rot_applied_ = target;
    drag_state().pending_forced_ = true;
}

// The scale made since the drag started, along the gizmo's axes: the selection's scale (along its own axes) over its
// scale then - an object's own axis nearest each world axis, the scenery's turned (odd quarter turns swap x and z).
V3 scale_applied_now()
{
    const V3 now = selection_scale_now();
    V3 ratio = {.x = now.x / drag_state().scale_start_.x,
                .y = now.y / drag_state().scale_start_.y,
                .z = now.z / drag_state().scale_start_.z};
    if (xform::at_hand().space == xform::Space::kLocal) {
        return ratio;
    }
    if (const objects::GameEntity* object = get_primary()) {
        std::array<V3, 3> rows{};
        for (int i = 0; i < 3; ++i) {
            core::at(rows, i) = {.x = core::at(object->basis_frame().row, i).x, .y = core::at(object->basis_frame().row, i).y, .z = core::at(object->basis_frame().row, i).z};
        }
        const std::array<float, 3> own = {{ratio.x, ratio.y, ratio.z}};
        return {.x = core::at(own, scale_tool::nearest_axis(rows, 0)), .y = core::at(own, scale_tool::nearest_axis(rows, 1)),
                .z = core::at(own, scale_tool::nearest_axis(rows, 2))};
    }
    return turned_scale(ratio, scenery::selection_quarters());
}

// Scaled to `wanted` (along the gizmo's axes) from where the drag started (within the limits - level_editor_scale.h):
// objects each about its position (Centre: their places spread from the middle too), scenery about the middle of its
// bottom (Centre: about its middle).
void apply_scale_to(V3 wanted, bool uniform)
{
    const V3 applied = scale_applied_now();
    const V3 ratio = {.x = wanted.x / applied.x, .y = wanted.y / applied.y, .z = wanted.z / applied.z};
    if (std::fabs(ratio.x - 1.0F) < kRatioEpsilon && std::fabs(ratio.y - 1.0F) < kRatioEpsilon &&
        std::fabs(ratio.z - 1.0F) < kRatioEpsilon) {
        return;
    }
    const bool local = xform::at_hand().space == xform::Space::kLocal;
    const bool centre = xform::at_hand().pivot == xform::Pivot::kCentre;
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        std::vector<V3> before;
        before.reserve(objects.size());
        for (const objects::GameEntity* object : objects) {
            before.push_back(object_edit::pos_of(object));
        }
        if (local) {
            for (objects::GameEntity* object : objects) {
                if (!scale_tool::object_scalable(object)) {
                    continue;
                }
                const V3 now = scale_tool::get_object_scale(object);
                const V3 allowed =
                    scale_tool::allowed_factor(ratio, {now}, {scale_tool::object_limit(object)}, uniform);
                if (!is_unit_scale(allowed)) {
                    scale_tool::set_object_scale(object, per_axis(now, allowed));
                }
            }
        } else {
            scale_objects(objects, ratio, uniform);
        }
        if (centre) {
            std::array<V3, 3> axes{};
            selection_axes(axes);
            const std::array<float, 3> ratios = {{ratio.x, ratio.y, ratio.z}};
            for (size_t i = 0; i < objects.size(); ++i) {
                const V3 delta = core::at(before, i) - drag_state().transform_pivot_;
                V3 placed = drag_state().transform_pivot_;
                for (int axis_index = 0; axis_index < 3; ++axis_index) {
                    placed = placed + core::at(axes, axis_index) *
                                          (get_dot(delta, core::at(axes, axis_index)) * core::at(ratios, axis_index));
                }
                object_edit::get_place(core::at(objects, i), placed);
            }
        }
    } else {
        const V3 centre_before = selection_centre();
        scenery::scale_selection(local ? turned_scale(ratio, scenery::selection_quarters()) : ratio, uniform);
        if (centre) {
            scenery::move_to(scenery::get_pivot() + (centre_before - selection_centre()));
        }
        follow_scenery();
    }
    drag_state().pending_forced_ = true;
}

// A Rotate / Scale drag starting (on a handle, or Alt+G's modal one): its pivot; the cursor's angle about it (on the
// level plane through it when looking down on it, else on the screen - which way round from two of the ring's points),
// or its distance from it (a handle's axis, else in pixels from the pivot's pixel).
void start_transform_drag(V3 origin, V3 dir, const TickCycle& frame)
{
    drag_state().transform_pivot_ = transform_pivot(drag_state().drag_mode_);
    if (drag_state().drag_mode_ == xform::Mode::kSpinBy) {
        drag_state().rot_total_ = drag_state().rot_applied_ = 0.0F;
        drag_state().quarters_applied_ = 0;
        drag_state().rot_sign_ = 1.0F;
        drag_state().rot_last_ = 0.0F;
        float fraction = 0.0F;
        drag_state().rot_by_plane_ = std::fabs(make_unit(dir).y) > kRotPlaneMinSlope &&
                                     ray_plane({.origin = origin, .dir = dir}, drag_state().transform_pivot_,
                                               {.x = 0.0F, .y = 1.0F, .z = 0.0F}, &fraction) &&
                                     fraction < kMaxPickDistance;
        if (drag_state().rot_by_plane_) {
            drag_state().rot_last_ = xform::turn_around_axis(drag_state().transform_pivot_, origin + dir * fraction);
            return;
        }
        float px = 0.0F;
        float py = 0.0F;
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        float const reach = arrow_length();
        if (!get_to_pixel(frame, drag_state().transform_pivot_, &px, &py)) {
            return;
        }
        drag_state().rot_last_ = std::atan2(drag_state().cursor_x_ - px, drag_state().cursor_y_ - py);
        if (get_to_pixel(frame, drag_state().transform_pivot_ + V3{.x = 0.0F, .y = 0.0F, .z = reach}, &ax, &ay) &&
            get_to_pixel(frame, drag_state().transform_pivot_ + V3{.x = reach, .y = 0.0F, .z = 0.0F}, &bx, &by)) {
            drag_state().rot_sign_ =
                xform::get_wrap(std::atan2(bx - px, by - py) - std::atan2(ax - px, ay - py)) < 0.0F ? -1.0F : 1.0F;
        }
        return;
    }
    drag_state().scale_start_ = selection_scale_now();
    drag_state().scale_s0_ = 0.0F;
    if (drag_state().axis_ >= 0 && drag_state().axis_ < 3) {
        closest_on_axis({.origin = drag_state().transform_pivot_, .dir = axis_vector(drag_state().axis_)}, {.origin = origin, .dir = dir}, &drag_state().scale_s0_);
    } else {
        float px = 0.0F;
        float py = 0.0F;
        if (get_to_pixel(frame, drag_state().transform_pivot_, &px, &py)) {
            drag_state().scale_s0_ = std::hypot(drag_state().cursor_x_ - px, drag_state().cursor_y_ - py);
        }
    }
}

void rotate_drag(V3 origin, V3 dir, const TickCycle& frame, bool snap)
{
    float typed = 0.0F;
    float target = 0.0F;
    if (xform::typed_value(drag_state().typed_, &typed)) {
        target = typed * maths::kPi / kDegreesPerHalfTurn;
    } else {
        float angle = 0.0F;
        bool have = false;
        float fraction = 0.0F;
        float px = 0.0F;
        float py = 0.0F;
        if (drag_state().rot_by_plane_) {
            if (ray_plane({.origin = origin, .dir = dir}, drag_state().transform_pivot_,
                          {.x = 0.0F, .y = 1.0F, .z = 0.0F}, &fraction) &&
                fraction < kMaxPickDistance) {
                angle = xform::turn_around_axis(drag_state().transform_pivot_, origin + dir * fraction);
                have = true;
            }
        } else if (get_to_pixel(frame, drag_state().transform_pivot_, &px, &py) &&
                   std::hypot(drag_state().cursor_x_ - px, drag_state().cursor_y_ - py) > kMinRotDragPixels) {
            angle = std::atan2(drag_state().cursor_x_ - px, drag_state().cursor_y_ - py);
            have = true;
        }
        if (have) {
            const float delta = xform::get_wrap(angle - drag_state().rot_last_);
            drag_state().rot_last_ = angle;
            drag_state().rot_total_ += drag_state().rot_by_plane_ ? delta : delta * drag_state().rot_sign_;
        }
        target = snap ? xform::snap_angle({.radians = drag_state().rot_total_, .step_degrees_ = xform::angle_step()}) : drag_state().rot_total_;
    }
    apply_turn_to(target);
}

void scale_drag(V3 origin, V3 dir, const TickCycle& frame, bool snap)
{
    // (the typed axis, else the arrow's, else none: uniform)
    int axis = -1;
    if (drag_state().typed_.axis >= 0) {
        axis = drag_state().typed_.axis;
    } else if (drag_state().axis_ >= 0 && drag_state().axis_ < 3) {
        axis = drag_state().axis_;
    }
    float factor = 1.0F;
    float typed = 0.0F;
    if (xform::typed_value(drag_state().typed_, &typed)) {
        if (typed <= 0.0F) {
            return;
        }
        factor = typed;
    } else {
        float scale = 0.0F;
        float px = 0.0F;
        float py = 0.0F;
        if (drag_state().axis_ >= 0 && drag_state().axis_ < 3) {
            if (!closest_on_axis({.origin = drag_state().transform_pivot_, .dir = axis_vector(drag_state().axis_)},
                                 {.origin = origin, .dir = dir}, &scale)) {
                return;
            }
            factor = xform::factor_along(drag_state().scale_s0_, scale);
        } else {
            if (!get_to_pixel(frame, drag_state().transform_pivot_, &px, &py)) {
                return;
            }
            factor = xform::factor_from_pixels(drag_state().scale_s0_, std::hypot(drag_state().cursor_x_ - px, drag_state().cursor_y_ - py));
        }
        if (snap) {
            factor = xform::snap_factor(factor);
        }
    }
    const V3 wanted =
        axis < 0 ? V3{.x = factor, .y = factor, .z = factor}
                 : V3{.x = axis == 0 ? factor : 1.0F, .y = axis == 1 ? factor : 1.0F, .z = axis == 2 ? factor : 1.0F};
    apply_scale_to(wanted, axis < 0);
}

} // namespace
namespace internal {

// --- dragging (Move) ---

void start_drag(V3 origin, V3 dir, bool vertical, const TickCycle& frame, float mouse_x, float mouse_y)
{
    if (drag_state().drag_mode_ != xform::Mode::kShiftBy) {
        start_transform_drag(origin, dir, frame);
        return;
    }
    const V3 pos = selection_pos();
    drag_state().drag_vertical_ = vertical;
    drag_state().drag_from_ = pos;
    drag_state().drag_mouse_x_ = mouse_x;
    drag_state().drag_mouse_y_ = mouse_y;
    drag_state().drag_push_ = 0.0F;
    if (drag_state().axis_ >= 0) {
        drag_state().axis_t0_ = 0.0F;
        closest_on_axis({.origin = pos, .dir = axis_vector(drag_state().axis_)}, {.origin = origin, .dir = dir}, &drag_state().axis_t0_);
        return;
    }
    drag_state().drag_plane_normal_ = drag_plane_normal(vertical, frame);
    drag_state().drag_plane_offset_ =
        vertical ? V3{.x = 0.0F, .y = 0.0F, .z = 0.0F} : V3{.x = 0.0F, .y = get_selection_base() - pos.y, .z = 0.0F};
    drag_state().drag_right_ = make_unit({.x = frame.right.x, .y = 0.0F, .z = frame.right.z});
    drag_state().drag_forward_ = make_unit({.x = -frame.back.x, .y = 0.0F, .z = -frame.back.z});
    const float depth = std::max(50.0F, -get_dot(pos - origin, frame.back));
    drag_state().drag_units_ = kScreenSpanFactor * depth * drawn_view().lens_.tan_x_ / static_cast<float>(std::max(1, stored_mouse().window_width_));
    float fraction = 0.0F;
    drag_state().drag_screen_ =
        !vertical && (std::fabs(make_unit(dir).y) < kScreenDragSlope ||
                      !ray_plane({.origin = origin, .dir = dir}, pos + drag_state().drag_plane_offset_,
                                 drag_state().drag_plane_normal_, &fraction));
    const V3 point = pos + drag_state().drag_plane_offset_;
    drag_state().drag_offset_ = !drag_state().drag_screen_ && ray_plane({.origin = origin, .dir = dir}, point,
                                                                        drag_state().drag_plane_normal_, &fraction)
                                    ? point - (origin + dir * fraction)
                                    : V3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    drag_state().drag_reach_ = std::max(kMinDragReach, kDragReachFactor * fraction);
    drag_state().drag_offset_ = drag_state().drag_offset_ - drag_state().drag_plane_offset_;
}

Snapping snapping_now(bool ctrl, bool shift, bool alt)
{
    Snapping snap;
    snap.on = true;
    if (ctrl && alt) {
        snap.target = xform::SnapTarget::kVertex;
    } else if (alt && shift) {
        snap.target = xform::SnapTarget::kSurface;
    } else if (ctrl && shift) {
        snap.target = xform::SnapTarget::kWorldGrid;
    } else if (ctrl) {
        snap.target = xform::SnapTarget::kGrid;
    } else if (drag_state().snap_always_) {
        snap.target = xform::at_hand().target;
    } else {
        snap.on = false;
    }
    return snap;
}

} // namespace internal
namespace {

// The selection's box corners (vertex snapping's own corner): its objects' model boxes, its scenery's boxes, else its
// place.
std::vector<V3> selection_corners()
{
    std::vector<V3> out;
    OrientedBox box{};
    std::array<V3, 8> corners{};
    auto add = [&](const OrientedBox& b) {
        box_corners(b.origin, b.axes, b.min, b.max, corners);
        out.insert(out.end(), corners.begin(), corners.end());
    };
    const std::vector<objects::GameEntity*> objects = selected_objects();
    for (objects::GameEntity* object : objects) {
        if (object_edit::model_box(object, &box)) {
            add(box);
        }
    }
    if (objects.empty() && picked_node_index() < 0 && (wall_only() == 0U)) {
        for (const uint32_t checksum : scenery::get_selection()) {
            if (scenery::cuboid(checksum, &box)) {
                add(box);
            }
        }
    }
    if (out.empty()) {
        out.push_back(selection_pos());
    }
    return out;
}

// Surface snapping: the selection's base onto what the collision ray meets first under the cursor (its own pieces
// passed); an object stood square to the slope with the toolbar's Align.
void surface_drag(V3 origin, V3 dir)
{
    std::vector<uint32_t> skip;
    if (!get_primary() && picked_node_index() < 0) {
        skip = scenery::get_selection();
        if (const uint32_t wall = wall_only()) {
            skip.push_back(wall);
        }
    }
    snap::SurfaceHit hit;
    if (!snap::get_surface(origin, make_unit(dir), kMaxPickDistance, skip, &hit)) {
        return;
    }
    const V3 pos = selection_pos();
    const V3 wanted = snap::on_surface(pos, get_selection_base(), hit.point);
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z) {
        place_selection(wanted);
    }
    if (!xform::at_hand().align_to_slope_) {
        return;
    }
    for (objects::GameEntity* object : selected_objects()) {
        for (maths::Mat4* matrix : {&object->basis_frame(), &object->display_matrix()}) {
            std::array<V3, 3> rows{};
            for (int i = 0; i < 3; ++i) {
                core::at(rows, i) = {
                    .x = core::at(matrix->row, i).x, .y = core::at(matrix->row, i).y, .z = core::at(matrix->row, i).z};
            }
            if (!snap::align_rows(rows, hit.normal)) {
                continue;
            }
            for (int i = 0; i < 3; ++i) {
                core::at(matrix->row, i).x = core::at(rows, i).x;
                core::at(matrix->row, i).y = core::at(rows, i).y;
                core::at(matrix->row, i).z = core::at(rows, i).z;
            }
            drag_state().pending_forced_ = true;
        }
    }
}

// Vertex snapping: the selection's corner nearest the cursor (chosen when it starts) onto the nearest vertex of what's
// under the cursor - an object's box corner, or a scenery piece's drawn vertex (the selection's own passed).
void vertex_drag(Ray ray, const TickCycle& frame)
{
    const auto origin = ray.origin;
    const auto dir = ray.dir;
    const V3 unit = make_unit(dir);
    const V3 pos = selection_pos();
    if (!drag_state().vertex_have_) {
        std::vector<V3> corners = selection_corners();
        const int nearest = snap::nearest_on_screen(
            corners, [&](V3 point, float* x, float* y) { return get_to_pixel(frame, point, x, y); },
            drag_state().cursor_x_, drag_state().cursor_y_);
        if (nearest < 0) {
            return;
        }
        drag_state().vertex_corner_ = core::at(corners, nearest) - pos;
        drag_state().vertex_have_ = true;
    }
    std::vector<V3> targets;
    V3 at = {};
    float reach = kMaxPickDistance;
    for (const auto& picked : object_edit::pick_all(origin, unit, kMaxPickDistance)) {
        if (is_picked(picked.first)) {
            continue;
        }
        OrientedBox box{};
        if (object_edit::model_box(picked.first, &box)) {
            std::array<V3, kBoxCornerCount> corners{};
            box_corners(box.origin, box.axes, box.min, box.max, corners);
            targets.assign(corners.begin(), corners.end());
            at = origin + unit * picked.second;
            reach = picked.second;
        }
        break;
    }
    scenery::Hit hit;
    std::vector<scenery::Layer> layers;
    scenery::get_pick(origin, unit, reach, &hit, &layers);
    for (const scenery::Layer& layer : layers) {
        if (scenery::get_selected(layer.checksum) || layer.t >= reach) {
            continue;
        }
        std::vector<V3> vertices = snap::sector_vertices(checksums::Name{layer.checksum}, kMaxLayerVertices);
        if (!vertices.empty()) {
            targets = std::move(vertices);
            at = origin + unit * layer.t;
        }
        break;
    }
    const int nearest = snap::get_nearest(targets, at);
    if (nearest < 0) {
        return;
    }
    const V3 wanted = core::at(targets, nearest) - drag_state().vertex_corner_;
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z) {
        place_selection(wanted);
    }
}

// A typed amount: that far along the typed axis - else the arrow's, else X - from where the drag started.
void typed_drag(V3 pos, float typed)
{
    int axis = 0;
    if (drag_state().typed_.axis >= 0) {
        axis = drag_state().typed_.axis;
    } else if (drag_state().axis_ >= 0) {
        axis = drag_state().axis_;
    }
    const V3 wanted = drag_state().drag_start_ + axis_vector(axis) * typed;
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z) {
        place_selection(wanted);
    }
}

// A scenery piece dragged without snapping: its box faces flush with its neighbours' (level_editor_arrange.h) -
// along the arrow's axis in world space (none in local space), else y up / down, else x and z.
V3 flush_with_neighbours(V3 pos, V3 wanted)
{
    // (the axes as bits: the arrow's axis in world space - none in local space -, else y up / down, else x and z)
    const int free_axes = drag_state().drag_vertical_ ? 2 : 5;
    int axes = free_axes;
    if (drag_state().axis_ >= 0) {
        axes = xform::at_hand().space == xform::Space::kWorld ? 1 << drag_state().axis_ : 0;
    }
    if (axes != 0) {
        wanted = arrange::edge_snapped(pos, wanted, axes);
    }
    return wanted;
}

} // namespace
namespace internal {

void get_drag(V3 origin, V3 dir, const TickCycle& frame, float mouse_x, float mouse_y, Snapping snap)
{
    if (drag_state().drag_mode_ == xform::Mode::kSpinBy) {
        rotate_drag(origin, dir, frame, snap.on);
        return;
    }
    if (drag_state().drag_mode_ == xform::Mode::kScale) {
        scale_drag(origin, dir, frame, snap.on);
        return;
    }
    const V3 pos = selection_pos();
    // (a typed amount: that far along the typed axis - else the arrow's, else X - from where the drag started)
    float typed = 0.0F;
    if (xform::typed_value(drag_state().typed_, &typed)) {
        typed_drag(pos, typed);
        return;
    }
    if (!snap.on || snap.target != xform::SnapTarget::kVertex) {
        drag_state().vertex_have_ = false;
    }
    if (snap.on && snap.target == xform::SnapTarget::kSurface) {
        surface_drag(origin, dir);
        return;
    }
    if (snap.on && snap.target == xform::SnapTarget::kVertex) {
        vertex_drag({.origin = origin, .dir = dir}, frame);
        return;
    }
    // Grid snapping: relative (whole steps from where the drag started), or the world's grid (absolute).
    const bool absolute = snap.target == xform::SnapTarget::kWorldGrid;
    float grid = xform::get_grid();
    auto relative = [&](float delta) { return xform::snap_relative(delta, grid); };
    V3 wanted{};
    if (drag_state().axis_ >= 0) {
        const V3 u = axis_vector(drag_state().axis_);
        float scale = 0.0F;
        if (!closest_on_axis({.origin = drag_state().drag_from_, .dir = u}, {.origin = origin, .dir = dir}, &scale)) {
            return;
        }
        const float along = scale - drag_state().axis_t0_;
        if (std::fabs(along) > fly_camera().orbit_distance_ * kAxisFarFactor) {
            return; // (the ray nearly along the axis: far off)
        }
        wanted = drag_state().drag_from_ + u * along;
        if (snap.on && absolute && xform::at_hand().space == xform::Space::kWorld) {
            float& component = axis_ref(wanted, drag_state().axis_);
            component = snap_to_grid(component, grid);
        } else if (snap.on) {
            const float total = get_dot(wanted - drag_state().drag_start_, u);
            wanted = wanted + u * (relative(total) - total);
        }
    } else if (drag_state().drag_screen_) {
        // (away for the cursor going up: 2.5 times a pixel's span, the ground being seen end-on)
        const float right = (mouse_x - drag_state().drag_mouse_x_) * drag_state().drag_units_;
        const float away = (drag_state().drag_mouse_y_ - mouse_y) * drag_state().drag_units_ * 2.5F;
        wanted = drag_state().drag_from_ + drag_state().drag_right_ * right + drag_state().drag_forward_ * away;
        wanted.y = pos.y;
    } else {
        float fraction = 0.0F;
        if (!ray_plane({.origin = origin, .dir = dir}, pos + drag_state().drag_plane_offset_,
                       drag_state().drag_plane_normal_, &fraction) ||
            fraction > drag_state().drag_reach_) {
            return; // (towards the horizon)
        }
        wanted = origin + dir * fraction + drag_state().drag_offset_;
        if (drag_state().drag_vertical_) {
            wanted = {.x = pos.x, .y = wanted.y, .z = pos.z};
        } else {
            wanted.y = pos.y;
        }
    }
    if (drag_state().axis_ < 0) {
        if (!drag_state().drag_vertical_) {
            wanted = wanted + drag_state().drag_forward_ * drag_state().drag_push_;
        }
        if (snap.on && absolute) {
            wanted = drag_state().drag_vertical_ ? V3{.x = wanted.x, .y = snap_to_grid(wanted.y, grid), .z = wanted.z}
                                     : V3{.x = snap_to_grid(wanted.x, grid), .y = wanted.y, .z = snap_to_grid(wanted.z, grid)};
        } else if (snap.on) {
            V3 delta = wanted - drag_state().drag_start_;
            delta = drag_state().drag_vertical_ ? V3{.x = delta.x, .y = relative(delta.y), .z = delta.z}
                                                : V3{.x = relative(delta.x), .y = delta.y, .z = relative(delta.z)};
            wanted = drag_state().drag_start_ + delta;
        }
    }
    // (no snapping held: a scenery piece's box faces flush with its neighbours' - level_editor_arrange.h)
    if (!snap.on && !get_primary() && picked_node_index() < 0 && (wall_only() == 0U)) {
        wanted = flush_with_neighbours(pos, wanted);
    }
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z) {
        place_selection(wanted);
    }
}

// A move, turn or scale made (its undo step just pushed): the last action for its panel (level_editor_adjust.h).
void note_for_adjust(xform::Mode mode, V3 moved, bool uniform)
{
    adjust::ActStep action;
    if (mode == xform::Mode::kSpinBy) {
        action.kind = adjust::Kind::kTurn;
        action.quarters = (get_primary() == nullptr) && picked_node_index() < 0;
        action.degrees =
            action.quarters ? static_cast<float>(drag_state().quarters_applied_) * kQuarterTurnDegrees : drag_state().rot_applied_ * kDegreesPerHalfTurn / maths::kPi;
    } else if (mode == xform::Mode::kScale) {
        action.kind = adjust::Kind::kScale;
        action.factor = scale_applied_now();
        action.uniform = uniform;
    } else {
        action.kind = adjust::Kind::kShiftBy;
        action.offset = moved;
    }
    adjust::get_note(action);
}

void end_drag()
{
    if (!drag_state().dragging_) {
        return;
    }
    drag_state().dragging_ = false;
    const bool uniform = drag_state().axis_ < 0 && drag_state().typed_.axis < 0;
    drag_state().axis_ = -1;
    const uint64_t serial = undo_steps().undo_.empty() ? 0 : undo_steps().undo_.back().serial;
    end_change();
    get_settle();
    if (!undo_steps().undo_.empty() && undo_steps().undo_.back().serial != serial) {
        note_for_adjust(drag_state().drag_mode_, selection_pos() - drag_state().drag_start_, uniform);
    }
    const V3 at = selection_pos();
    const char* done = "dragged";
    if (drag_state().drag_mode_ == xform::Mode::kSpinBy) {
        done = "turned";
    } else if (drag_state().drag_mode_ == xform::Mode::kScale) {
        done = "scaled";
    }
    note_action("%s to (%.0f, %.0f, %.0f): %s", done, at.x, at.y, at.z, describe_selection().c_str());
    drag_state().drag_mode_ = xform::Mode::kShiftBy;
    drag_state().modal_ = false;
    drag_state().typed_ = {};
    drag_state().vertex_have_ = false;
}

void set_relative(bool on)
{
    if (on == editor_session().relative_) {
        return;
    }
    input::set_relative_mouse(on);
    editor_session().relative_ = on;
}

} // namespace internal
} // namespace frontend::level_editor
