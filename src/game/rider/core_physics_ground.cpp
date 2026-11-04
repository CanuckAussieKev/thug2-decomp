// The skater core physics component: friction, braking and pushing off, the ground's normal and going up vert
// (0x004FE1F0-0x004FEE0B).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/walk_lerp.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/rider.h"
#include "game/park_editing/park_editor.h"
#include "game/rider/skater_state.h"
#include "base/core/checked_index.h"

namespace skater {

using namespace physics;

namespace {
constexpr float kVertStep = 6.0F;

// Physics values, stats and script values (names not recovered unless given).
constexpr uint32_t kSlopeTurnSpeed = 0x62A1FA03;
constexpr uint32_t kSlopeTurnNormal = 0xC3527EF2;
constexpr uint32_t kPushSpeedCrouched = 0x92E0247C;
constexpr uint32_t kKickoffSpeed = 0x4610C2E3;
constexpr uint32_t kPushCrouched = 0x3D24128E;
constexpr uint32_t kPush = 0x5F9B864D;
constexpr uint32_t kFrictionCrouched = 0xBED96EDA;
constexpr uint32_t kFriction = 0x1A78B6FC;
constexpr uint32_t kNormalBlendRate = 0xD8120182;
constexpr uint32_t kVertPushOut = 0x78384871;
constexpr uint32_t kOff = 0xD443A2BC; // Off

// The game's constants.
constexpr float kBarelyMovingSquared = 0.00001F; // 0x006453B8
constexpr float kFramesPerSecond = 60.0F;        // 0x00645EA0
constexpr float kSlow = 50.0F;                   // 0x006463CC
constexpr float kLockedSlow = 0.5F;              // 0x006453BC
constexpr float kStill = 0.0001F;                // 0x006453D4
constexpr float kNormalFeeler = 72.0F;           // 0x00646244
constexpr float kFlat = 0.001F;                  // 0x006461D8

// The state component's flags.

// The pad's buttons (offsets into the input component).

double speed_of(const maths::Vec4& v) { return length_unrounded(v); }

} // namespace

// 0x004FE1F0
void SkaterPhysicsModule::apply_friction(float friction)
{
    maths::Vec4& velocity = entity()->vel();
    auto length_squared = static_cast<float>(length_sqr_unrounded(velocity));
    if (length_squared < kBarelyMovingSquared) {
        return;
    }
    auto slowing =
        static_cast<float>(static_cast<double>(length_squared) * frame_length_ * friction * kFramesPerSecond);
    maths::Vec4 change = velocity;
    change.set_length(slowing);
    const double change_squared =
        ((static_cast<double>(change.z) * change.z) + (static_cast<double>(change.x) * change.x)) +
        (static_cast<double>(change.y) * change.y);
    if (change_squared > length_squared) {
        velocity = maths::Vec4(0.0F, 0.0F, 0.0F, 1.0F);
        return;
    }
    velocity.x -= change.x;
    velocity.y -= change.y;
    velocity.z -= change.z;
    velocity.w -= change.w;
}

// 0x004FE320
bool SkaterPhysicsModule::should_brake()
{
    const objects::PadInputModule* input = input_;
    if ((auto_kick_ == 0U) && (input->pad_primary().button_box().held_down() == 0U) &&
        speed_of(entity()->vel()) < kSlow) {
        return true;
    }
    if ((input->pad_primary().btn_down().held_down() == 0U) || (can_brake_ == 0U)) {
        return false;
    }
    if (speed_of(entity()->vel()) < kSlow) {
        return true;
    }
    if ((input->pad_primary().right_side().held_down() == 0U) && (input->pad_primary().left_side().held_down() == 0U)) {
        return true;
    }
    const objects::GameEntity* object = object_of(this);
    const maths::Vec4& v = object->vel();
    const maths::Vec4& facing = core::at(object->basis_frame().row, 2);
    const double along = ((static_cast<double>(v.z) * facing.z) + (static_cast<double>(v.y) * facing.y)) +
                         (static_cast<double>(facing.x) * v.x);
    return along < 0.0;
}

// 0x004FE420
bool SkaterPhysicsModule::may_turn_on_slope()
{
    auto speed = static_cast<float>(speed_of(entity()->vel()));
    if (!(script::fetch_float(kSlopeTurnSpeed) > speed)) {
        return false;
    }
    return script::fetch_float(kSlopeTurnNormal) > floor_normal_.y;
}

// 0x004FE490
bool SkaterPhysicsModule::can_push_off()
{
    const objects::PadInputModule* input = input_;
    if (no_kick_ != 0U) {
        return false;
    }
    if ((auto_kick_ == 0U) && (input->pad_primary().button_box().held_down() == 0U)) {
        return false;
    }
    if (input->pad_primary().btn_down().held_down() != 0U) {
        return false;
    }
    auto speed = static_cast<float>(speed_of(entity()->vel()));
    const bool crouched = state_->flag(PoseFlag::kCrouched).on != 0 || (crouch_stats_ != 0U);
    auto limit = static_cast<float>(stat_value(object_of(this), crouched ? kPushSpeedCrouched : kKickoffSpeed));
    return !(limit < speed);
}

// 0x004FE530
void SkaterPhysicsModule::push_off()
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4 direction;
    if (lock_velocity_ != 0U) {
        direction = object->vel();
        const maths::Vec4& v = direction;
        const double speed = std::sqrt(((static_cast<double>(v.z) * v.z) + (static_cast<double>(v.y) * v.y)) +
                                       (static_cast<double>(v.x) * v.x));
        if (speed < kLockedSlow) {
            direction = core::at(object->basis_frame().row, 2);
        } else {
            direction.make_unit();
        }
    } else {
        direction = core::at(object->basis_frame().row, 2);
    }
    const bool crouched = state_->flag(PoseFlag::kCrouched).on != 0 || (crouch_stats_ != 0U);
    auto push = static_cast<float>(stat_value(object, crouched ? kPushCrouched : kPush));
    const double frame = frame_length_;
    auto x = static_cast<float>(static_cast<double>(direction.x) * push * frame);
    const double y = static_cast<double>(direction.y) * push * frame;
    const double z = static_cast<double>(static_cast<float>(static_cast<double>(direction.z) * push)) * frame;
    auto w =
        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(direction.w) * push)) * frame);
    maths::Vec4& velocity = entity()->vel();
    velocity.x += x;
    velocity.y = static_cast<float>(velocity.y + y);
    velocity.z = static_cast<float>(velocity.z + z);
    velocity.w += w;
}

// 0x004FE6A0
void SkaterPhysicsModule::apply_stance_friction()
{
    objects::GameEntity* object = object_of(this);
    float crouched = tuning_float_lookup(kFrictionCrouched, object);
    float standing = tuning_float_lookup(kFriction, object);
    if (limits_time_ != 0.0F) {
        crouched = limits_friction_;
        standing = crouched;
    }
    apply_friction(state_->flag(PoseFlag::kCrouched).on != 0 ? crouched : standing);
}

// 0x004FE730
void SkaterPhysicsModule::apply_wheel_drag()
{
    maths::Vec4& velocity = entity()->vel();
    const double x = velocity.x;
    const double w = velocity.w;
    const float vy = velocity.y;
    const float vz = velocity.z;
    const double speed = std::sqrt(((static_cast<double>(vz) * vz) + (static_cast<double>(vy) * vy)) + (x * x));
    auto speed_rounded = static_cast<float>(speed);
    if (speed < kStill) {
        velocity = maths::Vec4(0.0F, 0.0F, 0.0F, 1.0F);
        return;
    }
    const double friction_scale =
        static_cast<double>(frame_length_) / speed_rounded * rolling_friction_ * kFramesPerSecond;
    auto dx = static_cast<float>(friction_scale * x);
    auto dy = static_cast<float>(vy * friction_scale);
    auto dz = static_cast<float>(vz * friction_scale);
    const double dw = w * friction_scale;
    const double change_squared =
        ((static_cast<double>(dz) * dz) + (static_cast<double>(dy) * dy)) + (static_cast<double>(dx) * dx);
    if (static_cast<double>(speed_rounded) * speed_rounded < change_squared) {
        velocity = maths::Vec4(0.0F, 0.0F, 0.0F, 1.0F);
        return;
    }
    velocity.x -= dx;
    velocity.y -= dy;
    velocity.z -= dz;
    velocity.w = static_cast<float>(velocity.w - dw);
}

// 0x004FE860
void SkaterPhysicsModule::turn_to_normal(maths::Vec4 normal)
{
    if (park_editing::ParkBuilderMode::sole()->editor_state() != kOff && normal.y > 0.0F) {
        const maths::Vec4& position = entity()->at_point();
        maths::Vec4 end;
        end.x = static_cast<float>(static_cast<float>(normal.x * static_cast<double>(kNormalFeeler)) + position.x);
        end.y = static_cast<float>(static_cast<float>(normal.y * static_cast<double>(kNormalFeeler)) + position.y);
        end.z = static_cast<float>(static_cast<float>(normal.z * static_cast<double>(kNormalFeeler)) + position.z);
        end.w = static_cast<float>((normal.w * static_cast<double>(kNormalFeeler)) + position.w);
        render::ProbeRay feeler(&position, &end);
        if (feeler.get_hit_shape(true, false) != 0) {
            normal = maths::Vec4(0.0F, 1.0F, 0.0F, 1.0F);
        }
    }
    if (floor_normal_ != normal) {
        floor_normal_ = normal;
        old_normal_ = drawn_normal_;
        normal_blend_ = 1.0F;
        core::at(entity()->basis_frame().row, 1) = normal;
        entity()->basis_frame().square_up_axes(1);
    }
}

// 0x004FEA50
void SkaterPhysicsModule::update_ground_normal()
{
    float& blend = normal_blend_;
    if (blend != 0.0F) {
        if (old_normal_ == floor_normal_) {
            blend = 0.0F;
        } else {
            const double left =
                blend - (static_cast<double>(script::fetch_float(kNormalBlendRate)) * frame_length_ * kFramesPerSecond);
            auto remaining_blend = static_cast<float>(left);
            blend = remaining_blend;
            if (left > 0.0 || left != left) {
                maths::Vec4 blended;
                walk::get_lerp(&blended, &floor_normal_, &old_normal_, remaining_blend);
                drawn_normal_ = blended;
                drawn_normal_.make_unit();
            } else {
                blend = 0.0F;
                drawn_normal_ = floor_normal_;
            }
        }
    }
    if (core::at(display_matrix_.row, 1) != drawn_normal_) {
        core::at(display_matrix_.row, 1) = drawn_normal_;
        maths::Mat4& display = display_matrix_;
        display.square_up_axes(1);
        core::at(display.row, 0) = core::at(entity()->basis_frame().row, 0);
        core::at(display_matrix_.row, 2) = core::at(entity()->basis_frame().row, 2);
    }
}

// 0x004FEBA0
void SkaterPhysicsModule::start_vert_air()
{
    if (state_->flag(PoseFlag::kOnVert).on == 0U) {
        set_timed_flag(this, PoseFlag::kVertAir, 0);
        return;
    }
    const objects::GameEntity* object = object_of(this);
    tracking_print(3, &object->old_pos(), &object->at_point());
    maths::Vec4 across = floor_normal_;
    vert_wall_normal_ = floor_normal_;
    vert_takeoff_ = entity()->old_pos();
    vert_takeoff_.y = static_cast<float>(vert_takeoff_.y - 1.0);
    across.y = 0.0F;
    if (!(std::sqrt((static_cast<double>(across.z) * across.z) + (static_cast<double>(across.x) * across.x)) > kFlat)) {
        set_timed_flag(this, PoseFlag::kVertAir, 0);
        return;
    }
    across.make_unit();
    entity()->vel().align_to_plane(across);
    turn_to_normal(across);
    vert_facing_ = core::at(entity()->basis_frame().row, 2);
    vert_facing_.y = -vert_facing_.y;
    const double out = script::fetch_float(kVertPushOut);
    maths::Vec4& position = entity()->at_point();
    auto y = static_cast<float>(across.y * out);
    auto z = static_cast<float>(across.z * out);
    auto w = static_cast<float>(across.w * out);
    position.x = static_cast<float>((across.x * out) + position.x);
    position.y += y;
    position.z += z;
    position.w += w;
    set_timed_flag(this, PoseFlag::kVertAir, 1);
    set_timed_flag(this, PoseFlag::kVertWall, 1);
    set_timed_flag(this, PoseFlag::kSpin, 1);
    vert_step_ = kVertStep;
}

} // namespace skater

