// The car physics component (Obj::CCarPhysicsComponent): see car_physics.h. From the disassembly of
// 0x0041B910-0x0041C1ED. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "game/obj/car_physics.h"
#include "game/gfx/d3dx.h"
#include "game/gfx/model.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/math.h"
#include "game/mth/vector.h"
#include "game/obj/motion_path.h"
#include "game/obj/suspend_component.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/sys/crt.h"

namespace Obj {

uint32_t g_carPhysicsMemberFunctions[18]{
            0xA2331896, 0xC695136D, 0, 0, 0, 0,
            0xC9F0DB4B, 0xC695136D, 0, 0, 0, 0,
            0, 0xC695136D, 0, 0, 0, 0};

namespace {

extern void** const kVtable;

constexpr uint32_t kMinTilt = 0x877DB8DA;
constexpr uint32_t kMaxTilt = 0xFB3464DF;
constexpr uint32_t kTiltRate = 0xFD8BC8D6;
constexpr uint32_t kTiltDivisor = 0xB2C7447B;
constexpr uint32_t kReadGlobalsEveryUpdate = 0x4CF13D8F; // (a script global: while set, the four re-read each update)

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kSuspend = 0xCE0CA665;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kModel = 0x286A8D26;

// The bones it turns.
constexpr uint32_t kWheelA = 0x80212262, kWheelB = 0x1E45B7C1;
constexpr uint32_t kWheelC = 0x69428757, kWheelD = 0xF04BD6ED;
constexpr uint32_t kSteeringWheelA = 0x6E2F434E, kSteeringWheelB = 0xF72612F4;
constexpr uint32_t kBody = 0x88C21962; // car

constexpr float kFramesPerSecond = 60.0f;         // 0x00645EA0
constexpr float kMinusRadiansToDegrees = -57.2957764f; // 0x00645EA4
constexpr float kFullTurn = 360.0f;               // 0x00645EA8
constexpr float kSpinScale = 0.000668449211f;     // 0x00645EAC
constexpr float kSpinSpeedScale = 90.0f;          // 0x00645EB0
constexpr float kDegreesToRadians = 0.0174532924f; // 0x00645464

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The game's C runtime asin (_CIasin: the argument and result on the FPU stack - game/sys/crt.h).
double GameAsin(double value)
{
    double result;
    void* function = Crt::AsinX87();
    __asm {
        fld value
        call function
        fstp result
    }
    return result;
}

// The model's skeleton description (0x00493040, no source yet).
CCarPhysicsComponent::BoneDesc* ModelBones(void* model)
{
    return reinterpret_cast<CCarPhysicsComponent::BoneDesc*>(static_cast<Nx::CModel*>(model)->GetAssetHierarchy());
}
// A skeleton's bone matrices (+0x04) and bone count (+0x14) - the folded getters 0x005480C0 / 0x004A2BA0.
Mth::Matrix* SkeletonMatrices(void* skeleton) { return *reinterpret_cast<Mth::Matrix**>(static_cast<uint8_t*>(skeleton) + 4); }
int SkeletonBoneCount(void* skeleton) { return *reinterpret_cast<int*>(static_cast<uint8_t*>(skeleton) + 0x14); }

// The motion component's speed (+0x30) and path follower (0x00423510).
float MotionSpeed(uint8_t* motion) { return *reinterpret_cast<float*>(motion + 0x30); }
uint8_t* MotionPathFollower(uint8_t* motion)
{
    return static_cast<uint8_t*>(reinterpret_cast<CMotionPathMembers*>(motion)->GetPathFollower());
}

// The suspend component (no source yet): suspended (+0x30, the getter 0x00433E90), and whether its object should be
// updated this frame (0x00433EA0).
bool SuspendSuspended(uint8_t* suspend) { return suspend[0x30] != 0; }
bool SuspendShouldUpdate(uint8_t* suspend, float* distance)
{
    return reinterpret_cast<CSuspendComponent*>(suspend)->ShouldAnimate(distance);
}

} // namespace

// 0x0041BD90
CBaseComponent* __cdecl CCarPhysicsComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CCarPhysicsComponent));
    return memory ? static_cast<CCarPhysicsComponent*>(memory)->Construct() : nullptr;
}

// 0x0041B910
void __cdecl CCarPhysicsComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_carPhysicsMemberFunctions, 0x18);
}

// 0x0041BAD0
CCarPhysicsComponent* CCarPhysicsComponent::Construct()
{
    CBaseComponent::Construct();
    m_wheelSpin = 0.0f;
    m_steer = 0.0f;
    m_targetSteer = 0.0f;
    m_vtable = kVtable;
    m_type = kType;
    m_bodyShown = true;
    return this;
}

// 0x0041B930
void CCarPhysicsComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0041BB00
CCarPhysicsComponent* CCarPhysicsComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041B940
void CCarPhysicsComponent::InitFromStructure(Script::CStruct* params)
{
    m_minTilt = Script::GetFloat(kMinTilt);
    params->GetFloat(kMinTilt, &m_minTilt, 0);
    m_maxTilt = Script::GetFloat(kMaxTilt);
    params->GetFloat(kMaxTilt, &m_maxTilt, 0);
    m_tiltRate = Script::GetFloat(kTiltRate);
    params->GetFloat(kTiltRate, &m_tiltRate, 0);
    m_tiltDivisor = Script::GetFloat(kTiltDivisor);
    params->GetFloat(kTiltDivisor, &m_tiltDivisor, 0);
}

// 0x0041BE10
void CCarPhysicsComponent::Finalize()
{
    mp_motion = reinterpret_cast<uint8_t*>(m_object->GetComponent(kMotion));
    mp_suspend = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSuspend));
    CBaseComponent* skeleton = m_object->GetComponent(kSkeleton);
    if (!skeleton)
        return;
    mp_model = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(m_object->GetComponent(kModel)) + 0x208);
    mp_skeleton = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(skeleton) + 0x18);
    BoneDesc* bones = ModelBones(mp_model);
    Mth::Matrix a = bones[2].matrix;
    MatrixMultiply(&a, &a, &bones[0].matrix);
    Mth::Matrix b = bones[4].matrix;
    MatrixMultiply(&b, &b, &bones[0].matrix);
    double dx = static_cast<double>(a.row[3].x) - b.row[3].x;
    double dy = static_cast<double>(a.row[3].y) - b.row[3].y;
    double dz = static_cast<double>(a.row[3].z) - b.row[3].z;
    m_wheelBase = static_cast<float>(std::fabs(std::sqrt(dz * dz + dy * dy + dx * dx)));
}

// 0x0041BB30
void CCarPhysicsComponent::UpdateAngles()
{
    uint8_t* motion = mp_motion;
    // The spin, kept within 0-360 (the steps taken on the FPU stack, from the stored value).
    double frames = Tmr::GetSlomoFrameSeconds() * kFramesPerSecond;
    double spin = frames * (static_cast<double>(MotionSpeed(motion)) * kSpinSpeedScale) * kSpinScale + m_wheelSpin;
    m_wheelSpin = static_cast<float>(spin);
    if (spin > kFullTurn) {
        double turned = m_wheelSpin;
        do
            turned -= kFullTurn;
        while (turned > kFullTurn);
        m_wheelSpin = static_cast<float>(turned);
    }
    if (m_wheelSpin < 0.0f) {
        double turned = m_wheelSpin;
        do
            turned += kFullTurn;
        while (turned < 0.0);
        m_wheelSpin = static_cast<float>(turned);
    }
    // The steering.
    uint8_t* follower = MotionPathFollower(motion);
    if (!follower) {
        m_steer = 0.0f;
    } else {
        float turn = *reinterpret_cast<float*>(follower + 0xE0);
        if (turn == 0.0f) {
            m_targetSteer = 0.0f;
        } else {
            const float* point = reinterpret_cast<const float*>(follower + 0x04);
            const float* from = reinterpret_cast<const float*>(follower + 0xB0);
            double dx = static_cast<double>(point[0]) - from[0];
            double dy = static_cast<double>(point[1]) - from[1];
            double dz = static_cast<double>(point[2]) - from[2];
            double ratio = m_wheelBase / std::sqrt(dz * dz + dy * dy + dx * dx);
            float clamped = static_cast<float>(ratio);
            if (ratio > 1.0f)
                clamped = 1.0f;
            else if (clamped < -1.0f)
                clamped = -1.0f;
            double target = GameAsin(clamped) * kMinusRadiansToDegrees;
            m_targetSteer = static_cast<float>(target);
            if (turn < 0.0f)
                m_targetSteer = static_cast<float>(-target);
        }
        m_steer = Mth::KeepWithin(m_targetSteer, m_steer,
                                  static_cast<float>(Tmr::GetSlomoFrameSeconds() * kFramesPerSecond));
    }
    // The tilt.
    float speed = MotionSpeed(mp_motion);
    float acceleration = static_cast<float>(static_cast<double>(speed) - m_lastSpeed);
    m_lastSpeed = speed;
    if (Script::GetInteger(kReadGlobalsEveryUpdate)) {
        m_minTilt = Script::GetFloat(kMinTilt);
        m_maxTilt = Script::GetFloat(kMaxTilt);
        m_tiltRate = Script::GetFloat(kTiltRate);
        m_tiltDivisor = Script::GetFloat(kTiltDivisor);
    }
    double target = m_tiltDivisor == 0.0f ? 0.0 : -(static_cast<double>(acceleration) / m_tiltDivisor);
    m_targetTilt = static_cast<float>(target);
    if (target > m_maxTilt)
        m_targetTilt = m_maxTilt;
    if (m_targetTilt < m_minTilt)
        m_targetTilt = m_minTilt;
    m_tilt = Mth::KeepWithin(m_targetTilt, m_tilt,
                             static_cast<float>(Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * m_tiltRate));
}

// 0x0041BF00
void __cdecl CCarPhysicsComponent::UpdateSkeleton(void* skeleton, void* model, float tilt, float spin, float steer)
{
    BoneDesc* bones = ModelBones(model);
    Mth::Matrix* matrices = SkeletonMatrices(skeleton);
    float tiltRadians = tilt * kDegreesToRadians;
    float steerRadians = steer * kDegreesToRadians;
    float spinRadians = spin * kDegreesToRadians;
    for (int i = 0; i < SkeletonBoneCount(skeleton); ++i) {
        BoneDesc& bone = bones[i];
        Mth::Matrix* matrix = &matrices[bone.index];
        *matrix = bone.matrix;
        Mth::Matrix rotation;
        switch (bone.name) {
        case kWheelA:
        case kWheelB:
        case kWheelC:
        case kWheelD:
            MatrixMultiply(matrix, &Mth::CreateRotateXMatrix(rotation, spinRadians), matrix);
            break;
        case kSteeringWheelA:
        case kSteeringWheelB:
            MatrixMultiply(matrix, &Mth::CreateRotateZMatrix(rotation, steerRadians), matrix);
            MatrixMultiply(matrix, &Mth::CreateRotateXMatrix(rotation, spinRadians), matrix);
            break;
        case kBody:
            MatrixMultiply(matrix, &Mth::CreateRotateXMatrix(rotation, tiltRadians), matrix);
            break;
        default:
            break;
        }
        if (i != 0)
            MatrixMultiply(matrix, matrix, &matrices[bone.parent]);
    }
}

// 0x0041C0B0
void CCarPhysicsComponent::UpdateBones()
{
    UpdateSkeleton(mp_skeleton, mp_model, m_tilt, m_wheelSpin, m_steer);
    if (m_bodyShown)
        return;
    Mth::Matrix collapsed;
    collapsed.Ident();
    Mth::Vector nothing = {0.0f, 0.0f, 0.0f, 1.0f};
    collapsed.Scale(nothing);
    SkeletonMatrices(mp_skeleton)[0] = collapsed;
}

// 0x0041C1B0
void CCarPhysicsComponent::Update()
{
    if (SuspendSuspended(mp_suspend))
        return;
    if (m_object->m_objectFlags & 1)
        return;
    if (!SuspendShouldUpdate(mp_suspend, nullptr))
        return;
    if (!mp_skeleton)
        return;
    UpdateAngles();
    UpdateBones();
}

// 0x0041B9E0
int CCarPhysicsComponent::ShowBody(Script::CStruct* params, Script::CScript*)
{
    int show = 0;
    params->GetInteger(0u, &show, 0);
    m_bodyShown = show != 0;
    return 1;
}

// 0x0041BA20
int CCarPhysicsComponent::DoNothing(Script::CStruct*, Script::CScript*) { return 1; }

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CCarPhysicsComponent::Destroy,
         &Obj::CCarPhysicsComponent::Update,
         &Obj::CCarPhysicsComponent::InitFromStructure,
         &Obj::CCarPhysicsComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &Obj::CBaseComponent::RunMemberFunction,
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
