// The car physics component (GameObjects::CAutomobileDynamicsModule): see car_physics.h. From the disassembly of
// 0x0041B910-0x0041C1ED. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "engine/objects/car_physics.h"
#include "engine/gfx/d3dx.h"
#include "engine/gfx/model.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/maths.h"
#include "engine/maths/vec4.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "platform/crt.h"

namespace GameObjects {

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
constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The game's C runtime asin (_CIasin: the argument and result on the FPU stack - platform/crt.h).
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
CAutomobileDynamicsModule::BoneDesc* ModelBones(void* model)
{
    return reinterpret_cast<CAutomobileDynamicsModule::BoneDesc*>(static_cast<Engine3D::CRenderModel*>(model)->GetAssetHierarchy());
}
// A skeleton's bone matrices (+0x04) and bone count (+0x14) - the folded getters 0x005480C0 / 0x004A2BA0.
Maths::Mat4* RigMatrixBlock(void* skeleton) { return *reinterpret_cast<Maths::Mat4**>(static_cast<uint8_t*>(skeleton) + 4); }
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
    return reinterpret_cast<CDormancyModule*>(suspend)->WantsAnimating(distance);
}

} // namespace

// 0x0041BD90
CObjectModule* __cdecl CAutomobileDynamicsModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CAutomobileDynamicsModule));
    return memory ? static_cast<CAutomobileDynamicsModule*>(memory)->MakeNew() : nullptr;
}

// 0x0041B910
void __cdecl CAutomobileDynamicsModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_carPhysicsMemberFunctions, 0x18);
}

// 0x0041BAD0
CAutomobileDynamicsModule* CAutomobileDynamicsModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_wheelSpin = 0.0f;
    m_steer = 0.0f;
    m_targetSteer = 0.0f;
    m_vtable = kVtable;
    m_kindTag = kType;
    m_bodyShown = true;
    return this;
}

// 0x0041B930
void CAutomobileDynamicsModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0041BB00
CAutomobileDynamicsModule* CAutomobileDynamicsModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041B940
void CAutomobileDynamicsModule::ConfigureFromData(Scripts::CStruct* params)
{
    m_minTilt = Scripts::FetchFloat(kMinTilt);
    params->FetchFloat(kMinTilt, &m_minTilt, 0);
    m_maxTilt = Scripts::FetchFloat(kMaxTilt);
    params->FetchFloat(kMaxTilt, &m_maxTilt, 0);
    m_tiltRate = Scripts::FetchFloat(kTiltRate);
    params->FetchFloat(kTiltRate, &m_tiltRate, 0);
    m_tiltDivisor = Scripts::FetchFloat(kTiltDivisor);
    params->FetchFloat(kTiltDivisor, &m_tiltDivisor, 0);
}

// 0x0041BE10
void CAutomobileDynamicsModule::Seal()
{
    m_motion = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kMotion));
    m_suspend = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSuspend));
    CObjectModule* skeleton = m_entity->FetchModule(kSkeleton);
    if (!skeleton)
        return;
    m_meshInstance = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(m_entity->FetchModule(kModel)) + 0x208);
    m_boneRig = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(skeleton) + 0x18);
    BoneDesc* bones = ModelBones(m_meshInstance);
    Maths::Mat4 a = bones[2].matrix;
    MatrixMultiply(&a, &a, &bones[0].matrix);
    Maths::Mat4 b = bones[4].matrix;
    MatrixMultiply(&b, &b, &bones[0].matrix);
    double dx = static_cast<double>(a.row[3].x) - b.row[3].x;
    double dy = static_cast<double>(a.row[3].y) - b.row[3].y;
    double dz = static_cast<double>(a.row[3].z) - b.row[3].z;
    m_wheelBase = static_cast<float>(std::fabs(std::sqrt(dz * dz + dy * dy + dx * dx)));
}

// 0x0041BB30
void CAutomobileDynamicsModule::UpdateAngles()
{
    uint8_t* motion = m_motion;
    // The spin, kept within 0-360 (the steps taken on the FPU stack, from the stored value).
    double frames = FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond;
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
        m_steer = Maths::KeepWithin(m_targetSteer, m_steer,
                                  static_cast<float>(FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond));
    }
    // The tilt.
    float speed = MotionSpeed(m_motion);
    float acceleration = static_cast<float>(static_cast<double>(speed) - m_lastSpeed);
    m_lastSpeed = speed;
    if (Scripts::FetchIntValue(kReadGlobalsEveryUpdate)) {
        m_minTilt = Scripts::FetchFloat(kMinTilt);
        m_maxTilt = Scripts::FetchFloat(kMaxTilt);
        m_tiltRate = Scripts::FetchFloat(kTiltRate);
        m_tiltDivisor = Scripts::FetchFloat(kTiltDivisor);
    }
    double target = m_tiltDivisor == 0.0f ? 0.0 : -(static_cast<double>(acceleration) / m_tiltDivisor);
    m_targetTilt = static_cast<float>(target);
    if (target > m_maxTilt)
        m_targetTilt = m_maxTilt;
    if (m_targetTilt < m_minTilt)
        m_targetTilt = m_minTilt;
    m_leanAngle = Maths::KeepWithin(m_targetTilt, m_leanAngle,
                             static_cast<float>(FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond * m_tiltRate));
}

// 0x0041BF00
void __cdecl CAutomobileDynamicsModule::RefreshBoneRig(void* skeleton, void* model, float tilt, float spin, float steer)
{
    BoneDesc* bones = ModelBones(model);
    Maths::Mat4* matrices = RigMatrixBlock(skeleton);
    float tiltRadians = tilt * kRadiansPerDegree;
    float steerRadians = steer * kRadiansPerDegree;
    float spinRadians = spin * kRadiansPerDegree;
    for (int i = 0; i < SkeletonBoneCount(skeleton); ++i) {
        BoneDesc& bone = bones[i];
        Maths::Mat4* matrix = &matrices[bone.index];
        *matrix = bone.matrix;
        Maths::Mat4 rotation;
        switch (bone.name) {
        case kWheelA:
        case kWheelB:
        case kWheelC:
        case kWheelD:
            MatrixMultiply(matrix, &Maths::MakePitchMatrix(rotation, spinRadians), matrix);
            break;
        case kSteeringWheelA:
        case kSteeringWheelB:
            MatrixMultiply(matrix, &Maths::MakeRollMatrix(rotation, steerRadians), matrix);
            MatrixMultiply(matrix, &Maths::MakePitchMatrix(rotation, spinRadians), matrix);
            break;
        case kBody:
            MatrixMultiply(matrix, &Maths::MakePitchMatrix(rotation, tiltRadians), matrix);
            break;
        default:
            break;
        }
        if (i != 0)
            MatrixMultiply(matrix, matrix, &matrices[bone.parent]);
    }
}

// 0x0041C0B0
void CAutomobileDynamicsModule::UpdateBones()
{
    RefreshBoneRig(m_boneRig, m_meshInstance, m_leanAngle, m_wheelSpin, m_steer);
    if (m_bodyShown)
        return;
    Maths::Mat4 collapsed;
    collapsed.ResetToIdentity();
    Maths::Vec4 nothing = {0.0f, 0.0f, 0.0f, 1.0f};
    collapsed.StretchBy(nothing);
    RigMatrixBlock(m_boneRig)[0] = collapsed;
}

// 0x0041C1B0
void CAutomobileDynamicsModule::TickFrame()
{
    if (SuspendSuspended(m_suspend))
        return;
    if (m_entity->m_objectFlags & 1)
        return;
    if (!SuspendShouldUpdate(m_suspend, nullptr))
        return;
    if (!m_boneRig)
        return;
    UpdateAngles();
    UpdateBones();
}

// 0x0041B9E0
int CAutomobileDynamicsModule::ShowBody(Scripts::CStruct* params, Scripts::CScript*)
{
    int show = 0;
    params->FetchIntValue(0u, &show, 0);
    m_bodyShown = show != 0;
    return 1;
}

// 0x0041BA20
int CAutomobileDynamicsModule::DoNothing(Scripts::CStruct*, Scripts::CScript*) { return 1; }

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CAutomobileDynamicsModule::Teardown,
         &GameObjects::CAutomobileDynamicsModule::TickFrame,
         &GameObjects::CAutomobileDynamicsModule::ConfigureFromData,
         &GameObjects::CAutomobileDynamicsModule::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
