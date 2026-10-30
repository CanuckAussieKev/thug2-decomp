// The Vehicle component's physics: the body's dependents, the wheels' suspension, grip, drive, brakes and steering,
// the controls, sleeping, and the skater carried along. See vehicle_component.h. From the disassembly of
// 0x00435B60-0x00436CE2, 0x00437550-0x00437A54, 0x00438A20-0x00438BA1 and 0x0043AE10-0x0043B661. Float arithmetic
// follows the original's x87 code: double (53 bits) where it stays on the stack, rounded to float where stored.

#include "game/obj/vehicle_component.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "game/gfx/debug_lines.h"
#include "game/mth/geometry.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_movable.h"
#include "game/obj/anim_blender.h"
#include "game/obj/vehicle_internal.h"
#include "game/sys/game_clock.h"
#include "game/ed/park_editor.h"
#include "game/obj/control_pad.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

using namespace Vehicle;

GAME_GLOBAL(int, g_vehicleNetUpdateCounter, 0x006951C4);

namespace {

const float kHalf = 0.5f;                                  // 0x006453BC
const float kTenth = std::bit_cast<float>(0x3DCCCCCDu);     // 0x00645D74
const float kRightingGravity = std::bit_cast<float>(0x42E7D70Bu); // 0x00646718: 115.92
const float kNoSteering = std::bit_cast<float>(0x3A83126Fu); // 0x006461D8: 0.001
const float kSteeringRate = 10.0f;                          // 0x00646710
const float kUprightBoost = std::bit_cast<float>(0x3F8E38E4u); // 0x00646728: 1.1111112 (1 / 0.9)
const float kReverseSpeed = 300.0f;                         // 0x0064675C
const float kStoppedSpeed = 30.0f;                          // 0x00646240
const float kMinSlip = 1e-6f;                               // (0x00646720 is the double 1e-6)
const float kSleepSpeed = 1.0f;                             // 0x006453A8
const float kSleepSpin = std::bit_cast<float>(0x38D1B717u); // 0x006453D4: 0.0001
const double kTwoTo32 = 4294967296.0;                       // 0x006453B4

// The trigger events a wheel's feeler is reported for (const int[3] each, for the trigger component's 0x00434BF0).
const int* const kWheelLeaves = reinterpret_cast<const int*>(0x00646680);    // {1, 4, 0}
const int* const kWheelLands = reinterpret_cast<const int*>(0x0064668C);     // {1, 3, 0}
const int* const kWheelCrossing = reinterpret_cast<const int*>(0x00646698);  // {9, 0, 0}

constexpr uint32_t kParkEditorOff = 0xD443A2BC;

// An unsigned count as the original's FILD (and its 2^32 fix for negatives).
double Unsigned(int value) { return value < 0 ? value + kTwoTo32 : value; }

// x87 instructions the original uses, so the results round exactly as there.
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
double X87Tan(double angle)
{
    double result;
    __asm {
        fld angle
        fptan
        fstp st(0)
        fstp result
    }
    return result;
}
// FPATAN: the angle of (x, y) - atan2(y, x).
double X87Atan2(double y, double x)
{
    double result;
    __asm {
        fld y
        fld x
        fpatan
        fstp result
    }
    return result;
}

// The cap on a brake-like torque: what stops the wheel in this step, |(-1 / (dt * invMoment)) * spin| as a float.
float StoppingTorque(double perStep, float spin) { return static_cast<float>(std::fabs(-1.0 / perStep * spin)); }

// A torque on the wheel (already times its inverse moment): its spin changed over the step, and summed.
void SpinWheel(CVehicleComponent::Wheel& wheel, double torque, float timestep)
{
    wheel.spin = static_cast<float>(torque * timestep + wheel.spin);
    wheel.torque = static_cast<float>(torque + wheel.torque);
}

// The steering angles for a steering input `steering` (-1..1): the wheel on the inside of the turn (by Ackermann)
// as a float, the outer one unrounded.
void SteeringAngles(const CVehicleComponent* vehicle, float steering, float* inner, double* outer)
{
    if (!(std::fabs(steering) > kNoSteering)) {
        *inner = 0.0f;
        *outer = 0.0;
        return;
    }
    double radius = vehicle->m_wheelbase / X87Tan(static_cast<double>(vehicle->m_maxSteeringAngle) * steering);
    double halfTrack = static_cast<double>(vehicle->m_track) * kHalf;
    double wheelbase = vehicle->m_wheelbase;
    if (radius > 0.0) {
        *inner = static_cast<float>(-X87Atan2(wheelbase, radius - halfTrack));
        *outer = -X87Atan2(wheelbase, halfTrack + radius);
    } else {
        *inner = static_cast<float>(X87Atan2(wheelbase, halfTrack - radius));
        *outer = X87Atan2(wheelbase, -radius - halfTrack);
    }
}

} // namespace

// 0x00435B60
void CVehicleComponent::UpdateWheelFriction()
{
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        if (wheel.state == kWheelInAir)
            continue;
        // The velocity at the wheel along the ground.
        Mth::Vector vel = wheel.vel;
        vel.ProjectToPlane(wheel.normal);
        // The tyre's sideways direction: the body's right turned by the steering, along the ground, unit.
        double c = X87Cos(wheel.steering);
        float s = static_cast<float>(X87Sin(wheel.steering));
        const Mth::Vector* rows = m_matrix.row;
        Mth::Vector sideways;
        sideways.x = static_cast<float>((c * rows[2].x + static_cast<double>(s) * rows[0].x) + rows[1].x * 0.0);
        sideways.y = static_cast<float>((static_cast<double>(s) * rows[0].y + c * rows[2].y) + rows[1].y * 0.0);
        sideways.z = static_cast<float>((static_cast<double>(s) * rows[0].z + c * rows[2].z) + rows[1].z * 0.0);
        sideways.w = 1.0f;
        sideways.ProjectToPlane(wheel.normal);
        double length = std::sqrt((static_cast<double>(sideways.z) * sideways.z + static_cast<double>(sideways.y) * sideways.y) +
                                  static_cast<double>(sideways.x) * sideways.x);
        if (length > 0.0) {
            double scale = 1.0 / length;
            sideways.x = static_cast<float>(sideways.x * scale);
            sideways.y = static_cast<float>(sideways.y * scale);
            sideways.z = static_cast<float>(sideways.z * scale);
        }
        wheel.sideways = sideways;
        wheel.contactVel = vel;
        // The slip: the ground velocity less the tyre's rolling along its sideways direction.
        double rolling = static_cast<double>(wheel.radius) * wheel.spin;
        float rollY = static_cast<float>(sideways.y * rolling);
        float rollZ = static_cast<float>(sideways.z * rolling);
        float rollW = static_cast<float>(sideways.w * rolling);
        vel.x = static_cast<float>(vel.x - sideways.x * rolling);
        vel.y = static_cast<float>(static_cast<double>(vel.y) - rollY);
        vel.z = static_cast<float>(static_cast<double>(vel.z) - rollZ);
        vel.w = static_cast<float>(static_cast<double>(vel.w) - rollW);
        float slip = static_cast<float>(std::sqrt(
            (static_cast<double>(vel.y) * vel.y + static_cast<double>(vel.z) * vel.z) + static_cast<double>(vel.x) * vel.x));
        wheel.friction = static_cast<float>(GetTyreFriction(&wheel, slip));
    }
}

// 0x00435DA0
void CVehicleComponent::UpdateCollisionCache()
{
    Mth::BBox box;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        box.AddPoint(m_wheels[i].feelerStart);
        box.AddPoint(m_wheels[i].feelerEnd);
    }
    for (int i = 1; i >= 0; --i) {
        const Collider& c = m_colliders[i];
        const Mth::Vector& o = c.worldCorner;
        const Mth::Vector& a = c.worldEdges[0];
        const Mth::Vector& b = c.worldEdges[1];
        box.AddPoint(o);
        Mth::Vector p;
        p = {static_cast<float>(static_cast<double>(o.x) + a.x), static_cast<float>(static_cast<double>(a.y) + o.y),
             static_cast<float>(static_cast<double>(o.z) + a.z), static_cast<float>(static_cast<double>(o.w) + a.w)};
        box.AddPoint(p);
        p = {static_cast<float>(static_cast<double>(o.x) + b.x), static_cast<float>(static_cast<double>(b.y) + o.y),
             static_cast<float>(static_cast<double>(o.z) + b.z), static_cast<float>(static_cast<double>(o.w) + b.w)};
        box.AddPoint(p);
        // The far corner: x and y summed unrounded, z and w rounded halfway.
        float zHalf = static_cast<float>(static_cast<double>(o.z) + a.z);
        float wHalf = static_cast<float>(static_cast<double>(o.w) + a.w);
        p = {static_cast<float>((static_cast<double>(o.x) + a.x) + b.x),
             static_cast<float>((static_cast<double>(a.y) + o.y) + b.y),
             static_cast<float>(static_cast<double>(zHalf) + b.z), static_cast<float>(static_cast<double>(wHalf) + b.w)};
        box.AddPoint(p);
    }
    m_collCache.Update(box);
}

// 0x00435F00
void CVehicleComponent::UpdateSteering()
{
    double previous = m_displaySteering;
    float display = static_cast<float>((static_cast<double>(m_steering) - previous) *
                                           (static_cast<double>(m_timestep) * kSteeringRate) +
                                       previous);
    m_displaySteering = display;
    float inner, displayInner;
    double outer, displayOuter;
    SteeringAngles(this, m_steering, &inner, &outer);
    SteeringAngles(this, display, &displayInner, &displayOuter);
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        if (wheel.steeringSide == 0)
            continue;
        wheel.steering = wheel.steeringSide == 1 ? inner : static_cast<float>(outer);
        wheel.displaySteering = wheel.steeringSide == 1 ? displayInner : static_cast<float>(displayOuter);
    }
}

// 0x004360C0
void CVehicleComponent::ApplyDamping()
{
    if (m_handbrake && static_cast<uint32_t>(m_wheelsInContact) > 2)
        return;
    if (!m_throttle && !(m_steering == 0.0f) && static_cast<uint32_t>(m_wheelsInContact) > 2)
        return;
    if (m_kickTime > 0.0f)
        return;
    // The spin damped by a part growing with its square; all of it when that's more than there is.
    double x = m_rotmom.x, y = m_rotmom.y, z = m_rotmom.z;
    double damping = ((x * x + y * y) + z * z) * m_spinDamping * m_timestep;
    float dampingRounded = static_cast<float>(damping);
    double length = std::sqrt((x * x + y * y) + z * z);
    if (damping > length) {
        m_rotmom = {0.0f, 0.0f, 0.0f, 1.0f};
    } else {
        Mth::Vector axis = m_rotmom;
        axis.Normalize();
        float dz = static_cast<float>(static_cast<double>(axis.z) * dampingRounded);
        float dw = static_cast<float>(static_cast<double>(axis.w) * dampingRounded);
        m_rotmom.x = static_cast<float>(m_rotmom.x - static_cast<double>(axis.x) * dampingRounded);
        m_rotmom.y = static_cast<float>(m_rotmom.y - static_cast<double>(axis.y) * dampingRounded);
        m_rotmom.z = static_cast<float>(static_cast<double>(m_rotmom.z) - dz);
        m_rotmom.w = static_cast<float>(static_cast<double>(m_rotmom.w) - dw);
    }
    // Upright on three or more wheels and not steering: the turning about up damped towards 0.
    if (m_steering == 0.0f && static_cast<uint32_t>(m_wheelsInContact) > 2 && m_matrix.row[1].y > kHalf) {
        double yaw = static_cast<double>(m_yawDamping) * m_timestep;
        if (std::fabs(static_cast<double>(m_rotmom.y)) < std::fabs(yaw)) {
            m_rotmom.y = 0.0f;
            return;
        }
        if (m_rotmom.y > 0.0f)
            yaw = -yaw;
        m_rotmom.y = static_cast<float>(yaw + m_rotmom.y);
    }
}

// 0x00436280
void CVehicleComponent::ApplyGravity()
{
    double gravity;
    if (m_righting && static_cast<uint32_t>(Tmr::GameClockElapsed()) +
                              (static_cast<uint32_t>(g_clockOffset) - static_cast<uint32_t>(m_rightingStart)) >
                          300)
        gravity = static_cast<double>(m_gravityScale) / m_invMass * kRightingGravity;
    else
        gravity = static_cast<double>(m_gravityScale) / m_invMass * kGravity;
    m_force.y = static_cast<float>(m_force.y - gravity);
    if (m_debugMode) {
        Mth::Vector down{m_pos.x, static_cast<float>(-static_cast<double>(kGravity) / m_invMass * 0.05f + m_pos.y),
                         m_pos.z, static_cast<float>(static_cast<double>(m_pos.w) + 0.05f)};
        Gfx::AddDebugLine(&m_pos, &down, 0xFF003232, 0xFF003232, 1);
    }
}

// 0x00436340
void CVehicleComponent::ApplySuspension()
{
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        if (wheel.state == kWheelInAir) {
            // (In the air the history gets an even share of the weight; the average isn't updated.)
            wheel.loadHistory[m_loadIndex] =
                static_cast<float>(static_cast<double>(kGravity) / m_invMass / Unsigned(m_wheelCount));
            continue;
        }
        // The spring (by how far the suspension is compressed along the ground's normal) and the damper.
        const Mth::Vector& n = wheel.normal;
        const Mth::Vector& up = m_matrix.row[1];
        double upAlongNormal = (static_cast<double>(up.z) * n.z + static_cast<double>(up.y) * n.y) + static_cast<double>(up.x) * n.x;
        double spring = -(upAlongNormal * (static_cast<double>(wheel.restY) - wheel.y) * wheel.springRate);
        double closing = (static_cast<double>(wheel.vel.z) * n.z + static_cast<double>(wheel.vel.y) * n.y) +
                         static_cast<double>(wheel.vel.x) * n.x;
        double force = spring - closing * wheel.dampingRate;
        float forceRounded = static_cast<float>(force);
        Mth::Vector push{static_cast<float>(force * n.x), static_cast<float>(static_cast<double>(forceRounded) * n.y),
                         static_cast<float>(static_cast<double>(forceRounded) * n.z),
                         static_cast<float>(static_cast<double>(forceRounded) * n.w)};
        AddForceAtPoint(&push, &wheel.pos, 0xFF006464);
        wheel.loadHistory[m_loadIndex] = forceRounded;
        // The load: the average of the last ten.
        wheel.load = 0.0f;
        for (int k = 9; k >= 0; --k)
            wheel.load = static_cast<float>(static_cast<double>(wheel.loadHistory[k]) + wheel.load);
        wheel.load = static_cast<float>(static_cast<double>(wheel.load) * kTenth);
    }
    int8_t next = static_cast<int8_t>(m_loadIndex + 1);
    m_loadIndex = static_cast<int8_t>(next % 10);
}

// 0x004364B0
void CVehicleComponent::ApplyTyreForces()
{
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        if (wheel.state == kWheelInAir)
            continue;
        // The slip velocity: the ground velocity less the tyre's rolling (as UpdateWheelFriction).
        const Mth::Vector side = wheel.sideways;
        double rolling = static_cast<double>(wheel.spin) * wheel.radius;
        float rollY = static_cast<float>(side.y * rolling);
        float rollZ = static_cast<float>(side.z * rolling);
        float rollW = static_cast<float>(side.w * rolling);
        Mth::Vector slip;
        slip.x = static_cast<float>(wheel.contactVel.x - side.x * rolling);
        slip.y = static_cast<float>(static_cast<double>(wheel.contactVel.y) - rollY);
        slip.z = static_cast<float>(static_cast<double>(wheel.contactVel.z) - rollZ);
        slip.w = static_cast<float>(static_cast<double>(wheel.contactVel.w) - rollW);
        double speed = std::sqrt((static_cast<double>(slip.z) * slip.z + static_cast<double>(slip.y) * slip.y) +
                                 static_cast<double>(slip.x) * slip.x);
        float speedRounded = static_cast<float>(speed);
        wheel.slip = speedRounded;
        if (speed < 1e-6)
            continue;
        // The friction against the slip's direction.
        double dirX = slip.x;
        float dirY = slip.y, dirZ = slip.z;
        if (speedRounded > 0.0f) {
            double inverse = 1.0 / speedRounded;
            float inverseRounded = static_cast<float>(inverse);
            dirX = inverse * slip.x;
            dirY = static_cast<float>(static_cast<double>(slip.y) * inverseRounded);
            dirZ = static_cast<float>(static_cast<double>(slip.z) * inverseRounded);
        }
        float scale = static_cast<float>(-(static_cast<double>(wheel.friction) * wheel.load));
        double frictionX = dirX * scale;
        Mth::Vector friction{0.0f, static_cast<float>(static_cast<double>(dirY) * scale),
                             static_cast<float>(static_cast<double>(dirZ) * scale),
                             static_cast<float>(static_cast<double>(slip.w) * scale)};
        // The torque that would stop the slip along the tyre this step, and the friction's own torque on it: the
        // smaller one acts on the wheel; when it's the stopping one, the rest of it pushes the body.
        float stop = static_cast<float>(
            ((static_cast<double>(slip.z) * side.z + static_cast<double>(slip.y) * side.y) + static_cast<double>(slip.x) * side.x) /
            wheel.radius / (static_cast<double>(m_timestep) * wheel.invMoment));
        double along = (static_cast<double>(friction.z) * side.z + static_cast<double>(friction.y) * side.y) + frictionX * side.x;
        double frictionTorque = -(along * wheel.radius);
        float wheelTorque;
        Mth::Vector push;
        if (std::fabs(static_cast<double>(stop)) > std::fabs(frictionTorque)) {
            push = {static_cast<float>(frictionX), friction.y, friction.z, friction.w};
            wheelTorque = static_cast<float>(frictionTorque);
        } else {
            double rest = -1.0 / wheel.radius * stop - along;
            Mth::Vector extra{static_cast<float>(side.x * rest), static_cast<float>(side.y * rest),
                              static_cast<float>(side.z * rest), static_cast<float>(side.w * rest)};
            push = {static_cast<float>(frictionX + extra.x), static_cast<float>(static_cast<double>(extra.y) + friction.y),
                    static_cast<float>(static_cast<double>(extra.z) + friction.z),
                    static_cast<float>(static_cast<double>(extra.w) + friction.w)};
            wheelTorque = stop;
        }
        AddForceAtPoint(&push, &wheel.pos, 0xFFFF0000);
        SpinWheel(wheel, static_cast<double>(wheelTorque) * wheel.invMoment, m_timestep);
    }
}

// 0x004367E0
void CVehicleComponent::ApplyEngineBraking()
{
    if (m_throttle || m_reverse || m_brake)
        return;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        if (!wheel.drive || wheel.state == kWheelInAir)
            continue;
        int gear = GetGear(static_cast<float>(std::fabs(static_cast<double>(wheel.spin))));
        double ratio = m_gearRatios[gear];
        double braking = ratio * m_engineBraking * ratio / Unsigned(m_driveWheelCount);
        float stop = StoppingTorque(static_cast<double>(wheel.invMoment) * m_timestep, wheel.spin);
        if (braking > stop)
            braking = stop;
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        SpinWheel(wheel, braking * wheel.invMoment, m_timestep);
    }
}

// 0x00436900
void CVehicleComponent::ApplyDriveTorque()
{
    // Less drive on a slope (up.y squared / 0.9, at most 1).
    double upright = static_cast<double>(m_matrix.row[1].y) * m_matrix.row[1].y * kUprightBoost;
    float grip = upright > 1.0 ? 1.0f : static_cast<float>(upright);
    if (!(m_throttle || m_reverse) || m_brake)
        return;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        if (!wheel.drive || (m_handbrake && wheel.steeringSide != 0))
            continue;
        float spin;
        if (!m_reverse)
            spin = wheel.spin > 0.0f ? wheel.spin : 0.0f;
        else
            spin = wheel.spin < 0.0f ? -wheel.spin : 0.0f;
        int gear = GetGear(spin);
        double torque = static_cast<double>(m_gearRatios[gear]) * m_differentialRatio * m_driveTorque /
                        Unsigned(m_driveWheelCount) * grip;
        if (m_reverse)
            torque = -(torque * m_reverseRatio);
        if (m_handbrake && m_driveWheelCount == 4)
            torque = torque + torque;
        SpinWheel(wheel, torque * wheel.invMoment, m_timestep);
    }
}

// 0x00436A90
void CVehicleComponent::ApplyBrakes()
{
    if (!m_brake)
        return;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        double braking = wheel.brakeTorque;
        float stop = StoppingTorque(static_cast<double>(m_timestep) * wheel.invMoment, wheel.spin);
        if (braking > stop) {
            wheel.brakeExcess = static_cast<float>(braking - stop);
            braking = stop;
        } else {
            wheel.brakeExcess = 0.0f;
        }
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        SpinWheel(wheel, braking * wheel.invMoment, m_timestep);
    }
}

// 0x00436B60
void CVehicleComponent::ApplyHandbrake(float scale)
{
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        double braking = static_cast<double>(scale) * wheel.handbrakeTorque;
        float stop = StoppingTorque(static_cast<double>(m_timestep) * wheel.invMoment, wheel.spin);
        if (braking > stop) {
            wheel.brakeExcess = static_cast<float>(braking - stop);
            braking = stop;
        } else {
            wheel.brakeExcess = 0.0f;
        }
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        SpinWheel(wheel, braking * wheel.invMoment, m_timestep);
    }
}

// 0x00436C20
void CVehicleComponent::ApplyBrakeExcess()
{
    if (!m_brake)
        return;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        if (wheel.brakeExcess == 0.0f)
            continue;
        double braking = wheel.brakeExcess;
        float stop = StoppingTorque(static_cast<double>(m_timestep) * wheel.invMoment, wheel.spin);
        if (braking > stop)
            braking = stop;
        if (!(wheel.spin < 0.0f))
            braking = -braking;
        SpinWheel(wheel, braking * wheel.invMoment, m_timestep);
    }
}

// 0x00438A20
void CVehicleComponent::UpdateInertia()
{
    // R * diag(inverse moments) * R^T.
    for (int r = 0; r < 3; ++r) {
        (&m_invInertia.row[r].x)[0] = static_cast<float>(static_cast<double>((&m_matrix.row[r].x)[0]) * m_invMoments[0]);
        (&m_invInertia.row[r].x)[1] = static_cast<float>(static_cast<double>((&m_matrix.row[r].x)[1]) * m_invMoments[1]);
        (&m_invInertia.row[r].x)[2] = static_cast<float>(static_cast<double>((&m_matrix.row[r].x)[2]) * m_invMoments[2]);
    }
    Nx::MatrixTranspose transposed;
    transposed.TransposeFrom(&m_matrix);
    MatrixMultiply(&m_invInertia, &m_invInertia, &transposed);
}

// 0x00438AE0
void CVehicleComponent::ApplyWheelForces()
{
    ApplyGravity();
    ApplySuspension();
    UpdateWheelFriction();
    if (!m_handbrake) {
        ApplyDriveTorque();
        ApplyEngineBraking();
        ApplyBrakes();
        ApplyTyreForces();
        ApplyBrakeExcess();
        return;
    }
    if (m_throttle) {
        ApplyDriveTorque();
        ApplyHandbrake(static_cast<float>(1.0 - std::fabs(static_cast<double>(m_steering))));
        ApplyTyreForces();
        ApplyBrakeExcess();
        return;
    }
    // The handbrake without throttle locks the wheels.
    ApplyTyreForces();
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        m_wheels[i].spin = 0.0f;
        m_wheels[i].torque = 0.0f;
    }
}

// 0x0043AE10
void CVehicleComponent::UpdateDependents()
{
    QuatToMatrix(&m_orientation, &m_matrix);
    m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector turned;
    m_comOffset = m_matrix.RotateVector(turned, m_centerOfMass);
    UpdateInertia();
    UpdateVelocities();
    const Mth::Vector* r = m_matrix.row;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        // The suspension's top: the wheel's place turned and moved (x summed unrounded).
        const Mth::Vector& o = wheel.offset;
        double x = (static_cast<double>(r[1].x) * o.y + static_cast<double>(r[2].x) * o.z) + static_cast<double>(r[0].x) * o.x;
        float y = static_cast<float>((static_cast<double>(r[1].y) * o.y + static_cast<double>(r[0].y) * o.x) +
                                     static_cast<double>(r[2].y) * o.z);
        float z = static_cast<float>((static_cast<double>(r[1].z) * o.y + static_cast<double>(r[0].z) * o.x) +
                                     static_cast<double>(r[2].z) * o.z);
        float w = o.w;
        wheel.feelerStart.z = static_cast<float>(static_cast<double>(z) + m_pos.z);
        wheel.feelerStart.w = static_cast<float>(static_cast<double>(w) + m_pos.w);
        wheel.feelerStart.x = static_cast<float>(x + m_pos.x);
        wheel.feelerStart.y = static_cast<float>(static_cast<double>(y) + m_pos.y);
        // The bottom: the place lowered to its rest less the radius.
        Mth::Vector e = o;
        e.y = static_cast<float>((static_cast<double>(wheel.restY) - wheel.radius) + e.y);
        wheel.feelerEnd = e;
        x = (static_cast<double>(e.x) * r[0].x + static_cast<double>(r[2].x) * e.z) + static_cast<double>(r[1].x) * e.y;
        float yy = static_cast<float>((static_cast<double>(e.x) * r[0].y + static_cast<double>(e.y) * r[1].y) +
                                      static_cast<double>(r[2].y) * e.z);
        z = static_cast<float>((static_cast<double>(r[2].z) * e.z + static_cast<double>(r[1].z) * e.y) +
                               static_cast<double>(r[0].z) * e.x);
        w = e.w;
        wheel.feelerEnd.z = static_cast<float>(static_cast<double>(z) + m_pos.z);
        wheel.feelerEnd.w = static_cast<float>(static_cast<double>(w) + m_pos.w);
        wheel.feelerEnd.x = static_cast<float>(x + m_pos.x);
        wheel.feelerEnd.y = static_cast<float>(static_cast<double>(yy) + m_pos.y);
    }
    for (int i = 1; i >= 0; --i) {
        Collider& c = m_colliders[i];
        // The corner turned and moved; the edges turned (each with its own order of sums, as the original).
        const Mth::Vector& o = c.corner;
        double x = (static_cast<double>(r[2].x) * o.z + static_cast<double>(r[1].x) * o.y) + static_cast<double>(o.x) * r[0].x;
        float y = static_cast<float>((static_cast<double>(r[0].y) * o.x + static_cast<double>(r[2].y) * o.z) +
                                     static_cast<double>(r[1].y) * o.y);
        float z = static_cast<float>((static_cast<double>(r[0].z) * o.x + static_cast<double>(r[2].z) * o.z) +
                                     static_cast<double>(r[1].z) * o.y);
        float w = o.w;
        c.worldCorner.z = static_cast<float>(static_cast<double>(z) + m_pos.z);
        c.worldCorner.w = static_cast<float>(static_cast<double>(w) + m_pos.w);
        c.worldCorner.x = static_cast<float>(x + m_pos.x);
        c.worldCorner.y = static_cast<float>(static_cast<double>(y) + m_pos.y);
        const Mth::Vector& a = c.edges[0];
        c.worldEdges[0].z = static_cast<float>((static_cast<double>(r[2].z) * a.z + static_cast<double>(r[0].z) * a.x) +
                                               static_cast<double>(r[1].z) * a.y);
        c.worldEdges[0].w = a.w;
        c.worldEdges[0].x = static_cast<float>((static_cast<double>(r[2].x) * a.z + static_cast<double>(r[1].x) * a.y) +
                                               static_cast<double>(a.x) * r[0].x);
        c.worldEdges[0].y = static_cast<float>((static_cast<double>(r[2].y) * a.z + static_cast<double>(r[0].y) * a.x) +
                                               static_cast<double>(r[1].y) * a.y);
        const Mth::Vector& b = c.edges[1];
        float bw = b.w;
        c.worldEdges[1].z = static_cast<float>((static_cast<double>(r[2].z) * b.z + static_cast<double>(r[1].z) * b.y) +
                                               static_cast<double>(b.x) * r[0].z);
        c.worldEdges[1].x = static_cast<float>((static_cast<double>(r[0].x) * b.x + static_cast<double>(r[2].x) * b.z) +
                                               static_cast<double>(b.y) * r[1].x);
        c.worldEdges[1].w = bw;
        c.worldEdges[1].y = static_cast<float>((static_cast<double>(r[1].y) * b.y + static_cast<double>(r[2].y) * b.z) +
                                               static_cast<double>(r[0].y) * b.x);
        for (int k = 0; k < 2; ++k) {
            double inverse = 1.0 / c.lengths[k];
            const Mth::Vector& edge = c.worldEdges[k];
            c.directions[k] = {static_cast<float>(inverse * edge.x), static_cast<float>(inverse * edge.y),
                               static_cast<float>(inverse * edge.z), static_cast<float>(inverse * edge.w)};
        }
    }
    UpdateCollisionCache();
}

// 0x0043B240
void CVehicleComponent::UpdateWheelContacts()
{
    Nx::CFeeler feeler;
    feeler.Construct();
    feeler.mp_cache = &m_collCache;
    m_wheelsInContact = 0;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        feeler.m_line.start = wheel.feelerStart;
        feeler.m_line.end = wheel.feelerEnd;
        feeler.SetIgnore(0x10, 0);
        if (feeler.GetCollision(false, false)) {
            if (wheel.state == kWheelInAir) {
                TriggerFeeler(mp_trigger, kWheelLands, &feeler);
                if (m_interrupted || (m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                    return; // (the feeler's destructor lets go of its object)
            } else if (feeler.m_hit.object != wheel.feeler.m_hit.object) {
                // Onto another collision object: off the old, onto the new.
                TriggerFeeler(mp_trigger, kWheelLeaves, &wheel.feeler);
                if (m_interrupted || (m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                    return;
                TriggerFeeler(mp_trigger, kWheelLands, &feeler);
                if (m_interrupted || (m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                    return;
            }
            // Touching: the suspension's extension where the ground is, the feeler kept, its line ending there.
            wheel.feeler.Assign(feeler);
            wheel.state = kWheelOnGround;
            wheel.normal = feeler.m_normal;
            wheel.y = static_cast<float>((static_cast<double>(wheel.restY) - wheel.radius) * feeler.m_distance + wheel.radius);
            ++m_wheelsInContact;
            feeler.m_line.end = feeler.m_point;
        } else {
            if (wheel.state != kWheelInAir) {
                TriggerFeeler(mp_trigger, kWheelLeaves, &wheel.feeler);
                if (m_interrupted || (m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                    return;
            }
            wheel.state = kWheelInAir;
            wheel.y = wheel.restY;
        }
        // Outside the park editor, triggers along the suspension (faces flagged 0x50).
        if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(Ed::g_parkEditor) + 0x40) != kParkEditorOff) {
            feeler.SetIgnore(0, 0x50);
            if (feeler.GetCollision(false, false)) {
                TriggerFeeler(mp_trigger, kWheelCrossing, &feeler);
                if (m_interrupted || (m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                    return;
            }
        }
    }
    // The contact points (relative to the body's position, turned) and the body's velocity there.
    const Mth::Vector* r = m_matrix.row;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        Mth::Vector& p = wheel.pos;
        p = wheel.offset;
        p.y = static_cast<float>((static_cast<double>(wheel.y) - wheel.radius) + p.y);
        double x = (static_cast<double>(p.x) * r[0].x + static_cast<double>(r[2].x) * p.z) + static_cast<double>(r[1].x) * p.y;
        double y = (static_cast<double>(p.x) * r[0].y + static_cast<double>(p.y) * r[1].y) + static_cast<double>(r[2].y) * p.z;
        float z = static_cast<float>((static_cast<double>(r[2].z) * p.z + static_cast<double>(r[1].z) * p.y) +
                                     static_cast<double>(r[0].z) * p.x);
        p.z = z;
        p.x = static_cast<float>(x);
        p.y = static_cast<float>(y);
        Mth::Vector vel;
        wheel.vel = *GetPointVelocity(&vel, &p);
    }
}

// 0x00437550
void CVehicleComponent::CheckSleep()
{
    int still = m_stillFrames;
    m_stillFrames = 0;
    if (m_throttle)
        return;
    if (m_reverse && m_wheelsInContact != 0)
        return;
    if (static_cast<uint32_t>(m_wheelsInContact + m_contactCount) < 3)
        return;
    if (m_wheelsInContact != 0 && m_wheelsInContact != m_wheelCount)
        return;
    double vx = m_vel.x, vy = m_vel.y, vz = m_vel.z;
    if ((vx * vx + vy * vy) + vz * vz > kSleepSpeed)
        return;
    double sx = m_rotvel.x, sy = m_rotvel.y, sz = m_rotvel.z;
    if ((sx * sx + sy * sy) + sz * sz > kSleepSpin)
        return;
    if (IsKicked())
        return;
    m_stillFrames = still + 1;
    if (m_stillFrames < 3)
        return;
    m_mom = {0.0f, 0.0f, 0.0f, 1.0f};
    m_rotmom = {0.0f, 0.0f, 0.0f, 1.0f};
    m_force = {0.0f, 0.0f, 0.0f, 1.0f};
    m_torque = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = m_wheelCount - 1; i >= 0; --i)
        m_wheels[i].torque = 0.0f;
    m_awake = 0;
}

// 0x004376B0
void CVehicleComponent::ReadControls()
{
    uint8_t* input = mp_input;
    // The stick squared (its sign kept); else the d-pad's left / right.
    float stick = *reinterpret_cast<float*>(input + 0x238);
    double steering = std::fabs(static_cast<double>(stick)) * stick;
    m_steering = static_cast<float>(steering);
    if (steering == 0.0) {
        bool left = input[0x60] != 0, right = input[0x80] != 0;
        if (left && !right)
            m_steering = -1.0f;
        else if (!left && right)
            m_steering = 1.0f;
    }
    m_throttle = input[0x1C0] != 0;
    m_handbrake = input[0x100] != 0 && !m_noHandbrake;
    // The brake button: braking while moving forward, else reversing; with Exitable, the other button too, which
    // also brakes while moving backwards and tells the object it has stopped (0xCBAA3476).
    bool pressed, other = false;
    if (input[0x180]) {
        pressed = true;
    } else if (m_exitable && input[0x1A0]) {
        pressed = true;
        other = true;
    } else {
        pressed = false;
    }
    if (pressed) {
        bool brake;
        const Mth::Vector& at = m_matrix.row[2];
        if (!m_reverse &&
            (!((static_cast<double>(m_vel.z) * at.z + static_cast<double>(m_vel.y) * at.y) + static_cast<double>(at.x) * m_vel.x <
               kReverseSpeed) ||
             m_wheelsInContact == 0))
            brake = true;
        else
            brake = !(m_awake != 0 || m_wheelsInContact != 0);
        m_brake = brake;
        m_reverse = !brake;
        if (other) {
            double forward = (static_cast<double>(m_vel.z) * at.z + static_cast<double>(m_vel.y) * at.y) +
                             static_cast<double>(m_vel.x) * at.x;
            if (forward < 0.0) {
                m_brake = true;
                m_reverse = false;
            }
            if (std::fabs(forward) < kStoppedSpeed)
                m_object->BroadcastEvent(0xCBAA3476, nullptr, 0.0f);
        }
    } else {
        m_reverse = false;
        m_brake = false;
    }
    if (m_handbrakeActive) {
        m_brake = true;
        m_reverse = false;
    }
    if (m_awake == 0 && (m_throttle || m_reverse)) {
        m_awake = 1;
        m_stillFrames = 0;
    }
}

// 0x004378B0
void CVehicleComponent::UpdateSkater()
{
    CCompositeObject* object = m_object;
    CCompositeObject* skater = mp_skater;
    Mth::Vector place;
    object->m_matrix.RotateVector(place, m_skaterPos);
    skater->m_pos.z = static_cast<float>(static_cast<double>(object->m_pos.z) + place.z);
    skater->m_pos.w = static_cast<float>(static_cast<double>(object->m_pos.w) + place.w);
    skater->m_pos.x = static_cast<float>(static_cast<double>(place.x) + object->m_pos.x);
    skater->m_pos.y = static_cast<float>(static_cast<double>(object->m_pos.y) + place.y);
    skater->m_matrix = object->m_matrix;
    // The skater's core physics keeps its own copy of its object's matrix (+0x18).
    uint8_t* core = reinterpret_cast<uint8_t*>(mp_skaterCorePhysics);
    *reinterpret_cast<Mth::Matrix*>(core + 0x18) = reinterpret_cast<CBaseComponent*>(core)->m_object->m_matrix;
    skater->m_vel = object->m_vel;
    if (!m_skaterVisible)
        return;
    // The steering animation's frame follows the smoothed steering (mirrored unless the skater's flag 0x28 is set).
    uint8_t* flags = *reinterpret_cast<uint8_t**>(core + 0x1528);
    float steering = flags[0x28] ? m_displaySteering : -m_displaySteering;
    auto* blender = reinterpret_cast<CAnimBlenderComponent*>(mp_animBlender);
    double duration = blender->GetValue(m_skaterAnim);
    float frame = static_cast<float>((static_cast<double>(steering) - -1.0) * kHalf * duration);
    uint32_t frameBits = std::bit_cast<uint32_t>(frame);
    uint32_t blend = std::bit_cast<uint32_t>(1000.0f);
    if (!GameNet::g_gameNetManager->InNetGame()) {
        blender->PlayAnimationEx(m_skaterAnim, 0, frameBits, blend, 1, 0, 0);
        return;
    }
    // In a network game every fifteenth update is sent on (b = 1).
    int& counter = g_vehicleNetUpdateCounter;
    if (++counter == 15) {
        counter = 0;
        blender->PlayAnimationEx(m_skaterAnim, 1, frameBits, blend, 1, 0, 0);
        return;
    }
    blender->PlayAnimationEx(m_skaterAnim, 0, frameBits, blend, 1, 0, 0);
}

namespace {

void __fastcall UpdateSteeringOf(CVehicleComponent* vehicle) { vehicle->UpdateSteering(); }

// 0x00435F00 as the game calls it: the original update (0x0043C1D0) calls ApplyDamping right after it without
// loading ECX again - kept.
__declspec(naked) void UpdateSteeringStub()
{
    __asm {
        push ecx
        call UpdateSteeringOf
        pop ecx
        ret
    }
}

} // namespace

} // namespace Obj

REPLACE(0x00435B60, &Obj::CVehicleComponent::UpdateWheelFriction);
REPLACE(0x00435DA0, &Obj::CVehicleComponent::UpdateCollisionCache);
REPLACE_UNCHECKED(0x00435F00, Obj::UpdateSteeringStub);
REPLACE(0x004360C0, &Obj::CVehicleComponent::ApplyDamping);
REPLACE(0x00436280, &Obj::CVehicleComponent::ApplyGravity);
REPLACE(0x00436340, &Obj::CVehicleComponent::ApplySuspension);
REPLACE(0x004364B0, &Obj::CVehicleComponent::ApplyTyreForces);
REPLACE(0x004367E0, &Obj::CVehicleComponent::ApplyEngineBraking);
REPLACE(0x00436900, &Obj::CVehicleComponent::ApplyDriveTorque);
REPLACE(0x00436A90, &Obj::CVehicleComponent::ApplyBrakes);
REPLACE(0x00436B60, &Obj::CVehicleComponent::ApplyHandbrake);
REPLACE(0x00436C20, &Obj::CVehicleComponent::ApplyBrakeExcess);
REPLACE(0x00438A20, &Obj::CVehicleComponent::UpdateInertia);
REPLACE(0x00438AE0, &Obj::CVehicleComponent::ApplyWheelForces);
REPLACE(0x0043AE10, &Obj::CVehicleComponent::UpdateDependents);
REPLACE(0x0043B240, &Obj::CVehicleComponent::UpdateWheelContacts);
REPLACE(0x00437550, &Obj::CVehicleComponent::CheckSleep);
REPLACE(0x004376B0, &Obj::CVehicleComponent::ReadControls);
REPLACE(0x004378B0, &Obj::CVehicleComponent::UpdateSkater);
