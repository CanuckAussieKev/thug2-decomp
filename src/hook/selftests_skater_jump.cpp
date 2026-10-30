// Self-tests for the skater core physics' jumping, skitching, rail-script and wallplant functions (core_jump.cpp):
// original vs ours on a stand-in component, every callee stood in for (skater_core_testkit.h).

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/obj/movable_contact.h"
#include "game/obj/object.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/smtptr.h"
#include "game/obj/trigger_component.h"
#include "game/obj/walk_hang.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/skater/skater_sound.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skater_core_testkit.h"

namespace {

using namespace SkaterCoreKit;
using Skater::CorePhysicsComponent;

// --- stand-ins shared by the tests ---
StandIn Clock() { return {0x0044ECE0, 0, 0, Ret::Int, false, 0, {Ours(&Tmr::GameClockElapsed)}}; }
StandIn SetStateCall() { return {0x004FDED0, 4, 1, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::SetPhysicsState)}}; }
StandIn TriggerFeelerCall() { return {0x00434BF0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CTriggerComponent::CheckFeeler)}}; }
StandIn GetScaled()
{
    return {0x00580790, 4, 1, Ret::Float, true, 0, {Ours(&Obj::CSkater::GetScaledPhysics)}};
}
StandIn ScriptGetInteger()
{
    return {0x00478F60, 0, 1, Ret::Int, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}};
}
StandIn ScriptGetFloat()
{
    return {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}};
}
StandIn ContainsFlag()
{
    return {0x00476B40, 4, 1, Ret::Bool, true, 0,
            {Ours(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))}};
}
StandIn RailScript()
{
    return {0x005092E0, 4, 1, Ret::None, true, 0, {Ours(&CorePhysicsComponent::TriggerRailScript)}};
}
StandIn SelfEvent() { return {0x004609F0, 8, 2, Ret::None, true, 0, {Ours(&Obj::CObject::SelfEvent)}}; }
StandIn BroadcastEvent() { return {0x00460A20, 12, 3, Ret::None, true, 0, {Ours(&Obj::CObject::BroadcastEvent)}}; }
StandIn StopRiding() { return {0x00427C70, 0, 0, Ret::None, true, 0, {Ours(&Obj::CMovableContactComponent::StopRiding)}}; }

// The script nodes TriggerRailScript looks at: rail node i's script node is the fake pointer 0x51000000 + i * 0x100
// (none when `missing`), with a TriggerScript or not, and its Name.
struct FakeScriptNode {
    uint32_t name;
    bool missing;
    bool hasScript;
    uint32_t script;
    uint32_t nodeName;
};
std::vector<FakeScriptNode> g_scriptNodes;

StandIn GetNodeCall()
{
    return {0x0045C1B0, 0, 2, Ret::Int, false, 0, {Ours(&SkateScript::GetNode)}, [](Frame& f, Result& r) {
                r.eax = 0;
                for (size_t i = 0; i < g_scriptNodes.size(); ++i) {
                    if (g_scriptNodes[i].name == f.args[0] && !g_scriptNodes[i].missing)
                        r.eax = 0x51000000u + static_cast<uint32_t>(i) * 0x100;
                }
            }};
}
StandIn GetChecksumCall()
{
    return {0x00476950, 12, 3, Ret::Bool, true, 0,
            {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
            [](Frame& f, Result& r) {
                uint32_t* out = reinterpret_cast<uint32_t*>(f.args[1]);
                uint32_t index = (f.ecx - 0x51000000u) / 0x100;
                if (f.ecx < 0x51000000u || index >= g_scriptNodes.size()) {
                    r.eax = 0;
                    return;
                }
                const FakeScriptNode& node = g_scriptNodes[index];
                if (f.args[0] == 0x2CA8A299) { // TriggerScript
                    r.eax = node.hasScript;
                    if (node.hasScript)
                        *out = node.script;
                } else if (f.args[0] == 0xA1DC81F9) { // Name
                    r.eax = 1;
                    *out = node.nodeName;
                }
            }};
}

// Each test's own arena things.
constexpr uint32_t kNodes = kFree;          // rail nodes, 0x54 bytes each (8)
constexpr uint32_t kSkate = kFree + 0x300;  // a stand-in skate module (+0x24 its rail manager)
constexpr uint32_t kSkitchObject = kFree + 0x380;

// TriggerRailScript 0x005092E0: a rail of up to 8 nodes back along m_prev (ending, or looping back to the first or
// into itself), each script node there or not, with a TriggerScript or not; the rail manager set or not; the last
// node's name the same or not; riding something (still there or gone) or not.
void TestTriggerRailScript()
{
    StandIns standIns({Clock(), GetNodeCall(), GetChecksumCall(), StopRiding(),
                       {0x00434A60, 16, 4, Ret::Bool, true, 0, {Ours(&Obj::CTriggerComponent::TriggerNode)}}});
    ClockOffsetZero clock;
    uint8_t* savedSkate = GameVariable<uint8_t*>(0x007CE478);
    bool same = true;
    int ran = 0, walked = 0, looped = 0;
    for (int variant = 0; variant < 4000 && same; ++variant) {
        g_rng = 0x9E3779B9u * (variant + 1);
        FillArena();
        GameVariable<uint8_t*>(0x007CE478) = ArenaPtr(kSkate);
        int count = 1 + static_cast<int>(Next() % 8);
        g_scriptNodes.clear();
        for (int i = 0; i < count; ++i) {
            uint8_t* node = ArenaPtr(kNodes + i * 0x54);
            uint32_t name = 0x7000 + i * 0x11;
            *reinterpret_cast<uint32_t*>(node + 0x4C) = name;
            *reinterpret_cast<uint8_t**>(node + 0x44) = i + 1 < count ? ArenaPtr(kNodes + (i + 1) * 0x54) : nullptr;
            g_scriptNodes.push_back({name, RandomBool(i == 0 ? 10 : 3) != 0, RandomBool(25) != 0, 0x9000 + Next() % 50,
                                     0x6000 + Next() % 3});
        }
        uint32_t end = Next() % 4;
        if (end == 1 && count > 1) // (back to the first)
            ArenaAt<uint8_t*>(kNodes + (count - 1) * 0x54 + 0x44) = ArenaPtr(kNodes);
        else if (end == 2 && count > 2) // (into a loop further on)
            ArenaAt<uint8_t*>(kNodes + (count - 1) * 0x54 + 0x44) = ArenaPtr(kNodes + 0x54 * (1 + Next() % (count - 2)));
        bool loops = (end == 1 && count > 1) || (end == 2 && count > 2);
        if (loops) // (a loop never finds a script: the walk must stop)
            for (FakeScriptNode& n : g_scriptNodes)
                n.hasScript = n.hasScript && RandomBool(30);
        ArenaAt<uint8_t*>(kComponent + 0x22C) = Next() % 15 ? ArenaPtr(kNodes) : nullptr;
        ArenaAt<uint32_t>(kComponent + 0x230) = Next() % 2 ? 0 : 0x12345;
        ArenaAt<uint32_t>(kComponent + 0x244) = 0x6000 + Next() % 3;
        ArenaAt<uint8_t>(kFlags + 0xC8) = RandomBool(50);
        uint32_t ride = Next() % 3;
        ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint32_t>(kTracker) = ride == 2 ? 0x0BEEF000 : 0;
        SaveStart();
        uint32_t seed = Next() | 1;
        const int* events = reinterpret_cast<const int*>(0x0064BC1C);
        auto expected = RunOnArena(seed, [events](uint8_t* c) {
            Original<void(__thiscall*)(void*, const int*)>(0x005092E0)(c, events);
            return 0u;
        });
        auto actual = RunOnArena(seed, [events](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->TriggerRailScript(events);
            return 0u;
        });
        if (Called(actual, 0x00434A60))
            ++ran;
        int nodeLookups = 0;
        for (size_t i = 1; i < actual.size() - kArenaSize / 4; ++i)
            nodeLookups += actual[i] == 0x0045C1B0;
        walked += nodeLookups > 2;
        looped += loops && nodeLookups > 1;
        same = Compare(expected, actual, "rail script", variant);
    }
    GameVariable<uint8_t*>(0x007CE478) = savedSkate;
    bool covered = ran > 300 && walked > 200 && looped > 100;
    if (!covered)
        Log("  rail script coverage: ran %d, walked %d, looped %d", ran, walked, looped);
    Check(same && covered, "skater: TriggerRailScript");
}

// DoJump 0x00509450: each state, no_sound or not, BonelessHeight or not, on vert or not, the held time below,
// at or over the max (and a max of 0), a speed in the parameters or not, going down or up, a steep ramp or not.
void TestDoJump()
{
    StandIns standIns({Clock(), ContainsFlag(), TriggerFeelerCall(), GetScaled(), ScriptGetInteger(), SetStateCall(),
                       RailScript(), BroadcastEvent(),
                       {0x00518EE0, 8, 2, Ret::None, true, 2, {Ours(&Obj::CSkaterSoundComponent::PlayJumpInState)}},
                       {0x004FEBA0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::StartVertAir)}},
                       {0x005002D0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::TurnForRailJump)}},
                       {0x004766D0, 12, 3, Ret::Bool, true, 0,
                        {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(&Script::CStruct::GetFloat))},
                        [](Frame& f, Result& r) {
                            if (r.eax)
                                *reinterpret_cast<float*>(f.args[1]) = RandomFloat(20.0f);
                        }}});
    ClockOffsetZero clock;
    bool same = true;
    int states[6] = {}, offVert = 0, steep = 0, tookOver = 0;
    for (int variant = 0; variant < 8000 && same; ++variant) {
        g_rng = 0x51ED2701u * (variant + 1);
        FillArena();
        int state = static_cast<int>(Next() % 6);
        ArenaAt<int>(kFlags + 0x18) = state;
        for (uint32_t f : {0x20u, 0x30u, 0x40u, 0x48u})
            ArenaAt<uint8_t>(kFlags + f) = RandomBool(40);
        ArenaAt<uint8_t>(kFlags + 0xE1) = static_cast<uint8_t>(Next());
        ArenaAt<uint8_t>(kComponent + 0x1AA) = static_cast<uint8_t>(Next());
        ArenaAt<uint8_t>(kComponent + 0x2D0) = RandomBool(50);
        ArenaAt<uint8_t>(kComponent + 0x2EC) = RandomBool(30);
        ArenaAt<int>(kComponent + 0x94) = static_cast<int>(Next() % 6);
        ArenaAt<int>(kComponent + 0x378) = PickInt();
        ArenaAt<uint8_t>(kTriggerState + 0x26) = RandomBool(20);
        if (Next() % 3 == 0)
            ArenaAt<float>(kObject + 0xB0) = 0.0f;
        if (Next() % 3 == 0)
            ArenaAt<float>(kObject + 0x80) = -0.1f;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto params = reinterpret_cast<Script::CStruct*>(0x5A5A0000);
        auto expected = RunOnArena(seed, [params](uint8_t* c) {
            Original<void(__thiscall*)(void*, void*)>(0x00509450)(c, params);
            return 0u;
        });
        auto actual = RunOnArena(seed, [params](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->DoJump(params);
            return 0u;
        });
        ++states[state];
        bool jumped = Called(actual, 0x00460A20);
        tookOver += !jumped;
        offVert += jumped && g_arenaStart[kFlags + 0x30] && g_arenaStart[kObject + 0xB3] & 0x80;
        steep += jumped && ArenaAt<float>(kObject + 0x80) < -0.1f;
        same = Compare(expected, actual, "jump", variant);
    }
    bool covered = tookOver > 50 && offVert > 200 && steep > 500;
    if (!covered)
        Log("  jump coverage: took over %d, off vert %d, steep %d", tookOver, offVert, steep);
    Check(same && covered, "skater: DoJump");
}

// BeginSkitch 0x00509920 / EndSkitch 0x00509990: something to skitch or not, skitching or not, riding or not.
void TestSkitch()
{
    StandIns standIns({Clock(), SelfEvent(),
                       {0x0043EB80, 4, 1, Ret::None, true, 0, {Ours(&Walk::MovableContact::StartRiding)}},
                       {0x00501DB0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::Skitch)}},
                       {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
                       {0x00416DD0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CSmtPtr<Obj::CRefCounted>::Release)}},
                       {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}}});
    ClockOffsetZero clock;
    bool same = true;
    for (int variant = 0; variant < 2000 && same; ++variant) {
        g_rng = 0x2545F491u * (variant + 1);
        FillArena();
        ArenaAt<uint8_t*>(kComponent + 0x364) = Next() % 3 ? ArenaPtr(kSkitchObject) : nullptr;
        ArenaAt<uint8_t>(kFlags + 0x88) = RandomBool(60);
        ArenaAt<uint8_t*>(kRide + 0x18) = Next() % 3 ? ArenaPtr(kTracker) : nullptr;
        SaveStart();
        uint32_t seed = Next() | 1;
        bool begin = variant & 1;
        auto expected = RunOnArena(seed, [begin](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(begin ? 0x00509920 : 0x00509990)(c);
            return 0u;
        });
        auto actual = RunOnArena(seed, [begin](uint8_t* c) {
            auto* component = reinterpret_cast<CorePhysicsComponent*>(c);
            if (begin)
                component->BeginSkitch();
            else
                component->EndSkitch();
            return 0u;
        });
        same = Compare(expected, actual, begin ? "begin skitch" : "end skitch", variant);
    }
    Check(same, "skater: BeginSkitch / EndSkitch");
}

// DoWallplantPhysics 0x00509050: the time up (or not; times that wrap), the feeler hitting or not; still on: riding
// nothing, something gone, something that turned or not.
void TestDoWallplantPhysics()
{
    StandIn duration = ScriptGetFloat();
    duration.action = [](Frame&, Result& r) {
        static const float values[] = {0.0f, 100.0f, 500.0f, 1000.0f, 2000.0f, 1.0e9f, 250.0f};
        r.st0 = values[Next() % 7];
    };
    StandIns standIns({Clock(), duration, SetStateCall(), TriggerFeelerCall(), StopRiding(),
                       {0x005003A0, 8, 2, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::CheckFeeler)}},
                       {0x00416F00, 4, 1, Ret::Bool, true, 0, {Ours(&Obj::CRideTracker::Update)}},
                       {0x005BFA8C, 12, 3, Ret::Int, false}});
    ClockOffsetZero clock;
    bool same = true;
    int over = 0, turned = 0;
    for (int variant = 0; variant < 4000 && same; ++variant) {
        g_rng = 0x6C078965u * (variant + 1);
        FillArena();
        ArenaAt<int>(kComponent + 0x90) = Next() % 4 ? PickInt() : static_cast<int>(Next());
        uint32_t ride = Next() % 4;
        ArenaAt<uint8_t*>(kRide + 0x18) = ride ? ArenaPtr(kTracker) : nullptr;
        ArenaAt<uint32_t>(kTracker) = ride >= 2 ? 0x0BEEF000 : 0;
        ArenaAt<uint8_t>(kTracker + 0xAC) = RandomBool(50);
        SaveStart();
        uint32_t seed = Next() | 1;
        auto expected = RunOnArena(seed, [](uint8_t* c) {
            Original<void(__thiscall*)(void*)>(0x00509050)(c);
            return 0u;
        });
        auto actual = RunOnArena(seed, [](uint8_t* c) {
            reinterpret_cast<CorePhysicsComponent*>(c)->DoWallplantPhysics();
            return 0u;
        });
        over += Called(actual, 0x004FDED0);
        turned += Called(actual, 0x005BFA8C);
        same = Compare(expected, actual, "wallplant", variant);
    }
    bool covered = over > 500 && turned > 100;
    if (!covered)
        Log("  wallplant coverage: over %d, turned %d", over, turned);
    Check(same && covered, "skater: DoWallplantPhysics");
}

// The script member functions 0x00509A40-0x00509BB0: each with its worker stood in; SetState with each state
// name (and others) in each state.
void TestCoreMemberFunctions()
{
    StandIns standIns({Clock(), SetStateCall(), RailScript(),
                       {0x00509450, 4, 1, Ret::None, true, 0, {Ours(&CorePhysicsComponent::DoJump)}},
                       {0x00509920, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::BeginSkitch)}},
                       {0x00509990, 0, 0, Ret::None, true, 0, {Ours(&CorePhysicsComponent::EndSkitch)}},
                       {0x00507120, 4, 1, Ret::None, true, 1, {Ours(&CorePhysicsComponent::BreakVert)}},
                       {0x00504DF0, 0, 0, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::KeepUpright)}},
                       {0x00507500, 12, 3, Ret::Bool, true, 0, {Ours(&Skater::CorePhysicsComponent::WallPlant)}},
                       {0x004FE860, 16, 4, Ret::None, true, 0, {Ours(&Skater::CorePhysicsComponent::TurnToNormal)}},
                       {0x00476950, 12, 3, Ret::Bool, true,
                        0,
                        {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(
                            &Script::CStruct::GetChecksum))},
                        [](Frame& f, Result& r) {
                            static const uint32_t names[] = {0x439F4704, 0x58007C97, 0xA549B57B, 0x12345678};
                            if (r.eax)
                                *reinterpret_cast<uint32_t*>(f.args[1]) = names[Next() % 4];
                        }}});
    ClockOffsetZero clock;
    using Member = int (CorePhysicsComponent::*)(Script::CStruct*, Script::CScript*);
    struct Case {
        uint32_t address;
        Member ours;
    };
    static const Case cases[] = {
        {0x00509A40, &CorePhysicsComponent::Jump},           {0x00509A60, &CorePhysicsComponent::StartSkitch},
        {0x00509A70, &CorePhysicsComponent::StopSkitch},     {0x00509A80, &CorePhysicsComponent::SetState},
        {0x00509B70, &CorePhysicsComponent::TriggerGrindOff}, {0x00509B90, &CorePhysicsComponent::ForceBreakVert},
        {0x00509BB0, &CorePhysicsComponent::WalkingWallplant},
    };
    bool same = true;
    for (int variant = 0; variant < 3000 && same; ++variant) {
        g_rng = 0x41C64E6Du * (variant + 1);
        FillArena();
        ArenaAt<int>(kFlags + 0x18) = static_cast<int>(Next() % 6);
        const Case& test = cases[variant % 7];
        SaveStart();
        uint32_t seed = Next() | 1;
        auto params = reinterpret_cast<Script::CStruct*>(0x5A5A0000);
        auto script = reinterpret_cast<Script::CScript*>(0x5C5C0000);
        auto expected = RunOnArena(seed, [&](uint8_t* c) {
            return static_cast<uint32_t>(
                Original<int(__thiscall*)(void*, void*, void*)>(test.address)(c, params, script));
        });
        auto actual = RunOnArena(seed, [&](uint8_t* c) {
            return static_cast<uint32_t>((reinterpret_cast<CorePhysicsComponent*>(c)->*test.ours)(params, script));
        });
        same = Compare(expected, actual, "core member function", variant);
    }
    Check(same, "skater: the core physics member functions (Jump, StartSkitch, StopSkitch, SetState, ...)");
}

} // namespace

SELFTEST(Early, TestTriggerRailScript);
SELFTEST(Early, TestDoJump);
SELFTEST(Early, TestSkitch);
SELFTEST(Early, TestDoWallplantPhysics);
SELFTEST(Early, TestCoreMemberFunctions);
