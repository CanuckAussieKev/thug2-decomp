// The Vehicle component: its small methods and script member functions. See vehicle_component.h (physics:
// vehicle_physics.cpp; collision: vehicle_collision.cpp). From the disassembly of 0x004351C0-0x0043C536 (the member
// functions 0x004353F0, 0x00435480, 0x00435490, 0x004354B0 and 0x0043B670, the helper 0x0043AA90 and the wheel's
// destructor 0x00437CF0 aren't Ghidra functions). Float arithmetic follows the original's x87 code: double (53 bits)
// where it stays on the stack, rounded to float where it is stored.

#include "game/obj/vehicle_component.h"

#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/mth/random.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr uint32_t kPercent = 0x9E497FC6;
constexpr uint32_t kDuration = 0x79A07F3F;
constexpr uint32_t kOffset = 0xA6F5352F;

const float kHalf = 0.5f;                      // 0x006453BC
const float kHundredPi = 314.159271f;          // 0x006466A8 (the wheels' roll wraps at 200 pi by 100 pi)
const float kTwoHundredPi = 628.318542f;       // 0x006466AC
const float kHundredth = 0.00999999978f;       // 0x0064544C

// The game's strings (the debug message's format and file).
const char* const kDebugFormat = reinterpret_cast<const char*>(0x006466B0);     // "M:%s:%i: %s\n"
const char* const kDebugFile = reinterpret_cast<const char*>(0x006466CC);       // "omponents\vehiclecomponent.cpp"

// A quaternion's *= (0x00420140; not rewritten yet).
void QuatMultiply(Mth::Vector* a, const Mth::Vector* b)
{
    GameFunction<void(__thiscall*)(Mth::Vector*, const Mth::Vector*)>(0x00420140)(a, b);
}

} // namespace

// 0x00435870
CVehicleComponent::Wheel* CVehicleComponent::Wheel::Construct()
{
    feeler.Construct();
    return this;
}

// 0x00437CF0
void CVehicleComponent::Wheel::Destruct() { feeler.m_object.Release(); }

// 0x004351C0
void CVehicleComponent::UpdateWheelRotations()
{
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        double rotation =
            (static_cast<double>(wheel.spin) - static_cast<double>(wheel.torque) * m_timestep * kHalf) * m_timestep +
            wheel.rotation;
        wheel.rotation = static_cast<float>(rotation);
        if (rotation > kTwoHundredPi)
            wheel.rotation = static_cast<float>(rotation - kHundredPi);
        else if (rotation < 0.0)
            wheel.rotation = static_cast<float>(rotation + kHundredPi);
    }
}

// 0x00435250
double CVehicleComponent::GetTyreFriction(Wheel* wheel, float slip)
{
    if (m_handbrake) {
        double friction;
        if (m_throttle) {
            friction = wheel->handbrakeThrottleFriction;
            wheel->state = kWheelHandbrakeThrottle;
        } else {
            friction = wheel->handbrakeLockedFriction;
            wheel->state = kWheelHandbrakeLocked;
        }
        if (slip < wheel->minStaticGripVel) {
            wheel->state = kWheelStaticSlow;
            return friction / wheel->minStaticGripVel * slip;
        }
        return friction;
    }
    double scale = m_brake ? 2.0 : 1.0; // (0x00645448 / 0x006453A8)
    if (slip < wheel->minStaticGripVel) {
        wheel->state = kWheelStaticSlow;
        return static_cast<double>(wheel->staticFriction) / wheel->minStaticGripVel * slip;
    }
    if (slip < wheel->maxStaticGripVel) {
        wheel->state = kWheelStatic;
        return scale * wheel->staticFriction;
    }
    if (slip < wheel->minDynamicGripVel) {
        wheel->state = kWheelStaticToDynamic;
        double blend = (static_cast<double>(wheel->staticFriction) - wheel->dynamicFriction) *
                       (static_cast<double>(slip) - wheel->maxStaticGripVel) /
                       (static_cast<double>(wheel->minDynamicGripVel) - wheel->maxStaticGripVel);
        return scale * wheel->dynamicFriction + blend;
    }
    wheel->state = kWheelDynamic;
    return scale * wheel->dynamicFriction;
}

// 0x00435380
int CVehicleComponent::GetGear(float spin)
{
    float upshift = m_upshiftSpeed;
    int gear = 0;
    if (!(static_cast<double>(m_gearRatios[0]) * m_differentialRatio * spin > upshift))
        return gear;
    while (gear < m_gearCount - 1) {
        double engine = static_cast<double>(m_gearRatios[gear + 1]) * m_differentialRatio * spin;
        ++gear;
        if (!(engine > upshift))
            break;
    }
    return gear;
}

// 0x00435520
void CVehicleComponent::UpdateVelocities()
{
    double invMass = m_invMass;
    m_vel.z = static_cast<float>(invMass * m_mom.z);
    m_vel.w = static_cast<float>(invMass * m_mom.w);
    m_vel.x = static_cast<float>(invMass * m_mom.x);
    m_vel.y = static_cast<float>(invMass * m_mom.y);
    m_vel.w = 0.0f;
    Mth::Vector rotvel;
    m_rotvel = m_invInertia.RotateVector(rotvel, m_rotmom);
}

// 0x004355B0
Mth::Vector* CVehicleComponent::GetPointVelocity(Mth::Vector* out, const Mth::Vector* offset)
{
    // The angular velocity across the offset, plus the velocity.
    float x = static_cast<float>(static_cast<double>(offset->z) * m_rotvel.y - static_cast<double>(offset->y) * m_rotvel.z);
    float y = static_cast<float>(static_cast<double>(m_rotvel.z) * offset->x - static_cast<double>(m_rotvel.x) * offset->z);
    float z = static_cast<float>(static_cast<double>(m_rotvel.x) * offset->y - static_cast<double>(m_rotvel.y) * offset->x);
    float w = m_vel.w;
    out->x = static_cast<float>(static_cast<double>(x) + m_vel.x);
    out->y = static_cast<float>(static_cast<double>(y) + m_vel.y);
    out->z = static_cast<float>(static_cast<double>(z) + m_vel.z);
    out->w = w;
    return out;
}

// 0x00435640
void CVehicleComponent::AddForceAtPoint(const Mth::Vector* force, const Mth::Vector* point, uint32_t)
{
    m_force.x = static_cast<float>(static_cast<double>(m_force.x) + force->x);
    m_force.y = static_cast<float>(static_cast<double>(force->y) + m_force.y);
    m_force.z = static_cast<float>(static_cast<double>(force->z) + m_force.z);
    m_force.w = static_cast<float>(static_cast<double>(force->w) + m_force.w);
    // The lever from the centre of mass; its torque's x and y rounded, z not.
    double rx = static_cast<double>(point->x) - m_comOffset.x;
    double ry = static_cast<double>(point->y) - m_comOffset.y;
    double rz = static_cast<double>(point->z) - m_comOffset.z;
    float tx = static_cast<float>(ry * force->z - rz * force->y);
    float ty = static_cast<float>(rz * force->x - rx * force->z);
    double tz = rx * force->y - ry * force->x;
    m_torque.x = static_cast<float>(static_cast<double>(tx) + m_torque.x);
    m_torque.y = static_cast<float>(static_cast<double>(ty) + m_torque.y);
    m_torque.z = static_cast<float>(tz + m_torque.z);
    // (the colour was for a debug line, compiled out)
}

// 0x00435710
void CVehicleComponent::AddForceAtOffset(const Mth::Vector* force, const Mth::Vector* offset)
{
    m_force.x = static_cast<float>(static_cast<double>(m_force.x) + force->x);
    m_force.y = static_cast<float>(static_cast<double>(force->y) + m_force.y);
    m_force.z = static_cast<float>(static_cast<double>(force->z) + m_force.z);
    m_force.w = static_cast<float>(static_cast<double>(force->w) + m_force.w);
    // The torque: x and y unrounded, z rounded.
    double tx = static_cast<double>(offset->y) * force->z - static_cast<double>(force->y) * offset->z;
    double ty = static_cast<double>(offset->z) * force->x - static_cast<double>(force->z) * offset->x;
    float tz = static_cast<float>(static_cast<double>(force->y) * offset->x - static_cast<double>(offset->y) * force->x);
    m_torque.x = static_cast<float>(tx + m_torque.x);
    m_torque.y = static_cast<float>(ty + m_torque.y);
    m_torque.z = static_cast<float>(static_cast<double>(tz) + m_torque.z);
}

// 0x004357C0
void CVehicleComponent::ApplyImpulseAtOffset(const Mth::Vector* impulse, const Mth::Vector* offset)
{
    m_mom.x = static_cast<float>(static_cast<double>(m_mom.x) + impulse->x);
    m_mom.y = static_cast<float>(static_cast<double>(impulse->y) + m_mom.y);
    m_mom.z = static_cast<float>(static_cast<double>(impulse->z) + m_mom.z);
    m_mom.w = static_cast<float>(static_cast<double>(impulse->w) + m_mom.w);
    double tx = static_cast<double>(offset->y) * impulse->z - static_cast<double>(impulse->y) * offset->z;
    double ty = static_cast<double>(offset->z) * impulse->x - static_cast<double>(impulse->z) * offset->x;
    float tz = static_cast<float>(static_cast<double>(impulse->y) * offset->x - static_cast<double>(offset->y) * impulse->x);
    m_rotmom.x = static_cast<float>(tx + m_rotmom.x);
    m_rotmom.y = static_cast<float>(ty + m_rotmom.y);
    m_rotmom.z = static_cast<float>(static_cast<double>(tz) + m_rotmom.z);
    UpdateVelocities();
}

// 0x00435850
bool CVehicleComponent::IsKicked() { return m_kickTime > 0.0f; }

// 0x00435890
void CVehicleComponent::KickRandomlyAlong(const Mth::Vector* along)
{
    // The direction flattened (y 0) and made unit (w kept), and its sideways (-z, 0, x).
    float dx = along->x, dy = 0.0f, dz = along->z, dw = along->w;
    double length = std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx);
    if (length > 0.0) {
        double scale = 1.0 / length;
        dx = static_cast<float>(dx * scale);
        dy = static_cast<float>(0.0 * scale);
        dz = static_cast<float>(dz * scale);
    }
    float sideways = -dz;

    // The push: along the direction (0x3DB3FA83 * (1 + +-0.5)), sideways (0x39E99FEC * +-1.5), up
    // (0x738500FD * (1 + +-0.2)).
    float forwardScale = static_cast<float>(Mth::PlusOrMinus(0.5f) + 1.0);
    float forward = static_cast<float>(Script::GetFloat(0x3DB3FA83) * static_cast<double>(forwardScale));
    float sideScale = Script::GetFloat(0x39E99FEC);
    double side = static_cast<double>(Mth::PlusOrMinus(1.5f)) * sideScale;
    double sideX = sideways * side;
    float sideY = static_cast<float>(side * 0.0);
    float sideZ = static_cast<float>(dx * side);
    float sideW = static_cast<float>(side);
    float pushX = static_cast<float>(static_cast<double>(dx) * forward + sideX);
    float pushY = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dy) * forward)) + sideY);
    float pushZ = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dz) * forward)) + sideZ);
    float pushW = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dw) * forward)) + sideW);
    float upScale = static_cast<float>(Mth::PlusOrMinus(0.2f) + 1.0);
    pushY = static_cast<float>(static_cast<double>(pushY) + Script::GetFloat(0x738500FD) * static_cast<double>(upScale));

    // The spin: about the up axis (0x14DDB3EF * +-1.5) and about the sideways one (0x4088E6EC * (1 + +-0.8)).
    float yawScale = Script::GetFloat(0x14DDB3EF);
    float yaw = static_cast<float>(static_cast<double>(Mth::PlusOrMinus(1.5f)) * yawScale);
    float rollScale = static_cast<float>(Mth::PlusOrMinus(0.8f) + 1.0);
    double roll = static_cast<double>(Script::GetFloat(0x4088E6EC)) * rollScale;
    double spinX = sideways * roll;
    float spinY = static_cast<float>(roll * 0.0 + yaw);
    float spinZ = static_cast<float>(dx * roll);
    float spinW = static_cast<float>(roll);

    // Into the momenta: the push times the mass, the spin times the first moment of inertia (for every axis).
    double mass = 1.0 / m_invMass;
    float momY = static_cast<float>(pushY * mass), momZ = static_cast<float>(pushZ * mass);
    float momW = static_cast<float>(pushW * mass);
    m_mom.x = static_cast<float>(pushX * mass + m_mom.x);
    m_mom.y = static_cast<float>(static_cast<double>(momY) + m_mom.y);
    m_mom.z = static_cast<float>(static_cast<double>(momZ) + m_mom.z);
    m_mom.w = static_cast<float>(static_cast<double>(momW) + m_mom.w);
    float moment = static_cast<float>(1.0 / m_invMoments[0]);
    float rotZ = static_cast<float>(static_cast<double>(spinZ) * moment);
    float rotW = static_cast<float>(static_cast<double>(spinW) * moment);
    m_rotmom.x = static_cast<float>(spinX * moment + m_rotmom.x);
    m_rotmom.y = static_cast<float>(static_cast<double>(spinY) * moment + m_rotmom.y);
    m_rotmom.z = static_cast<float>(static_cast<double>(rotZ) + m_rotmom.z);
    m_rotmom.w = static_cast<float>(static_cast<double>(rotW) + m_rotmom.w);
    UpdateVelocities();
    m_kickTime = Script::GetFloat(0x771922A6);
    m_awake = 1;
    m_stillFrames = 0;
}

// 0x004354C0 (cdecl)
Mth::Vector* QuatProduct(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b)
{
    Mth::Vector product = *a;
    QuatMultiply(&product, b);
    *out = product;
    return out;
}

// --- script member functions ---

// 0x00437A60
bool CVehicleComponent::Kick(Script::CStruct* params, Script::CScript*)
{
    m_awake = 1;
    m_stillFrames = 0;
    // (Without linear or angular the original uses whatever was on its stack.)
    Mth::Vector linear, angular, turned;
    params->GetVector(reinterpret_cast<const char*>(0x00646768), reinterpret_cast<Script::VectorValue*>(&linear), 0);
    params->GetVector(reinterpret_cast<const char*>(0x00646760), reinterpret_cast<Script::VectorValue*>(&angular), 0);
    linear = m_matrix.RotateVector(turned, linear);
    angular = m_matrix.RotateVector(turned, angular);
    double mass = 1.0 / m_invMass;
    float momY = static_cast<float>(linear.y * mass), momZ = static_cast<float>(linear.z * mass);
    float momW = static_cast<float>(linear.w * mass);
    m_mom.x = static_cast<float>(linear.x * mass + m_mom.x);
    m_mom.y = static_cast<float>(static_cast<double>(momY) + m_mom.y);
    m_mom.z = static_cast<float>(static_cast<double>(momZ) + m_mom.z);
    m_mom.w = static_cast<float>(static_cast<double>(momW) + m_mom.w);
    double moment = 1.0 / m_invMoments[0];
    float rotY = static_cast<float>(angular.y * moment), rotZ = static_cast<float>(angular.z * moment);
    float rotW = static_cast<float>(angular.w * moment);
    m_rotmom.x = static_cast<float>(angular.x * moment + m_rotmom.x);
    m_rotmom.y = static_cast<float>(static_cast<double>(rotY) + m_rotmom.y);
    m_rotmom.z = static_cast<float>(static_cast<double>(rotZ) + m_rotmom.z);
    m_rotmom.w = static_cast<float>(static_cast<double>(rotW) + m_rotmom.w);
    UpdateVelocities();
    return true;
}

// 0x004353F0
bool CVehicleComponent::Wake(Script::CStruct*, Script::CScript*)
{
    m_awake = 1;
    m_stillFrames = 0;
    return true;
}

// 0x00435410
bool CVehicleComponent::AdjustGravity(Script::CStruct* params, Script::CScript*)
{
    params->GetFloat(kPercent, &m_gravityScale, 1);
    m_gravityScale = static_cast<float>(static_cast<double>(m_gravityScale) * kHundredth);
    params->GetFloat(kDuration, &m_gravityTime, 1);
    Dbg::Printf(kDebugFormat, kDebugFile, 0xD78, reinterpret_cast<const char*>(0x006466EC)); // "Initiating vehicle gravity override"
    return true;
}

// 0x00435480
bool CVehicleComponent::HandbrakeActive(Script::CStruct*, Script::CScript*) { return m_handbrakeActive; }

// 0x00435490
bool CVehicleComponent::AllWheelsAreInContact(Script::CStruct*, Script::CScript*)
{
    return m_wheelsInContact == m_wheelCount;
}

// 0x00437BC0
bool CVehicleComponent::KickRandomly(Script::CStruct* params, Script::CScript*)
{
    Mth::Vector along;
    params->GetVector(kOffset, reinterpret_cast<Script::VectorValue*>(&along), 1);
    KickRandomlyAlong(&along);
    return true;
}

// 0x004354B0
bool CVehicleComponent::IsSkaterVisible(Script::CStruct*, Script::CScript*) { return m_skaterVisible; }

namespace {

void __fastcall UpdateWheelRotationsOf(CVehicleComponent* vehicle) { vehicle->UpdateWheelRotations(); }

// 0x004351C0 as the game calls it: the original update (0x0043C1D0, asleep) calls UpdateSkeleton right after it
// without loading ECX again - kept.
__declspec(naked) void UpdateWheelRotationsStub()
{
    __asm {
        push ecx
        call UpdateWheelRotationsOf
        pop ecx
        ret
    }
}

} // namespace

} // namespace Obj

REPLACE(0x00435870, &Obj::CVehicleComponent::Wheel::Construct);
REPLACE(0x00437CF0, &Obj::CVehicleComponent::Wheel::Destruct);
REPLACE_UNCHECKED(0x004351C0, Obj::UpdateWheelRotationsStub);
REPLACE(0x00435250, &Obj::CVehicleComponent::GetTyreFriction);
REPLACE(0x00435380, &Obj::CVehicleComponent::GetGear);
REPLACE(0x00435520, &Obj::CVehicleComponent::UpdateVelocities);
REPLACE(0x004355B0, &Obj::CVehicleComponent::GetPointVelocity);
REPLACE(0x00435640, &Obj::CVehicleComponent::AddForceAtPoint);
REPLACE(0x00435710, &Obj::CVehicleComponent::AddForceAtOffset);
REPLACE(0x004357C0, &Obj::CVehicleComponent::ApplyImpulseAtOffset);
REPLACE(0x00435850, &Obj::CVehicleComponent::IsKicked);
REPLACE(0x00435890, &Obj::CVehicleComponent::KickRandomlyAlong);
REPLACE(0x004354C0, Obj::QuatProduct);
REPLACE(0x00437A60, &Obj::CVehicleComponent::Kick);
REPLACE(0x004353F0, &Obj::CVehicleComponent::Wake);
REPLACE(0x00435410, &Obj::CVehicleComponent::AdjustGravity);
REPLACE(0x00435480, &Obj::CVehicleComponent::HandbrakeActive);
REPLACE(0x00435490, &Obj::CVehicleComponent::AllWheelsAreInContact);
REPLACE(0x00437BC0, &Obj::CVehicleComponent::KickRandomly);
REPLACE(0x004354B0, &Obj::CVehicleComponent::IsSkaterVisible);
