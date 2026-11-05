// The Vehicle component's physics: the body's dependents, the wheels' suspension, grip, drive, brakes and steering,
// the controls, sleeping, and the skater carried along. See vehicle_component.h. From the disassembly of
// 0x00435B60-0x00436CE2, 0x00437550-0x00437A54, 0x00438A20-0x00438BA1 and 0x0043AE10-0x0043B661. Float arithmetic
// follows the original's x87 code: double (53 bits) where it stays on the stack, rounded to float where stored.

#include "game/world/vehicle_component.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

#include "engine/gfx/debug_lines.h"
#include "base/maths/shape_tests.h"
#include "game/online/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/render/coll_movable.h"
#include "engine/objects/anim_blender.h"
#include "game/world/vehicle_internal.h"
#include "engine/render/probe_ray.h"
#include "engine/sys/game_clock.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "game/online/net_game_state.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_obj.h"

namespace objects {

using namespace vehicle;

namespace {
constexpr int kPassMinMs = 300;
constexpr int kLastLoadIndex = 9;
constexpr int kLoadHistoryLength = 10;
constexpr double kStandstillSpeed = 1e-6;
constexpr float kBlendFullSpeed = 1000.0F;
constexpr int kPeriodTicks = 15;
constexpr uint32_t kGravityLineColour = 0xFF003232; // ARGB: the gravity line
constexpr uint32_t kWheelLoadColour = 0xFF006464; // ARGB: a wheel load force
constexpr uint32_t kWheelDriveColour = 0xFFFF0000; // ARGB: a wheel drive force
constexpr uint32_t kExitVehicleRequest = 0xCBAA3476; // exitvehiclerequest (letter case unknown)
int& vehicle_net_update_counter()
{
    static int vehicle_net_update_counter{};
    return vehicle_net_update_counter;
}

const float kHalf = 0.5F;                                         // 0x006453BC
const float kTenth = std::bit_cast<float>(0x3DCCCCCDU);           // 0x00645D74
const float kRightingGravity = std::bit_cast<float>(0x42E7D70BU); // 0x00646718: 115.92
const float kNoSteering = std::bit_cast<float>(0x3A83126FU);      // 0x006461D8: 0.001
const float kSteeringRate = 10.0F;                                // 0x00646710
const float kUprightBoost = std::bit_cast<float>(0x3F8E38E4U);    // 0x00646728: 1.1111112 (1 / 0.9)
const float kReverseSpeed = 300.0F;                               // 0x0064675C
const float kStoppedSpeed = 30.0F;                                // 0x00646240
const float kMinSlip = 1e-6F;                                     // (0x00646720 is the double 1e-6)
const float kSleepSpeed = 1.0F;                                   // 0x006453A8
const float kSleepSpin = std::bit_cast<float>(0x38D1B717U);       // 0x006453D4: 0.0001
const double kTwoTo32 = 4294967296.0;                             // 0x006453B4

// The trigger events a wheel's feeler is reported for (const int[3] each, for the trigger component's 0x00434BF0).

constexpr uint32_t kParkEditorOff = 0xD443A2BC;

// An unsigned count as the original's FILD (and its 2^32 fix for negatives).
double as_unsigned(int value) { return value < 0 ? value + kTwoTo32 : value; }

// The cap on a brake-like torque: what stops the wheel in this step, |(-1 / (dt * inv_moment_)) * spin| as a float.
float brake_turn_force(double per_step, float spin) { return static_cast<float>(std::fabs(-1.0 / per_step * spin)); }

// A torque on the wheel (already times its inverse moment): its spin changed over the step, and summed.
void spin_wheel(DrivableModule::Wheel& wheel, double torque, float timestep)
{
    wheel.spin = static_cast<float>((torque * timestep) + wheel.spin);
    wheel.torque = static_cast<float>(torque + wheel.torque);
}

// The steering angles for a steering input `steering` (-1..1): the wheel on the inside of the turn (by Ackermann)
// as a float, the outer one unrounded.
void steering_angles(const DrivableModule* vehicle, float steering, float* inner, double* outer)
{
    if (!(std::fabs(steering) > kNoSteering)) {
        *inner = 0.0F;
        *outer = 0.0;
        return;
    }
    const double radius = vehicle->wheelbase() / std::tan(static_cast<double>(vehicle->get_max_steering_angle()) * steering);
    const double half_track = static_cast<double>(vehicle->track()) * kHalf;
    const double wheelbase = vehicle->wheelbase();
    if (radius > 0.0) {
        *inner = static_cast<float>(-std::atan2(wheelbase, radius - half_track));
        *outer = -std::atan2(wheelbase, half_track + radius);
    } else {
        *inner = static_cast<float>(std::atan2(wheelbase, half_track - radius));
        *outer = std::atan2(wheelbase, -radius - half_track);
    }
}

} // namespace

// 0x00435B60
void DrivableModule::update_wheel_friction()
{
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.state == DrivableModule::WheelState::kInAir) {
            continue;
        }
        // The velocity at the wheel along the ground.
        maths::Vec4 vel = wheel.vel;
        vel.flatten_onto_plane(wheel.normal);
        // The tyre's sideways direction: the body's right turned by the steering, along the ground, unit.
        const double cosine = std::cos(static_cast<double>(wheel.steering));
        const float sine = std::sin(wheel.steering);
        const auto& rows = basis_frame_.row;
        maths::Vec4 sideways;
        sideways.x =
            static_cast<float>(((cosine * core::at(rows, 2).x) + (static_cast<double>(sine) * core::at(rows, 0).x)) +
                               (core::at(rows, 1).x * 0.0));
        sideways.y =
            static_cast<float>(((static_cast<double>(sine) * core::at(rows, 0).y) + (cosine * core::at(rows, 2).y)) +
                               (core::at(rows, 1).y * 0.0));
        sideways.z =
            static_cast<float>(((static_cast<double>(sine) * core::at(rows, 0).z) + (cosine * core::at(rows, 2).z)) +
                               (core::at(rows, 1).z * 0.0));
        sideways.w = 1.0F;
        sideways.flatten_onto_plane(wheel.normal);
        const double length = std::sqrt(
            ((static_cast<double>(sideways.z) * sideways.z) + (static_cast<double>(sideways.y) * sideways.y)) +
            (static_cast<double>(sideways.x) * sideways.x));
        if (length > 0.0) {
            const double scale = 1.0 / length;
            sideways.x = static_cast<float>(sideways.x * scale);
            sideways.y = static_cast<float>(sideways.y * scale);
            sideways.z = static_cast<float>(sideways.z * scale);
        }
        wheel.sideways = sideways;
        wheel.contact_vel_ = vel;
        // The slip: the ground velocity less the tyre's rolling along its sideways direction.
        const double rolling = static_cast<double>(wheel.radius) * wheel.spin;
        auto roll_y = static_cast<float>(sideways.y * rolling);
        auto roll_z = static_cast<float>(sideways.z * rolling);
        auto roll_w = static_cast<float>(sideways.w * rolling);
        vel.x = static_cast<float>(vel.x - (sideways.x * rolling));
        vel.y = static_cast<float>(static_cast<double>(vel.y) - roll_y);
        vel.z = static_cast<float>(static_cast<double>(vel.z) - roll_z);
        vel.w = static_cast<float>(static_cast<double>(vel.w) - roll_w);
        auto slip =
            static_cast<float>(std::sqrt(((static_cast<double>(vel.y) * vel.y) + (static_cast<double>(vel.z) * vel.z)) +
                                         (static_cast<double>(vel.x) * vel.x)));
        wheel.friction = static_cast<float>(get_tyre_friction(&wheel, slip));
    }
}

// 0x00435DA0
void DrivableModule::refresh_hit_cache()
{
    maths::BBox box;
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        box.append_spot(core::at(wheels_, i).feeler_start_);
        box.append_spot(core::at(wheels_, i).feeler_end_);
    }
    for (int i = 1; i >= 0; --i) {
        const Collider& collider = core::at(colliders_, i);
        const maths::Vec4& origin = collider.world_corner_;
        const maths::Vec4& edge_a = core::at(collider.world_edges_, 0);
        const maths::Vec4& edge_b = core::at(collider.world_edges_, 1);
        box.append_spot(origin);
        maths::Vec4 corner;
        corner = {.x = static_cast<float>(static_cast<double>(origin.x) + edge_a.x),
                  .y = static_cast<float>(static_cast<double>(edge_a.y) + origin.y),
                  .z = static_cast<float>(static_cast<double>(origin.z) + edge_a.z),
                  .w = static_cast<float>(static_cast<double>(origin.w) + edge_a.w)};
        box.append_spot(corner);
        corner = {.x = static_cast<float>(static_cast<double>(origin.x) + edge_b.x),
                  .y = static_cast<float>(static_cast<double>(edge_b.y) + origin.y),
                  .z = static_cast<float>(static_cast<double>(origin.z) + edge_b.z),
                  .w = static_cast<float>(static_cast<double>(origin.w) + edge_b.w)};
        box.append_spot(corner);
        // The far corner: x and y summed unrounded, z and w rounded halfway.
        auto z_half = static_cast<float>(static_cast<double>(origin.z) + edge_a.z);
        auto w_half = static_cast<float>(static_cast<double>(origin.w) + edge_a.w);
        corner = {.x = static_cast<float>((static_cast<double>(origin.x) + edge_a.x) + edge_b.x),
                  .y = static_cast<float>((static_cast<double>(edge_a.y) + origin.y) + edge_b.y),
                  .z = static_cast<float>(static_cast<double>(z_half) + edge_b.z),
                  .w = static_cast<float>(static_cast<double>(w_half) + edge_b.w)};
        box.append_spot(corner);
    }
    contact_memo_.tick_frame(box);
}

// 0x00435F00
void DrivableModule::update_steering()
{
    const double previous = display_steering_;
    auto display = static_cast<float>(
        ((static_cast<double>(controls_.steering) - previous) * (static_cast<double>(step_length_) * kSteeringRate)) +
        previous);
    display_steering_ = display;
    float inner = 0.0F;
    float display_inner = 0.0F;
    double outer = 0.0;
    double display_outer = 0.0;
    steering_angles(this, controls_.steering, &inner, &outer);
    steering_angles(this, display, &display_inner, &display_outer);
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.steering_side_ == 0) {
            continue;
        }
        wheel.steering = wheel.steering_side_ == 1 ? inner : static_cast<float>(outer);
        wheel.display_steering_ = wheel.steering_side_ == 1 ? display_inner : static_cast<float>(display_outer);
    }
}

// 0x004360C0
void DrivableModule::apply_damping()
{
    if (controls_.handbrake && static_cast<uint32_t>(wheels_in_contact_) > 2) {
        return;
    }
    if (!controls_.throttle && !(controls_.steering == 0.0F) && static_cast<uint32_t>(wheels_in_contact_) > 2) {
        return;
    }
    if (kick_time_ > 0.0F) {
        return;
    }
    // The spin damped by a part growing with its square; all of it when that's more than there is.
    const double x = rotmom_.x;
    const double y = rotmom_.y;
    const double z = rotmom_.z;
    const double damping = (((x * x) + (y * y)) + (z * z)) * damping_.spin * step_length_;
    auto damping_rounded = static_cast<float>(damping);
    const double length = std::sqrt(((x * x) + (y * y)) + (z * z));
    if (damping > length) {
        rotmom_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    } else {
        maths::Vec4 axis = rotmom_;
        axis.make_unit();
        auto dz = static_cast<float>(static_cast<double>(axis.z) * damping_rounded);
        auto dw = static_cast<float>(static_cast<double>(axis.w) * damping_rounded);
        rotmom_.x = static_cast<float>(rotmom_.x - (static_cast<double>(axis.x) * damping_rounded));
        rotmom_.y = static_cast<float>(rotmom_.y - (static_cast<double>(axis.y) * damping_rounded));
        rotmom_.z = static_cast<float>(static_cast<double>(rotmom_.z) - dz);
        rotmom_.w = static_cast<float>(static_cast<double>(rotmom_.w) - dw);
    }
    // Upright on three or more wheels and not steering: the turning about up damped towards 0.
    if (controls_.steering == 0.0F && static_cast<uint32_t>(wheels_in_contact_) > 2 &&
        core::at(basis_frame_.row, 1).y > kHalf) {
        double yaw = static_cast<double>(damping_.yaw) * step_length_;
        if (std::fabs(static_cast<double>(rotmom_.y)) < std::fabs(yaw)) {
            rotmom_.y = 0.0F;
            return;
        }
        if (rotmom_.y > 0.0F) {
            yaw = -yaw;
        }
        rotmom_.y = static_cast<float>(yaw + rotmom_.y);
    }
}

// 0x00436280
void DrivableModule::apply_gravity()
{
    double gravity = 0.0;
    if (righting_ && static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
                             (static_cast<uint32_t>(clock_offset()) - static_cast<uint32_t>(righting_start_)) >
                         kPassMinMs) {
        gravity = static_cast<double>(gravity_scale_) / mass_.inverse * kRightingGravity;
    } else {
        gravity = static_cast<double>(gravity_scale_) / mass_.inverse * kGravity;
    }
    force_.y = static_cast<float>(force_.y - gravity);
    if (debug_mode_ != 0) {
        const maths::Vec4 down{
            .x = at_point_.x,
            .y = static_cast<float>((-static_cast<double>(kGravity) / mass_.inverse * 0.05F) + at_point_.y),
            .z = at_point_.z,
            .w = static_cast<float>(static_cast<double>(at_point_.w) + 0.05F)};
        gfx::push_diag_line(&at_point_, &down, kGravityLineColour, kGravityLineColour, 1);
    }
}

// 0x00436340
void DrivableModule::apply_suspension()
{
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.state == DrivableModule::WheelState::kInAir) {
            // (In the air the history gets an even share of the weight; the average isn't updated.)
            core::at(wheel.load_history_, load_index_) =
                static_cast<float>(static_cast<double>(kGravity) / mass_.inverse / as_unsigned(chassis_.wheel_count));
            continue;
        }
        // The spring (by how far the suspension is compressed along the ground's normal) and the damper.
        const maths::Vec4& normal = wheel.normal;
        const maths::Vec4& up = core::at(basis_frame_.row, 1);
        const double up_along_normal =
            ((static_cast<double>(up.z) * normal.z) + (static_cast<double>(up.y) * normal.y)) +
            (static_cast<double>(up.x) * normal.x);
        const double spring = -(up_along_normal * (static_cast<double>(wheel.rest_y_) - wheel.y) * wheel.spring_rate_);
        const double closing =
            ((static_cast<double>(wheel.vel.z) * normal.z) + (static_cast<double>(wheel.vel.y) * normal.y)) +
            (static_cast<double>(wheel.vel.x) * normal.x);
        const double force = spring - (closing * wheel.damping_rate_);
        auto force_rounded = static_cast<float>(force);
        const maths::Vec4 push{.x = static_cast<float>(force * normal.x),
                               .y = static_cast<float>(static_cast<double>(force_rounded) * normal.y),
                               .z = static_cast<float>(static_cast<double>(force_rounded) * normal.z),
                               .w = static_cast<float>(static_cast<double>(force_rounded) * normal.w)};
        add_force_at_point(&push, &wheel.pos, kWheelLoadColour);
        core::at(wheel.load_history_, load_index_) = force_rounded;
        // The load: the average of the last ten.
        wheel.load = 0.0F;
        for (int k = kLastLoadIndex; k >= 0; --k) {
            wheel.load = static_cast<float>(static_cast<double>(core::at(wheel.load_history_, k)) + wheel.load);
        }
        wheel.load = static_cast<float>(static_cast<double>(wheel.load) * kTenth);
    }
    auto next = static_cast<int8_t>(load_index_ + 1);
    load_index_ = static_cast<int8_t>(next % kLoadHistoryLength);
}

// 0x004364B0
void DrivableModule::apply_tyre_forces()
{
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.state == DrivableModule::WheelState::kInAir) {
            continue;
        }
        // The slip velocity: the ground velocity less the tyre's rolling (as update_wheel_friction).
        const maths::Vec4 side = wheel.sideways;
        const double rolling = static_cast<double>(wheel.spin) * wheel.radius;
        auto roll_y = static_cast<float>(side.y * rolling);
        auto roll_z = static_cast<float>(side.z * rolling);
        auto roll_w = static_cast<float>(side.w * rolling);
        maths::Vec4 slip;
        slip.x = static_cast<float>(wheel.contact_vel_.x - (side.x * rolling));
        slip.y = static_cast<float>(static_cast<double>(wheel.contact_vel_.y) - roll_y);
        slip.z = static_cast<float>(static_cast<double>(wheel.contact_vel_.z) - roll_z);
        slip.w = static_cast<float>(static_cast<double>(wheel.contact_vel_.w) - roll_w);
        const double speed =
            std::sqrt(((static_cast<double>(slip.z) * slip.z) + (static_cast<double>(slip.y) * slip.y)) +
                      (static_cast<double>(slip.x) * slip.x));
        auto speed_rounded = static_cast<float>(speed);
        wheel.slip = speed_rounded;
        if (speed < kStandstillSpeed) {
            continue;
        }
        // The friction against the slip's direction.
        double dir_x = slip.x;
        float dir_y = slip.y;
        float dir_z = slip.z;
        if (speed_rounded > 0.0F) {
            const double inverse = 1.0 / speed_rounded;
            auto inverse_rounded = static_cast<float>(inverse);
            dir_x = inverse * slip.x;
            dir_y = static_cast<float>(static_cast<double>(slip.y) * inverse_rounded);
            dir_z = static_cast<float>(static_cast<double>(slip.z) * inverse_rounded);
        }
        auto scale = static_cast<float>(-(static_cast<double>(wheel.friction) * wheel.load));
        const double friction_x = dir_x * scale;
        const maths::Vec4 friction{.x = 0.0F,
                                   .y = static_cast<float>(static_cast<double>(dir_y) * scale),
                                   .z = static_cast<float>(static_cast<double>(dir_z) * scale),
                                   .w = static_cast<float>(static_cast<double>(slip.w) * scale)};
        // The torque that would stop the slip along the tyre this step, and the friction's own torque on it: the
        // smaller one acts on the wheel; when it's the stopping one, the rest of it pushes the body.
        auto stop =
            static_cast<float>((((static_cast<double>(slip.z) * side.z) + (static_cast<double>(slip.y) * side.y)) +
                                (static_cast<double>(slip.x) * side.x)) /
                               wheel.radius / (static_cast<double>(step_length_) * wheel.inv_moment_));
        const double along = ((static_cast<double>(friction.z) * side.z) + (static_cast<double>(friction.y) * side.y)) +
                             (friction_x * side.x);
        const double friction_torque = -(along * wheel.radius);
        float wheel_torque = 0.0F;
        maths::Vec4 push;
        if (std::fabs(static_cast<double>(stop)) > std::fabs(friction_torque)) {
            push = {.x = static_cast<float>(friction_x), .y = friction.y, .z = friction.z, .w = friction.w};
            wheel_torque = static_cast<float>(friction_torque);
        } else {
            const double rest = (-1.0 / wheel.radius * stop) - along;
            const maths::Vec4 extra{.x = static_cast<float>(side.x * rest), .y = static_cast<float>(side.y * rest),
                                    .z = static_cast<float>(side.z * rest), .w = static_cast<float>(side.w * rest)};
            push = {.x = static_cast<float>(friction_x + extra.x),
                    .y = static_cast<float>(static_cast<double>(extra.y) + friction.y),
                    .z = static_cast<float>(static_cast<double>(extra.z) + friction.z),
                    .w = static_cast<float>(static_cast<double>(extra.w) + friction.w)};
            wheel_torque = stop;
        }
        add_force_at_point(&push, &wheel.pos, kWheelDriveColour);
        spin_wheel(wheel, static_cast<double>(wheel_torque) * wheel.inv_moment_, step_length_);
    }
}

// 0x004367E0
void DrivableModule::apply_engine_braking()
{
    if (controls_.throttle || controls_.reverse || controls_.brake) {
        return;
    }
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (!wheel.drive || wheel.state == DrivableModule::WheelState::kInAir) {
            continue;
        }
        const int gear = drivetrain_.get_gear(static_cast<float>(std::fabs(static_cast<double>(wheel.spin))));
        const double ratio = core::at(drivetrain_.ratios, gear);
        double braking = ratio * drivetrain_.engine_braking * ratio / as_unsigned(chassis_.drive_wheel_count);
        const float stop = brake_turn_force(static_cast<double>(wheel.inv_moment_) * step_length_, wheel.spin);
        braking = std::min<double>(braking, stop);
        if (!(wheel.spin < 0.0F)) {
            braking = -braking;
        }
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00436900
void DrivableModule::apply_drive_torque()
{
    // Less drive on a slope (up.y squared / 0.9, at most 1).
    const double upright = static_cast<double>(core::at(basis_frame_.row, 1).y) * core::at(basis_frame_.row, 1).y * kUprightBoost;
    const float grip = upright > 1.0 ? 1.0F : static_cast<float>(upright);
    if (!(controls_.throttle || controls_.reverse) || controls_.brake) {
        return;
    }
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (!wheel.drive || (controls_.handbrake && wheel.steering_side_ != 0)) {
            continue;
        }
        float spin = 0.0F;
        if (!controls_.reverse) {
            spin = wheel.spin > 0.0F ? wheel.spin : 0.0F;
        } else {
            spin = wheel.spin < 0.0F ? -wheel.spin : 0.0F;
        }
        const int gear = drivetrain_.get_gear(spin);
        double torque = static_cast<double>(core::at(drivetrain_.ratios, gear)) * drivetrain_.final_drive_ratio *
                        drivetrain_.engine_torque / as_unsigned(chassis_.drive_wheel_count) * grip;
        if (controls_.reverse) {
            torque = -(torque * drivetrain_.reverse_ratio);
        }
        if (controls_.handbrake && chassis_.drive_wheel_count == 4) {
            torque = torque + torque;
        }
        spin_wheel(wheel, torque * wheel.inv_moment_, step_length_);
    }
}

// 0x00436A90
void DrivableModule::apply_brakes()
{
    if (!controls_.brake) {
        return;
    }
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        double braking = wheel.brake_torque_;
        const float stop = brake_turn_force(static_cast<double>(step_length_) * wheel.inv_moment_, wheel.spin);
        if (braking > stop) {
            wheel.brake_excess_ = static_cast<float>(braking - stop);
            braking = stop;
        } else {
            wheel.brake_excess_ = 0.0F;
        }
        if (!(wheel.spin < 0.0F)) {
            braking = -braking;
        }
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00436B60
void DrivableModule::apply_handbrake(float scale)
{
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        double braking = static_cast<double>(scale) * wheel.handbrake_torque_;
        const float stop = brake_turn_force(static_cast<double>(step_length_) * wheel.inv_moment_, wheel.spin);
        if (braking > stop) {
            wheel.brake_excess_ = static_cast<float>(braking - stop);
            braking = stop;
        } else {
            wheel.brake_excess_ = 0.0F;
        }
        if (!(wheel.spin < 0.0F)) {
            braking = -braking;
        }
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00436C20
void DrivableModule::apply_brake_excess()
{
    if (!controls_.brake) {
        return;
    }
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.brake_excess_ == 0.0F) {
            continue;
        }
        double braking = wheel.brake_excess_;
        const float stop = brake_turn_force(static_cast<double>(step_length_) * wheel.inv_moment_, wheel.spin);
        braking = std::min<double>(braking, stop);
        if (!(wheel.spin < 0.0F)) {
            braking = -braking;
        }
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00438A20
void DrivableModule::update_inertia()
{
    // R * diag(inverse moments) * R^T.
    for (int row = 0; row < 3; ++row) {
        core::at(inv_inertia_.row, row)[0] = static_cast<float>(
            static_cast<double>(core::at(basis_frame_.row, row)[0]) * core::at(mass_.inverse_moments, 0));
        core::at(inv_inertia_.row, row)[1] = static_cast<float>(
            static_cast<double>(core::at(basis_frame_.row, row)[1]) * core::at(mass_.inverse_moments, 1));
        core::at(inv_inertia_.row, row)[2] = static_cast<float>(
            static_cast<double>(core::at(basis_frame_.row, row)[2]) * core::at(mass_.inverse_moments, 2));
    }
    render::MatrixTranspose transposed;
    transposed.transpose_from(&basis_frame_);
    maths::matrix_multiply(&inv_inertia_, &inv_inertia_, &transposed);
}

// 0x00438AE0
void DrivableModule::apply_wheel_forces()
{
    apply_gravity();
    apply_suspension();
    update_wheel_friction();
    if (!controls_.handbrake) {
        apply_drive_torque();
        apply_engine_braking();
        apply_brakes();
        apply_tyre_forces();
        apply_brake_excess();
        return;
    }
    if (controls_.throttle) {
        apply_drive_torque();
        apply_handbrake(static_cast<float>(1.0 - std::fabs(static_cast<double>(controls_.steering))));
        apply_tyre_forces();
        apply_brake_excess();
        return;
    }
    // The handbrake without throttle locks the wheels.
    apply_tyre_forces();
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        core::at(wheels_, i).spin = 0.0F;
        core::at(wheels_, i).torque = 0.0F;
    }
}

// 0x0043AE10
void DrivableModule::update_dependents()
{
    quat_to_matrix(&orientation_, &basis_frame_);
    core::at(basis_frame_.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    maths::Vec4 turned;
    com_offset_ = basis_frame_.rotate_vector(turned, mass_.balance_point);
    update_inertia();
    step_vehicle_speeds();
    const auto& rows = basis_frame_.row;
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        // The suspension's top: the wheel's place turned and moved (x summed unrounded).
        const maths::Vec4& offset = wheel.offset;
        double x = ((static_cast<double>(core::at(rows, 1).x) * offset.y) +
                    (static_cast<double>(core::at(rows, 2).x) * offset.z)) +
                   (static_cast<double>(core::at(rows, 0).x) * offset.x);
        auto y = static_cast<float>(((static_cast<double>(core::at(rows, 1).y) * offset.y) +
                                     (static_cast<double>(core::at(rows, 0).y) * offset.x)) +
                                    (static_cast<double>(core::at(rows, 2).y) * offset.z));
        auto z = static_cast<float>(((static_cast<double>(core::at(rows, 1).z) * offset.y) +
                                     (static_cast<double>(core::at(rows, 0).z) * offset.x)) +
                                    (static_cast<double>(core::at(rows, 2).z) * offset.z));
        float w = offset.w;
        wheel.feeler_start_.z = static_cast<float>(static_cast<double>(z) + at_point_.z);
        wheel.feeler_start_.w = static_cast<float>(static_cast<double>(w) + at_point_.w);
        wheel.feeler_start_.x = static_cast<float>(x + at_point_.x);
        wheel.feeler_start_.y = static_cast<float>(static_cast<double>(y) + at_point_.y);
        // The bottom: the place lowered to its rest less the radius.
        maths::Vec4 rest_offset = offset;
        rest_offset.y = static_cast<float>((static_cast<double>(wheel.rest_y_) - wheel.radius) + rest_offset.y);
        wheel.feeler_end_ = rest_offset;
        x = ((static_cast<double>(rest_offset.x) * core::at(rows, 0).x) +
             (static_cast<double>(core::at(rows, 2).x) * rest_offset.z)) +
            (static_cast<double>(core::at(rows, 1).x) * rest_offset.y);
        auto yy = static_cast<float>(((static_cast<double>(rest_offset.x) * core::at(rows, 0).y) +
                                      (static_cast<double>(rest_offset.y) * core::at(rows, 1).y)) +
                                     (static_cast<double>(core::at(rows, 2).y) * rest_offset.z));
        z = static_cast<float>(((static_cast<double>(core::at(rows, 2).z) * rest_offset.z) +
                                (static_cast<double>(core::at(rows, 1).z) * rest_offset.y)) +
                               (static_cast<double>(core::at(rows, 0).z) * rest_offset.x));
        w = rest_offset.w;
        wheel.feeler_end_.z = static_cast<float>(static_cast<double>(z) + at_point_.z);
        wheel.feeler_end_.w = static_cast<float>(static_cast<double>(w) + at_point_.w);
        wheel.feeler_end_.x = static_cast<float>(x + at_point_.x);
        wheel.feeler_end_.y = static_cast<float>(static_cast<double>(yy) + at_point_.y);
    }
    for (int i = 1; i >= 0; --i) {
        Collider& collider = core::at(colliders_, i);
        // The corner turned and moved; the edges turned (each with its own order of sums, as the original).
        const maths::Vec4& corner_origin = collider.corner;
        const double x = ((static_cast<double>(core::at(rows, 2).x) * corner_origin.z) +
                          (static_cast<double>(core::at(rows, 1).x) * corner_origin.y)) +
                         (static_cast<double>(corner_origin.x) * core::at(rows, 0).x);
        auto y = static_cast<float>(((static_cast<double>(core::at(rows, 0).y) * corner_origin.x) +
                                     (static_cast<double>(core::at(rows, 2).y) * corner_origin.z)) +
                                    (static_cast<double>(core::at(rows, 1).y) * corner_origin.y));
        auto z = static_cast<float>(((static_cast<double>(core::at(rows, 0).z) * corner_origin.x) +
                                     (static_cast<double>(core::at(rows, 2).z) * corner_origin.z)) +
                                    (static_cast<double>(core::at(rows, 1).z) * corner_origin.y));
        const float w = corner_origin.w;
        collider.world_corner_.z = static_cast<float>(static_cast<double>(z) + at_point_.z);
        collider.world_corner_.w = static_cast<float>(static_cast<double>(w) + at_point_.w);
        collider.world_corner_.x = static_cast<float>(x + at_point_.x);
        collider.world_corner_.y = static_cast<float>(static_cast<double>(y) + at_point_.y);
        const maths::Vec4& edge_a = core::at(collider.edges, 0);
        core::at(collider.world_edges_, 0).z =
            static_cast<float>(((static_cast<double>(core::at(rows, 2).z) * edge_a.z) +
                                (static_cast<double>(core::at(rows, 0).z) * edge_a.x)) +
                               (static_cast<double>(core::at(rows, 1).z) * edge_a.y));
        core::at(collider.world_edges_, 0).w = edge_a.w;
        core::at(collider.world_edges_, 0).x =
            static_cast<float>(((static_cast<double>(core::at(rows, 2).x) * edge_a.z) +
                                (static_cast<double>(core::at(rows, 1).x) * edge_a.y)) +
                               (static_cast<double>(edge_a.x) * core::at(rows, 0).x));
        core::at(collider.world_edges_, 0).y =
            static_cast<float>(((static_cast<double>(core::at(rows, 2).y) * edge_a.z) +
                                (static_cast<double>(core::at(rows, 0).y) * edge_a.x)) +
                               (static_cast<double>(core::at(rows, 1).y) * edge_a.y));
        const maths::Vec4& edge_b = core::at(collider.edges, 1);
        const float bw = edge_b.w;
        core::at(collider.world_edges_, 1).z =
            static_cast<float>(((static_cast<double>(core::at(rows, 2).z) * edge_b.z) +
                                (static_cast<double>(core::at(rows, 1).z) * edge_b.y)) +
                               (static_cast<double>(edge_b.x) * core::at(rows, 0).z));
        core::at(collider.world_edges_, 1).x =
            static_cast<float>(((static_cast<double>(core::at(rows, 0).x) * edge_b.x) +
                                (static_cast<double>(core::at(rows, 2).x) * edge_b.z)) +
                               (static_cast<double>(edge_b.y) * core::at(rows, 1).x));
        core::at(collider.world_edges_, 1).w = bw;
        core::at(collider.world_edges_, 1).y =
            static_cast<float>(((static_cast<double>(core::at(rows, 1).y) * edge_b.y) +
                                (static_cast<double>(core::at(rows, 2).y) * edge_b.z)) +
                               (static_cast<double>(core::at(rows, 0).y) * edge_b.x));
        for (int k = 0; k < 2; ++k) {
            const double inverse = 1.0 / core::at(collider.lengths, k);
            const maths::Vec4& edge = core::at(collider.world_edges_, k);
            core::at(collider.directions, k) = {.x = static_cast<float>(inverse * edge.x),
                                                .y = static_cast<float>(inverse * edge.y),
                                                .z = static_cast<float>(inverse * edge.z),
                                                .w = static_cast<float>(inverse * edge.w)};
        }
    }
    refresh_hit_cache();
}

// 0x0043B240
void DrivableModule::update_wheel_contacts()
{
    render::ProbeRay feeler;
    feeler.set_hit_cache(&contact_memo_);
    wheels_in_contact_ = 0;
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        feeler.line().start = wheel.feeler_start_;
        feeler.line().end = wheel.feeler_end_;
        feeler.set_excluded({.ignore = render::kDefaultIgnoredFaceFlags, .require = render::kNoFaceFlags});
        if (feeler.get_hit_shape(false, false) != 0) {
            if (wheel.state == DrivableModule::WheelState::kInAir) {
                trigger_feeler(trigger_ptr_, kWheelLandsEvents, &feeler);
                if (cut_short_ || ((entity()->object_flags() & kObjectFlagDead) != 0U)) {
                    return; // (the feeler's destructor lets go of its object)
                }
            } else if (feeler.hit().object != wheel.feeler.hit().object) {
                // Onto another collision object: off the old, onto the new.
                trigger_feeler(trigger_ptr_, kWheelLeavesEvents, &wheel.feeler);
                if (cut_short_ || ((entity()->object_flags() & kObjectFlagDead) != 0U)) {
                    return;
                }
                trigger_feeler(trigger_ptr_, kWheelLandsEvents, &feeler);
                if (cut_short_ || ((entity()->object_flags() & kObjectFlagDead) != 0U)) {
                    return;
                }
            }
            // Touching: the suspension's extension where the ground is, the feeler kept, its line ending there.
            wheel.feeler.get_assign(feeler);
            wheel.state = DrivableModule::WheelState::kOnGround;
            wheel.normal = feeler.surface_dir();
            wheel.y = static_cast<float>(((static_cast<double>(wheel.rest_y_) - wheel.radius) * feeler.reach()) +
                                         wheel.radius);
            ++wheels_in_contact_;
            feeler.line().end = feeler.contact_pos();
        } else {
            if (wheel.state != DrivableModule::WheelState::kInAir) {
                trigger_feeler(trigger_ptr_, kWheelLeavesEvents, &wheel.feeler);
                if (cut_short_ || ((entity()->object_flags() & kObjectFlagDead) != 0U)) {
                    return;
                }
            }
            wheel.state = DrivableModule::WheelState::kInAir;
            wheel.y = wheel.rest_y_;
        }
        // Outside the park editor, triggers along the suspension (faces flagged 0x50).
        if (park_editing::ParkBuilderMode::sole()->editor_state() != kParkEditorOff) {
            feeler.set_excluded({.ignore = render::kNoFaceFlags, .require = render::kTriggerVolumeFaceFlags});
            if (feeler.get_hit_shape(false, false) != 0) {
                trigger_feeler(trigger_ptr_, kCrossingEvents, &feeler);
                if (cut_short_ || ((entity()->object_flags() & kObjectFlagDead) != 0U)) {
                    return;
                }
            }
        }
    }
    // The contact points (relative to the body's position, turned) and the body's velocity there.
    const auto& rows = basis_frame_.row;
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        maths::Vec4& wheel_position = wheel.pos;
        wheel_position = wheel.offset;
        wheel_position.y = static_cast<float>((static_cast<double>(wheel.y) - wheel.radius) + wheel_position.y);
        const double x = ((static_cast<double>(wheel_position.x) * core::at(rows, 0).x) +
                          (static_cast<double>(core::at(rows, 2).x) * wheel_position.z)) +
                         (static_cast<double>(core::at(rows, 1).x) * wheel_position.y);
        const double y = ((static_cast<double>(wheel_position.x) * core::at(rows, 0).y) +
                          (static_cast<double>(wheel_position.y) * core::at(rows, 1).y)) +
                         (static_cast<double>(core::at(rows, 2).y) * wheel_position.z);
        auto z = static_cast<float>(((static_cast<double>(core::at(rows, 2).z) * wheel_position.z) +
                                     (static_cast<double>(core::at(rows, 1).z) * wheel_position.y)) +
                                    (static_cast<double>(core::at(rows, 0).z) * wheel_position.x));
        wheel_position.z = z;
        wheel_position.x = static_cast<float>(x);
        wheel_position.y = static_cast<float>(y);
        maths::Vec4 vel;
        wheel.vel = *get_point_velocity(&vel, &wheel_position);
    }
}

// 0x00437550
void DrivableModule::check_sleep()
{
    const int still = still_frames_;
    still_frames_ = 0;
    if (controls_.throttle) {
        return;
    }
    if (controls_.reverse && wheels_in_contact_ != 0) {
        return;
    }
    if (static_cast<uint32_t>(wheels_in_contact_ + contact_count_) < 3) {
        return;
    }
    if (wheels_in_contact_ != 0 && wheels_in_contact_ != chassis_.wheel_count) {
        return;
    }
    const double vx = vel_.x;
    const double vy = vel_.y;
    const double vz = vel_.z;
    if (((vx * vx) + (vy * vy)) + (vz * vz) > kSleepSpeed) {
        return;
    }
    const double sx = rotvel_.x;
    const double sy = rotvel_.y;
    const double sz = rotvel_.z;
    if (((sx * sx) + (sy * sy)) + (sz * sz) > kSleepSpin) {
        return;
    }
    if (is_kicked()) {
        return;
    }
    still_frames_ = still + 1;
    if (still_frames_ < 3) {
        return;
    }
    linear_momentum_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    rotmom_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    force_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    twist_force_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    for (int i = chassis_.wheel_count - 1; i >= 0; --i) {
        core::at(wheels_, i).torque = 0.0F;
    }
    awake_ = 0;
}

// 0x004376B0
void DrivableModule::read_controls()
{
    const PadSnapshot& input = input_ptr_->pad_primary();
    // The stick squared (its sign kept); else the d-pad's left / right.
    const float stick = core::at(input.scaled_stick(), 2);
    const double steering = std::fabs(static_cast<double>(stick)) * stick;
    controls_.steering = static_cast<float>(steering);
    if (steering == 0.0) {
        const bool left = input.left_side().held_down() != 0;
        const bool right = input.right_side().held_down() != 0;
        if (left && !right) {
            controls_.steering = -1.0F;
        } else if (!left && right) {
            controls_.steering = 1.0F;
        }
    }
    controls_.throttle = input.pos_x().held_down() != 0;
    controls_.handbrake = input.shoulder_right_a().held_down() != 0 && !handbrake_disabled_;
    // The brake button: braking while moving forward, else reversing; with Exitable, the other button too, which
    // also brakes while moving backwards and tells the object it has stopped (0xCBAA3476).
    bool pressed = false;
    bool other = false;
    if (input.button_box().held_down() != 0U) {
        pressed = true;
    } else if (can_dismount_ && (input.button_wedge().held_down() != 0U)) {
        pressed = true;
        other = true;
    } else {
        pressed = false;
    }
    if (pressed) {
        bool brake = false;
        const maths::Vec4& at = core::at(basis_frame_.row, 2);
        if (!controls_.reverse && (!(((static_cast<double>(vel_.z) * at.z) + (static_cast<double>(vel_.y) * at.y)) +
                                         (static_cast<double>(at.x) * vel_.x) <
                                     kReverseSpeed) ||
                                   wheels_in_contact_ == 0)) {
            brake = true;
        } else {
            brake = !(awake_ != 0 || wheels_in_contact_ != 0);
        }
        controls_.brake = brake;
        controls_.reverse = !brake;
        if (other) {
            const double forward = ((static_cast<double>(vel_.z) * at.z) + (static_cast<double>(vel_.y) * at.y)) +
                                   (static_cast<double>(vel_.x) * at.x);
            if (forward < 0.0) {
                controls_.brake = true;
                controls_.reverse = false;
            }
            if (std::fabs(forward) < kStoppedSpeed) {
                entity()->announce(kExitVehicleRequest, nullptr, 0.0F);
            }
        }
    } else {
        controls_.reverse = false;
        controls_.brake = false;
    }
    if (handbrake_active_) {
        controls_.brake = true;
        controls_.reverse = false;
    }
    if (awake_ == 0 && (controls_.throttle || controls_.reverse)) {
        awake_ = 1;
        still_frames_ = 0;
    }
}

// 0x004378B0
void DrivableModule::update_skater()
{
    const GameEntity* object = entity();
    GameEntity* skater = rider_ref_;
    maths::Vec4 place;
    object->basis_frame().rotate_vector(place, seat_.pos);
    skater->at_point().z = static_cast<float>(static_cast<double>(object->at_point().z) + place.z);
    skater->at_point().w = static_cast<float>(static_cast<double>(object->at_point().w) + place.w);
    skater->at_point().x = static_cast<float>(static_cast<double>(place.x) + object->at_point().x);
    skater->at_point().y = static_cast<float>(static_cast<double>(object->at_point().y) + place.y);
    skater->basis_frame() = object->basis_frame();
    // The skater's core physics keeps its own copy of its object's matrix (+0x18).
    auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater_core_physics_);
    core->display_matrix() = core->entity()->basis_frame();
    skater->vel() = object->vel();
    if (!seat_.shown) {
        return;
    }
    // The steering animation's frame follows the smoothed steering (mirrored unless the skater's flag 0x28 is set).
    objects::RiderPoseState* flags = core->state();
    const float steering = (flags->flag(PoseFlag::kFlipped).on != 0U) ? display_steering_ : -display_steering_;
    auto* blender = core::down_cast<AnimBlenderComponent>(anim_blender_ptr_);
    const double duration = blender->fetch_value(seat_.anim);
    auto frame = static_cast<float>((static_cast<double>(steering) - -1.0) * kHalf * duration);
    auto frame_bits = std::bit_cast<uint32_t>(frame);
    auto blend = std::bit_cast<uint32_t>(kBlendFullSpeed);
    if (!online::net_game_state().online_hub()->in_net_game()) {
        blender->play_animation_ex(seat_.anim, 0, frame_bits, blend, 1, 0, 0);
        return;
    }
    // In a network game every fifteenth update is sent on (b = 1).
    int& counter = vehicle_net_update_counter();
    if (++counter == kPeriodTicks) {
        counter = 0;
        blender->play_animation_ex(seat_.anim, 1, frame_bits, blend, 1, 0, 0);
        return;
    }
    blender->play_animation_ex(seat_.anim, 0, frame_bits, blend, 1, 0, 0);
}

} // namespace objects
