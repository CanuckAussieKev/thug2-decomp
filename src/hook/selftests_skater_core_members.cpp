// Self-tests for the skater core physics component's script member functions (core_member_functions.cpp): original
// vs ours on a stand-in component, every callee into game code stood in for (skater_core_testkit.h).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/object.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Skater::CorePhysicsComponent;

// Where the tests' own things are in the arena.
constexpr uint32_t kScript = kFree + 0x000;     // a script (+0x14 its parameters: a stand-in pointer)
constexpr uint32_t kComponentFake = kFree + 0x080; // what GetComponent returns
constexpr uint32_t kCameraFake = kFree + 0x100;    // what GetSkaterCamera returns
constexpr uint32_t kScoreFake = kFree + 0x180;     // what GetScore returns (or 0)
constexpr uint32_t kArrayFake = kFree + 0x200;     // the frictions' array (Script::CArray)
constexpr uint32_t kProfiles = kFree + 0x400;      // the profiles' table (*0x007CE478)

Script::CStruct* const kParams = reinterpret_cast<Script::CStruct*>(0x5A5A0000);

float PickFloatWide()
{
    static const float values[] = {0.0f, 0.5f, 1.0f, 2.5f, -1.5f, 30.0f, 60.0f, 89.0f, 200.0f, 0.001f, -0.25f, 7.0f,
                                   0.05f, -0.05f, 0.1f, -0.1f, 0.2f, 12.0f, 10.0f};
    return values[Next() % (sizeof(values) / sizeof(values[0]))];
}

StandIn Clock() { return {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}}; }

std::vector<StandIn> MemberStandIns()
{
    using Script::CArray;
    using Script::CStruct;
    return {
        Clock(),
        {0x004766D0, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))},
         [](Frame& f, Result& r) {
             if (r.eax) {
                 float value = PickFloatWide();
                 *reinterpret_cast<float*>(f.args[1]) = value;
                 RecordFloat(value);
             }
         }},
        {0x00476630, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         [](Frame& f, Result& r) {
             if (r.eax)
                 *reinterpret_cast<int*>(f.args[1]) = PickInt();
         }},
        {0x00476950, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         [](Frame& f, Result& r) {
             static const uint32_t names[] = {0x8D10119D, 0x12345678, 0x8D10119C};
             if (r.eax)
                 *reinterpret_cast<uint32_t*>(f.args[1]) = names[Next() % 3];
         }},
        {0x00476860, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray))},
         [](Frame& f, Result& r) {
             r.eax = 1;
             *reinterpret_cast<uint8_t**>(f.args[1]) = ArenaPtr(kArrayFake);
         }},
        {0x00476B40, 4, 1, Ret::Bool, true, 0,
         {Ours(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}},
        {0x0046CBF0, 4, 1, Ret::Float, true, 0, {Ours(&CArray::GetFloat)}},
        {0x00477C60, 8, 2, Ret::None, true, 0, {Ours(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}},
        {0x00477E20, 16, 4, Ret::None, true, 0,
         {Ours(static_cast<void (CStruct::*)(uint32_t, float, float, float)>(&CStruct::AddVector))}},
        {0x00477B80, 8, 2, Ret::None, true, 0, {Ours(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}},
        {0x00477D40, 8, 2, Ret::None, true, 0,
         {Ours(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)}},
        {0x0046F060, 0, 3, Ret::None, false, 0, {Ours(&Script::RegisterMemberFunctionTable)}},
        // The feeler's query: its line recorded; a hit or not, its face's flags, its normal and the object hit.
        {0x004172F0, 8, 2, Ret::Int, true, 3, {Ours(&Nx::CFeeler::GetCollision)},
         [](Frame& f, Result& r) {
             auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
             for (const Mth::Vector* v : {&feeler->m_line.start, &feeler->m_line.end}) {
                 RecordFloat(v->x);
                 RecordFloat(v->y);
                 RecordFloat(v->z);
                 RecordFloat(v->w);
             }
             r.eax = Next() % 4 ? 1 : 0;
             feeler->m_hit.faceFlags = static_cast<uint16_t>((Next() & 1 ? 8 : 0) | (Next() & 1 ? 0x101 : 0));
             feeler->m_normal = Mth::Vector(PickFloatWide(), PickFloatWide(), PickFloatWide(), 0.0f);
             feeler->m_object.mp_object = Next() % 3 ? nullptr : reinterpret_cast<Obj::CCompositeObject*>(0x5D5D0000);
         }},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
        {0x004061A0, 4, 1, Ret::Int, true, 0, {Ours(&Mth::Vector::SetDirection)},
         [](Frame& f, Result& r) {
             const auto* normal = reinterpret_cast<const Mth::Vector*>(f.args[0]);
             RecordFloat(normal->x);
             RecordFloat(normal->y);
             r.eax = f.ecx;
         }},
        {0x0045DB90, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObject::GetComponent)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kComponentFake)); }},
        {0x005114F0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CSkaterFlipAndRotateComponent::Flip)}},
        {0x00580880, 0, 0, Ret::Int, true, 0, {Ours(&Obj::CSkater::GetSkaterCamera)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kCameraFake)); }},
        {0x004E4000, 4, 1, Ret::None, true, 1, {Ours(&Obj::CCameraLookAroundComponent::SetLocked)}},
        {0x00580480, 0, 0, Ret::Int, true, 0, {Ours(&Obj::CSkater::GetScore)},
         [](Frame&, Result& r) { r.eax = Next() % 4 ? reinterpret_cast<uint32_t>(ArenaPtr(kScoreFake)) : 0; }},
    };
}

// The component's bytes and flags the member functions read, set at random (0, 1 or 2 for the bytes).
void FillMembers()
{
    FillArena();
    static const uint32_t bytes[] = {0x249, 0x2D8, 0x2D9, 0x2DB, 0x2ED, 0x300, 0x301, 0x302, 0x304, 0x310, 0x311, 0x324,
                                     0x370, 0x371, 0x372, 0x373, 0x375, 0x388, 0x3A4, 0x3A5, 0x1508, 0x1509, 0x14C8};
    for (uint32_t offset : bytes)
        ArenaAt<uint8_t>(kComponent + offset) = static_cast<uint8_t>(Next() % 3);
    static const uint32_t flags[] = {0x20, 0x28, 0x30, 0x58, 0x68, 0x70, 0x88, 0x90, 0xB8, 0xC8};
    for (uint32_t offset : flags) {
        ArenaAt<uint8_t>(kFlags + offset) = RandomBool(50);
        ArenaAt<int>(kFlags + offset + 4) = PickInt();
    }
    ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 6);
    // The air time's start and end: the clock's values less a threshold now and then (equal times).
    static const int starts[] = {0, 500, 970, 999, 1000, 940, -500, 300, 800, 50};
    static const int lengths[] = {30, 500, 200, 7, 0, 60, 2500, 1000};
    ArenaAt<int>(kComponent + 0x380) = starts[Next() % 10];
    ArenaAt<int>(kComponent + 0x384) = ArenaAt<int>(kComponent + 0x380) + lengths[Next() % 8];
    ArenaAt<float>(kComponent + 0x2E0) = PickFloatWide();
    ArenaAt<int>(kComponent + 0x318) = static_cast<int>(Next() % 6);
    if (Next() % 2)
        ArenaAt<float>(kComponent + 0x320) = 0.0f;
    if (Next() % 3 == 0)
        ArenaAt<uint32_t>(kComponent + 0x350) = 0xC0000000u;
    // The skater: its stance byte, its profile, a velocity fast or slow (or nearly still), a facing nearly vertical
    // at times.
    ArenaAt<uint8_t>(kObject + 0x110) = RandomBool(50);
    ArenaAt<int>(kObject + 0x2BC) = static_cast<int>(Next() % 4);
    static const float scales[] = {12.0f, 1.0f, 0.001f, 0.03f, 0.005f, 4.0f};
    float scale = scales[Next() % 6];
    for (uint32_t i = 0; i < 4; ++i)
        ArenaAt<float>(kObject + 0xAC + 4 * i) *= scale;
    switch (Next() % 4) {
    case 0: // (barely any horizontal facing)
        ArenaAt<float>(kObject + 0x8C) = 0.001f;
        ArenaAt<float>(kObject + 0x94) = -0.002f;
        break;
    case 1: // (a little)
        ArenaAt<float>(kObject + 0x8C) = 0.05f;
        ArenaAt<float>(kObject + 0x94) = 0.0f;
        break;
    default:
        break;
    }
    // The script, the frictions' array, the profiles.
    ArenaAt<uint32_t>(kScript + 0x14) = 0x5B5B0000;
    ArenaAt<uint32_t>(kArrayFake + 4) = 1 + Next() % 4;
    for (uint32_t i = 0; i < 0x400; ++i)
        ArenaAt<uint8_t>(kProfiles + i) = static_cast<uint8_t>(Next());
}

void TestCoreMemberFunctionTable()
{
    StandIns standIns(MemberStandIns());
    ClockOffsetZero clock;
    uint8_t* savedProfiles = GameVariable<uint8_t*>(0x007CE478);
    GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kProfiles);
    using Member = int (CorePhysicsComponent::*)(Script::CStruct*, Script::CScript*);
    struct Case {
        uint32_t address;
        Member ours;
    };
    static const Case cases[] = {
        {0x004FBF40, &CorePhysicsComponent::CanBrakeOff},
        {0x004FBF50, &CorePhysicsComponent::CanBrakeOn},
        {0x004FBF60, &CorePhysicsComponent::CanKickOn},
        {0x004FBF70, &CorePhysicsComponent::CanKickOff},
        {0x004FBF80, &CorePhysicsComponent::ForceAutoKickOn},
        {0x004FBF90, &CorePhysicsComponent::ForceAutoKickOff},
        {0x004FBFA0, &CorePhysicsComponent::AutoKickIsOff},
        {0x004FBFB0, &CorePhysicsComponent::SkaterVehicleOn},
        {0x004FBFC0, &CorePhysicsComponent::SkaterVehicleOff},
        {0x004FBFD0, &CorePhysicsComponent::IsSkaterOnVehicle},
        {0x004FBFE0, &CorePhysicsComponent::SetSkaterVehicle},
        {0x004FC000, &CorePhysicsComponent::CanSpin},
        {0x004FC010, &CorePhysicsComponent::BailOn},
        {0x004FC020, &CorePhysicsComponent::BailOff},
        {0x004FC030, &CorePhysicsComponent::BailIsOn},
        {0x004FC040, &CorePhysicsComponent::LockVelocityDirection},
        {0x004FC060, &CorePhysicsComponent::SetGrindTweak},
        {0x004FC080, &CorePhysicsComponent::BadLedge},
        {0x004FC090, &CorePhysicsComponent::LandedFromSpine},
        {0x004FC0A0, &CorePhysicsComponent::LandedFromVert},
        {0x004FC0B0, &CorePhysicsComponent::SetLandedFromVert},
        {0x004FC0C0, &CorePhysicsComponent::ResetLandedFromVert},
        {0x004FC0D0, &CorePhysicsComponent::AllowLipNoGrind},
        {0x004FC0E0, &CorePhysicsComponent::ClearAllowLipNoGrind},
        {0x004FC0F0, &CorePhysicsComponent::NoRailTricks},
        {0x004FC100, &CorePhysicsComponent::AllowRailTricks},
        {0x004FC110, &CorePhysicsComponent::SetExtraPush},
        {0x004FC180, &CorePhysicsComponent::SetAmmoType},
        {0x004FC1A0, &CorePhysicsComponent::InRevert},
        {0x004FC1B0, &CorePhysicsComponent::OutRevert},
        {0x004FC1C0, &CorePhysicsComponent::IsInRevert},
        {0x004FC1D0, &CorePhysicsComponent::ProjectilesOn},
        {0x004FC1E0, &CorePhysicsComponent::ProjectilesOff},
        {0x004FC1F0, &CorePhysicsComponent::FocusOn},
        {0x004FC200, &CorePhysicsComponent::IsSkaterFocused},
        {0x004FC210, &CorePhysicsComponent::ResetProjectileInput},
        {0x004FC230, &CorePhysicsComponent::EnterPowerslide},
        {0x004FC240, &CorePhysicsComponent::ExitPowerslide},
        {0x004FC250, &CorePhysicsComponent::MotoSkateboardOn},
        {0x004FC260, &CorePhysicsComponent::MotoSkateboardOff},
        {0x004FC270, &CorePhysicsComponent::IsOnMotoskateboard},
        {0x004FC280, &CorePhysicsComponent::SkaterIsFlipping},
        {0x004FC290, &CorePhysicsComponent::ResetIsFlipping},
        {0x004FC2A0, &CorePhysicsComponent::SetThrowingProjectile},
        {0x004FC2B0, &CorePhysicsComponent::UnsetThrowingProjectile},
        {0x004FC2C0, &CorePhysicsComponent::IsThrowingProjectile},
        {0x004FC2D0, &CorePhysicsComponent::EndManualTurnaround},
        {0x004FC2E0, &CorePhysicsComponent::EnterTantrum},
        {0x004FC2F0, &CorePhysicsComponent::ExitTantrum},
        {0x004FC300, &CorePhysicsComponent::InTantrum},
        {0x004FC310, &CorePhysicsComponent::EnterTagging},
        {0x004FC320, &CorePhysicsComponent::ExitTagging},
        {0x004FC330, &CorePhysicsComponent::IsTagging},
        {0x004FC340, &CorePhysicsComponent::DoingFlip},
        {0x004FC780, &CorePhysicsComponent::Flipped},
        {0x004FC7A0, &CorePhysicsComponent::Switched},
        {0x004FC7E0, &CorePhysicsComponent::Crouched},
        {0x004FC800, &CorePhysicsComponent::FirstTimeOnThisRail},
        {0x004FC820, &CorePhysicsComponent::Skitching},
        {0x004FC840, &CorePhysicsComponent::CancelWallpush},
        {0x004FC880, &CorePhysicsComponent::AirTimeLessThan},
        {0x004FC8E0, &CorePhysicsComponent::AirTimeGreaterThan},
        {0x004FC940, &CorePhysicsComponent::GetAirtime},
        {0x004FC9B0, &CorePhysicsComponent::Braking},
        {0x004FC9C0, &CorePhysicsComponent::RestoreAutoKick},
        {0x004FC9F0, &CorePhysicsComponent::DoCarPlantBoost},
        {0x004FCF90, &CorePhysicsComponent::NoSpin},
        {0x004FCFC0, &CorePhysicsComponent::InBail},
        {0x004FCFF0, &CorePhysicsComponent::NotInBail},
        {0x004FD020, &CorePhysicsComponent::IsInBail},
        {0x004FD040, &CorePhysicsComponent::SetRailSound},
        {0x004FD0D0, &CorePhysicsComponent::SetRollingFriction},
        {0x004FD140, &CorePhysicsComponent::LastSpinWas},
        {0x004FD1C0, &CorePhysicsComponent::InVertAir},
        {0x004FD1E0, &CorePhysicsComponent::TurnToFaceVelocity},
        {0x004FD360, &CorePhysicsComponent::OverrideLimits},
        {0x004FD4A0, &CorePhysicsComponent::SetSpecialFriction},
        {0x004FD530, &CorePhysicsComponent::OverrideCancelGround},
        {0x004FD580, &CorePhysicsComponent::GetMatrixNormal},
        {0x004FD5E0, &CorePhysicsComponent::GetLastGroundPos},
        {0x004FD610, &CorePhysicsComponent::GetLastGroundHeading},
        {0x004FD630, &CorePhysicsComponent::SetVertAir},
        {0x004FD660, &CorePhysicsComponent::IsFacingWall},
        {0x004FD840, &CorePhysicsComponent::TurnFromWall},
        {0x004FDA80, &CorePhysicsComponent::IsFacingVert},
        {0x004FDC30, &CorePhysicsComponent::GetAmmo},
        {0x004FDC50, &CorePhysicsComponent::GetAmmoType},
        {0x004FDC70, &CorePhysicsComponent::GetPitchBailDir},
        {0x004FDC90, &CorePhysicsComponent::FocusOff},
        {0x004FDCC0, &CorePhysicsComponent::GetFocusTimeLeft},
        {0x004FDCF0, &CorePhysicsComponent::GetFocusTimesAllowed},
        {0x004FDD20, &CorePhysicsComponent::SetFocusTimesAllowed},
    };
    constexpr int kCases = static_cast<int>(sizeof(cases) / sizeof(cases[0]));
    bool same = true;
    int variant = 0;
    for (; variant < kCases * 120 && same; ++variant) {
        g_rng = 0x2545F491u * (variant + 7);
        FillMembers();
        SaveStart();
        uint32_t seed = Next() | 1;
        auto* script = reinterpret_cast<Script::CScript*>(ArenaPtr(kScript));
        // Two extra cases: the threshold helper and the table's registration.
        int which = variant % (kCases + 2);
        std::vector<uint32_t> expected, actual;
        if (which == kCases) {
            expected = RunOnArena(seed, [](uint8_t* c) {
                return static_cast<uint32_t>(Original<int(__thiscall*)(void*, void*)>(0x004FBEA0)(c, kParams));
            });
            actual = RunOnArena(seed, [](uint8_t* c) {
                return static_cast<uint32_t>(reinterpret_cast<CorePhysicsComponent*>(c)->AirTimeThreshold(kParams));
            });
        } else if (which == kCases + 1) {
            expected = RunOnArena(seed, [](uint8_t*) {
                Original<void(__cdecl*)()>(0x004FBE80)();
                return 0u;
            });
            actual = RunOnArena(seed, [](uint8_t*) {
                CorePhysicsComponent::RegisterMemberFunctions();
                return 0u;
            });
        } else {
            const Case& test = cases[which];
            expected = RunOnArena(seed, [&](uint8_t* c) {
                return static_cast<uint32_t>(
                    Original<int(__thiscall*)(void*, void*, void*)>(test.address)(c, kParams, script));
            });
            actual = RunOnArena(seed, [&](uint8_t* c) {
                return static_cast<uint32_t>((reinterpret_cast<CorePhysicsComponent*>(c)->*test.ours)(kParams, script));
            });
            if (expected != actual)
                Log("  (member function 0x%08X)", test.address);
        }
        same = Compare(expected, actual, "core member function table", variant);
    }
    GameVariable<uint8_t*>(0x007CE478) = savedProfiles;
    Check(same, "skater: the core physics member functions of the table (flags, air time, feelers ahead, focus...)");
}

} // namespace

SELFTEST(Early, TestCoreMemberFunctionTable);
