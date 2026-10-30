// The Avoid component (Obj::CAvoidComponent): see avoid.h. From the disassembly of 0x004E3270-0x004E3F50 (the
// member functions 0x004E37D0, 0x004E3810, 0x004E3BD0 and 0x004E3EC0 aren't Ghidra functions).

#include "game/obj/avoid.h"
#include "game/skater/skater.h"
#include "game/skate/skate_module.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/motion_path.h"
#include "game/obj/smtptr.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

GAME_GLOBAL(uint32_t[54], g_avoidMemberFunctions, 0x006839E8,
            0x6157CBC0, 0x6EB050ED, 0, 0, 0, 0,
            0xD2AA4D2D, 0x6EB050ED, 0, 0, 0, 0,
            0x41226942, 0x6EB050ED, 0, 0, 0, 0,
            0x3C7F8F3E, 0x6EB050ED, 0, 0, 0, 0,
            0x6A4BCA91, 0x6EB050ED, 0, 0, 0, 0,
            0x3FB422E8, 0x6EB050ED, 0, 0, 0, 0,
            0x539A32DC, 0x6EB050ED, 0, 0, 0, 0,
            0x53BC3B4E, 0x6EB050ED, 0, 0, 0, 0,
            0, 0x6EB050ED, 0, 0, 0, 0);

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

// The local skater (Mdl::Skate 0x007CE478's 0x00566520; not rewritten yet).
CCompositeObject* LocalSkater()
{
    return static_cast<CCompositeObject*>(Mdl::Skate::Instance()->GetLocalSkater());
}

// The animation blender's (component 0x7149B753).
CAnimBlenderComponent* Blender(CBaseComponent* blender) { return static_cast<CAnimBlenderComponent*>(blender); }
void PlayAnim(CBaseComponent* blender, uint32_t anim, int unknown)
{
    Blender(blender)->PlayAnimation(anim, static_cast<uint32_t>(unknown));
}
float AnimDuration(CBaseComponent* blender, uint32_t anim) { return Blender(blender)->GetValue(anim); }
// (each channel's 0x0047DC40 with these: AnyChannel - its result unused here)
void FlipAnims(CBaseComponent* blender, uint32_t objectId, bool flip, int a, int b)
{
    Blender(blender)->AnyChannel(objectId, flip ? 1u : 0u, static_cast<uint32_t>(a), static_cast<uint32_t>(b));
}
bool AnimComplete(CBaseComponent* blender) { return Blender(blender)->IsAnimComplete(); }
// The motion component's (0x0A015E17) path follower, 0x00423510 (null, or something with a point at +0x74).
uint8_t* MotionPath(CBaseComponent* motion)
{
    return static_cast<uint8_t*>(reinterpret_cast<CMotionPathMembers*>(motion)->GetPathFollower());
}
uint32_t& MotionFlags(CBaseComponent* motion) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(motion) + 0x1C); }

// `out` = `in` (all four) times `m`, summed as 0x0040AF60 does.
void Transform(const Mth::Matrix& m, Mth::Vector& out, const Mth::Vector& in) { m.Transform(out, in); }
// D3DXMatrixMultiply (linked library code).
void D3dxMultiply(Mth::Matrix& out, const Mth::Matrix& a, const Mth::Matrix& b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(&out, &a, &b);
}

// The collision feeler (Nx's ray, 0xB0 bytes): constructed by 0x00417760, its line from +0x00 to +0x10, tested
// by 0x004172F0; what it hit is held by a smart pointer at +0x98.
class Feeler {
public:
    Feeler() { reinterpret_cast<Nx::CFeeler*>(m_bytes)->Construct(); }
    ~Feeler() { reinterpret_cast<CSmtPtr<CRefCounted>*>(m_bytes + 0x98)->~CSmtPtr(); }
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    Mth::Vector& Start() { return *reinterpret_cast<Mth::Vector*>(m_bytes); }
    Mth::Vector& End() { return *reinterpret_cast<Mth::Vector*>(m_bytes + 0x10); }
    bool GetCollision(int a, int b) { return reinterpret_cast<Nx::CFeeler*>(m_bytes)->GetCollision(a != 0, b != 0) != 0; }

private:
    alignas(16) uint8_t m_bytes[0xB0];
};

// The angle of `v` about the y row of `m` from its at row, in the original's degrees or radians.
float AngleInPlane(const Mth::Matrix& m, const Mth::Vector& v) { return Mth::GetAngleInPlane(m, v, 2, 1); }

} // namespace

// 0x004E3270
float AvoidDistance(float angle, Script::CStruct* params)
{
    float distance = 0.0f;
    switch (Quarter(angle)) {
    case 1: params->GetFloat("distLeft", &distance, 1); break;
    case 2: params->GetFloat("distBack", &distance, 1); break;
    case 3: params->GetFloat("distRight", &distance, 1); break;
    default: params->GetFloat("distForward", &distance, 1); break;
    }
    return distance;
}

// 0x004E3930
bool LineCollides(const Mth::Vector& start, const Mth::Vector& end)
{
    Feeler feeler;
    feeler.Start() = start;
    feeler.End() = end;
    return feeler.GetCollision(1, 0);
}

// 0x004E39E0
CBaseComponent* __cdecl CAvoidComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CAvoidComponent));
    return memory ? static_cast<CAvoidComponent*>(memory)->Construct() : nullptr;
}

// 0x004E3310
void __cdecl CAvoidComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kAvoid), g_avoidMemberFunctions, 0x18);
}

// 0x004E3660
CAvoidComponent* CAvoidComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kAvoid;
    return this;
}

// 0x004E3330
void CAvoidComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x004E36C0
CAvoidComponent* CAvoidComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004E34C0
float CAvoidComponent::SideAwayFromSkater()
{
    CCompositeObject* skater = LocalSkater();
    // The skater's heading (its at row) flat and normalised; across it (x up), as the original's cross product
    // with (0, 1, 0) - the zero terms multiplied out.
    const Mth::Vector& at = skater->m_matrix.row[2];
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
    Mth::Vector side = {sideX, sideY, sideZ, 0.0f};
    // Which of the object's two sides is further from the skater (the first one's x and y unrounded).
    const Mth::Vector& pos = m_object->m_pos;
    double aX = static_cast<double>(sideX) + pos.x, aY = static_cast<double>(sideY) + pos.y;
    float aZ = sideZ + pos.z;
    float bX = pos.x - sideX, bY = pos.y - sideY, bZ = pos.z - sideZ;
    const Mth::Vector& s = skater->m_pos;
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
    return static_cast<float>(static_cast<double>(AngleInPlane(m_object->m_displayMatrix, side)) * kToDegrees);
}

// 0x004E3A40
bool CAvoidComponent::TryJump(const Mth::Matrix& start, Mth::Matrix& out, float angle, float distance,
                              float maxDistance)
{
    out = start;
    Mth::Matrix turn;
    D3dxMultiply(out, Mth::CreateRotateYMatrix(turn, static_cast<float>(static_cast<double>(angle) * kToRadians)), out);
    Mth::Vector probe = {0.0f, 10.0f, distance + 15.0f, 1.0f};
    Mth::Vector probeEnd;
    Transform(out, probeEnd, probe);
    if (LineCollides(start.row[3], probeEnd))
        return false;
    Mth::Vector landing = {0.0f, 0.0f, distance, 1.0f};
    Mth::Vector end;
    Transform(out, end, landing);
    out.row[3] = end;
    const Mth::Vector& s = LocalSkater()->m_pos;
    double dx = static_cast<double>(end.x) - s.x, dy = static_cast<double>(end.y) - s.y,
           dz = static_cast<double>(end.z) - s.z;
    double fromSkater = std::sqrt((dz * dz + dy * dy) + dx * dx);
    if (fromSkater < 30.0) {
        Dbg::Printf("Too close to skater %f!\n", fromSkater);
        return false;
    }
    double hx = static_cast<double>(end.x) - m_herdPoint.x, hz = static_cast<double>(end.z) - m_herdPoint.z;
    return !(std::sqrt(hz * hz + hx * hx) >= maxDistance); // (not a number: within, as the original's FCOMP)
}

// 0x004E3340
void CAvoidComponent::PlayJump(float angle, Script::CStruct* params)
{
    // The checksum is read into the parameters' own argument slot: without one it's the pointer's value.
    uint32_t anim = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
    switch (Quarter(angle)) {
    case 0:
    case 4:
        params->GetChecksum(kJumpForward, &anim, 1);
        break;
    case 1:
    case 3: {
        // The side's own animation, or the other side's flipped.
        uint32_t own = Quarter(angle) == 1 ? kJumpLeft : kJumpRight;
        uint32_t other = Quarter(angle) == 1 ? kJumpRight : kJumpLeft;
        bool flip = !params->GetChecksum(own, &anim, 0);
        if (flip)
            params->GetChecksum(other, &anim, 1);
        if (CBaseComponent* blender = m_object->GetComponent(kAnimBlender))
            FlipAnims(blender, m_object->m_id, flip, 0, 1);
        break;
    }
    case 2:
        params->GetChecksum(kJumpBack, &anim, 1);
        break;
    default:
        return;
    }
    if (CBaseComponent* blender = m_object->GetComponent(kAnimBlender)) {
        PlayAnim(blender, anim, 0);
        m_duration = AnimDuration(blender, anim);
    }
}

// 0x004E3BD0
bool CAvoidComponent::Jump(Script::CStruct* params, Script::CScript* /*script*/)
{
    // From where it stands, as its display matrix shows it.
    Mth::Matrix start = m_object->m_displayMatrix;
    start.row[3] = m_object->m_pos;
    float maxDistance = 100.0f;
    params->GetFloat("maxHerdDistance", &maxDistance, 0);
    Mth::Matrix end;
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
        const Mth::Vector& pos = start.row[3];
        Mth::Vector away = {m_herdPoint.x - pos.x, m_herdPoint.y - pos.y, m_herdPoint.z - pos.z,
                            m_herdPoint.w - pos.w};
        float angle = AngleInPlane(m_object->m_matrix, away);
        found = TryJump(start, end, angle, AvoidDistance(angle, params), maxDistance);
    }
    if (!found)
        return false;
    // The landing keeps the start's rows; the slerp from the start to it.
    Mth::Vector landing = end.row[3];
    end = start;
    end.row[3] = landing;
    m_slerp.SetMatrices(start, end);
    m_progress = 0.0f;
    const Mth::Vector& pos = start.row[3];
    Mth::Vector move = {landing.x - pos.x, landing.y - pos.y, landing.z - pos.z, landing.w - pos.w};
    PlayJump(AngleInPlane(m_object->m_matrix, move), params);
    return true;
}

// 0x004E36F0
bool CAvoidComponent::DebugBox(Script::CStruct* params, Script::CScript* /*script*/)
{
    float distance; // (read into the parameters' own argument slot: without one it keeps the pointer's bits)
    std::memcpy(&distance, &params, sizeof(distance));
    params->GetFloat("dist", &distance, 1);
    return false;
}

// 0x004E3EC0
bool CAvoidComponent::StepJump(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    m_progress = static_cast<float>(Tmr::GetSlomoFrameSeconds() * 60.0f * m_duration * 0.01666666753590107f +
                                    m_progress);
    Mth::Matrix now;
    m_slerp.GetMatrix(now, m_progress);
    m_object->m_pos = now.row[3];
    m_object->m_matrix = now;
    m_object->m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return true;
}

// 0x004E37D0
bool CAvoidComponent::JumpFinished(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    if (!(m_progress >= 1.0f))
        return false;
    return AnimComplete(m_object->GetComponent(kAnimBlender));
}

// 0x004E3810
bool CAvoidComponent::StorePosition(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    m_herdPoint = m_object->m_pos;
    return true;
}

// 0x004E3850
bool CAvoidComponent::StoreTarget(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    const Mth::Vector* point = &m_object->m_pos;
    CBaseComponent* motion = m_object->GetComponent(kMotion);
    if (motion && MotionPath(motion))
        point = reinterpret_cast<const Mth::Vector*>(MotionPath(motion) + 0x74);
    m_target = *point;
    return true;
}

// 0x004E38C0
bool CAvoidComponent::HoldMotion(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    uint32_t& flags = MotionFlags(m_object->GetComponent(kMotion));
    flags |= 4;
    m_motionHeld = flags != 0;
    return true;
}

// 0x004E38F0
bool CAvoidComponent::RestoreMotion(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    if (CBaseComponent* motion = m_object->GetComponent(kMotion)) {
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

REPLACE(0x004E39E0, Obj::CAvoidComponent::Create);
REPLACE(0x004E3310, Obj::CAvoidComponent::RegisterMemberFunctions);
REPLACE(0x004E3660, &Obj::CAvoidComponent::Construct);
REPLACE(0x004E3330, &Obj::CAvoidComponent::DestroyBody);
REPLACE(0x004E36C0, &Obj::CAvoidComponent::Destroy);
REPLACE(0x004E34C0, &Obj::CAvoidComponent::SideAwayFromSkater);
REPLACE(0x004E3A40, &Obj::CAvoidComponent::TryJump);
REPLACE(0x004E3340, &Obj::CAvoidComponent::PlayJump);
REPLACE(0x004E3BD0, &Obj::CAvoidComponent::Jump);
REPLACE(0x004E36F0, &Obj::CAvoidComponent::DebugBox);
REPLACE(0x004E3EC0, &Obj::CAvoidComponent::StepJump);
REPLACE(0x004E37D0, &Obj::CAvoidComponent::JumpFinished);
REPLACE(0x004E3810, &Obj::CAvoidComponent::StorePosition);
REPLACE(0x004E3850, &Obj::CAvoidComponent::StoreTarget);
REPLACE(0x004E38C0, &Obj::CAvoidComponent::HoldMotion);
REPLACE(0x004E38F0, &Obj::CAvoidComponent::RestoreMotion);
REPLACE_UNCHECKED(0x004E3270, Obj::AvoidDistanceStub);
REPLACE_UNCHECKED(0x004E3930, Obj::LineCollidesStub);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x0064B220,
    &Obj::CAvoidComponent::Destroy,
    &Obj::ComponentWithoutUpdate::Update,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    &Obj::CBaseComponent::RunMemberFunction,
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
