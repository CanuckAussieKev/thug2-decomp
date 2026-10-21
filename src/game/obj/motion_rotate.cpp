// The motion component (Obj::CMotionComponent, motion.h): turning - about its own axes at a speed (Obj_RotX / Y / Z),
// to an orientation over time (Obj_Rotate's interpolation), to look at points, nodes and objects (Obj_LookAt*), and
// to its node's angles.

#include <cmath>

#include "game/core/manager.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

using namespace MotionDetail;

namespace {

constexpr uint32_t kAngle = 0xFF7EBAF6;          // Angle
constexpr uint32_t kDeceleration = 0xF59FF7D7;   // deceleration
constexpr uint32_t kAcceleration = 0x85894A84;   // Acceleration
constexpr uint32_t kTime = 0x906B67BA;           // time
constexpr uint32_t kAngleThreshold = 0xF73A4EFD; // AngleThreshold
constexpr uint32_t kInstant = 0xF16FB852;        // instant
constexpr uint32_t kAxes = 0xA4AECB20;           // (name unknown): which axes a look-at turns about (bits; default y)
constexpr uint32_t kLinkNum = 0xE85997D0;        // LinkNum
constexpr uint32_t kTypeName = 0x7321A8D6;       // Type
constexpr uint32_t kSkater = 0x5B8AB877;         // skater
constexpr uint32_t kPed = 0x061A741E;            // ped
constexpr uint32_t kGameObj = 0x3B5737A6;        // gameobj
constexpr uint32_t kCar = 0x88C21962;            // Car

// Degrees to radians (0x00645464).
constexpr float kDegrees = Bits(0x3C8EFA35);

// Each axis' value of a per-axis vector.
float& Axis(Mth::Vector& v, int axis) { return (&v.x)[axis]; }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library: not decompiled): out = a * b.
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

// The node after `node` in a manager's object list (null at the end: the head's data is -1).
Mem::ListNode* NextObjectNode(Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}

} // namespace

// --- turning about the axes ---

// 0x00423F10
void CMotionComponent::SetRotationDirection(int axis)
{
    float& speed = Axis(m_rotSpeed, axis);
    float& maxSpeed = Axis(m_rotMaxSpeed, axis);
    float& acceleration = Axis(m_rotAcceleration, axis);
    float& deceleration = Axis(m_rotDeceleration, axis);
    speed = std::fabs(speed);
    maxSpeed = std::fabs(maxSpeed);
    acceleration = std::fabs(acceleration);
    deceleration = std::fabs(deceleration);
    if (Axis(m_rotTarget, axis) < 0.0f) {
        speed = -speed;
        maxSpeed = -maxSpeed;
        acceleration = -acceleration;
        deceleration = -deceleration;
    }
}

// The body of Obj_RotX / RotY / RotZ (0x00424B90 / 0x00424D50 / 0x00424F50: three copies, the axis chosen before).
void CMotionComponent::RotateAboutAxisBy(Script::CStruct* params, int axis, float sign)
{
    float& target = Axis(m_rotTarget, axis);
    float& speed = Axis(m_rotSpeed, axis);
    float& deceleration = Axis(m_rotDeceleration, axis);
    float& acceleration = Axis(m_rotAcceleration, axis);
    uint32_t bit = 1u << axis;
    uint32_t forever = MOTION2_ROTATE_FOREVER_X << axis;
    deceleration = 0.0f;
    acceleration = 0.0f;
    target = 0.0f;
    if (params->GetFloat(kAngle, &target, 0)) {
        target = static_cast<float>(static_cast<double>(target) * kDegrees * sign);
        m_motionFlags2 &= ~forever;
        Axis(m_rotDone, axis) = 0.0f;
        speed = 0.0f;
        if (target == 0.0f) {
            m_motionFlags &= ~bit;
            return;
        }
    }
    float* speedParam;
    if (params->GetFloat(kDeceleration, &deceleration, 0)) {
        deceleration = static_cast<float>(static_cast<double>(deceleration) * kDegrees);
        speedParam = &Axis(m_rotMaxSpeed, axis);
    } else if (params->GetFloat(kAcceleration, &acceleration, 0)) {
        acceleration = static_cast<float>(static_cast<double>(acceleration) * kDegrees);
        float& maxSpeed = Axis(m_rotMaxSpeed, axis);
        if (params->GetFloat(kSpeed, &maxSpeed, 0))
            maxSpeed = static_cast<float>(static_cast<double>(maxSpeed) * kDegrees);
        if (!(m_motionFlags & bit))
            speed = 0.0f;
        speedParam = nullptr;
    } else {
        speedParam = &speed;
    }
    if (speedParam && params->GetFloat(kSpeed, speedParam, 0))
        *speedParam = static_cast<float>(static_cast<double>(*speedParam) * kDegrees);
    // (no angle and no deceleration: round and round)
    if (target == 0.0f && deceleration == 0.0f)
        m_motionFlags2 |= forever;
    if (!(m_motionFlags2 & forever))
        SetRotationDirection(axis);
    m_motionFlags |= bit;
}

// 0x00424B90
bool CMotionComponent::Obj_RotX(Script::CStruct* params, Script::CScript*)
{
    float sign = 1.0f;
    if (m_motionFlags & MOTION_LOOKING_AT)
        m_motionFlags &= ~(MOTION_LOOKING_AT | MOTION_ROTATE_AXES);
    if (params->ContainsFlag(kMaxCoords))
        sign = -1.0f;
    RotateAboutAxisBy(params, 0, sign);
    return true;
}

// 0x00424D50
bool CMotionComponent::Obj_RotY(Script::CStruct* params, Script::CScript*)
{
    int axis = 1;
    if (m_motionFlags & MOTION_LOOKING_AT)
        m_motionFlags &= ~(MOTION_LOOKING_AT | MOTION_ROTATE_AXES);
    if (params->ContainsFlag(kMaxCoords))
        axis = 2;
    RotateAboutAxisBy(params, axis, 1.0f);
    return true;
}

// 0x00424F50
bool CMotionComponent::Obj_RotZ(Script::CStruct* params, Script::CScript*)
{
    int axis = 2;
    if (m_motionFlags & MOTION_LOOKING_AT)
        m_motionFlags &= ~(MOTION_LOOKING_AT | MOTION_ROTATE_AXES);
    if (params->ContainsFlag(kMaxCoords))
        axis = 1;
    RotateAboutAxisBy(params, axis, 1.0f);
    return true;
}

// 0x004258B0
void CMotionComponent::RotateAboutAxis(int axis, float angle)
{
    Mth::Matrix rotation;
    Mth::Matrix& matrix = m_object->m_matrix;
    switch (axis) {
    case 0:
        MatrixMultiply(&matrix, &Mth::CreateRotateXMatrix(rotation, angle), &matrix);
        matrix.OrthoNormalizeAbout(0);
        break;
    case 1:
        MatrixMultiply(&matrix, &Mth::CreateRotateYMatrix(rotation, angle), &matrix);
        matrix.OrthoNormalizeAbout(1);
        break;
    case 2:
        MatrixMultiply(&matrix, &Mth::CreateRotateZMatrix(rotation, angle), &matrix);
        matrix.OrthoNormalizeAbout(2);
        break;
    default:
        break;
    }
}

// 0x00425A80
bool CMotionComponent::UpdateAxisRotation(int axis)
{
    float& speed = Axis(m_rotSpeed, axis);
    float& maxSpeed = Axis(m_rotMaxSpeed, axis);
    float& acceleration = Axis(m_rotAcceleration, axis);
    float& deceleration = Axis(m_rotDeceleration, axis);
    bool finished = false;
    if (!(acceleration == 0.0f)) {
        speed = static_cast<float>(static_cast<double>(m_frameTime) * acceleration + speed);
        // (past the speed to reach, going its way: that speed)
        if (std::fabs(maxSpeed) < std::fabs(speed) && (speed > 0.0f) == (maxSpeed > 0.0f))
            speed = maxSpeed;
    }
    if (!(deceleration == 0.0f)) {
        double slowing = static_cast<double>(m_frameTime) * deceleration;
        if (std::fabs(static_cast<double>(speed) - maxSpeed) < std::fabs(slowing)) {
            // (down to the speed to reach: done slowing - and, at a standstill, done turning; no turn this frame)
            speed = maxSpeed;
            deceleration = 0.0f;
            return maxSpeed == 0.0f;
        }
        speed = static_cast<float>(static_cast<double>(speed) - slowing);
    }
    float step = static_cast<float>(static_cast<double>(speed) * m_frameTime);
    if (!(m_motionFlags2 & (MOTION2_ROTATE_FOREVER_X << axis))) {
        float target = Axis(m_rotTarget, axis);
        float& done = Axis(m_rotDone, axis);
        bool before = target > done;
        done = static_cast<float>(static_cast<double>(step) + done);
        if (before != (target > done)) {
            // (past the target: only as far as it)
            finished = true;
            step = static_cast<float>(static_cast<double>(step) - (static_cast<double>(done) - target));
        }
    }
    RotateAboutAxis(axis, step);
    return finished;
}

// 0x00425C70
void CMotionComponent::UpdateRotations()
{
    for (int axis = 0; axis < 3; ++axis) {
        uint32_t bit = 1u << axis;
        if ((m_motionFlags & bit) && UpdateAxisRotation(axis))
            m_motionFlags &= ~bit;
    }
}

// --- Obj_Rotate's turn ---

// 0x00425520
void CMotionComponent::StartRotation(Script::CStruct* params, const Mth::Matrix* target)
{
    if (!mp_turn) {
        if (!GameVariable<bool>(0x007CC979)) // (the memory manager not set up yet)
            Mem::EnsureManager();
        // (its constructor, 0x005902E0, does nothing)
        mp_turn = static_cast<Mth::SlerpInterpolator*>(Mem::Manager::Get()->Allocate(0x98, 1, nullptr));
    }
    Mth::Matrix end;
    end = *target;
    mp_turn->SetMatrices(m_object->m_matrix, end);
    m_turnElapsed = 0.0f;
    m_turnTime = 0.0f;
    params->GetFloat(kTime, &m_turnTime, 0);
    m_motionFlags |= MOTION_TURNING;
}

// 0x00424440
void CMotionComponent::UpdateTurn()
{
    float elapsed = static_cast<float>(static_cast<double>(m_frameTime) + m_turnElapsed);
    m_turnElapsed = elapsed;
    float t;
    if (!(m_turnTime == 0.0f) && !(elapsed >= m_turnTime)) {
        t = static_cast<float>(static_cast<double>(elapsed) / m_turnTime);
    } else {
        t = 1.0f;
        m_motionFlags &= ~MOTION_TURNING;
    }
    mp_turn->GetMatrix(m_object->m_matrix, t);
}

// --- looking at things ---

// 0x004240E0
bool CMotionComponent::SetupLookAxis(const Mth::Vector* target, const Mth::Vector* from, int referenceAxis, int axis,
                                     float threshold)
{
    Mth::Vector offset = {static_cast<float>(static_cast<double>(target->x) - from->x),
                          static_cast<float>(static_cast<double>(target->y) - from->y),
                          static_cast<float>(static_cast<double>(target->z) - from->z),
                          static_cast<float>(static_cast<double>(target->w) - from->w)};
    Axis(m_rotDone, axis) = 0.0f;
    float angle = Mth::GetAngleInPlane(m_object->m_matrix, offset, referenceAxis, axis);
    Axis(m_rotTarget, axis) = angle;
    if (std::fabs(angle) > threshold) {
        m_motionFlags |= 1u << axis;
        return true;
    }
    return false;
}

// 0x004252E0
bool CMotionComponent::LookAt(Script::CStruct* params, const Mth::Vector* target)
{
    float threshold = 0.0f;
    params->GetFloat(kAngleThreshold, &threshold, 0);
    threshold = static_cast<float>(static_cast<double>(threshold) * kDegrees);
    if (params->ContainsFlag(kInstant)) {
        const Mth::Vector& pos = m_object->m_pos;
        Mth::Vector offset = {static_cast<float>(static_cast<double>(target->x) - pos.x),
                              static_cast<float>(static_cast<double>(target->y) - pos.y),
                              static_cast<float>(static_cast<double>(target->z) - pos.z),
                              static_cast<float>(static_cast<double>(target->w) - pos.w)};
        float angle = Mth::GetAngleInPlane(m_object->m_matrix, offset, 2, 1);
        if (!(std::fabs(angle) > threshold))
            return false;
        Mth::Matrix rotation;
        Mth::Matrix& matrix = m_object->m_matrix;
        MatrixMultiply(&matrix, &Mth::CreateRotateYMatrix(rotation, angle), &matrix);
        return true;
    }
    int speed = 360;
    int acceleration = 0;
    int axes = 2; // (y)
    float time = 0.0f;
    if (!(m_motionFlags & MOTION_LOOKING_AT)) {
        m_motionFlags = (m_motionFlags & ~MOTION_ROTATE_AXES) | MOTION_LOOKING_AT;
        m_motionFlags2 &= ~(MOTION2_ROTATE_FOREVER_X * 7);
    }
    params->GetFloat(kTime, &time, 0);
    if (time == 0.0f) {
        params->GetInteger(kSpeed, &speed, 0);
        params->GetInteger(kAcceleration, &acceleration, 0);
        params->GetInteger(kAxes, &axes, 0);
    }
    bool turning = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!(axes & (1 << axis)))
            continue;
        if (!SetupLookAxis(target, &m_object->m_pos, (axis != 2) + 1, axis, threshold))
            continue;
        if (!(time == 0.0f)) {
            Axis(m_rotAcceleration, axis) = 0.0f;
            Axis(m_rotSpeed, axis) = static_cast<float>(static_cast<double>(Axis(m_rotTarget, axis)) / time);
        } else {
            double rate = static_cast<double>(acceleration) * kDegrees;
            Axis(m_rotAcceleration, axis) = static_cast<float>(rate);
            if (rate == 0.0) {
                Axis(m_rotSpeed, axis) = static_cast<float>(static_cast<double>(speed) * kDegrees);
            } else {
                Axis(m_rotSpeed, axis) = 0.0f;
                Axis(m_rotMaxSpeed, axis) = 0.0f;
            }
            SetRotationDirection(axis);
        }
        turning = true;
    }
    return turning;
}

// 0x00425710
void CMotionComponent::LookAtNodeLinked(Script::CStruct* params, Script::CScript*)
{
    int links = static_cast<int>(SkateScript::GetLinkCount(m_node));
    if (!links)
        return;
    int link = 1;
    params->GetInteger(kLinkNum, &link, 0);
    link -= 1;
    if (link < 0 || link >= links)
        link = 0;
    Mth::Vector pos;
    SkateScript::GetPosition(SkateScript::GetLinkedNode(m_node, link), &pos);
    LookAt(params, &pos);
}

// 0x004257A0
void CMotionComponent::LookAtNode(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(kName, &name, 0);
    if (!name)
        return;
    Mth::Vector pos;
    SkateScript::GetPosition(SkateScript::GetNode(name, true), &pos);
    LookAt(params, &pos);
}

// 0x00425800
void CMotionComponent::LookAtPosition(Script::CStruct* params, bool absolute)
{
    Mth::Vector pos = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0u, reinterpret_cast<Script::VectorValue*>(&pos), 0);
    if (params->ContainsFlag(kMaxCoords))
        pos.NegateXRotateX90();
    if (!absolute) {
        const Mth::Vector& at = m_object->m_pos;
        pos = {pos.x + at.x, pos.y + at.y, pos.z + at.z, pos.w + at.w};
    }
    LookAt(params, &pos);
}

// 0x00427500
bool CMotionComponent::LookAtObject(Script::CStruct* params, Script::CScript*)
{
    uint32_t type;
    if (params->GetChecksum(kTypeName, &type, 0)) {
        uint32_t objectType;
        switch (type) {
        case kSkater:
            objectType = 0xBAF9B341; // OBJECT_TYPE_SKATER
            break;
        case kPed:
            objectType = 0xD2DB7F97; // OBJECT_TYPE_PED
            break;
        case kGameObj:
            objectType = 0xBC0D91D8; // OBJECT_TYPE_GAME_OBJ
            break;
        case kCar:
            objectType = 0x5C0312EB; // OBJECT_TYPE_CAR
            break;
        default:
            return false;
        }
        CCompositeObject* other = m_object->FindNearestObjectOfType(objectType);
        if (!other)
            return false;
        return LookAt(params, &other->m_pos);
    }
    uint32_t name;
    if (!params->GetChecksum(kName, &name, 0))
        return false;
    // (the object of that id in its manager's list, not itself)
    auto* list = reinterpret_cast<Mem::ListNode*(__thiscall*)(CBaseManager*)>(m_object->mp_manager->m_vtable[6])(
        m_object->mp_manager);
    Mem::ListNode* node = NextObjectNode(list);
    auto* object = node ? static_cast<CObject*>(node->data) : nullptr;
    while (object) {
        CObject* current = object;
        node = NextObjectNode(node);
        object = node ? static_cast<CObject*>(node->data) : nullptr;
        if (current != m_object && current->m_id == name)
            return LookAt(params, &static_cast<CCompositeObject*>(current)->m_pos);
    }
    return false;
}

// 0x00425970
void CMotionComponent::OrientToNode(Script::CStruct* node)
{
    Mth::Matrix& matrix = m_object->m_matrix;
    matrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    matrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector angles;
    SkateScript::GetAngles(node, &angles);
    if (!(angles.x == 0.0f) || !(angles.y == 0.0f) || !(angles.z == 0.0f)) {
        Mth::Matrix rotation;
        MatrixMultiply(&matrix, &matrix, &Mth::CreateRotateXMatrix(rotation, angles.x));
        MatrixMultiply(&matrix, &matrix, &Mth::CreateRotateYMatrix(rotation, angles.y));
        MatrixMultiply(&matrix, &matrix, &Mth::CreateRotateZMatrix(rotation, angles.z));
    }
    m_object->m_displayMatrix = matrix;
}

// --- script member functions ---

// 0x00426770
bool CMotionComponent::Obj_Rotate(Script::CStruct* params, Script::CScript*)
{
    RotateTo(params);
    return true;
}

// 0x00426790
bool CMotionComponent::Obj_LookAtNode(Script::CStruct* params, Script::CScript* script)
{
    LookAtNode(params, script);
    return true;
}

// 0x004267B0
bool CMotionComponent::Obj_LookAtNodeLinked(Script::CStruct* params, Script::CScript* script)
{
    LookAtNodeLinked(params, script);
    return true;
}

// 0x004267D0
bool CMotionComponent::Obj_LookAtNodeStored(Script::CStruct* params, Script::CScript*)
{
    Mth::Vector pos;
    SkateScript::GetPosition(m_storedNode, &pos);
    LookAt(params, &pos);
    return true;
}

// 0x00426810 (LookAtPosition's absolute case again, as its own code)
bool CMotionComponent::Obj_LookAtPos(Script::CStruct* params, Script::CScript*)
{
    Mth::Vector pos = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0u, reinterpret_cast<Script::VectorValue*>(&pos), 0);
    if (params->ContainsFlag(kMaxCoords))
        pos.NegateXRotateX90();
    LookAt(params, &pos);
    return true;
}

// 0x00426880
bool CMotionComponent::Obj_LookAtPosStored(Script::CStruct* params, Script::CScript*)
{
    LookAt(params, &m_storedPos);
    return true;
}

// 0x004268A0
bool CMotionComponent::Obj_LookAtRelPos(Script::CStruct* params, Script::CScript*)
{
    LookAtPosition(params, false);
    return true;
}

// 0x004276A0
bool CMotionComponent::Obj_LookAtObject(Script::CStruct* params, Script::CScript* script)
{
    return LookAtObject(params, script);
}

} // namespace Obj

using Obj::CMotionComponent;

REPLACE(0x00423F10, &CMotionComponent::SetRotationDirection);
REPLACE(0x00424B90, &CMotionComponent::Obj_RotX);
REPLACE(0x00424D50, &CMotionComponent::Obj_RotY);
REPLACE(0x00424F50, &CMotionComponent::Obj_RotZ);
REPLACE(0x004258B0, &CMotionComponent::RotateAboutAxis);
REPLACE(0x00425A80, &CMotionComponent::UpdateAxisRotation);
REPLACE(0x00425C70, &CMotionComponent::UpdateRotations);
REPLACE(0x00425520, &CMotionComponent::StartRotation);
REPLACE(0x00424440, &CMotionComponent::UpdateTurn);
REPLACE(0x004240E0, &CMotionComponent::SetupLookAxis);
REPLACE(0x004252E0, &CMotionComponent::LookAt);
REPLACE(0x00425710, &CMotionComponent::LookAtNodeLinked);
REPLACE(0x004257A0, &CMotionComponent::LookAtNode);
REPLACE(0x00425800, &CMotionComponent::LookAtPosition);
REPLACE(0x00427500, &CMotionComponent::LookAtObject);
REPLACE(0x00425970, &CMotionComponent::OrientToNode);
REPLACE(0x00426770, &CMotionComponent::Obj_Rotate);
REPLACE(0x00426790, &CMotionComponent::Obj_LookAtNode);
REPLACE(0x004267B0, &CMotionComponent::Obj_LookAtNodeLinked);
REPLACE(0x004267D0, &CMotionComponent::Obj_LookAtNodeStored);
REPLACE(0x00426810, &CMotionComponent::Obj_LookAtPos);
REPLACE(0x00426880, &CMotionComponent::Obj_LookAtPosStored);
REPLACE(0x004268A0, &CMotionComponent::Obj_LookAtRelPos);
REPLACE(0x004276A0, &CMotionComponent::Obj_LookAtObject);
