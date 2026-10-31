// The Vehicle component: its life (creation, parameters, the update, the model's bones), its small methods and
// script member functions. See vehicle_component.h (physics: vehicle_physics.cpp; collision: vehicle_collision.cpp). From the disassembly of 0x004351C0-0x0043C536 (the member
// functions 0x004353F0, 0x00435480, 0x00435490, 0x004354B0 and 0x0043B670, the helper 0x0043AA90 and the wheel's
// destructor 0x00437CF0 aren't Ghidra functions). Float arithmetic follows the original's x87 code: double (53 bits)
// where it stays on the stack, rounded to float where it is stored.

#include "game/obj/vehicle_component.h"
#include "game/gfx/model.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/viewport.h"
#include "game/skate/skate_module.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/gfx/debug_lines.h"
#include "game/mth/random.h"
#include "game/net/buddy_status.h"
#include "game/obj/proxim.h"
#include "game/obj/vehicle_internal.h"
#include "game/obj/vehicle_wheel.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/skater/skater_x87.h"
#include "game/sys/timer.h"
#include "game/ed/park_editor.h"
#include "game/core/code_address.h"
#include "game/gfx/engine.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"

namespace GameObjects {

// The contacts of the colliders with the world (one array for every vehicle), up to the lens flare's globals at
// 0x00696878: 132 of them.
CDrivableModule::Contact g_vehicleContacts[132];
CDrivableModule::Contact* CDrivableModule::Contacts() { return g_vehicleContacts; }

using namespace Vehicle;

namespace {

constexpr uint32_t kPercent = 0x9E497FC6;
constexpr uint32_t kDuration = 0x79A07F3F;
constexpr uint32_t kOffset = 0xA6F5352F;

const float kHalf = 0.5f;                      // 0x006453BC
const float kHundredPi = 314.159271f;          // 0x006466A8 (the wheels' roll wraps at 200 pi by 100 pi)
const float kTwoHundredPi = 628.318542f;       // 0x006466AC
const float kHundredth = 0.00999999978f;       // 0x0064544C

} // namespace

// 0x00435870
CDrivableModule::Wheel* CDrivableModule::Wheel::MakeNew()
{
    feeler.MakeNew();
    return this;
}

// 0x00437CF0
void CDrivableModule::Wheel::Destruct() { feeler.m_entity.Release(); }

// 0x004351C0
void CDrivableModule::UpdateWheelRotations()
{
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        double rotation =
            (static_cast<double>(wheel.spin) - static_cast<double>(wheel.torque) * m_stepLength * kHalf) * m_stepLength +
            wheel.rotation;
        wheel.rotation = static_cast<float>(rotation);
        if (rotation > kTwoHundredPi)
            wheel.rotation = static_cast<float>(rotation - kHundredPi);
        else if (rotation < 0.0)
            wheel.rotation = static_cast<float>(rotation + kHundredPi);
    }
}

// 0x00435250
double CDrivableModule::GetTyreFriction(Wheel* wheel, float slip)
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
int CDrivableModule::GetGear(float spin)
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
void CDrivableModule::UpdateVelocities()
{
    double invMass = m_invMass;
    m_vel.z = static_cast<float>(invMass * m_linearMomentum.z);
    m_vel.w = static_cast<float>(invMass * m_linearMomentum.w);
    m_vel.x = static_cast<float>(invMass * m_linearMomentum.x);
    m_vel.y = static_cast<float>(invMass * m_linearMomentum.y);
    m_vel.w = 0.0f;
    Maths::Vec4 rotvel;
    m_rotvel = m_invInertia.RotateVector(rotvel, m_rotmom);
}

// 0x004355B0
Maths::Vec4* CDrivableModule::GetPointVelocity(Maths::Vec4* out, const Maths::Vec4* offset)
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
void CDrivableModule::AddForceAtPoint(const Maths::Vec4* force, const Maths::Vec4* point, uint32_t)
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
    m_twistForce.x = static_cast<float>(static_cast<double>(tx) + m_twistForce.x);
    m_twistForce.y = static_cast<float>(static_cast<double>(ty) + m_twistForce.y);
    m_twistForce.z = static_cast<float>(tz + m_twistForce.z);
    // (the colour was for a debug line, compiled out)
}

// 0x00435710
void CDrivableModule::AddForceAtOffset(const Maths::Vec4* force, const Maths::Vec4* offset)
{
    m_force.x = static_cast<float>(static_cast<double>(m_force.x) + force->x);
    m_force.y = static_cast<float>(static_cast<double>(force->y) + m_force.y);
    m_force.z = static_cast<float>(static_cast<double>(force->z) + m_force.z);
    m_force.w = static_cast<float>(static_cast<double>(force->w) + m_force.w);
    // The torque: x and y unrounded, z rounded.
    double tx = static_cast<double>(offset->y) * force->z - static_cast<double>(force->y) * offset->z;
    double ty = static_cast<double>(offset->z) * force->x - static_cast<double>(force->z) * offset->x;
    float tz = static_cast<float>(static_cast<double>(force->y) * offset->x - static_cast<double>(offset->y) * force->x);
    m_twistForce.x = static_cast<float>(tx + m_twistForce.x);
    m_twistForce.y = static_cast<float>(ty + m_twistForce.y);
    m_twistForce.z = static_cast<float>(static_cast<double>(tz) + m_twistForce.z);
}

// 0x004357C0
void CDrivableModule::ApplyImpulseAtOffset(const Maths::Vec4* impulse, const Maths::Vec4* offset)
{
    m_linearMomentum.x = static_cast<float>(static_cast<double>(m_linearMomentum.x) + impulse->x);
    m_linearMomentum.y = static_cast<float>(static_cast<double>(impulse->y) + m_linearMomentum.y);
    m_linearMomentum.z = static_cast<float>(static_cast<double>(impulse->z) + m_linearMomentum.z);
    m_linearMomentum.w = static_cast<float>(static_cast<double>(impulse->w) + m_linearMomentum.w);
    double tx = static_cast<double>(offset->y) * impulse->z - static_cast<double>(impulse->y) * offset->z;
    double ty = static_cast<double>(offset->z) * impulse->x - static_cast<double>(impulse->z) * offset->x;
    float tz = static_cast<float>(static_cast<double>(impulse->y) * offset->x - static_cast<double>(offset->y) * impulse->x);
    m_rotmom.x = static_cast<float>(tx + m_rotmom.x);
    m_rotmom.y = static_cast<float>(ty + m_rotmom.y);
    m_rotmom.z = static_cast<float>(static_cast<double>(tz) + m_rotmom.z);
    UpdateVelocities();
}

// 0x00435850
bool CDrivableModule::IsKicked() { return m_kickTime > 0.0f; }

// 0x00435890
void CDrivableModule::KickRandomlyAlong(const Maths::Vec4* along)
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
    float forwardScale = static_cast<float>(Maths::SignedSpread(0.5f) + 1.0);
    float forward = static_cast<float>(Scripts::FetchFloat(0x3DB3FA83) * static_cast<double>(forwardScale));
    float sideScale = Scripts::FetchFloat(0x39E99FEC);
    double side = static_cast<double>(Maths::SignedSpread(1.5f)) * sideScale;
    double sideX = sideways * side;
    float sideY = static_cast<float>(side * 0.0);
    float sideZ = static_cast<float>(dx * side);
    float sideW = static_cast<float>(side);
    float pushX = static_cast<float>(static_cast<double>(dx) * forward + sideX);
    float pushY = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dy) * forward)) + sideY);
    float pushZ = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dz) * forward)) + sideZ);
    float pushW = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dw) * forward)) + sideW);
    float upScale = static_cast<float>(Maths::SignedSpread(0.2f) + 1.0);
    pushY = static_cast<float>(static_cast<double>(pushY) + Scripts::FetchFloat(0x738500FD) * static_cast<double>(upScale));

    // The spin: about the up axis (0x14DDB3EF * +-1.5) and about the sideways one (0x4088E6EC * (1 + +-0.8)).
    float yawScale = Scripts::FetchFloat(0x14DDB3EF);
    float yaw = static_cast<float>(static_cast<double>(Maths::SignedSpread(1.5f)) * yawScale);
    float rollScale = static_cast<float>(Maths::SignedSpread(0.8f) + 1.0);
    double roll = static_cast<double>(Scripts::FetchFloat(0x4088E6EC)) * rollScale;
    double spinX = sideways * roll;
    float spinY = static_cast<float>(roll * 0.0 + yaw);
    float spinZ = static_cast<float>(dx * roll);
    float spinW = static_cast<float>(roll);

    // Into the momenta: the push times the mass, the spin times the first moment of inertia (for every axis).
    double mass = 1.0 / m_invMass;
    float momY = static_cast<float>(pushY * mass), momZ = static_cast<float>(pushZ * mass);
    float momW = static_cast<float>(pushW * mass);
    m_linearMomentum.x = static_cast<float>(pushX * mass + m_linearMomentum.x);
    m_linearMomentum.y = static_cast<float>(static_cast<double>(momY) + m_linearMomentum.y);
    m_linearMomentum.z = static_cast<float>(static_cast<double>(momZ) + m_linearMomentum.z);
    m_linearMomentum.w = static_cast<float>(static_cast<double>(momW) + m_linearMomentum.w);
    float moment = static_cast<float>(1.0 / m_invMoments[0]);
    float turnZ = static_cast<float>(static_cast<double>(spinZ) * moment);
    float rotW = static_cast<float>(static_cast<double>(spinW) * moment);
    m_rotmom.x = static_cast<float>(spinX * moment + m_rotmom.x);
    m_rotmom.y = static_cast<float>(static_cast<double>(spinY) * moment + m_rotmom.y);
    m_rotmom.z = static_cast<float>(static_cast<double>(turnZ) + m_rotmom.z);
    m_rotmom.w = static_cast<float>(static_cast<double>(rotW) + m_rotmom.w);
    UpdateVelocities();
    m_kickTime = Scripts::FetchFloat(0x771922A6);
    m_awake = 1;
    m_stillFrames = 0;
}

// 0x004354C0 (cdecl)
Maths::Vec4* QuatProduct(Maths::Vec4* out, const Maths::Vec4* a, const Maths::Vec4* b)
{
    Maths::Vec4 product = *a;
    QuatMultiply(&product, b);
    *out = product;
    return out;
}

// --- script member functions ---

// 0x00437A60
bool CDrivableModule::Kick(Scripts::CStruct* params, Scripts::CScript*)
{
    m_awake = 1;
    m_stillFrames = 0;
    // (Without linear or angular the original uses whatever was on its stack.)
    Maths::Vec4 linear, angular, turned;
    params->FetchVec("linear", reinterpret_cast<Scripts::VectorValue*>(&linear), 0);
    params->FetchVec("angular", reinterpret_cast<Scripts::VectorValue*>(&angular), 0);
    linear = m_basisFrame.RotateVector(turned, linear);
    angular = m_basisFrame.RotateVector(turned, angular);
    double mass = 1.0 / m_invMass;
    float momY = static_cast<float>(linear.y * mass), momZ = static_cast<float>(linear.z * mass);
    float momW = static_cast<float>(linear.w * mass);
    m_linearMomentum.x = static_cast<float>(linear.x * mass + m_linearMomentum.x);
    m_linearMomentum.y = static_cast<float>(static_cast<double>(momY) + m_linearMomentum.y);
    m_linearMomentum.z = static_cast<float>(static_cast<double>(momZ) + m_linearMomentum.z);
    m_linearMomentum.w = static_cast<float>(static_cast<double>(momW) + m_linearMomentum.w);
    double moment = 1.0 / m_invMoments[0];
    float spinAboutY = static_cast<float>(angular.y * moment), turnZ = static_cast<float>(angular.z * moment);
    float rotW = static_cast<float>(angular.w * moment);
    m_rotmom.x = static_cast<float>(angular.x * moment + m_rotmom.x);
    m_rotmom.y = static_cast<float>(static_cast<double>(spinAboutY) + m_rotmom.y);
    m_rotmom.z = static_cast<float>(static_cast<double>(turnZ) + m_rotmom.z);
    m_rotmom.w = static_cast<float>(static_cast<double>(rotW) + m_rotmom.w);
    UpdateVelocities();
    return true;
}

// 0x004353F0
bool CDrivableModule::Wake(Scripts::CStruct*, Scripts::CScript*)
{
    m_awake = 1;
    m_stillFrames = 0;
    return true;
}

// 0x00435410
bool CDrivableModule::AdjustGravity(Scripts::CStruct* params, Scripts::CScript*)
{
    params->FetchFloat(kPercent, &m_gravityScale, 1);
    m_gravityScale = static_cast<float>(static_cast<double>(m_gravityScale) * kHundredth);
    params->FetchFloat(kDuration, &m_gravityTime, 1);
    DebugOut::Printf(kDebugFormat, kDebugFile, 0xD78, "Initiating vehicle gravity override"); // "Initiating vehicle gravity override"
    return true;
}

// 0x00435480
bool CDrivableModule::HandbrakeActive(Scripts::CStruct*, Scripts::CScript*) { return m_handbrakeActive; }

// 0x00435490
bool CDrivableModule::AllWheelsAreInContact(Scripts::CStruct*, Scripts::CScript*)
{
    return m_wheelsInContact == m_wheelCount;
}

// 0x00437BC0
bool CDrivableModule::KickRandomly(Scripts::CStruct* params, Scripts::CScript*)
{
    Maths::Vec4 along;
    params->FetchVec(kOffset, reinterpret_cast<Scripts::VectorValue*>(&along), 1);
    KickRandomlyAlong(&along);
    return true;
}

// 0x004354B0
bool CDrivableModule::IsSkaterVisible(Scripts::CStruct*, Scripts::CScript*) { return m_skaterVisible; }

// --- the component's life ---

namespace {

extern void** const kVtable;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTrigger = 0xE594F0A2;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kParkEditor = 0xD443A2BC;      // the park editor's level (its +0x40)
constexpr uint32_t kSkater = 0x2ED67657;          // a skater is shown driving
const float kToRadians = std::bit_cast<float>(0x3C8EFA35u);    // 0x00645464
const float kFeet = 12.0f;                                    // 0x0064546C
const float kRpmToRadians = std::bit_cast<float>(0x3DD67770u); // 0x006467A4: 2 pi / 60
const float kThird = std::bit_cast<float>(0x3EAAAAABu);        // 0x006463D8
const float kCameraBack = 48.0f;                              // 0x006461CC
const float kQuarter = 0.25f;                                 // 0x006467AC
const float kAbove = 24.0f;                                   // 0x00645EE8
const float kBelow = 240.0f;                                  // 0x00646558
const float kMaxStep = std::bit_cast<float>(0x3D088889u);      // 0x00646430: 1/30
const float kStepsPerSecond = std::bit_cast<float>(0x426FFFFFu); // 0x006467D0: 59.999996
const float kRimHalfWidth = std::bit_cast<float>(0x40766666u); // 0x006467CC: 3.85
const float kSpokeAngle = std::bit_cast<float>(0x3F060A90u);   // 0x006467C8: pi / 6
const double kTwoTo32 = 4294967296.0;                         // 0x006453B4

// A count as the original loads it (FILD, then 2^32 added when negative: unsigned).
double Unsigned(int value) { return value < 0 ? value + kTwoTo32 : value; }

// 0x00434F20 (the previous range; stdcall): a wheel's parameters from a structure.
void ReadWheel(CDrivableModule::Wheel* wheel, Scripts::CStruct* params)
{
    ReadWheelSettings(reinterpret_cast<uint8_t*>(wheel), params);
}
// The skate module's local skater (0x00566520) and a restart node's name (0x00566100).
CGameEntity* LocalSkater()
{
    return static_cast<CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
}
uint32_t SkateRestart(int index)
{
    return GameModules::Skate::Sole()->SkipToRestartPoint(static_cast<uint32_t>(index));
}
// The model's geometry (0x00493040 on the model's +0x208): its matrix at +0x60, the wheels' places (z up) at +0xE0 +
// 0x50 each.
const uint8_t* ModelGeometry(CObjectModule* model)
{
    void* instance = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x208);
    return reinterpret_cast<const uint8_t*>(static_cast<Engine3D::CRenderModel*>(instance)->GetAssetHierarchy());
}
const float* WheelPlace(const uint8_t* geometry, int i) { return reinterpret_cast<const float*>(geometry + 0xE0 + i * 0x50); }
// The skeleton (0x005480F0): its bone matrices (0x005480C0) and how many (0x004A2BA0).
void* SkeletonOf(CObjectModule* skeleton)
{
    return reinterpret_cast<void*>(reinterpret_cast<OnlinePlay::FoldedGetters*>(skeleton)->Word18());
}
// The viewport's camera (0x004A09C0; null if none), its position (0x00485A00) and matrix (0x00485A10).
void* ViewportCamera(int viewport) { return Engine3D::FetchLiveCamera(viewport); }
// 0x0041A730 (not rewritten yet): the rotation of a node's angles.
void MatrixFromAngles(Maths::Mat4* matrix, const Maths::Vec4* angles)
{
    matrix->InitFromAngles(*angles);
}

void ClearToW1(Maths::Vec4& v) { v = {0.0f, 0.0f, 0.0f, 1.0f}; }

} // namespace

// 0x0043AA30
CObjectModule* __cdecl CDrivableModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CDrivableModule));
    return memory ? static_cast<CDrivableModule*>(memory)->MakeNew() : nullptr;
}

// 0x00437C00
CDrivableModule* CDrivableModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    ClearToW1(m_orientation);
    // (the colliders' vectors' constructors do nothing: 0x005902E0)
    m_collCache.MakeNew();
    mp_input = nullptr;
    m_debugMode = 0;
    m_passive = false;
    m_displaySteering = 0.0f;
    m_kindTag = kType;
    ClearToW1(m_skaterPos);
    m_wheels = nullptr;
    m_contactCount = 0;
    m_kickTime = 0.0f;
    m_stillFrames = 0;
    m_soundsName = 0x1CA1FF20;
    m_awake = 1;
    return this;
}

// 0x0043B990
void CDrivableModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_wheels) {
        // (as `delete[]`: the count before the array, the wheels destroyed last to first)
        int* block = reinterpret_cast<int*>(m_wheels) - 1;
        for (int i = *block - 1; i >= 0; --i)
            m_wheels[i].Destruct();
        MemSys::Hub::Pull()->Dealloc(block);
    }
    m_collCache.Destruct();
    CObjectModule::DestroyBody();
}

// 0x0043C1A0
CDrivableModule* CDrivableModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00437D10
void CDrivableModule::ConfigureFromData(Scripts::CStruct* params)
{
    // debug / 0xE6B5FCD9 / 0x751DA48B: the debug drawing's next mode, the previous one, off.
    if (params->HasBareFlag(0x935AB858))
        m_debugMode = (m_debugMode + 1) % 3;
    else if (params->HasBareFlag(0xE6B5FCD9))
        m_debugMode = (m_debugMode + 2) % 3;
    else if (params->HasBareFlag(0x751DA48B))
        m_debugMode = 0;
    if (params->HasEntryNamed(0x93346B8C)) {
        // (read into the original's argument slot: missing, the height is that pointer's bits)
        float height = std::bit_cast<float>(reinterpret_cast<uint32_t>(params));
        params->FetchFloat(0x93346B8C, &height, 0);
        m_centerOfMass = {0.0f, height, 0.0f, 1.0f};
    }
    if (params->HasEntryNamed(0x93FCA499)) { // mass
        params->FetchFloat(0x93FCA499, &m_invMass, 0);
        m_invMass = static_cast<float>(static_cast<double>(kGravity) / m_invMass);
    }
    if (params->HasEntryNamed(0x8C6473EC)) { // moment_of_inertia (its w into +0x168)
        params->FetchVec(0x8C6473EC, reinterpret_cast<Scripts::VectorValue*>(m_invMoments), 0);
        for (float& moment : m_invMoments)
            moment = static_cast<float>(static_cast<double>(kGravity) / moment);
    }
    params->FetchFloat(0x79C3B862, &m_restitution, 0);
    params->FetchFloat(0xF476273C, &m_dragFactor, 0);
    params->FetchFloat(0x4A23A53C, &m_tippedFriction, 0);
    params->FetchFloat(0x01DD4890, &m_depthCorrection, 0);
    params->FetchFloat(0xB9F026ED, &m_wallSteering, 0);
    params->FetchFloat(0x8C14709E, &m_alignSpeed, 0);
    params->FetchFloat(0x2C91E49B, &m_alignTime, 0);
    params->FetchFloat(0x280F0E0B, &m_alignRate, 0);
    m_alignWhenSlow = params->HasBareFlag(0x5341806A);
    if (params->HasEntryNamed(0xC1E5ABDD)) { // max_steering_angle (degrees)
        params->FetchFloat(0xC1E5ABDD, &m_maxSteeringAngle, 0);
        m_maxSteeringAngle = static_cast<float>(static_cast<double>(m_maxSteeringAngle) * kToRadians);
    }
    params->FetchFloat(0x386837DB, &m_yawDamping, 0);
    params->FetchFloat(0x469CF79A, &m_spinDamping, 0);
    m_canDismount = params->HasBareFlag(0xC2A136CC);
    m_noHandbrake = params->HasBareFlag(0xAD6C0A46);

    // colliders: each three points, a corner and the ends of its two edges (the edges kept relative to the corner -
    // their w less the corner's, which nothing sets). The points' average distance from the body's origin, less 48,
    // goes to the skater (CRider 0x005802C0).
    if (params->HasEntryNamed(0xFC8C4AC6)) {
        Scripts::CArray* colliders;
        params->FetchList(0xFC8C4AC6, &colliders, 1);
        int count = static_cast<int>(colliders->m_extent);
        float sum = 0.0f;
        for (int k = count - 1; k >= 0; --k) {
            auto* points = reinterpret_cast<Scripts::CArray*>(colliders->FetchPart(static_cast<uint32_t>(k)));
            Collider& c = m_colliders[k];
            auto point = [points](int i) {
                return reinterpret_cast<const float*>(points->FetchPart(static_cast<uint32_t>(i))) + 1; // (x, y, z)
            };
            const float* p = point(0);
            c.corner.x = p[0];
            c.corner.y = p[1];
            c.corner.z = p[2];
            for (int e = 0; e < 2; ++e) {
                p = point(1 + e);
                Maths::Vec4& edge = c.edges[e];
                edge.x = p[0];
                edge.y = p[1];
                edge.z = p[2];
                edge.x = static_cast<float>(static_cast<double>(edge.x) - c.corner.x);
                edge.y = static_cast<float>(static_cast<double>(edge.y) - c.corner.y);
                edge.z = static_cast<float>(static_cast<double>(edge.z) - c.corner.z);
                edge.w = static_cast<float>(static_cast<double>(edge.w) - c.corner.w);
            }
            auto lengthSquared = [](double x, double y, double z) { return (x * x + y * y) + z * z; };
            const Maths::Vec4& o = c.corner;
            const Maths::Vec4& a = c.edges[0];
            const Maths::Vec4& b = c.edges[1];
            double s0 = lengthSquared(o.x, o.y, o.z);
            double s1 = lengthSquared(static_cast<double>(o.x) + a.x, static_cast<double>(a.y) + o.y, static_cast<double>(a.z) + o.z);
            float s2 = static_cast<float>(
                lengthSquared(static_cast<double>(o.x) + b.x, static_cast<double>(b.y) + o.y, static_cast<double>(b.z) + o.z));
            sum = static_cast<float>(((std::sqrt(s0) + sum) + std::sqrt(s1)) + std::sqrt(static_cast<double>(s2)));
            c.lengths[0] = static_cast<float>(std::sqrt(lengthSquared(a.y, a.z, a.x)));
            c.lengths[1] = static_cast<float>(std::sqrt(lengthSquared(b.y, b.z, b.x)));
        }
        float distance = static_cast<float>(static_cast<double>(sum) * kThird / count - kCameraBack);
        reinterpret_cast<CRider*>(LocalSkater())->Set42BD60(distance);
    }

    // engine: drive_torque and 0xB2268648 in foot-pounds, upshift_rpm, differential_ratio, reverse_torque_ratio,
    // gear_ratios (as many as there are - room for six).
    if (params->HasEntryNamed(0x1757E572)) {
        Scripts::CStruct* engine;
        params->ReadSubStruct(0x1757E572, &engine, 1);
        if (engine->HasEntryNamed(0x9AA9FAEE)) {
            engine->FetchFloat(0x9AA9FAEE, &m_driveTorque, 0);
            m_driveTorque = static_cast<float>(static_cast<double>(m_driveTorque) * kFeet);
        }
        if (engine->HasEntryNamed(0xB2268648)) {
            engine->FetchFloat(0xB2268648, &m_engineBraking, 0);
            m_engineBraking = static_cast<float>(static_cast<double>(m_engineBraking) * kFeet);
        }
        if (engine->HasEntryNamed(0x33921583)) {
            engine->FetchFloat(0x33921583, &m_upshiftSpeed, 0);
            m_upshiftSpeed = static_cast<float>(static_cast<double>(m_upshiftSpeed) * kRpmToRadians);
        }
        engine->FetchFloat(0x94EA6601, &m_differentialRatio, 0);
        engine->FetchFloat(0x82BCBB10, &m_reverseRatio, 0);
        if (engine->HasEntryNamed(0xC023FA75)) {
            Scripts::CArray* ratios;
            engine->FetchList(0xC023FA75, &ratios, 1);
            m_gearCount = static_cast<int>(ratios->m_extent);
            for (int i = 0; i < m_gearCount; ++i)
                m_gearRatios[i] = ratios->FetchFloat(static_cast<uint32_t>(i));
        }
    }

    // Wheels: made the first time (as `new[]`: the count before them), each read from its structure; all_wheels
    // for every one.
    if (params->HasEntryNamed(0xB3F8557E)) {
        Scripts::CArray* wheels = nullptr;
        params->FetchList(0xB3F8557E, &wheels, 1);
        int count = static_cast<int>(wheels->m_extent);
        m_wheelCount = count;
        if (!m_wheels) {
            if (!MemSys::g_managerReady)
                MemSys::EnsureManager();
            auto* block = static_cast<int*>(
                MemSys::Hub::Pull()->Carve(static_cast<uint32_t>(count) * sizeof(Wheel) + 4, 1, nullptr));
            Wheel* array = nullptr;
            if (block) {
                *block = count;
                array = reinterpret_cast<Wheel*>(block + 1);
                for (int i = 0; i < count; ++i)
                    array[i].MakeNew();
            }
            m_wheels = array;
        }
        for (int i = m_wheelCount - 1; i >= 0; --i)
            ReadWheel(&m_wheels[i], reinterpret_cast<Scripts::CStruct*>(wheels->FetchPart(static_cast<uint32_t>(i))));
    }
    if (params->HasEntryNamed(0x371BADFF)) {
        Scripts::CStruct* all;
        params->ReadSubStruct(0x371BADFF, &all, 1);
        for (int i = m_wheelCount - 1; i >= 0; --i)
            ReadWheel(&m_wheels[i], all);
    }
    m_driveWheelCount = 0;
    for (int i = m_wheelCount - 1; i >= 0; --i)
        if (m_wheels[i].drive)
            ++m_driveWheelCount;

    // The orientation from the object's matrix (or Save's Orientation: the object's matrix from it); the position
    // from the object's (or Pos: the object's too).
    Maths::Vec4 quat;
    MatrixToQuat(&quat, &m_entity->m_basisFrame);
    m_orientation = quat;
    QuatNormalize(&m_orientation, 1.0f);
    if (params->HasEntryNamed(0xAA99C521)) {
        Maths::Vec4 saved{0.0f, 0.0f, 0.0f, 1.0f};
        params->FetchVec(0xC97F3AA9, reinterpret_cast<Scripts::VectorValue*>(&saved), 0);
        m_orientation.x = saved.x;
        m_orientation.y = saved.y;
        m_orientation.z = saved.z;
        QuatNormalize(&m_orientation, 1.0f);
        QuatToMatrix(&m_orientation, &m_basisFrame);
        ClearToW1(m_basisFrame.row[3]);
        m_entity->m_basisFrame = m_basisFrame;
        m_entity->m_displayMatrix = m_basisFrame;
    }
    m_atPoint = m_entity->m_atPoint;
    if (params->HasEntryNamed(0x7F261953)) {
        params->FetchVec(0x7F261953, reinterpret_cast<Scripts::VectorValue*>(&m_atPoint), 0);
        m_entity->m_atPoint = m_atPoint;
    }
    m_skaterVisible = m_skaterVisible || params->HasBareFlag(kSkater);
    if (params->HasBareFlag(kSkater)) {
        params->FetchVec(0xEC86EF7A, reinterpret_cast<Scripts::VectorValue*>(&m_skaterPos), 1); // skater_pos
        params->ReadHashValue(0xDA75A33E, &m_skaterAnim, 1);                                       // skater_anim
    }
    params->ReadHashValue(0x0EDCF90E, &m_soundsName, 0); // sounds

    // At rest: no momenta or forces; the wheels in the air at their rest extension, each with an even share of the
    // weight.
    ClearToW1(m_linearMomentum);
    ClearToW1(m_rotmom);
    ClearToW1(m_force);
    ClearToW1(m_twistForce);
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        wheel.y = wheel.restY;
        wheel.state = kWheelInAir;
        wheel.rotation = 0.0f;
        wheel.spin = 0.0f;
        wheel.steering = 0.0f;
        wheel.displaySteering = 0.0f;
        wheel.torque = 0.0f;
        for (int k = 9; k >= 0; --k)
            wheel.loadHistory[k] = static_cast<float>(static_cast<double>(kGravity) / m_invMass / Unsigned(m_wheelCount));
    }
    m_loadIndex = 0;
    m_gravityTime = 0.0f;
    m_gravityScale = 1.0f;
    m_righting = false;
    m_boneRig = m_entity->FetchModule(kSkeleton);
}

// 0x0043BA20
void CDrivableModule::Seal()
{
    mp_input = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kInput));
    m_meshInstance = m_entity->FetchModule(kModel);
    const uint8_t* geometry = ModelGeometry(m_entity->FetchModule(kModel));
    // The wheels' places from the model (z up there; always four).
    for (int i = 3; i >= 0; --i) {
        const float* place = WheelPlace(geometry, i);
        m_wheels[i].offset = {-place[0], place[2], -place[1], 1.0f};
    }
    Maths::Mat4 bone = *reinterpret_cast<const Maths::Mat4*>(geometry + 0x60);
    m_skaterBone = {-bone.row[3].x, bone.row[3].z, -bone.row[3].y, 1.0f};
    // The wheelbase (a left wheel to a fixed one) and the track (left to right), from the lowest-numbered wheel of
    // each kind (-1 when there's none - read before the first, as the original).
    int fixed = -1, left = -1, right = -1;
    for (int i = 3; i >= 0; --i) {
        switch (m_wheels[i].steeringSide) {
        case 0: fixed = i; break;
        case 1: left = i; break;
        case 2: right = i; break;
        default: break;
        }
    }
    m_wheelbase = static_cast<float>(std::fabs(static_cast<double>(WheelPlace(geometry, left)[1]) - WheelPlace(geometry, fixed)[1]));
    m_track = static_cast<float>(std::fabs(static_cast<double>(WheelPlace(geometry, left)[0]) - WheelPlace(geometry, right)[0]));
    // The suspension: the wheel's place raised by its diameter; its rest extension down from there, sagged by its
    // share of the weight.
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        Wheel& wheel = m_wheels[i];
        double y = wheel.offset.y;
        double raised = (static_cast<double>(wheel.radius) + wheel.radius) + y;
        wheel.offset.y = static_cast<float>(raised);
        wheel.restY = static_cast<float>(
            (y - raised) - static_cast<double>(kGravity) / (Unsigned(m_wheelCount) * wheel.springRate * m_invMass));
    }
    // The colliders' lowest point.
    float lowest = m_colliders[0].corner.y;
    for (int i = 1; i >= 0; --i) {
        const Collider& c = m_colliders[i];
        if (c.corner.y < lowest)
            lowest = c.corner.y;
        for (const Maths::Vec4& edge : c.edges) {
            double end = static_cast<double>(edge.y) + c.corner.y;
            if (end < lowest)
                lowest = static_cast<float>(end);
        }
    }
    // The skater's components (hidden unless shown driving).
    m_riderRef = LocalSkater();
    mp_skaterCorePhysics = m_riderRef->FetchModule(kSkaterCorePhysics);
    mp_trigger = m_riderRef->FetchModule(kTrigger);
    if (!m_skaterVisible)
        m_riderRef->Conceal(true);
    else
        mp_animBlender = m_riderRef->FetchModule(kAnimBlender);
    // Everything made relative to the wheels' centre (x, z) and the colliders' lowest point.
    double sumX = 0.0, sumZ = 0.0;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        sumX += m_wheels[i].offset.x;
        sumZ += m_wheels[i].offset.z;
    }
    double share = 1.0 / Unsigned(m_wheelCount);
    float centreX = static_cast<float>(sumX * share);
    double centreZ = sumZ * share;
    auto recentre = [centreX, centreZ, lowest](Maths::Vec4& v) {
        v.x = static_cast<float>(static_cast<double>(v.x) - centreX);
        v.y = static_cast<float>(static_cast<double>(v.y) - lowest);
        v.z = static_cast<float>(v.z - centreZ);
    };
    for (int i = m_wheelCount - 1; i >= 0; --i)
        recentre(m_wheels[i].offset);
    for (int i = 1; i >= 0; --i)
        recentre(m_colliders[i].corner);
    recentre(m_skaterBone);
    recentre(m_skaterPos);
    UpdateDependents();
    m_entity->m_atPoint = m_atPoint;
    m_entity->m_vel = m_vel;
    m_entity->m_basisFrame = m_basisFrame;
    m_entity->m_displayMatrix = m_entity->m_basisFrame;
    RefreshBoneRig();
}

// 0x0043C1D0
void CDrivableModule::TickFrame()
{
    m_cutShort = false;
    if (!m_passive) {
        ReadControls();
    } else {
        m_brake = false;
        m_handbrake = false;
        m_throttle = false;
        m_reverse = false;
        m_steering = 0.0f;
    }
    if (m_awake == 0) {
        // Asleep: only the steering, the wheels' roll (stopped by the brake), the model and the skater.
        m_entity->m_displayMatrix = m_entity->m_basisFrame;
        UpdateSteering();
        if (m_brake)
            for (int i = m_wheelCount - 1; i >= 0; --i)
                m_wheels[i].spin = 0.0f;
        UpdateWheelRotations();
        RefreshBoneRig();
        UpdateSkater();
        return;
    }
    float frame = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    if (m_kickTime > 0.0f)
        m_kickTime = static_cast<float>(static_cast<double>(m_kickTime) - frame);
    if (!(m_gravityTime == 0.0f)) {
        // Vehicle_AdjustGravity's override: over when its time is up or two wheels touch.
        double left = static_cast<double>(m_gravityTime) - frame;
        m_gravityTime = static_cast<float>(left);
        if (left <= 0.0 || static_cast<uint32_t>(m_wheelsInContact) > 1) {
            DebugOut::Printf(kDebugFormat, kDebugFile, 0x2A0, "Ending vehicle gravity override"); // "Ending vehicle gravity override"
            m_gravityTime = 0.0f;
            m_gravityScale = 1.0f;
        }
    }
    // Equal steps of at most 1/30 s - six at most.
    int steps = 1;
    if (!(frame >= kMaxStep)) {
        m_stepLength = frame;
    } else {
        steps = static_cast<int>(static_cast<int64_t>(std::ceil(static_cast<double>(frame) * kStepsPerSecond)));
        if (steps > 6)
            steps = 6;
        m_stepLength = static_cast<float>(frame / static_cast<double>(steps));
    }
    while (steps != 0) {
        --steps;
        Integrate();
        ClearToW1(m_force);
        ClearToW1(m_twistForce);
        for (int i = m_wheelCount - 1; i >= 0; --i)
            m_wheels[i].torque = 0.0f;
        if (!m_passive) {
            Collide();
            if (m_cutShort || (m_entity->m_objectFlags & OBJECT_FLAG_DEAD))
                return;
        }
        UpdateWheelContacts();
        if (m_cutShort || (m_entity->m_objectFlags & OBJECT_FLAG_DEAD))
            return;
        UpdateSteering();
        ApplyDamping();
        ApplyWheelForces();
        if (m_wheelsInContact != 0)
            m_airTime = 0.0f;
        else
            m_airTime = static_cast<float>(static_cast<double>(m_stepLength) + m_airTime);
        if (m_wheelsInContact == 0 && !(m_collisionImpulse > 0.0f))
            m_freeFallTime = static_cast<float>(static_cast<double>(m_stepLength) + m_freeFallTime);
        else
            m_freeFallTime = 0.0f;
    }
    if (m_passive)
        return;
    DebugDraw();
    m_entity->m_atPoint = m_atPoint;
    m_entity->m_vel = m_vel;
    m_entity->m_basisFrame = m_basisFrame;
    m_entity->m_displayMatrix = m_entity->m_basisFrame;
    CheckSleep();
    RefreshBoneRig();
    UpdateSkater();
}

// 0x0043A070
void CDrivableModule::RefreshBoneRig()
{
    if (Engine3D::g_skipRender) // (rendering off)
        return;
    // (the skeleton's +0x04, through the getter folded at 0x005480C0)
    auto* bones = reinterpret_cast<Maths::Mat4*>(static_cast<OnlinePlay::FoldedGetters*>(SkeletonOf(m_boneRig))->Word04());
    int count = reinterpret_cast<int(__thiscall*)(void*)>(CodeAddress(&Gfx2D::CRig::GetNumBones))(SkeletonOf(m_boneRig));
    // Bone 0 the body (left as nothing), 1 the skater's place, 2 on the wheels: steered and rolled, at their place
    // with the suspension's extension, but no lower than maxDrawnY. In debug mode 2 all of them nothing.
    for (int b = count - 1; b >= 0; --b) {
        Maths::Mat4& bone = bones[b];
        std::memset(&bone, 0, sizeof(bone));
        if (m_debugMode == 2)
            continue;
        if (b == 0)
            return;
        if (b == 1) {
            bone.row[0].x = 1.0f;
            bone.row[1].z = -1.0f;
            bone.row[2].y = 1.0f;
            bone.row[3] = m_skaterBone;
            continue;
        }
        const Wheel& wheel = m_wheels[b - 2];
        bone.row[0].x = -1.0f;
        bone.row[1].z = 1.0f;
        bone.row[2].y = 1.0f;
        Maths::Mat4 turn;
        MatrixMultiply(&bone, &Maths::MakeRollMatrix(turn, wheel.displaySteering), &bone);
        MatrixMultiply(&bone, &Maths::MakePitchMatrix(turn, -wheel.rotation), &bone);
        bone.row[3] = wheel.offset;
        float drop = wheel.y > wheel.maxDrawnY ? wheel.maxDrawnY : wheel.y;
        bone.row[3].y = static_cast<float>(static_cast<double>(drop) + bone.row[3].y);
        bone.row[3].w = 1.0f;
    }
}

// 0x0043A2C0
Maths::Vec4* CDrivableModule::GetWheelRimPoint(Maths::Vec4* out, const Wheel* wheel, int spoke, bool outer)
{
    Maths::Vec4 point;
    point.x = static_cast<float>((outer ? 1.0f : -1.0f) * static_cast<double>(kRimHalfWidth));
    double angle = static_cast<double>(spoke) * kSpokeAngle + wheel->rotation;
    point.y = static_cast<float>(Skater::X87Cos(angle) * wheel->radius);
    point.z = static_cast<float>(Skater::X87Sin(angle) * wheel->radius);
    point.w = 1.0f;
    Maths::Vec4 up{0.0f, 1.0f, 0.0f, 1.0f};
    Maths::Mat4 steer;
    Maths::BuildAxisAngleMatrix(steer, up, wheel->steering);
    Maths::Vec4 turned;
    steer.RotateVector(turned, point);
    Maths::Vec4 placed{static_cast<float>(static_cast<double>(turned.x) + wheel->offset.x),
                       static_cast<float>((static_cast<double>(turned.y) + wheel->y) + wheel->offset.y),
                       static_cast<float>(static_cast<double>(turned.z) + wheel->offset.z),
                       static_cast<float>(static_cast<double>(turned.w) + wheel->offset.w)};
    Maths::Vec4 world;
    m_basisFrame.RotateVector(world, placed);
    out->x = static_cast<float>(static_cast<double>(m_atPoint.x) + world.x);
    out->y = static_cast<float>(static_cast<double>(m_atPoint.y) + world.y);
    out->z = static_cast<float>(static_cast<double>(m_atPoint.z) + world.z);
    out->w = static_cast<float>(static_cast<double>(m_atPoint.w) + world.w);
    return out;
}

// 0x0043A400
void CDrivableModule::DebugDraw()
{
    // Each wheel's tyre (two rims of twelve spokes and the tread between them, coloured by its grip state) and its
    // axle; then (two spheres, compiled out as well) the centre of mass and the position. Nothing else is touched.
    if (!m_debugMode)
        return;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        const Wheel& wheel = m_wheels[i];
        uint32_t colour;
        switch (wheel.state) {
        case kWheelStaticSlow: colour = 0xFF00FF00; break;
        case kWheelStatic: colour = 0xFF000000; break;
        case kWheelStaticToDynamic: colour = 0xFF00C8FF; break;
        case kWheelDynamic: colour = 0xFF0000FF; break;
        case kWheelHandbrakeThrottle: colour = 0xFF960096; break;
        case kWheelHandbrakeLocked: colour = 0xFF969600; break;
        default: colour = 0xFFFFFFFF; break;
        }
        for (int spoke = 11; spoke >= 0; --spoke) {
            Maths::Vec4 a, b;
            for (bool outer : {true, false}) {
                GetWheelRimPoint(&a, &wheel, spoke, outer);
                GetWheelRimPoint(&b, &wheel, spoke + 1, outer);
                Gfx2D::PushDiagLine(&a, &b, colour, colour, 1);
            }
            GetWheelRimPoint(&a, &wheel, spoke, true);
            GetWheelRimPoint(&b, &wheel, spoke, false);
            Gfx2D::PushDiagLine(&a, &b, colour, colour, 1);
            // The axle: 240 either side of the wheel along its steered x, in the world.
            Maths::Vec4 up{0.0f, 1.0f, 0.0f, 1.0f};
            Maths::Mat4 steer;
            Maths::BuildAxisAngleMatrix(steer, up, wheel.steering);
            Maths::Vec4 ends[2];
            for (int e = 0; e < 2; ++e) {
                float side = e ? kBelow : -kBelow;
                Maths::Vec4 local{steer.row[0].x * side + wheel.offset.x, steer.row[0].y * side + wheel.offset.y + wheel.y,
                                  steer.row[0].z * side + wheel.offset.z, 1.0f};
                Maths::Vec4 world;
                m_basisFrame.RotateVector(world, local);
                ends[e] = {world.x + m_atPoint.x, world.y + m_atPoint.y, world.z + m_atPoint.z, world.w + m_atPoint.w};
            }
            Gfx2D::PushDiagLine(&ends[0], &ends[1], 0xFF000000, 0xFF000000, 1);
        }
    }
}

// 0x0043AA90 (not a Ghidra function)
void CDrivableModule::GlideToNode(Scripts::CStruct* node)
{
    Maths::Vec4 pos, angles;
    LevelNodes::FetchPlace(node, &pos);
    LevelNodes::ReadTurnAngles(node, &angles);
    Maths::Mat4 matrix;
    MatrixFromAngles(&matrix, &angles);
    // How far the body's origin sits above the ground: the wheels' rest heights (sagged by their share of the
    // weight) less their radii, averaged (always four wheels).
    double count = Unsigned(m_wheelCount);
    double height = 0.0;
    for (int i = 3; i >= 0; --i) {
        const Wheel& wheel = m_wheels[i];
        double rest = static_cast<double>(kGravity) / (static_cast<double>(wheel.springRate) * m_invMass * count) +
                      wheel.offset.y + wheel.restY;
        height = (height + rest) - wheel.radius;
    }
    float above = static_cast<float>(height * kQuarter);
    // The ground under the node (from 24 above to 240 below).
    Maths::Vec4 start{pos.x, static_cast<float>(static_cast<double>(pos.y) + kAbove), pos.z,
                      static_cast<float>(static_cast<double>(pos.w) + 1.0)};
    Maths::Vec4 end{pos.x, static_cast<float>(static_cast<double>(pos.y) - kBelow), pos.z, start.w};
    Engine3D::CProbeRay feeler;
    feeler.MakeNew(&start, &end);
    if (feeler.GetHitShape(false, false))
        pos.y = static_cast<float>(static_cast<double>(feeler.m_contactPos.y) - above);
    // The node marks the fixed axle (the first fixed wheel from the last): back along the node's facing by its place.
    int fixed = -1;
    for (int i = 3; i >= 0; --i) {
        if (m_wheels[i].steeringSide == 0) {
            fixed = i;
            break;
        }
    }
    double back = m_wheels[fixed].offset.z;
    const Maths::Vec4& at = matrix.row[2];
    m_atPoint.x = pos.x = static_cast<float>(pos.x - at.x * back);
    m_atPoint.y = pos.y = static_cast<float>(pos.y - at.y * back);
    m_atPoint.z = pos.z = static_cast<float>(pos.z - at.z * back);
    m_atPoint.w = static_cast<float>(pos.w - at.w * back);
    Maths::Vec4 quat;
    MatrixToQuat(&quat, &matrix);
    m_orientation = quat;
    ClearToW1(m_linearMomentum);
    ClearToW1(m_rotmom);
    // Settled by sixty updates without input, collision or debug drawing, the wheels kept from turning.
    m_passive = true;
    for (int n = 60; n != 0; --n) {
        for (int i = m_wheelCount - 1; i >= 0; --i)
            m_wheels[i].spin = 0.0f;
        CObjectModule::TickFrame();
    }
    m_passive = false;
    ClearToW1(m_linearMomentum);
    ClearToW1(m_rotmom);
    m_cutShort = true;
    m_awake = 0;
    m_entity->m_atPoint = m_atPoint;
    m_entity->m_vel = m_vel;
    m_entity->m_basisFrame = m_basisFrame;
    m_entity->m_displayMatrix = m_entity->m_basisFrame;
    RefreshBoneRig();
    UpdateSkater();
    m_entity->MarkJumpedAhead(true);
    m_riderRef->MarkJumpedAhead(true);
}

// 0x0043B670 (not a Ghidra function)
bool CDrivableModule::TeleportToSpawn(Scripts::CStruct* params, Scripts::CScript* script)
{
    // The node named; else the first restart linked from the script's node; else (outside the park editor) the skate
    // module's first restart.
    uint32_t name;
    if (params->ReadHashValue(0u, &name, 0)) {
        GlideToNode(LevelNodes::FetchVertex(name, true));
        return true;
    }
    uint32_t from = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(script) + 0xB8);
    int links = static_cast<int>(LevelNodes::GetLinkCount(from));
    for (int i = 0; i < links; ++i) {
        uint32_t link = LevelNodes::FetchLink(from, i);
        if (IsRestartNode(link)) {
            GlideToNode(LevelNodes::FetchVertex(link, true));
            return true;
        }
    }
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor) + 0x40) == kParkEditor)
        return true;
    GlideToNode(LevelNodes::FetchVertex(SkateRestart(0), true));
    return true;
}

// 0x0043B720
bool CDrivableModule::PlaceBeforeCamera(Scripts::CStruct*, Scripts::CScript*)
{
    void* camera = ViewportCamera(0);
    if (!camera)
        return true;
    const Maths::Vec4& eye = *static_cast<Gfx2D::ViewCam*>(camera)->FetchLocation();
    const Maths::Mat4& view = *static_cast<Gfx2D::ViewCam*>(camera)->FetchMat();
    m_atPoint = eye;
    // 144 along the camera's up, then 144 back along its at (12 feet each, scaled twice - as the original).
    const Maths::Vec4& up = view.row[1];
    double upX = static_cast<double>(up.x) * kFeet, upY = static_cast<double>(up.y) * kFeet;
    float upZ = static_cast<float>(static_cast<double>(up.z) * kFeet);
    float upW = static_cast<float>(static_cast<double>(up.w) * kFeet);
    m_atPoint.x = static_cast<float>(static_cast<double>(static_cast<float>(upX * kFeet)) + m_atPoint.x);
    m_atPoint.y = static_cast<float>(upY * kFeet + m_atPoint.y);
    m_atPoint.z = static_cast<float>(static_cast<double>(upZ) * kFeet + m_atPoint.z);
    m_atPoint.w = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(upW) * kFeet)) + m_atPoint.w);
    const Maths::Vec4& at = view.row[2];
    double atX = static_cast<double>(at.x) * kFeet, atY = static_cast<double>(at.y) * kFeet;
    float atZ = static_cast<float>(static_cast<double>(at.z) * kFeet);
    float atW = static_cast<float>(static_cast<double>(at.w) * kFeet);
    m_atPoint.x = static_cast<float>(static_cast<double>(m_atPoint.x) - static_cast<float>(atX * kFeet));
    m_atPoint.y = static_cast<float>(m_atPoint.y - atY * kFeet);
    m_atPoint.z = static_cast<float>(m_atPoint.z - static_cast<double>(atZ) * kFeet);
    m_atPoint.w = static_cast<float>(static_cast<double>(m_atPoint.w) - static_cast<float>(static_cast<double>(atW) * kFeet));
    m_entity->m_atPoint = m_atPoint;
    // Facing away from the camera.
    m_basisFrame.row[0] = {-view.row[0].x, -view.row[0].y, -view.row[0].z, -view.row[0].w};
    m_basisFrame.row[1] = view.row[1];
    m_basisFrame.row[2] = {-view.row[2].x, -view.row[2].y, -view.row[2].z, -view.row[2].w};
    Maths::Vec4 quat;
    MatrixToQuat(&quat, &m_basisFrame);
    m_orientation = quat;
    QuatNormalize(&m_orientation, 1.0f);
    QuatToMatrix(&m_orientation, &m_basisFrame);
    ClearToW1(m_basisFrame.row[3]);
    m_entity->m_basisFrame = m_basisFrame;
    UpdateDependents();
    return true;
}

namespace {

void __fastcall UpdateWheelRotationsOf(CDrivableModule* vehicle) { vehicle->UpdateWheelRotations(); }

// 0x004351C0 as the game calls it: the original update (0x0043C1D0, asleep) calls RefreshBoneRig right after it
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

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CDrivableModule::Teardown,
         &GameObjects::CDrivableModule::TickFrame,
         &GameObjects::CDrivableModule::ConfigureFromData,
         &GameObjects::CDrivableModule::Seal,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
