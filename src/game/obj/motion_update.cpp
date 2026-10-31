// The motion component (Obj::CMotionComponent, motion.h): its update, moving to points, following paths of nodes and
// following a leader's trail.

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/random.h"
#include "game/obj/emitter.h"
#include "game/obj/lock_obj.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/obj/motion_path.h"
#include "game/obj/path_follower.h"
#include "game/obj/path_manager.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"

namespace Obj {

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
void UpdatePathSpeed(CMotionComponent* motion) { reinterpret_cast<CMotionPathMembers*>(motion)->UpdatePathSpeed(); }

// The velocity of a step from where the object was (`back`: minus that position) to where it is now: x from the
// unrounded difference and reciprocal, the others from the rounded ones (as the original's x87 code).
void SetStepVelocity(CCompositeObject* object, const float back[4], float frameTime)
{
    const Mth::Vector& pos = object->m_pos;
    double x = static_cast<double>(back[0]) + pos.x;
    float y = static_cast<float>(static_cast<double>(back[1]) + pos.y);
    float z = static_cast<float>(static_cast<double>(back[2]) + pos.z);
    float w = static_cast<float>(static_cast<double>(back[3]) + pos.w);
    double inverse = 1.0 / static_cast<double>(frameTime);
    float rounded = static_cast<float>(inverse);
    Mth::Vector& vel = object->m_vel;
    vel.z = static_cast<float>(static_cast<double>(z) * rounded);
    vel.w = static_cast<float>(static_cast<double>(w) * rounded);
    vel.x = static_cast<float>(x * inverse);
    vel.y = static_cast<float>(static_cast<double>(y) * rounded);
}

// Minus the object's position (the start of a step).
void Back(const CCompositeObject* object, float back[4])
{
    back[0] = -object->m_pos.x;
    back[1] = -object->m_pos.y;
    back[2] = -object->m_pos.z;
    back[3] = -object->m_pos.w;
}

// The node a path follower is headed for (MOTION_FOLLOWING_PATH and a follower), or 0; and its last node.
uint32_t PathNode(const CMotionComponent* motion)
{
    if (!(motion->m_motionFlags & MOTION_FOLLOWING_PATH) || !motion->mp_pathFollower)
        return 0;
    return motion->mp_pathFollower->m_node;
}
uint32_t PathLastNode(const CMotionComponent* motion)
{
    if (!(motion->m_motionFlags & MOTION_FOLLOWING_PATH) || !motion->mp_pathFollower)
        return 0;
    return motion->mp_pathFollower->m_lastNode;
}

} // namespace

// --- the update ---

// 0x004270E0
void CMotionComponent::Update()
{
    m_frameTime = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    if (m_motionFlags) {
        if (m_motionFlags & MOTION_HOVERING) {
            m_object->m_pos.y = m_hoverBase;
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
            m_object->m_pos = m_jumpPos;
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
        m_object->m_pos.y = static_cast<float>(static_cast<double>(step) + m_object->m_pos.y);
        m_groundOffset = static_cast<float>(static_cast<double>(m_groundOffset) - step);
    }
    if (m_object->m_id > 15)
        m_object->m_displayMatrix = m_object->m_matrix;
}

// 0x00424200
bool CMotionComponent::UpdateMoveTo()
{
    float back[4];
    Back(m_object, back);
    if (!(m_moveAcceleration == 0.0f)) {
        double speed = Tmr::GetSlomoFrameSeconds() * 60.0 * m_moveAcceleration + m_moveSpeed;
        m_moveSpeed = static_cast<float>(speed);
        if (speed >= m_moveMaxSpeed) { // (not with a NaN)
            m_moveSpeed = m_moveMaxSpeed;
            m_moveAcceleration = 0.0f;
        }
    }
    double done = static_cast<double>(m_moveSpeed) * m_frameTime + m_moveDone;
    m_moveDone = static_cast<float>(done);
    Mth::Vector& pos = m_object->m_pos;
    if (done >= m_moveDistance) {
        pos = m_moveTarget;
        if (m_moveArriveNode)
            ArriveAtNode(m_moveArriveNode);
        m_object->m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
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
    SetStepVelocity(m_object, back, m_frameTime);
    return false;
}

// 0x00426E70
void CMotionComponent::UpdatePath()
{
    if (m_object->GetComponent(kPedLogic))
        return;
    float back[4];
    Back(m_object, back);
    UpdatePathSpeed(this);
    if (!(m_pathSpeed == 0.0f)) {
        Mth::Matrix& matrix = m_object->m_matrix;
        const Mth::Vector* along = (m_motionFlags2 & MOTION2_NO_PATH_HEADING) ? nullptr : &matrix.row[2];
        if (mp_pathFollower->Update(static_cast<float>(static_cast<double>(m_frameTime) * m_pathSpeed), along)) {
            m_motionFlags &= ~MOTION_FOLLOWING_PATH;
            m_pathSpeed = 0.0f;
        }
        if (!(m_motionFlags2 & MOTION2_NO_PATH_HEADING)) {
            mp_pathFollower->GetHeading(&matrix.row[2]);
            if ((m_motionFlags2 & MOTION2_LEVEL_HEADING) && !(matrix.row[2].y == 0.0f)) {
                matrix.row[2].y = 0.0f;
                matrix.row[2].Normalize();
            }
            if (!(m_motionFlags2 & MOTION2_STICK_TO_GROUND)) {
                matrix.row[0] = Mth::CrossProduct(matrix.row[1], matrix.row[2]);
                matrix.row[0].Normalize();
                matrix.row[2] = Mth::CrossProduct(matrix.row[0], matrix.row[1]);
                matrix.row[2].Normalize();
            }
        }
        float height = m_object->m_pos.y;
        m_object->m_pos = mp_pathFollower->m_pos;
        if (m_motionFlags2 & MOTION2_STICK_TO_GROUND) {
            m_object->m_pos.y = height;
            StickToGround();
        } else if (m_motionFlags2 & MOTION2_CONSTANT_HEIGHT) {
            m_object->m_pos.y = height;
        }
    }
    SetStepVelocity(m_object, back, m_frameTime);
}

// 0x004246A0
void CMotionComponent::UpdateFollowLeader()
{
    mp_trail->Update();
    float height = m_object->m_pos.y;
    mp_trail->GetPosition(&m_object->m_pos, &m_object->m_matrix);
    if (m_leaveYUnaffected)
        m_object->m_pos.y = height;
}

// --- paths ---

// 0x00423FC0
void CMotionComponent::StartPath(uint32_t node)
{
    if (m_pathGroup)
        m_pathGroup->Remove(m_object);
    m_pathGroup = CPathManager::Instance()->AddToPath(m_object, node);
    InitPathFollower(m_object);
    mp_pathFollower->Start(node, &m_object->m_pos);
}

// 0x00424020
void CMotionComponent::ArriveAtNode(uint32_t node)
{
    Script::CStruct* nodeStruct = SkateScript::GetNode(node, false);
    m_node = node;
    uint32_t script;
    if (nodeStruct->GetChecksum(kSpawnObjScript, &script, 0)) {
        Script::CStruct* params = nullptr;
        nodeStruct->GetStructure(kParamsName, &params, 0);
        m_object->SpawnScript(script, params, 0, false);
        return;
    }
    if (nodeStruct->GetChecksum(kSwitchObjScript, &script, 0)) {
        Script::CStruct* params = nullptr;
        nodeStruct->GetStructure(kParamsName, &params, 0);
        m_object->SwitchScript(script, params);
    }
}

// 0x00424180
void CMotionComponent::FollowPathLinked(Script::CStruct* params, Script::CScript*)
{
    uint32_t node = params->ContainsFlag(kOriginalNode) ? m_firstNode : m_node;
    uint32_t links = SkateScript::GetLinkCount(node);
    if (!links)
        return;
    uint32_t next;
    if (static_cast<int>(links) > 1 && (m_motionFlags2 & MOTION2_RANDOM_LINKS))
        next = SkateScript::GetLink(node, Mth::Rnd(static_cast<int>(links)));
    else
        next = SkateScript::GetLink(node, 0);
    StartPath(next);
    m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_PATH;
}

// 0x004244C0
CCompositeObject* CMotionComponent::GetNextObjOnPath(float range)
{
    if (!m_pathGroup)
        return nullptr;
    CSmtPtr<CObject>* slot = m_pathGroup->m_slots;
    int count = m_pathGroup->m_size;
    CCompositeObject* nearest = nullptr;
    float nearestDistance = 0.0f; // (only read once there is a nearest)
    for (; count > 0; --count, ++slot) {
        auto* other = static_cast<CCompositeObject*>(slot->mp_object);
        if (!other || other == m_object)
            continue;
        const Mth::Vector& at = m_object->m_matrix.row[2];
        const Mth::Vector& otherAt = other->m_matrix.row[2];
        // (facing the same way, and ahead)
        double facing = (static_cast<double>(at.z) * otherAt.z + static_cast<double>(at.y) * otherAt.y) +
                        static_cast<double>(at.x) * otherAt.x;
        if (!(facing >= 0.0))
            continue;
        double ahead = ((static_cast<double>(other->m_pos.z) - m_object->m_pos.z) * at.z +
                        (static_cast<double>(other->m_pos.y) - m_object->m_pos.y) * at.y) +
                       (static_cast<double>(other->m_pos.x) - m_object->m_pos.x) * at.x;
        if (!(ahead >= 0.0))
            continue;
        // (both on the same segment: headed for the same node - and if that's the same, from the same one)
        auto* otherMotion = static_cast<CMotionComponent*>(other->GetComponent(kType));
        if (PathNode(otherMotion) == PathNode(this)) {
            otherMotion = static_cast<CMotionComponent*>(other->GetComponent(kType));
            if (PathLastNode(otherMotion) != PathLastNode(this))
                continue;
        }
        double dx = static_cast<double>(m_object->m_pos.x) - other->m_pos.x;
        double dy = static_cast<double>(m_object->m_pos.y) - other->m_pos.y;
        double dz = static_cast<double>(m_object->m_pos.z) - other->m_pos.z;
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
void CMotionComponent::FollowLeader(Script::CStruct* params)
{
    if (params->ContainsFlag(kOff)) {
        m_motionFlags &= ~MOTION_FOLLOWING_LEADER;
        if (CFollowTrail* trail = mp_trail) {
            // (its destructor: the empty 0x005480A0)
            Mem::Manager::Get()->Free(trail);
            mp_trail = nullptr;
        }
        return;
    }
    m_leaveYUnaffected = params->ContainsFlag("LeaveYUnaffected");
    m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_LEADER;
    static_cast<CLockObjComponent*>(m_object->GetComponent(kLockObj))->m_lockFlags &= ~LOCK_FLAG_LOCKED;
    if (!mp_trail) {
        void* memory = Mem::Calloc(sizeof(CFollowTrail));
        mp_trail = memory ? static_cast<CFollowTrail*>(memory)->Init() : nullptr;
    }
    mp_trail->Reset();
    float distance = 0.0f;
    params->GetFloat(kDistance, &distance, 0);
    mp_trail->m_distance = distance;
    uint32_t leader = 0;
    params->GetChecksum(kName, &leader, 0);
    mp_trail->m_objectId = leader;
    if (params->ContainsFlag(kOrientY))
        mp_trail->m_freeFacing = true;
}

// --- script member functions ---

// 0x00425150
bool CMotionComponent::Obj_GetNextObjOnPath(Script::CStruct* params, Script::CScript* script)
{
    float range = 100.0f;
    params->GetFloat(kRange, &range, 0);
    range = static_cast<float>(static_cast<double>(range) * 12.0f); // (feet)
    script->m_params->RemoveComponent(kOb);
    if (CCompositeObject* next = GetNextObjOnPath(range))
        script->m_params->AddChecksum(kOb, next->m_id);
    return true;
}

// 0x004251C0
bool CMotionComponent::Obj_SetPathTurnDist(Script::CStruct* params, Script::CScript*)
{
    float distance = 0.0f; // (the original's is whatever its stack slot held: only read once found)
    if (params->GetFloat(0u, &distance, 0)) {
        if (!params->ContainsFlag(kInches))
            distance = static_cast<float>(static_cast<double>(distance) * 12.0f);
        InitPathFollower(m_object);
        mp_pathFollower->m_cornerDistance = distance;
    }
    return true;
}

// 0x00425220
bool CMotionComponent::Obj_FollowPath(Script::CStruct* params, Script::CScript*)
{
    m_motionFlags2 &= ~MOTION2_STOPPED_ON_PATH;
    uint32_t node;
    if (params->GetChecksum(kName, &node, 0)) {
        StartPath(node);
        m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_PATH;
    }
    return true;
}

// 0x00425270
bool CMotionComponent::Obj_FollowPathLinked(Script::CStruct* params, Script::CScript* script)
{
    m_motionFlags2 &= ~MOTION2_STOPPED_ON_PATH;
    FollowPathLinked(params, script);
    return true;
}

// 0x00425290
bool CMotionComponent::Obj_FollowPathStored(Script::CStruct*, Script::CScript*)
{
    StartPath(m_storedNode);
    m_motionFlags = (m_motionFlags & ~MOTION_MOVING) | MOTION_FOLLOWING_PATH;
    return true;
}

// 0x004252C0
bool CMotionComponent::Obj_FollowLeader(Script::CStruct* params, Script::CScript*)
{
    FollowLeader(params);
    return true;
}

} // namespace Obj

using Obj::CMotionComponent;
