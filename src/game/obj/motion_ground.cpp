// The motion component (Obj::CMotionComponent, motion.h): jumping, sticking to the ground, and moving to points
// (Obj_MoveTo*, Obj_MoveForward / Left, Obj_SkipToRestart).

#include <cmath>

#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_tests.h"
#include "game/nx/coll_tri.h"
#include "game/nx/feeler.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

using namespace MotionDetail;

namespace {

constexpr uint32_t kDistAbove = 0xA8A5A3C7;  // DistAbove
constexpr uint32_t kDistBelow = 0x182D87A7;  // DistBelow
constexpr uint32_t kTime = 0x906B67BA;       // time
constexpr uint32_t kAcceleration = 0x85894A84; // Acceleration
constexpr uint32_t kOrient = 0x90A91232;     // orient
constexpr uint32_t kDist = 0x7E832F08;       // dist
constexpr uint32_t kLinkNum = 0xE85997D0;    // LinkNum
constexpr uint32_t kRandomLink = 0x90A1C52A; // randomlink
constexpr uint32_t kModel = 0x286A8D26;      // the model component

// Miles per hour to inches per second (63360 inches a mile, 1 / 3600 hours a second: 0x006461E4, 0x006461E0).
constexpr float kInchesPerMile = Bits(0x47778000);
constexpr float kHoursPerSecond = Bits(0x3991A2B4);

// A vertex of a collision object (its vtable slot 9).
Mth::Vector VertexPos(Nx::CCollObj* object, int vertex)
{
    Mth::Vector pos;
    reinterpret_cast<Mth::Vector*(__thiscall*)(Nx::CCollObj*, Mth::Vector*, int)>(
        (*reinterpret_cast<void***>(object))[9])(object, &pos, vertex);
    return pos;
}

} // namespace

// 0x00425CB0
bool __cdecl GroundHit(Mth::Vector* triangle, const Mth::Vector* end, const Mth::Vector* start, Mth::Vector* out)
{
    Mth::Vector along = {static_cast<float>(static_cast<double>(end->x) - start->x),
                         static_cast<float>(static_cast<double>(end->y) - start->y),
                         static_cast<float>(static_cast<double>(end->z) - start->z),
                         static_cast<float>(static_cast<double>(end->w) - start->w)};
    float t = 0.0f;
    if (Nx::RayHitsTriangle(*start, along, triangle[0], triangle[1], triangle[2], &t)) {
        // (x through the unrounded product, the others rounded first)
        double x = static_cast<double>(along.x) * t;
        float y = static_cast<float>(static_cast<double>(along.y) * t);
        float z = static_cast<float>(static_cast<double>(along.z) * t);
        float w = static_cast<float>(static_cast<double>(along.w) * t);
        out->z = static_cast<float>(static_cast<double>(z) + start->z);
        out->w = static_cast<float>(static_cast<double>(w) + start->w);
        out->x = static_cast<float>(x + start->x);
        out->y = static_cast<float>(static_cast<double>(y) + start->y);
        return true;
    }
    Nx::CFeeler feeler;
    feeler.Construct();
    if (!feeler.GetCollisionAlong(start, end, false))
        return false;
    // (the face hit becomes the triangle tried first next time)
    Nx::CCollObj* object = feeler.m_hit.object;
    Nx::CCollObjTriData* geometry = object->m_geometry;
    int face = static_cast<int>(feeler.m_hit.face);
    for (int corner = 0; corner < 3; ++corner) {
        int vertex;
        if (geometry->m_smallFaces)
            vertex = geometry->m_faces[face * 8 + 4 + corner];
        else
            vertex = *reinterpret_cast<const uint16_t*>(geometry->m_faces + face * 10 + 4 + 2 * corner);
        triangle[corner] = VertexPos(object, vertex);
    }
    *out = feeler.m_point;
    return true;
}

// The original's convention: ECX the triangle, EDX the line's end; the start and the result on the stack (caller
// pops); the result in AL.
__declspec(naked) void GroundHitStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edx
        push ecx
        call GroundHit
        add esp, 16
        ret
    }
}

// 0x00424870
void CMotionComponent::UpdateJump()
{
    const Mth::Vector& pos = m_object->m_pos;
    float x = pos.x;
    float z = pos.z;
    float w = pos.w;
    double fall = static_cast<double>(m_jumpGravity) * m_frameTime;
    double rise = static_cast<double>(m_jumpSpeed) * m_frameTime + fall * m_frameTime * 0.5;
    if (m_jumpHasHeading) {
        // (along the heading at the start's speed: x and z through rounded products, w through an unrounded one)
        float frame = m_frameTime;
        double speed = m_jumpStartSpeed;
        double alongW = speed * m_jumpHeading.w * frame;
        float alongZ = static_cast<float>(speed * m_jumpHeading.z);
        float alongX = static_cast<float>(speed * m_jumpHeading.x);
        x = static_cast<float>(static_cast<double>(alongX) * frame + x);
        z = static_cast<float>(static_cast<double>(z) + static_cast<double>(alongZ) * frame);
        w = static_cast<float>(static_cast<double>(w) + alongW);
    }
    float y = static_cast<float>(static_cast<double>(pos.y) + rise);
    m_jumpSpeed = static_cast<float>(fall + m_jumpSpeed);
    if (rise < 0.0) {
        // (coming down: landed at land_height, or on whatever is below)
        if (m_jumpHasLandHeight) {
            if (y < m_jumpLandHeight) {
                m_motionFlags &= ~MOTION_JUMPING;
                y = m_jumpLandHeight;
            }
        } else {
            Nx::CFeeler feeler;
            feeler.Construct();
            float above = static_cast<float>(static_cast<double>(w) + 1.0f);
            feeler.m_line.start = {x, static_cast<float>(static_cast<double>(y) + m_jumpColAbove), z, above};
            feeler.m_line.end = {x, static_cast<float>(static_cast<double>(y) + -m_jumpColBelow), z, above};
            if (feeler.GetCollision(true, false))
                m_motionFlags &= ~MOTION_JUMPING;
        }
    }
    m_jumpPos = {x, y, z, w};
}

// 0x00425F40
bool CMotionComponent::StickToGround()
{
    if (m_groundAbove == 0.0f && m_groundBelow == 0.0f)
        return false;
    Mth::Matrix& matrix = m_object->m_matrix;
    Mth::Vector& pos = m_object->m_pos;
    if (m_groundHeightOnly) {
        // The right axis from up and at, then the ground under it: the height to make up (Update eases it in).
        const Mth::Vector& up = matrix.row[1];
        const Mth::Vector& at = matrix.row[2];
        float rightX = static_cast<float>(static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z);
        float rightY = static_cast<float>(static_cast<double>(up.z) * at.x - static_cast<double>(at.z) * up.x);
        float rightZ = static_cast<float>(static_cast<double>(at.y) * up.x - static_cast<double>(up.y) * at.x);
        matrix.row[0] = {rightX, rightY, rightZ, 0.0f};
        matrix.row[0].Normalize();
        float above = static_cast<float>(std::fabs(static_cast<double>(m_groundAbove)) * 12.0f);
        float below = -static_cast<float>(std::fabs(static_cast<double>(m_groundBelow)) * 12.0f);
        float w = static_cast<float>(static_cast<double>(pos.w) + 1.0f);
        Mth::Vector start = {pos.x, static_cast<float>(static_cast<double>(above) + pos.y), pos.z, w};
        Mth::Vector end = {pos.x, static_cast<float>(static_cast<double>(below) + pos.y), pos.z, w};
        Mth::Vector hit;
        if (!GroundHit(m_groundTriangle, &end, &start, &hit))
            return false;
        m_groundOffset = static_cast<float>(static_cast<double>(hit.y) - pos.y);
        return true;
    }

    // Three feelers - behind on the right, behind on the left, ahead in the middle (72 back or ahead, 30 to the
    // side): the ground's points give the height and the orientation.
    Nx::CFeeler feeler; // (made and never used)
    feeler.Construct();
    Mth::Vector& right = matrix.row[0];
    Mth::Vector& up = matrix.row[1];
    Mth::Vector& at = matrix.row[2];
    up = {0.0f, 1.0f, 0.0f, 1.0f};
    {
        float z = static_cast<float>(static_cast<double>(up.x) * at.y - static_cast<double>(up.y) * at.x);
        float y = static_cast<float>(static_cast<double>(up.z) * at.x - static_cast<double>(up.x) * at.z);
        float x = static_cast<float>(static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z);
        right = {x, y, z, 0.0f};
        right.Normalize();
    }
    const float ahead[4] = {static_cast<float>(static_cast<double>(at.x) * 72.0f),
                            static_cast<float>(static_cast<double>(at.y) * 72.0f),
                            static_cast<float>(static_cast<double>(at.z) * 72.0f),
                            static_cast<float>(static_cast<double>(at.w) * 72.0f)};
    const float side[4] = {static_cast<float>(static_cast<double>(right.x) * 30.0f),
                           static_cast<float>(static_cast<double>(right.y) * 30.0f),
                           static_cast<float>(static_cast<double>(right.z) * 30.0f),
                           static_cast<float>(static_cast<double>(right.w) * 30.0f)};
    auto move = [&pos](const float* by, double scale) {
        float* p = &pos.x;
        for (int i = 0; i < 4; ++i)
            p[i] = static_cast<float>(static_cast<double>(p[i]) + by[i] * scale);
    };
    // (a feeler from DistAbove over the position to DistBelow under it, feet; the products unrounded)
    auto line = [&](Mth::Vector* start, Mth::Vector* end) {
        float x = pos.x, y = pos.y, z = pos.z, w = pos.w;
        *start = {x, static_cast<float>(static_cast<double>(m_groundAbove) * 12.0f + y), z, w};
        *end = {x, static_cast<float>(static_cast<double>(y) - static_cast<double>(m_groundBelow) * 12.0f), z, w};
    };
    Mth::Vector start, end, backRight, backLeft, front;
    move(ahead, -1.0);
    move(side, 1.0);
    line(&start, &end);
    bool hitRight = GroundHit(m_groundTriangles[0], &end, &start, &backRight);
    move(side, -2.0);
    line(&start, &end);
    bool hitLeft = false;
    if (GroundHit(m_groundTriangles[1], &end, &start, &backLeft)) {
        hitLeft = true;
        if (hitRight) {
            right = {static_cast<float>(static_cast<double>(backRight.x) - backLeft.x),
                     static_cast<float>(static_cast<double>(backRight.y) - backLeft.y),
                     static_cast<float>(static_cast<double>(backRight.z) - backLeft.z),
                     static_cast<float>(static_cast<double>(backRight.w) - backLeft.w)};
            right.Normalize();
        }
    }
    move(ahead, 2.0);
    move(side, 1.0);
    line(&start, &end);
    move(ahead, -1.0); // (back where it was)
    if (!GroundHit(m_groundTriangles[2], &end, &start, &front) || !hitLeft || !hitRight)
        return false;
    // The height: half way between the back's middle and the front; at: towards the front from the back's
    // middle, half way from the old at; up: at x right.
    pos.y = static_cast<float>(static_cast<double>(backLeft.y) + backRight.y);
    pos.y = static_cast<float>(static_cast<double>(pos.y) * 0.5f);
    pos.y = static_cast<float>(static_cast<double>(front.y) + pos.y);
    pos.y = static_cast<float>(static_cast<double>(pos.y) * 0.5f);
    Mth::Vector back = backRight + backLeft;
    back /= 2.0f;
    back -= front;
    back *= -1.0f;
    back.Normalize();
    at += back;
    at /= 2.0f;
    up = Mth::CrossProduct(at, right);
    up.Normalize();
    return true;
}

// 0x004269F0
bool CMotionComponent::MoveTo(Script::CStruct* params, Script::CScript*, Mth::Vector target, uint32_t node)
{
    m_moveTarget = target;
    const Mth::Vector& pos = m_object->m_pos;
    double dx = static_cast<double>(m_moveTarget.x) - pos.x;
    double dy = static_cast<double>(m_moveTarget.y) - pos.y;
    double dz = static_cast<double>(m_moveTarget.z) - pos.z;
    m_moveAcceleration = 0.0f;
    m_moveDistance = static_cast<float>(std::sqrt((dz * dz + dy * dy) + dx * dx));
    m_moveStart = pos;
    m_moveDone = 0.0f;
    m_moveArriveNode = 0;
    m_motionFlags = (m_motionFlags & ~MOTION_FOLLOWING_PATH) | MOTION_MOVING;
    float time = target.x; // (the original reads `time` into the target's x argument)
    if (params->GetFloat(kTime, &time, 0)) {
        m_moveSpeed = static_cast<float>(static_cast<double>(m_moveDistance) / time);
        return false;
    }
    float speed;
    if (!params->GetFloat(kSpeed, &speed, 0)) {
        // (no speed or time: there at once)
        m_object->m_pos = m_moveTarget;
        m_motionFlags &= ~MOTION_MOVING;
        if (node && params->ContainsFlag(kOrient))
            OrientToNode(SkateScript::GetNode(node, true));
        return true;
    }
    int units = 2; // (miles per hour)
    if (params->ContainsFlag(kMph))
        units = 2;
    else if (params->ContainsFlag(kFps))
        units = 1;
    else if (params->ContainsFlag(kIps))
        units = 0;
    params->GetFloat(kAcceleration, &m_moveAcceleration, 0);
    double inches = speed; // (kept unrounded)
    if (units == 2) {
        inches = inches * kInchesPerMile * kHoursPerSecond;
        m_moveAcceleration = static_cast<float>(static_cast<double>(m_moveAcceleration) * kInchesPerMile * kHoursPerSecond);
    } else if (units == 1) {
        inches = inches * 12.0f;
        m_moveAcceleration = static_cast<float>(static_cast<double>(m_moveAcceleration) * 12.0f);
    }
    if (m_moveAcceleration == 0.0f) {
        m_moveSpeed = static_cast<float>(inches);
        return false;
    }
    m_moveMaxSpeed = static_cast<float>(inches);
    m_moveSpeed = 0.0f;
    return false;
}

// 0x00426C10
void CMotionComponent::MoveToNode(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kName, &name, 0);
    if (!name)
        return;
    Mth::Vector pos;
    SkateScript::GetPosition(name, &pos);
    if (MoveTo(params, script, pos, name))
        ArriveAtNode(name);
    else
        m_moveArriveNode = name;
}

// 0x00426CB0
void CMotionComponent::MoveToPosition(Script::CStruct* params, Script::CScript* script, bool absolute)
{
    Mth::Vector pos = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0u, reinterpret_cast<Script::VectorValue*>(&pos), 0);
    if (params->ContainsFlag(kMaxCoords))
        pos.NegateXRotateX90();
    if (!absolute) {
        const Mth::Vector& at = m_object->m_pos;
        pos = {pos.x + at.x, pos.y + at.y, pos.z + at.z, pos.w + at.w};
    }
    MoveTo(params, script, pos, 0);
}

// 0x00426D80
void CMotionComponent::MoveToLink(Script::CStruct* params, Script::CScript* script)
{
    int link = 0;
    int links = static_cast<int>(SkateScript::GetLinkCount(m_node));
    if (!links)
        return;
    if (params->GetInteger(kLinkNum, &link, 0))
        link -= 1;
    else if (links > 1 && params->ContainsFlag(kRandomLink))
        link = Mth::Rnd(links);
    Script::CStruct* node = SkateScript::GetLinkedNode(m_node, link);
    uint32_t name = SkateScript::GetNodeName(node);
    Mth::Vector pos;
    SkateScript::GetPosition(node, &pos);
    if (MoveTo(params, script, pos, 0))
        ArriveAtNode(name);
    else
        m_moveArriveNode = name;
}

// The body of Obj_MoveForward / Obj_MoveLeft: `dist` along the matrix row (x, y, z through rounded products, w
// through an unrounded one).
void CMotionComponent::MoveAlong(Script::CStruct* params, Script::CScript* script, const Mth::Vector& direction)
{
    float distance = 0.0f;
    params->GetFloat(kDist, &distance, 0);
    const Mth::Vector& pos = m_object->m_pos;
    double w = static_cast<double>(distance) * direction.w;
    float z = static_cast<float>(static_cast<double>(distance) * direction.z);
    float y = static_cast<float>(static_cast<double>(distance) * direction.y);
    float x = static_cast<float>(static_cast<double>(distance) * direction.x);
    Mth::Vector target = {static_cast<float>(static_cast<double>(x) + pos.x),
                          static_cast<float>(static_cast<double>(y) + pos.y),
                          static_cast<float>(static_cast<double>(z) + pos.z), static_cast<float>(w + pos.w)};
    MoveTo(params, script, target, 0);
}

// --- script member functions ---

// 0x004268C0
bool CMotionComponent::Obj_StickToGround(Script::CStruct* params, Script::CScript*)
{
    params->GetFloat(kDistAbove, &m_groundAbove, 0);
    params->GetFloat(kDistBelow, &m_groundBelow, 0);
    if (params->ContainsFlag(kOff)) {
        m_motionFlags2 &= ~MOTION2_STICK_TO_GROUND;
        return true;
    }
    m_motionFlags2 |= MOTION2_STICK_TO_GROUND;
    if (m_groundAbove == 0.0f && m_groundBelow == 0.0f)
        return false;
    StickToGround();
    return true;
}

// 0x00426960
bool CMotionComponent::Obj_SkipToRestart(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    Script::CStruct* node = SkateScript::GetNode(name, true);
    Mth::Vector pos;
    SkateScript::GetPosition(node, &pos);
    m_object->m_pos = pos;
    OrientToNode(node);
    // (its model brought up to date: a crash without a model component)
    CBaseComponent* model = m_object->GetComponent(kModel);
    model->Update();
    return true;
}

// 0x004272D0
bool CMotionComponent::Obj_MoveToPosStored(Script::CStruct* params, Script::CScript* script)
{
    MoveTo(params, script, m_storedPos, 0);
    return true;
}

// 0x00427320
bool CMotionComponent::Obj_MoveForward(Script::CStruct* params, Script::CScript* script)
{
    MoveAlong(params, script, m_object->m_matrix.row[2]);
    return true;
}

// 0x004273D0
bool CMotionComponent::Obj_MoveLeft(Script::CStruct* params, Script::CScript* script)
{
    MoveAlong(params, script, m_object->m_matrix.row[0]);
    return true;
}

// 0x00427470
bool CMotionComponent::Obj_MoveToPos(Script::CStruct* params, Script::CScript* script)
{
    MoveToPosition(params, script, true);
    return true;
}

// 0x00427490
bool CMotionComponent::Obj_MoveToRelPos(Script::CStruct* params, Script::CScript* script)
{
    MoveToPosition(params, script, false);
    return true;
}

// 0x004274B0
bool CMotionComponent::Obj_MoveToLink(Script::CStruct* params, Script::CScript* script)
{
    MoveToLink(params, script);
    return true;
}

} // namespace Obj

using Obj::CMotionComponent;

REPLACE_UNCHECKED(0x00425CB0, Obj::GroundHitStub); // (ECX, EDX and two stack arguments the caller pops)
REPLACE(0x00424870, &CMotionComponent::UpdateJump);
REPLACE(0x00425F40, &CMotionComponent::StickToGround);
REPLACE(0x004269F0, &CMotionComponent::MoveTo);
REPLACE(0x00426C10, &CMotionComponent::MoveToNode);
REPLACE(0x00426CB0, &CMotionComponent::MoveToPosition);
REPLACE(0x00426D80, &CMotionComponent::MoveToLink);
REPLACE(0x004268C0, &CMotionComponent::Obj_StickToGround);
REPLACE(0x00426960, &CMotionComponent::Obj_SkipToRestart);
REPLACE(0x004272D0, &CMotionComponent::Obj_MoveToPosStored);
REPLACE(0x00427320, &CMotionComponent::Obj_MoveForward);
REPLACE(0x004273D0, &CMotionComponent::Obj_MoveLeft);
REPLACE(0x00427470, &CMotionComponent::Obj_MoveToPos);
REPLACE(0x00427490, &CMotionComponent::Obj_MoveToRelPos);
REPLACE(0x004274B0, &CMotionComponent::Obj_MoveToLink);
