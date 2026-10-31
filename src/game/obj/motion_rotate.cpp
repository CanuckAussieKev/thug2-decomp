// The motion component (GameObjects::CKinematicsModule, motion.h): turning - about its own axes at a speed (Obj_RotX / Y / Z),
// to an orientation over time (Obj_Rotate's interpolation), to look at points, nodes and objects (Obj_LookAt*), and
// to its node's angles.

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/gfx/d3dx.h"
#include "game/maths/mat4.h"
#include "game/maths/rotation_blend.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/obj/entity_hub.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"

namespace GameObjects {

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
float& Axis(Maths::Vec4& v, int axis) { return (&v.x)[axis]; }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library: not decompiled): out = a * b.
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The node after `node` in a manager's object list (null at the end: the head's data is -1).
MemSys::ListNode* NextObjectNode(MemSys::ListNode* node)
{
    MemSys::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}

} // namespace

// --- turning about the axes ---

// 0x00423F10
void CKinematicsModule::SetRotationDirection(int axis)
{
    float& speed = Axis(m_spinRates, axis);
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
void CKinematicsModule::RotateAboutAxisBy(Scripts::CStruct* params, int axis, float sign)
{
    float& target = Axis(m_rotTarget, axis);
    float& speed = Axis(m_spinRates, axis);
    float& deceleration = Axis(m_rotDeceleration, axis);
    float& acceleration = Axis(m_rotAcceleration, axis);
    uint32_t bit = 1u << axis;
    uint32_t forever = MOTION2_ROTATE_FOREVER_X << axis;
    deceleration = 0.0f;
    acceleration = 0.0f;
    target = 0.0f;
    if (params->FetchFloat(kAngle, &target, 0)) {
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
    if (params->FetchFloat(kDeceleration, &deceleration, 0)) {
        deceleration = static_cast<float>(static_cast<double>(deceleration) * kDegrees);
        speedParam = &Axis(m_rotMaxSpeed, axis);
    } else if (params->FetchFloat(kAcceleration, &acceleration, 0)) {
        acceleration = static_cast<float>(static_cast<double>(acceleration) * kDegrees);
        float& maxSpeed = Axis(m_rotMaxSpeed, axis);
        if (params->FetchFloat(kSpeed, &maxSpeed, 0))
            maxSpeed = static_cast<float>(static_cast<double>(maxSpeed) * kDegrees);
        if (!(m_motionFlags & bit))
            speed = 0.0f;
        speedParam = nullptr;
    } else {
        speedParam = &speed;
    }
    if (speedParam && params->FetchFloat(kSpeed, speedParam, 0))
        *speedParam = static_cast<float>(static_cast<double>(*speedParam) * kDegrees);
    // (no angle and no deceleration: round and round)
    if (target == 0.0f && deceleration == 0.0f)
        m_motionFlags2 |= forever;
    if (!(m_motionFlags2 & forever))
        SetRotationDirection(axis);
    m_motionFlags |= bit;
}

// 0x00424B90
bool CKinematicsModule::Obj_RotX(Scripts::CStruct* params, Scripts::CScript*)
{
    float sign = 1.0f;
    if (m_motionFlags & MOTION_LOOKING_AT)
        m_motionFlags &= ~(MOTION_LOOKING_AT | MOTION_ROTATE_AXES);
    if (params->HasBareFlag(kMaxCoords))
        sign = -1.0f;
    RotateAboutAxisBy(params, 0, sign);
    return true;
}

// 0x00424D50
bool CKinematicsModule::Obj_RotY(Scripts::CStruct* params, Scripts::CScript*)
{
    int axis = 1;
    if (m_motionFlags & MOTION_LOOKING_AT)
        m_motionFlags &= ~(MOTION_LOOKING_AT | MOTION_ROTATE_AXES);
    if (params->HasBareFlag(kMaxCoords))
        axis = 2;
    RotateAboutAxisBy(params, axis, 1.0f);
    return true;
}

// 0x00424F50
bool CKinematicsModule::Obj_RotZ(Scripts::CStruct* params, Scripts::CScript*)
{
    int axis = 2;
    if (m_motionFlags & MOTION_LOOKING_AT)
        m_motionFlags &= ~(MOTION_LOOKING_AT | MOTION_ROTATE_AXES);
    if (params->HasBareFlag(kMaxCoords))
        axis = 1;
    RotateAboutAxisBy(params, axis, 1.0f);
    return true;
}

// 0x004258B0
void CKinematicsModule::RotateAboutAxis(int axis, float angle)
{
    Maths::Mat4 rotation;
    Maths::Mat4& matrix = m_entity->m_basisFrame;
    switch (axis) {
    case 0:
        MatrixMultiply(&matrix, &Maths::MakePitchMatrix(rotation, angle), &matrix);
        matrix.SquareUpAxes(0);
        break;
    case 1:
        MatrixMultiply(&matrix, &Maths::MakeYawMatrix(rotation, angle), &matrix);
        matrix.SquareUpAxes(1);
        break;
    case 2:
        MatrixMultiply(&matrix, &Maths::MakeRollMatrix(rotation, angle), &matrix);
        matrix.SquareUpAxes(2);
        break;
    default:
        break;
    }
}

// 0x00425A80
bool CKinematicsModule::UpdateAxisRotation(int axis)
{
    float& speed = Axis(m_spinRates, axis);
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
void CKinematicsModule::UpdateRotations()
{
    for (int axis = 0; axis < 3; ++axis) {
        uint32_t bit = 1u << axis;
        if ((m_motionFlags & bit) && UpdateAxisRotation(axis))
            m_motionFlags &= ~bit;
    }
}

// --- Obj_Rotate's turn ---

// 0x00425520
void CKinematicsModule::StartRotation(Scripts::CStruct* params, const Maths::Mat4* target)
{
    if (!mp_turn) {
        if (!MemSys::g_managerReady) // (the memory manager not set up yet)
            MemSys::EnsureManager();
        // (its constructor, 0x005902E0, does nothing)
        mp_turn = static_cast<Maths::ArcRotationBlender*>(MemSys::Hub::Pull()->Carve(0x98, 1, nullptr));
    }
    Maths::Mat4 end;
    end = *target;
    mp_turn->SetMatrices(m_entity->m_basisFrame, end);
    m_turnElapsed = 0.0f;
    m_turnTime = 0.0f;
    params->FetchFloat(kTime, &m_turnTime, 0);
    m_motionFlags |= MOTION_TURNING;
}

// 0x00424440
void CKinematicsModule::UpdateTurn()
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
    mp_turn->FetchMat(m_entity->m_basisFrame, t);
}

// --- looking at things ---

// 0x004240E0
bool CKinematicsModule::SetupLookAxis(const Maths::Vec4* target, const Maths::Vec4* from, int referenceAxis, int axis,
                                     float threshold)
{
    Maths::Vec4 offset = {static_cast<float>(static_cast<double>(target->x) - from->x),
                          static_cast<float>(static_cast<double>(target->y) - from->y),
                          static_cast<float>(static_cast<double>(target->z) - from->z),
                          static_cast<float>(static_cast<double>(target->w) - from->w)};
    Axis(m_rotDone, axis) = 0.0f;
    float angle = Maths::GetAngleInPlane(m_entity->m_basisFrame, offset, referenceAxis, axis);
    Axis(m_rotTarget, axis) = angle;
    if (std::fabs(angle) > threshold) {
        m_motionFlags |= 1u << axis;
        return true;
    }
    return false;
}

// 0x004252E0
bool CKinematicsModule::LookAt(Scripts::CStruct* params, const Maths::Vec4* target)
{
    float threshold = 0.0f;
    params->FetchFloat(kAngleThreshold, &threshold, 0);
    threshold = static_cast<float>(static_cast<double>(threshold) * kDegrees);
    if (params->HasBareFlag(kInstant)) {
        const Maths::Vec4& pos = m_entity->m_atPoint;
        Maths::Vec4 offset = {static_cast<float>(static_cast<double>(target->x) - pos.x),
                              static_cast<float>(static_cast<double>(target->y) - pos.y),
                              static_cast<float>(static_cast<double>(target->z) - pos.z),
                              static_cast<float>(static_cast<double>(target->w) - pos.w)};
        float angle = Maths::GetAngleInPlane(m_entity->m_basisFrame, offset, 2, 1);
        if (!(std::fabs(angle) > threshold))
            return false;
        Maths::Mat4 rotation;
        Maths::Mat4& matrix = m_entity->m_basisFrame;
        MatrixMultiply(&matrix, &Maths::MakeYawMatrix(rotation, angle), &matrix);
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
    params->FetchFloat(kTime, &time, 0);
    if (time == 0.0f) {
        params->FetchIntValue(kSpeed, &speed, 0);
        params->FetchIntValue(kAcceleration, &acceleration, 0);
        params->FetchIntValue(kAxes, &axes, 0);
    }
    bool turning = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!(axes & (1 << axis)))
            continue;
        if (!SetupLookAxis(target, &m_entity->m_atPoint, (axis != 2) + 1, axis, threshold))
            continue;
        if (!(time == 0.0f)) {
            Axis(m_rotAcceleration, axis) = 0.0f;
            Axis(m_spinRates, axis) = static_cast<float>(static_cast<double>(Axis(m_rotTarget, axis)) / time);
        } else {
            double rate = static_cast<double>(acceleration) * kDegrees;
            Axis(m_rotAcceleration, axis) = static_cast<float>(rate);
            if (rate == 0.0) {
                Axis(m_spinRates, axis) = static_cast<float>(static_cast<double>(speed) * kDegrees);
            } else {
                Axis(m_spinRates, axis) = 0.0f;
                Axis(m_rotMaxSpeed, axis) = 0.0f;
            }
            SetRotationDirection(axis);
        }
        turning = true;
    }
    return turning;
}

// 0x00425710
void CKinematicsModule::LookAtNodeLinked(Scripts::CStruct* params, Scripts::CScript*)
{
    int links = static_cast<int>(LevelNodes::GetLinkCount(m_listLink));
    if (!links)
        return;
    int link = 1;
    params->FetchIntValue(kLinkNum, &link, 0);
    link -= 1;
    if (link < 0 || link >= links)
        link = 0;
    Maths::Vec4 pos;
    LevelNodes::FetchPlace(LevelNodes::FetchNeighbourNode(m_listLink, link), &pos);
    LookAt(params, &pos);
}

// 0x004257A0
void CKinematicsModule::LookAtNode(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, 0);
    if (!name)
        return;
    Maths::Vec4 pos;
    LevelNodes::FetchPlace(LevelNodes::FetchVertex(name, true), &pos);
    LookAt(params, &pos);
}

// 0x00425800
void CKinematicsModule::LookAtPosition(Scripts::CStruct* params, bool absolute)
{
    Maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&pos), 0);
    if (params->HasBareFlag(kMaxCoords))
        pos.NegateXRotateX90();
    if (!absolute) {
        const Maths::Vec4& at = m_entity->m_atPoint;
        pos = {pos.x + at.x, pos.y + at.y, pos.z + at.z, pos.w + at.w};
    }
    LookAt(params, &pos);
}

// 0x00427500
bool CKinematicsModule::LookAtObject(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t type;
    if (params->ReadHashValue(kTypeName, &type, 0)) {
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
        CGameEntity* other = m_entity->FindNearestObjectOfType(objectType);
        if (!other)
            return false;
        return LookAt(params, &other->m_atPoint);
    }
    uint32_t name;
    if (!params->ReadHashValue(kName, &name, 0))
        return false;
    // (the object of that id in its manager's list, not itself)
    auto* list = reinterpret_cast<MemSys::ListNode*(__thiscall*)(CObjectHubBase*)>(m_entity->m_parkOwner->m_vtable[6])(
        m_entity->m_parkOwner);
    MemSys::ListNode* node = NextObjectNode(list);
    auto* object = node ? static_cast<CEntity*>(node->data) : nullptr;
    while (object) {
        CEntity* current = object;
        node = NextObjectNode(node);
        object = node ? static_cast<CEntity*>(node->data) : nullptr;
        if (current != m_entity && current->m_ident == name)
            return LookAt(params, &static_cast<CGameEntity*>(current)->m_atPoint);
    }
    return false;
}

// 0x00425970
void CKinematicsModule::FaceNode(Scripts::CStruct* node)
{
    Maths::Mat4& matrix = m_entity->m_basisFrame;
    matrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    matrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Vec4 angles;
    LevelNodes::ReadTurnAngles(node, &angles);
    if (!(angles.x == 0.0f) || !(angles.y == 0.0f) || !(angles.z == 0.0f)) {
        Maths::Mat4 rotation;
        MatrixMultiply(&matrix, &matrix, &Maths::MakePitchMatrix(rotation, angles.x));
        MatrixMultiply(&matrix, &matrix, &Maths::MakeYawMatrix(rotation, angles.y));
        MatrixMultiply(&matrix, &matrix, &Maths::MakeRollMatrix(rotation, angles.z));
    }
    m_entity->m_displayMatrix = matrix;
}

// --- script member functions ---

// 0x00426770
bool CKinematicsModule::Obj_Rotate(Scripts::CStruct* params, Scripts::CScript*)
{
    RotateTo(params);
    return true;
}

// 0x00426790
bool CKinematicsModule::Obj_LookAtNode(Scripts::CStruct* params, Scripts::CScript* script)
{
    LookAtNode(params, script);
    return true;
}

// 0x004267B0
bool CKinematicsModule::Obj_LookAtNodeLinked(Scripts::CStruct* params, Scripts::CScript* script)
{
    LookAtNodeLinked(params, script);
    return true;
}

// 0x004267D0
bool CKinematicsModule::Obj_LookAtNodeStored(Scripts::CStruct* params, Scripts::CScript*)
{
    Maths::Vec4 pos;
    LevelNodes::FetchPlace(m_savedNodeId, &pos);
    LookAt(params, &pos);
    return true;
}

// 0x00426810 (LookAtPosition's absolute case again, as its own code)
bool CKinematicsModule::Obj_LookAtPos(Scripts::CStruct* params, Scripts::CScript*)
{
    Maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&pos), 0);
    if (params->HasBareFlag(kMaxCoords))
        pos.NegateXRotateX90();
    LookAt(params, &pos);
    return true;
}

// 0x00426880
bool CKinematicsModule::Obj_LookAtPosStored(Scripts::CStruct* params, Scripts::CScript*)
{
    LookAt(params, &m_storedPos);
    return true;
}

// 0x004268A0
bool CKinematicsModule::Obj_LookAtRelPos(Scripts::CStruct* params, Scripts::CScript*)
{
    LookAtPosition(params, false);
    return true;
}

// 0x004276A0
bool CKinematicsModule::Obj_LookAtObject(Scripts::CStruct* params, Scripts::CScript* script)
{
    return LookAtObject(params, script);
}

} // namespace Obj

using GameObjects::CKinematicsModule;
