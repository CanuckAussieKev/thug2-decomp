// Self-tests for the skater core physics' frame update (core_update.cpp): original vs ours on a stand-in component,
// every callee stood in for (skater_core_testkit.h) - the original's inlined TryGrind against our TryGrind.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/movable_contact.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/walk_hang.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Mth::Vector;
using Skater::CorePhysicsComponent;

constexpr uint32_t kSkate = kFree;              // a stand-in skate module (+0x24 the level's rail manager)
constexpr uint32_t kSkate2 = kFree + 0x40;      // the other module (0x007CE5D8: +0x40 the level)
constexpr uint32_t kComponents = kFree + 0x80;  // two moving rail managers' components (0x20 each), +0x40 the first
constexpr uint32_t kRideObject = kFree + 0x100; // what the skater ends up riding (+0xAC its velocity)

int g_airLands; // (the air's stand-in: 1 lands the skater, 2 leaves it in another state)

// The frame: every state (and an unknown one), the air's update landing or not, button +0x1C0 and flag +0x20, flags
// +0x98 / +0xA0, then the grind try in every case (as TestTryGrind), and +0x4F0 against the state, +0x30 and +0x78.
void TestCorePhysicsUpdate()
{
    static int s_found; // which FindGrindRail call finds one (1-based; 0 none)
    static int s_finds;
    auto state = [](uint32_t address, uint32_t ours) {
        return StandIn{address, 0, 0, Ret::None, true, 0, {ours}};
    };
    StandIns standIns({
        {0x005BEAA0, 0, 0, Ret::Float, false, 0, {Ours(&Tmr::GetSlomoFrameSeconds)}},
        {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}},
        {0x00501850, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::LimitSpeed)}},
        {0x00500670, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::UpdateCollisionCache)}, [](Frame&, Result&) {
             GameVariable<uint32_t>(0x00691974) = 0x0B0C5000;
         }},
        {0x00500900, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::UpdateModelRotation)}},
        state(0x0050EE70, Ours(&CorePhysicsComponent::DoGroundPhysics)),
        {0x0050CA00, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::DoAirPhysics)}, [](Frame&, Result&) {
             if (g_airLands)
                 ArenaAt<int>(kFlags + 0x18) = g_airLands == 1 ? 0 : 3;
         }},
        state(0x0050F830, Ours(&CorePhysicsComponent::DoWallRidePhysics)),
        state(0x0050BF50, Ours(&CorePhysicsComponent::DoLipPhysics)),
        state(0x0050DDF0, Ours(&CorePhysicsComponent::DoRailPhysics)),
        state(0x00509050, Ours(&CorePhysicsComponent::DoWallplantPhysics)),
        {0x00503EC0, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::TryStartSkitch)}},
        {0x004FF200, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::UpdateSpeedOverride)}},
        {0x00500710, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::UpdateFreeRevert)}},
        {0x005007A0, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::UpdateFocusButton)}},
        {0x00500820, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::UpdateProjectiles)}},
        {0x00500BC0, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::UpdateSpecialTime)}},
        {0x00460A20, 12, 3, Ret::None, true, 0, {Ours(&Obj::CObject::BroadcastEvent)}},
        // TryGrind's
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
             float* m = reinterpret_cast<float*>(f.args[0]);
             static const float matrix[16] = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 10, -4, 2.5f, 1};
             std::memcpy(m, matrix, sizeof(matrix));
             m[12] = RandomFloat(20.0f);
         }},
        {0x0043EB80, 4, 1, Ret::None, true, 0, {Ours(&Walk::MovableContact::StartRiding)}},
        {0x00427DD0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::UpdateContactPoint)}},
    });
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
    uint32_t savedBox = GameVariable<uint32_t>(0x00691974);
    bool same = true;
    int states[7] = {}, landed = 0, grinds = 0, events = 0, pressed = 0;
    for (int variant = 0; variant < 6000 && same; ++variant) {
        g_rng = 0x510830u * (variant + 1) + 0x9E3779B9u;
        FillArena();
        GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kSkate);
        GameVariable<uint8_t*>(0x007CE5D8) = ArenaPtr(kSkate2);
        GameVariable<void*>(0x006F0FB8) = reinterpret_cast<void*>(0x0C0C0000);
        GameVariable<uint32_t>(0x00691974) = 0x0DDBA11;
        ArenaAt<uint8_t*>(kSkate + 0x24) = ArenaPtr(kComponents + 0x60);
        ArenaAt<uint32_t>(kSkate2 + 0x40) = Next() % 4 ? 0x1234 : 0xD443A2BC;
        int st = Next() % 3 ? 1 : static_cast<int>(Next() % 7); // (6: none of the states)
        ArenaAt<int>(kFlags + 0x18) = st;
        g_airLands = static_cast<int>(Next() % 3);
        for (uint32_t f : {0x20u, 0x30u, 0x70u, 0x78u, 0x98u, 0xA0u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(40);
        ArenaAt<uint8_t>(kInput + 0x1C0) = RandomBool(50);
        ArenaAt<uint8_t>(kComponent + 0x3A4) = RandomBool(10);
        ArenaAt<uint8_t>(kInput + 0x1A0) = RandomBool(85);
        ArenaAt<uint32_t>(kComponent + 0x23C) = Next() % 400;
        ArenaAt<uint8_t>(kComponent + 0x4F0) = RandomBool(50);
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
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            s_finds = 0;
            Original<void(__thiscall*)(void*)>(0x00510830)(c);
            return GameVariable<uint32_t>(0x00691974);
        });
        GameVariable<uint32_t>(0x00691974) = 0x0DDBA11;
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            s_finds = 0;
            reinterpret_cast<CorePhysicsComponent*>(c)->Update();
            return GameVariable<uint32_t>(0x00691974);
        });
        ++states[st];
        landed += st == 1 && g_airLands == 1;
        grinds += Called(actual, 0x0050A770);
        events += Called(actual, 0x00460A20);
        pressed += Called(actual, 0x0044ECE0);
        same = Compare(expected, actual, "core physics update", variant);
    }
    GameVariable<uint8_t*>(0x007CE478) = savedSkate;
    GameVariable<uint8_t*>(0x007CE5D8) = savedSkate2;
    GameVariable<void*>(0x006F0FB8) = savedManager;
    GameVariable<uint32_t>(0x00691974) = savedBox;
    bool covered = landed > 300 && grinds > 100 && events > 500 && pressed > 1000;
    for (int s : states)
        covered = covered && s > 50;
    if (!covered)
        Log("  core physics update coverage: landed %d, grinds %d, events %d, clock %d, states %d %d %d %d %d %d %d",
            landed, grinds, events, pressed, states[0], states[1], states[2], states[3], states[4], states[5],
            states[6]);
    Check(same && covered, "skater: core physics Update");
}

} // namespace

SELFTEST(Early, TestCorePhysicsUpdate);
