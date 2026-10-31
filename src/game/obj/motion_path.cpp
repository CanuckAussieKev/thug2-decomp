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

namespace Obj {

uint32_t g_motionPathMemberFunctions[294]{
            0x21ACCF0D, 0x0A015E17, 0, 0, 0, 0,
            0x7BC461AE, 0x0A015E17, 0, 0, 0, 0,
            0x8E52ABEA, 0x0A015E17, 0, 0, 0, 0,
            0xC8CC6820, 0x0A015E17, 0, 0, 0, 0,
            0x68E1F022, 0x0A015E17, 0, 0, 0, 0,
            0xDFBE3DB3, 0x0A015E17, 0, 0, 0, 0,
            0xFB603EA9, 0x0A015E17, 0, 0, 0, 0,
            0x2CA63CDF, 0x0A015E17, 0, 0, 0, 0,
            0xD6697AD0, 0x0A015E17, 0, 0, 0, 0,
            0xA67FC783, 0x0A015E17, 0, 0, 0, 0,
            0x5CF1B7D4, 0x0A015E17, 0, 0, 0, 0,
            0x0B231A9D, 0x0A015E17, 0, 0, 0, 0,
            0x7C242A0B, 0x0A015E17, 0, 0, 0, 0,
            0xE52D7BB1, 0x0A015E17, 0, 0, 0, 0,
            0xD8F4BC32, 0x0A015E17, 0, 0, 0, 0,
            0xAA3CB476, 0x0A015E17, 0, 0, 0, 0,
            0xB75589CE, 0x0A015E17, 0, 0, 0, 0,
            0x6866312F, 0x0A015E17, 0, 0, 0, 0,
            0x434EA1FB, 0x0A015E17, 0, 0, 0, 0,
            0x76003D82, 0x0A015E17, 0, 0, 0, 0,
            0xBFB0031D, 0x0A015E17, 0, 0, 0, 0,
            0x33E04032, 0x0A015E17, 0, 0, 0, 0,
            0x103BD253, 0x0A015E17, 0, 0, 0, 0,
            0x6419CEFF, 0x0A015E17, 0, 0, 0, 0,
            0xA5A2D218, 0x0A015E17, 0, 0, 0, 0,
            0x74C30242, 0x0A015E17, 0, 0, 0, 0,
            0xE8D121CB, 0x0A015E17, 0, 0, 0, 0,
            0x33DF698F, 0x0A015E17, 0, 0, 0, 0,
            0xFE71453F, 0x0A015E17, 0, 0, 0, 0,
            0xBD6C6807, 0x0A015E17, 0, 0, 0, 0,
            0x0DA2D86D, 0x0A015E17, 0, 0, 0, 0,
            0xCC19C48A, 0x0A015E17, 0, 0, 0, 0,
            0x8B669799, 0x0A015E17, 0, 0, 0, 0,
            0xF389285D, 0x0A015E17, 0, 0, 0, 0,
            0x5CE058FD, 0x0A015E17, 0, 0, 0, 0,
            0x84EC6600, 0x0A015E17, 0, 0, 0, 0,
            0xEA81A32B, 0x0A015E17, 0, 0, 0, 0,
            0x3BCAAC3F, 0x0A015E17, 0, 0, 0, 0,
            0xC5ECA638, 0x0A015E17, 0, 0, 0, 0,
            0x1E094B84, 0x0A015E17, 0, 0, 0, 0,
            0xBDC4C878, 0x0A015E17, 0, 0, 0, 0,
            0xDBFDD02F, 0x0A015E17, 0, 0, 0, 0,
            0x8D95F1E1, 0x0A015E17, 0, 0, 0, 0,
            0x3407AD3D, 0x0A015E17, 0, 0, 0, 0,
            0xE606EBA2, 0x0A015E17, 0, 0, 0, 0,
            0xE76D73D2, 0x0A015E17, 0, 0, 0, 0,
            0x1AE46261, 0x0A015E17, 0, 0, 0, 0,
            0x2FAD370D, 0x0A015E17, 0, 0, 0, 0,
            0, 0x0A015E17, 0, 0, 0, 0};

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
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_motionPathMemberFunctions, 0x18);
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
