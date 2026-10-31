// The Vehicle component's collision and integration: the colliders against the world (contact points, impulses,
// depth correction, friction, steering against walls), moving the body without tunnelling, righting itself, turning
// in the air, and a whole sub-step. See vehicle_component.h. From the disassembly of 0x00436CF0-0x00437549,
// 0x00438770-0x00438A16, 0x00438BB0-0x00439D86, 0x00439D90-0x0043A066 and 0x0043BEE0-0x0043C19A. Float arithmetic
// follows the original's x87 code: double (53 bits) where it stays on the stack, rounded to float where stored.

#include "game/obj/model_component.h"
#include "game/obj/vehicle_component.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/gfx/debug_lines.h"
#include "game/maths/rotation_blend.h"
#include "game/maths/vec4.h"
#include "game/engine3d/coll_rect.h"
#include "game/obj/vehicle_internal.h"
#include "game/sys/game_clock.h"
#include "game/sys/stopwatch.h"
#include "game/obj/control_pad.h"
#include "game/obj/trigger_events.h"

namespace GameObjects {

using namespace Vehicle;

namespace {

const float kHalf = 0.5f;                                   // 0x006453BC
const float kMinusHalf = -0.5f;                             // 0x0064642C
const float kMinDepth = std::bit_cast<float>(0x3C23D70Au);   // 0x0064544C: 0.01
const float kSameEnd = std::bit_cast<float>(0x3DCCCCCDu);    // 0x00645D74: 0.1
const double kOpposite = -0.99;                             // 0x00646730 (a double)
const float kBounce = 1.5f;                                 // 0x00646744
const float kDropTest = 4800.0f;                            // 0x006461D0
const float kMaxDepth = 2.0f;                               // 0x00645448
const float kUpright = std::bit_cast<float>(0x3F34FDF4u);    // 0x006467BC: 0.707
const float kMinSlide = std::bit_cast<float>(0x3A83126Fu);   // 0x006461D8: 0.001
const float kRightingUp = std::bit_cast<float>(0x3F7FBE77u); // 0x006467B8: 0.999
const float kRightingSpeed = 90000.0f;                      // 0x006467B4 (300 squared)
const float kRightingSpin = std::bit_cast<float>(0x3F1DE9E7u); // 0x006467B0: 0.61685
const float kRightingTilt = 0.25f;                          // 0x006467AC
const float kRightingRate = 3.0f;                           // 0x006467A8
const float kAlignFast = 122500.0f;                         // 0x006467C4 (350 squared)
const float kAlignSlowRate = std::bit_cast<float>(0x3E99999Au); // 0x006467C0: 0.3

// The model component's 0x00420670 (not rewritten yet): its lighting from what a feeler hit.
void LightFromFeeler(CObjectModule* model, Engine3D::CProbeRay* feeler, bool b, float c)
{
    static_cast<CMeshInstanceModule*>(model)->ApplyBrightness(feeler, b, c);
}

bool Interrupted(const CDrivableModule* vehicle)
{
    return vehicle->m_cutShort || (vehicle->m_entity->m_objectFlags & OBJECT_FLAG_DEAD);
}

// A slerp between two matrices, on the stack without a constructor (as the original's).
struct SlerpStorage {
    alignas(Maths::ArcRotationBlender) uint8_t bytes[sizeof(Maths::ArcRotationBlender)];
    Maths::ArcRotationBlender* Pull() { return reinterpret_cast<Maths::ArcRotationBlender*>(bytes); }
};

} // namespace

// 0x00436CF0 (stdcall)
double __stdcall CDrivableModule::GetPenetration(const Contact* contact, const Collider* collider)
{
    // The contact point's distance in from each of the face's four edges; the nearest.
    double dx = static_cast<double>(contact->point.x) - collider->worldCorner.x;
    double dy = static_cast<double>(contact->point.y) - collider->worldCorner.y;
    double dz = static_cast<double>(contact->point.z) - collider->worldCorner.z;
    const Maths::Vec4& a = collider->directions[0];
    const Maths::Vec4& b = collider->directions[1];
    int side = 0;
    float nearest = static_cast<float>((dz * a.z + dy * a.y) + dx * a.x);
    double distance = static_cast<double>(collider->lengths[0]) - nearest;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 1;
    }
    distance = (dz * b.z + dy * b.y) + dx * b.x;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 2;
    }
    distance = collider->lengths[1] - distance;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 3;
    }
    if (nearest < kMinDepth)
        return 0.0;
    // That distance along the contact's normal.
    const Maths::Vec4& n = contact->normal;
    switch (side) {
    case 0:
        return nearest / ((static_cast<double>(a.z) * n.z + static_cast<double>(a.y) * n.y) + static_cast<double>(n.x) * a.x);
    case 1:
        return -(nearest / ((static_cast<double>(a.z) * n.z + static_cast<double>(a.y) * n.y) + static_cast<double>(a.x) * n.x));
    case 2:
        return nearest / ((static_cast<double>(b.z) * n.z + static_cast<double>(b.y) * n.y) + static_cast<double>(b.x) * n.x);
    default:
        return -(nearest / ((static_cast<double>(b.z) * n.z + static_cast<double>(b.y) * n.y) + static_cast<double>(b.x) * n.x));
    }
}

// 0x00436E50
bool CDrivableModule::FindEdgeContact(Contact* contact, Engine3D::CBoxProbe* rect, const Collider* collider, int hit, int end)
{
    // Another joined hit with an end at this point (within 0.1) facing the opposite way.
    for (int k = rect->m_joinedCount - 1; k >= 0; --k) {
        if (k == hit)
            continue;
        const Engine3D::RectHit& other = rect->m_joined[k];
        for (int e = 1; e >= 0; --e) {
            const Maths::Vec4& p = other.ends[e].point;
            if (!(std::fabs(static_cast<double>(contact->point.x) - p.x) < kSameEnd))
                continue;
            if (!(std::fabs(static_cast<double>(contact->point.y) - p.y) < kSameEnd))
                continue;
            if (!(std::fabs(static_cast<double>(contact->point.z) - p.z) < kSameEnd))
                continue;
            const Maths::Vec4& n = contact->normal;
            if (!((static_cast<double>(n.z) * other.normal.z + static_cast<double>(n.y) * other.normal.y) +
                      static_cast<double>(n.x) * other.normal.x <
                  kOpposite))
                continue;
            // On the edge between them: the normal across the hit's triangle edge, and the depth.
            const Maths::Vec4& edge = rect->m_joined[hit].ends[end].edge;
            double x = static_cast<double>(edge.y) * n.z - static_cast<double>(edge.z) * n.y;
            double y = static_cast<double>(edge.z) * n.x - static_cast<double>(edge.x) * n.z;
            contact->normal.w = 0.0f;
            contact->normal.z = static_cast<float>(static_cast<double>(edge.x) * n.y - static_cast<double>(edge.y) * n.x);
            contact->normal.x = static_cast<float>(x);
            contact->normal.y = static_cast<float>(y);
            contact->depth = static_cast<float>(GetPenetration(contact, collider));
            if (m_debugMode) {
                Maths::Vec4 tip{static_cast<float>(static_cast<double>(contact->normal.x) * 72.0f + contact->point.x),
                                static_cast<float>(static_cast<double>(contact->normal.y) * 72.0f + contact->point.y),
                                static_cast<float>(static_cast<double>(static_cast<float>(contact->normal.z * 72.0f)) +
                                                   contact->point.z),
                                static_cast<float>(static_cast<double>(static_cast<float>(contact->normal.w * 72.0f)) +
                                                   contact->point.w)};
                Gfx2D::PushDiagLine(&contact->point, &tip, 0xFFFF64FF, 0xFFFF64FF, 1);
            }
            return true;
        }
    }
    return false;
}

// 0x00437060
bool CDrivableModule::NormalFacesAway(const Contact* contact)
{
    const Maths::Vec4& p = contact->point;
    const Maths::Vec4& n = contact->normal;
    double along = ((static_cast<double>(p.z) - m_atPoint.z) * n.z + (static_cast<double>(p.y) - m_atPoint.y) * n.y) +
                   (static_cast<double>(p.x) - m_atPoint.x) * n.x;
    bool away = along >= 0.0;
    if (m_debugMode && away) {
        Maths::Vec4 tip{static_cast<float>(static_cast<double>(n.x) * 48.0f + p.x),
                        static_cast<float>(static_cast<double>(n.y) * 48.0f + p.y),
                        static_cast<float>(static_cast<double>(static_cast<float>(n.z * 48.0f)) + p.z),
                        static_cast<float>(static_cast<double>(static_cast<float>(n.w * 48.0f)) + p.w)};
        Gfx2D::PushDiagLine(&p, &tip, 0xFF000000, 0xFF000000, 1);
    }
    return away;
}

// 0x00437120
void CDrivableModule::MoveBy(const Maths::Vec4* move)
{
    Maths::Vec4 saved = m_atPoint;
    Maths::Vec4 end{static_cast<float>(static_cast<double>(move->x) + m_atPoint.x),
                    static_cast<float>(static_cast<double>(m_atPoint.y) + move->y),
                    static_cast<float>(static_cast<double>(m_atPoint.z) + move->z),
                    static_cast<float>(static_cast<double>(m_atPoint.w) + move->w)};
    Engine3D::CProbeRay feeler;
    feeler.MakeNew(&m_atPoint, &end);
    if (feeler.GetHitShape(false, false)) {
        // Going through something: put just off the surface, the rest of the move reflected, the momentum into it
        // turned back by half again.
        DebugOut::Printf(kDebugFormat, kDebugFile, 0xB10, "less than uber frig"); // "less than uber frig"
        const Maths::Vec4& point = feeler.m_contactPos;
        const Maths::Vec4& n = feeler.m_surfaceDir;
        m_atPoint.x = static_cast<float>(static_cast<double>(n.x) + point.x);
        m_atPoint.y = static_cast<float>(static_cast<double>(point.y) + n.y);
        m_atPoint.z = static_cast<float>(static_cast<double>(point.z) + n.z);
        m_atPoint.w = static_cast<float>(static_cast<double>(point.w) + n.w);
        double distance = feeler.m_reach;
        double vx = distance * move->x, vy = distance * move->y, vz = distance * move->z, vw = distance * move->w;
        double twice = (static_cast<double>(n.x) * vx + static_cast<double>(n.y) * vy) + static_cast<double>(n.z) * vz;
        twice = twice + twice;
        float kw = static_cast<float>(static_cast<double>(n.w) * twice);
        float kz = static_cast<float>(static_cast<double>(n.z) * twice);
        float ky = static_cast<float>(static_cast<double>(n.y) * twice);
        double kx = twice * n.x;
        float rx = static_cast<float>(vx - kx);
        float ry = static_cast<float>(vy - ky);
        double rz = vz - kz;
        float rw = static_cast<float>(vw - kw);
        m_atPoint.x = static_cast<float>(static_cast<double>(rx) + m_atPoint.x);
        m_atPoint.y = static_cast<float>(static_cast<double>(ry) + m_atPoint.y);
        m_atPoint.z = static_cast<float>(rz + m_atPoint.z);
        m_atPoint.w = static_cast<float>(static_cast<double>(rw) + m_atPoint.w);
        double into = ((static_cast<double>(n.z) * m_linearMomentum.z + static_cast<double>(n.y) * m_linearMomentum.y) + static_cast<double>(n.x) * m_linearMomentum.x) *
                      kBounce;
        m_linearMomentum.x = static_cast<float>(m_linearMomentum.x - static_cast<double>(n.x) * into);
        m_linearMomentum.y = static_cast<float>(m_linearMomentum.y - static_cast<double>(n.y) * into);
        m_linearMomentum.z = static_cast<float>(m_linearMomentum.z - static_cast<double>(n.z) * into);
        m_linearMomentum.w = static_cast<float>(m_linearMomentum.w - static_cast<double>(n.w) * into);
    } else {
        m_atPoint.x = static_cast<float>(static_cast<double>(m_atPoint.x) + move->x);
        m_atPoint.y = static_cast<float>(static_cast<double>(move->y) + m_atPoint.y);
        m_atPoint.z = static_cast<float>(static_cast<double>(m_atPoint.z) + move->z);
        m_atPoint.w = static_cast<float>(static_cast<double>(m_atPoint.w) + move->w);
    }
    // Nothing below at all (4800 down): through the world - put back where it was. Else the model lit from the
    // ground.
    feeler.m_line.start = m_atPoint;
    feeler.m_line.end = m_atPoint;
    feeler.m_line.end.y = static_cast<float>(static_cast<double>(m_atPoint.y) - kDropTest);
    if (!feeler.GetHitShape(false, false)) {
        DebugOut::Printf(kDebugFormat, kDebugFile, 0xB26, "uber frig"); // "uber frig"
        m_atPoint = saved;
    } else {
        LightFromFeeler(m_meshInstance, &feeler, true, 1.0f);
    }
}

// 0x00437450
double CDrivableModule::GetImpulseDenominator(const Maths::Vec4* offset, const Maths::Vec4* direction)
{
    // 1 / (n . (n / m + (I^-1 (r x n)) x r)).
    const Maths::Vec4& r = *offset;
    const Maths::Vec4& n = *direction;
    float invMass = m_invMass;
    Maths::Vec4 cross;
    cross.x = static_cast<float>(static_cast<double>(n.z) * r.y - static_cast<double>(r.z) * n.y);
    cross.y = static_cast<float>(static_cast<double>(r.z) * n.x - static_cast<double>(r.x) * n.z);
    cross.z = static_cast<float>(static_cast<double>(n.y) * r.x - static_cast<double>(n.x) * r.y);
    cross.w = 0.0f;
    float linearX = static_cast<float>(static_cast<double>(n.x) * invMass);
    float linearY = static_cast<float>(static_cast<double>(invMass) * n.y);
    float linearZ = static_cast<float>(static_cast<double>(n.z) * invMass);
    Maths::Vec4 w;
    m_invInertia.RotateVector(w, cross);
    double tx = static_cast<double>(w.y) * r.z - static_cast<double>(w.z) * r.y;
    double ty = static_cast<double>(w.z) * r.x - static_cast<double>(w.x) * r.z;
    float tz = static_cast<float>(static_cast<double>(w.x) * r.y - static_cast<double>(w.y) * r.x);
    float ux = static_cast<float>(tx + linearX);
    double uy = ty + linearY;
    double uz = static_cast<double>(tz) + linearZ;
    return 1.0 / ((uy * n.y + uz * n.z) + static_cast<double>(ux) * n.x);
}

// 0x00438770
void CDrivableModule::UpdateRighting()
{
    QuatToMatrix(&m_orientation, &m_basisFrame);
    m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (m_basisFrame.row[1].y > kRightingUp) {
        m_righting = false;
        return;
    }
    // Upside down (or on its side), still, after a collision, with the throttle held: turn upright.
    double vx = m_vel.x, vy = m_vel.y, vz = m_vel.z;
    double sx = m_rotvel.x, sy = m_rotvel.y, sz = m_rotvel.z;
    bool stuck = (vx * vx + vy * vy) + vz * vz < kRightingSpeed && (sx * sx + sy * sy) + sz * sz < kRightingSpin &&
                 m_collisionImpulse > 0.0f && m_basisFrame.row[1].y < kRightingTilt &&
                 (m_wheelsInContact == 0 || m_basisFrame.row[1].y < 0.0f) && m_throttle;
    if (!m_righting) {
        if (!stuck)
            return;
        m_linearMomentum = {0.0f, 0.0f, 0.0f, 1.0f};
        m_rotmom = {0.0f, 0.0f, 0.0f, 1.0f};
        m_righting = true;
        m_rightingStart = FrameClock::GameClockElapsed() + g_clockOffset;
    }
    int start = m_rightingStart;
    if (static_cast<uint32_t>(FrameClock::GameClockElapsed()) + (static_cast<uint32_t>(g_clockOffset) - static_cast<uint32_t>(start)) >
        5300) {
        m_righting = false;
        return;
    }
    start = m_rightingStart;
    if (static_cast<uint32_t>(FrameClock::GameClockElapsed()) + (static_cast<uint32_t>(g_clockOffset) - static_cast<uint32_t>(start)) <
        300)
        return;
    // After 300 ms, turned towards upright facing the same way.
    Maths::Vec4 forward{m_basisFrame.row[2].x, 0.0f, m_basisFrame.row[2].z, m_basisFrame.row[2].w};
    forward.MakeUnit();
    Maths::Mat4 target;
    target.row[0] = {forward.z, 0.0f, -forward.x, 1.0f};
    target.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    target.row[2] = forward;
    target.row[3] = {0.0f, 0.0f, 0.0f, 1.0f}; // (the original leaves its stack there)
    SlerpStorage storage;
    Maths::ArcRotationBlender* slerp = storage.Pull();
    slerp->SetMatrices(m_basisFrame, target);
    double t = FrameClock::GetSlomoFrameSeconds() / slerp->angle * kRightingRate;
    slerp->FetchMat(m_basisFrame, t > 1.0 ? 1.0f : static_cast<float>(t));
    Maths::Vec4 quat;
    MatrixToQuat(&quat, &m_basisFrame);
    m_orientation = quat;
}

// 0x00439D90
void CDrivableModule::AlignInAir()
{
    if (m_righting)
        return;
    if (m_kickTime > 0.0f)
        return;
    double vx = m_vel.x, vz = m_vel.z;
    double align = m_alignSpeed;
    bool slow = align * align > vx * vx + vz * vz;
    if (slow && !m_alignWhenSlow)
        return;
    QuatToMatrix(&m_orientation, &m_basisFrame);
    m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Slow: towards its facing (level); fast: towards where it's going.
    Maths::Vec4 at;
    if (slow)
        at = {m_basisFrame.row[2].x, 0.0f, m_basisFrame.row[2].z, m_basisFrame.row[2].w};
    else
        at = m_vel;
    at.MakeUnit();
    if ((static_cast<double>(at.z) * m_basisFrame.row[2].z + static_cast<double>(at.y) * m_basisFrame.row[2].y) +
            static_cast<double>(at.x) * m_basisFrame.row[2].x <
        0.0) {
        at.x = at.x * -1.0f;
        at.y = at.y * -1.0f;
        at.z = at.z * -1.0f;
        at.w = at.w * -1.0f;
    }
    Maths::Vec4 up{0.0f, 1.0f, 0.0f, 1.0f};
    Maths::Mat4 target;
    target.row[0] = Maths::CrossVec(up, at);
    target.row[1] = Maths::CrossVec(at, target.row[0]);
    target.row[2] = at;
    target.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    SlerpStorage storage;
    Maths::ArcRotationBlender* slerp = storage.Pull();
    slerp->SetMatrices(m_basisFrame, target);
    // The turn ramps in over alignTime of free fall; slower under 350.
    vx = m_vel.x;
    vz = m_vel.z;
    bool fast = vx * vx + vz * vz > kAlignFast;
    float time = m_alignTime;
    float falling = m_freeFallTime;
    double ramp = falling < time ? falling : time;
    double rate = fast ? static_cast<double>(m_alignRate) * m_stepLength
                       : static_cast<double>(m_alignRate) * m_stepLength * kAlignSlowRate;
    slerp->FetchMat(m_basisFrame, static_cast<float>(ramp / time * rate));
    Maths::Vec4 quat;
    MatrixToQuat(&quat, &m_basisFrame);
    m_orientation = quat;
}

// 0x0043BEE0
void CDrivableModule::Integrate()
{
    // The accelerations.
    double invMass = m_invMass;
    float accelX = static_cast<float>(invMass * m_force.x), accelY = static_cast<float>(invMass * m_force.y);
    float accelZ = static_cast<float>(invMass * m_force.z), accelW = static_cast<float>(invMass * m_force.w);
    Maths::Vec4 spinAccel;
    m_invInertia.RotateVector(spinAccel, m_twistForce);
    // The momentum.
    double dt = m_stepLength;
    float dy = static_cast<float>(dt * m_force.y), dz = static_cast<float>(dt * m_force.z);
    float dw = static_cast<float>(dt * m_force.w);
    m_linearMomentum.x = static_cast<float>(dt * m_force.x + m_linearMomentum.x);
    m_linearMomentum.y = static_cast<float>(static_cast<double>(dy) + m_linearMomentum.y);
    m_linearMomentum.z = static_cast<float>(static_cast<double>(dz) + m_linearMomentum.z);
    m_linearMomentum.w = static_cast<float>(static_cast<double>(dw) + m_linearMomentum.w);
    // The move: (velocity + half the acceleration over the step) over the step.
    float timestep = m_stepLength;
    double half = static_cast<double>(m_stepLength) * kHalf;
    float hy = static_cast<float>(accelY * half), hz = static_cast<float>(accelZ * half);
    float hw = static_cast<float>(accelW * half);
    double vx = accelX * half + m_vel.x;
    double vy = static_cast<double>(hy) + m_vel.y;
    float vz = static_cast<float>(static_cast<double>(hz) + m_vel.z);
    float vw = static_cast<float>(static_cast<double>(hw) + m_vel.w);
    Maths::Vec4 move{static_cast<float>(vx * timestep), static_cast<float>(vy * timestep),
                     static_cast<float>(static_cast<double>(vz) * timestep), static_cast<float>(static_cast<double>(vw) * timestep)};
    MoveBy(&move);
    // The orientation: q += q * (-dt / 2) * (angular velocity + half the angular acceleration over the step).
    half = static_cast<double>(m_stepLength) * kHalf;
    Maths::Vec4 quat = m_orientation;
    double wx = spinAccel.x * half + m_rotvel.x;
    float wy = static_cast<float>(static_cast<double>(static_cast<float>(spinAccel.y * half)) + m_rotvel.y);
    float wz = static_cast<float>(static_cast<double>(static_cast<float>(spinAccel.z * half)) + m_rotvel.z);
    float scale = static_cast<float>(static_cast<double>(m_stepLength) * kMinusHalf);
    Maths::Vec4 turn{static_cast<float>(wx * scale), static_cast<float>(static_cast<double>(wy) * scale),
                     static_cast<float>(static_cast<double>(wz) * scale), 0.0f};
    QuatMultiply(&quat, &turn);
    m_orientation.x = static_cast<float>(static_cast<double>(quat.x) + m_orientation.x);
    m_orientation.y = static_cast<float>(static_cast<double>(quat.y) + m_orientation.y);
    m_orientation.z = static_cast<float>(static_cast<double>(quat.z) + m_orientation.z);
    m_orientation.w = static_cast<float>(static_cast<double>(quat.w) + m_orientation.w);
    QuatNormalize(&m_orientation, 1.0f);
    // The angular momentum.
    dt = m_stepLength;
    dy = static_cast<float>(dt * m_twistForce.y);
    dz = static_cast<float>(dt * m_twistForce.z);
    dw = static_cast<float>(dt * m_twistForce.w);
    m_rotmom.x = static_cast<float>(dt * m_twistForce.x + m_rotmom.x);
    m_rotmom.y = static_cast<float>(static_cast<double>(dy) + m_rotmom.y);
    m_rotmom.z = static_cast<float>(static_cast<double>(dz) + m_rotmom.z);
    m_rotmom.w = static_cast<float>(static_cast<double>(dw) + m_rotmom.w);
    UpdateWheelRotations();
    if (m_wheelsInContact == 0 && m_collisionImpulse == 0.0f)
        AlignInAir();
    UpdateRighting();
    UpdateDependents();
}

// 0x00438BB0
void CDrivableModule::Collide()
{
    m_collisionImpulse = 0.0f;
    Engine3D::CBoxProbe rect;
    rect.MakeNew();
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    rect.m_hitCache = &m_contactMemo;
    feeler.m_hitCache = &m_contactMemo;
    m_contactCount = 0;
    Contact* contacts = Contacts();
    // Each collider's face against the world: where it crosses the world's faces, and lines from the body's
    // position to its four corners.
    for (int i = 1; i >= 0; --i) {
        const Collider& collider = m_colliders[i];
        rect.m_rect.origin = collider.worldCorner;
        rect.m_rect.sideA = collider.worldEdges[0];
        rect.m_rect.sideB = collider.worldEdges[1];
        if (rect.Query(0)) {
            TriggerRect(mp_trigger, g_crossingEvents, &rect);
            if (Interrupted(this))
                return;
            rect.JoinHits();
            for (int k = rect.m_joinedCount - 1; k >= 0; --k) {
                const Engine3D::RectHit& hit = rect.m_joined[k];
                for (int e = 1; e >= 0; --e) {
                    Contact& contact = contacts[m_contactCount];
                    contact.point = hit.ends[e].point;
                    contact.normal = hit.normal;
                    contact.fromFeeler = false;
                    bool onEdge = hit.ends[e].onTriangleEdge;
                    if (!NormalFacesAway(&contact)) {
                        contact.depth = onEdge ? static_cast<float>(GetPenetration(&contact, &collider)) : 0.0f;
                        ++m_contactCount;
                    } else if (onEdge && FindEdgeContact(&contact, &rect, &collider, k, e)) {
                        ++m_contactCount;
                    }
                }
            }
        }
        if (m_debugMode)
            rect.DebugDraw(0xFF, 0xFF, 0xFF, 1);
        feeler.m_line.start = m_atPoint;
        feeler.m_line.end = rect.m_rect.origin;
        for (int corner = 0; corner < 4; ++corner) {
            if (corner == 1) {
                feeler.m_line.end = {static_cast<float>(static_cast<double>(feeler.m_line.end.x) + rect.m_rect.sideA.x),
                                     static_cast<float>(static_cast<double>(rect.m_rect.sideA.y) + feeler.m_line.end.y),
                                     static_cast<float>(static_cast<double>(rect.m_rect.sideA.z) + feeler.m_line.end.z),
                                     static_cast<float>(static_cast<double>(rect.m_rect.sideA.w) + feeler.m_line.end.w)};
            } else if (corner == 2) {
                feeler.m_line.end = {static_cast<float>(static_cast<double>(feeler.m_line.end.x) + rect.m_rect.sideB.x),
                                     static_cast<float>(static_cast<double>(rect.m_rect.sideB.y) + feeler.m_line.end.y),
                                     static_cast<float>(static_cast<double>(rect.m_rect.sideB.z) + feeler.m_line.end.z),
                                     static_cast<float>(static_cast<double>(rect.m_rect.sideB.w) + feeler.m_line.end.w)};
            } else if (corner == 3) {
                feeler.m_line.end = {static_cast<float>(static_cast<double>(feeler.m_line.end.x) - rect.m_rect.sideA.x),
                                     static_cast<float>(static_cast<double>(feeler.m_line.end.y) - rect.m_rect.sideA.y),
                                     static_cast<float>(static_cast<double>(feeler.m_line.end.z) - rect.m_rect.sideA.z),
                                     static_cast<float>(static_cast<double>(feeler.m_line.end.w) - rect.m_rect.sideA.w)};
            }
            if (feeler.GetHitShape(false, false)) {
                TriggerFeeler(mp_trigger, g_crossingEvents, &feeler);
                if (Interrupted(this))
                    return;
                Contact& contact = contacts[m_contactCount];
                contact.point = feeler.m_contactPos;
                contact.normal = feeler.m_surfaceDir;
                if (!NormalFacesAway(&contact)) {
                    // How far in: the rest of the line, along the normal.
                    const Maths::Seg& line = feeler.m_line;
                    double dz = static_cast<double>(line.start.z) - line.end.z;
                    double dy = static_cast<double>(line.start.y) - line.end.y;
                    double dx = static_cast<double>(line.start.x) - line.end.x;
                    contact.depth = static_cast<float>(
                        (1.0 - feeler.m_reach) * ((dz * feeler.m_surfaceDir.z + dy * feeler.m_surfaceDir.y) + dx * feeler.m_surfaceDir.x));
                    ++m_contactCount;
                    // (the original marks the next record, not this one)
                    contacts[m_contactCount].fromFeeler = true;
                }
            }
            if (m_debugMode)
                feeler.DebugDraw(0xFF, 0xFF, 0xFF, 1);
        }
    }
    if (m_contactCount == 0)
        return;
    for (int n = m_contactCount - 1; n >= 0; --n)
        contacts[n].impulse = 0.0f;
    Maths::Vec4 mom = m_linearMomentum, rotmom = m_rotmom, vel = m_vel;
    Maths::Vec4 rotvel = m_rotvel;

    // Impulses at the contacts closing in (with restitution), up to six passes until none is.
    for (int pass = 0;;) {
        bool separating = true;
        for (int n = m_contactCount - 1; n >= 0; --n) {
            Contact& contact = contacts[n];
            Maths::Vec4 r{static_cast<float>(static_cast<double>(contact.point.x) - m_atPoint.x),
                          static_cast<float>(static_cast<double>(contact.point.y) - m_atPoint.y),
                          static_cast<float>(static_cast<double>(contact.point.z) - m_atPoint.z),
                          static_cast<float>(static_cast<double>(contact.point.w) - m_atPoint.w)};
            Maths::Vec4 v;
            GetPointVelocity(&v, &r);
            const Maths::Vec4& nrm = contact.normal;
            double closingExact = (static_cast<double>(v.z) * nrm.z + static_cast<double>(v.x) * nrm.x) +
                                  static_cast<double>(v.y) * nrm.y;
            float closing = static_cast<float>(closingExact);
            if (closingExact > 0.0)
                continue;
            separating = false;
            float bounce = static_cast<float>(-(static_cast<double>(closing) * m_restitution));
            double denominator = GetImpulseDenominator(&r, &nrm);
            float impulse = static_cast<float>((static_cast<double>(bounce) - closing) * denominator);
            contact.impulse = static_cast<float>(static_cast<double>(impulse) + contact.impulse);
            Maths::Vec4 push{static_cast<float>(static_cast<double>(impulse) * nrm.x),
                             static_cast<float>(static_cast<double>(impulse) * nrm.y),
                             static_cast<float>(static_cast<double>(impulse) * nrm.z),
                             static_cast<float>(static_cast<double>(impulse) * nrm.w)};
            ApplyImpulseAtOffset(&push, &r);
        }
        ++pass;
        if (pass > 5 || separating)
            break;
    }

    // Pushed out by how deep each contact is (at most 2).
    for (int n = m_contactCount - 1; n >= 0; --n) {
        Contact& contact = contacts[n];
        if (contact.depth < 0.0f || contact.depth == 0.0f)
            continue;
        if (contact.depth > kMaxDepth)
            contact.depth = kMaxDepth;
        double push = static_cast<double>(m_depthCorrection) * contact.depth;
        float pushRounded = static_cast<float>(push);
        const Maths::Vec4& nrm = contact.normal;
        Maths::Vec4 force{static_cast<float>(static_cast<double>(pushRounded) * nrm.x),
                          static_cast<float>(static_cast<double>(pushRounded) * nrm.y),
                          static_cast<float>(static_cast<double>(pushRounded) * nrm.z), static_cast<float>(push * nrm.w)};
        Maths::Vec4 r{static_cast<float>(static_cast<double>(contact.point.x) - m_atPoint.x),
                      static_cast<float>(static_cast<double>(contact.point.y) - m_atPoint.y),
                      static_cast<float>(static_cast<double>(contact.point.z) - m_atPoint.z),
                      static_cast<float>(static_cast<double>(contact.point.w) - m_atPoint.w)};
        AddForceAtOffset(&force, &r);
    }

    // Friction against the sliding at each contact that took an impulse (on the velocities as they were, updated
    // by each friction impulse in turn); at most the friction coefficient times that impulse.
    float friction = m_basisFrame.row[1].y > kUpright ? m_dragFactor : m_tippedFriction;
    for (int n = m_contactCount - 1; n >= 0; --n) {
        Contact& contact = contacts[n];
        if (contact.impulse == 0.0f)
            continue;
        const Maths::Vec4& nrm = contact.normal;
        float rz = static_cast<float>(static_cast<double>(contact.point.z) - m_atPoint.z);
        float ry = static_cast<float>(static_cast<double>(contact.point.y) - m_atPoint.y);
        Maths::Vec4 r{static_cast<float>(static_cast<double>(contact.point.x) - m_atPoint.x), ry, rz,
                      static_cast<float>(static_cast<double>(contact.point.w) - m_atPoint.w)};
        double cz = static_cast<double>(ry) * rotvel.x - static_cast<double>(r.x) * rotvel.y;
        double cy = static_cast<double>(r.x) * rotvel.z - static_cast<double>(rz) * rotvel.x;
        float cx = static_cast<float>(static_cast<double>(rz) * rotvel.y - static_cast<double>(ry) * rotvel.z);
        float pvz = static_cast<float>(cz + vel.z);
        double pvy = cy + vel.y;
        double pvx = static_cast<double>(cx) + vel.x;
        double along = (static_cast<double>(pvz) * nrm.z + pvx * nrm.x) + pvy * nrm.y;
        float tz = static_cast<float>(along * nrm.z);
        float ty = static_cast<float>(along * nrm.y);
        float tx = static_cast<float>(along * nrm.x);
        float slideW = static_cast<float>(vel.w - along * nrm.w);
        float slideZ = static_cast<float>(static_cast<double>(pvz) - tz);
        float slideY = static_cast<float>(pvy - ty);
        double slideX = pvx - tx;
        double speed = std::sqrt((slideX * slideX + static_cast<double>(slideY) * slideY) + static_cast<double>(slideZ) * slideZ);
        float speedRounded = static_cast<float>(speed);
        if (speed < kMinSlide)
            continue;
        double inverse = 1.0 / speedRounded;
        Maths::Vec4 slide{static_cast<float>(slideX * inverse), static_cast<float>(slideY * inverse),
                          static_cast<float>(slideZ * inverse), static_cast<float>(slideW * inverse)};
        double stop = GetImpulseDenominator(&r, &slide) * speedRounded;
        float most = static_cast<float>(static_cast<double>(friction) * contact.impulse);
        if (stop > most)
            stop = most;
        double against = -stop;
        Maths::Vec4 push{static_cast<float>(static_cast<double>(slide.x) * against),
                         static_cast<float>(static_cast<double>(slide.y) * against),
                         static_cast<float>(static_cast<double>(slide.z) * against),
                         static_cast<float>(static_cast<double>(slide.w) * against)};
        ApplyImpulseAtOffset(&push, &r);
        // The copies of the momenta and velocities followed along.
        mom.x = static_cast<float>(static_cast<double>(push.x) + mom.x);
        mom.y = static_cast<float>(static_cast<double>(push.y) + mom.y);
        mom.z = static_cast<float>(static_cast<double>(push.z) + mom.z);
        mom.w = static_cast<float>(static_cast<double>(push.w) + mom.w);
        double az = static_cast<double>(push.y) * r.x - static_cast<double>(push.x) * ry;
        double ay = static_cast<double>(push.x) * rz - static_cast<double>(push.z) * r.x;
        double ax = static_cast<double>(push.z) * ry - static_cast<double>(push.y) * rz;
        rotmom.x = static_cast<float>(rotmom.x + ax);
        rotmom.y = static_cast<float>(ay + rotmom.y);
        rotmom.z = static_cast<float>(az + rotmom.z);
        double invMass = m_invMass;
        vel = {static_cast<float>(invMass * mom.x), static_cast<float>(invMass * mom.y),
               static_cast<float>(invMass * mom.z), static_cast<float>(invMass * mom.w)};
        Maths::Vec4 spin;
        m_invInertia.RotateVector(spin, rotmom);
        rotvel.x = spin.x;
        rotvel.y = spin.y;
        rotvel.z = spin.z;
    }

    for (int n = m_contactCount - 1; n >= 0; --n) {
        float impulse = contacts[n].impulse;
        if (impulse > m_collisionImpulse)
            m_collisionImpulse = impulse;
    }

    // Steering against a wall: a push along it, by how much the wall faces the vehicle's front.
    if (m_steering == 0.0f || static_cast<uint32_t>(m_wheelsInContact) < 3 || m_handbrake)
        return;
    for (int n = m_contactCount - 1; n >= 0; --n) {
        Contact& contact = contacts[n];
        Maths::Vec4 r{static_cast<float>(static_cast<double>(contact.point.x) - m_atPoint.x),
                      static_cast<float>(static_cast<double>(contact.point.y) - m_atPoint.y),
                      static_cast<float>(static_cast<double>(contact.point.z) - m_atPoint.z),
                      static_cast<float>(static_cast<double>(contact.point.w) - m_atPoint.w)};
        const Maths::Vec4& nrm = contact.normal;
        const Maths::Vec4& at = m_basisFrame.row[2];
        float strength = static_cast<float>(((static_cast<double>(at.x) * nrm.x + static_cast<double>(at.z) * nrm.z) +
                                             static_cast<double>(nrm.y) * at.y) *
                                            contact.impulse * m_wallSteering * m_steering);
        Maths::Vec4 push{static_cast<float>(static_cast<double>(strength) * -nrm.z),
                         static_cast<float>(static_cast<double>(strength) * 0.0f),
                         static_cast<float>(static_cast<double>(strength) * nrm.x), strength};
        ApplyImpulseAtOffset(&push, &r);
    }
}

} // namespace Obj
