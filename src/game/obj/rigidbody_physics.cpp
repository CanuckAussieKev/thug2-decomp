// The rigid body component's physics: the frame's update, integration, collisions with the world, contacts and
// springs (game/obj/rigidbody_component.h).
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value is rounded where the original rounds it, and sums keep its grouping.

#include <cmath>
#include <cstdint>

#include "game/gfx/debug_lines.h"
#include "game/maths/maths.h"
#include "game/maths/quat_rotation.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/sound_component.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sound/sfx.h"
#include "game/sys/stopwatch.h"

namespace GameObjects {

bool g_rigidBodyDrawShape;
bool g_rigidBodyDrawDetails;

namespace {

constexpr float kZero = 0.0f;                     // 0x0066D520 (rotational w changes by 0 * mass_over_moment)
constexpr float kSpringMinSpeed = 0.1f;           // 0x00645D74
constexpr float kSpringScale = -10.0f;            // 0x006463E0
constexpr float kHalf = 0.5f;                     // 0x006453BC
constexpr float kTurnRate = -0.5f;                // 0x0064642C
constexpr float kMinLength = 0.001f;              // 0x006461D8
constexpr float kHitBackOff = 6.0f;               // 0x00646284
constexpr float kDebugImpulseScale = 60.0f;       // 0x00645EA0
constexpr int kMaxRounds = 20;
constexpr int kMaxSteps = 5;

double Wide(float value) { return value; }
float Narrow(double value) { return static_cast<float>(value); }
// Two floats added / subtracted on the x87 (at double precision, then stored: rounded twice).
float AppendOne(float a, float b) { return Narrow(Wide(a) + b); }
float Sub(float a, float b) { return Narrow(Wide(a) - b); }

// Debug drawing switches (0x006939D4: the shape each frame; 0x006939D5: the centre, the contacts' pushes).
bool DrawShape() { return g_rigidBodyDrawShape; }
bool DrawDetails() { return g_rigidBodyDrawDetails; }

// Quaternions: `quat` times `other`, in place (0x00420140); the matrix of one.
void QuatMultiply(Maths::QuatRotation* quat, const Maths::QuatRotation* other) { *quat *= *other; }
void QuatToMatrix(const Maths::QuatRotation* quat, Maths::Mat4* matrix) { quat->FetchMat(matrix); }

void FlagAsDead(CGameEntity* object)
{
    reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[6])(object);
}

// The matrix's element at row `r`, column `c`.
double At(const Maths::Mat4& m, int r, int c) { return reinterpret_cast<const float*>(&m.row[r])[c]; }

} // namespace

// 0x0042CEE0: 1 / (n . (n + (m (r x n)) x r)) - m the mass over the moment.
double CBodyDynamicsModule::ImpulseScale(const Maths::Vec4* normal, const Maths::Vec4* offset) const
{
    const Maths::Vec4& n = *normal;
    const Maths::Vec4& r = *offset;
    const double m = m_massOverMoment;
    // a = m (r x n): x rounded, y and z not (z from its rounded cross product)
    float ax = Narrow((Wide(r.y) * n.z - Wide(r.z) * n.y) * m);
    double ay = (Wide(n.x) * r.z - Wide(r.x) * n.z) * m;
    double az = Narrow(Wide(r.x) * n.y - Wide(n.x) * r.y) * m;
    // b = a x r, plus n
    float sx = Narrow(Narrow(ay * r.z - az * r.y) + Wide(n.x));
    double sy = (az * r.x - ax * Wide(r.z)) + n.y;
    double sz = Wide(Narrow(ax * Wide(r.y) - ay * r.x)) + n.z;
    return 1.0 / ((sx * Wide(n.x) + sz * n.z) + sy * n.y);
}

// 0x0042CF90
void CBodyDynamicsModule::ApplySprings()
{
    const Maths::Vec4* world = WorldSpringPoints();
    // The springs' points held still: each moving point's velocity taken out by an impulse, round after round.
    int round = 0;
    bool settled;
    do {
        settled = true;
        for (int i = m_springCount; i != 0;) {
            --i;
            if (!mp_springs[i].active)
                continue;
            const Maths::Vec4& r = world[i];
            // its velocity: vel + rotvel x r
            double cx = Wide(r.z) * m_rotVel.y - Wide(r.y) * m_rotVel.z;
            double cy = Wide(m_rotVel.z) * r.x - Wide(r.z) * m_rotVel.x;
            float cz = Narrow(Wide(r.y) * m_rotVel.x - Wide(m_rotVel.y) * r.x);
            Maths::Vec4 v = {Narrow(cx + m_vel.x), Narrow(cy + m_vel.y), Narrow(Wide(cz) + m_vel.z), m_vel.w};
            double length = std::sqrt((Wide(v.z) * v.z + Wide(v.y) * v.y) + Wide(v.x) * v.x);
            float lengthF = Narrow(length);
            if (length < kSpringMinSpeed)
                continue;
            settled = false;
            const double m = m_massOverMoment;
            double inverse = 1.0 / lengthF;
            double nx = v.x * inverse;
            float ny = Narrow(v.y * inverse), nz = Narrow(v.z * inverse), nw = Narrow(v.w * inverse);
            // the impulse that stops it (as ImpulseScale, inlined with the normal's x unrounded)
            float ax = Narrow((Wide(nz) * r.y - Wide(ny) * r.z) * m);
            double ay = (nx * r.z - Wide(nz) * r.x) * m;
            double az = Narrow(Wide(ny) * r.x - nx * r.y) * m;
            float sx = Narrow(Wide(Narrow(ay * r.z - az * r.y)) + nx);
            double sy = (az * r.x - ax * Wide(r.z)) + ny;
            double sz = Wide(Narrow(ax * Wide(r.y) - ay * r.x)) + nz;
            double scale = 1.0 / ((sx * nx + sz * nz) + sy * ny);
            float j = Narrow(-(scale * lengthF));
            double jx = nx * j, jy = Wide(ny) * j, jz = Wide(nz) * j, jw = Wide(nw) * j;
            m_vel = {Narrow(jx + m_vel.x), Narrow(jy + m_vel.y), Narrow(jz + m_vel.z), Narrow(jw + m_vel.w)};
            float tx = Narrow(jz * r.y - jy * r.z), ty = Narrow(jx * r.z - jz * r.x);
            double tz = jy * r.x - jx * r.y;
            m_rotVel = {Narrow(Wide(Narrow(tx * m)) + m_rotVel.x), Narrow(Wide(Narrow(ty * m)) + m_rotVel.y),
                        Narrow(Wide(Narrow(tz * m)) + m_rotVel.z), Narrow(m * kZero + m_rotVel.w)};
        }
    } while (++round != kMaxRounds && m_springCount != 1 && !settled);

    // Each point pulled towards its anchor.
    float dt = Narrow(FrameClock::GetSlomoFrameSeconds());
    for (int i = m_springCount; i != 0;) {
        --i;
        const Spring& spring = mp_springs[i];
        if (!spring.active)
            continue;
        const Maths::Vec4& r = world[i];
        float px = Narrow(Wide(m_atPoint.x) + r.x);
        double py = Wide(m_atPoint.y) + r.y, pz = Wide(m_atPoint.z) + r.z;
        float pw = Narrow(Wide(m_atPoint.w) + r.w);
        float dx = Narrow(px - Wide(spring.anchor.x)), dy = Narrow(py - spring.anchor.y);
        double dz = pz - spring.anchor.z;
        float dw = Narrow(pw - Wide(spring.anchor.w));
        float length = Narrow(std::sqrt((Wide(dx) * dx + dz * dz) + Wide(dy) * dy));
        if (length == kZero)
            continue;
        const double m = m_massOverMoment;
        double inverse = 1.0 / length;
        double nx = dx * inverse;
        float ny = Narrow(dy * inverse), nz = Narrow(dz * inverse), nw = Narrow(dw * inverse);
        double force = ((Wide(length) * m_springConst) * dt) * kSpringScale;
        float forceF = Narrow(force);
        double fx = nx * force, fy = Wide(ny) * forceF;
        float fz = Narrow(Wide(nz) * forceF), fw = Narrow(Wide(nw) * forceF);
        double tx = Wide(fz) * r.y - fy * r.z, ty = fx * r.z - Wide(fz) * r.x;
        float tz = Narrow(fy * r.x - fx * r.y);
        Maths::Vec4 turn = {Narrow(tx * m), Narrow(ty * m), Narrow(tz * m), Narrow(m * kZero)};
        m_vel = {Narrow(fx + m_vel.x), Narrow(fy + m_vel.y), Narrow(Wide(fz) + m_vel.z), Narrow(Wide(fw) + m_vel.w)};
        m_rotVel = {AppendOne(turn.x, m_rotVel.x), AppendOne(turn.y, m_rotVel.y), AppendOne(turn.z, m_rotVel.z), AppendOne(turn.w, m_rotVel.w)};
    }
}

// 0x0042D9D0
void CBodyDynamicsModule::DebugDraw()
{
    uint32_t colour = 0;
    switch (m_curPhase) {
    case STATE_ASLEEP: colour = 0xFFC80000; break;
    case STATE_AWAKE: colour = m_pushCooldown > kZero ? 0xFF00C800 : 0xFF0000C8; break;
    case STATE_DISABLED: colour = 0xFF640064; break;
    }
    auto at = [this](const Maths::Vec4& p) {
        return Maths::Vec4{p.x + m_atPoint.x, p.y + m_atPoint.y, p.z + m_atPoint.z, p.w + m_atPoint.w};
    };
    // Every pair of contact points.
    const Maths::Vec4* points = WorldContactPoints();
    for (int i = m_contactCount - 1; i > 0; --i)
        for (int j = 0; j < i; ++j) {
            Maths::Vec4 from = at(points[i]), to = at(points[j]);
            Gfx2D::PushDiagLine(&from, &to, colour, 0, 1);
        }
    const Maths::Vec4* centers = WorldSphereCenters();
    for (int i = m_sphereCount; i != 0;) {
        --i;
        Maths::Vec4 center = at(centers[i]);
        Gfx2D::AddDebugSphere(&center, mp_spheres[i].radius, &m_basisFrame, 12, colour, 1);
    }
    if (DrawDetails())
        Gfx2D::PushDiagMarker(m_atPoint, 36.0f, 0xFFC800C8, 1);
    const Maths::Vec4* springs = WorldSpringPoints();
    for (int i = m_springCount; i != 0;) {
        --i;
        if (!mp_springs[i].active)
            continue;
        Gfx2D::PushDiagMarker(at(springs[i]), 24.0f, 0xFFC86400, 1);
        Gfx2D::PushDiagMarker(mp_springs[i].anchor, 24.0f, 0xFFC80064, 1);
    }
}

// 0x0042E540
void CBodyDynamicsModule::Integrate(float dt)
{
    if (m_unknown191 < 3) {
        // Moved by its velocity and half a step of acceleration, then accelerated.
        double half = Wide(dt) * kHalf;
        double ax = half * m_steadyAccel.x;
        float ay = Narrow(half * m_steadyAccel.y), az = Narrow(half * m_steadyAccel.z), aw = Narrow(half * m_steadyAccel.w);
        float mx = Narrow(ax + m_vel.x);
        double my = Wide(ay) + m_vel.y, mz = Wide(az) + m_vel.z;
        float mw = Narrow(Wide(aw) + m_vel.w);
        float px = Narrow(Wide(mx) * dt), py = Narrow(my * dt);
        double pz = mz * dt, pw = Wide(mw) * dt;
        m_atPoint = {Narrow(Wide(px) + m_atPoint.x), Narrow(Wide(py) + m_atPoint.y), Narrow(pz + m_atPoint.z), Narrow(pw + m_atPoint.w)};
        double vx = Wide(dt) * m_steadyAccel.x, vy = Wide(dt) * m_steadyAccel.y;
        float vz = Narrow(Wide(dt) * m_steadyAccel.z), vw = Narrow(Wide(dt) * m_steadyAccel.w);
        m_vel = {Narrow(vx + m_vel.x), Narrow(vy + m_vel.y), Narrow(Wide(vz) + m_vel.z), Narrow(Wide(vw) + m_vel.w)};
    } else {
        double px = Wide(dt) * m_vel.x, py = Wide(dt) * m_vel.y;
        float pz = Narrow(Wide(dt) * m_vel.z), pw = Narrow(Wide(dt) * m_vel.w);
        m_atPoint = {Narrow(px + m_atPoint.x), Narrow(py + m_atPoint.y), Narrow(Wide(pz) + m_atPoint.z), Narrow(Wide(pw) + m_atPoint.w)};
    }

    // Turned: q += (q * (-rotvel / 2, 0)) dt, normalised.
    Maths::QuatRotation spin = {Narrow(Wide(m_rotVel.x) * kTurnRate), Narrow(Wide(m_rotVel.y) * kTurnRate),
                      Narrow(Wide(m_rotVel.z) * kTurnRate), 0.0f};
    Maths::QuatRotation change = m_orientation;
    QuatMultiply(&change, &spin);
    float cz = Narrow(Wide(change.z) * dt), cw = Narrow(Wide(change.w) * dt);
    m_orientation = {Narrow(Wide(change.x) * dt + m_orientation.x), Narrow(Wide(change.y) * dt + m_orientation.y),
                     AppendOne(cz, m_orientation.z), AppendOne(cw, m_orientation.w)};
    m_orientation.MakeUnit(1.0f);
    QuatToMatrix(&m_orientation, &m_basisFrame);

    // The shape turned to the world (each set of points adds its terms in its own order).
    const Maths::Mat4& t = m_basisFrame;
    for (int i = m_contactCount; i != 0;) {
        --i;
        const Maths::Vec4& c = mp_contacts[i];
        const double x = c.x, y = c.y, z = c.z;
        WorldContactPoints()[i] = {Narrow((y * At(t, 1, 0) + At(t, 2, 0) * z) + At(t, 0, 0) * x),
                                   Narrow((At(t, 0, 1) * x + At(t, 2, 1) * z) + y * At(t, 1, 1)),
                                   Narrow((At(t, 2, 2) * z + y * At(t, 1, 2)) + At(t, 0, 2) * x), c.w};
    }
    for (int i = m_sphereCount; i != 0;) {
        --i;
        const Maths::Vec4& c = mp_spheres[i].center;
        const double x = c.x, y = c.y, z = c.z;
        WorldSphereCenters()[i] = {Narrow((y * At(t, 1, 0) + At(t, 2, 0) * z) + x * At(t, 0, 0)),
                                   Narrow((x * At(t, 0, 1) + At(t, 2, 1) * z) + y * At(t, 1, 1)),
                                   Narrow((At(t, 2, 2) * z + x * At(t, 0, 2)) + y * At(t, 1, 2)), c.w};
    }
    for (int i = m_springCount; i != 0;) {
        --i;
        if (!mp_springs[i].active)
            continue;
        const Maths::Vec4& c = mp_springs[i].point;
        const double x = c.x, y = c.y, z = c.z;
        WorldSpringPoints()[i] = {Narrow((At(t, 2, 0) * z + At(t, 1, 0) * y) + x * At(t, 0, 0)),
                                  Narrow((At(t, 2, 1) * z + At(t, 1, 1) * y) + At(t, 0, 1) * x),
                                  Narrow((At(t, 2, 2) * z + At(t, 1, 2) * y) + At(t, 0, 2) * x), c.w};
    }
}

// 0x0042E980
void CBodyDynamicsModule::OnBounce(float speed, const Maths::Vec4* point)
{
    if (!(speed > m_bounceCallbackThreshold))
        return;
    if (m_bounceScript) {
        Scripts::CStruct* params = Scripts::SpawnParamBag();
        if (params)
            params = params->PrepareAll();
        if (mp_callbackParams)
            params->MergeStructure(mp_callbackParams); // (out of memory: through null, as the original)
        params->PutVec3(0x43F454D0, Scripts::VectorValue{point->x, point->y, point->z, point->w});
        m_entity->SpawnScript(m_bounceScript, params, 0, false)->TickFrame();
        if (params) {
            params->Clear();
            Scripts::DeleteStruct(params);
        }
    }
    if (m_audioModule && mp_bounceSound && 48 - SoundFx::CountPlayingVoices() > 12) {
        float percent = Maths::Confine(Narrow(Wide(speed) / m_bounceFullSpeed * 100.0f), 0.0f, 100.0f);
        mp_bounceSound->PushFloat(0x9E497FC6, percent); // Percent
        m_audioModule->EmitSound(mp_bounceSound);
    }
}

// 0x00430140
void CBodyDynamicsModule::ResolveContacts()
{
    Contact* contacts = Contacts();
    // The velocities before the impulses (friction works from these).
    Maths::Vec4 vel0 = m_vel, firstTurn = m_rotVel;
    float fastest = 0.0f; // the fastest approach

    // Impulses along the normals until nothing approaches.
    for (int round = 0;;) {
        bool done = true;
        for (int i = m_contactsHit; i != 0;) {
            Contact& c = contacts[--i];
            if (!c.active)
                continue;
            const Maths::Vec4& r = c.offset;
            const Maths::Vec4& n = c.normal;
            double cx = Wide(m_rotVel.y) * r.z - Wide(m_rotVel.z) * r.y;
            float cy = Narrow(Wide(m_rotVel.z) * r.x - Wide(m_rotVel.x) * r.z);
            float cz = Narrow(Wide(m_rotVel.x) * r.y - Wide(m_rotVel.y) * r.x);
            float vx = Narrow(cx + m_vel.x);
            double vy = Wide(cy) + m_vel.y, vz = Wide(cz) + m_vel.z;
            double along = (vz * n.z + Wide(vx) * n.x) + vy * n.y;
            if (along > 0.0)
                continue;
            float alongF = Narrow(along);
            done = false;
            if (-alongF > fastest)
                fastest = -alongF;
            float target = Narrow(-(Wide(alongF) * m_restitution));
            if (!c.scaled) {
                c.impulseScale = Narrow(ImpulseScale(&c.normal, &c.offset));
                c.scaled = true;
            }
            double j = (Wide(target) - alongF) * c.impulseScale;
            float jx = Narrow(Wide(n.x) * j);
            double jy = Wide(n.y) * j, jz = Wide(n.z) * j, jw = Wide(n.w) * j;
            c.impulse = Narrow(j + c.impulse);
            m_vel = {AppendOne(jx, m_vel.x), Narrow(jy + m_vel.y), Narrow(jz + m_vel.z), Narrow(jw + m_vel.w)};
            float tx = Narrow(jz * r.y - jy * r.z), ty = Narrow(Wide(jx) * r.z - jz * r.x);
            double tz = jy * r.x - Wide(jx) * r.y;
            const double m = m_massOverMoment;
            m_rotVel = {AppendOne(Narrow(tx * m), m_rotVel.x), AppendOne(Narrow(ty * m), m_rotVel.y), AppendOne(Narrow(tz * m), m_rotVel.z),
                        Narrow(m * kZero + m_rotVel.w)};
        }
        if (++round == kMaxRounds) {
            // Stuck: the script, or no velocity at all.
            if (m_scriptD0) {
                m_entity->SpawnScript(m_scriptD0, mp_callbackParams, 0, false)->TickFrame();
                return;
            }
            m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
            m_rotVel = {0.0f, 0.0f, 0.0f, 1.0f};
            return;
        }
        if (m_contactCount == 1 || done)
            break;
    }

    // Friction: across the normal (along the directed friction where there is one), at most the normal impulse
    // times the friction.
    for (int i = m_contactsHit; i != 0;) {
        Contact& c = contacts[--i];
        if (!c.active)
            continue;
        const Maths::Vec4& r = c.offset;
        const Maths::Vec4& n = c.normal;
        double cx = Wide(firstTurn.y) * r.z - Wide(firstTurn.z) * r.y;
        double cy = Wide(firstTurn.z) * r.x - Wide(firstTurn.x) * r.z;
        float cz = Narrow(Wide(firstTurn.x) * r.y - Wide(firstTurn.y) * r.x);
        float vx = Narrow(cx + vel0.x);
        double vy = cy + vel0.y, vz = Wide(cz) + vel0.z;
        double back = -((vz * n.z + Wide(vx) * n.x) + vy * n.y);
        Maths::Vec4 across = {Narrow(back * n.x + vx), Narrow(Wide(Narrow(back * n.y)) + vy),
                              Narrow(Wide(Narrow(back * n.z)) + vz), Narrow(Wide(Narrow(back * n.w)) + vel0.w)};
        if (mp_directedFriction) {
            const DirectedFriction& friction = mp_directedFriction[c.friction];
            if (friction.active) {
                Maths::Vec4 d;
                m_basisFrame.RotateVector(d, friction.direction);
                double dn = (Wide(d.x) * n.x + Wide(d.z) * n.z) + Wide(d.y) * n.y;
                float dny = Narrow(dn * n.y), dnz = Narrow(dn * n.z), dnw = Narrow(dn * n.w);
                d = {Narrow(d.x - dn * n.x), Sub(d.y, dny), Sub(d.z, dnz), Sub(d.w, dnw)};
                double length = std::sqrt((Wide(d.z) * d.z + Wide(d.y) * d.y) + Wide(d.x) * d.x);
                if (length > kMinLength) {
                    double inverse = 1.0 / length;
                    d = {Narrow(d.x * inverse), Narrow(d.y * inverse), Narrow(d.z * inverse), Narrow(d.w * inverse)};
                    across.ProjectOnto(d);
                }
            }
        }
        double length = std::sqrt((Wide(across.z) * across.z + Wide(across.y) * across.y) + Wide(across.x) * across.x);
        if (length < kMinLength)
            continue;
        float lengthF = Narrow(length);
        double inverse = 1.0 / lengthF;
        across = {Narrow(across.x * inverse), Narrow(across.y * inverse), Narrow(across.z * inverse),
                  Narrow(across.w * inverse)};
        double stop = ImpulseScale(&across, &c.offset) * lengthF;
        float most = Narrow(Wide(c.impulse) * m_dragFactor);
        double f = -(stop > most ? most : stop);
        double jx = across.x * f;
        float jy = Narrow(across.y * f), jz = Narrow(across.z * f), jw = Narrow(across.w * f);
        double tx = Wide(jz) * r.y - Wide(jy) * r.z, ty = jx * r.z - Wide(jz) * r.x;
        float tz = Narrow(Wide(jy) * r.x - jx * r.y);
        const double m = m_massOverMoment;
        Maths::Vec4 turn = {Narrow(tx * m), Narrow(ty * m), Narrow(tz * m), Narrow(m * kZero)};
        m_vel = {Narrow(jx + m_vel.x), AppendOne(jy, m_vel.y), AppendOne(jz, m_vel.z), AppendOne(jw, m_vel.w)};
        m_rotVel = {AppendOne(turn.x, m_rotVel.x), AppendOne(turn.y, m_rotVel.y), AppendOne(turn.z, m_rotVel.z), AppendOne(turn.w, m_rotVel.w)};
        vel0 = {Narrow(jx + vel0.x), AppendOne(jy, vel0.y), AppendOne(jz, vel0.z), AppendOne(jw, vel0.w)};
        firstTurn = {AppendOne(turn.x, firstTurn.x), AppendOne(turn.y, firstTurn.y), AppendOne(turn.z, firstTurn.z), AppendOne(turn.w, firstTurn.w)};
    }

    // Pushed out of the world by each contact's depth.
    float dt = Narrow(FrameClock::GetSlomoFrameSeconds());
    for (int i = m_contactsHit; i != 0;) {
        Contact& c = contacts[--i];
        if (!c.active)
            continue;
        const Maths::Vec4& r = c.offset;
        const Maths::Vec4& n = c.normal;
        double k = -((Wide(c.depth) * m_springConst) * dt);
        Maths::Vec4 push = {Narrow(k * n.x), Narrow(k * n.y), Narrow(k * n.z), Narrow(k * n.w)};
        double tx = Wide(push.z) * r.y - Wide(push.y) * r.z, ty = Wide(push.x) * r.z - Wide(push.z) * r.x;
        float tz = Narrow(Wide(push.y) * r.x - Wide(push.x) * r.y);
        const double m = m_massOverMoment;
        m_vel = {AppendOne(push.x, m_vel.x), AppendOne(push.y, m_vel.y), AppendOne(push.z, m_vel.z), AppendOne(push.w, m_vel.w)};
        m_rotVel = {AppendOne(Narrow(tx * m), m_rotVel.x), Narrow(ty * m + m_rotVel.y), Narrow(Wide(tz) * m + m_rotVel.z),
                    AppendOne(Narrow(m * kZero), m_rotVel.w)};
        if (DrawDetails()) {
            Maths::Vec4 from = {r.x + m_atPoint.x, m_atPoint.y + r.y, m_atPoint.z + r.z, m_atPoint.w + r.w};
            Maths::Vec4 to = {from.x + push.x * kDebugImpulseScale, from.y + push.y * kDebugImpulseScale,
                              from.z + push.z * kDebugImpulseScale, from.w + push.w * kDebugImpulseScale};
            Gfx2D::PushDiagLine(&to, &from, 0xFF00FFFF, 0, 1);
        }
    }

    // The bounce, at the last active contact.
    for (int i = m_contactsHit; i != 0;) {
        const Contact& c = contacts[--i];
        if (c.active) {
            Maths::Vec4 point = {AppendOne(c.offset.x, m_atPoint.x), AppendOne(c.offset.y, m_atPoint.y), AppendOne(c.offset.z, m_atPoint.z),
                                 AppendOne(c.offset.w, m_atPoint.w)};
            OnBounce(fastest, &point);
            return;
        }
    }
}

// 0x00430BE0
void CBodyDynamicsModule::Collide(Engine3D::CProbeRay* feeler)
{
    const Maths::Vec4& n = feeler->m_surfaceDir;
    double along = (Wide(m_vel.z) * n.z + Wide(m_vel.y) * n.y) + Wide(m_vel.x) * n.x;
    // The bounce: -(1 + restitution) of the speed along the normal.
    double k = -((Wide(m_restitution) + 1.0f) * along);
    Maths::Vec4 bounce = {Narrow(k * n.x), Narrow(k * n.y), Narrow(k * n.z), 0.0f};
    double bounceW = k * n.w;
    // Friction against what's across the normal.
    float tx = Narrow(m_vel.x - along * n.x);
    double ty = m_vel.y - along * n.y;
    float tz = Narrow(m_vel.z - Wide(Narrow(along * n.z)));
    double tw = m_vel.w - Wide(Narrow(along * n.w));
    double length = std::sqrt((Wide(tz) * tz + ty * ty) + Wide(tx) * tx);
    float lengthF = Narrow(length);
    if (length > kMinLength) {
        double inverse = 1.0 / lengthF;
        float ux = Narrow(tx * inverse), uy = Narrow(ty * inverse), uz = Narrow(tz * inverse);
        double uw = tw * inverse;
        float most = Narrow(std::sqrt((Wide(bounce.z) * bounce.z + Wide(bounce.y) * bounce.y) +
                                      Wide(bounce.x) * bounce.x) *
                            m_dragFactor);
        double f = -(lengthF > most ? most : lengthF);
        m_vel = {Narrow(ux * f + m_vel.x), AppendOne(Narrow(uy * f), m_vel.y), AppendOne(Narrow(uz * f), m_vel.z),
                 AppendOne(Narrow(uw * f), m_vel.w)};
    }
    m_vel = {AppendOne(bounce.x, m_vel.x), AppendOne(bounce.y, m_vel.y), AppendOne(bounce.z, m_vel.z), Narrow(bounceW + m_vel.w)};
    OnBounce(Narrow(-along), &feeler->m_contactPos);
}

// 0x00430E20
void CBodyDynamicsModule::TickFrame()
{
    if (m_curPhase == STATE_DISABLED)
        return;
    float frame = Narrow(FrameClock::GetSlomoFrameSeconds());
    if (m_pushCooldown > kZero)
        m_pushCooldown = Sub(m_pushCooldown, frame);
    else if (!(m_flagBits & FLAG_NO_PLAYER_COLLISION))
        FeelPushers();
    if (m_dieTimer >= kZero) {
        if (m_dieTimer <= frame)
            FlagAsDead(m_entity);
        else
            m_dieTimer = Sub(m_dieTimer, frame);
    }
    if (m_curPhase == STATE_ASLEEP) {
        m_entity->m_displayMatrix = m_basisFrame;
        return;
    }

    UpdateWorldPoints();
    // The frame in steps: each to the frame's end, or to just before what the path hits.
    float remaining = frame;
    for (int step = 0;;) {
        float length = remaining;
        Engine3D::CProbeRay feeler;
        feeler.MakeNew();
        double vx = Wide(remaining) * m_vel.x, vy = Wide(remaining) * m_vel.y, vz = Wide(remaining) * m_vel.z,
               vw = Wide(remaining) * m_vel.w;
        double half = (Wide(remaining) * remaining) * kHalf;
        feeler.m_line.start = m_atPoint;
        feeler.m_line.end = {Narrow(half * m_steadyAccel.x + (Wide(Narrow(vx)) + m_atPoint.x)),
                             Narrow(Wide(Narrow(half * m_steadyAccel.y)) + (Wide(Narrow(vy)) + m_atPoint.y)),
                             Narrow(Wide(Narrow(half * m_steadyAccel.z)) + (vz + m_atPoint.z)),
                             Narrow(Wide(Narrow(half * m_steadyAccel.w)) + Narrow(vw + m_atPoint.w))};
        bool hit = feeler.GetHitShape(false, false) != 0;
        if (hit) {
            const Maths::Seg& line = feeler.m_line;
            double dx = Wide(line.end.x) - line.start.x, dy = Wide(line.end.y) - line.start.y,
                   dz = Wide(line.end.z) - line.start.z;
            double path = std::sqrt((dx * dx + dy * dy) + dz * dz);
            double speed = std::sqrt((Wide(m_vel.x) * m_vel.x + Wide(m_vel.y) * m_vel.y) + Wide(m_vel.z) * m_vel.z);
            length = Narrow((path * feeler.m_reach - kHitBackOff) / speed);
            if (length < kZero)
                length = 0.0f;
        }
        Integrate(length);
        if (hit)
            Collide(&feeler);
        remaining = Sub(remaining, length);
        if (++step == kMaxSteps) {
            Sleep();
            break;
        }
        if (!hit)
            break;
    }

    if (FindContacts())
        ResolveContacts();
    if (m_springCount)
        ApplySprings();
    Damp();
    CheckSleep();

    Maths::Vec4 offset;
    m_basisFrame.RotateVector(offset, m_modelOffset);
    m_entity->m_atPoint = {AppendOne(m_atPoint.x, offset.x), AppendOne(m_atPoint.y, offset.y), AppendOne(m_atPoint.z, offset.z), AppendOne(m_atPoint.w, offset.w)};
    m_entity->m_basisFrame = m_basisFrame;
    m_entity->m_displayMatrix = m_basisFrame;
    m_entity->m_vel = m_vel;
    if (DrawShape())
        DebugDraw();
}

} // namespace Obj
