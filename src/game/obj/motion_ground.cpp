// The motion component (GameObjects::CKinematicsModule, motion.h): jumping, sticking to the ground, and moving to points
// (Obj_MoveTo*, Obj_MoveForward / Left, Obj_SkipToRestart).

#include <cmath>
#include <cstdint>

#include "game/mth/random.h"
#include "game/mth/vec4.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_tests.h"
#include "game/nx/coll_tri.h"
#include "game/nx/probe_ray.h"
#include "game/obj/motion.h"
#include "game/obj/motion_internal.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"

namespace GameObjects {

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
Maths::Vec4 VertexPos(Engine3D::CHitMesh* object, int vertex)
{
    Maths::Vec4 pos;
    reinterpret_cast<Maths::Vec4*(__thiscall*)(Engine3D::CHitMesh*, Maths::Vec4*, int)>(
        (*reinterpret_cast<void***>(object))[9])(object, &pos, vertex);
    return pos;
}

} // namespace

// 0x00425CB0
bool __cdecl GroundHit(Maths::Vec4* triangle, const Maths::Vec4* end, const Maths::Vec4* start, Maths::Vec4* out)
{
    Maths::Vec4 along = {static_cast<float>(static_cast<double>(end->x) - start->x),
                         static_cast<float>(static_cast<double>(end->y) - start->y),
                         static_cast<float>(static_cast<double>(end->z) - start->z),
                         static_cast<float>(static_cast<double>(end->w) - start->w)};
    float t = 0.0f;
    if (Engine3D::RayHitsTriangle(*start, along, triangle[0], triangle[1], triangle[2], &t)) {
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
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    if (!feeler.GetCollisionAlong(start, end, false))
        return false;
    // (the face hit becomes the triangle tried first next time)
    Engine3D::CHitMesh* object = feeler.m_hit.object;
    Engine3D::CTriMeshSource* geometry = object->m_geometry;
    int face = static_cast<int>(feeler.m_hit.face);
    for (int corner = 0; corner < 3; ++corner) {
        int vertex;
        if (geometry->m_smallFaces)
            vertex = geometry->m_faces[face * 8 + 4 + corner];
        else
            vertex = *reinterpret_cast<const uint16_t*>(geometry->m_faces + face * 10 + 4 + 2 * corner);
        triangle[corner] = VertexPos(object, vertex);
    }
    *out = feeler.m_contactPos;
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
void CKinematicsModule::UpdateJump()
{
    const Maths::Vec4& pos = m_entity->m_atPoint;
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
            Engine3D::CProbeRay feeler;
            feeler.MakeNew();
            float above = static_cast<float>(static_cast<double>(w) + 1.0f);
            feeler.m_line.start = {x, static_cast<float>(static_cast<double>(y) + m_jumpColAbove), z, above};
            feeler.m_line.end = {x, static_cast<float>(static_cast<double>(y) + -m_jumpColBelow), z, above};
            if (feeler.GetHitShape(true, false))
                m_motionFlags &= ~MOTION_JUMPING;
        }
    }
    m_jumpPos = {x, y, z, w};
}

// 0x00425F40
bool CKinematicsModule::StickToGround()
{
    if (m_groundAbove == 0.0f && m_groundBelow == 0.0f)
        return false;
    Maths::Mat4& matrix = m_entity->m_basisFrame;
    Maths::Vec4& pos = m_entity->m_atPoint;
    if (m_groundHeightOnly) {
        // The right axis from up and at, then the ground under it: the height to make up (Update eases it in).
        const Maths::Vec4& up = matrix.row[1];
        const Maths::Vec4& at = matrix.row[2];
        float rightX = static_cast<float>(static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z);
        float rightY = static_cast<float>(static_cast<double>(up.z) * at.x - static_cast<double>(at.z) * up.x);
        float rightZ = static_cast<float>(static_cast<double>(at.y) * up.x - static_cast<double>(up.y) * at.x);
        matrix.row[0] = {rightX, rightY, rightZ, 0.0f};
        matrix.row[0].MakeUnit();
        float above = static_cast<float>(std::fabs(static_cast<double>(m_groundAbove)) * 12.0f);
        float below = -static_cast<float>(std::fabs(static_cast<double>(m_groundBelow)) * 12.0f);
        float w = static_cast<float>(static_cast<double>(pos.w) + 1.0f);
        Maths::Vec4 start = {pos.x, static_cast<float>(static_cast<double>(above) + pos.y), pos.z, w};
        Maths::Vec4 end = {pos.x, static_cast<float>(static_cast<double>(below) + pos.y), pos.z, w};
        Maths::Vec4 hit;
        if (!GroundHit(m_groundTriangle, &end, &start, &hit))
            return false;
        m_groundOffset = static_cast<float>(static_cast<double>(hit.y) - pos.y);
        return true;
    }

    // Three feelers - behind on the right, behind on the left, ahead in the middle (72 back or ahead, 30 to the
    // side): the ground's points give the height and the orientation.
    Engine3D::CProbeRay feeler; // (made and never used)
    feeler.MakeNew();
    Maths::Vec4& right = matrix.row[0];
    Maths::Vec4& up = matrix.row[1];
    Maths::Vec4& at = matrix.row[2];
    up = {0.0f, 1.0f, 0.0f, 1.0f};
    {
        float z = static_cast<float>(static_cast<double>(up.x) * at.y - static_cast<double>(up.y) * at.x);
        float y = static_cast<float>(static_cast<double>(up.z) * at.x - static_cast<double>(up.x) * at.z);
        float x = static_cast<float>(static_cast<double>(at.z) * up.y - static_cast<double>(at.y) * up.z);
        right = {x, y, z, 0.0f};
        right.MakeUnit();
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
    auto line = [&](Maths::Vec4* start, Maths::Vec4* end) {
        float x = pos.x, y = pos.y, z = pos.z, w = pos.w;
        *start = {x, static_cast<float>(static_cast<double>(m_groundAbove) * 12.0f + y), z, w};
        *end = {x, static_cast<float>(static_cast<double>(y) - static_cast<double>(m_groundBelow) * 12.0f), z, w};
    };
    Maths::Vec4 start, end, backRight, backLeft, front;
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
            right.MakeUnit();
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
    Maths::Vec4 back = backRight + backLeft;
    back /= 2.0f;
    back -= front;
    back *= -1.0f;
    back.MakeUnit();
    at += back;
    at /= 2.0f;
    up = Maths::CrossVec(at, right);
    up.MakeUnit();
    return true;
}

// 0x004269F0
bool CKinematicsModule::MoveTo(Scripts::CStruct* params, Scripts::CScript*, Maths::Vec4 target, uint32_t node)
{
    m_moveTarget = target;
    const Maths::Vec4& pos = m_entity->m_atPoint;
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
    if (params->FetchFloat(kTime, &time, 0)) {
        m_moveSpeed = static_cast<float>(static_cast<double>(m_moveDistance) / time);
        return false;
    }
    float speed;
    if (!params->FetchFloat(kSpeed, &speed, 0)) {
        // (no speed or time: there at once)
        m_entity->m_atPoint = m_moveTarget;
        m_motionFlags &= ~MOTION_MOVING;
        if (node && params->HasBareFlag(kOrient))
            FaceNode(LevelNodes::FetchVertex(node, true));
        return true;
    }
    int units = 2; // (miles per hour)
    if (params->HasBareFlag(kMph))
        units = 2;
    else if (params->HasBareFlag(kFps))
        units = 1;
    else if (params->HasBareFlag(kIps))
        units = 0;
    params->FetchFloat(kAcceleration, &m_moveAcceleration, 0);
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
void CKinematicsModule::GlideToNode(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, 0);
    if (!name)
        return;
    Maths::Vec4 pos;
    LevelNodes::FetchPlace(name, &pos);
    if (MoveTo(params, script, pos, name))
        ArriveAtNode(name);
    else
        m_moveArriveNode = name;
}

// 0x00426CB0
void CKinematicsModule::MoveToPosition(Scripts::CStruct* params, Scripts::CScript* script, bool absolute)
{
    Maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&pos), 0);
    if (params->HasBareFlag(kMaxCoords))
        pos.NegateXRotateX90();
    if (!absolute) {
        const Maths::Vec4& at = m_entity->m_atPoint;
        pos = {pos.x + at.x, pos.y + at.y, pos.z + at.z, pos.w + at.w};
    }
    MoveTo(params, script, pos, 0);
}

// 0x00426D80
void CKinematicsModule::MoveToLink(Scripts::CStruct* params, Scripts::CScript* script)
{
    int link = 0;
    int links = static_cast<int>(LevelNodes::GetLinkCount(m_listLink));
    if (!links)
        return;
    if (params->FetchIntValue(kLinkNum, &link, 0))
        link -= 1;
    else if (links > 1 && params->HasBareFlag(kRandomLink))
        link = Maths::Rnd(links);
    Scripts::CStruct* node = LevelNodes::GetLinkedNode(m_listLink, link);
    uint32_t name = LevelNodes::GetNodeName(node);
    Maths::Vec4 pos;
    LevelNodes::FetchPlace(node, &pos);
    if (MoveTo(params, script, pos, 0))
        ArriveAtNode(name);
    else
        m_moveArriveNode = name;
}

// The body of Obj_MoveForward / Obj_MoveLeft: `dist` along the matrix row (x, y, z through rounded products, w
// through an unrounded one).
void CKinematicsModule::MoveAlong(Scripts::CStruct* params, Scripts::CScript* script, const Maths::Vec4& direction)
{
    float distance = 0.0f;
    params->FetchFloat(kDist, &distance, 0);
    const Maths::Vec4& pos = m_entity->m_atPoint;
    double w = static_cast<double>(distance) * direction.w;
    float z = static_cast<float>(static_cast<double>(distance) * direction.z);
    float y = static_cast<float>(static_cast<double>(distance) * direction.y);
    float x = static_cast<float>(static_cast<double>(distance) * direction.x);
    Maths::Vec4 target = {static_cast<float>(static_cast<double>(x) + pos.x),
                          static_cast<float>(static_cast<double>(y) + pos.y),
                          static_cast<float>(static_cast<double>(z) + pos.z), static_cast<float>(w + pos.w)};
    MoveTo(params, script, target, 0);
}

// --- script member functions ---

// 0x004268C0
bool CKinematicsModule::Obj_StickToGround(Scripts::CStruct* params, Scripts::CScript*)
{
    params->FetchFloat(kDistAbove, &m_groundAbove, 0);
    params->FetchFloat(kDistBelow, &m_groundBelow, 0);
    if (params->HasBareFlag(kOff)) {
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
bool CKinematicsModule::Obj_SkipToRestart(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    Scripts::CStruct* node = LevelNodes::FetchVertex(name, true);
    Maths::Vec4 pos;
    LevelNodes::FetchPlace(node, &pos);
    m_entity->m_atPoint = pos;
    FaceNode(node);
    // (its model brought up to date: a crash without a model component)
    CObjectModule* model = m_entity->FetchModule(kModel);
    model->TickFrame();
    return true;
}

// 0x004272D0
bool CKinematicsModule::Obj_MoveToPosStored(Scripts::CStruct* params, Scripts::CScript* script)
{
    MoveTo(params, script, m_storedPos, 0);
    return true;
}

// 0x00427320
bool CKinematicsModule::Obj_MoveForward(Scripts::CStruct* params, Scripts::CScript* script)
{
    MoveAlong(params, script, m_entity->m_basisFrame.row[2]);
    return true;
}

// 0x004273D0
bool CKinematicsModule::Obj_MoveLeft(Scripts::CStruct* params, Scripts::CScript* script)
{
    MoveAlong(params, script, m_entity->m_basisFrame.row[0]);
    return true;
}

// 0x00427470
bool CKinematicsModule::Obj_MoveToPos(Scripts::CStruct* params, Scripts::CScript* script)
{
    MoveToPosition(params, script, true);
    return true;
}

// 0x00427490
bool CKinematicsModule::Obj_MoveToRelPos(Scripts::CStruct* params, Scripts::CScript* script)
{
    MoveToPosition(params, script, false);
    return true;
}

// 0x004274B0
bool CKinematicsModule::Obj_MoveToLink(Scripts::CStruct* params, Scripts::CScript* script)
{
    MoveToLink(params, script);
    return true;
}

} // namespace Obj

using GameObjects::CKinematicsModule;
