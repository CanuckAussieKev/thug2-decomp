// Self-tests for the skater core physics' ground update (core_ground.cpp): original vs ours on a stand-in component,
// every callee stood in for (skater_core_testkit.h); the vector functions the original calls pointed at ours.

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/mth/math.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/movable_contact.h"
#include "game/obj/ride_tracker.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/manual.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
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

constexpr uint32_t kBalanceTrick = kFree;       // (+0x18 the kind; balances at +0x24 / +0x12C; leans)
constexpr uint32_t kSkate = kFree + 0x180;      // the skate module (+0x20 the career)
constexpr uint32_t kSkitched = kFree + 0x1C0;   // the skitched object (+0xAC its velocity)

// CheckForGround stand-in: sometimes moves the skater back part of the way, sometimes changes the state.
void GroundStandIn(Frame&, Result&)
{
    float* p = &ArenaAt<float>(kObject + 0x4C);
    switch (Next() % 4) {
    case 0:
        p[0] -= 0.5f, p[2] += 0.25f;
        break;
    case 1:
        p[1] = 0.0f;
        break;
    case 2:
        if (Next() % 3 == 0) { // (stopped dead, short of the step)
            std::memset(ArenaPtr(kObject + 0xAC), 0, 12);
            p[0] -= 3.0f;
        }
        break;
    default:
        break;
    }
    if (Next() % 8 == 0)
        ArenaAt<int>(kFlags + 0x18) = 1;
    RecordFloat(p[0]);
}

void TestDoGroundPhysics()
{
    StandIns standIns({
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00403EF0, 0, 1, Ret::Float, false, 0, {Ours(&Mth::FastACos)},
         [](Frame&, Result& r) { r.st0 = static_cast<float>(static_cast<double>(Next()) / 4294967296.0 * 3.14159); }},
        {0x00580400, 0, 3, Ret::Float, false, 0, {Ours(&Obj::GetPhysicsFloat)}},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}},
        {0x00478F60, 0, 1, Ret::Int, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}},
        {0x004FE320, 0, 0, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::ShouldBrake)}},
        {0x005019E0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::Brake)}},
        {0x004FE490, 0, 0, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CanPushOff)}},
        {0x004FE530, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::PushOff)}},
        {0x00579CC0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CBalance::Update)}},
        {0x00584CC0, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CSkaterCareer::GetCheat)}},
        {0x005843A0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CSkaterCareer::SetGlobalFlag)}},
        {0x005232A0, 4, 1, Ret::None, true, 1, {Ours(&Obj::CTrickComponent::SetTrickObjects)}},
        {0x00506DF0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::ApplyGroundFriction)}},
        {0x005055B0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckSideFeelers)}},
        {0x00501DB0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::Skitch)}},
        {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}},
        {0x00416F00, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CRideTracker::Update)}},
        {0x005BFA8C, 12, 3, Ret::Int, false},
        {0x00506E50, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckWallAhead)}},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckFeeler)}, [](Frame& f, Result&) {
             for (int i = 0; i < 8; ++i) // (the feeler's own line, set before)
                 RecordFloat(reinterpret_cast<float*>(f.ecx + 0xA8)[i]);
         }},
        {0x005196A0, 4, 1, Ret::None, true, 1, {Ours(&Obj::CSkaterStateComponent::SetTerrain)}},
        {0x00509BD0, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::CheckForGround)}, GroundStandIn},
        {0x00503560, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::TurnOnGround)}},
        {0x004FF590, 4, 1, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::AlongFacing)}},
        {0x0050C930, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::CheckSideCollisions)}},
        {0x00503B30, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::LeanPushOffWall)}},
        {0x004FF920, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::TurnRoundIfBackwards)}},
        {0x004FFB10, 0, 0, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::JumpReleased)}},
        {0x004FEBA0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::StartVertAir)}},
        {0x00522370, 4, 1, Ret::None, true, 0, {Ours(&Obj::CTrickComponent::PassToTrickObjects)}},
    });
    UseOurs vectors({{0x004060F0, Ours(&Vector::RotateToPlane)}, {0x00406080, Ours(&Vector::ProjectToPlane)}});
    ClockOffsetZero clock;
    uint8_t* savedSkate = GameVariable<uint8_t*>(0x007CE478);
    uint8_t savedFlag = GameVariable<uint8_t>(0x007CE4FD);
    bool same = true;
    int retried = 0, skitching = 0, cheats = 0, kinds[4] = {};
    for (int variant = 0; variant < 16000 && same; ++variant) {
        g_rng = 0x1F83D9ABu * (variant + 1);
        FillArena();
        GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kSkate);
        GameVariable<uint8_t>(0x007CE4FD) = 0;
        ArenaAt<uint32_t>(kSkate + 0x20) = 0xCA000000;
        ArenaAt<uint8_t*>(kComponent + 0x1524) = ArenaPtr(kBalanceTrick);
        static const uint32_t kinds4[] = {0, 0x3506CE64, 0x0AC90769, 0xEF24413B, 0x255ED86F};
        uint32_t kind = kinds4[Next() % 5];
        ArenaAt<uint32_t>(kBalanceTrick + 0x18) = kind;
        ArenaAt<int>(kFlags + 0x18) = 0;
        for (uint32_t f : {0x30u, 0x38u, 0x48u, 0x78u, 0x80u, 0x88u, 0xA8u, 0xB0u, 0xD8u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(f == 0x88 ? 25 : 50);
        for (uint32_t b : {0x2D8u, 0x302u, 0x388u})
            ArenaAt<uint8_t>(kComponent + b) = RandomBool(40);
        if (Next() % 3)
            ArenaAt<float>(kComponent + 0x350) = 0.0f;
        uint32_t ride = Next() % 4;
        ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint8_t*>(kTracker) = ride >= 2 ? ArenaPtr(kSkitched) : nullptr;
        ArenaAt<uint8_t>(kTracker + 0xAC) = RandomBool(50);
        ArenaAt<float>(kComponent + 0x8C) = Next() % 2 ? 1.0f / 60.0f : RandomFloat(0.5f);
        float* v = &ArenaAt<float>(kObject + 0xAC);
        if (Next() % 5 == 0)
            v[0] = v[1] = v[2] = 0.0f;
        else if (Next() % 8 == 0)
            v[0] = 0.001f, v[1] = v[2] = 0.0f; // (moving exactly the least)
        if (Next() % 4 == 0)
            for (int i = 0; i < 4; ++i)
                v[i] = static_cast<float>((static_cast<double>(Next()) / 4294967296.0 - 0.5) * 900.0);
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(0x0050EE70)(c);
            return static_cast<uint32_t>(GameVariable<uint8_t>(0x007CE4FD));
        });
        GameVariable<uint8_t>(0x007CE4FD) = 0;
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->DoGroundPhysics();
            return static_cast<uint32_t>(GameVariable<uint8_t>(0x007CE4FD));
        });
        int grounds = 0;
        for (size_t i = 1; i < actual.size() - kArenaSize / 4; ++i)
            grounds += actual[i] == 0x00509BD0 || actual[i] == 0x005003A0;
        retried += grounds > 1;
        skitching += g_arenaStart[kFlags + 0x88] != 0;
        cheats += actual[0] != 0;
        ++kinds[kind == 0 ? 0 : kind == 0x3506CE64 ? 1 : kind == 0x255ED86F ? 3 : 2];
        same = Compare(expected, actual, "ground physics", variant);
    }
    GameVariable<uint8_t*>(0x007CE478) = savedSkate;
    GameVariable<uint8_t>(0x007CE4FD) = savedFlag;
    bool covered = retried > 500 && skitching > 1000 && cheats > 500;
    if (!covered)
        Log("  ground physics coverage: retried %d, skitching %d, cheats %d", retried, skitching, cheats);
    Check(same && covered, "skater: DoGroundPhysics");
}

} // namespace

SELFTEST(Early, TestDoGroundPhysics);
