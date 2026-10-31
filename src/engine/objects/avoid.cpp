// The Avoid component (GameObjects::CObstacleDodgeModule): see avoid.h. From the disassembly of 0x004E3270-0x004E3F50 (the
// member functions 0x004E37D0, 0x004E3810, 0x004E3BD0 and 0x004E3EC0 aren't Ghidra functions).

#include "engine/objects/avoid.h"
#include "engine/gfx/d3dx.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/smtptr.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "game/rider/skitch.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

uint32_t g_avoidMemberFunctions[54]{
            0x6157CBC0, 0x6EB050ED, 0, 0, 0, 0,
            0xD2AA4D2D, 0x6EB050ED, 0, 0, 0, 0,
            0x41226942, 0x6EB050ED, 0, 0, 0, 0,
            0x3C7F8F3E, 0x6EB050ED, 0, 0, 0, 0,
            0x6A4BCA91, 0x6EB050ED, 0, 0, 0, 0,
            0x3FB422E8, 0x6EB050ED, 0, 0, 0, 0,
            0x539A32DC, 0x6EB050ED, 0, 0, 0, 0,
            0x53BC3B4E, 0x6EB050ED, 0, 0, 0, 0,
            0, 0x6EB050ED, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kAvoid = 0x6EB050ED;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kJumpLeft = 0x733DA756, kJumpRight = 0xA7A0DD72, kJumpBack = 0x64948109,
                   kJumpForward = 0x4DE2C9E8;

constexpr float kQuarterTurn = 0.7853981852531433f; // 0x006453F8 (pi / 4)
constexpr float kFullTurn = 6.2831854820251465f;    // 0x0064545C
constexpr float kToQuarters = 0.6366197466850281f;  // 0x00645488 (2 / pi)
constexpr float kToRadians = 0.01745329238474369f;  // 0x00645464
constexpr float kToDegrees = 57.2957763671875f;     // 0x00645468

// The quarter an angle falls in (0-4; 4 is ahead again), as the original's x87 sum and _ftol2.
int Quarter(float angle)
{
    double value = static_cast<double>(angle) + kQuarterTurn;
    if (value < 0.0)
        value += kFullTurn;
    // _ftol2 converts to 64 bits and the low half is used: not a number or out of range (the integer
    // indefinite, 0x8000000000000000) gives 0 - ahead.
    double quarters = value * kToQuarters;
    if (!(std::fabs(quarters) < 9.2e18))
        return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(quarters))));
}

// The local skater (GameModules::Skate 0x007CE478's 0x00566520; not rewritten yet).
CGameEntity* LocalSkater()
{
    return static_cast<CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
}

// The animation blender's (component 0x7149B753).
CAnimBlenderComponent* Blender(CObjectModule* blender) { return static_cast<CAnimBlenderComponent*>(blender); }
void PlayAnim(CObjectModule* blender, uint32_t anim, int unknown)
{
    Blender(blender)->PlayAnimation(anim, static_cast<uint32_t>(unknown));
}
float ClipLength(CObjectModule* blender, uint32_t anim) { return Blender(blender)->FetchValue(anim); }
// (each channel's 0x0047DC40 with these: AnyChannel - its result unused here)
void FlipAnims(CObjectModule* blender, uint32_t objectId, bool flip, int a, int b)
{
    Blender(blender)->AnyChannel(objectId, flip ? 1u : 0u, static_cast<uint32_t>(a), static_cast<uint32_t>(b));
}
bool AnimComplete(CObjectModule* blender) { return Blender(blender)->IsClipDone(); }
// The motion component's (0x0A015E17) path follower, 0x00423510 (null, or something with a point at +0x74).
uint8_t* MotionPath(CObjectModule* motion)
{
    return static_cast<uint8_t*>(reinterpret_cast<CMotionPathMembers*>(motion)->GetPathFollower());
}
uint32_t& MotionFlags(CObjectModule* motion) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(motion) + 0x1C); }

// `out` = `in` (all four) times `m`, summed as 0x0040AF60 does.
void Xform(const Maths::Mat4& m, Maths::Vec4& out, const Maths::Vec4& in) { m.Xform(out, in); }
// D3DXMatrixMultiply (linked library code).
void D3dxMultiply(Maths::Mat4& out, const Maths::Mat4& a, const Maths::Mat4& b)
{
    D3dx::MatrixMultiply(&out, &a, &b);
}

// The collision feeler (Nx's ray, 0xB0 bytes): constructed by 0x00417760, its line from +0x00 to +0x10, tested
// by 0x004172F0; what it hit is held by a smart pointer at +0x98.
class Feeler {
public:
    Feeler() { reinterpret_cast<Engine3D::CProbeRay*>(m_bytes)->MakeNew(); }
    ~Feeler() { reinterpret_cast<CSharedRef<CSharedBase>*>(m_bytes + 0x98)->~CSharedRef(); }
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    Maths::Vec4& Kickoff() { return *reinterpret_cast<Maths::Vec4*>(m_bytes); }
    Maths::Vec4& End() { return *reinterpret_cast<Maths::Vec4*>(m_bytes + 0x10); }
    bool GetHitShape(int a, int b) { return reinterpret_cast<Engine3D::CProbeRay*>(m_bytes)->GetHitShape(a != 0, b != 0) != 0; }

private:
    alignas(16) uint8_t m_bytes[0xB0];
};

// The angle of `v` about the y row of `m` from its at row, in the original's degrees or radians.
float AngleInPlane(const Maths::Mat4& m, const Maths::Vec4& v) { return Maths::GetAngleInPlane(m, v, 2, 1); }

} // namespace

// 0x004E3270
float AvoidDistance(float angle, Scripts::CStruct* params)
{
    float distance = 0.0f;
    switch (Quarter(angle)) {
    case 1: params->FetchFloat("distLeft", &distance, 1); break;
    case 2: params->FetchFloat("distBack", &distance, 1); break;
    case 3: params->FetchFloat("distRight", &distance, 1); break;
    default: params->FetchFloat("distForward", &distance, 1); break;
    }
    return distance;
}

// 0x004E3930
bool LineCollides(const Maths::Vec4& start, const Maths::Vec4& end)
{
    Feeler feeler;
    feeler.Kickoff() = start;
    feeler.End() = end;
    return feeler.GetHitShape(1, 0);
}

// 0x004E39E0
CObjectModule* __cdecl CObstacleDodgeModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CObstacleDodgeModule));
    return memory ? static_cast<CObstacleDodgeModule*>(memory)->MakeNew() : nullptr;
}

// 0x004E3310
void __cdecl CObstacleDodgeModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kAvoid), g_avoidMemberFunctions, 0x18);
}

// 0x004E3660
CObstacleDodgeModule* CObstacleDodgeModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kAvoid;
    return this;
}

// 0x004E3330
void CObstacleDodgeModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x004E36C0
CObstacleDodgeModule* CObstacleDodgeModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004E34C0
float CObstacleDodgeModule::SideAwayFromSkater()
{
    CGameEntity* skater = LocalSkater();
    // The skater's heading (its at row) flat and normalised; across it (x up), as the original's cross product
    // with (0, 1, 0) - the zero terms multiplied out.
    const Maths::Vec4& at = skater->m_basisFrame.row[2];
    double x = at.x, z = at.z, y = 0.0;
    float length = static_cast<float>(std::sqrt(z * z + x * x));
    if (length > 0.0f) {
        double exactInverse = 1.0 / length; // (x by the unrounded inverse, z and y by the rounded one)
        float inverse = static_cast<float>(exactInverse);
        x *= exactInverse;
        y = static_cast<double>(inverse) * 0.0f;
        z *= inverse;
    }
    double zeroY = y * 0.0f;
    float sideX = static_cast<float>(zeroY - z);
    float sideY = static_cast<float>(z * 0.0f - x * 0.0f);
    float sideZ = static_cast<float>(x - zeroY);
    Maths::Vec4 side = {sideX, sideY, sideZ, 0.0f};
    // Which of the object's two sides is further from the skater (the first one's x and y unrounded).
    const Maths::Vec4& pos = m_entity->m_atPoint;
    double aX = static_cast<double>(sideX) + pos.x, aY = static_cast<double>(sideY) + pos.y;
    float aZ = sideZ + pos.z;
    float bX = pos.x - sideX, bY = pos.y - sideY, bZ = pos.z - sideZ;
    const Maths::Vec4& s = skater->m_atPoint;
    float d1x = static_cast<float>(s.x - aX);
    double d1y = s.y - aY;
    double d1z = static_cast<double>(s.z) - aZ;
    double d2x = static_cast<double>(s.x) - bX, d2y = static_cast<double>(s.y) - bY, d2z = static_cast<double>(s.z) - bZ;
    double first = (d1z * d1z + d1y * d1y) + static_cast<double>(d1x) * d1x;
    double second = (d2z * d2z + d2y * d2y) + d2x * d2x;
    if (first < second) {
        side.x = -sideX;
        side.z = -sideZ;
    }
    return static_cast<float>(static_cast<double>(AngleInPlane(m_entity->m_displayMatrix, side)) * kToDegrees);
}

// 0x004E3A40
bool CObstacleDodgeModule::TryJump(const Maths::Mat4& start, Maths::Mat4& out, float angle, float distance,
                              float maxDistance)
{
    out = start;
    Maths::Mat4 turn;
    D3dxMultiply(out, Maths::MakeYawMatrix(turn, static_cast<float>(static_cast<double>(angle) * kToRadians)), out);
    Maths::Vec4 probe = {0.0f, 10.0f, distance + 15.0f, 1.0f};
    Maths::Vec4 probeEnd;
    Xform(out, probeEnd, probe);
    if (LineCollides(start.row[3], probeEnd))
        return false;
    Maths::Vec4 landing = {0.0f, 0.0f, distance, 1.0f};
    Maths::Vec4 end;
    Xform(out, end, landing);
    out.row[3] = end;
    const Maths::Vec4& s = LocalSkater()->m_atPoint;
    double dx = static_cast<double>(end.x) - s.x, dy = static_cast<double>(end.y) - s.y,
           dz = static_cast<double>(end.z) - s.z;
    double fromSkater = std::sqrt((dz * dz + dy * dy) + dx * dx);
    if (fromSkater < 30.0) {
        DebugOut::Printf("Too close to skater %f!\n", fromSkater);
        return false;
    }
    double hx = static_cast<double>(end.x) - m_herdPoint.x, hz = static_cast<double>(end.z) - m_herdPoint.z;
    return !(std::sqrt(hz * hz + hx * hx) >= maxDistance); // (not a number: within, as the original's FCOMP)
}

// 0x004E3340
void CObstacleDodgeModule::PlayJump(float angle, Scripts::CStruct* params)
{
    // The checksum is read into the parameters' own argument slot: without one it's the pointer's value.
    uint32_t anim = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
    switch (Quarter(angle)) {
    case 0:
    case 4:
        params->ReadHashValue(kJumpForward, &anim, 1);
        break;
    case 1:
    case 3: {
        // The side's own animation, or the other side's flipped.
        uint32_t own = Quarter(angle) == 1 ? kJumpLeft : kJumpRight;
        uint32_t other = Quarter(angle) == 1 ? kJumpRight : kJumpLeft;
        bool flip = !params->ReadHashValue(own, &anim, 0);
        if (flip)
            params->ReadHashValue(other, &anim, 1);
        if (CObjectModule* blender = m_entity->FetchModule(kAnimBlender))
            FlipAnims(blender, m_entity->m_ident, flip, 0, 1);
        break;
    }
    case 2:
        params->ReadHashValue(kJumpBack, &anim, 1);
        break;
    default:
        return;
    }
    if (CObjectModule* blender = m_entity->FetchModule(kAnimBlender)) {
        PlayAnim(blender, anim, 0);
        m_lastsFor = ClipLength(blender, anim);
    }
}

// 0x004E3BD0
bool CObstacleDodgeModule::Jump(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    // From where it stands, as its display matrix shows it.
    Maths::Mat4 start = m_entity->m_displayMatrix;
    start.row[3] = m_entity->m_atPoint;
    float maxDistance = 100.0f;
    params->FetchFloat("maxHerdDistance", &maxDistance, 0);
    Maths::Mat4 end;
    float side = SideAwayFromSkater();
    bool found = TryJump(start, end, side, AvoidDistance(side, params), maxDistance);
    if (!found) {
        float other = static_cast<float>(static_cast<double>(side) + 180.0f);
        found = TryJump(start, end, other, AvoidDistance(other, params), maxDistance);
    }
    if (!found) {
        float back = static_cast<float>(static_cast<double>(side) - 90.0f);
        found = TryJump(start, end, back, AvoidDistance(back, params), maxDistance);
    }
    if (!found) {
        // Away from the herd point (this angle in radians, as the original passes it).
        const Maths::Vec4& pos = start.row[3];
        Maths::Vec4 away = {m_herdPoint.x - pos.x, m_herdPoint.y - pos.y, m_herdPoint.z - pos.z,
                            m_herdPoint.w - pos.w};
        float angle = AngleInPlane(m_entity->m_basisFrame, away);
        found = TryJump(start, end, angle, AvoidDistance(angle, params), maxDistance);
    }
    if (!found)
        return false;
    // The landing keeps the start's rows; the slerp from the start to it.
    Maths::Vec4 landing = end.row[3];
    end = start;
    end.row[3] = landing;
    m_slerp.SetMatrices(start, end);
    m_progress = 0.0f;
    const Maths::Vec4& pos = start.row[3];
    Maths::Vec4 move = {landing.x - pos.x, landing.y - pos.y, landing.z - pos.z, landing.w - pos.w};
    PlayJump(AngleInPlane(m_entity->m_basisFrame, move), params);
    return true;
}

// 0x004E36F0
bool CObstacleDodgeModule::DebugBox(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    float distance; // (read into the parameters' own argument slot: without one it keeps the pointer's bits)
    std::memcpy(&distance, &params, sizeof(distance));
    params->FetchFloat("dist", &distance, 1);
    return false;
}

// 0x004E3EC0
bool CObstacleDodgeModule::StepJump(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    m_progress = static_cast<float>(FrameClock::GetSlomoFrameSeconds() * 60.0f * m_lastsFor * 0.01666666753590107f +
                                    m_progress);
    Maths::Mat4 now;
    m_slerp.FetchMat(now, m_progress);
    m_entity->m_atPoint = now.row[3];
    m_entity->m_basisFrame = now;
    m_entity->m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return true;
}

// 0x004E37D0
bool CObstacleDodgeModule::JumpFinished(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    if (!(m_progress >= 1.0f))
        return false;
    return AnimComplete(m_entity->FetchModule(kAnimBlender));
}

// 0x004E3810
bool CObstacleDodgeModule::StorePosition(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    m_herdPoint = m_entity->m_atPoint;
    return true;
}

// 0x004E3850
bool CObstacleDodgeModule::StoreTarget(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    const Maths::Vec4* point = &m_entity->m_atPoint;
    CObjectModule* motion = m_entity->FetchModule(kMotion);
    if (motion && MotionPath(motion))
        point = reinterpret_cast<const Maths::Vec4*>(MotionPath(motion) + 0x74);
    m_aimPoint = *point;
    return true;
}

// 0x004E38C0
bool CObstacleDodgeModule::HoldMotion(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    uint32_t& flags = MotionFlags(m_entity->FetchModule(kMotion));
    flags |= 4;
    m_motionHeld = flags != 0;
    return true;
}

// 0x004E38F0
bool CObstacleDodgeModule::RestoreMotion(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    if (CObjectModule* motion = m_entity->FetchModule(kMotion)) {
        if (m_motionHeld)
            MotionFlags(motion) |= 4;
        else
            MotionFlags(motion) &= ~4u;
    }
    return true;
}

// The two helpers' register arguments.
namespace {

// 0x004E3270: the angle on the stack, the parameters in ESI; the result in ST0.
__declspec(naked) void AvoidDistanceStub()
{
    __asm {
        push esi
        push dword ptr [esp + 8]
        call AvoidDistance
        add esp, 8
        ret
    }
}

// 0x004E3930: the start in EDI, the end in ESI; the result in AL.
__declspec(naked) void LineCollidesStub()
{
    __asm {
        push esi
        push edi
        call LineCollides
        add esp, 8
        ret
    }
}

} // namespace

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CObstacleDodgeModule::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
