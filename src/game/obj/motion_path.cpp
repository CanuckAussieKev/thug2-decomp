// The motion component's members of 0x00423130-0x00423642 (Obj::CMotionPathMembers): see motion_path.h. From their
// disassembly (0x00423160-0x004234F0 are no Ghidra functions; their names from the member-function table's static
// initialiser, 0x00640E30). The x87 roundings are kept: values the original stores are rounded to float, the ones it
// keeps on the FPU stack are worked in double.

#include "game/obj/motion_path.h"

#include <cstdint>

#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr uint32_t kFps = 0xAAD7C80F, kIps = 0xA18B8F32;     // speeds: feet / inches a second
constexpr uint32_t kFpsps = 0xF414A5EA, kIpsps = 0x7644323B; // accelerations: feet / inches a second per second
constexpr uint32_t kOff = 0xD443A2BC;
constexpr uint32_t kInches = 0xF915B4F3;

constexpr float kFeetToInches = 12.0f;          // 0x0064546C
constexpr float kMilesToInches = 63360.0f;      // 0x006461E4
constexpr float kPerHourToPerSecond = 0.000277777785f; // 0x006461E0 (1 / 3600)
constexpr float kDefaultMinStopVelocity = 15.0f;

// A speed (or acceleration) from the unnamed float into `value`: miles an hour, feet (`feet`) or inches (`inches`) a
// second.
void GetSpeed(Script::CStruct* params, float* value, uint32_t feet, uint32_t inches)
{
    if (!params->GetFloat(0u, value, 0))
        return;
    if (params->ContainsFlag(feet)) {
        *value *= kFeetToInches;
        return;
    }
    if (params->ContainsFlag(inches))
        return;
    *value = static_cast<float>(static_cast<double>(*value) * kMilesToInches * kPerHourToPerSecond);
}

// The script waits on the component (CScript::SetWaitOnComponent).
int WaitOn(Script::CScript* script, CMotionPathMembers* component, Script::EWaitType type)
{
    script->SetWaitOnComponent(type, reinterpret_cast<CBaseComponent*>(component), 0);
    return 1;
}

} // namespace

// 0x00423130
void __cdecl CMotionPathMembers::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00681EB8), 0x18);
}

// 0x00423150
bool CMotionPathMembers::IsRotating() const { return (m_motionFlags & MOTION_FLAG_ROTATING) != 0; }

// 0x00423520
bool CMotionPathMembers::PicksRandomLink() const { return (m_pathFlags & MOTION_PATH_RANDOM) != 0; }

// 0x00423530
void CMotionPathMembers::UpdatePathSpeed()
{
    if (!(m_motionFlags & MOTION_FLAG_ON_PATH))
        return;
    if (m_pathFlags & MOTION_PATH_STOPPING) {
        if (m_stopCovered >= m_stopDistance)
            return; // (NaN goes on)
        float covered = static_cast<float>(static_cast<double>(m_frameTime) * m_velocity + m_stopCovered);
        m_stopCovered = covered;
        if (m_stopDistance == 0.0f || covered >= m_stopDistance) {
            m_velocity = 0.0f;
            return;
        }
        double t = static_cast<double>(covered) / m_stopDistance;
        m_velocity = static_cast<float>((1.0 - t) * m_stopStartVelocity + t * m_minStopVelocity);
        return;
    }
    if (m_velocity < m_pathVelocity) {
        // Up to the path velocity.
        if (m_acceleration == 0.0f) {
            m_velocity = m_pathVelocity;
            return;
        }
        double velocity = static_cast<double>(m_frameTime) * m_acceleration + m_velocity;
        m_velocity = static_cast<float>(velocity);
        if (velocity > m_pathVelocity)
            m_velocity = m_pathVelocity;
        return;
    }
    if (!(m_velocity > m_pathVelocity))
        return; // (there, or NaN)
    // Down to it.
    if (m_deceleration == 0.0f) {
        m_velocity = m_pathVelocity;
        return;
    }
    double velocity = m_velocity - static_cast<double>(m_frameTime) * m_deceleration;
    m_velocity = static_cast<float>(velocity);
    if (velocity < m_pathVelocity)
        m_velocity = m_pathVelocity;
}

// 0x00423160
int CMotionPathMembers::IsMovingFromScript(Script::CStruct*, Script::CScript*)
{
    return (m_motionFlags & MOTION_FLAG_MOVING) ? 1 : 0;
}

// 0x00423170
int CMotionPathMembers::IsRotatingFromScript(Script::CStruct*, Script::CScript*)
{
    return (m_motionFlags & MOTION_FLAG_ROTATING) ? 1 : 0;
}

// 0x00423180
int CMotionPathMembers::StopMoving(Script::CStruct*, Script::CScript*)
{
    m_motionFlags &= ~MOTION_FLAG_MOVING;
    return 1;
}

// 0x00423190
int CMotionPathMembers::StopRotating(Script::CStruct*, Script::CScript*)
{
    m_motionFlags &= ~(MOTION_FLAG_ROTATING | 0x40u);
    return 1;
}

// 0x004231A0
int CMotionPathMembers::SetPathVelocity(Script::CStruct* params, Script::CScript*)
{
    GetSpeed(params, &m_pathVelocity, kFps, kIps);
    return 1;
}

// 0x00423210
int CMotionPathMembers::SetPathMinStopVel(Script::CStruct* params, Script::CScript*)
{
    GetSpeed(params, &m_minStopVelocity, kFps, kIps);
    return 1;
}

// 0x00423280
int CMotionPathMembers::SetPathAcceleration(Script::CStruct* params, Script::CScript*)
{
    GetSpeed(params, &m_acceleration, kFpsps, kIpsps);
    return 1;
}

// 0x004232F0
int CMotionPathMembers::SetPathDeceleration(Script::CStruct* params, Script::CScript*)
{
    GetSpeed(params, &m_deceleration, kFpsps, kIpsps);
    return 1;
}

// 0x00423360
int CMotionPathMembers::PathHeading(Script::CStruct* params, Script::CScript*)
{
    if (params->ContainsFlag(kOff))
        m_pathFlags |= MOTION_PATH_NO_HEADING;
    else
        m_pathFlags &= ~MOTION_PATH_NO_HEADING;
    return 1;
}

// 0x004233A0
int CMotionPathMembers::RandomPathMode(Script::CStruct* params, Script::CScript*)
{
    if (params->ContainsFlag(kOff))
        m_pathFlags &= ~MOTION_PATH_RANDOM;
    else
        m_pathFlags |= MOTION_PATH_RANDOM;
    return 1;
}

// 0x004233E0
int CMotionPathMembers::StopAlongPath(Script::CStruct* params, Script::CScript*)
{
    m_stopDistance = 0.0f;
    m_stopCovered = 0.0f;
    if (m_minStopVelocity == 0.0f)
        m_minStopVelocity = kDefaultMinStopVelocity;
    params->GetFloat(0u, &m_stopDistance, 0);
    if (!params->ContainsFlag(kInches)) {
        // (in feet: the stop's starting speed noted - not with `inches`, as the original)
        m_stopStartVelocity = m_velocity;
        m_stopDistance *= kFeetToInches;
    }
    if (m_stopDistance == 0.0f)
        m_velocity = 0.0f;
    m_pathFlags |= MOTION_PATH_STOPPING;
    return 1;
}

// 0x00423480
int CMotionPathMembers::StartAlongPath(Script::CStruct*, Script::CScript*)
{
    m_pathFlags &= ~MOTION_PATH_STOPPING;
    return 1;
}

// 0x00423490
int CMotionPathMembers::WaitJumpFinished(Script::CStruct*, Script::CScript* script)
{
    return WaitOn(script, this, static_cast<Script::EWaitType>(7));
}

// 0x004234B0
int CMotionPathMembers::WaitRotate(Script::CStruct*, Script::CScript* script)
{
    return WaitOn(script, this, static_cast<Script::EWaitType>(9));
}

// 0x004234D0
int CMotionPathMembers::WaitMove(Script::CStruct*, Script::CScript* script)
{
    return WaitOn(script, this, static_cast<Script::EWaitType>(5));
}

// 0x004234F0
int CMotionPathMembers::WaitStop(Script::CStruct*, Script::CScript* script)
{
    return WaitOn(script, this, static_cast<Script::EWaitType>(8));
}

} // namespace Obj

REPLACE(0x00423130, Obj::CMotionPathMembers::RegisterMemberFunctions);
REPLACE(0x00423150, &Obj::CMotionPathMembers::IsRotating);
REPLACE(0x00423520, &Obj::CMotionPathMembers::PicksRandomLink);
REPLACE(0x00423510, &Obj::CMotionPathMembers::GetPathFollower);
REPLACE(0x00423530, &Obj::CMotionPathMembers::UpdatePathSpeed);
REPLACE(0x00423160, &Obj::CMotionPathMembers::IsMovingFromScript);
REPLACE(0x00423170, &Obj::CMotionPathMembers::IsRotatingFromScript);
REPLACE(0x00423180, &Obj::CMotionPathMembers::StopMoving);
REPLACE(0x00423190, &Obj::CMotionPathMembers::StopRotating);
REPLACE(0x004231A0, &Obj::CMotionPathMembers::SetPathVelocity);
REPLACE(0x00423210, &Obj::CMotionPathMembers::SetPathMinStopVel);
REPLACE(0x00423280, &Obj::CMotionPathMembers::SetPathAcceleration);
REPLACE(0x004232F0, &Obj::CMotionPathMembers::SetPathDeceleration);
REPLACE(0x00423360, &Obj::CMotionPathMembers::PathHeading);
REPLACE(0x004233A0, &Obj::CMotionPathMembers::RandomPathMode);
REPLACE(0x004233E0, &Obj::CMotionPathMembers::StopAlongPath);
REPLACE(0x00423480, &Obj::CMotionPathMembers::StartAlongPath);
REPLACE(0x00423490, &Obj::CMotionPathMembers::WaitJumpFinished);
REPLACE(0x004234B0, &Obj::CMotionPathMembers::WaitRotate);
REPLACE(0x004234D0, &Obj::CMotionPathMembers::WaitMove);
REPLACE(0x004234F0, &Obj::CMotionPathMembers::WaitStop);
