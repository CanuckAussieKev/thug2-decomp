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
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

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

// --- the component's life ---

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646770);
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
void ReadWheel(CVehicleComponent::Wheel* wheel, Script::CStruct* params)
{
    ReadWheelSettings(reinterpret_cast<uint8_t*>(wheel), params);
}
// The skate module's local skater (0x00566520) and a restart node's name (0x00566100).
CCompositeObject* LocalSkater()
{
    return static_cast<CCompositeObject*>(Mdl::Skate::Instance()->GetLocalSkater());
}
uint32_t SkateRestart(int index)
{
    return Mdl::Skate::Instance()->SkipToRestartPoint(static_cast<uint32_t>(index));
}
// The model's geometry (0x00493040 on the model's +0x208): its matrix at +0x60, the wheels' places (z up) at +0xE0 +
// 0x50 each.
const uint8_t* ModelGeometry(CBaseComponent* model)
{
    void* instance = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(model) + 0x208);
    return reinterpret_cast<const uint8_t*>(static_cast<Nx::CModel*>(instance)->GetAssetHierarchy());
}
const float* WheelPlace(const uint8_t* geometry, int i) { return reinterpret_cast<const float*>(geometry + 0xE0 + i * 0x50); }
// The skeleton (0x005480F0): its bone matrices (0x005480C0) and how many (0x004A2BA0).
void* SkeletonOf(CBaseComponent* skeleton)
{
    return reinterpret_cast<void*>(reinterpret_cast<GameNet::FoldedGetters*>(skeleton)->Word18());
}
// The viewport's camera (0x004A09C0; null if none), its position (0x00485A00) and matrix (0x00485A10).
void* ViewportCamera(int viewport) { return Nx::GetActiveCamera(viewport); }
// 0x0041A730 (not rewritten yet): the rotation of a node's angles.
void MatrixFromAngles(Mth::Matrix* matrix, const Mth::Vector* angles)
{
    matrix->InitFromAngles(*angles);
}

void ClearToW1(Mth::Vector& v) { v = {0.0f, 0.0f, 0.0f, 1.0f}; }

} // namespace

// 0x0043AA30
CBaseComponent* __cdecl CVehicleComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CVehicleComponent));
    return memory ? static_cast<CVehicleComponent*>(memory)->Construct() : nullptr;
}

// 0x00437C00
CVehicleComponent* CVehicleComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    ClearToW1(m_orientation);
    // (the colliders' vectors' constructors do nothing: 0x005902E0)
    m_collCache.Construct();
    mp_input = nullptr;
    m_debugMode = 0;
    m_passive = false;
    m_displaySteering = 0.0f;
    m_type = kType;
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
void CVehicleComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (m_wheels) {
        // (as `delete[]`: the count before the array, the wheels destroyed last to first)
        int* block = reinterpret_cast<int*>(m_wheels) - 1;
        for (int i = *block - 1; i >= 0; --i)
            m_wheels[i].Destruct();
        Mem::Manager::Get()->Free(block);
    }
    m_collCache.Destruct();
    CBaseComponent::DestroyBody();
}

// 0x0043C1A0
CVehicleComponent* CVehicleComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00437D10
void CVehicleComponent::InitFromStructure(Script::CStruct* params)
{
    // debug / 0xE6B5FCD9 / 0x751DA48B: the debug drawing's next mode, the previous one, off.
    if (params->ContainsFlag(0x935AB858))
        m_debugMode = (m_debugMode + 1) % 3;
    else if (params->ContainsFlag(0xE6B5FCD9))
        m_debugMode = (m_debugMode + 2) % 3;
    else if (params->ContainsFlag(0x751DA48B))
        m_debugMode = 0;
    if (params->ContainsComponentNamed(0x93346B8C)) {
        // (read into the original's argument slot: missing, the height is that pointer's bits)
        float height = std::bit_cast<float>(reinterpret_cast<uint32_t>(params));
        params->GetFloat(0x93346B8C, &height, 0);
        m_centerOfMass = {0.0f, height, 0.0f, 1.0f};
    }
    if (params->ContainsComponentNamed(0x93FCA499)) { // mass
        params->GetFloat(0x93FCA499, &m_invMass, 0);
        m_invMass = static_cast<float>(static_cast<double>(kGravity) / m_invMass);
    }
    if (params->ContainsComponentNamed(0x8C6473EC)) { // moment_of_inertia (its w into +0x168)
        params->GetVector(0x8C6473EC, reinterpret_cast<Script::VectorValue*>(m_invMoments), 0);
        for (float& moment : m_invMoments)
            moment = static_cast<float>(static_cast<double>(kGravity) / moment);
    }
    params->GetFloat(0x79C3B862, &m_restitution, 0);
    params->GetFloat(0xF476273C, &m_friction, 0);
    params->GetFloat(0x4A23A53C, &m_tippedFriction, 0);
    params->GetFloat(0x01DD4890, &m_depthCorrection, 0);
    params->GetFloat(0xB9F026ED, &m_wallSteering, 0);
    params->GetFloat(0x8C14709E, &m_alignSpeed, 0);
    params->GetFloat(0x2C91E49B, &m_alignTime, 0);
    params->GetFloat(0x280F0E0B, &m_alignRate, 0);
    m_alignWhenSlow = params->ContainsFlag(0x5341806A);
    if (params->ContainsComponentNamed(0xC1E5ABDD)) { // max_steering_angle (degrees)
        params->GetFloat(0xC1E5ABDD, &m_maxSteeringAngle, 0);
        m_maxSteeringAngle = static_cast<float>(static_cast<double>(m_maxSteeringAngle) * kToRadians);
    }
    params->GetFloat(0x386837DB, &m_yawDamping, 0);
    params->GetFloat(0x469CF79A, &m_spinDamping, 0);
    m_exitable = params->ContainsFlag(0xC2A136CC);
    m_noHandbrake = params->ContainsFlag(0xAD6C0A46);

    // colliders: each three points, a corner and the ends of its two edges (the edges kept relative to the corner -
    // their w less the corner's, which nothing sets). The points' average distance from the body's origin, less 48,
    // goes to the skater (CSkater 0x005802C0).
    if (params->ContainsComponentNamed(0xFC8C4AC6)) {
        Script::CArray* colliders;
        params->GetArray(0xFC8C4AC6, &colliders, 1);
        int count = static_cast<int>(colliders->m_size);
        float sum = 0.0f;
        for (int k = count - 1; k >= 0; --k) {
            auto* points = reinterpret_cast<Script::CArray*>(colliders->GetElement(static_cast<uint32_t>(k)));
            Collider& c = m_colliders[k];
            auto point = [points](int i) {
                return reinterpret_cast<const float*>(points->GetElement(static_cast<uint32_t>(i))) + 1; // (x, y, z)
            };
            const float* p = point(0);
            c.corner.x = p[0];
            c.corner.y = p[1];
            c.corner.z = p[2];
            for (int e = 0; e < 2; ++e) {
                p = point(1 + e);
                Mth::Vector& edge = c.edges[e];
                edge.x = p[0];
                edge.y = p[1];
                edge.z = p[2];
                edge.x = static_cast<float>(static_cast<double>(edge.x) - c.corner.x);
                edge.y = static_cast<float>(static_cast<double>(edge.y) - c.corner.y);
                edge.z = static_cast<float>(static_cast<double>(edge.z) - c.corner.z);
                edge.w = static_cast<float>(static_cast<double>(edge.w) - c.corner.w);
            }
            auto lengthSquared = [](double x, double y, double z) { return (x * x + y * y) + z * z; };
            const Mth::Vector& o = c.corner;
            const Mth::Vector& a = c.edges[0];
            const Mth::Vector& b = c.edges[1];
            double s0 = lengthSquared(o.x, o.y, o.z);
            double s1 = lengthSquared(static_cast<double>(o.x) + a.x, static_cast<double>(a.y) + o.y, static_cast<double>(a.z) + o.z);
            float s2 = static_cast<float>(
                lengthSquared(static_cast<double>(o.x) + b.x, static_cast<double>(b.y) + o.y, static_cast<double>(b.z) + o.z));
            sum = static_cast<float>(((std::sqrt(s0) + sum) + std::sqrt(s1)) + std::sqrt(static_cast<double>(s2)));
            c.lengths[0] = static_cast<float>(std::sqrt(lengthSquared(a.y, a.z, a.x)));
            c.lengths[1] = static_cast<float>(std::sqrt(lengthSquared(b.y, b.z, b.x)));
        }
        float distance = static_cast<float>(static_cast<double>(sum) * kThird / count - kCameraBack);
        reinterpret_cast<CSkater*>(LocalSkater())->Set42BD60(distance);
    }

    // engine: drive_torque and 0xB2268648 in foot-pounds, upshift_rpm, differential_ratio, reverse_torque_ratio,
    // gear_ratios (as many as there are - room for six).
    if (params->ContainsComponentNamed(0x1757E572)) {
        Script::CStruct* engine;
        params->GetStructure(0x1757E572, &engine, 1);
        if (engine->ContainsComponentNamed(0x9AA9FAEE)) {
            engine->GetFloat(0x9AA9FAEE, &m_driveTorque, 0);
            m_driveTorque = static_cast<float>(static_cast<double>(m_driveTorque) * kFeet);
        }
        if (engine->ContainsComponentNamed(0xB2268648)) {
            engine->GetFloat(0xB2268648, &m_engineBraking, 0);
            m_engineBraking = static_cast<float>(static_cast<double>(m_engineBraking) * kFeet);
        }
        if (engine->ContainsComponentNamed(0x33921583)) {
            engine->GetFloat(0x33921583, &m_upshiftSpeed, 0);
            m_upshiftSpeed = static_cast<float>(static_cast<double>(m_upshiftSpeed) * kRpmToRadians);
        }
        engine->GetFloat(0x94EA6601, &m_differentialRatio, 0);
        engine->GetFloat(0x82BCBB10, &m_reverseRatio, 0);
        if (engine->ContainsComponentNamed(0xC023FA75)) {
            Script::CArray* ratios;
            engine->GetArray(0xC023FA75, &ratios, 1);
            m_gearCount = static_cast<int>(ratios->m_size);
            for (int i = 0; i < m_gearCount; ++i)
                m_gearRatios[i] = ratios->GetFloat(static_cast<uint32_t>(i));
        }
    }

    // Wheels: made the first time (as `new[]`: the count before them), each read from its structure; all_wheels
    // for every one.
    if (params->ContainsComponentNamed(0xB3F8557E)) {
        Script::CArray* wheels = nullptr;
        params->GetArray(0xB3F8557E, &wheels, 1);
        int count = static_cast<int>(wheels->m_size);
        m_wheelCount = count;
        if (!m_wheels) {
            if (!GameVariable<bool>(0x007CC979))
                Mem::EnsureManager();
            auto* block = static_cast<int*>(
                Mem::Manager::Get()->Allocate(static_cast<uint32_t>(count) * sizeof(Wheel) + 4, 1, nullptr));
            Wheel* array = nullptr;
            if (block) {
                *block = count;
                array = reinterpret_cast<Wheel*>(block + 1);
                for (int i = 0; i < count; ++i)
                    array[i].Construct();
            }
            m_wheels = array;
        }
        for (int i = m_wheelCount - 1; i >= 0; --i)
            ReadWheel(&m_wheels[i], reinterpret_cast<Script::CStruct*>(wheels->GetElement(static_cast<uint32_t>(i))));
    }
    if (params->ContainsComponentNamed(0x371BADFF)) {
        Script::CStruct* all;
        params->GetStructure(0x371BADFF, &all, 1);
        for (int i = m_wheelCount - 1; i >= 0; --i)
            ReadWheel(&m_wheels[i], all);
    }
    m_driveWheelCount = 0;
    for (int i = m_wheelCount - 1; i >= 0; --i)
        if (m_wheels[i].drive)
            ++m_driveWheelCount;

    // The orientation from the object's matrix (or Save's Orientation: the object's matrix from it); the position
    // from the object's (or Pos: the object's too).
    Mth::Vector quat;
    MatrixToQuat(&quat, &m_object->m_matrix);
    m_orientation = quat;
    QuatNormalize(&m_orientation, 1.0f);
    if (params->ContainsComponentNamed(0xAA99C521)) {
        Mth::Vector saved{0.0f, 0.0f, 0.0f, 1.0f};
        params->GetVector(0xC97F3AA9, reinterpret_cast<Script::VectorValue*>(&saved), 0);
        m_orientation.x = saved.x;
        m_orientation.y = saved.y;
        m_orientation.z = saved.z;
        QuatNormalize(&m_orientation, 1.0f);
        QuatToMatrix(&m_orientation, &m_matrix);
        ClearToW1(m_matrix.row[3]);
        m_object->m_matrix = m_matrix;
        m_object->m_displayMatrix = m_matrix;
    }
    m_pos = m_object->m_pos;
    if (params->ContainsComponentNamed(0x7F261953)) {
        params->GetVector(0x7F261953, reinterpret_cast<Script::VectorValue*>(&m_pos), 0);
        m_object->m_pos = m_pos;
    }
    m_skaterVisible = m_skaterVisible || params->ContainsFlag(kSkater);
    if (params->ContainsFlag(kSkater)) {
        params->GetVector(0xEC86EF7A, reinterpret_cast<Script::VectorValue*>(&m_skaterPos), 1); // skater_pos
        params->GetChecksum(0xDA75A33E, &m_skaterAnim, 1);                                       // skater_anim
    }
    params->GetChecksum(0x0EDCF90E, &m_soundsName, 0); // sounds

    // At rest: no momenta or forces; the wheels in the air at their rest extension, each with an even share of the
    // weight.
    ClearToW1(m_mom);
    ClearToW1(m_rotmom);
    ClearToW1(m_force);
    ClearToW1(m_torque);
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
    mp_skeleton = m_object->GetComponent(kSkeleton);
}

// 0x0043BA20
void CVehicleComponent::Finalize()
{
    mp_input = reinterpret_cast<uint8_t*>(m_object->GetComponent(kInput));
    mp_model = m_object->GetComponent(kModel);
    const uint8_t* geometry = ModelGeometry(m_object->GetComponent(kModel));
    // The wheels' places from the model (z up there; always four).
    for (int i = 3; i >= 0; --i) {
        const float* place = WheelPlace(geometry, i);
        m_wheels[i].offset = {-place[0], place[2], -place[1], 1.0f};
    }
    Mth::Matrix bone = *reinterpret_cast<const Mth::Matrix*>(geometry + 0x60);
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
        for (const Mth::Vector& edge : c.edges) {
            double end = static_cast<double>(edge.y) + c.corner.y;
            if (end < lowest)
                lowest = static_cast<float>(end);
        }
    }
    // The skater's components (hidden unless shown driving).
    mp_skater = LocalSkater();
    mp_skaterCorePhysics = mp_skater->GetComponent(kSkaterCorePhysics);
    mp_trigger = mp_skater->GetComponent(kTrigger);
    if (!m_skaterVisible)
        mp_skater->Hide(true);
    else
        mp_animBlender = mp_skater->GetComponent(kAnimBlender);
    // Everything made relative to the wheels' centre (x, z) and the colliders' lowest point.
    double sumX = 0.0, sumZ = 0.0;
    for (int i = m_wheelCount - 1; i >= 0; --i) {
        sumX += m_wheels[i].offset.x;
        sumZ += m_wheels[i].offset.z;
    }
    double share = 1.0 / Unsigned(m_wheelCount);
    float centreX = static_cast<float>(sumX * share);
    double centreZ = sumZ * share;
    auto recentre = [centreX, centreZ, lowest](Mth::Vector& v) {
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
    m_object->m_pos = m_pos;
    m_object->m_vel = m_vel;
    m_object->m_matrix = m_matrix;
    m_object->m_displayMatrix = m_object->m_matrix;
    UpdateSkeleton();
}

// 0x0043C1D0
void CVehicleComponent::Update()
{
    m_interrupted = false;
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
        m_object->m_displayMatrix = m_object->m_matrix;
        UpdateSteering();
        if (m_brake)
            for (int i = m_wheelCount - 1; i >= 0; --i)
                m_wheels[i].spin = 0.0f;
        UpdateWheelRotations();
        UpdateSkeleton();
        UpdateSkater();
        return;
    }
    float frame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    if (m_kickTime > 0.0f)
        m_kickTime = static_cast<float>(static_cast<double>(m_kickTime) - frame);
    if (!(m_gravityTime == 0.0f)) {
        // Vehicle_AdjustGravity's override: over when its time is up or two wheels touch.
        double left = static_cast<double>(m_gravityTime) - frame;
        m_gravityTime = static_cast<float>(left);
        if (left <= 0.0 || static_cast<uint32_t>(m_wheelsInContact) > 1) {
            Dbg::Printf(kDebugFormat, kDebugFile, 0x2A0, reinterpret_cast<const char*>(0x006467D4)); // "Ending vehicle gravity override"
            m_gravityTime = 0.0f;
            m_gravityScale = 1.0f;
        }
    }
    // Equal steps of at most 1/30 s - six at most.
    int steps = 1;
    if (!(frame >= kMaxStep)) {
        m_timestep = frame;
    } else {
        steps = static_cast<int>(static_cast<int64_t>(std::ceil(static_cast<double>(frame) * kStepsPerSecond)));
        if (steps > 6)
            steps = 6;
        m_timestep = static_cast<float>(frame / static_cast<double>(steps));
    }
    while (steps != 0) {
        --steps;
        Integrate();
        ClearToW1(m_force);
        ClearToW1(m_torque);
        for (int i = m_wheelCount - 1; i >= 0; --i)
            m_wheels[i].torque = 0.0f;
        if (!m_passive) {
            Collide();
            if (m_interrupted || (m_object->m_objectFlags & OBJECT_FLAG_DEAD))
                return;
        }
        UpdateWheelContacts();
        if (m_interrupted || (m_object->m_objectFlags & OBJECT_FLAG_DEAD))
            return;
        UpdateSteering();
        ApplyDamping();
        ApplyWheelForces();
        if (m_wheelsInContact != 0)
            m_airTime = 0.0f;
        else
            m_airTime = static_cast<float>(static_cast<double>(m_timestep) + m_airTime);
        if (m_wheelsInContact == 0 && !(m_collisionImpulse > 0.0f))
            m_freeFallTime = static_cast<float>(static_cast<double>(m_timestep) + m_freeFallTime);
        else
            m_freeFallTime = 0.0f;
    }
    if (m_passive)
        return;
    DebugDraw();
    m_object->m_pos = m_pos;
    m_object->m_vel = m_vel;
    m_object->m_matrix = m_matrix;
    m_object->m_displayMatrix = m_object->m_matrix;
    CheckSleep();
    UpdateSkeleton();
    UpdateSkater();
}

// 0x0043A070
void CVehicleComponent::UpdateSkeleton()
{
    if (GameVariable<bool>(0x007D1EC0)) // (rendering off)
        return;
    // (the skeleton's +0x04, through the getter folded at 0x005480C0)
    auto* bones = reinterpret_cast<Mth::Matrix*>(static_cast<GameNet::FoldedGetters*>(SkeletonOf(mp_skeleton))->Word04());
    int count = GameFunction<int(__thiscall*)(void*)>(0x004A2BA0)(SkeletonOf(mp_skeleton));
    // Bone 0 the body (left as nothing), 1 the skater's place, 2 on the wheels: steered and rolled, at their place
    // with the suspension's extension, but no lower than maxDrawnY. In debug mode 2 all of them nothing.
    for (int b = count - 1; b >= 0; --b) {
        Mth::Matrix& bone = bones[b];
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
        Mth::Matrix turn;
        MatrixMultiply(&bone, &Mth::CreateRotateZMatrix(turn, wheel.displaySteering), &bone);
        MatrixMultiply(&bone, &Mth::CreateRotateXMatrix(turn, -wheel.rotation), &bone);
        bone.row[3] = wheel.offset;
        float drop = wheel.y > wheel.maxDrawnY ? wheel.maxDrawnY : wheel.y;
        bone.row[3].y = static_cast<float>(static_cast<double>(drop) + bone.row[3].y);
        bone.row[3].w = 1.0f;
    }
}

// 0x0043A2C0
Mth::Vector* CVehicleComponent::GetWheelRimPoint(Mth::Vector* out, const Wheel* wheel, int spoke, bool outer)
{
    Mth::Vector point;
    point.x = static_cast<float>((outer ? 1.0f : -1.0f) * static_cast<double>(kRimHalfWidth));
    double angle = static_cast<double>(spoke) * kSpokeAngle + wheel->rotation;
    point.y = static_cast<float>(Skater::X87Cos(angle) * wheel->radius);
    point.z = static_cast<float>(Skater::X87Sin(angle) * wheel->radius);
    point.w = 1.0f;
    Mth::Vector up{0.0f, 1.0f, 0.0f, 1.0f};
    Mth::Matrix steer;
    Mth::CreateRotateMatrix(steer, up, wheel->steering);
    Mth::Vector turned;
    steer.RotateVector(turned, point);
    Mth::Vector placed{static_cast<float>(static_cast<double>(turned.x) + wheel->offset.x),
                       static_cast<float>((static_cast<double>(turned.y) + wheel->y) + wheel->offset.y),
                       static_cast<float>(static_cast<double>(turned.z) + wheel->offset.z),
                       static_cast<float>(static_cast<double>(turned.w) + wheel->offset.w)};
    Mth::Vector world;
    m_matrix.RotateVector(world, placed);
    out->x = static_cast<float>(static_cast<double>(m_pos.x) + world.x);
    out->y = static_cast<float>(static_cast<double>(m_pos.y) + world.y);
    out->z = static_cast<float>(static_cast<double>(m_pos.z) + world.z);
    out->w = static_cast<float>(static_cast<double>(m_pos.w) + world.w);
    return out;
}

// 0x0043A400
void CVehicleComponent::DebugDraw()
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
            Mth::Vector a, b;
            for (bool outer : {true, false}) {
                GetWheelRimPoint(&a, &wheel, spoke, outer);
                GetWheelRimPoint(&b, &wheel, spoke + 1, outer);
                Gfx::AddDebugLine(&a, &b, colour, colour, 1);
            }
            GetWheelRimPoint(&a, &wheel, spoke, true);
            GetWheelRimPoint(&b, &wheel, spoke, false);
            Gfx::AddDebugLine(&a, &b, colour, colour, 1);
            // The axle: 240 either side of the wheel along its steered x, in the world.
            Mth::Vector up{0.0f, 1.0f, 0.0f, 1.0f};
            Mth::Matrix steer;
            Mth::CreateRotateMatrix(steer, up, wheel.steering);
            Mth::Vector ends[2];
            for (int e = 0; e < 2; ++e) {
                float side = e ? kBelow : -kBelow;
                Mth::Vector local{steer.row[0].x * side + wheel.offset.x, steer.row[0].y * side + wheel.offset.y + wheel.y,
                                  steer.row[0].z * side + wheel.offset.z, 1.0f};
                Mth::Vector world;
                m_matrix.RotateVector(world, local);
                ends[e] = {world.x + m_pos.x, world.y + m_pos.y, world.z + m_pos.z, world.w + m_pos.w};
            }
            Gfx::AddDebugLine(&ends[0], &ends[1], 0xFF000000, 0xFF000000, 1);
        }
    }
}

// 0x0043AA90 (not a Ghidra function)
void CVehicleComponent::MoveToNode(Script::CStruct* node)
{
    Mth::Vector pos, angles;
    SkateScript::GetPosition(node, &pos);
    SkateScript::GetAngles(node, &angles);
    Mth::Matrix matrix;
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
    Mth::Vector start{pos.x, static_cast<float>(static_cast<double>(pos.y) + kAbove), pos.z,
                      static_cast<float>(static_cast<double>(pos.w) + 1.0)};
    Mth::Vector end{pos.x, static_cast<float>(static_cast<double>(pos.y) - kBelow), pos.z, start.w};
    Nx::CFeeler feeler;
    feeler.Construct(&start, &end);
    if (feeler.GetCollision(false, false))
        pos.y = static_cast<float>(static_cast<double>(feeler.m_point.y) - above);
    // The node marks the fixed axle (the first fixed wheel from the last): back along the node's facing by its place.
    int fixed = -1;
    for (int i = 3; i >= 0; --i) {
        if (m_wheels[i].steeringSide == 0) {
            fixed = i;
            break;
        }
    }
    double back = m_wheels[fixed].offset.z;
    const Mth::Vector& at = matrix.row[2];
    m_pos.x = pos.x = static_cast<float>(pos.x - at.x * back);
    m_pos.y = pos.y = static_cast<float>(pos.y - at.y * back);
    m_pos.z = pos.z = static_cast<float>(pos.z - at.z * back);
    m_pos.w = static_cast<float>(pos.w - at.w * back);
    Mth::Vector quat;
    MatrixToQuat(&quat, &matrix);
    m_orientation = quat;
    ClearToW1(m_mom);
    ClearToW1(m_rotmom);
    // Settled by sixty updates without input, collision or debug drawing, the wheels kept from turning.
    m_passive = true;
    for (int n = 60; n != 0; --n) {
        for (int i = m_wheelCount - 1; i >= 0; --i)
            m_wheels[i].spin = 0.0f;
        CBaseComponent::Update();
    }
    m_passive = false;
    ClearToW1(m_mom);
    ClearToW1(m_rotmom);
    m_interrupted = true;
    m_awake = 0;
    m_object->m_pos = m_pos;
    m_object->m_vel = m_vel;
    m_object->m_matrix = m_matrix;
    m_object->m_displayMatrix = m_object->m_matrix;
    UpdateSkeleton();
    UpdateSkater();
    m_object->SetTeleported(true);
    mp_skater->SetTeleported(true);
}

// 0x0043B670 (not a Ghidra function)
bool CVehicleComponent::MoveToRestart(Script::CStruct* params, Script::CScript* script)
{
    // The node named; else the first restart linked from the script's node; else (outside the park editor) the skate
    // module's first restart.
    uint32_t name;
    if (params->GetChecksum(0u, &name, 0)) {
        MoveToNode(SkateScript::GetNode(name, true));
        return true;
    }
    uint32_t from = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(script) + 0xB8);
    int links = static_cast<int>(SkateScript::GetLinkCount(from));
    for (int i = 0; i < links; ++i) {
        uint32_t link = SkateScript::GetLink(from, i);
        if (IsRestartNode(link)) {
            MoveToNode(SkateScript::GetNode(link, true));
            return true;
        }
    }
    if (*reinterpret_cast<uint32_t*>(GameVariable<uint8_t*>(0x007CE5D8) + 0x40) == kParkEditor)
        return true;
    MoveToNode(SkateScript::GetNode(SkateRestart(0), true));
    return true;
}

// 0x0043B720
bool CVehicleComponent::PlaceBeforeCamera(Script::CStruct*, Script::CScript*)
{
    void* camera = ViewportCamera(0);
    if (!camera)
        return true;
    const Mth::Vector& eye = *static_cast<Gfx::Camera*>(camera)->GetPos();
    const Mth::Matrix& view = *static_cast<Gfx::Camera*>(camera)->GetMatrix();
    m_pos = eye;
    // 144 along the camera's up, then 144 back along its at (12 feet each, scaled twice - as the original).
    const Mth::Vector& up = view.row[1];
    double upX = static_cast<double>(up.x) * kFeet, upY = static_cast<double>(up.y) * kFeet;
    float upZ = static_cast<float>(static_cast<double>(up.z) * kFeet);
    float upW = static_cast<float>(static_cast<double>(up.w) * kFeet);
    m_pos.x = static_cast<float>(static_cast<double>(static_cast<float>(upX * kFeet)) + m_pos.x);
    m_pos.y = static_cast<float>(upY * kFeet + m_pos.y);
    m_pos.z = static_cast<float>(static_cast<double>(upZ) * kFeet + m_pos.z);
    m_pos.w = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(upW) * kFeet)) + m_pos.w);
    const Mth::Vector& at = view.row[2];
    double atX = static_cast<double>(at.x) * kFeet, atY = static_cast<double>(at.y) * kFeet;
    float atZ = static_cast<float>(static_cast<double>(at.z) * kFeet);
    float atW = static_cast<float>(static_cast<double>(at.w) * kFeet);
    m_pos.x = static_cast<float>(static_cast<double>(m_pos.x) - static_cast<float>(atX * kFeet));
    m_pos.y = static_cast<float>(m_pos.y - atY * kFeet);
    m_pos.z = static_cast<float>(m_pos.z - static_cast<double>(atZ) * kFeet);
    m_pos.w = static_cast<float>(static_cast<double>(m_pos.w) - static_cast<float>(static_cast<double>(atW) * kFeet));
    m_object->m_pos = m_pos;
    // Facing away from the camera.
    m_matrix.row[0] = {-view.row[0].x, -view.row[0].y, -view.row[0].z, -view.row[0].w};
    m_matrix.row[1] = view.row[1];
    m_matrix.row[2] = {-view.row[2].x, -view.row[2].y, -view.row[2].z, -view.row[2].w};
    Mth::Vector quat;
    MatrixToQuat(&quat, &m_matrix);
    m_orientation = quat;
    QuatNormalize(&m_orientation, 1.0f);
    QuatToMatrix(&m_orientation, &m_matrix);
    ClearToW1(m_matrix.row[3]);
    m_object->m_matrix = m_matrix;
    UpdateDependents();
    return true;
}

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
REPLACE(0x0043AA30, Obj::CVehicleComponent::Create);
REPLACE(0x00437C00, &Obj::CVehicleComponent::Construct);
REPLACE(0x0043B990, &Obj::CVehicleComponent::DestroyBody);
REPLACE(0x0043C1A0, &Obj::CVehicleComponent::Destroy);
REPLACE(0x00437D10, &Obj::CVehicleComponent::InitFromStructure);
REPLACE(0x0043BA20, &Obj::CVehicleComponent::Finalize);
REPLACE(0x0043C1D0, &Obj::CVehicleComponent::Update);
REPLACE(0x0043A070, &Obj::CVehicleComponent::UpdateSkeleton);
REPLACE(0x0043A2C0, &Obj::CVehicleComponent::GetWheelRimPoint);
REPLACE(0x0043A400, &Obj::CVehicleComponent::DebugDraw);
REPLACE(0x0043AA90, &Obj::CVehicleComponent::MoveToNode);
REPLACE(0x0043B670, &Obj::CVehicleComponent::MoveToRestart);
REPLACE(0x0043B720, &Obj::CVehicleComponent::PlaceBeforeCamera);
