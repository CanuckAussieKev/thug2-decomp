// The skater's core physics component: grinding - the rail state's update and moving on from a rail's end.

#include <cmath>
#include <cstdint>

#include "base/maths/maths.h"
#include "base/core/enum_ops.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/walk_lerp.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/manual_balance.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "game/skate/point_tally.h"
#include "game/park_editing/park_editor.h"
#include "game/skate/skate_module.h"
#include "game/online/gamenet_manager.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/balance_trick.h"
#include "game/rider/skater_score.h"
#include "base/core/checked_index.h"
#include "game/online/net_game_state.h"
#include "engine/render/coll_obj.h"

namespace skater {

using namespace core_fields;

namespace {
constexpr float kMidpointDivisor = 2.0F;
constexpr float kSegmentSlack = 0.1F;
constexpr float kSameWayTolerance = 0.01F;

constexpr uint32_t kCornerLeaveAngle = 0x76C1DA15; // Rail_Corner_Leave_Angle
constexpr uint32_t kRailRegrabDelay = 0x2B4645AD;  // Rail_minimum_rerail_time
constexpr uint32_t kJumpRerailTime = 0x0BF35053;   // Rail_jump_rerail_time
constexpr uint32_t kRailGravity = 0xD1F46992;      // Physics_Rail_Gravity
constexpr uint32_t kRailManagerType = 0x1CDFB2A3;  // RailManager (a component)
constexpr uint32_t kGrind = 0x255ED86F;            // Grind (the balance trick's kind)
constexpr uint32_t kSlide = 0x8D10119D;            // Slide
constexpr uint32_t kPerfectRail = 0xCD09E062;      // CHEAT_PERFECT_RAIL
constexpr uint32_t kOffRail = 0xAFAA46BA;          // OffRail (event)
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kParkEditorLevel = 0xD443A2BC;

constexpr float kDegrees = 0.0174532924F;
constexpr float kRaise = 6.0F;       // 0x00646284: the transfer line's start raised
constexpr float kRaiseEnd = 6.1F;    // 0x0064BDA8: its end
constexpr float kFeelerAhead = 6.0F; // (the feeler past the move: 6 along it)
constexpr float kTurnFacing = 0.3F;  // (how far the display facing turns to the rail each frame)

bool in_park_editor() { return park_editing::ParkBuilderMode::sole()->editor_state() == kParkEditorLevel; }

// -1 for a dot product below 0, else 1 (NaN: 1).
float sign_of(double dot) { return dot < 0.0 ? -1.0F : 1.0F; }
double dot3(const maths::Vec4& a, const maths::Vec4& v)
{
    return ((static_cast<double>(a.z) * v.z) + (static_cast<double>(a.y) * v.y)) + (static_cast<double>(a.x) * v.x);
}

// A node rails go on from (flag 4 - its >> 2 & 1).
bool continues(const objects::GrindPoint* node) { return ((node->flag_bits() >> 2) & 1) != 0U; }

// The corner from segment `a` (to - from) into segment `b` (level): turning less than Rail_Corner_Leave_Angle.
bool gentle_corner(maths::Vec4 a, maths::Vec4 b)
{
    a.y = 0.0F;
    b.y = 0.0F;
    a.make_unit();
    b.make_unit();
    auto turn = static_cast<float>(((static_cast<double>(b.z) * a.z) + (static_cast<double>(b.y) * a.y)) +
                                   (static_cast<double>(b.x) * a.x));
    return !(std::cos(static_cast<double>(script::fetch_float(kCornerLeaveAngle)) * kDegrees) > turn);
}

} // namespace

maths::Vec4 SkaterPhysicsModule::Rail::grind_point_in_world(const objects::GrindPoint* point) const
{
    maths::Vec4 out;
    return *manager->get_absolute_pos(&out, point);
}

// 0x0050C200
void SkaterPhysicsModule::transfer_or_leave_rail(const maths::Vec4* end)
{
    // The line looked along: from `end` (in the park editor: back along the velocity) to the skater, raised.
    const objects::GameEntity* object = object_of(this);
    maths::Vec4 start;
    if (!in_park_editor()) {
        start = *end;
    } else {
        const maths::Vec4& position = object->at_point();
        const maths::Vec4& v = object->vel();
        const float frame_length = frame_.length;
        const float bx = frame_length * v.x;
        const float by = frame_length * v.y;
        const float bz = frame_length * v.z;
        const double bw = static_cast<double>(frame_length) * v.w;
        start.x = position.x - bx;
        start.y = position.y - by;
        start.z = position.z - bz;
        start.w = static_cast<float>(position.w - bw);
    }
    start.y = start.y + kRaise;
    maths::Vec4 stop = entity()->at_point();
    stop.y = stop.y + kRaiseEnd;
    const float angle = script::fetch_float(kCornerLeaveAngle);
    auto* node = rail_.node;
    const int side = node->get_wall_side(&entity()->vel());
    auto alignment = static_cast<float>(std::cos(static_cast<double>(angle) * kDegrees));
    maths::Vec4 point;
    objects::GrindPoint* found = nullptr;
    bool onto = rail_.manager->find_grind_rail(&start, &stop, &point, &found, rail_.node,
                                               {.min_alignment_ = alignment, .side = side}) &&
                (found != nullptr) && found != rail_.node && (found->succ() != nullptr);
    if (onto && !in_park_editor()) {
        // (Not this rail's next or last node.)
        auto* current = rail_.node;
        onto = current->succ() != found && current->last_value() != found;
    }
    if (onto) {
        const objects::GrindPoint* next = found->succ();
        const maths::Vec4& position = entity()->at_point();
        const maths::Vec4 found_pos = rail_.grind_point_in_world(found);
        const maths::Vec4 to_found = found_pos - position;
        const maths::Vec4 next_pos = rail_.grind_point_in_world(next);
        const maths::Vec4 to_next = next_pos - entity()->at_point();
        const double toward =
            ((static_cast<double>(to_next.x) * to_found.x) + (static_cast<double>(to_next.z) * to_found.z)) +
            (static_cast<double>(to_next.y) * to_found.y);
        auto toward_rounded = static_cast<float>(toward);
        if (!(toward < 0.0)) {
            // Not between its two nodes: never in the park editor; elsewhere only when the move reaches as far as
            // the segment is long - then onto its middle.
            if (!in_park_editor()) {
                const maths::Vec4 found_point = rail_.grind_point_in_world(found);
                const maths::Vec4 next_point = rail_.grind_point_in_world(next);
                const maths::Vec4 segment = next_point - found_point;
                const maths::Vec4 move = stop - start;
                auto segment_length = static_cast<float>(length_of(segment));
                onto = !(length_of(move) < segment_length);
                if (onto && toward_rounded >= 0.0F) {
                    const maths::Vec4 middle_from = rail_.grind_point_in_world(found);
                    const maths::Vec4 middle_to = rail_.grind_point_in_world(next);
                    point = (middle_to + middle_from) / kMidpointDivisor;
                }
            } else {
                onto = false;
            }
        }
    }
    if (!onto) {
        trigger_rail_script(kGrindOffEvents);
        set_physics_state(kAir);
        maths::Vec4& position = entity()->at_point();
        position.y = position.y + 1.0F;
        static_cast<objects::Entity*>(object_of(this))->post_own_event(kOffRail, nullptr);
        return;
    }
    // Onto it: the velocity along it (the same way), the skater at the point.
    const objects::GrindPoint* next = found->succ();
    const maths::Vec4 found_point = rail_.grind_point_in_world(found);
    const maths::Vec4 next_point = rail_.grind_point_in_world(next);
    maths::Vec4 direction = next_point - found_point;
    direction.make_unit();
    maths::Vec4& v = entity()->vel();
    if (v.x == 0.0F && v.z == 0.0F) {
        v.x = core::at(entity()->basis_frame().row, 2).x;
        entity()->vel().z = core::at(entity()->basis_frame().row, 2).z;
    }
    maths::Vec4& velocity = entity()->vel();
    const float sign = maths::get_sign(static_cast<float>(dot3(direction, velocity)));
    velocity.change_heading(direction);
    entity()->vel() *= sign;
    rail_.node = found;
    entity()->at_point() = point;
    trigger_rail_script(kGrindEvents);
    set_surface_kind(rail_.node->surface_type());
}

// 0x0050DDF0
void SkaterPhysicsModule::step_grind_move()
{
    set_flag(this, PoseFlag::kAcidDrop, 0);
    set_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_flag(this, PoseFlag::kRecoverInAir, 0);
    set_flag(this, PoseFlag::kNoAcidDrop, 0);
    model_rotation_.flip_spin_started = 0;
    ride_moving_rail();

    objects::GameEntity* object = object_of(this);
    rail_.wait = static_cast<int>(script::fetch_float(kRailRegrabDelay));
    set_flag(this, PoseFlag::kLipToAir, 0);
    // Off the rail (0x004FFB10): the TriggerScripts, the jump's rerail time, up and out.
    if (jump_released()) {
        trigger_rail_script(kJumpTriggerEvents);
        trigger_rail_script(kGrindOffEvents);
        rail_.wait = static_cast<int>(script::fetch_float(kJumpRerailTime));
        set_flag(this, PoseFlag::kJumpedOffRail, 1);
        set_flag(this, PoseFlag::kJumped, 1);
        maths::Vec4& position = entity()->at_point();
        position.y = position.y + 1.0F;
        finish_rail();
        if (thaw_bmx::live_now(this)) {
            thaw_bmx::rail_stall_slip(this); // (not original: THAW 0x005E7074 - the bike's stall)
        }
        return;
    }
    update_grind_balance();

    auto* node = rail_.node;
    objects::GrindPoint* const start_node = node;
    objects::GrindPoint* next = node->succ();
    objects::GrindPoint* go_on_to = nullptr;
    if (!next) {
        // The rail's last node.
        natas_spin();
        return;
    }

    // The segment's direction (x scaled by the unrounded inverse length, the rest by it rounded - as the original).
    const maths::Vec4 from = rail_.grind_point_in_world(node);
    const maths::Vec4 to = rail_.grind_point_in_world(next);
    const double dx = static_cast<double>(to.x) - from.x;
    const double dy = static_cast<double>(to.y) - from.y;
    const double dz = static_cast<double>(to.z) - from.z;
    const double dw = static_cast<double>(to.w) - from.w;
    auto segment_length = static_cast<float>(std::sqrt(((dx * dx) + (dy * dy)) + (dz * dz)));
    const double inverse = 1.0 / segment_length;
    auto inverse_rounded = static_cast<float>(inverse);
    const maths::Vec4 along = {.x = static_cast<float>(inverse * dx), .y = static_cast<float>(inverse_rounded * dy),
                               .z = static_cast<float>(inverse_rounded * dz), .w = static_cast<float>(inverse_rounded * dw)};

    // The rail's gravity along it; turned back by it: the other way round (+0x34C).
    const float before = sign_of(dot3(along, entity()->vel()));
    maths::Vec4 gravity = {.x = 0.0F, .y = tuning_float_lookup(kRailGravity, object), .z = 0.0F, .w = 1.0F};
    gravity.project_onto(along);
    {
        const float frame_length = frame_.length;
        maths::Vec4& v = entity()->vel();
        const float gy = frame_length * gravity.y;
        const float gz = frame_length * gravity.z;
        const float gw = frame_length * gravity.w;
        v.x = static_cast<float>((static_cast<double>(gravity.x) * frame_length) + v.x);
        v.y = gy + v.y;
        v.z = gz + v.z;
        v.w = gw + v.w;
    }
    const float after = sign_of(dot3(along, entity()->vel()));
    if (before != after) {
        rail_backwards_ = static_cast<uint8_t>(rail_backwards_ == 0U);
    }

    // The segment to go on to that way (its node flagged 4, the corner gentle enough) - or none.
    bool no_next_segment = false;
    if (after < 0.0F) {
        const objects::GrindPoint* prev = node->last_value();
        if (prev && continues(prev)) {
            const maths::Vec4 node_point = rail_.grind_point_in_world(node);
            const maths::Vec4 next_point = rail_.grind_point_in_world(next);
            const maths::Vec4 ahead = next_point - node_point;
            const maths::Vec4 prev_point = rail_.grind_point_in_world(prev);
            const maths::Vec4 corner_point = rail_.grind_point_in_world(node);
            const maths::Vec4 behind = corner_point - prev_point;
            if (gentle_corner(ahead, behind)) {
                go_on_to = node->last_value();
            } else {
                no_next_segment = true;
            }
        } else {
            no_next_segment = true;
        }
    } else {
        const objects::GrindPoint* beyond = next->succ();
        if (beyond && continues(beyond)) {
            const maths::Vec4 next_point = rail_.grind_point_in_world(next);
            const maths::Vec4 node_point = rail_.grind_point_in_world(node);
            const maths::Vec4 back = node_point - next_point;
            const maths::Vec4 beyond_point = rail_.grind_point_in_world(next->succ());
            const maths::Vec4 corner_point = rail_.grind_point_in_world(next);
            const maths::Vec4 back_further = corner_point - beyond_point;
            if (gentle_corner(back, back_further)) {
                go_on_to = next;
            } else {
                no_next_segment = true;
            }
        } else {
            no_next_segment = true;
        }
    }

    // Past the segment's end (measured from the node it started at): onto the next segment, else on from the rail.
    float overshoot = 0.0F;
    {
        const objects::GrindPoint* from_node = after < 0.0F ? next : node;
        const maths::Vec4& position = entity()->at_point();
        const maths::Vec4 from_point = rail_.grind_point_in_world(from_node);
        const double ex = static_cast<double>(position.x) - from_point.x;
        const double ey = static_cast<double>(position.y) - from_point.y;
        const double ez = static_cast<double>(position.z) - from_point.z;
        const double distance = std::sqrt(((ez * ez) + (ey * ey)) + (ex * ex));
        if (distance > static_cast<double>(segment_length) + kSegmentSlack) {
            overshoot = static_cast<float>(distance - segment_length);
            const objects::GrindPoint* corner = after < 0.0F ? node : next; // the node passed
            const objects::GrindPoint* onward =
                after < 0.0F ? node->last_value() : next->succ(); // the next segment's far node
            const objects::GrindPoint* flagged =
                after < 0.0F ? node->last_value() : next; // the node whose flag says go on
            bool clear = false;
            if (onward && continues(flagged)) {
                const maths::Vec4 far = rail_.grind_point_in_world(onward);
                const maths::Vec4 near = rail_.grind_point_in_world(corner);
                clear = objects::rail_clear(near, far);
            }
            if (clear && !no_next_segment) {
                entity()->at_point() = rail_.grind_point_in_world(corner);
                rail_.node = after < 0.0F ? node->last_value() : next;
                set_surface_kind(rail_.node->surface_type());
                trigger_rail_script(kGrindEvents);
            } else {
                const maths::Vec4 end = rail_.grind_point_in_world(corner);
                transfer_or_leave_rail(&end);
            }
        }
    }
    if (state_of(this) != kRail) {
        finish_rail();
        if (thaw_bmx::live_now(this)) {
            thaw_bmx::rail_stall_slip(this); // (not original, as above)
        }
        return;
    }

    grind_along(start_node, go_on_to, no_next_segment, overshoot);
    finish_rail();
    if (thaw_bmx::live_now(this)) {
        thaw_bmx::rail_stall_slip(this); // (not original, as above)
    }
}

// (Part of 0x0050DDF0) A moving rail: its matrix asked for (0x0042BA10 - not used), then carried along with it.
void SkaterPhysicsModule::ride_moving_rail()
{
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity().get_pull()) {
            stop_riding(this);
        } else {
            void* rails = static_cast<objects::GameEntity*>(get_tracker(this)->entity().get_pull())
                              ->fetch_module(kRailManagerType);
            maths::Mat4 matrix;
            static_cast<objects::GrindPathLinkModule*>(rails)->update_rail_matrix(&matrix);
            tracker = get_tracker(this);
            if (tracker) {
                if (!tracker->entity().get_pull() || !tracker->tick_frame(&entity()->at_point())) {
                    stop_riding(this);
                } else {
                    entity()->at_point() += get_tracker(this)->carried();
                    if (get_tracker(this)->turned()) {
                        maths::Mat4& basis = entity()->basis_frame();
                        maths::matrix_multiply(&basis, &basis, &get_tracker(this)->turn());
                        maths::matrix_multiply(&ground_.display_matrix, &ground_.display_matrix,
                                               &get_tracker(this)->turn());
                    }
                }
            }
        }
    }
}

// (Part of 0x0050DDF0) A grind or a slide: its balance (+0x7C of the balance trick component), the Perfect Rail cheat.
void SkaterPhysicsModule::update_grind_balance()
{
    const uint32_t kind = balance_->trick();
    if (kind == kGrind || kind == kSlide) {
        balance_->grind().tick_frame();
        auto career = [] { return modules::Skate::sole()->career().get(); };
        if (career()->is_code_on(kPerfectRail)) {
            career()->set_global_flag(script::fetch_int_value(kPerfectRail));
            balance_->grind().set_lean(0.0F);
            balance_->grind().set_lean_speed(0.0F);
            online::net_game_state().set_cheat_flag(true);
        }
    }
}

// (Part of 0x0050DDF0) Still on the rail: the velocity along the segment, the facing turned to it, the move (and
// what went past the last segment's end), then a feeler ahead along the move.
void SkaterPhysicsModule::grind_along(objects::GrindPoint* start_node, objects::GrindPoint* go_on_to,
                                      bool no_next_segment, float overshoot)
{
    auto* node = rail_.node;
    const objects::GrindPoint* next = node->succ();
    const maths::Vec4 from = rail_.grind_point_in_world(node);
    const maths::Vec4 to = rail_.grind_point_in_world(next);
    maths::Vec4 direction = {.x = to.x - from.x, .y = to.y - from.y, .z = to.z - from.z, .w = to.w - from.w};
    direction.make_unit();
    const float sign = sign_of(dot3(direction, entity()->vel()));
    rail_.time = get_now();
    entity()->vel().change_heading(direction);
    maths::Vec4& v = entity()->vel();
    v.x = sign * v.x;
    v.y = sign * v.y;
    v.z = sign * v.z;
    v.w = sign * v.w;

    // The facing: along the rail (the other way round with +0x34C); the display facing (+0x38) turned 0.3 of the way
    // to it, the display and the object's matrices built around them (level rights).
    const float way = (rail_backwards_ != 0U) ? -sign : sign;
    const maths::Vec4 facing = {.x = direction.x * way, .y = direction.y * way, .z = direction.z * way, .w = way * direction.w};
    maths::Vec4& display = core::at(ground_.display_matrix.row, 2);
    maths::Vec4 turned;
    walk::get_lerp(&turned, &display, &facing, kTurnFacing);
    display = turned;
    display.make_unit();
    maths::Vec4& right = core::at(ground_.display_matrix.row, 0);
    right.x = core::at(ground_.display_matrix.row, 2).z;
    right.z = -display.x;
    right.y = 0.0F;
    right.w = 0.0F;
    right.make_unit();
    core::at(ground_.display_matrix.row, 1) = maths::cross_vec(display, right);
    core::at(ground_.display_matrix.row, 1).make_unit();
    objects::GameEntity* object = object_of(this);
    core::at(object->basis_frame().row, 2) = facing;
    core::at(entity()->basis_frame().row, 2).make_unit();
    object = object_of(this);
    maths::Vec4& object_right = core::at(object->basis_frame().row, 0);
    const float fz = core::at(object->basis_frame().row, 2).z;
    object_right.z = -core::at(object->basis_frame().row, 2).x;
    object_right.x = fz;
    object_right.y = 0.0F;
    object_right.w = 0.0F;
    core::at(entity()->basis_frame().row, 0).make_unit();
    object = object_of(this);
    core::at(object->basis_frame().row, 1) = maths::cross_vec(core::at(object->basis_frame().row, 2), core::at(object->basis_frame().row, 0));
    core::at(entity()->basis_frame().row, 1).make_unit();

    // What went past the last segment's end is only carried on when this segment is longer.
    const maths::Vec4 node_point = rail_.grind_point_in_world(node);
    const maths::Vec4 next_point = rail_.grind_point_in_world(next);
    const double sx = static_cast<double>(next_point.x) - node_point.x;
    const double sy = static_cast<double>(next_point.y) - node_point.y;
    const double sz = static_cast<double>(next_point.z) - node_point.z;
    const double length = std::sqrt(((sx * sx) + (sz * sz)) + (sy * sy));
    if (length <= overshoot) {
        overshoot = 0.0F;
    }

    // The move: the velocity over the frame, plus the overshoot along the facing (x, y of both unrounded).
    maths::Vec4& skater_position = entity()->at_point();
    const maths::Vec4 old = skater_position;
    {
        const float frame_length = frame_.length;
        const maths::Vec4& u = entity()->vel();
        const float my = frame_length * u.y;
        const float mz = frame_length * u.z;
        const float mw = frame_length * u.w;
        skater_position.x = static_cast<float>((static_cast<double>(frame_length) * u.x) + skater_position.x);
        skater_position.y = my + skater_position.y;
        skater_position.z = mz + skater_position.z;
        skater_position.w = mw + skater_position.w;
        maths::Vec4& overshot_position = entity()->at_point();
        const float oz = facing.z * overshoot;
        const float ow = facing.w * overshoot;
        overshot_position.x = static_cast<float>((static_cast<double>(facing.x) * overshoot) + overshot_position.x);
        overshot_position.y = static_cast<float>((static_cast<double>(facing.y) * overshoot) + overshot_position.y);
        overshot_position.z = oz + overshot_position.z;
        overshot_position.w = ow + overshot_position.w;
        overshot_position.w = overshot_position.w + 1.0F;
    }
    const maths::Vec4& now = entity()->at_point();
    const maths::Vec4 move = {.x = now.x - old.x, .y = now.y - old.y, .z = now.z - old.z, .w = now.w - old.w};
    maths::Vec4 move_direction = move;
    move_direction.make_unit();

    // Going on along the rail: only (not in the park editor) when the next segment climbs as this one does.
    if (!no_next_segment) {
        if (in_park_editor()) {
            return;
        }
        const maths::Vec4 start_point = rail_.grind_point_in_world(start_node);
        const maths::Vec4 start_next_point = rail_.grind_point_in_world(start_node->succ());
        maths::Vec4 this_way = start_next_point - start_point;
        const maths::Vec4 go_on_point = rail_.grind_point_in_world(go_on_to);
        const maths::Vec4 go_on_next_point = rail_.grind_point_in_world(go_on_to->succ());
        maths::Vec4 that_way = go_on_next_point - go_on_point;
        this_way.make_unit();
        that_way.make_unit();
        if (!(std::fabs(static_cast<double>(this_way.y) - that_way.y) < kSameWayTolerance)) {
            return;
        }
    }

    // The feeler: from the last position to 6 past the new one along the move, raised by the up.
    maths::Vec4& start = feelers_.probe_start;
    maths::Vec4& end = feelers_.probe_end;
    const double ahead_x = static_cast<double>(move_direction.x) * kFeelerAhead;
    const double ahead_y = static_cast<double>(move_direction.y) * kFeelerAhead;
    const float ahead_z = move_direction.z * kFeelerAhead;
    const float ahead_w = move_direction.w * kFeelerAhead;
    start = old;
    end = entity()->at_point();
    auto extend_x = static_cast<float>(ahead_x + move.x);
    const double extend_y = ahead_y + move.y;
    const double extend_z = static_cast<double>(ahead_z) + move.z;
    const float extend_w = ahead_w + move.w;
    end.x = extend_x + end.x;
    end.y = static_cast<float>(extend_y + end.y);
    end.z = static_cast<float>(extend_z + end.z);
    end.w = extend_w + end.w;
    const maths::Vec4& up = core::at(entity()->basis_frame().row, 1);
    start.x = up.x + start.x, start.y = up.y + start.y, start.z = up.z + start.z, start.w = up.w + start.w;
    const maths::Vec4& up2 = core::at(entity()->basis_frame().row, 1);
    end.x = end.x + up2.x, end.y = end.y + up2.y, end.z = end.z + up2.z, end.w = end.w + up2.w;
    if (!get_feel(this, render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
        return;
    }
    // Something ahead: off the rail (not for faces flagged 0x10 at +0xF9, outside the park editor).
    if (!in_park_editor() && core::any(feelers_.feeler.hit().face_flags_ & render::FaceFlag::kNoParkEdit)) {
        return;
    }
    trigger_rail_script(kGrindOffEvents);
    objects::GameEntity* skater = object_of(this);
    skater->at_point() = skater->old_pos();
    maths::Vec4& position = entity()->at_point();
    position.y = position.y + 1.0F;
    entity()->vel().flatten_onto_plane(feelers_.feeler.surface_dir());
    set_physics_state(kAir);
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kOffRail, nullptr);
}

// (Part of 0x0050DDF0) Every way out of the update (but the rail's last node): the ground normal, the score's rail
// time (0x00564270, +0x3A8 scaled), the node's Name.
void SkaterPhysicsModule::finish_rail()
{
    ground_.floor_normal = core::at(entity()->basis_frame().row, 1);
    modules::PointTally* score = score_->points_held();
    const int time = static_cast<int>(static_cast<double>(rail_adjust_) * score->robot_factor());
    score->tweak_trick(time);
    if (rail_.manager->node_array()) {
        script::Struct* script = level_nodes::fetch_vertex(rail_.node->label(), true);
        // (0x00476EC0: read_hash_value with a bool for its last argument - the wrapper passes it on as 1)
        script->read_hash_value(kName, &rail_.script_node, 1);
        static_cast<objects::MoveExecutor*>(trick_)->pass_to_trick_objects(rail_.script_node);
    }
}

// Not original: THAW's stall state (THAW 0x005E2E30; game/rider/thaw_bmx.h - its bike's, R1 held as a grind
// starts): the rail state's update around a bike held still on the rail, facing along it. Update runs it in that state
// (ThawBmx::grind_stall / rail_stall_slip set it) while the feature is on.
void SkaterPhysicsModule::do_bike_stall_physics()
{
    // As the rail state's: the flags (THAW clears a fifth of its own), the moving rail, the rerail time.
    set_flag(this, PoseFlag::kAcidDrop, 0);
    set_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_flag(this, PoseFlag::kRecoverInAir, 0);
    set_flag(this, PoseFlag::kNoAcidDrop, 0);
    model_rotation_.flip_spin_started = 0;
    ride_moving_rail();
    rail_.wait = static_cast<int>(script::fetch_float(kRailRegrabDelay));
    set_flag(this, PoseFlag::kLipToAir, 0);
    if (jump_released()) {
        // Jumped (0x005E3053): the rail's TriggerScripts (THAW's with its stall event, which THUG2's haven't), the
        // jump's rerail time, up.
        trigger_rail_script(kJumpTriggerEvents);
        trigger_rail_script(kGrindOffEvents);
        rail_.wait = static_cast<int>(script::fetch_float(kJumpRerailTime));
        set_flag(this, PoseFlag::kJumpedOffRail, 1);
        set_flag(this, PoseFlag::kJumped, 1);
        maths::Vec4& position = entity()->at_point();
        position.y = position.y + 1.0F;
    } else {
        update_grind_balance();
        // (THAW draws the segment here with rail_highlights on - a debug view of its own)
        auto* node = rail_.node;
        const objects::GrindPoint* next = node->succ();
        if (!next) {
            return;
        }
        entity()->vel() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        if (state_of(this) == kBikeStall) {
            // 0x005E34AE: the facing along the segment the way it points, the display facing turned 0.3 of the way
            // to it and the matrices built round them as a grind's (grind_along); no move.
            const maths::Vec4 from = rail_.grind_point_in_world(node);
            const maths::Vec4 to = rail_.grind_point_in_world(next);
            maths::Vec4 direction = {.x = to.x - from.x, .y = to.y - from.y, .z = to.z - from.z, .w = to.w - from.w};
            direction.make_unit();
            rail_.time = get_now();
            const maths::Vec4 facing = thaw_bmx::bike::stall_facing(core::at(entity()->basis_frame().row, 2), direction);
            maths::Vec4& display = core::at(ground_.display_matrix.row, 2);
            maths::Vec4 turned;
            walk::get_lerp(&turned, &display, &facing, kTurnFacing);
            display = turned;
            display.make_unit();
            maths::Vec4& right = core::at(ground_.display_matrix.row, 0);
            right.x = core::at(ground_.display_matrix.row, 2).z;
            right.z = -display.x;
            right.y = 0.0F;
            right.w = 0.0F;
            right.make_unit();
            core::at(ground_.display_matrix.row, 1) = maths::cross_vec(display, right);
            core::at(ground_.display_matrix.row, 1).make_unit();
            objects::GameEntity* object = object_of(this);
            core::at(object->basis_frame().row, 2) = facing;
            core::at(object->basis_frame().row, 2).make_unit();
            maths::Vec4& object_right = core::at(object->basis_frame().row, 0);
            const float fz = core::at(object->basis_frame().row, 2).z;
            object_right.z = -core::at(object->basis_frame().row, 2).x;
            object_right.x = fz;
            object_right.y = 0.0F;
            object_right.w = 0.0F;
            object_right.make_unit();
            core::at(object->basis_frame().row, 1) = maths::cross_vec(core::at(object->basis_frame().row, 2), object_right);
            core::at(object->basis_frame().row, 1).make_unit();
            object->vel() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        }
    }
    // 0x005E37FE: on a rail too steep, grinding again; the ground normal the up, the score's rail time, the node's name.
    thaw_bmx::stall_steep_rail(this);
    finish_rail();
}

} // namespace skater
