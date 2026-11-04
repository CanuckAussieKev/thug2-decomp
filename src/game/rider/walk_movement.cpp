// The Walk component's movement (GameObjects::FootMovement, see walk_physics.h): its control, speed and facing, the
// push-back, the jump's steering, the collision cache, and its object's position, orientation and velocity. From the
// disassembly of 0x00526260-0x00529F62.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "base/maths/shape_tests.h"
#include "engine/script/member_code.h"
#include "base/maths/vec4.h"
#include "base/maths/mat4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/walk_hang.h"
#include "game/rider/walk_fields.h"
#include "game/rider/walk_physics.h"
#include "base/maths/trig.h"
#include "engine/sys/stopwatch.h"
#include "engine/objects/input_component.h"
#include "engine/objects/model_component.h"
#include "base/core/checked_index.h"

namespace objects {

namespace {

using namespace walk_fields;

// walk_parameters' names.
constexpr uint32_t kWallTurnSpeedThreshold = 0x0515A933, kMaxWallTurnSpeedThreshold = 0xE6C1CD0D,
                   kWallTurnFactor = 0x7A583B9B, kWalkSpeed = 0x79D182AD, kLerpUprightRate = 0x0F22C135,
                   kFeelerLength = 0x99978D2B, kWalkerHeight = 0x9EA1974A, kSnapDownHeight = 0xAF3E4251,
                   kDisplayOffsetRestoreRate = 0xB32C972B, kPushFeelerLength = 0xA20C43B7, kPushStrength = 0x4D16F37D,
                   kForwardTolerance = 0x4676A268, kWalkPoint = 0xC1528F7F, kJumpAdjustSpeed = 0x896C8888,
                   kJumpAccelFactor = 0xF085443B, kIgnoreVelocityThreshold = 0x8AFB8788, kSlopLockAngle = 0xDB05E84E,
                   kRotateInPlaceRate = 0xB557804B, kWalkRotateFactor = 0x7B446C98, kWallRotateFactor = 0xE0456382,
                   kWorseTurnFactor = 0x6CB2E5DE, kWorseWorseTurnFactor = 0xA26760F5, kDecelFactor = 0xACFA4E0C,
                   kLowSpeedDecelFactor = 0x0A2AA811, kRunAccelRate = 0x4F47C998, kWalkAccelRate = 0x6590A49B,
                   kRotateUprightDuration = 0xB0675803, kProbeElevation = 0x6DA7F696;
constexpr uint32_t kStand = 0x9B46E749, kRun = 0xAF895B3F;

constexpr float kOne = 1.0F;                       // 0x006453A8
constexpr float kSteepSlope = 0.95F;               // 0x0064BC7C (0.949999988f)
constexpr float kNearlyUpright = 0.999F;           // 0x006467B8 (0.999000013f)
constexpr float kMinFacingLength = 0.001F;         // 0x006461D8
constexpr float kUprightNudge = 0.01F;             // 0x0064544C
constexpr float kRadiansPerDegree = 0.0174532924F; // 0x00645464
constexpr float kStickScale = 1.17647052F;         // 0x00647B28 (1 / 0.85)
constexpr float kWalkStart = 0.3F;                 // 0x006467C0: the stick's push where walking starts
constexpr float kMinMoveSpeed = 0.01F;             // 0x0064544C
constexpr float kNoFlat = 9.99999975e-05F;         // 0x006453D4
constexpr float kDegrees = 57.2957763671875F;      // 0x00645468

// |acos(value)| in degrees, rounded to float once.
float acos_degrees(float value)
{
    return static_cast<float>(std::fabs(std::acos(static_cast<double>(value)) * kDegrees));
}

// a + (b - a) * t.
maths::Vec4* get_lerp(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float fraction)
{
    return walk::get_lerp(out, a, b, fraction);
}

double get_frame_length(FootMovement* walk) { return walk->frame_length(); }

} // namespace

// 0x00526260
void FootMovement::turn_facing_to_velocity()
{
    const maths::Vec4& vel = vel_;
    auto speed =
        static_cast<float>(std::sqrt(((static_cast<double>(vel.x) * vel.x) + (static_cast<double>(vel.y) * vel.y)) +
                                     (static_cast<double>(vel.z) * vel.z)));
    if (speed < walk_parameter(kWallTurnSpeedThreshold)) {
        return;
    }
    maths::Vec4 direction = vel;
    direction.make_unit();
    // The turn this frame: wall_turn_factor a second at max_wall_turn_speed_threshold and above, less from the
    // threshold up.
    double rate = 0.0;
    if (speed > walk_parameter(kMaxWallTurnSpeedThreshold)) {
        rate = static_cast<double>(walk_parameter(kWallTurnFactor)) * get_frame_length(this);
    } else {
        const float most = walk_parameter(kMaxWallTurnSpeedThreshold);
        const float least = walk_parameter(kWallTurnSpeedThreshold);
        const float factor = walk_parameter(kWallTurnFactor);
        rate = (static_cast<double>(speed) - least) / (static_cast<double>(most) - least) *
               (static_cast<double>(factor) * get_frame_length(this));
    }
    maths::Vec4& facing = facing_;
    if (rate >= kOne) {
        facing = direction;
        return;
    }
    // (below 1 - or NaN - the rate rounded to float)
    maths::Vec4 turned{};
    facing = *get_lerp(&turned, &facing, &direction, static_cast<float>(rate));
    facing.make_unit();
}

// 0x00526430
double FootMovement::adjust_speed_for_slope(float speed, const maths::Vec4* direction)
{
    const maths::Vec4& normal = floor_normal_;
    if (normal.y > kSteepSlope) {
        return speed;
    }
    if (speed <= walk_parameter(kWalkSpeed)) {
        return speed;
    }
    // On a steep slope, faster than walking: slowed by how much the direction runs up or down it.
    maths::Vec4 downhill = {.x = normal.x, .y = 0.0F, .z = normal.z, .w = normal.w};
    downhill.make_unit();
    const double along = std::fabs(
        ((static_cast<double>(downhill.z) * direction->z) + (static_cast<double>(downhill.y) * direction->y)) +
        (static_cast<double>(downhill.x) * direction->x));
    return ((kOne - along) + (along * normal.y)) * speed;
}

// 0x00527420
void FootMovement::update_upright()
{
    maths::Vec4& up = up_;
    if (up_.y == 1.0F) { // (up.y exactly 1)
        return;
    }
    if (up.y > kNearlyUpright) {
        up = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
        upright_time_ = 0.0F;
        return;
    }
    if (!(upright_time_ == 0.0F)) {
        return;
    }
    const float rate = walk_parameter(kLerpUprightRate);
    const maths::Vec4 upright = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    maths::Vec4 eased{};
    up = *get_lerp(&eased, &up, &upright, static_cast<float>(frame_clock::get_slomo_frame_seconds() * rate));
    up.make_unit();
}

// 0x00527720
void FootMovement::set_transition_facing(const maths::Vec4* facing, float rate)
{
    const maths::Vec4& current = facing_;
    const double turn = std::atan2(static_cast<double>(facing->x), static_cast<double>(facing->z)) -
                        std::atan2(static_cast<double>(current.x), static_cast<double>(current.z));
    transition_turn_ = static_cast<float>(turn);
    if (std::fabs(turn) > maths::kPi) { // the shorter way round
        transition_turn_ = static_cast<float>(turn - ((turn < 0.0 ? -1.0F : kOne) * maths::kTwoPi));
    }
    transition_facing_ = *facing;
    transition_rate_ = rate;
}

// 0x005277B0
void FootMovement::refresh_hit_cache()
{
    auto reach = static_cast<float>(static_cast<double>(walk_parameter(kFeelerLength)) + kOne);
    const float height = walk_parameter(kWalkerHeight);
    float below = walk_parameter(kSnapDownHeight);
    auto above = static_cast<float>(static_cast<double>(height) + kOne);
    const maths::Vec4& pos = static_cast<GameEntity*>(entity())->at_point();
    maths::BBox box;
    box.max = {.x = pos.x + reach, .y = pos.y + above, .z = pos.z + reach, .w = pos.w};
    below = static_cast<float>(static_cast<double>(below) + kOne);
    box.min = {.x = pos.x - reach, .y = pos.y - below, .z = pos.z - reach, .w = pos.w};
    render::HitMeshCache& cache = hit_cache_;
    cache.tick_frame(box);
    render::set_current_cache(&cache); // the feelers' current cache
}

// 0x00527BE0
void FootMovement::restore_display_offset()
{
    const int state = state_;
    if (state == 2 || state == 4) {
        return;
    }
    const float rate = walk_parameter(kDisplayOffsetRestoreRate);
    const double step = frame_clock::get_slomo_frame_seconds() * rate;
    float& offset = model_lift_;
    offset = static_cast<float>((-static_cast<double>(offset) * step) + offset);
}

// 0x00527C50
void FootMovement::update_push()
{
    if (!pushing_) {
        return;
    }
    maths::Vec4& pos = pos_;
    if (pos.y > push_height_) {
        pushing_ = false;
        return;
    }
    const maths::Vec4& normal = push_normal_;
    auto distance =
        static_cast<float>((((static_cast<double>(normal.z) * pos.z) + (static_cast<double>(normal.y) * pos.y)) +
                            (static_cast<double>(normal.x) * pos.x)) -
                           push_plane_distance_);
    if (!(-walk_parameter(kPushFeelerLength) < distance)) {
        return;
    }
    // Within reach of the plane: pushed away from it, and the velocity into it taken away.
    const double push = static_cast<double>(walk_parameter(kPushStrength)) * get_frame_length(this);
    const double push_x = push * normal.x; // (x's kept unrounded, as the original's register)
    auto push_y = static_cast<float>(push * normal.y);
    auto push_z = static_cast<float>(push * normal.z);
    auto push_w = static_cast<float>(push * normal.w);
    pos.x = static_cast<float>(pos.x - push_x);
    pos.y = pos.y - push_y;
    pos.z = pos.z - push_z;
    pos.w = pos.w - push_w;
    maths::Vec4& vel = vel_;
    const double into = ((static_cast<double>(normal.z) * vel.z) + (static_cast<double>(normal.y) * vel.y)) +
                        (static_cast<double>(vel.x) * normal.x);
    if (into == 0.0) {
        return;
    }
    vel -= maths::operator*(static_cast<float>(into), normal);
}

// 0x00527DB0
void FootMovement::copy_from_object()
{
    auto* object = static_cast<GameEntity*>(entity());
    pos_ = object->at_point();
    maths::Vec4& vel = vel_;
    vel = object->vel();
    vel.y = 0.0F;
    up_speed_ = object->vel().y;
    maths::Vec4& up = up_;
    up = core::at(object->basis_frame().row, 1);
    maths::Vec4& facing = facing_;
    facing = core::at(object->basis_frame().row, 2);
    facing_y_ = facing.y;
    facing.y = 0.0F;
    const double length =
        std::sqrt(((static_cast<double>(facing.x) * facing.x) + (static_cast<double>(facing.y) * facing.y)) +
                  (static_cast<double>(facing.z) * facing.z));
    if (length < kMinFacingLength) {
        // Facing straight up or down: the way the up row leans, backwards; the up row nudged upright.
        const maths::Vec4& object_up = core::at(object->basis_frame().row, 1);
        facing = {.x = -object_up.x, .y = -object_up.y, .z = -object_up.z, .w = -object_up.w};
        facing.y = 0.0F;
        facing.make_unit();
        up.y = static_cast<float>(static_cast<double>(up.y) + kUprightNudge);
        up.make_unit();
        return;
    }
    const double inverse = kOne / length;
    facing.x = static_cast<float>(inverse * facing.x);
    facing.y = static_cast<float>(inverse * facing.y);
    facing.z = static_cast<float>(inverse * facing.z);
    facing.w = static_cast<float>(inverse * facing.w);
}

// 0x00527F20
void FootMovement::update_control()
{
    // The camera's flat forward (its at-row backwards), turned by the stick's angle - straight ahead within
    // forward_tolerance degrees.
    const maths::Vec4& at = core::at(camera_->basis_frame().row, 2);
    auto* pad = &input_->pad_primary();
    float forward_x = -at.x;
    float forward_z = -at.z;
    const double length =
        std::sqrt((static_cast<double>(forward_z) * forward_z) + (static_cast<double>(forward_x) * forward_x));
    if (length > 0.0) {
        const double inverse = kOne / length;
        forward_x = static_cast<float>(forward_x * inverse);
        forward_z = static_cast<float>(forward_z * inverse);
    }
    float angle = core::at(pad->polar(), 1).angle;
    if (walk_parameter(kForwardTolerance) * static_cast<double>(kRadiansPerDegree) > std::fabs(angle)) {
        angle = 0.0F;
    }
    double turned_z = 0.0;
    double turned_x = 0.0;
    maths::spin_by(angle, forward_z, forward_x, &turned_z, &turned_x);
    maths::Vec4& control = stick_heading_;
    control.x = static_cast<float>(turned_x);
    control.z = static_cast<float>(turned_z);

    float& magnitude = stick_deflection_;
    bool& full = control_full_;
    if (!(core::at(pad->stick(), 2) == 0.0F) || !(core::at(pad->stick(), 3) == 0.0F)) {
        // The stick: its push, full from 85 %.
        stick_steers_ = true;
        const double push = stick_length({.x = core::at(pad->stick(), 2), .y = core::at(pad->stick(), 3)}, core::at(pad->polar(), 1).angle) * kStickScale;
        magnitude = static_cast<float>(push);
        if (push < kOne || std::isnan(push)) {
            full = false;
        } else {
            magnitude = kOne;
            full = true;
        }
    } else {
        // The d-pad: on the ground without X held, walk_point (the original calls 0x0043DA70 for it); else full.
        stick_steers_ = false;
        if (core::at(pad->polar(), 1).length == 0.0F) {
            magnitude = 0.0F;
            full = false;
        } else if (state_ == 0 && (pad->pos_x().held_down() == 0U)) {
            magnitude = walk_parameter(kWalkPoint);
            full = true;
        } else {
            magnitude = kOne;
            full = true;
        }
    }
    if (state_ == 0 && locked_to_heading_) {
        control = facing_;
    }
}

// 0x00528120
double FootMovement::get_control_speed()
{
    const float magnitude = stick_deflection_;
    if (magnitude == 0.0F) {
        return 0.0;
    }
    const float walk_point = walk_parameter(kWalkPoint);
    if (magnitude <= walk_point) {
        // Walking: from walk_start's push up to walk_point, up to walk_speed.
        running_ = false;
        const float walk_speed = walk_parameter(kWalkSpeed);
        return (static_cast<double>(magnitude) - kWalkStart) / (static_cast<double>(walk_point) - kWalkStart) *
               walk_speed;
    }
    // Running: from walk_speed at walk_point up to the run speed at a full push.
    running_ = true;
    const float walk_speed = walk_parameter(kWalkSpeed);
    auto fraction =
        static_cast<float>((static_cast<double>(magnitude) - walk_point) / (static_cast<double>(kOne) - walk_point));
    const double speed_of_run = run_speed();
    return ((speed_of_run - walk_speed) * fraction) + walk_speed;
}

// 0x00526850
void FootMovement::steer_jump()
{
    float& delay = leap_duration_;
    if (!(delay == 0.0F)) {
        // (still counting down: no steering yet)
        const double left = static_cast<double>(delay) - get_frame_length(this);
        delay = left < 0.0 ? 0.0F : static_cast<float>(left);
        return;
    }
    // The velocity apart from what the jump carries along unsteered, eased toward what the push asks for: across
    // the jump's direction and along it.
    maths::Vec4& vel = vel_;
    const maths::Vec4& carried = jump_base_vel_;
    vel = {.x = vel.x - carried.x, .y = vel.y - carried.y, .z = vel.z - carried.z, .w = vel.w - carried.w};
    const maths::Vec4& dir = leap_heading_;
    const maths::Vec4& control = stick_heading_;
    const float magnitude = stick_deflection_;
    const float side_x = -dir.z;
    const float side_z = dir.x; // (the jump's sideways direction: y 0, w 1)
    auto side_target =
        static_cast<float>((((static_cast<double>(side_z) * control.z) + (static_cast<double>(side_x) * control.x)) +
                            (static_cast<double>(control.y) * 0.0)) *
                           walk_parameter(kJumpAdjustSpeed) * magnitude);
    auto side = static_cast<float>(((static_cast<double>(side_z) * vel.z) + (static_cast<double>(side_x) * vel.x)) +
                                   (static_cast<double>(vel.y) * 0.0));
    side = static_cast<float>(((static_cast<double>(side_target) - side) *
                               (static_cast<double>(walk_parameter(kJumpAccelFactor)) * get_frame_length(this))) +
                              side);
    auto forward_target =
        static_cast<float>((((static_cast<double>(control.z) * dir.z) + (static_cast<double>(control.y) * dir.y)) +
                            (static_cast<double>(control.x) * dir.x)) *
                           walk_parameter(kJumpAdjustSpeed) * magnitude);
    auto forward = static_cast<float>(((static_cast<double>(vel.z) * dir.z) + (static_cast<double>(vel.y) * dir.y)) +
                                      (static_cast<double>(vel.x) * dir.x));
    bool ease = false;
    if (!jump_unsteered_ && stick_deflection_ == 0.0F) {
        // no push: along the jump, eased to a stop
        forward_target = 0.0F;
        forward = 0.0F;
        ease = true;
    } else {
        if (jump_unsteered_) {
            side = 0.0F;
        }
        // Faster ahead than asked: kept. Pushing back while moving ahead: from the speed there.
        ease = !(forward_target >= 0.0F && forward > forward_target);
        if (ease && forward_target < 0.0F && forward > 0.0F) {
            forward_target = forward + forward_target;
        }
    }
    if (ease) {
        forward =
            static_cast<float>(((static_cast<double>(forward_target) - forward) *
                                (static_cast<double>(walk_parameter(kJumpAccelFactor)) * get_frame_length(this))) +
                               forward);
    }
    const float side_y = side * 0.0F;
    const float side_zpart = side_z * side;
    vel.x = static_cast<float>((static_cast<double>(side_x) * side) + (static_cast<double>(forward) * dir.x));
    vel.y = (forward * dir.y) + side_y;
    vel.z = (forward * dir.z) + side_zpart;
    vel.w = (forward * dir.w) + side;
    vel = {.x = carried.x + vel.x, .y = carried.y + vel.y, .z = carried.z + vel.z, .w = carried.w + vel.w};
}

// 0x00528950
void FootMovement::update_ground_velocity(maths::Vec4* velocity)
{
    auto speed = static_cast<float>(std::sqrt(
        ((static_cast<double>(velocity->x) * velocity->x) + (static_cast<double>(velocity->y) * velocity->y)) +
        (static_cast<double>(velocity->z) * velocity->z)));
    const double wanted = get_control_speed();
    auto target = static_cast<float>(wanted);
    anim_state_ = wanted <= 0.0 ? kStand : kRun;

    // The way it's going: the velocity's (past ignore_velocity_threshold), else the push's, else the velocity's
    // (past 0.01), else the facing.
    const float magnitude = stick_deflection_;
    maths::Vec4 direction{};
    double direction_w = 0.0;
    if (speed > walk_parameter(kIgnoreVelocityThreshold)) {
        const double inverse = kOne / static_cast<double>(speed);
        auto inverse_f = static_cast<float>(inverse);
        direction = {
            .x = inverse_f * velocity->x, .y = inverse_f * velocity->y, .z = inverse_f * velocity->z, .w = 0.0F};
        direction_w = inverse * velocity->w;
    } else if (!(magnitude == 0.0F)) {
        direction = stick_heading_;
        direction_w = direction.w;
    } else if (speed > kMinMoveSpeed) {
        direction = maths::operator/(*velocity, speed);
        direction_w = direction.w;
    } else {
        direction = facing_;
        direction_w = direction.w;
    }
    direction.w = static_cast<float>(direction_w);
    maths::Vec4 result = direction;

    if (magnitude > 0.0F) {
        const maths::Vec4& control = stick_heading_;
        maths::Vec4& facing = facing_;
        // Pushed against the way it's going: no speed asked for.
        if (((static_cast<double>(direction.z) * control.z) + (static_cast<double>(direction.y) * control.y)) +
                (static_cast<double>(direction.x) * control.x) <=
            0.0) {
            target = 0.0F;
        }
        const double cosine =
            ((static_cast<double>(direction.z) * facing.z) + (static_cast<double>(direction.y) * facing.y)) +
            (static_cast<double>(direction.x) * facing.x);
        auto cosine_f = static_cast<float>(cosine);
        const float clamped = cosine > kOne ? kOne : std::max(cosine_f, -kOne);
        if (acos_degrees(clamped) > walk_parameter(kSlopLockAngle)) {
            // Far off the facing: turned in place at rotate_in_place_rate (walking: x walk_rotate_factor; against a
            // wall, or a wall where it turns to: x wall_rotate_factor), going the pushed way.
            const float rate = walk_parameter(kRotateInPlaceRate);
            auto turn = static_cast<float>(frame_clock::get_slomo_frame_seconds() * stick_deflection_ * rate *
                                           kRadiansPerDegree);
            const bool left =
                (static_cast<double>(facing.x) * control.z) - (static_cast<double>(control.x) * facing.z) < 0.0;
            if (!running_) {
                turn = static_cast<float>(static_cast<double>(walk_parameter(kWalkRotateFactor)) * turn);
            }
            if (blocked_) {
                turn = static_cast<float>(static_cast<double>(walk_parameter(kWallRotateFactor)) * turn);
            } else {
                double turned_x = 0.0;
                double turned_z = 0.0;
                maths::spin_by(left ? turn : -turn, facing.x, facing.z, &turned_x, &turned_z);
                auto ahead_x = static_cast<float>(turned_x);
                auto ahead_z = static_cast<float>(turned_z);
                render::ProbeRay feeler;
                maths::Vec4& start = feeler.line().start;
                start = pos_;
                start.y = static_cast<float>(static_cast<double>(start.y) + walk_parameter(kProbeElevation));
                const float reach = walk_parameter(kFeelerLength);
                const float none = reach * 0.0F;
                feeler.line().end = {.x = (ahead_x * reach) + start.x,
                                     .y = none + start.y,
                                     .z = (ahead_z * reach) + start.z,
                                     .w = none + start.w};
                // (the original reads wall_rotate_factor through 0x0043DA70)
                if (feeler.get_hit_shape(false, false) != 0) {
                    turn = static_cast<float>(static_cast<double>(walk_parameter(kWallRotateFactor)) * turn);
                }
            }
            double new_x = 0.0;
            double new_z = 0.0;
            maths::spin_by(left ? turn : -turn, facing.x, facing.z, &new_x, &new_z);
            facing.z = static_cast<float>(new_z);
            facing.x = static_cast<float>(new_x);
            // Turned past the push: onto it.
            const bool still_left = (new_x * control.z) - (static_cast<double>(control.x) * facing.z) < 0.0;
            if (left != still_left) {
                facing = control;
            }
            result = control;
        } else {
            // Near the facing: the facing eased toward the push, the more the further off (worse_turn_factor to
            // worse_worse_turn_factor), going the facing's way.
            double off =
                kOne - (((static_cast<double>(control.z) * facing.z) + (static_cast<double>(control.y) * facing.y)) +
                        (static_cast<double>(control.x) * facing.x));
            off = off + off;
            auto off_f = static_cast<float>(off);
            const float turn_blend = off > kOne ? kOne : std::max(off_f, 0.0F);
            const float worse = walk_parameter(kWorseTurnFactor);
            const float worst = walk_parameter(kWorseWorseTurnFactor);
            auto rate =
                static_cast<float>((((static_cast<double>(worse) - worst) * turn_blend) + worst) * stick_deflection_);
            const double step = frame_clock::get_slomo_frame_seconds() * rate;
            if (step >= kOne) {
                facing = control;
            } else {
                maths::Vec4 eased{};
                facing = *get_lerp(&eased, &facing, &control, static_cast<float>(step));
                facing.make_unit();
            }
            result = facing;
        }
    }

    // The speed: toward what's asked (as the slope allows) - up at walk_accel_rate / run_accel_rate, down by
    // decel_factor (low_speed_decel_factor stopping from walking speed).
    speed_ = target;
    auto allowed = static_cast<float>(adjust_speed_for_slope(target, &direction));
    double new_speed = 0.0;
    if (speed > allowed) {
        float factor = walk_parameter(kDecelFactor);
        if (allowed == 0.0F && !(speed > walk_parameter(kWalkSpeed))) {
            factor = walk_parameter(kLowSpeedDecelFactor);
        }
        new_speed =
            ((static_cast<double>(allowed) - speed) * (frame_clock::get_slomo_frame_seconds() * factor)) + speed;
    } else {
        const float accel = walk_parameter(running_ ? kRunAccelRate : kWalkAccelRate);
        new_speed = (frame_clock::get_slomo_frame_seconds() * accel) + speed;
        new_speed = std::min<double>(new_speed, allowed);
    }
    *velocity = {.x = static_cast<float>(result.x * new_speed), .y = static_cast<float>(result.y * new_speed),
                 .z = static_cast<float>(result.z * new_speed), .w = static_cast<float>(result.w * new_speed)};
}

// 0x00529800
void FootMovement::update_rotate_upright()
{
    float& left = upright_time_;
    if (left == 0.0F) {
        return;
    }
    // This frame's share of the turn (+0x730 about +0x734) over rotate_upright_duration.
    const float duration = walk_parameter(kRotateUprightDuration);
    auto angle = static_cast<float>(get_frame_length(this) / duration * upright_angle_);
    maths::Mat4 rotation{};
    maths::build_axis_angle_matrix(rotation, upright_axis_, angle);
    auto* object = static_cast<GameEntity*>(entity());
    // D3DXMatrixMultiply (0x005BFA8C)
    maths::matrix_multiply(&object->basis_frame(), &object->basis_frame(), &rotation);
    object->display_matrix() = object->basis_frame();
    const double remaining = static_cast<double>(left) - get_frame_length(this);
    left = remaining < 0.0 ? 0.0F : static_cast<float>(remaining);
}

// 0x005298D0
void FootMovement::apply_transition()
{
    auto* object = static_cast<GameEntity*>(entity());
    const maths::Vec4& at = transition_pos_;
    const maths::Vec4& offset = climb_offset_;
    object->at_point() = {.x = at.x + offset.x, .y = at.y + offset.y, .z = at.z + offset.z, .w = at.w + offset.w};
    // Upright, facing the transition's facing (row 0's w 1, as the original).
    const maths::Vec4& facing = transition_facing_;
    maths::Mat4 matrix{};
    core::at(matrix.row, 0) = {.x = facing.z, .y = 0.0F, .z = -facing.x, .w = 1.0F};
    core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    core::at(matrix.row, 2) = facing;
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    object->basis_frame() = matrix;
    model_lift_ = transition_display_offset_;
    // Then the transition's member function, if any (+0x1948, this adjusted by +0x194C).
    script::ObjectCallback callback = anim_done_.code_;
    if (callback) {
        callback(this); // (this_adjust_ is always 0: walk_hang sets it so)
    }
}

// 0x00529CE0
void FootMovement::copy_to_object()
{
    // The facing tilted back up by its y (+0x744), keeping it unit.
    const float facing_y = facing_y_;
    const double flat = kOne - (static_cast<double>(facing_y) * facing_y);
    const double upright_scale = flat > 0.0 ? std::sqrt(flat) : static_cast<double>(kNoFlat);
    maths::Vec4& facing = facing_;
    facing = {.x = static_cast<float>(upright_scale * facing.x),
              .y = static_cast<float>(upright_scale * facing.y),
              .z = static_cast<float>(upright_scale * facing.z),
              .w = static_cast<float>(upright_scale * facing.w)};
    facing.y = facing_y;
    // The orientation from the up vector and the facing.
    const maths::Vec4& up = up_;
    maths::Mat4 matrix{};
    maths::Vec4& right = core::at(matrix.row, 0);
    right = {.x = static_cast<float>((static_cast<double>(up.y) * facing.z) - (static_cast<double>(up.z) * facing.y)),
             .y = static_cast<float>((static_cast<double>(up.z) * facing.x) - (static_cast<double>(up.x) * facing.z)),
             .z = static_cast<float>((static_cast<double>(up.x) * facing.y) - (static_cast<double>(up.y) * facing.x)),
             .w = 0.0F};
    right.make_unit();
    core::at(matrix.row, 1) = up;
    core::at(matrix.row, 2) = {
        .x = static_cast<float>((static_cast<double>(up.z) * right.y) - (static_cast<double>(up.y) * right.z)),
        .y = static_cast<float>((static_cast<double>(up.x) * right.z) - (static_cast<double>(up.z) * right.x)),
        .z = static_cast<float>((static_cast<double>(up.y) * right.x) - (static_cast<double>(up.x) * right.y)),
        .w = 0.0F};
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    auto* object = static_cast<GameEntity*>(entity());
    object->at_point() = pos_;
    object->basis_frame() = matrix;
    object->display_matrix() = matrix;
    // The object's velocity by state.
    maths::Vec4& vel = object->vel();
    switch (state_) {
    case 0: // on the ground, in the air
    case 1:
        vel = vel_;
        vel.y = up_speed_;
        break;
    case 2: { // hanging: sideways along the ledge
        const float scale = climb_scale_;
        vel = {.x = scale * -facing.z, .y = scale * 0.0F, .z = scale * facing.x, .w = scale * 1.0F};
        break;
    }
    case 3: // climbing: up at its speed
        vel = {.x = 0.0F, .y = speed_, .z = 0.0F, .w = 1.0F};
        break;
    default:
        vel = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        break;
    }
    // The model raised along the up vector by the display offset.
    const float offset = model_lift_;
    model_->set_shift({.x = core::at(matrix.row, 1).x * offset, .y = core::at(matrix.row, 1).y * offset, .z = core::at(matrix.row, 1).z * offset,
                      .w = core::at(matrix.row, 1).w * offset});
}

} // namespace objects
