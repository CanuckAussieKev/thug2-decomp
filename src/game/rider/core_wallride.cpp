// The skater's core physics component: the wall ride state's update.

#include <cmath>
#include <cstdint>

#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/ride_tracker.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/trick_component.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/skater_sound.h"
#include "game/rider/matrix_queries.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/render/coll_obj.h"

namespace skater {

using namespace core_fields;

namespace {
constexpr float kAlongWallMin = 0.01F;

constexpr uint32_t kWallRideGravity = 0x0C617CAF;   // Wall_Ride_Gravity
constexpr uint32_t kWallRideTurnSpeed = 0x64A48A64; // Wall_Ride_Turn_Speed
constexpr uint32_t kForwardLength = 0x20102726;     // Skater_First_Forward_Collision_Length
constexpr uint32_t kGroundSnapUp = 0xE4D79235;      // Physics_Ground_Snap_Up
constexpr uint32_t kDownCheckLength = 0x438BA168;   // Wall_Ride_Down_Collision_Check_Length
constexpr uint32_t kJumpOutSpeed = 0x349D02D4;      // Wall_Ride_Jump_Out_Speed
constexpr uint32_t kJumpUpSpeed = 0x5A9DE35A;       // Wall_Ride_Jump_Up_Speed
constexpr uint32_t kMatrixQueries = 0xBF518E27;     // SkaterMatrixQueries (a component)
constexpr uint32_t kLanded = 0x532B16EF;            // Landed (event)
constexpr uint32_t kOffEdge = 0xD96F01F1;           // SkaterOffEdge (event)
constexpr uint32_t kGroundGone = 0x3B1001B6;        // GroundGone (event)

constexpr float kHeadingUp = 0.68F; // 0x0064BDB4: heading up the wall this much: no turning
constexpr float kTopFacesUp = 0.9F; // 0x00649FE4: a hit ahead facing up this much: the wall's top
constexpr float kPushOff = 18.0F;   // 0x0064AAAC
constexpr float kPushAway = 100.0F; // 0x006463D4

const maths::Vec4 kUpAxis = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};

// velocity or position += normal * scale, x and y from the unrounded products (as the original).
void add_scaled(maths::Vec4& to, const maths::Vec4& normal, float scale)
{
    const float z = normal.z * scale;
    const float w = normal.w * scale;
    to.x = static_cast<float>((static_cast<double>(normal.x) * scale) + to.x);
    to.y = static_cast<float>((static_cast<double>(normal.y) * scale) + to.y);
    to.z = z + to.z;
    to.w = w + to.w;
}

} // namespace

// (Part of 0x0050F830) Off the wall's edge: the up straight, pushed out 18 from the wall, into the air.
void SkaterPhysicsModule::off_wall_edge(bool reset_display)
{
    turn_to_normal(kUpAxis);
    if (reset_display) {
        display_matrix_ = entity()->basis_frame();
    }
    add_scaled(entity()->at_point(), wall_.normal, kPushOff);
    set_physics_state(kAir);
    static_cast<objects::Entity*>(object_of(this))->announce(kOffEdge, nullptr, 0.0F);
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kGroundGone, nullptr);
}

// (Part of 0x0050F830) What the wall riding skater rides on carries it by (its turn applied too).
maths::Vec4 SkaterPhysicsModule::carried_on_wall()
{
    maths::Vec4 carried = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity().get_pull() || !tracker->tick_frame(&entity()->at_point())) {
            stop_riding(this);
        } else {
            carried = get_tracker(this)->carried();
            if (get_tracker(this)->turned()) {
                maths::Mat4& basis = entity()->basis_frame();
                maths::matrix_multiply(&basis, &basis, &get_tracker(this)->turn());
                maths::matrix_multiply(&display_matrix_, &display_matrix_, &get_tracker(this)->turn());
            }
        }
    }
    return carried;
}

// 0x0050F830
void SkaterPhysicsModule::step_wall_ride()
{
    set_flag(this, PoseFlag::kAcidDrop, 0);
    set_flag(this, PoseFlag::kFromAcidDrop, 0);
    set_flag(this, PoseFlag::kRecoverInAir, 0);
    set_flag(this, PoseFlag::kNoAcidDrop, 0);
    flip_spin_started_ = 0;
    rail_node_ = nullptr;
    last_rail_script_ = 0;
    wall_.ride_start = get_now();

    // What the skater rides on carries it (its turn too).
    const maths::Vec4 carried = carried_on_wall();

    // The move: velocity * t + (0, gravity, 0, 1) * t * t / 2 (rounded as the original), plus the carry; then the
    // velocity += (0, gravity, 0, 1) * t.
    objects::GameEntity* object = object_of(this);
    const float gravity = tuning_float_lookup(kWallRideGravity, object);
    {
        const float frame_length = frame_length_;
        const double half = static_cast<double>(frame_length) * frame_length * 0.5F;
        const double zero_half = 0.0F * half;
        auto zero_half_rounded = static_cast<float>(zero_half);
        auto gravity_half = static_cast<float>(half * gravity);
        auto half_rounded = static_cast<float>(half);
        const maths::Vec4& v = entity()->vel();
        const double tx = static_cast<double>(frame_length) * v.x;
        const float ty = frame_length * v.y;
        const float tz = frame_length * v.z;
        const float tw = frame_length * v.w;
        auto mx = static_cast<float>(tx + zero_half_rounded);
        const float my = ty + gravity_half;
        const double mz = zero_half + tz;
        const double mw = static_cast<double>(tw) + half_rounded;
        maths::Vec4& position = entity()->at_point();
        position.x = mx + position.x;
        position.y = my + position.y;
        position.z = static_cast<float>(mz + position.z);
        position.w = static_cast<float>(mw + position.w);
        position.x = carried.x + position.x;
        position.y = carried.y + position.y;
        position.z = carried.z + position.z;
        position.w = carried.w + position.w;
        const float tt = frame_length_;
        const double zero_t = 0.0F * static_cast<double>(tt);
        const float fall = tt * gravity;
        maths::Vec4& u = entity()->vel();
        u.x = static_cast<float>(zero_t) + u.x;
        u.y = fall + u.y;
        u.z = static_cast<float>(zero_t + u.z);
        u.w = tt + u.w;
    }

    // Up the wall: the wall's level tangent (normalised) crossed with its normal.
    const maths::Vec4& wall = wall_.normal;
    double tangent_z = wall.z;
    double tangent_x = -static_cast<double>(wall.x);
    float tangent_y = 0.0F;
    const double tangent_length = std::sqrt((tangent_z * tangent_z) + (tangent_x * tangent_x));
    if (tangent_length > 0.0) {
        const double inverse = 1.0 / tangent_length;
        tangent_z *= inverse;
        tangent_y = static_cast<float>(0.0 * inverse);
        tangent_x *= inverse;
    }
    maths::Vec4 up_wall = carried;
    up_wall.x = static_cast<float>((tangent_y * wall.z) - (tangent_x * wall.y));
    up_wall.y = static_cast<float>((tangent_x * wall.x) - (tangent_z * wall.z));
    up_wall.z = static_cast<float>((tangent_z * wall.y) - (tangent_y * wall.x));

    // Turned towards up the wall when not heading up it enough (a turn's cosine at most).
    float turn = script::fetch_float(kWallRideTurnSpeed);
    const maths::Vec4& facing = core::at(entity()->basis_frame().row, 2);
    const double heading = ((static_cast<double>(up_wall.z) * facing.z) + (static_cast<double>(up_wall.y) * facing.y)) +
                           (static_cast<double>(up_wall.x) * facing.x);
    const bool turning = !(heading > kHeadingUp) && !(heading > 0.0 && heading > std::cos(static_cast<double>(turn)));
    if (turning) {
        const maths::Vec4& right = core::at(entity()->basis_frame().row, 0);
        const double side = ((static_cast<double>(up_wall.z) * right.z) + (static_cast<double>(up_wall.y) * right.y)) +
                            (static_cast<double>(up_wall.x) * right.x);
        if (side <= 0.0) {
            turn = -turn;
        }
        objects::GameEntity* skater = object_of(this);
        skater->vel().w = 1.0F;
        entity()->vel().rotate_about_axis(wall_.normal, turn); // (about the wall)
        skater = object_of(this);
        core::at(skater->basis_frame().row, 2) = skater->vel();
        core::at(entity()->basis_frame().row, 2).make_unit();
        skater = object_of(this);
        core::at(skater->basis_frame().row, 1) = wall_.normal;
        const maths::Vec4& forward = core::at(skater->basis_frame().row, 2);
        const maths::Vec4& u = core::at(skater->basis_frame().row, 1);
        maths::Vec4& right_row = core::at(skater->basis_frame().row, 0);
        auto rx = static_cast<float>((static_cast<double>(forward.z) * u.y) - (static_cast<double>(forward.y) * u.z));
        auto ry = static_cast<float>((static_cast<double>(forward.x) * u.z) - (static_cast<double>(forward.z) * u.x));
        auto rz = static_cast<float>((static_cast<double>(forward.y) * u.x) - (static_cast<double>(forward.x) * u.y));
        right_row.w = 0.0F;
        right_row.z = rz;
        right_row.x = rx;
        right_row.y = ry;
        core::at(entity()->basis_frame().row, 0).make_unit();
        display_matrix_ = entity()->basis_frame();
    }

    // A feeler ahead along the move (Skater_First_Forward_Collision_Length past the new position).
    {
        const objects::GameEntity* skater = object_of(this);
        const maths::Vec4& position = skater->at_point();
        const maths::Vec4& old = skater->old_pos();
        maths::Vec4 move = {
            .x = position.x - old.x, .y = position.y - old.y, .z = position.z - old.z, .w = position.w - old.w};
        const double moved =
            std::sqrt(((static_cast<double>(move.z) * move.z) + (static_cast<double>(move.y) * move.y)) +
                      (static_cast<double>(move.x) * move.x));
        if (moved > 0.0) {
            const double inverse = 1.0 / moved;
            move.x = static_cast<float>(move.x * inverse);
            move.y = static_cast<float>(move.y * inverse);
            move.z = static_cast<float>(inverse * move.z);
        }
        feelers_.probe_start = skater->old_pos();
        maths::Vec4& end = feelers_.probe_end;
        end = entity()->at_point();
        const float reach = tuning_float_lookup(kForwardLength, object_of(this));
        const double ahead_x = static_cast<double>(move.x) * reach;
        const float ahead_y = reach * move.y;
        const float ahead_z = reach * move.z;
        const float ahead_w = reach * move.w;
        end.x = static_cast<float>(ahead_x + end.x);
        end.y = ahead_y + end.y;
        end.z = ahead_z + end.z;
        end.w = ahead_w + end.w;
    }
    if (get_feel(this, render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
        const maths::Vec4 hit = feelers_.feeler.surface_dir();
        const maths::Vec4& w = wall_.normal;
        const double along_wall = ((static_cast<double>(hit.z) * w.z) + (static_cast<double>(hit.y) * w.y)) +
                                  (static_cast<double>(hit.x) * w.x);
        const maths::Vec4& point = feelers_.feeler.contact_pos();
        maths::Vec4& position = entity()->at_point();
        position.z = hit.z + point.z;
        position.w = hit.w + point.w;
        position.x = hit.x + point.x;
        position.y = hit.y + point.y;
        if ((feelers_.hit_wall != 0U) && !(std::fabs(along_wall) < kAlongWallMin)) {
            // Round a corner: the new wall.
            wall_.normal = hit;
            turn_to_normal(wall_.normal);
            display_matrix_ = entity()->basis_frame();
            entity()->vel().align_to_plane(wall_.normal);
        } else if (hit.y > kTopFacesUp) {
            // Onto the top: the ground (a grind first), upright on it, heading along the velocity.
            objects::GameEntity* landed_skater = object_of(this);
            landed_skater->at_point() = feelers_.feeler.contact_pos();
            core::at(landed_skater->basis_frame().row, 2) = landed_skater->vel();
            core::at(entity()->basis_frame().row, 2).flatten_onto_plane(hit);
            core::at(entity()->basis_frame().row, 2).make_unit();
            core::at(entity()->basis_frame().row, 1) = hit;
            objects::GameEntity* skater = object_of(this);
            core::at(skater->basis_frame().row, 0) =
                maths::cross_vec(core::at(skater->basis_frame().row, 1), core::at(skater->basis_frame().row, 2));
            core::at(entity()->basis_frame().row, 0).make_unit();
            display_matrix_ = entity()->basis_frame();
            auto* queries = core::down_cast<objects::RiderFrameLookups>(
                static_cast<objects::GameEntity*>(object_of(this))->fetch_module(kMatrixQueries));
            queries->set_basis_frame(queries->entity()->basis_frame());
            old_normal_ = core::at(entity()->basis_frame().row, 1);
            drawn_normal_ = core::at(entity()->basis_frame().row, 1);
            floor_normal_ = core::at(entity()->basis_frame().row, 1);
            try_grind(true);
            if (state_of(this) == kRail) {
                return;
            }
            set_physics_state(kGround);
            static_cast<objects::Entity*>(object_of(this))->post_own_event(kLanded, nullptr);
            return;
        } else {
            off_wall_edge(false);
        }
    }
    if (keep_off_ceiling_at_wall()) {
        off_wall_edge(false);
    }

    // A feeler down the skater's up: still on the wall?
    {
        void* tuning_owner = object;
        const float snap = tuning_float_lookup(kGroundSnapUp, tuning_owner);
        const maths::Vec4& up = core::at(entity()->basis_frame().row, 1);
        maths::Vec4& start = feelers_.probe_start;
        start = {.x = snap * up.x, .y = snap * up.y, .z = snap * up.z, .w = snap * up.w};
        const maths::Vec4& start_position = entity()->at_point();
        start.x = start.x + start_position.x, start.y = start_position.y + start.y,
        start.z = start_position.z + start.z, start.w = start_position.w + start.w;
        const float down = script::fetch_float(kDownCheckLength);
        const maths::Vec4& up2 = core::at(entity()->basis_frame().row, 1);
        maths::Vec4& end = feelers_.probe_end;
        end = {.x = down * up2.x, .y = down * up2.y, .z = down * up2.z, .w = down * up2.w};
        const maths::Vec4& end_position = entity()->at_point();
        end.x = end.x + end_position.x, end.y = end_position.y + end.y, end.z = end_position.z + end.z,
        end.w = end_position.w + end.w;
    }
    if (get_feel(this, render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags)) {
        // At the wall (just off it), along it.
        wall_.normal = feelers_.feeler.surface_dir();
        objects::GameEntity* wall_skater = object_of(this);
        wall_skater->at_point() = feelers_.feeler.contact_pos();
        maths::Vec4& skater_position = entity()->at_point();
        const maths::Vec4& w = wall_.normal;
        skater_position.x = skater_position.x + w.x, skater_position.y = w.y + skater_position.y,
        skater_position.z = w.z + skater_position.z, skater_position.w = w.w + skater_position.w;
        entity()->vel().align_to_plane(wall_.normal);
        turn_to_normal(wall_.normal);
        display_matrix_ = entity()->basis_frame();
        if (feelers_.hit_wall == 0U) {
            // Not a wall to ride: pushed away from it into the air.
            add_scaled(entity()->vel(), wall_.normal, kPushAway);
            turn_to_normal(kUpAxis);
            display_matrix_ = entity()->basis_frame();
            set_physics_state(kAir);
            static_cast<objects::Entity*>(object_of(this))->announce(kOffEdge, nullptr, 0.0F);
            static_cast<objects::Entity*>(object_of(this))->post_own_event(kGroundGone, nullptr);
            trigger_feeler(this, kOtherFeelerLeaveEvents, &feelers_.floor_feeler);
            if (trigger_took_over(this)) {
                return;
            }
        } else {
            if (feelers_.feeler.hit().object != feelers_.floor_feeler.hit().object) {
                trigger_feeler(this, kOtherFeelerLeaveEvents, &feelers_.floor_feeler);
                if (trigger_took_over(this)) {
                    return;
                }
                trigger_feeler(this, kSkaterEvents0064bBb0, &feelers_.floor_feeler);
                if (trigger_took_over(this)) {
                    return;
                }
            }
            objects::MoveExecutor* skater = trick_;
            if (!skater->trick_objects_on()) {
                skater->set_trick_objects(false);
            }
            assign_feeler(&feelers_.floor_feeler, &feelers_.feeler);
            sound_->set_last_ground(feelers_.floor_feeler.hit().terrain);
        }
    } else {
        off_wall_edge(true);
        trigger_feeler(this, kOtherFeelerLeaveEvents, &feelers_.floor_feeler);
        if (trigger_took_over(this)) {
            return;
        }
    }

    if ((bit_mark(this, PoseFlag::kCrouched) == 0U) && (input_->pad_primary().pos_x().held_down() != 0U)) {
        set_flag(this, PoseFlag::kCrouched, 1);
    }
    // Jumping off: out from the wall and up, upright.
    if (jump_released()) {
        set_flag(this, PoseFlag::kJumped, 1);
        trigger_feeler(this, kJumpTriggerEvents, &feelers_.floor_feeler);
        if (trigger_took_over(this)) {
            return;
        }
        const float out = script::fetch_float(kJumpOutSpeed);
        maths::Vec4& v = entity()->vel();
        const maths::Vec4& w = wall_.normal;
        const float y = out * w.y;
        const float z = out * w.z;
        const float ww = out * w.w;
        v.x = static_cast<float>((static_cast<double>(out) * w.x) + v.x);
        v.y = y + v.y;
        v.z = z + v.z;
        v.w = ww + v.w;
        float& vy = entity()->vel().y;
        vy = script::fetch_float(kJumpUpSpeed) + vy;
        core::at(entity()->basis_frame().row, 1) = kUpAxis;
        entity()->basis_frame().square_up_axes(1);
        display_matrix_ = entity()->basis_frame();
        normal_blend_ = 0.0F;
        drawn_normal_ = core::at(entity()->basis_frame().row, 1);
        floor_normal_ = drawn_normal_;
        old_normal_ = drawn_normal_;
    }
    static_cast<objects::MoveExecutor*>(trick_)->pass_to_trick_objects(feelers_.floor_feeler.hit().object_checksum_);
}

} // namespace skater
