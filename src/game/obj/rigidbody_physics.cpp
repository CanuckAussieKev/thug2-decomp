// The rigid body component's physics: the frame's update, integration, collisions with the world, contacts and
// springs (game/obj/rigidbody_component.h).
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value is rounded where the original rounds it, and sums keep its grouping.

#include <cmath>
#include <cstdint>

#include "game/gfx/debug_lines.h"
#include "game/mth/math.h"
#include "game/mth/quat.h"
#include "game/nx/feeler.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/sound_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

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
float Add(float a, float b) { return Narrow(Wide(a) + b); }
float Sub(float a, float b) { return Narrow(Wide(a) - b); }

// Debug drawing switches (0x006939D4: the shape each frame; 0x006939D5: the centre, the contacts' pushes).
bool DrawShape() { return GameVariable<bool>(0x006939D4); }
bool DrawDetails() { return GameVariable<bool>(0x006939D5); }

// Quaternions: `quat` times `other`, in place (0x00420140); the matrix of one.
void QuatMultiply(Mth::Quat* quat, const Mth::Quat* other) { *quat *= *other; }
void QuatToMatrix(const Mth::Quat* quat, Mth::Matrix* matrix) { quat->GetMatrix(matrix); }

void MarkAsDead(CCompositeObject* object)
{
    reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[6])(object);
}

// The matrix's element at row `r`, column `c`.
double At(const Mth::Matrix& m, int r, int c) { return reinterpret_cast<const float*>(&m.row[r])[c]; }

} // namespace

// 0x0042CEE0: 1 / (n . (n + (m (r x n)) x r)) - m the mass over the moment.
double CRigidBodyComponent::ImpulseScale(const Mth::Vector* normal, const Mth::Vector* offset) const
{
    const Mth::Vector& n = *normal;
    const Mth::Vector& r = *offset;
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
void CRigidBodyComponent::ApplySprings()
{
    const Mth::Vector* world = WorldSpringPoints();
    // The springs' points held still: each moving point's velocity taken out by an impulse, round after round.
    int round = 0;
    bool settled;
    do {
        settled = true;
        for (int i = m_springCount; i != 0;) {
            --i;
            if (!mp_springs[i].active)
                continue;
            const Mth::Vector& r = world[i];
            // its velocity: vel + rotvel x r
            double cx = Wide(r.z) * m_rotVel.y - Wide(r.y) * m_rotVel.z;
            double cy = Wide(m_rotVel.z) * r.x - Wide(r.z) * m_rotVel.x;
            float cz = Narrow(Wide(r.y) * m_rotVel.x - Wide(m_rotVel.y) * r.x);
            Mth::Vector v = {Narrow(cx + m_vel.x), Narrow(cy + m_vel.y), Narrow(Wide(cz) + m_vel.z), m_vel.w};
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
    float dt = Narrow(Tmr::GetSlomoFrameSeconds());
    for (int i = m_springCount; i != 0;) {
        --i;
        const Spring& spring = mp_springs[i];
        if (!spring.active)
            continue;
        const Mth::Vector& r = world[i];
        float px = Narrow(Wide(m_pos.x) + r.x);
        double py = Wide(m_pos.y) + r.y, pz = Wide(m_pos.z) + r.z;
        float pw = Narrow(Wide(m_pos.w) + r.w);
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
        Mth::Vector turn = {Narrow(tx * m), Narrow(ty * m), Narrow(tz * m), Narrow(m * kZero)};
        m_vel = {Narrow(fx + m_vel.x), Narrow(fy + m_vel.y), Narrow(Wide(fz) + m_vel.z), Narrow(Wide(fw) + m_vel.w)};
        m_rotVel = {Add(turn.x, m_rotVel.x), Add(turn.y, m_rotVel.y), Add(turn.z, m_rotVel.z), Add(turn.w, m_rotVel.w)};
    }
}

// 0x0042D9D0
void CRigidBodyComponent::DebugDraw()
{
    uint32_t colour = 0;
    switch (m_state) {
    case STATE_ASLEEP: colour = 0xFFC80000; break;
    case STATE_AWAKE: colour = m_pushCooldown > kZero ? 0xFF00C800 : 0xFF0000C8; break;
    case STATE_DISABLED: colour = 0xFF640064; break;
    }
    auto at = [this](const Mth::Vector& p) {
        return Mth::Vector{p.x + m_pos.x, p.y + m_pos.y, p.z + m_pos.z, p.w + m_pos.w};
    };
    // Every pair of contact points.
    const Mth::Vector* points = WorldContactPoints();
    for (int i = m_contactCount - 1; i > 0; --i)
        for (int j = 0; j < i; ++j) {
            Mth::Vector from = at(points[i]), to = at(points[j]);
            Gfx::AddDebugLine(&from, &to, colour, 0, 1);
        }
    const Mth::Vector* centers = WorldSphereCenters();
    for (int i = m_sphereCount; i != 0;) {
        --i;
        Mth::Vector center = at(centers[i]);
        Gfx::AddDebugSphere(&center, mp_spheres[i].radius, &m_matrix, 12, colour, 1);
    }
    if (DrawDetails())
        Gfx::AddDebugStar(m_pos, 36.0f, 0xFFC800C8, 1);
    const Mth::Vector* springs = WorldSpringPoints();
    for (int i = m_springCount; i != 0;) {
        --i;
        if (!mp_springs[i].active)
            continue;
        Gfx::AddDebugStar(at(springs[i]), 24.0f, 0xFFC86400, 1);
        Gfx::AddDebugStar(mp_springs[i].anchor, 24.0f, 0xFFC80064, 1);
    }
}

// 0x0042E540
void CRigidBodyComponent::Integrate(float dt)
{
    if (m_unknown191 < 3) {
        // Moved by its velocity and half a step of acceleration, then accelerated.
        double half = Wide(dt) * kHalf;
        double ax = half * m_constAcc.x;
        float ay = Narrow(half * m_constAcc.y), az = Narrow(half * m_constAcc.z), aw = Narrow(half * m_constAcc.w);
        float mx = Narrow(ax + m_vel.x);
        double my = Wide(ay) + m_vel.y, mz = Wide(az) + m_vel.z;
        float mw = Narrow(Wide(aw) + m_vel.w);
        float px = Narrow(Wide(mx) * dt), py = Narrow(my * dt);
        double pz = mz * dt, pw = Wide(mw) * dt;
        m_pos = {Narrow(Wide(px) + m_pos.x), Narrow(Wide(py) + m_pos.y), Narrow(pz + m_pos.z), Narrow(pw + m_pos.w)};
        double vx = Wide(dt) * m_constAcc.x, vy = Wide(dt) * m_constAcc.y;
        float vz = Narrow(Wide(dt) * m_constAcc.z), vw = Narrow(Wide(dt) * m_constAcc.w);
        m_vel = {Narrow(vx + m_vel.x), Narrow(vy + m_vel.y), Narrow(Wide(vz) + m_vel.z), Narrow(Wide(vw) + m_vel.w)};
    } else {
        double px = Wide(dt) * m_vel.x, py = Wide(dt) * m_vel.y;
        float pz = Narrow(Wide(dt) * m_vel.z), pw = Narrow(Wide(dt) * m_vel.w);
        m_pos = {Narrow(px + m_pos.x), Narrow(py + m_pos.y), Narrow(Wide(pz) + m_pos.z), Narrow(Wide(pw) + m_pos.w)};
    }

    // Turned: q += (q * (-rotvel / 2, 0)) dt, normalised.
    Mth::Quat spin = {Narrow(Wide(m_rotVel.x) * kTurnRate), Narrow(Wide(m_rotVel.y) * kTurnRate),
                      Narrow(Wide(m_rotVel.z) * kTurnRate), 0.0f};
    Mth::Quat change = m_orientation;
    QuatMultiply(&change, &spin);
    float cz = Narrow(Wide(change.z) * dt), cw = Narrow(Wide(change.w) * dt);
    m_orientation = {Narrow(Wide(change.x) * dt + m_orientation.x), Narrow(Wide(change.y) * dt + m_orientation.y),
                     Add(cz, m_orientation.z), Add(cw, m_orientation.w)};
    m_orientation.Normalize(1.0f);
    QuatToMatrix(&m_orientation, &m_matrix);

    // The shape turned to the world (each set of points adds its terms in its own order).
    const Mth::Matrix& t = m_matrix;
    for (int i = m_contactCount; i != 0;) {
        --i;
        const Mth::Vector& c = mp_contacts[i];
        const double x = c.x, y = c.y, z = c.z;
        WorldContactPoints()[i] = {Narrow((y * At(t, 1, 0) + At(t, 2, 0) * z) + At(t, 0, 0) * x),
                                   Narrow((At(t, 0, 1) * x + At(t, 2, 1) * z) + y * At(t, 1, 1)),
                                   Narrow((At(t, 2, 2) * z + y * At(t, 1, 2)) + At(t, 0, 2) * x), c.w};
    }
    for (int i = m_sphereCount; i != 0;) {
        --i;
        const Mth::Vector& c = mp_spheres[i].center;
        const double x = c.x, y = c.y, z = c.z;
        WorldSphereCenters()[i] = {Narrow((y * At(t, 1, 0) + At(t, 2, 0) * z) + x * At(t, 0, 0)),
                                   Narrow((x * At(t, 0, 1) + At(t, 2, 1) * z) + y * At(t, 1, 1)),
                                   Narrow((At(t, 2, 2) * z + x * At(t, 0, 2)) + y * At(t, 1, 2)), c.w};
    }
    for (int i = m_springCount; i != 0;) {
        --i;
        if (!mp_springs[i].active)
            continue;
        const Mth::Vector& c = mp_springs[i].point;
        const double x = c.x, y = c.y, z = c.z;
        WorldSpringPoints()[i] = {Narrow((At(t, 2, 0) * z + At(t, 1, 0) * y) + x * At(t, 0, 0)),
                                  Narrow((At(t, 2, 1) * z + At(t, 1, 1) * y) + At(t, 0, 1) * x),
                                  Narrow((At(t, 2, 2) * z + At(t, 1, 2) * y) + At(t, 0, 2) * x), c.w};
    }
}

// 0x0042E980
void CRigidBodyComponent::OnBounce(float speed, const Mth::Vector* point)
{
    if (!(speed > m_bounceCallbackThreshold))
        return;
    if (m_bounceScript) {
        Script::CStruct* params = Script::NewStruct();
        if (params)
            params = params->Init();
        if (mp_callbackParams)
            params->AppendStructure(mp_callbackParams); // (out of memory: through null, as the original)
        params->AddVector(0x43F454D0, Script::VectorValue{point->x, point->y, point->z, point->w});
        m_object->SpawnScript(m_bounceScript, params, 0, false)->Update();
        if (params) {
            params->Clear();
            Script::DeleteStruct(params);
        }
    }
    if (mp_soundComponent && mp_bounceSound && 48 - Sfx::CountPlayingVoices() > 12) {
        float percent = Mth::Clamp(Narrow(Wide(speed) / m_bounceFullSpeed * 100.0f), 0.0f, 100.0f);
        mp_bounceSound->AddFloat(0x9E497FC6, percent); // Percent
        mp_soundComponent->PlaySound(mp_bounceSound);
    }
}

// 0x00430140
void CRigidBodyComponent::ResolveContacts()
{
    Contact* contacts = Contacts();
    // The velocities before the impulses (friction works from these).
    Mth::Vector vel0 = m_vel, rot0 = m_rotVel;
    float fastest = 0.0f; // the fastest approach

    // Impulses along the normals until nothing approaches.
    for (int round = 0;;) {
        bool done = true;
        for (int i = m_contactsHit; i != 0;) {
            Contact& c = contacts[--i];
            if (!c.active)
                continue;
            const Mth::Vector& r = c.offset;
            const Mth::Vector& n = c.normal;
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
            m_vel = {Add(jx, m_vel.x), Narrow(jy + m_vel.y), Narrow(jz + m_vel.z), Narrow(jw + m_vel.w)};
            float tx = Narrow(jz * r.y - jy * r.z), ty = Narrow(Wide(jx) * r.z - jz * r.x);
            double tz = jy * r.x - Wide(jx) * r.y;
            const double m = m_massOverMoment;
            m_rotVel = {Add(Narrow(tx * m), m_rotVel.x), Add(Narrow(ty * m), m_rotVel.y), Add(Narrow(tz * m), m_rotVel.z),
                        Narrow(m * kZero + m_rotVel.w)};
        }
        if (++round == kMaxRounds) {
            // Stuck: the script, or no velocity at all.
            if (m_scriptD0) {
                m_object->SpawnScript(m_scriptD0, mp_callbackParams, 0, false)->Update();
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
        const Mth::Vector& r = c.offset;
        const Mth::Vector& n = c.normal;
        double cx = Wide(rot0.y) * r.z - Wide(rot0.z) * r.y;
        double cy = Wide(rot0.z) * r.x - Wide(rot0.x) * r.z;
        float cz = Narrow(Wide(rot0.x) * r.y - Wide(rot0.y) * r.x);
        float vx = Narrow(cx + vel0.x);
        double vy = cy + vel0.y, vz = Wide(cz) + vel0.z;
        double back = -((vz * n.z + Wide(vx) * n.x) + vy * n.y);
        Mth::Vector across = {Narrow(back * n.x + vx), Narrow(Wide(Narrow(back * n.y)) + vy),
                              Narrow(Wide(Narrow(back * n.z)) + vz), Narrow(Wide(Narrow(back * n.w)) + vel0.w)};
        if (mp_directedFriction) {
            const DirectedFriction& friction = mp_directedFriction[c.friction];
            if (friction.active) {
                Mth::Vector d;
                m_matrix.RotateVector(d, friction.direction);
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
        float most = Narrow(Wide(c.impulse) * m_friction);
        double f = -(stop > most ? most : stop);
        double jx = across.x * f;
        float jy = Narrow(across.y * f), jz = Narrow(across.z * f), jw = Narrow(across.w * f);
        double tx = Wide(jz) * r.y - Wide(jy) * r.z, ty = jx * r.z - Wide(jz) * r.x;
        float tz = Narrow(Wide(jy) * r.x - jx * r.y);
        const double m = m_massOverMoment;
        Mth::Vector turn = {Narrow(tx * m), Narrow(ty * m), Narrow(tz * m), Narrow(m * kZero)};
        m_vel = {Narrow(jx + m_vel.x), Add(jy, m_vel.y), Add(jz, m_vel.z), Add(jw, m_vel.w)};
        m_rotVel = {Add(turn.x, m_rotVel.x), Add(turn.y, m_rotVel.y), Add(turn.z, m_rotVel.z), Add(turn.w, m_rotVel.w)};
        vel0 = {Narrow(jx + vel0.x), Add(jy, vel0.y), Add(jz, vel0.z), Add(jw, vel0.w)};
        rot0 = {Add(turn.x, rot0.x), Add(turn.y, rot0.y), Add(turn.z, rot0.z), Add(turn.w, rot0.w)};
    }

    // Pushed out of the world by each contact's depth.
    float dt = Narrow(Tmr::GetSlomoFrameSeconds());
    for (int i = m_contactsHit; i != 0;) {
        Contact& c = contacts[--i];
        if (!c.active)
            continue;
        const Mth::Vector& r = c.offset;
        const Mth::Vector& n = c.normal;
        double k = -((Wide(c.depth) * m_springConst) * dt);
        Mth::Vector push = {Narrow(k * n.x), Narrow(k * n.y), Narrow(k * n.z), Narrow(k * n.w)};
        double tx = Wide(push.z) * r.y - Wide(push.y) * r.z, ty = Wide(push.x) * r.z - Wide(push.z) * r.x;
        float tz = Narrow(Wide(push.y) * r.x - Wide(push.x) * r.y);
        const double m = m_massOverMoment;
        m_vel = {Add(push.x, m_vel.x), Add(push.y, m_vel.y), Add(push.z, m_vel.z), Add(push.w, m_vel.w)};
        m_rotVel = {Add(Narrow(tx * m), m_rotVel.x), Narrow(ty * m + m_rotVel.y), Narrow(Wide(tz) * m + m_rotVel.z),
                    Add(Narrow(m * kZero), m_rotVel.w)};
        if (DrawDetails()) {
            Mth::Vector from = {r.x + m_pos.x, m_pos.y + r.y, m_pos.z + r.z, m_pos.w + r.w};
            Mth::Vector to = {from.x + push.x * kDebugImpulseScale, from.y + push.y * kDebugImpulseScale,
                              from.z + push.z * kDebugImpulseScale, from.w + push.w * kDebugImpulseScale};
            Gfx::AddDebugLine(&to, &from, 0xFF00FFFF, 0, 1);
        }
    }

    // The bounce, at the last active contact.
    for (int i = m_contactsHit; i != 0;) {
        const Contact& c = contacts[--i];
        if (c.active) {
            Mth::Vector point = {Add(c.offset.x, m_pos.x), Add(c.offset.y, m_pos.y), Add(c.offset.z, m_pos.z),
                                 Add(c.offset.w, m_pos.w)};
            OnBounce(fastest, &point);
            return;
        }
    }
}

// 0x00430BE0
void CRigidBodyComponent::Collide(Nx::CFeeler* feeler)
{
    const Mth::Vector& n = feeler->m_normal;
    double along = (Wide(m_vel.z) * n.z + Wide(m_vel.y) * n.y) + Wide(m_vel.x) * n.x;
    // The bounce: -(1 + restitution) of the speed along the normal.
    double k = -((Wide(m_restitution) + 1.0f) * along);
    Mth::Vector bounce = {Narrow(k * n.x), Narrow(k * n.y), Narrow(k * n.z), 0.0f};
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
                            m_friction);
        double f = -(lengthF > most ? most : lengthF);
        m_vel = {Narrow(ux * f + m_vel.x), Add(Narrow(uy * f), m_vel.y), Add(Narrow(uz * f), m_vel.z),
                 Add(Narrow(uw * f), m_vel.w)};
    }
    m_vel = {Add(bounce.x, m_vel.x), Add(bounce.y, m_vel.y), Add(bounce.z, m_vel.z), Narrow(bounceW + m_vel.w)};
    OnBounce(Narrow(-along), &feeler->m_point);
}

// 0x00430E20
void CRigidBodyComponent::Update()
{
    if (m_state == STATE_DISABLED)
        return;
    float frame = Narrow(Tmr::GetSlomoFrameSeconds());
    if (m_pushCooldown > kZero)
        m_pushCooldown = Sub(m_pushCooldown, frame);
    else if (!(m_flags & FLAG_NO_PLAYER_COLLISION))
        FeelPushers();
    if (m_dieTimer >= kZero) {
        if (m_dieTimer <= frame)
            MarkAsDead(m_object);
        else
            m_dieTimer = Sub(m_dieTimer, frame);
    }
    if (m_state == STATE_ASLEEP) {
        m_object->m_displayMatrix = m_matrix;
        return;
    }

    UpdateWorldPoints();
    // The frame in steps: each to the frame's end, or to just before what the path hits.
    float remaining = frame;
    for (int step = 0;;) {
        float length = remaining;
        Nx::CFeeler feeler;
        feeler.Construct();
        double vx = Wide(remaining) * m_vel.x, vy = Wide(remaining) * m_vel.y, vz = Wide(remaining) * m_vel.z,
               vw = Wide(remaining) * m_vel.w;
        double half = (Wide(remaining) * remaining) * kHalf;
        feeler.m_line.start = m_pos;
        feeler.m_line.end = {Narrow(half * m_constAcc.x + (Wide(Narrow(vx)) + m_pos.x)),
                             Narrow(Wide(Narrow(half * m_constAcc.y)) + (Wide(Narrow(vy)) + m_pos.y)),
                             Narrow(Wide(Narrow(half * m_constAcc.z)) + (vz + m_pos.z)),
                             Narrow(Wide(Narrow(half * m_constAcc.w)) + Narrow(vw + m_pos.w))};
        bool hit = feeler.GetCollision(false, false) != 0;
        if (hit) {
            const Mth::Line& line = feeler.m_line;
            double dx = Wide(line.end.x) - line.start.x, dy = Wide(line.end.y) - line.start.y,
                   dz = Wide(line.end.z) - line.start.z;
            double path = std::sqrt((dx * dx + dy * dy) + dz * dz);
            double speed = std::sqrt((Wide(m_vel.x) * m_vel.x + Wide(m_vel.y) * m_vel.y) + Wide(m_vel.z) * m_vel.z);
            length = Narrow((path * feeler.m_distance - kHitBackOff) / speed);
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

    Mth::Vector offset;
    m_matrix.RotateVector(offset, m_modelOffset);
    m_object->m_pos = {Add(m_pos.x, offset.x), Add(m_pos.y, offset.y), Add(m_pos.z, offset.z), Add(m_pos.w, offset.w)};
    m_object->m_matrix = m_matrix;
    m_object->m_displayMatrix = m_matrix;
    m_object->m_vel = m_vel;
    if (DrawShape())
        DebugDraw();
}

} // namespace Obj

REPLACE(0x0042CEE0, &Obj::CRigidBodyComponent::ImpulseScale);
REPLACE(0x0042CF90, &Obj::CRigidBodyComponent::ApplySprings);
REPLACE(0x0042D9D0, &Obj::CRigidBodyComponent::DebugDraw);
REPLACE(0x0042E540, &Obj::CRigidBodyComponent::Integrate);
REPLACE(0x0042E980, &Obj::CRigidBodyComponent::OnBounce);
REPLACE(0x00430140, &Obj::CRigidBodyComponent::ResolveContacts);
REPLACE(0x00430BE0, &Obj::CRigidBodyComponent::Collide);
REPLACE(0x00430E20, &Obj::CRigidBodyComponent::Update);
