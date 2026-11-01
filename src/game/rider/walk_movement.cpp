// The Walk component's movement (GameObjects::FootMovement, see walk_physics.h): its control, speed and facing, the
// push-back, the jump's steering, the collision cache, and its object's position, orientation and velocity. From the
// disassembly of 0x00526260-0x00529F62.

#include <cmath>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/vec4.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/walk_hang.h"
#include "game/rider/walk_fields.h"
#include "game/rider/walk_physics.h"
#include "engine/maths/trig.h"
#include "platform/stopwatch.h"
#include "platform/crt.h"
#include "engine/objects/input_component.h"
#include "engine/objects/model_component.h"

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

constexpr float kOne = 1.0f;                       // 0x006453A8
constexpr float kSteepSlope = 0.95f;               // 0x0064BC7C (0.949999988f)
constexpr float kNearlyUpright = 0.999f;           // 0x006467B8 (0.999000013f)
constexpr float kPi = 3.14159274f;                 // 0x0064C62C
constexpr float kTwoPi = 6.28318548f;              // 0x0064545C
constexpr float kMinFacingLength = 0.001f;         // 0x006461D8
constexpr float kUprightNudge = 0.01f;             // 0x0064544C
constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kStickScale = 1.17647052f;         // 0x00647B28 (1 / 0.85)
constexpr float kWalkStart = 0.3f;                 // 0x006467C0: the stick's push where walking starts
constexpr float kMinMoveSpeed = 0.01f;             // 0x0064544C
constexpr float kNoFlat = 9.99999975e-05f;         // 0x006453D4
constexpr float kDegrees = 57.2957763671875f;      // 0x00645468

// |acos(value)| in degrees, rounded to float once.
float acos_degrees(float value)
{
    return static_cast<float>(std::fabs(std::acos(static_cast<double>(value)) * kDegrees));
}

// a + (b - a) * t.
maths::Vec4* get_lerp(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float t)
{
    return walk::get_lerp(out, a, b, t);
}

double get_frame_length(FootMovement* walk) { return walk->frame_length_; }

} // namespace

// 0x00526260
void FootMovement::turn_facing_to_velocity()
{
    const maths::Vec4& vel = vel_;
    auto speed =
        static_cast<float>(std::sqrt((static_cast<double>(vel.x) * vel.x + static_cast<double>(vel.y) * vel.y) +
                                     static_cast<double>(vel.z) * vel.z));
    if (speed < walk_parameter(kWallTurnSpeedThreshold))
        return;
    maths::Vec4 direction = vel;
    direction.make_unit();
    // The turn this frame: wall_turn_factor a second at max_wall_turn_speed_threshold and above, less from the
    // threshold up.
    double rate = 0.0;
    if (speed > walk_parameter(kMaxWallTurnSpeedThreshold)) {
        rate = static_cast<double>(walk_parameter(kWallTurnFactor)) * get_frame_length(this);
    } else {
        float most = walk_parameter(kMaxWallTurnSpeedThreshold);
        float least = walk_parameter(kWallTurnSpeedThreshold);
        float factor = walk_parameter(kWallTurnFactor);
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
    if (normal.y > kSteepSlope)
        return speed;
    if (speed <= walk_parameter(kWalkSpeed))
        return speed;
    // On a steep slope, faster than walking: slowed by how much the direction runs up or down it.
    maths::Vec4 downhill = {normal.x, 0.0f, normal.z, normal.w};
    downhill.make_unit();
    double along =
        std::fabs((static_cast<double>(downhill.z) * direction->z + static_cast<double>(downhill.y) * direction->y) +
                  static_cast<double>(downhill.x) * direction->x);
    return ((kOne - along) + along * normal.y) * speed;
}

// 0x00527420
void FootMovement::update_upright()
{
    maths::Vec4& up = up_;
    if (memory::bits_as<uint32_t>(up_.y) == 0x3F800000) // (up.y exactly 1)
        return;
    if (up.y > kNearlyUpright) {
        up = {0.0f, 1.0f, 0.0f, 1.0f};
        upright_time_ = 0.0f;
        return;
    }
    if (!(upright_time_ == 0.0f))
        return;
    float rate = walk_parameter(kLerpUprightRate);
    maths::Vec4 upright = {0.0f, 1.0f, 0.0f, 1.0f};
    maths::Vec4 eased{};
    up = *get_lerp(&eased, &up, &upright, static_cast<float>(frame_clock::get_slomo_frame_seconds() * rate));
    up.make_unit();
}

// 0x00527720
void FootMovement::set_transition_facing(const maths::Vec4* facing, float rate)
{
    const maths::Vec4& current = facing_;
    double turn = std::atan2(static_cast<double>(facing->x), static_cast<double>(facing->z)) -
                  std::atan2(static_cast<double>(current.x), static_cast<double>(current.z));
    transition_turn_ = static_cast<float>(turn);
    if (std::fabs(turn) > kPi) // the shorter way round
        transition_turn_ = static_cast<float>(turn - (turn < 0.0 ? -1.0f : kOne) * kTwoPi);
    transition_facing_ = *facing;
    transition_rate_ = rate;
}

// 0x005277B0
void FootMovement::refresh_hit_cache()
{
    auto reach = static_cast<float>(static_cast<double>(walk_parameter(kFeelerLength)) + kOne);
    float height = walk_parameter(kWalkerHeight);
    float below = walk_parameter(kSnapDownHeight);
    auto above = static_cast<float>(static_cast<double>(height) + kOne);
    const maths::Vec4& pos = static_cast<GameEntity*>(entity_)->at_point_;
    maths::BBox box;
    box.max = {pos.x + reach, pos.y + above, pos.z + reach, pos.w};
    below = static_cast<float>(static_cast<double>(below) + kOne);
    box.min = {pos.x - reach, pos.y - below, pos.z - reach, pos.w};
    render::HitMeshCache& cache = hit_cache_;
    cache.tick_frame(box);
    render::g_current_cache = &cache; // the feelers' current cache
}

// 0x00527BE0
void FootMovement::restore_display_offset()
{
    int state = state_;
    if (state == 2 || state == 4)
        return;
    float rate = walk_parameter(kDisplayOffsetRestoreRate);
    double step = frame_clock::get_slomo_frame_seconds() * rate;
    float& offset = model_lift_;
    offset = static_cast<float>(-static_cast<double>(offset) * step + offset);
}

// 0x00527C50
void FootMovement::update_push()
{
    if (!pushing_)
        return;
    maths::Vec4& pos = pos_;
    if (pos.y > push_height_) {
        pushing_ = false;
        return;
    }
    const maths::Vec4& normal = push_normal_;
    auto distance =
        static_cast<float>(((static_cast<double>(normal.z) * pos.z + static_cast<double>(normal.y) * pos.y) +
                            static_cast<double>(normal.x) * pos.x) -
                           push_plane_distance_);
    if (!(-walk_parameter(kPushFeelerLength) < distance))
        return;
    // Within reach of the plane: pushed away from it, and the velocity into it taken away.
    double push = static_cast<double>(walk_parameter(kPushStrength)) * get_frame_length(this);
    double push_x = push * normal.x; // (x's kept unrounded, as the original's register)
    auto push_y = static_cast<float>(push * normal.y);
    auto push_z = static_cast<float>(push * normal.z);
    auto push_w = static_cast<float>(push * normal.w);
    pos.x = static_cast<float>(pos.x - push_x);
    pos.y = pos.y - push_y;
    pos.z = pos.z - push_z;
    pos.w = pos.w - push_w;
    maths::Vec4& vel = vel_;
    double into = (static_cast<double>(normal.z) * vel.z + static_cast<double>(normal.y) * vel.y) +
                  static_cast<double>(vel.x) * normal.x;
    if (into == 0.0)
        return;
    vel -= maths::operator*(static_cast<float>(into), normal);
}

// 0x00527DB0
void FootMovement::copy_from_object()
{
    auto* object = static_cast<GameEntity*>(entity_);
    pos_ = object->at_point_;
    maths::Vec4& vel = vel_;
    vel = object->vel_;
    vel.y = 0.0f;
    up_speed_ = object->vel_.y;
    maths::Vec4& up = up_;
    up = object->basis_frame_.row[1];
    maths::Vec4& facing = facing_;
    facing = object->basis_frame_.row[2];
    facing_y_ = facing.y;
    facing.y = 0.0f;
    double length = std::sqrt((static_cast<double>(facing.x) * facing.x + static_cast<double>(facing.y) * facing.y) +
                              static_cast<double>(facing.z) * facing.z);
    if (length < kMinFacingLength) {
        // Facing straight up or down: the way the up row leans, backwards; the up row nudged upright.
        const maths::Vec4& object_up = object->basis_frame_.row[1];
        facing = {-object_up.x, -object_up.y, -object_up.z, -object_up.w};
        facing.y = 0.0f;
        facing.make_unit();
        up.y = static_cast<float>(static_cast<double>(up.y) + kUprightNudge);
        up.make_unit();
        return;
    }
    double inverse = kOne / length;
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
    const maths::Vec4& at = camera_->basis_frame_.row[2];
    auto* pad = &input_->pad_primary_;
    float forward_x = -at.x;
    float forward_z = -at.z;
    double length = std::sqrt(static_cast<double>(forward_z) * forward_z + static_cast<double>(forward_x) * forward_x);
    if (length > 0.0) {
        double inverse = kOne / length;
        forward_x = static_cast<float>(forward_x * inverse);
        forward_z = static_cast<float>(forward_z * inverse);
    }
    float angle = pad->polar_[1].angle;
    if (walk_parameter(kForwardTolerance) * static_cast<double>(kRadiansPerDegree) > std::fabs(angle))
        angle = 0.0f;
    double turned_z = 0.0, turned_x = 0.0;
    maths::spin_by(angle, forward_z, forward_x, &turned_z, &turned_x);
    maths::Vec4& control = stick_heading_;
    control.x = static_cast<float>(turned_x);
    control.z = static_cast<float>(turned_z);

    float& magnitude = stick_deflection_;
    bool& full = control_full_;
    if (!(pad->stick_[2] == 0.0f) || !(pad->stick_[3] == 0.0f)) {
        // The stick: its push, full from 85 %.
        stick_steers_ = true;
        double push = stick_length(pad->stick_[2], pad->stick_[3], pad->polar_[1].angle) * kStickScale;
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
        if (pad->polar_[1].length == 0.0f) {
            magnitude = 0.0f;
            full = false;
        } else if (state_ == 0 && !pad->pos_x_.held_down_) {
            magnitude = walk_parameter(kWalkPoint);
            full = true;
        } else {
            magnitude = kOne;
            full = true;
        }
    }
    if (state_ == 0 && locked_to_heading_)
        control = facing_;
}

// 0x00528120
double FootMovement::get_control_speed()
{
    float magnitude = stick_deflection_;
    if (magnitude == 0.0f)
        return 0.0;
    float walk_point = walk_parameter(kWalkPoint);
    if (magnitude <= walk_point) {
        // Walking: from walk_start's push up to walk_point, up to walk_speed.
        running_ = false;
        float walk_speed = walk_parameter(kWalkSpeed);
        return (static_cast<double>(magnitude) - kWalkStart) / (static_cast<double>(walk_point) - kWalkStart) *
               walk_speed;
    }
    // Running: from walk_speed at walk_point up to the run speed at a full push.
    running_ = true;
    float walk_speed = walk_parameter(kWalkSpeed);
    auto t =
        static_cast<float>((static_cast<double>(magnitude) - walk_point) / (static_cast<double>(kOne) - walk_point));
    double speed_of_run = run_speed();
    return (speed_of_run - walk_speed) * t + walk_speed;
}

// 0x00526850
void FootMovement::steer_jump()
{
    float& delay = leap_duration_;
    if (!(delay == 0.0f)) {
        // (still counting down: no steering yet)
        double left = static_cast<double>(delay) - get_frame_length(this);
        delay = left < 0.0 ? 0.0f : static_cast<float>(left);
        return;
    }
    // The velocity apart from what the jump carries along unsteered, eased toward what the push asks for: across
    // the jump's direction and along it.
    maths::Vec4& vel = vel_;
    const maths::Vec4& carried = jump_base_vel_;
    vel = {vel.x - carried.x, vel.y - carried.y, vel.z - carried.z, vel.w - carried.w};
    const maths::Vec4& dir = leap_heading_;
    const maths::Vec4& control = stick_heading_;
    float magnitude = stick_deflection_;
    float side_x = -dir.z, side_z = dir.x; // (the jump's sideways direction: y 0, w 1)
    auto side_target =
        static_cast<float>(((static_cast<double>(side_z) * control.z + static_cast<double>(side_x) * control.x) +
                            static_cast<double>(control.y) * 0.0) *
                           walk_parameter(kJumpAdjustSpeed) * magnitude);
    auto side = static_cast<float>((static_cast<double>(side_z) * vel.z + static_cast<double>(side_x) * vel.x) +
                                   static_cast<double>(vel.y) * 0.0);
    side = static_cast<float>((static_cast<double>(side_target) - side) *
                                  (static_cast<double>(walk_parameter(kJumpAccelFactor)) * get_frame_length(this)) +
                              side);
    auto forward_target =
        static_cast<float>(((static_cast<double>(control.z) * dir.z + static_cast<double>(control.y) * dir.y) +
                            static_cast<double>(control.x) * dir.x) *
                           walk_parameter(kJumpAdjustSpeed) * magnitude);
    auto forward = static_cast<float>((static_cast<double>(vel.z) * dir.z + static_cast<double>(vel.y) * dir.y) +
                                      static_cast<double>(vel.x) * dir.x);
    bool ease = false;
    if (!jump_unsteered_ && stick_deflection_ == 0.0f) {
        // no push: along the jump, eased to a stop
        forward_target = 0.0f;
        forward = 0.0f;
        ease = true;
    } else {
        if (jump_unsteered_)
            side = 0.0f;
        // Faster ahead than asked: kept. Pushing back while moving ahead: from the speed there.
        ease = !(forward_target >= 0.0f && forward > forward_target);
        if (ease && forward_target < 0.0f && forward > 0.0f)
            forward_target = forward + forward_target;
    }
    if (ease)
        forward =
            static_cast<float>((static_cast<double>(forward_target) - forward) *
                                   (static_cast<double>(walk_parameter(kJumpAccelFactor)) * get_frame_length(this)) +
                               forward);
    float side_y = side * 0.0f;
    float side_zpart = side_z * side;
    vel.x = static_cast<float>(static_cast<double>(side_x) * side + static_cast<double>(forward) * dir.x);
    vel.y = forward * dir.y + side_y;
    vel.z = forward * dir.z + side_zpart;
    vel.w = forward * dir.w + side;
    vel = {carried.x + vel.x, carried.y + vel.y, carried.z + vel.z, carried.w + vel.w};
}

// 0x00528950
void FootMovement::update_ground_velocity(maths::Vec4* velocity)
{
    auto speed = static_cast<float>(
        std::sqrt((static_cast<double>(velocity->x) * velocity->x + static_cast<double>(velocity->y) * velocity->y) +
                  static_cast<double>(velocity->z) * velocity->z));
    double wanted = get_control_speed();
    auto target = static_cast<float>(wanted);
    anim_state_ = wanted <= 0.0 ? kStand : kRun;

    // The way it's going: the velocity's (past ignore_velocity_threshold), else the push's, else the velocity's
    // (past 0.01), else the facing.
    float magnitude = stick_deflection_;
    maths::Vec4 direction{};
    double direction_w = 0.0;
    if (speed > walk_parameter(kIgnoreVelocityThreshold)) {
        double inverse = kOne / static_cast<double>(speed);
        auto inverse_f = static_cast<float>(inverse);
        direction = {inverse_f * velocity->x, inverse_f * velocity->y, inverse_f * velocity->z, 0.0f};
        direction_w = inverse * velocity->w;
    } else if (!(magnitude == 0.0f)) {
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

    if (magnitude > 0.0f) {
        const maths::Vec4& control = stick_heading_;
        maths::Vec4& facing = facing_;
        // Pushed against the way it's going: no speed asked for.
        if ((static_cast<double>(direction.z) * control.z + static_cast<double>(direction.y) * control.y) +
                static_cast<double>(direction.x) * control.x <=
            0.0)
            target = 0.0f;
        double cosine = (static_cast<double>(direction.z) * facing.z + static_cast<double>(direction.y) * facing.y) +
                        static_cast<double>(direction.x) * facing.x;
        auto cosine_f = static_cast<float>(cosine);
        float clamped = cosine > kOne ? kOne : (cosine_f < -kOne ? -kOne : cosine_f);
        if (acos_degrees(clamped) > walk_parameter(kSlopLockAngle)) {
            // Far off the facing: turned in place at rotate_in_place_rate (walking: x walk_rotate_factor; against a
            // wall, or a wall where it turns to: x wall_rotate_factor), going the pushed way.
            float rate = walk_parameter(kRotateInPlaceRate);
            auto turn = static_cast<float>(frame_clock::get_slomo_frame_seconds() * stick_deflection_ * rate *
                                           kRadiansPerDegree);
            bool left = static_cast<double>(facing.x) * control.z - static_cast<double>(control.x) * facing.z < 0.0;
            if (!running_)
                turn = static_cast<float>(static_cast<double>(walk_parameter(kWalkRotateFactor)) * turn);
            if (blocked_) {
                turn = static_cast<float>(static_cast<double>(walk_parameter(kWallRotateFactor)) * turn);
            } else {
                double turned_x = 0.0, turned_z = 0.0;
                maths::spin_by(left ? turn : -turn, facing.x, facing.z, &turned_x, &turned_z);
                auto ahead_x = static_cast<float>(turned_x), ahead_z = static_cast<float>(turned_z);
                render::ProbeRay feeler;
                maths::Vec4& start = feeler.line_.start;
                start = pos_;
                start.y = static_cast<float>(static_cast<double>(start.y) + walk_parameter(kProbeElevation));
                float reach = walk_parameter(kFeelerLength);
                float none = reach * 0.0f;
                feeler.line_.end = {ahead_x * reach + start.x, none + start.y, ahead_z * reach + start.z,
                                    none + start.w};
                // (the original reads wall_rotate_factor through 0x0043DA70)
                if (feeler.get_hit_shape(false, false))
                    turn = static_cast<float>(static_cast<double>(walk_parameter(kWallRotateFactor)) * turn);
            }
            double new_x = 0.0, new_z = 0.0;
            maths::spin_by(left ? turn : -turn, facing.x, facing.z, &new_x, &new_z);
            facing.z = static_cast<float>(new_z);
            facing.x = static_cast<float>(new_x);
            // Turned past the push: onto it.
            bool still_left = new_x * control.z - static_cast<double>(control.x) * facing.z < 0.0;
            if (left != still_left)
                facing = control;
            result = control;
        } else {
            // Near the facing: the facing eased toward the push, the more the further off (worse_turn_factor to
            // worse_worse_turn_factor), going the facing's way.
            double off =
                kOne - ((static_cast<double>(control.z) * facing.z + static_cast<double>(control.y) * facing.y) +
                        static_cast<double>(control.x) * facing.x);
            off = off + off;
            auto off_f = static_cast<float>(off);
            float k = off > kOne ? kOne : (off_f < 0.0f ? 0.0f : off_f);
            float worse = walk_parameter(kWorseTurnFactor);
            float worst = walk_parameter(kWorseWorseTurnFactor);
            auto rate = static_cast<float>(((static_cast<double>(worse) - worst) * k + worst) * stick_deflection_);
            double step = frame_clock::get_slomo_frame_seconds() * rate;
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
        if (allowed == 0.0f && !(speed > walk_parameter(kWalkSpeed)))
            factor = walk_parameter(kLowSpeedDecelFactor);
        new_speed = (static_cast<double>(allowed) - speed) * (frame_clock::get_slomo_frame_seconds() * factor) + speed;
    } else {
        float accel = walk_parameter(running_ ? kRunAccelRate : kWalkAccelRate);
        new_speed = frame_clock::get_slomo_frame_seconds() * accel + speed;
        if (new_speed > allowed)
            new_speed = allowed;
    }
    *velocity = {static_cast<float>(result.x * new_speed), static_cast<float>(result.y * new_speed),
                 static_cast<float>(result.z * new_speed), static_cast<float>(result.w * new_speed)};
}

// 0x00529800
void FootMovement::update_rotate_upright()
{
    float& left = upright_time_;
    if (left == 0.0f)
        return;
    // This frame's share of the turn (+0x730 about +0x734) over rotate_upright_duration.
    float duration = walk_parameter(kRotateUprightDuration);
    auto angle = static_cast<float>(get_frame_length(this) / duration * upright_angle_);
    maths::Mat4 rotation{};
    maths::build_axis_angle_matrix(rotation, upright_axis_, angle);
    auto* object = static_cast<GameEntity*>(entity_);
    // D3DXMatrixMultiply (0x005BFA8C)
    d3dx::matrix_multiply(&object->basis_frame_, &object->basis_frame_, &rotation);
    object->display_matrix_ = object->basis_frame_;
    double remaining = static_cast<double>(left) - get_frame_length(this);
    left = remaining < 0.0 ? 0.0f : static_cast<float>(remaining);
}

// 0x005298D0
void FootMovement::apply_transition()
{
    auto* object = static_cast<GameEntity*>(entity_);
    const maths::Vec4& at = transition_pos_;
    const maths::Vec4& offset = climb_offset_;
    object->at_point_ = {at.x + offset.x, at.y + offset.y, at.z + offset.z, at.w + offset.w};
    // Upright, facing the transition's facing (row 0's w 1, as the original).
    const maths::Vec4& facing = transition_facing_;
    maths::Mat4 matrix{};
    matrix.row[0] = {facing.z, 0.0f, -facing.x, 1.0f};
    matrix.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    matrix.row[2] = facing;
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    object->basis_frame_ = matrix;
    model_lift_ = transition_display_offset_;
    // Then the transition's member function, if any (+0x1948, this adjusted by +0x194C).
    script::ObjectCallback callback = anim_done_.code_;
    if (callback)
        callback(reinterpret_cast<uint8_t*>(this) + anim_done_.this_adjust_);
}

// 0x00529CE0
void FootMovement::copy_to_object()
{
    // The facing tilted back up by its y (+0x744), keeping it unit.
    float facing_y = facing_y_;
    double flat = kOne - static_cast<double>(facing_y) * facing_y;
    double k = flat > 0.0 ? std::sqrt(flat) : static_cast<double>(kNoFlat);
    maths::Vec4& facing = facing_;
    facing = {static_cast<float>(k * facing.x), static_cast<float>(k * facing.y), static_cast<float>(k * facing.z),
              static_cast<float>(k * facing.w)};
    facing.y = facing_y;
    // The orientation from the up vector and the facing.
    const maths::Vec4& up = up_;
    maths::Mat4 matrix{};
    maths::Vec4& right = matrix.row[0];
    right = {static_cast<float>(static_cast<double>(up.y) * facing.z - static_cast<double>(up.z) * facing.y),
             static_cast<float>(static_cast<double>(up.z) * facing.x - static_cast<double>(up.x) * facing.z),
             static_cast<float>(static_cast<double>(up.x) * facing.y - static_cast<double>(up.y) * facing.x), 0.0f};
    right.make_unit();
    matrix.row[1] = up;
    matrix.row[2] = {static_cast<float>(static_cast<double>(up.z) * right.y - static_cast<double>(up.y) * right.z),
                     static_cast<float>(static_cast<double>(up.x) * right.z - static_cast<double>(up.z) * right.x),
                     static_cast<float>(static_cast<double>(up.y) * right.x - static_cast<double>(up.x) * right.y),
                     0.0f};
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto* object = static_cast<GameEntity*>(entity_);
    object->at_point_ = pos_;
    object->basis_frame_ = matrix;
    object->display_matrix_ = matrix;
    // The object's velocity by state.
    maths::Vec4& vel = object->vel_;
    switch (state_) {
    case 0: // on the ground, in the air
    case 1:
        vel = vel_;
        vel.y = up_speed_;
        break;
    case 2: { // hanging: sideways along the ledge
        float scale = climb_scale_;
        vel = {scale * -facing.z, scale * 0.0f, scale * facing.x, scale * 1.0f};
        break;
    }
    case 3: // climbing: up at its speed
        vel = {0.0f, speed_, 0.0f, 1.0f};
        break;
    default:
        vel = {0.0f, 0.0f, 0.0f, 1.0f};
        break;
    }
    // The model raised along the up vector by the display offset.
    float offset = model_lift_;
    model_->shift_ = {matrix.row[1].x * offset, matrix.row[1].y * offset, matrix.row[1].z * offset,
                      matrix.row[1].w * offset};
}

} // namespace objects
