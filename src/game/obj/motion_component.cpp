// The motion component (Obj::CMotionComponent, motion.h): its life, the script's wait on it, and its simpler
// member functions - storing positions and nodes, constant height, random links, hovering, speed, jumping.

#include <cmath>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/obj/emitter.h"
#include "game/obj/lock_obj.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/obj/path_follower.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/obj/control_pad.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

using namespace MotionDetail;

static_assert(sizeof(CPathFollower) == 0xE4, "0x00423C90 allocates 0xE4 bytes");

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064620C);

// Script wait types a component decides (CScript::m_waitType 5-10; cscript.h).
constexpr uint32_t kWaitMove = 5;
constexpr uint32_t kWaitJump = 7;
constexpr uint32_t kWaitPathStop = 8;
constexpr uint32_t kWaitRotate = 9;

// The game clock with its offset (0x0044ECE0 + 0x00696D60).
int GameClock() { return Tmr::GameClockElapsed() + g_clockOffset; }

} // namespace

// --- life ---

// 0x00426650
CBaseComponent* __cdecl CMotionComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CMotionComponent));
    return memory ? static_cast<CMotionComponent*>(memory)->Construct() : nullptr;
}

// 0x00424AA0
CMotionComponent* CMotionComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_groundSpeed = 0.0f;
    m_groundOffset = 0.0f;
    m_type = kType;
    mp_pathFollower = nullptr;
    m_pathGroup = nullptr;
    m_groundHeightOnly = true;
    Reset();
    return this;
}

// 0x00424AE0
void CMotionComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_pathFollower) {
        reinterpret_cast<CPathFollower*(__thiscall*)(CPathFollower*, unsigned int)>(mp_pathFollower->m_vtable[0])(
            mp_pathFollower, 1);
        mp_pathFollower = nullptr;
    }
    if (mp_turn) {
        Mem::Manager::Get()->Free(mp_turn); // (no destructor run)
        mp_turn = nullptr;
    }
    if (CFollowTrail* trail = mp_trail) {
        // (its destructor: the empty 0x005480A0)
        Mem::Manager::Get()->Free(trail);
        mp_trail = nullptr;
    }
    CBaseComponent::DestroyBody();
}

// 0x004266F0
CMotionComponent* CMotionComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00423D00
void CMotionComponent::Reset()
{
    const Mth::Vector rest = {0.0f, 0.0f, 0.0f, 1.0f};
    m_rotTarget = rest;
    m_rotDone = rest;
    m_rotMaxSpeed = rest;
    m_rotSpeed = rest;
    m_rotAcceleration = rest;
    m_rotDeceleration = rest;
    m_moveStart = rest;
    m_moveTarget = rest;
    m_moveDistance = 0.0f;
    m_moveDone = 0.0f;
    m_moveSpeed = 0.0f;
    m_moveMaxSpeed = 0.0f;
    m_moveAcceleration = 0.0f;
    m_pathAcceleration = 0.0f;
    m_pathDeceleration = 0.0f;
    m_pathMaxSpeed = 0.0f;
    m_pathSpeed = 0.0f;
    m_pathTurnDistance = 0.0f;
    m_pathSpeed1A4 = 0.0f;
    m_pathSpeed1A8 = 0.0f;
    m_pathMinStopSpeed = 0.0f;
    m_turnTime = 0.0f;
    m_turnElapsed = 0.0f;
    m_motionFlags = 0;
    m_motionFlags2 = 0;
    for (Mth::Vector& corner : m_groundTriangle)
        corner = rest;
    for (auto& triangle : m_groundTriangles)
        for (Mth::Vector& corner : triangle)
            corner = rest;
}

// 0x00423C90
void CMotionComponent::InitPathFollower(CCompositeObject* owner)
{
    if (mp_pathFollower)
        return;
    void* memory = Mem::Calloc(sizeof(CPathFollower));
    mp_pathFollower = memory ? static_cast<CPathFollower*>(memory)->Construct(owner) : nullptr;
}

// --- the vtable ---

// 0x00426720
void CMotionComponent::InitFromStructure(Script::CStruct* params)
{
    uint32_t name = 0;
    params->GetChecksum(kName, &name, 0);
    m_node = name;
    m_firstNode = name;
    OrientToNode(params);
}

// 0x004236C0
void CMotionComponent::ProcessWait(Script::CScript* script, uint32_t /*arg*/)
{
    switch (script->m_waitType) {
    case kWaitMove:
        if (!(m_motionFlags & MOTION_MOVE_MASK))
            script->StopWaiting();
        break;
    case kWaitJump:
        if (!(m_motionFlags & MOTION_JUMPING))
            script->StopWaiting();
        break;
    case kWaitPathStop:
        if (!(m_pathSpeed > 0.0f)) // (a NaN stops it too)
            script->StopWaiting();
        break;
    case kWaitRotate:
        if (!(m_motionFlags & MOTION_ROTATE_MASK))
            script->StopWaiting();
        break;
    default: // (6: never)
        break;
    }
}

// 0x004274D0
int CMotionComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    if (name != kObjMoveToNode)
        return 2;
    MoveToNode(params, script);
    return 1;
}

// --- the update's parts ---

// 0x00423740
void CMotionComponent::UpdateHover()
{
    uint32_t period = static_cast<uint32_t>(m_hoverPeriod);
    uint32_t inPeriod = static_cast<uint32_t>(GameClock()) % period;
    int doubled = static_cast<int>(inPeriod * 2);
    double phase = static_cast<double>(doubled);
    if (doubled < 0)
        phase += 4294967296.0; // (the count taken as unsigned)
    phase = phase * static_cast<double>(Bits(0x40490FDB)) / static_cast<double>(m_hoverPeriod); // (pi)
    float offset = static_cast<float>(X87Sin(phase) * static_cast<double>(m_hoverAmplitude));
    m_hoverOffset = offset;
    float height = m_object->m_pos.y;
    m_hoverBase = height;
    m_object->m_pos.y = static_cast<float>(static_cast<double>(offset) + height);
}

// --- script member functions ---

// 0x004237D0
bool CMotionComponent::Obj_StorePos(Script::CStruct*, Script::CScript*)
{
    m_storedPos = m_object->m_pos;
    return true;
}

// 0x00423810
bool CMotionComponent::Obj_StoreNode(Script::CStruct*, Script::CScript*)
{
    m_storedNode = mp_pathFollower->m_node;
    return true;
}

// 0x00423830
bool CMotionComponent::Obj_SetConstantHeight(Script::CStruct* params, Script::CScript*)
{
    if (params->ContainsFlag(kOff)) {
        m_motionFlags2 &= ~MOTION2_CONSTANT_HEIGHT;
        return true;
    }
    m_motionFlags2 |= MOTION2_CONSTANT_HEIGHT;
    params->GetFloat(0u, &m_object->m_pos.y, 0);
    return true;
}

// 0x00423890
bool CMotionComponent::Obj_GetRandomLink(Script::CStruct*, Script::CScript* script)
{
    uint32_t links = SkateScript::GetLinkCount(m_node);
    if (links)
        script->m_params->AddInteger(kLink, Mth::Rnd(static_cast<int>(links)) + 1);
    return true;
}

// 0x004238D0
bool CMotionComponent::Obj_Hover(Script::CStruct* params, Script::CScript*)
{
    if (params->ContainsFlag(kOff)) {
        m_object->m_pos.y = m_hoverBase;
        m_motionFlags &= ~MOTION_HOVERING;
        return true;
    }
    m_hoverOffset = 0.0f;
    params->GetFloat(kAmp, &m_hoverAmplitude, 0);
    float frequency = 1.0f;
    params->GetFloat(kFreq, &frequency, 0);
    int period = Truncate(static_cast<double>(Bits(0x447A0000)) / frequency); // (1000)
    m_hoverPeriod = period;
    // (a tenth either way: period - period / 10 + Rnd(period * 2 / 10), in the original's 32-bit arithmetic)
    int twice = static_cast<int>(static_cast<uint32_t>(period) * 20u);
    int random = Mth::Rnd(twice / 10);
    period = static_cast<int>(static_cast<uint32_t>(m_hoverPeriod - m_hoverPeriod / 10) + static_cast<uint32_t>(random));
    m_hoverPeriod = period;
    if (period <= 0)
        m_hoverPeriod = 1;
    m_motionFlags |= MOTION_HOVERING;
    m_hoverBase = m_object->m_pos.y;
    return true;
}

// 0x004239F0
bool CMotionComponent::Obj_GetSpeed(Script::CStruct* params, Script::CScript* script)
{
    float speed = 0.0f;
    if (m_motionFlags & MOTION_MOVING) {
        speed = m_moveSpeed;
    } else if (m_object->GetComponent(kLockObj) &&
               (static_cast<CLockObjComponent*>(m_object->GetComponent(kLockObj))->m_lockFlags & LOCK_FLAG_LOCKED)) {
        float dx = static_cast<float>(static_cast<double>(m_object->m_pos.x) - m_object->m_oldPos.x);
        float dy = static_cast<float>(static_cast<double>(m_object->m_pos.y) - m_object->m_oldPos.y);
        float dz = static_cast<float>(static_cast<double>(m_object->m_pos.z) - m_object->m_oldPos.z);
        double frame = Tmr::GetSlomoFrameSeconds();
        if (!(frame == 0.0)) {
            double moved = std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) +
                                     static_cast<double>(dx) * dx);
            speed = static_cast<float>(moved / frame);
        }
    } else {
        const Mth::Vector& v = m_object->m_vel;
        speed = static_cast<float>(std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                                             static_cast<double>(v.z) * v.z));
    }
    if (!params->ContainsFlag(kIps)) {
        // (inches per second to feet per second, else to miles per hour)
        float scale = params->ContainsFlag(kFps) ? Bits(0x3DAAAAAB) : Bits(0x3D68BA2E); // 1 / 12, 1 / 17.6
        speed = static_cast<float>(static_cast<double>(speed) * scale);
    }
    script->m_params->AddFloat("Speed", speed);
    return true;
}

// 0x00423B30
bool CMotionComponent::Obj_Jump(Script::CStruct* params, Script::CScript*)
{
    m_jumpSpeed = 150.0f;
    params->GetFloat(kSpeed, &m_jumpSpeed, 0);
    m_jumpStartSpeed = m_jumpSpeed;
    m_jumpGravity = -360.0f;
    params->GetFloat(kGravity, &m_jumpGravity, 0);
    m_jumpStart = m_object->m_pos;
    m_jumpColAbove = 6.0f;
    params->GetFloat(kColDistAbove, &m_jumpColAbove, 0);
    m_jumpColBelow = 6.0f;
    params->GetFloat(kColDistBelow, &m_jumpColBelow, 0);
    bool heading = params->GetVector(kHeading, reinterpret_cast<Script::VectorValue*>(&m_jumpHeading), 0);
    if (!heading && params->ContainsFlag(kUseCurrentHeading)) {
        m_jumpHeading = m_object->m_displayMatrix.row[2];
        heading = true;
    }
    if (heading) {
        // (the heading's y scales the speed up, then goes)
        m_jumpHasHeading = true;
        m_jumpSpeed = static_cast<float>(static_cast<double>(m_jumpHeading.y) * m_jumpSpeed);
        m_jumpHeading.y = 0.0f;
    } else {
        m_jumpHasHeading = false;
    }
    m_jumpHasLandHeight = false;
    if (params->GetFloat(kLandHeight, &m_jumpLandHeight, 0))
        m_jumpHasLandHeight = true;
    m_motionFlags |= MOTION_JUMPING;
    return true;
}

} // namespace Obj

using Obj::CMotionComponent;

REPLACE(0x00426650, CMotionComponent::Create);
REPLACE(0x00424AA0, &CMotionComponent::Construct);
REPLACE(0x00424AE0, &CMotionComponent::DestroyBody);
REPLACE(0x004266F0, &CMotionComponent::Destroy);
REPLACE(0x00423D00, &CMotionComponent::Reset);
REPLACE(0x00423C90, &CMotionComponent::InitPathFollower);
REPLACE(0x00426720, &CMotionComponent::InitFromStructure);
REPLACE(0x004236C0, &CMotionComponent::ProcessWait);
REPLACE(0x004274D0, &CMotionComponent::CallMemberFunction);
REPLACE(0x00423740, &CMotionComponent::UpdateHover);
REPLACE(0x004237D0, &CMotionComponent::Obj_StorePos);
REPLACE(0x00423810, &CMotionComponent::Obj_StoreNode);
REPLACE(0x00423830, &CMotionComponent::Obj_SetConstantHeight);
REPLACE(0x00423890, &CMotionComponent::Obj_GetRandomLink);
REPLACE(0x004238D0, &CMotionComponent::Obj_Hover);
REPLACE(0x004239F0, &CMotionComponent::Obj_GetSpeed);
REPLACE(0x00423B30, &CMotionComponent::Obj_Jump);
