// The Vehicle component's physics: the body's dependents, the wheels' suspension, grip, drive, brakes and steering,
// the controls, sleeping, and the skater carried along. See vehicle_component.h. From the disassembly of
// 0x00435B60-0x00436CE2, 0x00437550-0x00437A54, 0x00438A20-0x00438BA1 and 0x0043AE10-0x0043B661. Float arithmetic
// follows the original's x87 code: double (53 bits) where it stays on the stack, rounded to float where stored.

#include "engine/objects/vehicle_component.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "engine/gfx/debug_lines.h"
#include "engine/maths/shape_tests.h"
#include "engine/net/gamenet_manager.h"
#include "engine/render/coll_movable.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/vehicle_internal.h"
#include "platform/game_clock.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

using namespace vehicle;

namespace {
int g_vehicle_net_update_counter;

const float kHalf = 0.5f;                                         // 0x006453BC
const float kTenth = std::bit_cast<float>(0x3DCCCCCDu);           // 0x00645D74
const float kRightingGravity = std::bit_cast<float>(0x42E7D70Bu); // 0x00646718: 115.92
const float kNoSteering = std::bit_cast<float>(0x3A83126Fu);      // 0x006461D8: 0.001
const float kSteeringRate = 10.0f;                                // 0x00646710
const float kUprightBoost = std::bit_cast<float>(0x3F8E38E4u);    // 0x00646728: 1.1111112 (1 / 0.9)
const float kReverseSpeed = 300.0f;                               // 0x0064675C
const float kStoppedSpeed = 30.0f;                                // 0x00646240
const float kMinSlip = 1e-6f;                                     // (0x00646720 is the double 1e-6)
const float kSleepSpeed = 1.0f;                                   // 0x006453A8
const float kSleepSpin = std::bit_cast<float>(0x38D1B717u);       // 0x006453D4: 0.0001
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
    wheel.spin = static_cast<float>(torque * timestep + wheel.spin);
    wheel.torque = static_cast<float>(torque + wheel.torque);
}

// The steering angles for a steering input `steering` (-1..1): the wheel on the inside of the turn (by Ackermann)
// as a float, the outer one unrounded.
void steering_angles(const DrivableModule* vehicle, float steering, float* inner, double* outer)
{
    if (!(std::fabs(steering) > kNoSteering)) {
        *inner = 0.0f;
        *outer = 0.0;
        return;
    }
    const double radius = vehicle->wheelbase_ / std::tan(static_cast<double>(vehicle->max_steering_angle_) * steering);
    const double half_track = static_cast<double>(vehicle->track_) * kHalf;
    const double wheelbase = vehicle->wheelbase_;
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
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.state == kWheelInAir)
            continue;
        // The velocity at the wheel along the ground.
        maths::Vec4 vel = wheel.vel;
        vel.flatten_onto_plane(wheel.normal);
        // The tyre's sideways direction: the body's right turned by the steering, along the ground, unit.
        const double c = std::cos(static_cast<double>(wheel.steering));
        const float s = std::sin(wheel.steering);
        const maths::Vec4* rows = basis_frame_.row.data();
        maths::Vec4 sideways;
        sideways.x = static_cast<float>((c * rows[2].x + static_cast<double>(s) * rows[0].x) + rows[1].x * 0.0);
        sideways.y = static_cast<float>((static_cast<double>(s) * rows[0].y + c * rows[2].y) + rows[1].y * 0.0);
        sideways.z = static_cast<float>((static_cast<double>(s) * rows[0].z + c * rows[2].z) + rows[1].z * 0.0);
        sideways.w = 1.0f;
        sideways.flatten_onto_plane(wheel.normal);
        const double length =
            std::sqrt((static_cast<double>(sideways.z) * sideways.z + static_cast<double>(sideways.y) * sideways.y) +
                      static_cast<double>(sideways.x) * sideways.x);
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
        vel.x = static_cast<float>(vel.x - sideways.x * rolling);
        vel.y = static_cast<float>(static_cast<double>(vel.y) - roll_y);
        vel.z = static_cast<float>(static_cast<double>(vel.z) - roll_z);
        vel.w = static_cast<float>(static_cast<double>(vel.w) - roll_w);
        auto slip =
            static_cast<float>(std::sqrt((static_cast<double>(vel.y) * vel.y + static_cast<double>(vel.z) * vel.z) +
                                         static_cast<double>(vel.x) * vel.x));
        wheel.friction = static_cast<float>(get_tyre_friction(&wheel, slip));
    }
}

// 0x00435DA0
void DrivableModule::refresh_hit_cache()
{
    maths::BBox box;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        box.append_spot(core::at(wheels_, i).feeler_start_);
        box.append_spot(core::at(wheels_, i).feeler_end_);
    }
    for (int i = 1; i >= 0; --i) {
        const Collider& c = core::at(colliders_, i);
        const maths::Vec4& o = c.world_corner_;
        const maths::Vec4& a = core::at(c.world_edges_, 0);
        const maths::Vec4& b = core::at(c.world_edges_, 1);
        box.append_spot(o);
        maths::Vec4 p;
        p = {static_cast<float>(static_cast<double>(o.x) + a.x), static_cast<float>(static_cast<double>(a.y) + o.y),
             static_cast<float>(static_cast<double>(o.z) + a.z), static_cast<float>(static_cast<double>(o.w) + a.w)};
        box.append_spot(p);
        p = {static_cast<float>(static_cast<double>(o.x) + b.x), static_cast<float>(static_cast<double>(b.y) + o.y),
             static_cast<float>(static_cast<double>(o.z) + b.z), static_cast<float>(static_cast<double>(o.w) + b.w)};
        box.append_spot(p);
        // The far corner: x and y summed unrounded, z and w rounded halfway.
        auto z_half = static_cast<float>(static_cast<double>(o.z) + a.z);
        auto w_half = static_cast<float>(static_cast<double>(o.w) + a.w);
        p = {static_cast<float>((static_cast<double>(o.x) + a.x) + b.x),
             static_cast<float>((static_cast<double>(a.y) + o.y) + b.y),
             static_cast<float>(static_cast<double>(z_half) + b.z),
             static_cast<float>(static_cast<double>(w_half) + b.w)};
        box.append_spot(p);
    }
    contact_memo_.tick_frame(box);
}

// 0x00435F00
void DrivableModule::update_steering()
{
    const double previous = display_steering_;
    auto display = static_cast<float>(
        (static_cast<double>(steering_) - previous) * (static_cast<double>(step_length_) * kSteeringRate) + previous);
    display_steering_ = display;
    float inner = 0.0f, display_inner = 0.0f;
    double outer = 0.0, display_outer = 0.0;
    steering_angles(this, steering_, &inner, &outer);
    steering_angles(this, display, &display_inner, &display_outer);
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.steering_side_ == 0)
            continue;
        wheel.steering = wheel.steering_side_ == 1 ? inner : static_cast<float>(outer);
        wheel.display_steering_ = wheel.steering_side_ == 1 ? display_inner : static_cast<float>(display_outer);
    }
}

// 0x004360C0
void DrivableModule::apply_damping()
{
    if (handbrake_ && static_cast<uint32_t>(wheels_in_contact_) > 2)
        return;
    if (!throttle_ && !(steering_ == 0.0f) && static_cast<uint32_t>(wheels_in_contact_) > 2)
        return;
    if (kick_time_ > 0.0f)
        return;
    // The spin damped by a part growing with its square; all of it when that's more than there is.
    const double x = rotmom_.x;
    const double y = rotmom_.y;
    const double z = rotmom_.z;
    const double damping = ((x * x + y * y) + z * z) * spin_damping_ * step_length_;
    auto damping_rounded = static_cast<float>(damping);
    const double length = std::sqrt((x * x + y * y) + z * z);
    if (damping > length) {
        rotmom_ = {0.0f, 0.0f, 0.0f, 1.0f};
    } else {
        maths::Vec4 axis = rotmom_;
        axis.make_unit();
        auto dz = static_cast<float>(static_cast<double>(axis.z) * damping_rounded);
        auto dw = static_cast<float>(static_cast<double>(axis.w) * damping_rounded);
        rotmom_.x = static_cast<float>(rotmom_.x - static_cast<double>(axis.x) * damping_rounded);
        rotmom_.y = static_cast<float>(rotmom_.y - static_cast<double>(axis.y) * damping_rounded);
        rotmom_.z = static_cast<float>(static_cast<double>(rotmom_.z) - dz);
        rotmom_.w = static_cast<float>(static_cast<double>(rotmom_.w) - dw);
    }
    // Upright on three or more wheels and not steering: the turning about up damped towards 0.
    if (steering_ == 0.0f && static_cast<uint32_t>(wheels_in_contact_) > 2 && core::at(basis_frame_.row, 1).y > kHalf) {
        double yaw = static_cast<double>(yaw_damping_) * step_length_;
        if (std::fabs(static_cast<double>(rotmom_.y)) < std::fabs(yaw)) {
            rotmom_.y = 0.0f;
            return;
        }
        if (rotmom_.y > 0.0f)
            yaw = -yaw;
        rotmom_.y = static_cast<float>(yaw + rotmom_.y);
    }
}

// 0x00436280
void DrivableModule::apply_gravity()
{
    double gravity = 0.0;
    if (righting_ && static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
                             (static_cast<uint32_t>(g_clock_offset) - static_cast<uint32_t>(righting_start_)) >
                         300)
        gravity = static_cast<double>(gravity_scale_) / inv_mass_ * kRightingGravity;
    else
        gravity = static_cast<double>(gravity_scale_) / inv_mass_ * kGravity;
    force_.y = static_cast<float>(force_.y - gravity);
    if (debug_mode_) {
        const maths::Vec4 down{at_point_.x,
                               static_cast<float>(-static_cast<double>(kGravity) / inv_mass_ * 0.05f + at_point_.y),
                               at_point_.z, static_cast<float>(static_cast<double>(at_point_.w) + 0.05f)};
        gfx::push_diag_line(&at_point_, &down, 0xFF003232, 0xFF003232, 1);
    }
}

// 0x00436340
void DrivableModule::apply_suspension()
{
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.state == kWheelInAir) {
            // (In the air the history gets an even share of the weight; the average isn't updated.)
            core::at(wheel.load_history_, load_index_) =
                static_cast<float>(static_cast<double>(kGravity) / inv_mass_ / as_unsigned(wheel_count_));
            continue;
        }
        // The spring (by how far the suspension is compressed along the ground's normal) and the damper.
        const maths::Vec4& n = wheel.normal;
        const maths::Vec4& up = core::at(basis_frame_.row, 1);
        const double up_along_normal =
            (static_cast<double>(up.z) * n.z + static_cast<double>(up.y) * n.y) + static_cast<double>(up.x) * n.x;
        const double spring = -(up_along_normal * (static_cast<double>(wheel.rest_y_) - wheel.y) * wheel.spring_rate_);
        const double closing = (static_cast<double>(wheel.vel.z) * n.z + static_cast<double>(wheel.vel.y) * n.y) +
                               static_cast<double>(wheel.vel.x) * n.x;
        const double force = spring - closing * wheel.damping_rate_;
        auto force_rounded = static_cast<float>(force);
        const maths::Vec4 push{static_cast<float>(force * n.x),
                               static_cast<float>(static_cast<double>(force_rounded) * n.y),
                               static_cast<float>(static_cast<double>(force_rounded) * n.z),
                               static_cast<float>(static_cast<double>(force_rounded) * n.w)};
        add_force_at_point(&push, &wheel.pos, 0xFF006464);
        core::at(wheel.load_history_, load_index_) = force_rounded;
        // The load: the average of the last ten.
        wheel.load = 0.0f;
        for (int k = 9; k >= 0; --k)
            wheel.load = static_cast<float>(static_cast<double>(core::at(wheel.load_history_, k)) + wheel.load);
        wheel.load = static_cast<float>(static_cast<double>(wheel.load) * kTenth);
    }
    auto next = static_cast<int8_t>(load_index_ + 1);
    load_index_ = static_cast<int8_t>(next % 10);
}

// 0x004364B0
void DrivableModule::apply_tyre_forces()
{
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.state == kWheelInAir)
            continue;
        // The slip velocity: the ground velocity less the tyre's rolling (as update_wheel_friction).
        const maths::Vec4 side = wheel.sideways;
        const double rolling = static_cast<double>(wheel.spin) * wheel.radius;
        auto roll_y = static_cast<float>(side.y * rolling);
        auto roll_z = static_cast<float>(side.z * rolling);
        auto roll_w = static_cast<float>(side.w * rolling);
        maths::Vec4 slip;
        slip.x = static_cast<float>(wheel.contact_vel_.x - side.x * rolling);
        slip.y = static_cast<float>(static_cast<double>(wheel.contact_vel_.y) - roll_y);
        slip.z = static_cast<float>(static_cast<double>(wheel.contact_vel_.z) - roll_z);
        slip.w = static_cast<float>(static_cast<double>(wheel.contact_vel_.w) - roll_w);
        const double speed = std::sqrt((static_cast<double>(slip.z) * slip.z + static_cast<double>(slip.y) * slip.y) +
                                       static_cast<double>(slip.x) * slip.x);
        auto speed_rounded = static_cast<float>(speed);
        wheel.slip = speed_rounded;
        if (speed < 1e-6)
            continue;
        // The friction against the slip's direction.
        double dir_x = slip.x;
        float dir_y = slip.y, dir_z = slip.z;
        if (speed_rounded > 0.0f) {
            const double inverse = 1.0 / speed_rounded;
            auto inverse_rounded = static_cast<float>(inverse);
            dir_x = inverse * slip.x;
            dir_y = static_cast<float>(static_cast<double>(slip.y) * inverse_rounded);
            dir_z = static_cast<float>(static_cast<double>(slip.z) * inverse_rounded);
        }
        auto scale = static_cast<float>(-(static_cast<double>(wheel.friction) * wheel.load));
        const double friction_x = dir_x * scale;
        const maths::Vec4 friction{0.0f, static_cast<float>(static_cast<double>(dir_y) * scale),
                                   static_cast<float>(static_cast<double>(dir_z) * scale),
                                   static_cast<float>(static_cast<double>(slip.w) * scale)};
        // The torque that would stop the slip along the tyre this step, and the friction's own torque on it: the
        // smaller one acts on the wheel; when it's the stopping one, the rest of it pushes the body.
        auto stop = static_cast<float>(((static_cast<double>(slip.z) * side.z + static_cast<double>(slip.y) * side.y) +
                                        static_cast<double>(slip.x) * side.x) /
                                       wheel.radius / (static_cast<double>(step_length_) * wheel.inv_moment_));
        const double along =
            (static_cast<double>(friction.z) * side.z + static_cast<double>(friction.y) * side.y) + friction_x * side.x;
        const double friction_torque = -(along * wheel.radius);
        float wheel_torque = 0.0f;
        maths::Vec4 push;
        if (std::fabs(static_cast<double>(stop)) > std::fabs(friction_torque)) {
            push = {static_cast<float>(friction_x), friction.y, friction.z, friction.w};
            wheel_torque = static_cast<float>(friction_torque);
        } else {
            const double rest = -1.0 / wheel.radius * stop - along;
            const maths::Vec4 extra{static_cast<float>(side.x * rest), static_cast<float>(side.y * rest),
                                    static_cast<float>(side.z * rest), static_cast<float>(side.w * rest)};
            push = {static_cast<float>(friction_x + extra.x),
                    static_cast<float>(static_cast<double>(extra.y) + friction.y),
                    static_cast<float>(static_cast<double>(extra.z) + friction.z),
                    static_cast<float>(static_cast<double>(extra.w) + friction.w)};
            wheel_torque = stop;
        }
        add_force_at_point(&push, &wheel.pos, 0xFFFF0000);
        spin_wheel(wheel, static_cast<double>(wheel_torque) * wheel.inv_moment_, step_length_);
    }
}

// 0x004367E0
void DrivableModule::apply_engine_braking()
{
    if (throttle_ || reverse_ || brake_)
        return;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (!wheel.drive || wheel.state == kWheelInAir)
            continue;
        const int gear = get_gear(static_cast<float>(std::fabs(static_cast<double>(wheel.spin))));
        const double ratio = core::at(transmission_ratios_, gear);
        double braking = ratio * engine_braking_ * ratio / as_unsigned(drive_wheel_count_);
        const float stop = brake_turn_force(static_cast<double>(wheel.inv_moment_) * step_length_, wheel.spin);
        if (braking > stop)
            braking = stop;
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00436900
void DrivableModule::apply_drive_torque()
{
    // Less drive on a slope (up.y squared / 0.9, at most 1).
    const double upright = static_cast<double>(core::at(basis_frame_.row, 1).y) * core::at(basis_frame_.row, 1).y * kUprightBoost;
    const float grip = upright > 1.0 ? 1.0f : static_cast<float>(upright);
    if (!(throttle_ || reverse_) || brake_)
        return;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (!wheel.drive || (handbrake_ && wheel.steering_side_ != 0))
            continue;
        float spin = 0.0f;
        if (!reverse_)
            spin = wheel.spin > 0.0f ? wheel.spin : 0.0f;
        else
            spin = wheel.spin < 0.0f ? -wheel.spin : 0.0f;
        const int gear = get_gear(spin);
        double torque = static_cast<double>(core::at(transmission_ratios_, gear)) * final_drive_ratio_ * engine_torque_ /
                        as_unsigned(drive_wheel_count_) * grip;
        if (reverse_)
            torque = -(torque * reverse_ratio_);
        if (handbrake_ && drive_wheel_count_ == 4)
            torque = torque + torque;
        spin_wheel(wheel, torque * wheel.inv_moment_, step_length_);
    }
}

// 0x00436A90
void DrivableModule::apply_brakes()
{
    if (!brake_)
        return;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        double braking = wheel.brake_torque_;
        const float stop = brake_turn_force(static_cast<double>(step_length_) * wheel.inv_moment_, wheel.spin);
        if (braking > stop) {
            wheel.brake_excess_ = static_cast<float>(braking - stop);
            braking = stop;
        } else {
            wheel.brake_excess_ = 0.0f;
        }
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00436B60
void DrivableModule::apply_handbrake(float scale)
{
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        double braking = static_cast<double>(scale) * wheel.handbrake_torque_;
        const float stop = brake_turn_force(static_cast<double>(step_length_) * wheel.inv_moment_, wheel.spin);
        if (braking > stop) {
            wheel.brake_excess_ = static_cast<float>(braking - stop);
            braking = stop;
        } else {
            wheel.brake_excess_ = 0.0f;
        }
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00436C20
void DrivableModule::apply_brake_excess()
{
    if (!brake_)
        return;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        if (wheel.brake_excess_ == 0.0f)
            continue;
        double braking = wheel.brake_excess_;
        const float stop = brake_turn_force(static_cast<double>(step_length_) * wheel.inv_moment_, wheel.spin);
        if (braking > stop)
            braking = stop;
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        spin_wheel(wheel, braking * wheel.inv_moment_, step_length_);
    }
}

// 0x00438A20
void DrivableModule::update_inertia()
{
    // R * diag(inverse moments) * R^T.
    for (int r = 0; r < 3; ++r) {
        (&core::at(inv_inertia_.row, r).x)[0] =
            static_cast<float>(static_cast<double>((&core::at(basis_frame_.row, r).x)[0]) * core::at(inv_moments_, 0));
        (&core::at(inv_inertia_.row, r).x)[1] =
            static_cast<float>(static_cast<double>((&core::at(basis_frame_.row, r).x)[1]) * core::at(inv_moments_, 1));
        (&core::at(inv_inertia_.row, r).x)[2] =
            static_cast<float>(static_cast<double>((&core::at(basis_frame_.row, r).x)[2]) * core::at(inv_moments_, 2));
    }
    render::MatrixTranspose transposed;
    transposed.transpose_from(&basis_frame_);
    matrix_multiply(&inv_inertia_, &inv_inertia_, &transposed);
}

// 0x00438AE0
void DrivableModule::apply_wheel_forces()
{
    apply_gravity();
    apply_suspension();
    update_wheel_friction();
    if (!handbrake_) {
        apply_drive_torque();
        apply_engine_braking();
        apply_brakes();
        apply_tyre_forces();
        apply_brake_excess();
        return;
    }
    if (throttle_) {
        apply_drive_torque();
        apply_handbrake(static_cast<float>(1.0 - std::fabs(static_cast<double>(steering_))));
        apply_tyre_forces();
        apply_brake_excess();
        return;
    }
    // The handbrake without throttle locks the wheels.
    apply_tyre_forces();
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        core::at(wheels_, i).spin = 0.0f;
        core::at(wheels_, i).torque = 0.0f;
    }
}

// 0x0043AE10
void DrivableModule::update_dependents()
{
    quat_to_matrix(&orientation_, &basis_frame_);
    core::at(basis_frame_.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Vec4 turned;
    com_offset_ = basis_frame_.rotate_vector(turned, balance_point_);
    update_inertia();
    step_vehicle_speeds();
    const maths::Vec4* r = basis_frame_.row.data();
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        // The suspension's top: the wheel's place turned and moved (x summed unrounded).
        const maths::Vec4& o = wheel.offset;
        double x =
            (static_cast<double>(r[1].x) * o.y + static_cast<double>(r[2].x) * o.z) + static_cast<double>(r[0].x) * o.x;
        auto y = static_cast<float>((static_cast<double>(r[1].y) * o.y + static_cast<double>(r[0].y) * o.x) +
                                    static_cast<double>(r[2].y) * o.z);
        auto z = static_cast<float>((static_cast<double>(r[1].z) * o.y + static_cast<double>(r[0].z) * o.x) +
                                    static_cast<double>(r[2].z) * o.z);
        float w = o.w;
        wheel.feeler_start_.z = static_cast<float>(static_cast<double>(z) + at_point_.z);
        wheel.feeler_start_.w = static_cast<float>(static_cast<double>(w) + at_point_.w);
        wheel.feeler_start_.x = static_cast<float>(x + at_point_.x);
        wheel.feeler_start_.y = static_cast<float>(static_cast<double>(y) + at_point_.y);
        // The bottom: the place lowered to its rest less the radius.
        maths::Vec4 e = o;
        e.y = static_cast<float>((static_cast<double>(wheel.rest_y_) - wheel.radius) + e.y);
        wheel.feeler_end_ = e;
        x = (static_cast<double>(e.x) * r[0].x + static_cast<double>(r[2].x) * e.z) + static_cast<double>(r[1].x) * e.y;
        auto yy = static_cast<float>((static_cast<double>(e.x) * r[0].y + static_cast<double>(e.y) * r[1].y) +
                                     static_cast<double>(r[2].y) * e.z);
        z = static_cast<float>((static_cast<double>(r[2].z) * e.z + static_cast<double>(r[1].z) * e.y) +
                               static_cast<double>(r[0].z) * e.x);
        w = e.w;
        wheel.feeler_end_.z = static_cast<float>(static_cast<double>(z) + at_point_.z);
        wheel.feeler_end_.w = static_cast<float>(static_cast<double>(w) + at_point_.w);
        wheel.feeler_end_.x = static_cast<float>(x + at_point_.x);
        wheel.feeler_end_.y = static_cast<float>(static_cast<double>(yy) + at_point_.y);
    }
    for (int i = 1; i >= 0; --i) {
        Collider& c = core::at(colliders_, i);
        // The corner turned and moved; the edges turned (each with its own order of sums, as the original).
        const maths::Vec4& o = c.corner;
        const double x =
            (static_cast<double>(r[2].x) * o.z + static_cast<double>(r[1].x) * o.y) + static_cast<double>(o.x) * r[0].x;
        auto y = static_cast<float>((static_cast<double>(r[0].y) * o.x + static_cast<double>(r[2].y) * o.z) +
                                    static_cast<double>(r[1].y) * o.y);
        auto z = static_cast<float>((static_cast<double>(r[0].z) * o.x + static_cast<double>(r[2].z) * o.z) +
                                    static_cast<double>(r[1].z) * o.y);
        const float w = o.w;
        c.world_corner_.z = static_cast<float>(static_cast<double>(z) + at_point_.z);
        c.world_corner_.w = static_cast<float>(static_cast<double>(w) + at_point_.w);
        c.world_corner_.x = static_cast<float>(x + at_point_.x);
        c.world_corner_.y = static_cast<float>(static_cast<double>(y) + at_point_.y);
        const maths::Vec4& a = core::at(c.edges, 0);
        core::at(c.world_edges_, 0).z =
            static_cast<float>((static_cast<double>(r[2].z) * a.z + static_cast<double>(r[0].z) * a.x) +
                               static_cast<double>(r[1].z) * a.y);
        core::at(c.world_edges_, 0).w = a.w;
        core::at(c.world_edges_, 0).x =
            static_cast<float>((static_cast<double>(r[2].x) * a.z + static_cast<double>(r[1].x) * a.y) +
                               static_cast<double>(a.x) * r[0].x);
        core::at(c.world_edges_, 0).y =
            static_cast<float>((static_cast<double>(r[2].y) * a.z + static_cast<double>(r[0].y) * a.x) +
                               static_cast<double>(r[1].y) * a.y);
        const maths::Vec4& b = core::at(c.edges, 1);
        const float bw = b.w;
        core::at(c.world_edges_, 1).z =
            static_cast<float>((static_cast<double>(r[2].z) * b.z + static_cast<double>(r[1].z) * b.y) +
                               static_cast<double>(b.x) * r[0].z);
        core::at(c.world_edges_, 1).x =
            static_cast<float>((static_cast<double>(r[0].x) * b.x + static_cast<double>(r[2].x) * b.z) +
                               static_cast<double>(b.y) * r[1].x);
        core::at(c.world_edges_, 1).w = bw;
        core::at(c.world_edges_, 1).y =
            static_cast<float>((static_cast<double>(r[1].y) * b.y + static_cast<double>(r[2].y) * b.z) +
                               static_cast<double>(r[0].y) * b.x);
        for (int k = 0; k < 2; ++k) {
            const double inverse = 1.0 / core::at(c.lengths, k);
            const maths::Vec4& edge = core::at(c.world_edges_, k);
            core::at(c.directions, k) = {static_cast<float>(inverse * edge.x), static_cast<float>(inverse * edge.y),
                               static_cast<float>(inverse * edge.z), static_cast<float>(inverse * edge.w)};
        }
    }
    refresh_hit_cache();
}

// 0x0043B240
void DrivableModule::update_wheel_contacts()
{
    render::ProbeRay feeler;
    feeler.hit_cache_ = &contact_memo_;
    wheels_in_contact_ = 0;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        feeler.line_.start = wheel.feeler_start_;
        feeler.line_.end = wheel.feeler_end_;
        feeler.set_excluded(0x10, 0);
        if (feeler.get_hit_shape(false, false)) {
            if (wheel.state == kWheelInAir) {
                trigger_feeler(trigger_ptr_, kWheelLandsEvents.data(), &feeler);
                if (cut_short_ || (entity_->object_flags_ & kObjectFlagDead))
                    return; // (the feeler's destructor lets go of its object)
            } else if (feeler.hit_.object != wheel.feeler.hit_.object) {
                // Onto another collision object: off the old, onto the new.
                trigger_feeler(trigger_ptr_, kWheelLeavesEvents.data(), &wheel.feeler);
                if (cut_short_ || (entity_->object_flags_ & kObjectFlagDead))
                    return;
                trigger_feeler(trigger_ptr_, kWheelLandsEvents.data(), &feeler);
                if (cut_short_ || (entity_->object_flags_ & kObjectFlagDead))
                    return;
            }
            // Touching: the suspension's extension where the ground is, the feeler kept, its line ending there.
            wheel.feeler.get_assign(feeler);
            wheel.state = kWheelOnGround;
            wheel.normal = feeler.surface_dir_;
            wheel.y =
                static_cast<float>((static_cast<double>(wheel.rest_y_) - wheel.radius) * feeler.reach_ + wheel.radius);
            ++wheels_in_contact_;
            feeler.line_.end = feeler.contact_pos_;
        } else {
            if (wheel.state != kWheelInAir) {
                trigger_feeler(trigger_ptr_, kWheelLeavesEvents.data(), &wheel.feeler);
                if (cut_short_ || (entity_->object_flags_ & kObjectFlagDead))
                    return;
            }
            wheel.state = kWheelInAir;
            wheel.y = wheel.rest_y_;
        }
        // Outside the park editor, triggers along the suspension (faces flagged 0x50).
        if (park_editing::g_park_editor->cur_phase_ != kParkEditorOff) {
            feeler.set_excluded(0, 0x50);
            if (feeler.get_hit_shape(false, false)) {
                trigger_feeler(trigger_ptr_, kCrossingEvents.data(), &feeler);
                if (cut_short_ || (entity_->object_flags_ & kObjectFlagDead))
                    return;
            }
        }
    }
    // The contact points (relative to the body's position, turned) and the body's velocity there.
    const maths::Vec4* r = basis_frame_.row.data();
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        maths::Vec4& p = wheel.pos;
        p = wheel.offset;
        p.y = static_cast<float>((static_cast<double>(wheel.y) - wheel.radius) + p.y);
        const double x =
            (static_cast<double>(p.x) * r[0].x + static_cast<double>(r[2].x) * p.z) + static_cast<double>(r[1].x) * p.y;
        const double y =
            (static_cast<double>(p.x) * r[0].y + static_cast<double>(p.y) * r[1].y) + static_cast<double>(r[2].y) * p.z;
        auto z = static_cast<float>((static_cast<double>(r[2].z) * p.z + static_cast<double>(r[1].z) * p.y) +
                                    static_cast<double>(r[0].z) * p.x);
        p.z = z;
        p.x = static_cast<float>(x);
        p.y = static_cast<float>(y);
        maths::Vec4 vel;
        wheel.vel = *get_point_velocity(&vel, &p);
    }
}

// 0x00437550
void DrivableModule::check_sleep()
{
    const int still = still_frames_;
    still_frames_ = 0;
    if (throttle_)
        return;
    if (reverse_ && wheels_in_contact_ != 0)
        return;
    if (static_cast<uint32_t>(wheels_in_contact_ + contact_count_) < 3)
        return;
    if (wheels_in_contact_ != 0 && wheels_in_contact_ != wheel_count_)
        return;
    const double vx = vel_.x;
    const double vy = vel_.y;
    const double vz = vel_.z;
    if ((vx * vx + vy * vy) + vz * vz > kSleepSpeed)
        return;
    const double sx = rotvel_.x;
    const double sy = rotvel_.y;
    const double sz = rotvel_.z;
    if ((sx * sx + sy * sy) + sz * sz > kSleepSpin)
        return;
    if (is_kicked())
        return;
    still_frames_ = still + 1;
    if (still_frames_ < 3)
        return;
    linear_momentum_ = {0.0f, 0.0f, 0.0f, 1.0f};
    rotmom_ = {0.0f, 0.0f, 0.0f, 1.0f};
    force_ = {0.0f, 0.0f, 0.0f, 1.0f};
    twist_force_ = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = wheel_count_ - 1; i >= 0; --i)
        core::at(wheels_, i).torque = 0.0f;
    awake_ = 0;
}

// 0x004376B0
void DrivableModule::read_controls()
{
    const PadSnapshot& input = input_ptr_->pad_primary_;
    // The stick squared (its sign kept); else the d-pad's left / right.
    const float stick = core::at(input.scaled_stick_, 2);
    const double steering = std::fabs(static_cast<double>(stick)) * stick;
    steering_ = static_cast<float>(steering);
    if (steering == 0.0) {
        const bool left = input.left_side_.held_down_ != 0;
        const bool right = input.right_side_.held_down_ != 0;
        if (left && !right)
            steering_ = -1.0f;
        else if (!left && right)
            steering_ = 1.0f;
    }
    throttle_ = input.pos_x_.held_down_ != 0;
    handbrake_ = input.shoulder_right_a_.held_down_ != 0 && !handbrake_disabled_;
    // The brake button: braking while moving forward, else reversing; with Exitable, the other button too, which
    // also brakes while moving backwards and tells the object it has stopped (0xCBAA3476).
    bool pressed = false, other = false;
    if (input.button_box_.held_down_) {
        pressed = true;
    } else if (can_dismount_ && input.button_wedge_.held_down_) {
        pressed = true;
        other = true;
    } else {
        pressed = false;
    }
    if (pressed) {
        bool brake = false;
        const maths::Vec4& at = core::at(basis_frame_.row, 2);
        if (!reverse_ && (!((static_cast<double>(vel_.z) * at.z + static_cast<double>(vel_.y) * at.y) +
                                static_cast<double>(at.x) * vel_.x <
                            kReverseSpeed) ||
                          wheels_in_contact_ == 0))
            brake = true;
        else
            brake = !(awake_ != 0 || wheels_in_contact_ != 0);
        brake_ = brake;
        reverse_ = !brake;
        if (other) {
            const double forward = (static_cast<double>(vel_.z) * at.z + static_cast<double>(vel_.y) * at.y) +
                                   static_cast<double>(vel_.x) * at.x;
            if (forward < 0.0) {
                brake_ = true;
                reverse_ = false;
            }
            if (std::fabs(forward) < kStoppedSpeed)
                entity_->announce(0xCBAA3476, nullptr, 0.0f);
        }
    } else {
        reverse_ = false;
        brake_ = false;
    }
    if (handbrake_active_) {
        brake_ = true;
        reverse_ = false;
    }
    if (awake_ == 0 && (throttle_ || reverse_)) {
        awake_ = 1;
        still_frames_ = 0;
    }
}

// 0x004378B0
void DrivableModule::update_skater()
{
    const GameEntity* object = entity_;
    GameEntity* skater = rider_ref_;
    maths::Vec4 place;
    object->basis_frame_.rotate_vector(place, skater_pos_);
    skater->at_point_.z = static_cast<float>(static_cast<double>(object->at_point_.z) + place.z);
    skater->at_point_.w = static_cast<float>(static_cast<double>(object->at_point_.w) + place.w);
    skater->at_point_.x = static_cast<float>(static_cast<double>(place.x) + object->at_point_.x);
    skater->at_point_.y = static_cast<float>(static_cast<double>(object->at_point_.y) + place.y);
    skater->basis_frame_ = object->basis_frame_;
    // The skater's core physics keeps its own copy of its object's matrix (+0x18).
    auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater_core_physics_);
    core->display_matrix_ = core->entity_->basis_frame_;
    skater->vel_ = object->vel_;
    if (!rider_shown_)
        return;
    // The steering animation's frame follows the smoothed steering (mirrored unless the skater's flag 0x28 is set).
    objects::RiderPoseState* flags = core->state_;
    const float steering = flags->flag(PoseFlag::kFlipped).on ? display_steering_ : -display_steering_;
    auto* blender = core::down_cast<AnimBlenderComponent>(anim_blender_ptr_);
    const double duration = blender->fetch_value(rider_anim_name_);
    auto frame = static_cast<float>((static_cast<double>(steering) - -1.0) * kHalf * duration);
    auto frame_bits = std::bit_cast<uint32_t>(frame);
    auto blend = std::bit_cast<uint32_t>(1000.0f);
    if (!online::g_online_hub->in_net_game()) {
        blender->play_animation_ex(rider_anim_name_, 0, frame_bits, blend, 1, 0, 0);
        return;
    }
    // In a network game every fifteenth update is sent on (b = 1).
    int& counter = g_vehicle_net_update_counter;
    if (++counter == 15) {
        counter = 0;
        blender->play_animation_ex(rider_anim_name_, 1, frame_bits, blend, 1, 0, 0);
        return;
    }
    blender->play_animation_ex(rider_anim_name_, 0, frame_bits, blend, 1, 0, 0);
}

} // namespace objects
