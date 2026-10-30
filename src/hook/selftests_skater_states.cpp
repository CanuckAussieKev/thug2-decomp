// Self-tests for the skater core physics' lip update, getting back on the board and trying for a rail
// (core_states.cpp, core_grind.cpp TryGrind): original vs ours on a stand-in component, every callee stood in for
// (skater_core_testkit.h).

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/movable_contact.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/obj/walk_hang.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/manual.h"
#include "game/skater/trick_component.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Mth::Vector;
using Skater::CorePhysicsComponent;

constexpr uint32_t kSkate = kFree;              // a stand-in skate module (+0x20 career, +0x24 rail manager)
constexpr uint32_t kBalance = kFree + 0x40;     // the balance trick component (0x110 bytes)
constexpr uint32_t kRailMgr = kFree + 0x160;    // a rail manager (+0x44 its node array)
constexpr uint32_t kNode = kFree + 0x1C0;       // a rail node
constexpr uint32_t kGrindData = kFree + 0x220;  // ResumeSkating's rail (point, +0x10 manager, +0x14 node, +0x18)
constexpr uint32_t kOther1534 = kFree + 0x260;
constexpr uint32_t kComponents = kFree + 0x280; // two moving rail managers' components (0x20 each)
constexpr uint32_t kSkate2 = kFree + 0x2C0;     // the other module (0x007CE5D8: +0x40 the level)
constexpr uint32_t kRideObject = kFree + 0x300; // what the skater ends up riding (+0xAC its velocity)

// LipPhysics' stand-ins.
std::vector<StandIn> LipStandIns()
{
    return {
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}},
        {0x00416F00, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CRideTracker::Update)}},
        {0x00427A00, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CMovableContactComponent::GetContactPoint)}, [](Frame& f, Result& r) {
             float* out = reinterpret_cast<float*>(f.args[0]);
             for (int i = 0; i < 4; ++i)
                 out[i] = RandomFloat(9.0f);
             r.eax = f.args[0];
         }},
        {0x005BFA8C, 12, 3, Ret::Int, false},
        {0x00584CC0, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CSkaterCareer::GetCheat)}},
        {0x005843A0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CSkaterCareer::SetGlobalFlag)}},
        {0x00478F60, 0, 1, Ret::Int, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}},
        {0x00579CC0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBalance::Update)}},
        {0x004FFB10, 0, 0, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::JumpReleased)}},
        {0x005092E0, 4, 1, Ret::None, true, 0, {Ours(&CorePhysicsComponent::TriggerRailScript)}},
        {0x0045C1B0, 0, 2, Ret::Int, false, 0, {Ours(&SkateScript::GetNode)},
         [](Frame&, Result& r) { r.eax = 0x51000000u; }},
        {0x00476950, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         [](Frame& f, Result& r) {
             r.eax = 1;
             *reinterpret_cast<uint32_t*>(f.args[1]) = 0x6000 + Next() % 4;
         }},
        {0x00522370, 4, 1, Ret::None, true, 0, {Ours(&Obj::CTrickComponent::PassToTrickObjects)}},
    };
}

// DoLipPhysics 0x0050BF50.
void TestDoLipPhysics()
{
    StandIns standIns(LipStandIns());
    ClockOffsetZero clock;
    uint8_t* savedSkate = GameVariable<uint8_t*>(0x007CE478);
    uint8_t savedFlag = GameVariable<uint8_t>(0x007CE4FD);
    bool same = true;
    int carried = 0, perfect = 0, off = 0;
    for (int variant = 0; variant < 4000 && same; ++variant) {
        g_rng = 0x3C6EF372u * (variant + 1);
        FillArena();
        GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kSkate);
        GameVariable<uint8_t>(0x007CE4FD) = 0;
        ArenaAt<uint32_t>(kSkate + 0x20) = 0xCA000000; // (the career: stood in)
        ArenaAt<uint8_t*>(kComponent + 0x1524) = ArenaPtr(kBalance);
        ArenaAt<uint32_t>(kBalance + 0x18) = Next() % 3 ? 0xA549B57B : 0x255ED86F;
        for (uint32_t f : {0x20u, 0x78u, 0x80u, 0xA8u, 0xD8u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(50);
        ArenaAt<uint8_t>(kInput + 0x1C0) = RandomBool(50);
        uint32_t ride = Next() % 4;
        ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint32_t>(kTracker) = ride >= 2 ? 0x0BEEF000 : 0;
        ArenaAt<uint8_t>(kTracker + 0xAC) = RandomBool(50);
        ArenaAt<uint8_t*>(kComponent + 0x22C) = Next() % 3 ? ArenaPtr(kNode) : nullptr;
        ArenaAt<uint8_t*>(kComponent + 0x230) = ArenaPtr(kRailMgr);
        ArenaAt<uint32_t>(kRailMgr + 0x44) = Next() % 3 ? 0x0A000000 : 0;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(0x0050BF50)(c);
            return static_cast<uint32_t>(GameVariable<uint8_t>(0x007CE4FD));
        });
        GameVariable<uint8_t>(0x007CE4FD) = 0;
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->DoLipPhysics();
            return static_cast<uint32_t>(GameVariable<uint8_t>(0x007CE4FD));
        });
        carried += Called(actual, 0x00427A00);
        perfect += Called(actual, 0x005843A0);
        off += Called(actual, 0x005092E0);
        same = Compare(expected, actual, "lip physics", variant);
    }
    GameVariable<uint8_t*>(0x007CE478) = savedSkate;
    GameVariable<uint8_t>(0x007CE4FD) = savedFlag;
    bool covered = carried > 300 && perfect > 300 && off > 500;
    if (!covered)
        Log("  lip physics coverage: carried %d, perfect rail %d, off %d", carried, perfect, off);
    Check(same && covered, "skater: DoLipPhysics");
}

// ResumeSkating 0x0050C670: on the ground or not (+0x1534's +0x9B), +0x350's special value or not, a rail (with an
// object or not, riding something before or not) or an acid drop or neither.
void TestResumeSkating()
{
    StandIns standIns({{0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
                       {0x004FDE40, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::ClearFlags)}},
                       {0x004FDED0, 4, 1, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::SetPhysicsState)}},
                       {0x00416DD0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CSmtPtr<Obj::CRefCounted>::Release)}},
                       {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
                       {0x0043EB80, 4, 1, Ret::None, true, 0, {Ours(&Walk::MovableContact::StartRiding)}},
                       {0x0050A770, 0x14, 5, Ret::None, true, 0x18, {Ours(&CorePhysicsComponent::StartGrind)},
                        [](Frame&, Result&) { ArenaAt<float>(kObject + 0x4C) += 3.0f; }},
                       {0x00502F20, 4, 1, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::StartAcidDrop)}}});
    UseOurs matrices({{0x00404090, Ours(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(&Mth::Matrix::operator=))}});
    ClockOffsetZero clock;
    bool same = true;
    for (int variant = 0; variant < 3000 && same; ++variant) {
        g_rng = 0x6A09E667u * (variant + 1);
        FillArena();
        ArenaAt<uint8_t*>(kComponent + 0x1534) = ArenaPtr(kOther1534);
        ArenaAt<uint8_t>(kOther1534 + 0x9B) = RandomBool(50);
        for (uint32_t f : {0xB0u, 0xC0u, 0xF4u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(50);
        if (Next() % 3 == 0)
            ArenaAt<uint32_t>(kComponent + 0x350) = 0xC0000000;
        ArenaAt<uint8_t*>(kRide + 0x18) = Next() % 2 ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint32_t>(kGrindData + 0x18) = Next() % 2 ? 0x0BEE0000 : 0;
        bool onGround = Next() & 1;
        uint32_t what = Next() % 3;
        const uint8_t* grind = what == 0 ? ArenaPtr(kGrindData) : nullptr;
        void* acid = what == 1 ? reinterpret_cast<void*>(0x0AC1D000) : nullptr;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [&](uint8_t* c) {
            Original<void(__thiscall*)(void*, bool, const uint8_t*, void*)>(0x0050C670)(c, onGround, grind, acid);
            return 0u;
        });
        auto actual = RunOnArena(seed, [&](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->ResumeSkating(onGround, grind, acid);
            return 0u;
        });
        same = Compare(expected, actual, "resume skating", variant);
    }
    Check(same, "skater: ResumeSkating");
}

// TryGrind 0x0050DAE0: in the air or `always`, the blocking flags, the grind button, the wallplant's time, the
// corner angle's conditions (the park editor, the time since the last rail), the level's rail found (allowed or
// not), else the moving rail managers (none, one, two; found on the first, the second or neither).
void TestTryGrind()
{
    static int s_found; // which FindGrindRail call finds one (1-based; 0 none)
    static int s_finds;
    StandIns standIns({
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00478F60, 0, 1, Ret::Int, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}},
        {0x00420250, 0, 1, Ret::Int, false, 0, {Ours(&Tmr::GameClockTimeSince)}},
        {0x0057E2D0, 0x1C, 7, Ret::Bool, true, 0, {Ours(&Obj::CRailManager::FindGrindRail)},
         [](Frame& f, Result& r) {
             for (int i = 0; i < 4; ++i) {
                 RecordFloat(reinterpret_cast<float*>(f.args[0])[i]);
                 RecordFloat(reinterpret_cast<float*>(f.args[1])[i]);
             }
             r.eax = ++s_finds == s_found;
             float* point = reinterpret_cast<float*>(f.args[2]);
             for (int i = 0; i < 4; ++i)
                 point[i] = RandomFloat(50.0f);
             *reinterpret_cast<uint32_t*>(f.args[3]) = 0x0DE00000 + s_finds;
         }},
        {0x004FC4A0, 0, 3, Ret::None, false, 0, {Ours(Skater::TrackingPrint)}},
        {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}},
        {0x00500020, 12, 3, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CanGrindRail)}},
        {0x0050A770, 0x14, 5, Ret::None, true, 0x18, {Ours(&CorePhysicsComponent::StartGrind)},
         [](Frame& f, Result&) {
             for (int i = 0; i < 4; ++i)
                 RecordFloat(reinterpret_cast<float*>(f.args[0])[i]);
         }},
        {0x0045F2D0, 4, 1, Ret::Int, true, 0, {Ours(&Obj::CCompositeObjectManager::GetFirstComponentByType)},
         [](Frame&, Result& r) { r.eax = ArenaAt<uint32_t>(kComponents + 0x40); }},
        {0x0042BA10, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRailManagerComponent::UpdateRailMatrix)}, [](Frame& f, Result&) {
             // (the object's matrix: a turn and a move, invertible)
             float* m = reinterpret_cast<float*>(f.args[0]);
             static const float matrix[16] = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 10, -4, 2.5f, 1};
             std::memcpy(m, matrix, sizeof(matrix));
             m[12] = RandomFloat(20.0f);
         }},
        {0x0043EB80, 4, 1, Ret::None, true, 0, {Ours(&Walk::MovableContact::StartRiding)}},
        {0x00427DD0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::UpdateContactPoint)}},
    });
    // The components' vtable: slot 13 returns the component's rail manager (+0x1C).
    static void* vtable[16];
    struct Slot {
        static void* __fastcall Rails(void* component) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(component) + 0x1C); }
    };
    for (void*& slot : vtable)
        slot = reinterpret_cast<void*>(&Slot::Rails);
    UseOurs matrices({{0x00405600, Ours(&Mth::Matrix::Invert)},
                      {0x0040AF60, Ours(&Mth::Matrix::Transform)},
                      {0x00404090, Ours(static_cast<Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&)>(&Mth::Matrix::operator=))},
                      {0x00406050, Ours(&Vector::operator-=)}});
    ClockOffsetZero clock;
    uint8_t* savedSkate = GameVariable<uint8_t*>(0x007CE478);
    uint8_t* savedSkate2 = GameVariable<uint8_t*>(0x007CE5D8);
    void* savedManager = GameVariable<void*>(0x006F0FB8);
    bool same = true;
    int levelRail = 0, movingRail = 0, cornered = 0;
    for (int variant = 0; variant < 6000 && same; ++variant) {
        g_rng = 0xBB67AE85u * (variant + 1);
        FillArena();
        GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kSkate);
        GameVariable<uint8_t*>(0x007CE5D8) = ArenaPtr(kSkate2);
        GameVariable<void*>(0x006F0FB8) = reinterpret_cast<void*>(0x0C0C0000);
        ArenaAt<uint8_t*>(kSkate + 0x24) = ArenaPtr(kRailMgr);
        ArenaAt<uint32_t>(kSkate2 + 0x40) = Next() % 4 ? 0x1234 : 0xD443A2BC;
        ArenaAt<int>(kFlags + 0x18) = Next() % 3 ? 1 : static_cast<int>(Next() % 6);
        ArenaAt<uint8_t>(kFlags + 0x70) = RandomBool(10);
        ArenaAt<uint8_t>(kComponent + 0x3A4) = RandomBool(10);
        ArenaAt<uint8_t>(kInput + 0x1A0) = RandomBool(85);
        ArenaAt<uint32_t>(kComponent + 0x23C) = Next() % 400;
        // Moving rail managers: none, one or two (each: +0x0C its object, +0x14 the next, +0x1C its rails).
        int moving = static_cast<int>(Next() % 3);
        for (int i = 0; i < 2; ++i) {
            uint32_t at = kComponents + i * 0x20;
            ArenaAt<void**>(at) = vtable;
            ArenaAt<uint32_t>(at + 0xC) = 0x0B000000 + i;
            ArenaAt<uint8_t*>(at + 0x14) = i == 0 && moving == 2 ? ArenaPtr(kComponents + 0x20) : nullptr;
            ArenaAt<uint32_t>(at + 0x1C) = 0x0EA00000 + i;
        }
        ArenaAt<uint8_t*>(kComponents + 0x40) = moving ? ArenaPtr(kComponents) : nullptr;
        ArenaAt<uint8_t*>(kRide + 0x18) = ArenaPtr(kTracker);
        ArenaAt<uint8_t*>(kTracker) = ArenaPtr(kRideObject);
        s_found = static_cast<int>(Next() % 4);
        bool always = Next() % 4 == 0;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [always](uint8_t* c) {
            s_finds = 0;
            Original<void(__thiscall*)(void*, bool)>(0x0050DAE0)(c, always);
            return 0u;
        });
        auto actual = RunOnArena(seed, [always](uint8_t* c) {
            s_finds = 0;
            reinterpret_cast<CorePhysicsComponent*>(c)->TryGrind(always);
            return 0u;
        });
        levelRail += Called(actual, 0x004FC4A0) && Called(actual, 0x0050A770);
        movingRail += Called(actual, 0x00427DD0);
        cornered += Called(actual, 0x00478F00);
        same = Compare(expected, actual, "try grind", variant);
    }
    GameVariable<uint8_t*>(0x007CE478) = savedSkate;
    GameVariable<uint8_t*>(0x007CE5D8) = savedSkate2;
    GameVariable<void*>(0x006F0FB8) = savedManager;
    bool covered = levelRail > 200 && movingRail > 100 && cornered > 500;
    if (!covered)
        Log("  try grind coverage: level rail %d, moving rail %d, corner angle %d", levelRail, movingRail, cornered);
    Check(same && covered, "skater: TryGrind");
}

} // namespace

SELFTEST(Early, TestDoLipPhysics);
SELFTEST(Early, TestResumeSkating);
SELFTEST(Early, TestTryGrind);
