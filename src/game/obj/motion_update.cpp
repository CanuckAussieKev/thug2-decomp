// The motion component (GameObjects::CKinematicsModule, motion.h): its update, moving to points, following paths of nodes and
// following a leader's trail.

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/obj/emitter_object.h"
#include "game/obj/lock_obj.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/obj/motion_path.h"
#include "game/obj/path_follower.h"
#include "game/obj/path_manager.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"

namespace GameObjects {

using namespace MotionDetail;

namespace {

constexpr uint32_t kSpawnObjScript = 0x3AEFE377;  // SpawnObjScript
constexpr uint32_t kSwitchObjScript = 0x86125749; // switchobjscript
constexpr uint32_t kParamsName = 0x7031F10C;      // Params
constexpr uint32_t kOriginalNode = 0x422E1FCE;    // originalnode
constexpr uint32_t kRange = 0x6C78A5B6;           // Range
constexpr uint32_t kOb = 0xFFFF9A1C;              // ob
constexpr uint32_t kInches = 0xF915B4F3;          // inches
constexpr uint32_t kDistance = 0xE36D657E;        // distance
constexpr uint32_t kOrientY = 0xE19E310A;         // OrientY
constexpr uint32_t kPedLogic = 0x6EFDDFB8;        // the pedlogic component

// The speed along a path (motion_path.h).
void UpdatePathSpeed(CKinematicsModule* motion) { reinterpret_cast<CMotionPathMembers*>(motion)->UpdatePathSpeed(); }

// The velocity of a step from where the object was (`back`: minus that position) to where it is now: x from the
// unrounded difference and reciprocal, the others from the rounded ones (as the original's x87 code).
void SetStepVelocity(CGameEntity* object, const float back[4], float frameTime)
{
    const Maths::Vec4& pos = object->m_atPoint;
    double x = static_cast<double>(back[0]) + pos.x;
    float y = static_cast<float>(static_cast<double>(back[1]) + pos.y);
    float z = static_cast<float>(static_cast<double>(back[2]) + pos.z);
    float w = static_cast<float>(static_cast<double>(back[3]) + pos.w);
    double inverse = 1.0 / static_cast<double>(frameTime);
    float rounded = static_cast<float>(inverse);
    Maths::Vec4& vel = object->m_vel;
    vel.z = static_cast<float>(static_cast<double>(z) * rounded);
    vel.w = static_cast<float>(static_cast<double>(w) * rounded);
    vel.x = static_cast<float>(x * inverse);
    vel.y = static_cast<float>(static_cast<double>(y) * rounded);
}

// Minus the object's position (the start of a step).
void Back(const CGameEntity* object, float back[4])
{
    back[0] = -object->m_atPoint.x;
    back[1] = -object->m_atPoint.y;
    back[2] = -object->m_atPoint.z;
    back[3] = -object->m_atPoint.w;
}

// The node a path follower is headed for (MOTION_FOLLOWING_PATH and a follower), or 0; and its last node.
uint32_t PathNode(const CKinematicsModule* motion)
{
    if (!(motion->m_motionFlags & MOTION_FOLLOWING_PATH) || !motion->mp_pathFollower)
        return 0;
    return motion->mp_pathFollower->m_listLink;
}
uint32_t PathLastNode(const CKinematicsModule* motion)
{
    if (!(motion->m_motionFlags & MOTION_FOLLOWING_PATH) || !motion->mp_pathFollower)
        return 0;
    return motion->mp_pathFollower->m_lastNode;
}

} // namespace

// --- the update ---

// 0x004270E0
void CKinematicsModule::TickFrame()
{
    m_frameTime = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    if (m_motionFlags) {
        if (m_motionFlags & MOTION_HOVERING) {
            m_entity->m_atPoint.y = m_hoverBase;
            UpdateHover();
        }
        if (m_motionFlags & MOTION_ROTATE_AXES)
            UpdateRotations();
        if (m_motionFlags & MOTION_TURNING)
            UpdateTurn();
        if (m_motionFlags & MOTION_MOVING) {
            if (UpdateMoveTo())
                m_motionFlags &= ~MOTION_MOVING;
        } else if (m_motionFlags & MOTION_FOLLOWING_PATH) {
            UpdatePath();
        } else if (m_motionFlags & MOTION_FOLLOWING_LEADER) {
            UpdateFollowLeader();
        }
        if (m_motionFlags & MOTION_JUMPING) {
            UpdateJump();
            m_entity->m_atPoint = m_jumpPos;
        }
    }
    // The ground offset (Obj_StickToGround's height to make up), eased in.
    if (!(std::fabs(m_groundOffset) > 0.0f)) {
        m_groundSpeed = 0.0f;
    } else {
        bool up = m_groundOffset > 0.0f;
        float acceleration = up ? 384.0f : -384.0f;
        if (up != (m_groundSpeed > 0.0f))
            m_groundSpeed = 0.0f;
        double change = static_cast<double>(acceleration) * m_frameTime;
        float step = static_cast<float>((0.5 * change + m_groundSpeed) * m_frameTime);
        m_groundSpeed = static_cast<float>(change + m_groundSpeed);
        // (past the offset: the rest of it, and no more speed; a NaN step goes as it is)
        if (up ? step > m_groundOffset : step < m_groundOffset) {
            m_groundSpeed = 0.0f;
            step = m_groundOffset;
        }
        m_entity->m_atPoint.y = static_cast<float>(static_cast<double>(step) + m_entity->m_atPoint.y);
        m_groundOffset = static_cast<float>(static_cast<double>(m_groundOffset) - step);
    }
    if (m_entity->m_ident > 15)
        m_entity->m_displayMatrix = m_entity->m_basisFrame;
}

// 0x00424200
bool CKinematicsModule::UpdateMoveTo()
{
    float back[4];
    Back(m_entity, back);
    if (!(m_moveAcceleration == 0.0f)) {
        double speed = FrameClock::GetSlomoFrameSeconds() * 60.0 * m_moveAcceleration + m_moveSpeed;
        m_moveSpeed = static_cast<float>(speed);
        if (speed >= m_moveMaxSpeed) { // (not with a NaN)
            m_moveSpeed = m_moveMaxSpeed;
            m_moveAcceleration = 0.0f;
        }
    }
    double done = static_cast<double>(m_moveSpeed) * m_frameTime + m_moveDone;
    m_moveDone = static_cast<float>(done);
    Maths::Vec4& pos = m_entity->m_atPoint;
    if (done >= m_moveDistance) {
        pos = m_moveTarget;
        if (m_moveArriveNode)
            ArriveAtNode(m_moveArriveNode);
        m_entity->m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
        return true;
    }
    // (the fraction of the way rounded to a float; x and y through rounded products, z through an unrounded one)
    double dx = static_cast<double>(m_moveTarget.x) - m_moveStart.x;
    double dy = static_cast<double>(m_moveTarget.y) - m_moveStart.y;
    float dz = static_cast<float>(static_cast<double>(m_moveTarget.z) - m_moveStart.z);
    float dw = static_cast<float>(static_cast<double>(m_moveTarget.w) - m_moveStart.w);
    float t = static_cast<float>(done / m_moveDistance);
    float x = static_cast<float>(dx * t);
    float y = static_cast<float>(dy * t);
    float w = static_cast<float>(static_cast<double>(dw) * t);
    pos.z = static_cast<float>(static_cast<double>(dz) * t + m_moveStart.z);
    pos.w = static_cast<float>(static_cast<double>(w) + m_moveStart.w);
    pos.x = static_cast<float>(static_cast<double>(x) + m_moveStart.x);
    pos.y = static_cast<float>(static_cast<double>(y) + m_moveStart.y);
    SetStepVelocity(m_entity, back, m_frameTime);
    return false;
}

// 0x00426E70
void CKinematicsModule::UpdatePath()
{
    if (m_entity->FetchModule(kPedLogic))
        return;
    float back[4];
    Back(m_entity, back);
    UpdatePathSpeed(this);
    if (!(m_pathSpeed == 0.0f)) {
        Maths::Mat4& matrix = m_entity->m_basisFrame;
        const Maths::Vec4* along = (m_motionFlags2 & MOTION2_NO_PATH_HEADING) ? nullptr : &matrix.row[2];
        if (mp_pathFollower->TickFrame(static_cast<float>(static_cast<double>(m_frameTime) * m_pathSpeed), along)) {
            m_motionFlags &= ~MOTION_FOLLOWING_PATH;
            m_pathSpeed = 0.0f;
        }
        if (!(m_motionFlags2 & MOTION2_NO_PATH_HEADING)) {
            mp_pathFollower->GetHeading(&matrix.row[2]);
            if ((m_motionFlags2 & MOTION2_LEVEL_HEADING) && !(matrix.row[2].y == 0.0f)) {
                matrix.row[2].y = 0.0f;
                matrix.row[2].MakeUnit();
            }
            if (!(m_motionFlags2 & MOTION2_STICK_TO_GROUND)) {
                matrix.row[0] = Maths::CrossVec(matrix.row[1], matrix.row[2]);
                matrix.row[0].MakeUnit();
                matrix.row[2] = Maths::CrossVec(matrix.row[0], matrix.row[1]);
                matrix.row[2].MakeUnit();
            }
        }
        float height = m_entity->m_atPoint.y;
        m_entity->m_atPoint = mp_pathFollower->m_atPoint;
        if (m_motionFlags2 & MOTION2_STICK_TO_GROUND) {
            m_entity->m_atPoint.y = height;
            StickToGround();
        } else if (m_motionFlags2 & MOTION2_CONSTANT_HEIGHT) {
            m_entity->m_atPoint.y = height;
        }
    }
    SetStepVelocity(m_entity, back, m_frameTime);
}

// 0x004246A0
void CKinematicsModule::UpdateFollowLeader()
{
    mp_trail->TickFrame();
    float height = m_entity->m_atPoint.y;
    mp_trail->FetchPlace(&m_entity->m_atPoint, &m_entity->m_basisFrame);
    if (m_leaveYUnaffected)
        m_entity->m_atPoint.y = height;
}

// --- paths ---

// 0x00423FC0
void CKinematicsModule::StartPath(uint32_t node)
{
    if (m_pathGroup)
        m_pathGroup->DropOut(m_entity);
    m_pathGroup = CPathManager::Sole()->AddToPath(m_entity, node);
    InitPathFollower(m_entity);
    mp_pathFollower->Kickoff(node, &m_entity->m_atPoint);
}

// 0x00424020
void CKinematicsModule::ArriveAtNode(uint32_t node)
{
    Scripts::CStruct* nodeStruct = LevelNodes::FetchVertex(node, false);
    m_listLink = node;
    uint32_t script;
    if (nodeStruct->ReadHashValue(kSpawnObjScript, &script, 0)) {
        Scripts::CStruct* params = nullptr;
        nodeStruct->ReadSubStruct(kParamsName, &params, 0);
        m_entity->SpawnScript(script, params, 0, false);
        return;
    }
    if (nodeStruct->ReadHashValue(kSwitchObjScript, &script, 0)) {
        Scripts::CStruct* params = nullptr;
        nodeStruct->ReadSubStruct(kParamsName, &params, 0);
        m_entity->ReplaceScript(script, params);
    }
}

// 0x00424180
void CKinematicsModule::FollowPathLinked(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t node = params->HasBareFlag(kOriginalNode) ? m_firstNode : m_listLink;
    uint32_t links = LevelNodes::GetLinkCount(node);
    if (!links)
        return;
    uint32_t next;
    if (static_cast<int>(links) > 1 && (m_motionFlags2 & MOTION2_RANDOM_LINKS))
        next = LevelNodes::FetchLink(node, Maths::Rnd(static_cast<int>(links)));
    else
        next = LevelNodes::FetchLink(node, 0);
    StartPath(next);
    m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_PATH;
}

// 0x004244C0
CGameEntity* CKinematicsModule::GetNextObjOnPath(float range)
{
    if (!m_pathGroup)
        return nullptr;
    CSharedRef<CEntity>* slot = m_pathGroup->m_slots;
    int count = m_pathGroup->m_extent;
    CGameEntity* nearest = nullptr;
    float nearestDistance = 0.0f; // (only read once there is a nearest)
    for (; count > 0; --count, ++slot) {
        auto* other = static_cast<CGameEntity*>(slot->m_ownerObject);
        if (!other || other == m_entity)
            continue;
        const Maths::Vec4& at = m_entity->m_basisFrame.row[2];
        const Maths::Vec4& otherAt = other->m_basisFrame.row[2];
        // (facing the same way, and ahead)
        double facing = (static_cast<double>(at.z) * otherAt.z + static_cast<double>(at.y) * otherAt.y) +
                        static_cast<double>(at.x) * otherAt.x;
        if (!(facing >= 0.0))
            continue;
        double ahead = ((static_cast<double>(other->m_atPoint.z) - m_entity->m_atPoint.z) * at.z +
                        (static_cast<double>(other->m_atPoint.y) - m_entity->m_atPoint.y) * at.y) +
                       (static_cast<double>(other->m_atPoint.x) - m_entity->m_atPoint.x) * at.x;
        if (!(ahead >= 0.0))
            continue;
        // (both on the same segment: headed for the same node - and if that's the same, from the same one)
        auto* otherMotion = static_cast<CKinematicsModule*>(other->FetchModule(kType));
        if (PathNode(otherMotion) == PathNode(this)) {
            otherMotion = static_cast<CKinematicsModule*>(other->FetchModule(kType));
            if (PathLastNode(otherMotion) != PathLastNode(this))
                continue;
        }
        double dx = static_cast<double>(m_entity->m_atPoint.x) - other->m_atPoint.x;
        double dy = static_cast<double>(m_entity->m_atPoint.y) - other->m_atPoint.y;
        double dz = static_cast<double>(m_entity->m_atPoint.z) - other->m_atPoint.z;
        double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
        if (!(distance <= range))
            continue;
        if (nearest && !(distance < nearestDistance))
            continue;
        nearestDistance = static_cast<float>(distance);
        nearest = other;
    }
    return nearest;
}

// 0x004246F0
void CKinematicsModule::TrailBehindLead(Scripts::CStruct* params)
{
    if (params->HasBareFlag(kOff)) {
        m_motionFlags &= ~MOTION_FOLLOWING_LEADER;
        if (CFollowTrail* trail = mp_trail) {
            // (its destructor: the empty 0x005480A0)
            MemSys::Hub::Pull()->Dealloc(trail);
            mp_trail = nullptr;
        }
        return;
    }
    m_leaveYUnaffected = params->HasBareFlag("LeaveYUnaffected");
    m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_LEADER;
    static_cast<CPinnedObjectModule*>(m_entity->FetchModule(kLockObj))->m_lockFlags &= ~LOCK_FLAG_LOCKED;
    if (!mp_trail) {
        void* memory = MemSys::AllocZeroed(sizeof(CFollowTrail));
        mp_trail = memory ? static_cast<CFollowTrail*>(memory)->PrepareAll() : nullptr;
    }
    mp_trail->Reset();
    float distance = 0.0f;
    params->FetchFloat(kDistance, &distance, 0);
    mp_trail->m_reach = distance;
    uint32_t leader = 0;
    params->ReadHashValue(kName, &leader, 0);
    mp_trail->m_entityKey = leader;
    if (params->HasBareFlag(kOrientY))
        mp_trail->m_freeFacing = true;
}

// --- script member functions ---

// 0x00425150
bool CKinematicsModule::Obj_GetNextObjOnPath(Scripts::CStruct* params, Scripts::CScript* script)
{
    float range = 100.0f;
    params->FetchFloat(kRange, &range, 0);
    range = static_cast<float>(static_cast<double>(range) * 12.0f); // (feet)
    script->m_args->EraseModule(kOb);
    if (CGameEntity* next = GetNextObjOnPath(range))
        script->m_args->PutNameKey(kOb, next->m_ident);
    return true;
}

// 0x004251C0
bool CKinematicsModule::Obj_SetPathTurnDist(Scripts::CStruct* params, Scripts::CScript*)
{
    float distance = 0.0f; // (the original's is whatever its stack slot held: only read once found)
    if (params->FetchFloat(0u, &distance, 0)) {
        if (!params->HasBareFlag(kInches))
            distance = static_cast<float>(static_cast<double>(distance) * 12.0f);
        InitPathFollower(m_entity);
        mp_pathFollower->m_cornerDistance = distance;
    }
    return true;
}

// 0x00425220
bool CKinematicsModule::Obj_FollowPath(Scripts::CStruct* params, Scripts::CScript*)
{
    m_motionFlags2 &= ~MOTION2_STOPPED_ON_PATH;
    uint32_t node;
    if (params->ReadHashValue(kName, &node, 0)) {
        StartPath(node);
        m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_PATH;
    }
    return true;
}

// 0x00425270
bool CKinematicsModule::Obj_FollowPathLinked(Scripts::CStruct* params, Scripts::CScript* script)
{
    m_motionFlags2 &= ~MOTION2_STOPPED_ON_PATH;
    FollowPathLinked(params, script);
    return true;
}

// 0x00425290
bool CKinematicsModule::Obj_FollowPathStored(Scripts::CStruct*, Scripts::CScript*)
{
    StartPath(m_storedNode);
    m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_PATH;
    return true;
}

// 0x004252C0
bool CKinematicsModule::Obj_FollowLeader(Scripts::CStruct* params, Scripts::CScript*)
{
    TrailBehindLead(params);
    return true;
}

} // namespace Obj

using GameObjects::CKinematicsModule;
