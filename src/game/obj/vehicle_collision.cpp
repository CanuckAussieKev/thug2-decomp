// The Vehicle component's collision and integration: the colliders against the world (contact points, impulses,
// depth correction, friction, steering against walls), moving the body without tunnelling, righting itself, turning
// in the air, and a whole sub-step. See vehicle_component.h. From the disassembly of 0x00436CF0-0x00437549,
// 0x00438770-0x00438A16, 0x00438BB0-0x00439D86, 0x00439D90-0x0043A066 and 0x0043BEE0-0x0043C19A. Float arithmetic
// follows the original's x87 code: double (53 bits) where it stays on the stack, rounded to float where stored.

#include "game/obj/vehicle_component.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/gfx/debug_lines.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/nx/coll_rect.h"
#include "game/obj/vehicle_internal.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

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

const int* const kCrossing = reinterpret_cast<const int*>(0x00646698); // trigger events {9, 0, 0}

// The model component's 0x00420670 (not rewritten yet): its lighting from what a feeler hit.
void LightFromFeeler(CBaseComponent* model, Nx::CFeeler* feeler, bool b, float c)
{
    GameFunction<void(__thiscall*)(CBaseComponent*, Nx::CFeeler*, bool, float)>(0x00420670)(model, feeler, b, c);
}

bool Interrupted(const CVehicleComponent* vehicle)
{
    return vehicle->m_interrupted || (vehicle->m_object->m_objectFlags & OBJECT_FLAG_DEAD);
}

// A slerp between two matrices, on the stack without a constructor (as the original's).
struct SlerpStorage {
    alignas(Mth::SlerpInterpolator) uint8_t bytes[sizeof(Mth::SlerpInterpolator)];
    Mth::SlerpInterpolator* Get() { return reinterpret_cast<Mth::SlerpInterpolator*>(bytes); }
};

} // namespace

// 0x00436CF0 (stdcall)
double __stdcall CVehicleComponent::GetPenetration(const Contact* contact, const Collider* collider)
{
    // The contact point's distance in from each of the face's four edges; the nearest.
    double dx = static_cast<double>(contact->point.x) - collider->worldCorner.x;
    double dy = static_cast<double>(contact->point.y) - collider->worldCorner.y;
    double dz = static_cast<double>(contact->point.z) - collider->worldCorner.z;
    const Mth::Vector& a = collider->directions[0];
    const Mth::Vector& b = collider->directions[1];
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
    const Mth::Vector& n = contact->normal;
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
bool CVehicleComponent::FindEdgeContact(Contact* contact, Nx::CRectFeeler* rect, const Collider* collider, int hit, int end)
{
    // Another joined hit with an end at this point (within 0.1) facing the opposite way.
    for (int k = rect->m_joinedCount - 1; k >= 0; --k) {
        if (k == hit)
            continue;
        const Nx::RectHit& other = rect->m_joined[k];
        for (int e = 1; e >= 0; --e) {
            const Mth::Vector& p = other.ends[e].point;
            if (!(std::fabs(static_cast<double>(contact->point.x) - p.x) < kSameEnd))
                continue;
            if (!(std::fabs(static_cast<double>(contact->point.y) - p.y) < kSameEnd))
                continue;
            if (!(std::fabs(static_cast<double>(contact->point.z) - p.z) < kSameEnd))
                continue;
            const Mth::Vector& n = contact->normal;
            if (!((static_cast<double>(n.z) * other.normal.z + static_cast<double>(n.y) * other.normal.y) +
                      static_cast<double>(n.x) * other.normal.x <
                  kOpposite))
                continue;
            // On the edge between them: the normal across the hit's triangle edge, and the depth.
            const Mth::Vector& edge = rect->m_joined[hit].ends[end].edge;
            double x = static_cast<double>(edge.y) * n.z - static_cast<double>(edge.z) * n.y;
            double y = static_cast<double>(edge.z) * n.x - static_cast<double>(edge.x) * n.z;
            contact->normal.w = 0.0f;
            contact->normal.z = static_cast<float>(static_cast<double>(edge.x) * n.y - static_cast<double>(edge.y) * n.x);
            contact->normal.x = static_cast<float>(x);
            contact->normal.y = static_cast<float>(y);
            contact->depth = static_cast<float>(GetPenetration(contact, collider));
            if (m_debugMode) {
                Mth::Vector tip{static_cast<float>(static_cast<double>(contact->normal.x) * 72.0f + contact->point.x),
                                static_cast<float>(static_cast<double>(contact->normal.y) * 72.0f + contact->point.y),
                                static_cast<float>(static_cast<double>(static_cast<float>(contact->normal.z * 72.0f)) +
                                                   contact->point.z),
                                static_cast<float>(static_cast<double>(static_cast<float>(contact->normal.w * 72.0f)) +
                                                   contact->point.w)};
                Gfx::AddDebugLine(&contact->point, &tip, 0xFFFF64FF, 0xFFFF64FF, 1);
            }
            return true;
        }
    }
    return false;
}

// 0x00437060
bool CVehicleComponent::NormalFacesAway(const Contact* contact)
{
    const Mth::Vector& p = contact->point;
    const Mth::Vector& n = contact->normal;
    double along = ((static_cast<double>(p.z) - m_pos.z) * n.z + (static_cast<double>(p.y) - m_pos.y) * n.y) +
                   (static_cast<double>(p.x) - m_pos.x) * n.x;
    bool away = along >= 0.0;
    if (m_debugMode && away) {
        Mth::Vector tip{static_cast<float>(static_cast<double>(n.x) * 48.0f + p.x),
                        static_cast<float>(static_cast<double>(n.y) * 48.0f + p.y),
                        static_cast<float>(static_cast<double>(static_cast<float>(n.z * 48.0f)) + p.z),
                        static_cast<float>(static_cast<double>(static_cast<float>(n.w * 48.0f)) + p.w)};
        Gfx::AddDebugLine(&p, &tip, 0xFF000000, 0xFF000000, 1);
    }
    return away;
}

// 0x00437120
void CVehicleComponent::MoveBy(const Mth::Vector* move)
{
    Mth::Vector saved = m_pos;
    Mth::Vector end{static_cast<float>(static_cast<double>(move->x) + m_pos.x),
                    static_cast<float>(static_cast<double>(m_pos.y) + move->y),
                    static_cast<float>(static_cast<double>(m_pos.z) + move->z),
                    static_cast<float>(static_cast<double>(m_pos.w) + move->w)};
    Nx::CFeeler feeler;
    feeler.Construct(&m_pos, &end);
    if (feeler.GetCollision(false, false)) {
        // Going through something: put just off the surface, the rest of the move reflected, the momentum into it
        // turned back by half again.
        Dbg::Printf(kDebugFormat, kDebugFile, 0xB10, reinterpret_cast<const char*>(0x00646748)); // "less than uber frig"
        const Mth::Vector& point = feeler.m_point;
        const Mth::Vector& n = feeler.m_normal;
        m_pos.x = static_cast<float>(static_cast<double>(n.x) + point.x);
        m_pos.y = static_cast<float>(static_cast<double>(point.y) + n.y);
        m_pos.z = static_cast<float>(static_cast<double>(point.z) + n.z);
        m_pos.w = static_cast<float>(static_cast<double>(point.w) + n.w);
        double distance = feeler.m_distance;
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
        m_pos.x = static_cast<float>(static_cast<double>(rx) + m_pos.x);
        m_pos.y = static_cast<float>(static_cast<double>(ry) + m_pos.y);
        m_pos.z = static_cast<float>(rz + m_pos.z);
        m_pos.w = static_cast<float>(static_cast<double>(rw) + m_pos.w);
        double into = ((static_cast<double>(n.z) * m_mom.z + static_cast<double>(n.y) * m_mom.y) + static_cast<double>(n.x) * m_mom.x) *
                      kBounce;
        m_mom.x = static_cast<float>(m_mom.x - static_cast<double>(n.x) * into);
        m_mom.y = static_cast<float>(m_mom.y - static_cast<double>(n.y) * into);
        m_mom.z = static_cast<float>(m_mom.z - static_cast<double>(n.z) * into);
        m_mom.w = static_cast<float>(m_mom.w - static_cast<double>(n.w) * into);
    } else {
        m_pos.x = static_cast<float>(static_cast<double>(m_pos.x) + move->x);
        m_pos.y = static_cast<float>(static_cast<double>(move->y) + m_pos.y);
        m_pos.z = static_cast<float>(static_cast<double>(m_pos.z) + move->z);
        m_pos.w = static_cast<float>(static_cast<double>(m_pos.w) + move->w);
    }
    // Nothing below at all (4800 down): through the world - put back where it was. Else the model lit from the
    // ground.
    feeler.m_line.start = m_pos;
    feeler.m_line.end = m_pos;
    feeler.m_line.end.y = static_cast<float>(static_cast<double>(m_pos.y) - kDropTest);
    if (!feeler.GetCollision(false, false)) {
        Dbg::Printf(kDebugFormat, kDebugFile, 0xB26, reinterpret_cast<const char*>(0x00646738)); // "uber frig"
        m_pos = saved;
    } else {
        LightFromFeeler(mp_model, &feeler, true, 1.0f);
    }
}

// 0x00437450
double CVehicleComponent::GetImpulseDenominator(const Mth::Vector* offset, const Mth::Vector* direction)
{
    // 1 / (n . (n / m + (I^-1 (r x n)) x r)).
    const Mth::Vector& r = *offset;
    const Mth::Vector& n = *direction;
    float invMass = m_invMass;
    Mth::Vector cross;
    cross.x = static_cast<float>(static_cast<double>(n.z) * r.y - static_cast<double>(r.z) * n.y);
    cross.y = static_cast<float>(static_cast<double>(r.z) * n.x - static_cast<double>(r.x) * n.z);
    cross.z = static_cast<float>(static_cast<double>(n.y) * r.x - static_cast<double>(n.x) * r.y);
    cross.w = 0.0f;
    float linearX = static_cast<float>(static_cast<double>(n.x) * invMass);
    float linearY = static_cast<float>(static_cast<double>(invMass) * n.y);
    float linearZ = static_cast<float>(static_cast<double>(n.z) * invMass);
    Mth::Vector w;
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
void CVehicleComponent::UpdateRighting()
{
    QuatToMatrix(&m_orientation, &m_matrix);
    m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (m_matrix.row[1].y > kRightingUp) {
        m_righting = false;
        return;
    }
    // Upside down (or on its side), still, after a collision, with the throttle held: turn upright.
    double vx = m_vel.x, vy = m_vel.y, vz = m_vel.z;
    double sx = m_rotvel.x, sy = m_rotvel.y, sz = m_rotvel.z;
    bool stuck = (vx * vx + vy * vy) + vz * vz < kRightingSpeed && (sx * sx + sy * sy) + sz * sz < kRightingSpin &&
                 m_collisionImpulse > 0.0f && m_matrix.row[1].y < kRightingTilt &&
                 (m_wheelsInContact == 0 || m_matrix.row[1].y < 0.0f) && m_throttle;
    if (!m_righting) {
        if (!stuck)
            return;
        m_mom = {0.0f, 0.0f, 0.0f, 1.0f};
        m_rotmom = {0.0f, 0.0f, 0.0f, 1.0f};
        m_righting = true;
        m_rightingStart = Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60);
    }
    int start = m_rightingStart;
    if (static_cast<uint32_t>(Tmr::GameClockElapsed()) + (GameVariable<uint32_t>(0x00696D60) - static_cast<uint32_t>(start)) >
        5300) {
        m_righting = false;
        return;
    }
    start = m_rightingStart;
    if (static_cast<uint32_t>(Tmr::GameClockElapsed()) + (GameVariable<uint32_t>(0x00696D60) - static_cast<uint32_t>(start)) <
        300)
        return;
    // After 300 ms, turned towards upright facing the same way.
    Mth::Vector forward{m_matrix.row[2].x, 0.0f, m_matrix.row[2].z, m_matrix.row[2].w};
    forward.Normalize();
    Mth::Matrix target;
    target.row[0] = {forward.z, 0.0f, -forward.x, 1.0f};
    target.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    target.row[2] = forward;
    target.row[3] = {0.0f, 0.0f, 0.0f, 1.0f}; // (the original leaves its stack there)
    SlerpStorage storage;
    Mth::SlerpInterpolator* slerp = storage.Get();
    slerp->SetMatrices(m_matrix, target);
    double t = Tmr::GetSlomoFrameSeconds() / slerp->angle * kRightingRate;
    slerp->GetMatrix(m_matrix, t > 1.0 ? 1.0f : static_cast<float>(t));
    Mth::Vector quat;
    MatrixToQuat(&quat, &m_matrix);
    m_orientation = quat;
}

// 0x00439D90
void CVehicleComponent::AlignInAir()
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
    QuatToMatrix(&m_orientation, &m_matrix);
    m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Slow: towards its facing (level); fast: towards where it's going.
    Mth::Vector at;
    if (slow)
        at = {m_matrix.row[2].x, 0.0f, m_matrix.row[2].z, m_matrix.row[2].w};
    else
        at = m_vel;
    at.Normalize();
    if ((static_cast<double>(at.z) * m_matrix.row[2].z + static_cast<double>(at.y) * m_matrix.row[2].y) +
            static_cast<double>(at.x) * m_matrix.row[2].x <
        0.0) {
        at.x = at.x * -1.0f;
        at.y = at.y * -1.0f;
        at.z = at.z * -1.0f;
        at.w = at.w * -1.0f;
    }
    Mth::Vector up{0.0f, 1.0f, 0.0f, 1.0f};
    Mth::Matrix target;
    target.row[0] = Mth::CrossProduct(up, at);
    target.row[1] = Mth::CrossProduct(at, target.row[0]);
    target.row[2] = at;
    target.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    SlerpStorage storage;
    Mth::SlerpInterpolator* slerp = storage.Get();
    slerp->SetMatrices(m_matrix, target);
    // The turn ramps in over alignTime of free fall; slower under 350.
    vx = m_vel.x;
    vz = m_vel.z;
    bool fast = vx * vx + vz * vz > kAlignFast;
    float time = m_alignTime;
    float falling = m_freeFallTime;
    double ramp = falling < time ? falling : time;
    double rate = fast ? static_cast<double>(m_alignRate) * m_timestep
                       : static_cast<double>(m_alignRate) * m_timestep * kAlignSlowRate;
    slerp->GetMatrix(m_matrix, static_cast<float>(ramp / time * rate));
    Mth::Vector quat;
    MatrixToQuat(&quat, &m_matrix);
    m_orientation = quat;
}

// 0x0043BEE0
void CVehicleComponent::Integrate()
{
    // The accelerations.
    double invMass = m_invMass;
    float accelX = static_cast<float>(invMass * m_force.x), accelY = static_cast<float>(invMass * m_force.y);
    float accelZ = static_cast<float>(invMass * m_force.z), accelW = static_cast<float>(invMass * m_force.w);
    Mth::Vector spinAccel;
    m_invInertia.RotateVector(spinAccel, m_torque);
    // The momentum.
    double dt = m_timestep;
    float dy = static_cast<float>(dt * m_force.y), dz = static_cast<float>(dt * m_force.z);
    float dw = static_cast<float>(dt * m_force.w);
    m_mom.x = static_cast<float>(dt * m_force.x + m_mom.x);
    m_mom.y = static_cast<float>(static_cast<double>(dy) + m_mom.y);
    m_mom.z = static_cast<float>(static_cast<double>(dz) + m_mom.z);
    m_mom.w = static_cast<float>(static_cast<double>(dw) + m_mom.w);
    // The move: (velocity + half the acceleration over the step) over the step.
    float timestep = m_timestep;
    double half = static_cast<double>(m_timestep) * kHalf;
    float hy = static_cast<float>(accelY * half), hz = static_cast<float>(accelZ * half);
    float hw = static_cast<float>(accelW * half);
    double vx = accelX * half + m_vel.x;
    double vy = static_cast<double>(hy) + m_vel.y;
    float vz = static_cast<float>(static_cast<double>(hz) + m_vel.z);
    float vw = static_cast<float>(static_cast<double>(hw) + m_vel.w);
    Mth::Vector move{static_cast<float>(vx * timestep), static_cast<float>(vy * timestep),
                     static_cast<float>(static_cast<double>(vz) * timestep), static_cast<float>(static_cast<double>(vw) * timestep)};
    MoveBy(&move);
    // The orientation: q += q * (-dt / 2) * (angular velocity + half the angular acceleration over the step).
    half = static_cast<double>(m_timestep) * kHalf;
    Mth::Vector quat = m_orientation;
    double wx = spinAccel.x * half + m_rotvel.x;
    float wy = static_cast<float>(static_cast<double>(static_cast<float>(spinAccel.y * half)) + m_rotvel.y);
    float wz = static_cast<float>(static_cast<double>(static_cast<float>(spinAccel.z * half)) + m_rotvel.z);
    float scale = static_cast<float>(static_cast<double>(m_timestep) * kMinusHalf);
    Mth::Vector turn{static_cast<float>(wx * scale), static_cast<float>(static_cast<double>(wy) * scale),
                     static_cast<float>(static_cast<double>(wz) * scale), 0.0f};
    QuatMultiply(&quat, &turn);
    m_orientation.x = static_cast<float>(static_cast<double>(quat.x) + m_orientation.x);
    m_orientation.y = static_cast<float>(static_cast<double>(quat.y) + m_orientation.y);
    m_orientation.z = static_cast<float>(static_cast<double>(quat.z) + m_orientation.z);
    m_orientation.w = static_cast<float>(static_cast<double>(quat.w) + m_orientation.w);
    QuatNormalize(&m_orientation, 1.0f);
    // The angular momentum.
    dt = m_timestep;
    dy = static_cast<float>(dt * m_torque.y);
    dz = static_cast<float>(dt * m_torque.z);
    dw = static_cast<float>(dt * m_torque.w);
    m_rotmom.x = static_cast<float>(dt * m_torque.x + m_rotmom.x);
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
void CVehicleComponent::Collide()
{
    m_collisionImpulse = 0.0f;
    Nx::CRectFeeler rect;
    rect.Construct();
    Nx::CFeeler feeler;
    feeler.Construct();
    rect.mp_cache = &m_collCache;
    feeler.mp_cache = &m_collCache;
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
            TriggerRect(mp_trigger, kCrossing, &rect);
            if (Interrupted(this))
                return;
            rect.JoinHits();
            for (int k = rect.m_joinedCount - 1; k >= 0; --k) {
                const Nx::RectHit& hit = rect.m_joined[k];
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
        feeler.m_line.start = m_pos;
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
            if (feeler.GetCollision(false, false)) {
                TriggerFeeler(mp_trigger, kCrossing, &feeler);
                if (Interrupted(this))
                    return;
                Contact& contact = contacts[m_contactCount];
                contact.point = feeler.m_point;
                contact.normal = feeler.m_normal;
                if (!NormalFacesAway(&contact)) {
                    // How far in: the rest of the line, along the normal.
                    const Mth::Line& line = feeler.m_line;
                    double dz = static_cast<double>(line.start.z) - line.end.z;
                    double dy = static_cast<double>(line.start.y) - line.end.y;
                    double dx = static_cast<double>(line.start.x) - line.end.x;
                    contact.depth = static_cast<float>(
                        (1.0 - feeler.m_distance) * ((dz * feeler.m_normal.z + dy * feeler.m_normal.y) + dx * feeler.m_normal.x));
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
    Mth::Vector mom = m_mom, rotmom = m_rotmom, vel = m_vel;
    Mth::Vector rotvel = m_rotvel;

    // Impulses at the contacts closing in (with restitution), up to six passes until none is.
    for (int pass = 0;;) {
        bool separating = true;
        for (int n = m_contactCount - 1; n >= 0; --n) {
            Contact& contact = contacts[n];
            Mth::Vector r{static_cast<float>(static_cast<double>(contact.point.x) - m_pos.x),
                          static_cast<float>(static_cast<double>(contact.point.y) - m_pos.y),
                          static_cast<float>(static_cast<double>(contact.point.z) - m_pos.z),
                          static_cast<float>(static_cast<double>(contact.point.w) - m_pos.w)};
            Mth::Vector v;
            GetPointVelocity(&v, &r);
            const Mth::Vector& nrm = contact.normal;
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
            Mth::Vector push{static_cast<float>(static_cast<double>(impulse) * nrm.x),
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
        const Mth::Vector& nrm = contact.normal;
        Mth::Vector force{static_cast<float>(static_cast<double>(pushRounded) * nrm.x),
                          static_cast<float>(static_cast<double>(pushRounded) * nrm.y),
                          static_cast<float>(static_cast<double>(pushRounded) * nrm.z), static_cast<float>(push * nrm.w)};
        Mth::Vector r{static_cast<float>(static_cast<double>(contact.point.x) - m_pos.x),
                      static_cast<float>(static_cast<double>(contact.point.y) - m_pos.y),
                      static_cast<float>(static_cast<double>(contact.point.z) - m_pos.z),
                      static_cast<float>(static_cast<double>(contact.point.w) - m_pos.w)};
        AddForceAtOffset(&force, &r);
    }

    // Friction against the sliding at each contact that took an impulse (on the velocities as they were, updated
    // by each friction impulse in turn); at most the friction coefficient times that impulse.
    float friction = m_matrix.row[1].y > kUpright ? m_friction : m_tippedFriction;
    for (int n = m_contactCount - 1; n >= 0; --n) {
        Contact& contact = contacts[n];
        if (contact.impulse == 0.0f)
            continue;
        const Mth::Vector& nrm = contact.normal;
        float rz = static_cast<float>(static_cast<double>(contact.point.z) - m_pos.z);
        float ry = static_cast<float>(static_cast<double>(contact.point.y) - m_pos.y);
        Mth::Vector r{static_cast<float>(static_cast<double>(contact.point.x) - m_pos.x), ry, rz,
                      static_cast<float>(static_cast<double>(contact.point.w) - m_pos.w)};
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
        Mth::Vector slide{static_cast<float>(slideX * inverse), static_cast<float>(slideY * inverse),
                          static_cast<float>(slideZ * inverse), static_cast<float>(slideW * inverse)};
        double stop = GetImpulseDenominator(&r, &slide) * speedRounded;
        float most = static_cast<float>(static_cast<double>(friction) * contact.impulse);
        if (stop > most)
            stop = most;
        double against = -stop;
        Mth::Vector push{static_cast<float>(static_cast<double>(slide.x) * against),
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
        Mth::Vector spin;
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
        Mth::Vector r{static_cast<float>(static_cast<double>(contact.point.x) - m_pos.x),
                      static_cast<float>(static_cast<double>(contact.point.y) - m_pos.y),
                      static_cast<float>(static_cast<double>(contact.point.z) - m_pos.z),
                      static_cast<float>(static_cast<double>(contact.point.w) - m_pos.w)};
        const Mth::Vector& nrm = contact.normal;
        const Mth::Vector& at = m_matrix.row[2];
        float strength = static_cast<float>(((static_cast<double>(at.x) * nrm.x + static_cast<double>(at.z) * nrm.z) +
                                             static_cast<double>(nrm.y) * at.y) *
                                            contact.impulse * m_wallSteering * m_steering);
        Mth::Vector push{static_cast<float>(static_cast<double>(strength) * -nrm.z),
                         static_cast<float>(static_cast<double>(strength) * 0.0f),
                         static_cast<float>(static_cast<double>(strength) * nrm.x), strength};
        ApplyImpulseAtOffset(&push, &r);
    }
}

} // namespace Obj

REPLACE(0x00436CF0, Obj::CVehicleComponent::GetPenetration);
REPLACE(0x00436E50, &Obj::CVehicleComponent::FindEdgeContact);
REPLACE(0x00437060, &Obj::CVehicleComponent::NormalFacesAway);
REPLACE(0x00437120, &Obj::CVehicleComponent::MoveBy);
REPLACE(0x00437450, &Obj::CVehicleComponent::GetImpulseDenominator);
REPLACE(0x00438770, &Obj::CVehicleComponent::UpdateRighting);
REPLACE(0x00439D90, &Obj::CVehicleComponent::AlignInAir);
REPLACE(0x0043BEE0, &Obj::CVehicleComponent::Integrate);
REPLACE(0x00438BB0, &Obj::CVehicleComponent::Collide);
