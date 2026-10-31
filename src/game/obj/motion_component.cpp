// The motion component (GameObjects::CKinematicsModule, motion.h): its life, the script's wait on it, and its simpler
// member functions - storing positions and nodes, constant height, random links, hovering, speed, jumping.

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/maths/random.h"
#include "game/obj/emitter_object.h"
#include "game/obj/lock_obj.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/obj/path_follower.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/stopwatch.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

using namespace MotionDetail;

static_assert(sizeof(CPathFollower) == 0xE4, "0x00423C90 allocates 0xE4 bytes");

namespace {

extern void** const kVtable;

// Script wait types a component decides (CScript::m_suspendReason 5-10; cscript.h).
constexpr uint32_t kWaitMove = 5;
constexpr uint32_t kWaitJump = 7;
constexpr uint32_t kWaitPathStop = 8;
constexpr uint32_t kWaitRotate = 9;

// The game clock with its offset (0x0044ECE0 + 0x00696D60).
int GameClock() { return FrameClock::GameClockElapsed() + g_clockOffset; }

} // namespace

// --- life ---

// 0x00426650
CObjectModule* __cdecl CKinematicsModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CKinematicsModule));
    return memory ? static_cast<CKinematicsModule*>(memory)->MakeNew() : nullptr;
}

// 0x00424AA0
CKinematicsModule* CKinematicsModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_groundSpeed = 0.0f;
    m_groundOffset = 0.0f;
    m_kindTag = kType;
    m_pathFollower = nullptr;
    m_pathGroup = nullptr;
    m_groundHeightOnly = true;
    Reset();
    return this;
}

// 0x00424AE0
void CKinematicsModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_pathFollower) {
        reinterpret_cast<CPathFollower*(__thiscall*)(CPathFollower*, unsigned int)>(m_pathFollower->m_vtable[0])(
            m_pathFollower, 1);
        m_pathFollower = nullptr;
    }
    if (m_turnPtr) {
        MemSys::Hub::Pull()->Dealloc(m_turnPtr); // (no destructor run)
        m_turnPtr = nullptr;
    }
    if (CFollowTrail* trail = m_trail) {
        // (its destructor: the empty 0x005480A0)
        MemSys::Hub::Pull()->Dealloc(trail);
        m_trail = nullptr;
    }
    CObjectModule::DestroyBody();
}

// 0x004266F0
CKinematicsModule* CKinematicsModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00423D00
void CKinematicsModule::Reset()
{
    const Maths::Vec4 rest = {0.0f, 0.0f, 0.0f, 1.0f};
    m_rotTarget = rest;
    m_rotDone = rest;
    m_rotMaxSpeed = rest;
    m_spinRates = rest;
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
    for (Maths::Vec4& corner : m_groundTriangle)
        corner = rest;
    for (auto& triangle : m_groundTriangles)
        for (Maths::Vec4& corner : triangle)
            corner = rest;
}

// 0x00423C90
void CKinematicsModule::InitPathFollower(CGameEntity* owner)
{
    if (m_pathFollower)
        return;
    void* memory = MemSys::AllocZeroed(sizeof(CPathFollower));
    m_pathFollower = memory ? static_cast<CPathFollower*>(memory)->MakeNew(owner) : nullptr;
}

// --- the vtable ---

// 0x00426720
void CKinematicsModule::ConfigureFromData(Scripts::CStruct* params)
{
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, 0);
    m_listLink = name;
    m_ownNodeName = name;
    FaceNode(params);
}

// 0x004236C0
void CKinematicsModule::StepWaitState(Scripts::CScript* script, uint32_t /*arg*/)
{
    switch (script->m_suspendReason) {
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
int CKinematicsModule::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    if (name != kObjMoveToNode)
        return 2;
    GlideToNode(params, script);
    return 1;
}

// --- the update's parts ---

// 0x00423740
void CKinematicsModule::UpdateHover()
{
    uint32_t period = static_cast<uint32_t>(m_bobPeriod);
    uint32_t inPeriod = static_cast<uint32_t>(GameClock()) % period;
    int doubled = static_cast<int>(inPeriod * 2);
    double phase = static_cast<double>(doubled);
    if (doubled < 0)
        phase += 4294967296.0; // (the count taken as unsigned)
    phase = phase * static_cast<double>(Bits(0x40490FDB)) / static_cast<double>(m_bobPeriod); // (pi)
    float offset = static_cast<float>(X87Sin(phase) * static_cast<double>(m_bobHeight));
    m_bobNow = offset;
    float height = m_entity->m_atPoint.y;
    m_hoverBase = height;
    m_entity->m_atPoint.y = static_cast<float>(static_cast<double>(offset) + height);
}

// --- script member functions ---

// 0x004237D0
bool CKinematicsModule::Obj_StorePos(Scripts::CStruct*, Scripts::CScript*)
{
    m_storedPos = m_entity->m_atPoint;
    return true;
}

// 0x00423810
bool CKinematicsModule::Obj_StoreNode(Scripts::CStruct*, Scripts::CScript*)
{
    m_savedNodeId = m_pathFollower->m_listLink;
    return true;
}

// 0x00423830
bool CKinematicsModule::Obj_SetConstantHeight(Scripts::CStruct* params, Scripts::CScript*)
{
    if (params->HasBareFlag(kOff)) {
        m_motionFlags2 &= ~MOTION2_CONSTANT_HEIGHT;
        return true;
    }
    m_motionFlags2 |= MOTION2_CONSTANT_HEIGHT;
    params->FetchFloat(0u, &m_entity->m_atPoint.y, 0);
    return true;
}

// 0x00423890
bool CKinematicsModule::Obj_GetRandomLink(Scripts::CStruct*, Scripts::CScript* script)
{
    uint32_t links = LevelNodes::GetLinkCount(m_listLink);
    if (links)
        script->m_args->PutInt(kLink, Maths::Rnd(static_cast<int>(links)) + 1);
    return true;
}

// 0x004238D0
bool CKinematicsModule::Obj_Hover(Scripts::CStruct* params, Scripts::CScript*)
{
    if (params->HasBareFlag(kOff)) {
        m_entity->m_atPoint.y = m_hoverBase;
        m_motionFlags &= ~MOTION_HOVERING;
        return true;
    }
    m_bobNow = 0.0f;
    params->FetchFloat(kAmp, &m_bobHeight, 0);
    float frequency = 1.0f;
    params->FetchFloat(kFreq, &frequency, 0);
    int period = Truncate(static_cast<double>(Bits(0x447A0000)) / frequency); // (1000)
    m_bobPeriod = period;
    // (a tenth either way: period - period / 10 + Rnd(period * 2 / 10), in the original's 32-bit arithmetic)
    int twice = static_cast<int>(static_cast<uint32_t>(period) * 20u);
    int random = Maths::Rnd(twice / 10);
    period = static_cast<int>(static_cast<uint32_t>(m_bobPeriod - m_bobPeriod / 10) + static_cast<uint32_t>(random));
    m_bobPeriod = period;
    if (period <= 0)
        m_bobPeriod = 1;
    m_motionFlags |= MOTION_HOVERING;
    m_hoverBase = m_entity->m_atPoint.y;
    return true;
}

// 0x004239F0
bool CKinematicsModule::Obj_GetSpeed(Scripts::CStruct* params, Scripts::CScript* script)
{
    float speed = 0.0f;
    if (m_motionFlags & MOTION_MOVING) {
        speed = m_moveSpeed;
    } else if (m_entity->FetchModule(kLockObj) &&
               (static_cast<CPinnedObjectModule*>(m_entity->FetchModule(kLockObj))->m_lockFlags & LOCK_FLAG_LOCKED)) {
        float dx = static_cast<float>(static_cast<double>(m_entity->m_atPoint.x) - m_entity->m_oldPos.x);
        float dy = static_cast<float>(static_cast<double>(m_entity->m_atPoint.y) - m_entity->m_oldPos.y);
        float dz = static_cast<float>(static_cast<double>(m_entity->m_atPoint.z) - m_entity->m_oldPos.z);
        double frame = FrameClock::GetSlomoFrameSeconds();
        if (!(frame == 0.0)) {
            double moved = std::sqrt((static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) +
                                     static_cast<double>(dx) * dx);
            speed = static_cast<float>(moved / frame);
        }
    } else {
        const Maths::Vec4& v = m_entity->m_vel;
        speed = static_cast<float>(std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                                             static_cast<double>(v.z) * v.z));
    }
    if (!params->HasBareFlag(kIps)) {
        // (inches per second to feet per second, else to miles per hour)
        float scale = params->HasBareFlag(kFps) ? Bits(0x3DAAAAAB) : Bits(0x3D68BA2E); // 1 / 12, 1 / 17.6
        speed = static_cast<float>(static_cast<double>(speed) * scale);
    }
    script->m_args->PushFloat("Speed", speed);
    return true;
}

// 0x00423B30
bool CKinematicsModule::Obj_Jump(Scripts::CStruct* params, Scripts::CScript*)
{
    m_jumpSpeed = 150.0f;
    params->FetchFloat(kSpeed, &m_jumpSpeed, 0);
    m_jumpStartSpeed = m_jumpSpeed;
    m_leapPull = -360.0f;
    params->FetchFloat(kGravity, &m_leapPull, 0);
    m_jumpStart = m_entity->m_atPoint;
    m_jumpColAbove = 6.0f;
    params->FetchFloat(kColDistAbove, &m_jumpColAbove, 0);
    m_jumpColBelow = 6.0f;
    params->FetchFloat(kColDistBelow, &m_jumpColBelow, 0);
    bool heading = params->FetchVec(kHeading, reinterpret_cast<Scripts::VectorValue*>(&m_leapDirection), 0);
    if (!heading && params->HasBareFlag(kUseCurrentHeading)) {
        m_leapDirection = m_entity->m_displayMatrix.row[2];
        heading = true;
    }
    if (heading) {
        // (the heading's y scales the speed up, then goes)
        m_jumpHasHeading = true;
        m_jumpSpeed = static_cast<float>(static_cast<double>(m_leapDirection.y) * m_jumpSpeed);
        m_leapDirection.y = 0.0f;
    } else {
        m_jumpHasHeading = false;
    }
    m_jumpHasLandHeight = false;
    if (params->FetchFloat(kLandHeight, &m_leapTouchdownY, 0))
        m_jumpHasLandHeight = true;
    m_motionFlags |= MOTION_JUMPING;
    return true;
}

} // namespace Obj

using GameObjects::CKinematicsModule;

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&CKinematicsModule::Teardown,
         &CKinematicsModule::TickFrame,
         &CKinematicsModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         &CKinematicsModule::StepWaitState,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &CKinematicsModule::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
