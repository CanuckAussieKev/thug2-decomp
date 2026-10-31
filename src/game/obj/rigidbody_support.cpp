// The rigid body component's code before its life and physics (game/obj/rigidbody_component.h): the debug switch,
// the member function table and three of its members, the pushers' reach, Finalize, Sleep, the world points, the
// damping and the check for sleep; and the components' shared vtable slot 11 (RunMemberFunction).
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value rounded where the original rounds it, and sums keep its grouping.

#include <cmath>
#include <cstdint>

#include "game/core/crc.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/sector.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/rigidbody_component.h"
#include "game/obj/smtptr.h"
#include "game/obj/sound_component.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"

namespace Obj {

uint32_t g_rigidBodyMemberFunctions[78]{
            0xCC12CF87, 0xFFB39248, 0, 0, 0, 0,
            0x9599C10F, 0xFFB39248, 0, 0, 0, 0,
            0xCD5BA67B, 0xFFB39248, 0, 0, 0, 0,
            0xAE24DF9F, 0xFFB39248, 0, 0, 0, 0,
            0xE8B73AB3, 0xFFB39248, 0, 0, 0, 0,
            0x8A3A6B5F, 0xFFB39248, 0, 0, 0, 0,
            0x39D708AC, 0xFFB39248, 0, 0, 0, 0,
            0x92F5DB9A, 0xFFB39248, 0, 0, 0, 0,
            0x0A2BBB3A, 0xFFB39248, 0, 0, 0, 0,
            0x49F44585, 0xFFB39248, 0, 0, 0, 0,
            0xD4A0946B, 0xFFB39248, 0, 0, 0, 0,
            0x949BE8A9, 0xFFB39248, 0, 0, 0, 0,
            0, 0xFFB39248, 0, 0, 0, 0};
int g_nextCollideSoundTime;

namespace {

constexpr uint32_t kSound = 0x07713C7B;          // the sound component
constexpr uint32_t kFrames = 0x019176C5;         // frames
constexpr uint32_t kFrame = 0x4A07C332;          // frame
constexpr float kFrameSeconds = 1.0f / 60;       // 0x006463B8
constexpr float kSettledDistanceSqr = 2304.0f;   // 0x006463DC (48 squared)
constexpr float kDieAfterSettling = 1.5f;        // (an immediate)

double Wide(float value) { return value; }
float Narrow(double value) { return static_cast<float>(value); }

// The debug drawing switches (rigidbody_physics.cpp reads them): the shapes, the details.
bool& DrawShape() { return g_rigidBodyDrawShape; }
bool& DrawDetails() { return g_rigidBodyDrawDetails; }

// The 8 objects rigid bodies feel (smart pointers, 0x00695150) and each one's extra reach (0x006939B0).
CSmtPtr<CCompositeObject>* Pushers() { return g_rigidBodyPushers; }
float* PusherReach() { return g_rigidBodyPusherReach; }

// The matrix's element at byte offset `offset` from its start.
double M(const Mth::Matrix& m, int offset) { return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(&m) + offset); }

} // namespace

// 0x0042BAC0
bool __cdecl CFunc_ToggleRigidBodyDebug(Script::CStruct*, Script::CScript*)
{
    if (!DrawShape()) {
        DrawShape() = true;
    } else if (!DrawDetails()) {
        DrawDetails() = true;
    } else {
        DrawDetails() = false;
        DrawShape() = false;
    }
    return true;
}

// 0x0042BB00
void __cdecl CRigidBodyComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_rigidBodyMemberFunctions, 0x18);
}

// 0x0042BB20
int CBaseComponent::RunMemberFunction(Script::CMemberFunctionEntry* entry, Script::CStruct* params,
                                      Script::CScript* script)
{
    if (entry->mCode) {
        void* self = reinterpret_cast<uint8_t*>(this) + entry->mThisAdjustment;
        return reinterpret_cast<int(__thiscall*)(void*, Script::CStruct*, Script::CScript*)>(entry->mCode)(self, params,
                                                                                                         script);
    }
    return CallMemberFunction(entry->mName, params, script);
}

// 0x0042BB60
bool CRigidBodyComponent::RigidBody_IgnoreSkater(Script::CStruct* params, Script::CScript*)
{
    params->GetFloat(0u, &m_pushCooldown, 1);
    if (params->ContainsFlag(kFrames) || params->ContainsFlag(kFrame))
        m_pushCooldown = Narrow(Wide(m_pushCooldown) * kFrameSeconds);
    return true;
}

// 0x0042BBB0
bool CRigidBodyComponent::RigidBody_Disable(Script::CStruct*, Script::CScript*)
{
    m_state = STATE_DISABLED;
    return true;
}

// 0x0042BBD0
bool CRigidBodyComponent::RigidBody_Reset(Script::CStruct* params, Script::CScript*)
{
    CBaseComponent::InitFromStructure(params);
    return true;
}

// 0x0042BD60
void __cdecl SetRigidBodyPusherReach(CCompositeObject* object, float reach)
{
    for (int i = 7; i >= 0; --i) {
        if (Pushers()[i].mp_object == object) {
            PusherReach()[i] = reach;
            return;
        }
    }
}

// 0x0042BD90
void CRigidBodyComponent::Finalize()
{
    mp_soundComponent = static_cast<CSoundComponent*>(m_object->GetComponent(kSound));
    const Mth::Matrix& m = m_matrix;
    for (int i = 0; i < m_springCount; ++i) {
        Spring& spring = mp_springs[i];
        const Mth::Vector& p = spring.point;
        double x = (Wide(p.y) * M(m, 0x10) + Wide(p.x) * M(m, 0x00)) + Wide(p.z) * M(m, 0x20);
        float y = Narrow((Wide(p.x) * M(m, 0x04) + Wide(p.z) * M(m, 0x24)) + Wide(p.y) * M(m, 0x14));
        float z = Narrow((Wide(p.x) * M(m, 0x08) + Wide(p.z) * M(m, 0x28)) + Wide(p.y) * M(m, 0x18));
        spring.anchor.z = Narrow(Wide(z) + m_pos.z);
        spring.anchor.w = Narrow(Wide(p.w) + m_pos.w);
        spring.anchor.x = Narrow(x + m_pos.x);
        spring.anchor.y = Narrow(Wide(y) + m_pos.y);
    }
}

// 0x0042BE90
void CRigidBodyComponent::Sleep()
{
    if (m_settleScript)
        m_object->SpawnScript(m_settleScript, mp_callbackParams, 0, false)->Update();
    m_vel = {0.0f, 0.0f, 0.0f, 1.0f};
    m_rotVel = {0.0f, 0.0f, 0.0f, 1.0f};
    m_state = STATE_ASLEEP;
}

// 0x0042BF00 (each array from its last element back)
void CRigidBodyComponent::UpdateWorldPoints()
{
    const Mth::Matrix& m = m_matrix;
    for (int i = m_contactCount - 1; i >= 0; --i) {
        const Mth::Vector& p = mp_contacts[i];
        Mth::Vector& out = WorldContactPoints()[i];
        double x = (Wide(p.y) * M(m, 0x10) + Wide(p.z) * M(m, 0x20)) + Wide(p.x) * M(m, 0x00);
        double y = (Wide(p.x) * M(m, 0x04) + Wide(p.z) * M(m, 0x24)) + Wide(p.y) * M(m, 0x14);
        double z = (Wide(p.z) * M(m, 0x28) + Wide(p.y) * M(m, 0x18)) + Wide(p.x) * M(m, 0x08);
        out = {Narrow(x), Narrow(y), Narrow(z), p.w};
    }
    for (int i = m_sphereCount - 1; i >= 0; --i) {
        const Mth::Vector& p = mp_spheres[i].center;
        Mth::Vector& out = WorldSphereCenters()[i];
        double x = (Wide(p.y) * M(m, 0x10) + Wide(p.z) * M(m, 0x20)) + Wide(p.x) * M(m, 0x00);
        double y = (Wide(p.x) * M(m, 0x04) + Wide(p.z) * M(m, 0x24)) + Wide(p.y) * M(m, 0x14);
        double z = (Wide(p.x) * M(m, 0x08) + Wide(p.z) * M(m, 0x28)) + Wide(p.y) * M(m, 0x18);
        out = {Narrow(x), Narrow(y), Narrow(z), p.w};
    }
    for (int i = m_springCount - 1; i >= 0; --i) {
        const Spring& spring = mp_springs[i];
        if (!spring.active)
            continue;
        const Mth::Vector& p = spring.point;
        Mth::Vector& out = WorldSpringPoints()[i];
        double x = (Wide(p.z) * M(m, 0x20) + Wide(p.y) * M(m, 0x10)) + Wide(p.x) * M(m, 0x00);
        double y = (Wide(p.z) * M(m, 0x24) + Wide(p.y) * M(m, 0x14)) + Wide(p.x) * M(m, 0x04);
        double z = (Wide(p.z) * M(m, 0x28) + Wide(p.y) * M(m, 0x18)) + Wide(p.x) * M(m, 0x08);
        out = {Narrow(x), Narrow(y), Narrow(z), p.w};
    }
}

namespace {

// A velocity slowed by `drag` (a speed; 0: left alone): stopped (w 1) at or below it, else each component, w too,
// less its share (a not-a-number speed or drag: that).
void Slow(Mth::Vector& v, float drag)
{
    if (drag == 0.0f)
        return;
    double speed = std::sqrt((Wide(v.x) * v.x + Wide(v.y) * v.y) + Wide(v.z) * v.z);
    if (speed <= drag) {
        v = {0.0f, 0.0f, 0.0f, 1.0f};
        return;
    }
    double f = -(drag / speed);
    double dx = f * v.x;
    float dy = Narrow(f * v.y);
    float dz = Narrow(f * v.z);
    float dw = Narrow(f * v.w);
    v.x = Narrow(dx + v.x);
    v.y = Narrow(Wide(dy) + v.y);
    v.z = Narrow(Wide(dz) + v.z);
    v.w = Narrow(Wide(dw) + v.w);
}

// x * x + y * y + z * z, as the original sums it.
double LengthSqr(const Mth::Vector& v) { return (Wide(v.x) * v.x + Wide(v.y) * v.y) + Wide(v.z) * v.z; }

} // namespace

// 0x0042C6D0
void CRigidBodyComponent::Damp()
{
    Slow(m_vel, m_unknown38);
    Slow(m_rotVel, m_unknown3C);
}

// 0x0042C890 (the comparisons let a not-a-number speed through)
void CRigidBodyComponent::CheckSleep()
{
    if (m_unknown191 < 3)
        return;
    if (LengthSqr(m_vel) > m_linearSleepSquared)
        return;
    if (LengthSqr(m_rotVel) > m_angularSleepSquared)
        return;
    Sleep();
    if ((m_flags & FLAG_DIE_UPON_SETTLING) && !(Mth::DistanceSquared(m_pos, m_wakePos) < kSettledDistanceSqr))
        m_dieTimer = kDieAfterSettling;
}

namespace {

constexpr float kSlow = 0.01f;            // 0x0064544C: a relative speed this small (each axis) is none
constexpr float kUpShare = 1.0f / 3;      // 0x006463D8: the vertical speed's share of the push
constexpr float kHardPush = 100.0f;       // 0x006463D4
constexpr float kHardPushAdd = 51.0f;     // 0x006463D0
constexpr float kMidPush = 50.0f;         // 0x006463CC
constexpr float kBelowFeet = -12.0f;      // 0x006463C8: where a push from below lands (y, from the centre)
constexpr float kTurnShare = 0.6f;        // 0x006463C4
constexpr float kSoundScale = 0.1f;       // 0x00645D74: the push's size to the collide sound's percent
constexpr uint32_t kPercent = 0x9E497FC6; // percent

// The pushers' height (0x006939D0: 77, set by InitFromStructure) and the time the next collide sound of any rigid
// body may play (0x006939D8).
float PusherHeight() { return g_rigidBodyPusherHeight; }
int& NextCollideSoundTime() { return g_nextCollideSoundTime; }

// The scene's sector named `checksum` hidden (0x0048C770 the first scene - the name pushed as its argument slot,
// which 0x0049CB00 then takes as its own; neither rewritten yet).
void HideSector(uint32_t checksum)
{
    Nx::CScene* scene = Nx::GetMainScene();
    void* sector = scene->GetSector(checksum);
    if (sector)
        static_cast<Nx::CSector*>(sector)->SetActive(false);
}

// A mute delay: 50-149 % of `delay` milliseconds from now (the product divided unsigned).
int MuteUntil(int delay)
{
    uint32_t scaled = static_cast<uint32_t>((Mth::Rnd(100) + 50) * delay) / 100u;
    return static_cast<int>(scaled + Tmr::GetTime());
}

} // namespace

// 0x0042C140: the first pusher (from the last place back) inside the skater collision radius - plus its reach - and
// within the band from (its height + the application radius + reach) below to (the application radius + reach)
// above, moving at it (the horizontal approach plus a third of the vertical speed, boosted), pushes it: an impulse
// up and away at the assent angle, applied at the application radius from the centre (y: the pusher's height, or
// its feet, or 12 below); then it wakes, ignores pushers for a while, runs CollideScript, hides its shadow sector
// and plays the collide sound (mute delays allowing). A pusher not moving relative to it (0.01 on each axis) ends
// the look at once - the pushers after it aren't felt.
void CRigidBodyComponent::FeelPushers()
{
    for (int i = 7; i >= 0; --i) {
        CCompositeObject* pusher = Pushers()[i].mp_object;
        if (!pusher)
            continue;
        const float reach = PusherReach()[i];
        const Mth::Vector& at = pusher->m_pos;
        double outer = Wide(reach) + m_skaterRadius;
        double dz = Wide(m_pos.z) - at.z;
        double dx = Wide(m_pos.x) - at.x;
        if (outer * outer < dx * dx + dz * dz)
            continue;
        float dy = Narrow(Wide(at.y) - m_pos.y);
        if (Wide(reach) + m_skaterApplicationRadius < dy)
            continue;
        if (-((Wide(PusherHeight()) + m_skaterApplicationRadius) + reach) > dy)
            continue;

        // Away from the pusher (horizontally), and how they move relative to each other.
        float ex = Narrow(Wide(m_pos.x) - at.x);
        double ez = Wide(m_pos.z) - at.z;
        double inverse = 1.0 / std::sqrt(ez * ez + Wide(ex) * ex);
        float nx = Narrow(Wide(ex) * inverse);
        float nz = Narrow(ez * inverse);
        float rx = Narrow(Wide(pusher->m_vel.x) - m_vel.x);
        double ry = Wide(pusher->m_vel.y) - m_vel.y;
        float rz = Narrow(Wide(pusher->m_vel.z) - m_vel.z);
        if (std::fabs(rx) <= kSlow && std::fabs(ry) <= kSlow && std::fabs(rz) <= kSlow)
            return;
        double approach = (std::fabs(ry * kUpShare) + Wide(rx) * nx) + Wide(rz) * nz;
        if (approach <= 0.0)
            continue;
        double push;
        if (approach > kHardPush)
            push = approach + kHardPushAdd;
        else if (approach > kMidPush)
            push = (approach + (approach - kMidPush)) + kMidPush;
        else
            push = approach + approach;

        // The direction: the relative motion's (horizontal, unit) plus the normal, made unit, tilted up.
        double h = std::sqrt(Wide(rx) * rx + Wide(rz) * rz);
        if (h == 0.0)
            h = kSlow;
        double ih = 1.0 / h;
        float ax = Narrow(Wide(rx) * ih + nx);
        double az = Wide(rz) * ih + nz;
        double g = std::sqrt(Wide(ax) * ax + az * az);
        if (g == 0.0)
            g = kSlow;
        double ig = 1.0 / g;
        float jz = Narrow(az * ig);
        float jx = Narrow(Wide(ax) * ig * m_skaterAssentCos);
        jz = Narrow(Wide(jz) * m_skaterAssentCos);
        double size = push * m_skaterImpulseFactor;
        Mth::Vector impulse = {Narrow(Wide(jx) * size), Narrow(Wide(m_skaterAssentSin) * size),
                               Narrow(Wide(jz) * size), 0.0f};

        // Where it's applied (from the centre).
        double px = Wide(at.x) - m_pos.x;
        float pz = Narrow(Wide(at.z) - m_pos.z);
        double across = std::sqrt(px * px + Wide(pz) * pz);
        if (across > 0.0) {
            double scale = m_skaterApplicationRadius / across;
            px = px * scale;
            pz = Narrow(Wide(pz) * scale);
        }
        double py;
        if (dy > 0.0f)
            py = Wide(at.y) - m_pos.y;
        else if (-PusherHeight() > dy)
            py = (Wide(PusherHeight()) + at.y) - m_pos.y;
        else
            py = kBelowFeet;

        m_vel.x = Narrow(Wide(impulse.x) + m_vel.x);
        m_vel.y = Narrow(Wide(impulse.y) + m_vel.y);
        m_vel.z = Narrow(Wide(impulse.z) + m_vel.z);
        m_vel.w = Narrow(size + m_vel.w);
        float tx = Narrow(Wide(impulse.z) * py - Wide(pz) * impulse.y);
        float ty = Narrow(Wide(pz) * impulse.x - Wide(impulse.z) * px);
        double tz = Wide(impulse.y) * px - py * impulse.x;
        double turn = (Wide(m_skaterRotationFactor) * m_massOverMoment) * kTurnShare;
        float wx = Narrow(Wide(tx) * turn);
        float wy = Narrow(Wide(ty) * turn);
        float wz = Narrow(tz * turn);
        m_rotVel.x = Narrow(Wide(wx) + m_rotVel.x);
        m_rotVel.y = Narrow(Wide(wy) + m_rotVel.y);
        m_rotVel.z = Narrow(Wide(wz) + m_rotVel.z);
        m_rotVel.w = Narrow(turn * 0.0f + m_rotVel.w);

        Wake();
        m_pushCooldown = m_pushCooldownTime;
        if (m_collideScript)
            m_object->SpawnScript(m_collideScript, mp_callbackParams, 0, false)->Update();
        HideSector(Crc::ExtendCRCWithString(m_object->m_id, "_Shadow"));
        int now = static_cast<int>(Tmr::GetTime());
        if (now - static_cast<int>(m_unknownF0) > 0 && now - NextCollideSoundTime() > 0 && mp_soundComponent &&
            mp_collideSound) {
            double length = std::sqrt((Wide(impulse.x) * impulse.x + Wide(impulse.z) * impulse.z) +
                                      Wide(impulse.y) * impulse.y);
            mp_collideSound->AddFloat(kPercent, Narrow(length * kSoundScale));
            mp_soundComponent->PlaySound(mp_collideSound);
            m_unknownF0 = static_cast<uint32_t>(MuteUntil(m_collideMuteDelay));
            NextCollideSoundTime() = MuteUntil(m_globalCollideMuteDelay);
        }
        return;
    }
}

} // namespace Obj

using Obj::CRigidBodyComponent;
