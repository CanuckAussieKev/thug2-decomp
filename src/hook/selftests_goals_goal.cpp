// Self-tests for the goal base class (Game::CGoal, 0x00553E00-0x00557ABF): the original against ours on the same
// stand-in goals (their vtable a recording stand-in, so the virtual calls show), parameters (every structure method
// stood in for, answered from tables), goal flags (hand-built structures, walked for real), goal manager.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "game/core/manager.h"
#include "game/mth/random.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/goals/goal.h"
#include "game/goals/goal_types.h"
#include "game/gfx/camera.h"
#include "game/skate/competition.h"
#include "game/core/crc.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/trick_objects.h"
#include "game/skater/end_run.h"
#include "game/skater/skater.h"
#include "game/goals/goal_manager.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Game::CGoal;
using Game::GoalLink;

alignas(16) uint8_t g_goals[4][0x80];
alignas(16) uint8_t g_params[4][0x10];   // (each goal's parameters: a stand-in - all their methods are)
alignas(16) uint8_t g_saveData[0x10];
alignas(16) GoalLink g_links[8];
Params g_flagsA, g_flagsB, g_skaterA, g_skaterB, g_flagsSkaters;
alignas(16) uint8_t g_trickArray[0x10];
alignas(16) uint8_t g_keyArray[0x10];

CGoal* Goal(int i) { return reinterpret_cast<CGoal*>(g_goals[i]); }
uint32_t Address(const void* p) { return reinterpret_cast<uint32_t>(p); }

// A deterministic generator (the test's own).
uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
bool Coin() { return (Next() & 1) != 0; }

constexpr uint32_t kNames[] = {
    0xF0E712D2, 0x906B67BA, 0xCD66C8AE, 0xB3790F33, 0xCF6CD3C1, 0x1171A555, 0xE462E527, 0xC8A82B5A, 0xB394C01C,
    0x4147922B, 0x981D3AD0, 0x129DAA10, 0xFD7AE1F4, 0x696FE0AB, 0x955C6305, 0xDBEB46E2, 0x5FD80EDC, 0x38221DF4,
    0xD303A1A3, 0x0D15EA00, 0x4AF5D34E, 0xA302D7CD, 0x49807745, 0xD3E93882, 0x2206B1E7, 0xC22A2B72, 0x96963902,
    0xCC3C4CC4, 0x32D771EE, 0xA36700EF, 0x9D65D0E7, 0xC5EC08E6, 0xF8E31760, 0x9982E501, 0x11111111, 0x22222222,
};

// A state made from the seed: the goals' fields, the answers of every structure method and virtual.
void MakeState(uint32_t seed)
{
    g_seed = seed * 2654435761u + 12345;
    for (int i = 0; i < 4; ++i) {
        for (uint32_t k = 0; k < sizeof(g_goals[i]); ++k)
            g_goals[i][k] = static_cast<uint8_t>(Next());
        CGoal* goal = Goal(i);
        goal->m_node.vtable = g_goalVtable;
        goal->m_params = reinterpret_cast<Script::CStruct*>(g_params[i]);
        goal->m_goalFlags = g_flagsA.Struct();
        goal->m_children = nullptr;
        goal->m_parents = nullptr;
        goal->m_level = static_cast<int>(Next() % 12);
        goal->m_group = static_cast<int>(Next() % 5) - 1;
        goal->m_numFlags = static_cast<int>(Next() % 6);
        goal->m_numFlagsSet = static_cast<int>(Next() % 6);
        goal->m_timeLeft = static_cast<int>(Next() % 200000) - 1000;
    }
    // Links: goal 0's children 1 and 2 (or a first link with no goal), goal 1's child 3.
    std::memset(g_links, 0, sizeof(g_links));
    for (GoalLink& link : g_links)
        link.m_won = Coin();
    g_links[0] = {Coin() ? 0x1001u : 0u, &g_links[1], Coin(), {}};
    g_links[1] = {0x1002, Coin() ? &g_links[2] : nullptr, Coin(), {}};
    g_links[2] = {0x9999, nullptr, Coin(), {}}; // (no such goal)
    g_links[3] = {0x1003, nullptr, Coin(), {}};
    Goal(0)->m_children = &g_links[0];
    Goal(1)->m_children = Coin() ? &g_links[3] : nullptr;
    g_goalsById = {{0x1001, Address(Goal(1))}, {0x1002, Address(Goal(2))}, {0x1003, Address(Goal(3))}};
    g_groupLocked = Coin();
    for (uint32_t& answer : g_virtualAnswers)
        answer = Next() & 1;
    g_getAnswers.clear();
    g_typedAnswers.clear();
    g_hasFlag.clear();
    g_hasComponent.clear();
    g_emptyAnswer = Coin();
    for (int i = 0; i < 4; ++i) {
        for (uint32_t name : kNames) {
            uint32_t structure = Address(g_params[i]);
            if (Next() % 3 == 0)
                g_getAnswers[{structure, name}] = Next() % 4 == 0 ? Next() % 5 : Next() % 300000;
            if (Next() % 3 == 0)
                g_hasFlag.insert({structure, name});
            if (Next() % 3 == 0)
                g_hasComponent.insert({structure, name});
        }
    }
    // (number is asserted: the original's local is left unset without it)
    for (int i = 0; i < 4; ++i)
        g_getAnswers[{Address(g_params[i]), 0x696FE0AB}] = Next() % 4;
    // record_type: time, score or another
    uint32_t types[3] = {0x906B67BA, 0xCD66C8AE, 0x12345678};
    for (int i = 0; i < 4; ++i) {
        if (Coin())
            g_getAnswers[{Address(g_params[i]), 0x96963902}] = types[Next() % 3];
        else
            g_getAnswers.erase({Address(g_params[i]), 0x96963902});
        // proset_prefix: a string
        static const char* const kPrefixes[] = {"ProA", "PROA", "prob"};
        if (Coin())
            g_getAnswers[{Address(g_params[i]), 0x955C6305}] = Address(kPrefixes[Next() % 3]);
        else
            g_getAnswers.erase({Address(g_params[i]), 0x955C6305});
    }
    // The goal flags' structures: their integers and the skater_flags flag.
    for (Params* p : {&g_flagsA, &g_flagsB, &g_skaterA, &g_skaterB, &g_flagsSkaters}) {
        uint32_t structure = Address(p->Struct());
        for (uint32_t name : {0xB3790F33u, 0xCF6CD3C1u, 0x11111111u, 0x22222222u})
            if (Coin() || p == &g_skaterA || p == &g_skaterB) // (the skaters' counts are always there: asserted)
                g_getAnswers[{structure, name}] = Next() % 4;
        if (Next() % 4 == 0)
            g_hasFlag.insert({structure, 0xE462E527});
        if (Coin())
            g_hasComponent.insert({structure, 0x11111111});
        if (Coin())
            g_hasComponent.insert({structure, 0x12121212}); // (there, but not an integer: no answer)
        if (Coin())
            g_getAnswers[{structure, 0x33333333}] = Address(g_skaterB.Struct());
        for (int c = 0; c < p->count; ++c)
            p->components[c].raw = Next() % 3;
    }
    g_hasFlag.insert({Address(g_flagsSkaters.Struct()), 0xE462E527});
    g_skaterA.components[0].raw = Address(g_skaterA.Struct()); // (refilled below)
    g_flagsSkaters.components[0].raw = Address(g_skaterA.Struct());
    g_flagsSkaters.components[2].raw = Address(g_skaterB.Struct());
    g_getAnswers[{Address(g_saveData), 0xCC3C4CC4}] = Address(g_flagsB.Struct());
    for (uint32_t name : {0x49807745u, 0xD3E93882u, 0x2206B1E7u, 0xC22A2B72u})
        if (Coin())
            g_getAnswers[{Address(g_saveData), name}] = Next() % 3;
    if (Coin())
        g_hasComponent.insert({Address(g_saveData), 0xCC3C4CC4});
}

void RecordGoals()
{
    for (auto& goal : g_goals)
        for (uint32_t k = 0; k < sizeof(goal); k += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(goal + k));
    for (GoalLink& link : g_links)
        g_trace.insert(g_trace.end(), {link.m_goalId, Address(link.m_next), link.m_won ? 1u : 0u});
    for (Params* p : {&g_flagsA, &g_flagsB, &g_skaterA, &g_skaterB, &g_flagsSkaters})
        for (int c = 0; c < p->count; ++c)
            g_trace.push_back(p->components[c].raw);
}

template <typename R, typename... A>
R Call(bool original, uint32_t address, R (CGoal::*ours)(A...), CGoal* goal, std::type_identity_t<A>... args)
{
    return original ? Original<R(__thiscall*)(CGoal*, A...)>(address)(goal, args...) : (goal->*ours)(args...);
}

void StandInGoalCalls(StandIns& s)
{
    StandInGoalMemory(s);
    StandInStructAdds(s);
    StandInPrintf(s);
    StandInScripts(s);
    StandInArrays(s);
    StandInGetters(s);
    StandInContains(s);
    StandInStructChanges(s);
    StandInGoalManager(s);
    MakeGoalVtable(s);
    // The game's C runtime: _stricmp, sprintf (the original's; ours call the C runtime's own).
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]),
                                              reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x0046CB70, AddressOf(&CArray::GetLocalString)}, 4, [](Regs& r) {
        Record({0xCB70, r.ecx, r.args[0]});
        static const char* const kTricks[] = {"Kickflip", "Heel flip", "", "Varial", "Impossible"};
        static const char* const kKeys[] = {"Up+Square", "Left+Circle", "Down", "X"};
        return r.ecx == Address(g_trickArray) ? Address(kTricks[r.args[0] % 5]) : Address(kKeys[r.args[0] % 4]);
    });
}

void TestGoalBasics()
{
    Setup();
    SaveGlobal(0x00684FC8, 4); // the slomo
    auto* standIns = new StandIns;
    StandInGoalCalls(*standIns);
    // The goal flags' structures (unnamed flags and num_flags: no num_flags where the original would loop for ever).
    g_flagsA = Params();
    g_flagsA.Integer(0x11111111, 1).Integer(0x22222222, 0).Integer(0x44444444, 2);
    g_flagsB = Params();
    g_flagsB.Integer(0x55555555, 0).Integer(0x66666666, 1);
    g_skaterA = Params();
    g_skaterA.Integer(0x77777777, 1).Integer(0x88888888, 1);
    g_skaterB = Params();
    g_skaterB.Integer(0x99999999, 0);
    g_flagsSkaters = Params();
    g_flagsSkaters.Structure(0xAAAAAAAA, g_skaterA.Struct()).Flag(0xE462E527).Structure(0xBBBBBBBB, g_skaterB.Struct());

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        GameVariable<float>(0x00684FC8) = 1.25f;
        for (uint32_t seed = 0; seed < 160; ++seed) {
            MakeState(seed);
            CGoal* g = Goal(0);
            Record({0xEEEE0000 | seed});
            if (seed % 4 == 1)
                g->m_goalFlags = g_flagsSkaters.Struct();
            else if (seed % 4 == 2)
                g->m_goalFlags = g_flagsB.Struct();
            Call(o, 0x00553E00, &CGoal::PassInheritedFlags, g, static_cast<int>(seed));
            Call(o, 0x00553E50, &CGoal::ClearChildWins, g);
            RecordGoals();
            Record({Address(Call(o, 0x00553E90, &CGoal::GetParams, g))});
            Call(o, 0x00553EA0, &CGoal::ResetTimer, g);
            Record({static_cast<uint32_t>(g->m_timeLeft), g->m_state});
            Call(o, 0x00553F10, &CGoal::SetTimeLeft, g, static_cast<int>(seed) - 7);
            Record({static_cast<uint32_t>(Call(o, 0x00553F20, &CGoal::GetTimeLeft, g))});
            Call(o, 0x00553F30, &CGoal::AddTime, g, static_cast<int>(seed * 3));
            if (seed & 1)
                Call(o, 0x00553F50, &CGoal::SetActive, g);
            else
                Call(o, 0x00553F60, &CGoal::SetInactive, g);
            Record({Call(o, 0x00553F70, &CGoal::EditParams, g, reinterpret_cast<Script::CStruct*>(g_params[2])) ? 1u
                                                                                                                : 0u});
            Record({Call(o, 0x00553F90, &CGoal::IsActive, g), Call(o, 0x00553FA0, &CGoal::HasSeen, g)});
            if (seed & 2)
                Call(o, 0x00553FB0, &CGoal::Pause, g);
            else
                Call(o, 0x00553FC0, &CGoal::UnPause, g);
            for (uint32_t flag : {0x11111111u, 0x22222222u, 0x12121212u}) {
                Record({Call(o, 0x00553FD0, &CGoal::GoalFlagEquals, g, flag, static_cast<int>(seed % 3), 0u),
                        Call(o, 0x00553FD0, &CGoal::GoalFlagEquals, g, flag, 1, 0x33333333u),
                        Call(o, 0x00553FD0, &CGoal::GoalFlagEquals, g, flag,
                             static_cast<int>(Address(g_skaterB.Struct())), 0x33333333u),
                        Call(o, 0x005541E0, &CGoal::GoalFlagSet, g, flag, 0u),
                        Call(o, 0x005541E0, &CGoal::GoalFlagSet, g, flag, seed & 1 ? 0x33333333u : 0x34343434u)});
            }
            Record({Call(o, 0x00554110, &CGoal::AllFlagsSet, g)});
            RecordGoals();
            if (seed % 4 != 2 || !(seed & 4)) {
                Call(o, 0x00554070, &CGoal::ResetGoalFlags, g);
                RecordGoals();
            }
            if (seed % 5 == 0) {
                if (o)
                    Original<void(__cdecl*)(Script::CStruct*)>(0x00554030)(g_flagsB.Struct());
                else
                    CGoal::ClearFlagValues(g_flagsB.Struct());
            }
            Record({Call(o, 0x00554220, &CGoal::IsLocked, g), Call(o, 0x00554250, &CGoal::IsGraffiti, g),
                    Call(o, 0x00554260, &CGoal::IsSpecial, g), Call(o, 0x00554270, &CGoal::IsTetris, g),
                    Call(o, 0x00554280, &CGoal::GetGoalId, g), Call(o, 0x005542B0, &CGoal::IsEditedGoal, g),
                    Call(o, 0x005542C0, &CGoal::HasWonGoalOrChild, g), Call(o, 0x005542F0, &CGoal::IsSelected, g),
                    Call(o, 0x00554300, &CGoal::IsEditedGoal2, g)});
            Call(o, 0x00554320, &CGoal::GotCounterObject, g);
            Record({Call(o, 0x00554390, &CGoal::CounterGoalDone, g), Call(o, 0x005543E0, &CGoal::State02, g),
                    Call(o, 0x005543F0, &CGoal::HasProsetPrefix, g, "proa"),
                    Call(o, 0x005543F0, &CGoal::HasProsetPrefix, g, "proB")});
            switch (seed % 3) {
            case 0: Call(o, 0x00554430, &CGoal::SetHasSeen, g); break;
            case 1: Call(o, 0x00554440, &CGoal::Select, g); break;
            default: Call(o, 0x00554450, &CGoal::Deselect, g); break;
            }
            Record({Call(o, 0x00554460, &CGoal::CanRetry, g)});
            if (seed & 8)
                Call(o, 0x00554470, &CGoal::ResetCurrentTime, g);
            Call(o, 0x00554480, &CGoal::UpdateCurrentTime, g);
            if (seed & 16)
                Call(o, 0x00554510, &CGoal::SetQuickStart, g);
            else
                Call(o, 0x00554520, &CGoal::UnsetQuickStart, g);
            const char* text = reinterpret_cast<const char*>(0x5EED);
            Record({Call(o, 0x00554530, &CGoal::IsProGoal, g), Call(o, 0x00554540, &CGoal::IsNetGoal, g),
                    Call(o, 0x00554550, &CGoal::IsCompetition, g), Call(o, 0x00554560, &CGoal::IsPaused, g),
                    Call(o, 0x00554570, &CGoal::GetViewGoalsText, g, &text), Address(text),
                    Call(o, 0x00554590, &CGoal::EndOfRunStarted, g), Call(o, 0x005545A0, &CGoal::HasTimeLimit, g),
                    Call(o, 0x005545F0, &CGoal::HasComponentA302D7CD, g)});
            Call(o, 0x00554770, &CGoal::GetSaveData, g, reinterpret_cast<Script::CStruct*>(g_saveData));
            Call(o, 0x00554610, &CGoal::LoadSaveData, g, reinterpret_cast<Script::CStruct*>(g_saveData));
            Record({static_cast<uint32_t>(Call(o, 0x005549A0, &CGoal::GetNumberCollected, g)),
                    static_cast<uint32_t>(Call(o, 0x005549D0, &CGoal::GetNumFlagsToWin, g)),
                    static_cast<uint32_t>(Call(o, 0x00554A00, &CGoal::GetTimesStarted, g))});
            Call(o, 0x00554A10, &CGoal::SetEndRunType, g, static_cast<int>(seed % 3));
            Call(o, 0x00554A20, &CGoal::SetDeactivateOnExpire, g, (seed & 32) != 0);
            Record({Call(o, 0x00554A40, &CGoal::IsHorse, g), Call(o, 0x00554A50, &CGoal::IsFindGaps, g),
                    Call(o, 0x00554A70, &CGoal::RequiresCombo, g)});
            RecordGoals();
        }
        // The trick tokens.
        static const char* const kTexts[] = {"", "plain", "Do a \\t1 then \\k2!", "\\t12 and \\k10\\k3", "\\x\\t",
                                             "end \\", "\\tA\\kB\\t9", "%d \\t2 \\t3"};
        for (const char* text : kTexts) {
            char out[512];
            std::memset(out, 0x5A, sizeof(out));
            auto* keys = reinterpret_cast<CArray*>(g_keyArray);
            auto* tricks = reinterpret_cast<CArray*>(g_trickArray);
            if (o)
                Original<void(__cdecl*)(const char*, char*, CArray*, CArray*, int)>(0x00554890)(text, out, keys, tricks,
                                                                                                512);
            else
                CGoal::ReplaceTrickTokens(text, out, keys, tricks, 512);
            g_trace.push_back(HashBytes(reinterpret_cast<uint8_t*>(out), sizeof(out)));
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "goal basics: the original reached its calls");
    CompareTraces(traces[0], traces[1], "goal basics (0x00553E00-0x00554A7F): flags, timer, links, save data, tokens");
}

void RestoreGoalBasics() { Restore(); }

// --- The goal while it runs (0x00554A80-0x00555A5F) ----------------------------------------------------------------

alignas(16) uint8_t g_skater[0x400];
alignas(16) uint8_t g_object[0x100];
alignas(16) uint8_t g_endRun[0x20];
alignas(16) uint8_t g_scoreComponent[0x20];
alignas(16) uint8_t g_score[0x80];
alignas(16) uint8_t g_stateComponent[0x200];
alignas(16) uint8_t g_netManager[0x40];
alignas(16) uint8_t g_trickObjects[0x40];
alignas(16) uint8_t g_objectManager[0x40];
void* g_skaterVtable[8];
bool g_noSkater, g_noObject, g_inNetGame;
uint32_t g_players, g_modeName, g_endRunAnswers, g_trickAnswer;
CArray g_gapArray, g_waypointArray, g_clusterArray;
uint32_t g_gapElements[4], g_waypointElements[3], g_clusterElements[3];
Params g_waypoint[3], g_cluster[3];

void StandInRunCalls(StandIns& s)
{
    using EndRun = Obj::CSkaterEndRunComponent;
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return g_noSkater ? 0u : Address(g_skater);
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        switch (r.args[0]) {
        case 0xB1D5A3E1: return Address(g_endRun);
        case 0xD262802F: return Address(g_scoreComponent);
        case 0x43686585: return Address(g_stateComponent);
        default: return 0u;
        }
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, r.ecx});
        return Address(g_score);
    });
    struct Answer {
        uint32_t address;
        uint32_t ours;
        uint32_t bit;
    };
    for (Answer a : {Answer{0x00510F30, AddressOf(&EndRun::RunDone), 1}, Answer{0x00510F40, AddressOf(&EndRun::GoalRunDone), 2},
                     Answer{0x00510F50, AddressOf(&EndRun::EndOfRunStarted), 4}}) {
        uint32_t bit = a.bit;
        s.At({a.address, a.ours}, 0, [bit](Regs& r) {
            Record({0x0F30 + bit, r.ecx});
            return (g_endRunAnswers & bit) ? 1u : 0u;
        });
    }
    s.At({0x00510F10, AddressOf(&EndRun::EndGoalRun)}, 4, [](Regs& r) {
        Record({0x0F10, r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00510EA0, AddressOf(&EndRun::EndRun)}, 4, [](Regs& r) {
        Record({0x0EA0, r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00510F60, AddressOf(&EndRun::ClearEndOfRunStarted)}, 0, [](Regs& r) {
        Record({0x0F60, r.ecx});
        return 0u;
    });
    s.At({0x00510F70, AddressOf(&EndRun::ClearGoalEndOfRunStarted)}, 0, [](Regs& r) {
        Record({0x0F70, r.ecx});
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, r.ecx});
        return g_inNetGame ? 1u : 0u;
    });
    s.At({0x00539B00, AddressOf(&GameNet::Manager::CountPlayers)}, 0, [](Regs& r) {
        Record({0x9B00, r.ecx});
        return g_players;
    });
    s.At({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, [](Regs& r) {
        Record({0x32A0, r.ecx});
        return g_modeName;
    });
    s.At({0x00553150, AddressOf(&Game::CGameMode::ShouldTimerBeep)}, 0, [](Regs& r) {
        Record({0x3150, r.ecx});
        return g_modeName & 1;
    });
    s.At({0x005624C0, AddressOf(&Mdl::Score::CountTrickAfterTaps)}, 16, [](Regs& r) {
        Record({0x24C0, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return (r.args[0] % 7 == g_trickAnswer) ? 2u : 0u;
    });
    s.At({0x00588FF0, AddressOf(&Obj::CTrickObjectManager::SetObjectGraffitiColor)}, 8, [](Regs& r) {
        Record({0x8FF0, r.ecx, r.args[0], r.args[1]});
        return r.args[0] & 1;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        return g_noObject ? 0u : Address(g_object);
    });
    // The skater's member functions by name (its vtable +0x0C).
    for (void*& slot : g_skaterVtable)
        slot = s.Thunk(12, [](Regs& r) {
            Record({0x5C0C, r.ecx, r.args[0], r.args[1], r.args[2]});
            return 1u;
        });
}

void MakeArray(CArray& array, uint32_t* elements, uint32_t size, Script::ESymbolType type)
{
    std::memset(&array, 0, sizeof(array));
    array.m_type = type;
    array.m_size = size;
    if (size == 1)
        array.m_single = elements[0];
    else
        array.m_elements = elements;
}

void MakeRunState(uint32_t seed)
{
    MakeState(seed);
    for (uint8_t* buffer : {g_skater, g_object, g_endRun, g_scoreComponent, g_score, g_stateComponent})
        for (uint32_t k = 0; k < 0x20; ++k)
            buffer[k + 0x40] = static_cast<uint8_t>(Next());
    *reinterpret_cast<void***>(g_skater) = g_skaterVtable;
    *reinterpret_cast<uint8_t**>(g_scoreComponent + 0x18) = g_score;
    *reinterpret_cast<int*>(g_score + 0x14) = static_cast<int>(Next() % 3);
    *reinterpret_cast<float*>(g_score + 0x60) = (Next() % 4) * 0.5f;
    *reinterpret_cast<float*>(g_stateComponent + 0x11C) = static_cast<float>(Next() % 30000) + 0.75f;
    for (int i = 0; i < 3; ++i) {
        reinterpret_cast<float*>(g_skater + 0x4C)[i] = static_cast<float>(Next() % 2000) - 1000.5f;
        reinterpret_cast<float*>(g_object + 0x4C)[i] = static_cast<float>(Next() % 2000) - 1000.25f;
    }
    g_noSkater = Next() % 6 == 0;
    g_noObject = Next() % 5 == 0;
    g_inNetGame = Coin();
    g_players = Next() % 4 ? 2 : 0;
    static const uint32_t kModes[] = {0xEC200EAA, 0x1C471C60, 0x12345671, 0x22222222};
    g_modeName = kModes[Next() % 4];
    g_endRunAnswers = Next() % 8;
    g_trickAnswer = Next() % 7;
    // Gaps, waypoints, clusters.
    for (uint32_t& gap : g_gapElements)
        gap = 0x6A900000 + Next() % 6;
    MakeArray(g_gapArray, g_gapElements, 1 + Next() % 4, Script::ESYMBOLTYPE_NAME);
    for (int i = 0; i < 3; ++i) {
        uint32_t w = Address(g_waypoint[i].Struct());
        g_waypointElements[i] = w;
        g_getAnswers[{w, 0x2E0B1465}] = 0x11111111 + i * 0x11111111; // (flags 0x11111111, 0x22222222, 0x33333333)
        g_getAnswers[{w, 0x906B67BA}] = Next() % 90;
        uint32_t c = Address(g_cluster[i].Struct());
        g_clusterElements[i] = c;
        g_getAnswers[{c, 0x40C698AF}] = 0x0C100000 + Next() % 4;
        if (Coin())
            g_getAnswers[{c, 0xCD66C8AE}] = Next() % 5000;
        g_getAnswers[{c, 0x2E0B1465}] = 0xF1A90000 + i;
    }
    for (int i = 0; i < 4; ++i)
        g_getAnswers[{Address(g_params[i]), 0xCD66C8AE}] = Next() % 5000;
    MakeArray(g_waypointArray, g_waypointElements, 1 + Next() % 3, Script::ESYMBOLTYPE_STRUCTURE);
    MakeArray(g_clusterArray, g_clusterElements, 1 + Next() % 3, Script::ESYMBOLTYPE_STRUCTURE);
    static const char* const kTricks[] = {"", "Kickflip", "50-50", "Varial Heelflip"};
    for (int i = 0; i < 4; ++i) {
        uint32_t p = Address(g_params[i]);
        if (Coin())
            g_getAnswers[{p, 0x52D4489E}] = Address(&g_gapArray);
        else
            g_getAnswers.erase({p, 0x52D4489E});
        g_getAnswers[{p, 0x730844A3}] = Address(kTricks[Next() % 4]);
        g_getAnswers[{p, 0x3A54CC4B}] = Address(kTricks[1 + Next() % 3]);
        g_getAnswers[{p, 0x3D929B66}] = Coin() ? 0x255ED86F : 0x439F4704;
        if (Coin())
            g_getAnswers[{p, 0x160B3220}] = Address(&g_waypointArray);
        else
            g_getAnswers.erase({p, 0x160B3220});
        g_getAnswers[{p, 0x92C6E839}] = Address(&g_waypointArray);
        g_getAnswers[{p, 0x191F66E9}] = Address(&g_clusterArray);
        g_getAnswers[{p, 0x8E18F3B3}] = 0x0B1EC700;
        // (the scripts' names: checksum components of the same name)
        for (uint32_t name : {0xD4E0306Au, 0x79D8D4B6u, 0xB4E103FDu, 0xD34EB52Bu})
            if (Coin())
                g_getAnswers[{p, name}] = name ^ 0x5C5C5C5C;
    }
    // The goal flags for the waypoints: integers 1 for some.
    for (uint32_t flag : {0x11111111u, 0x22222222u, 0x33333333u}) {
        uint32_t flags = Address(g_flagsA.Struct());
        if (Coin())
            g_hasComponent.insert({flags, flag});
        g_getAnswers[{flags, flag}] = Next() % 3;
    }
    // Goal 0's parents: goal 1, then goal 2 (or none).
    g_links[4] = {Coin() ? 0x1001u : 0u, Coin() ? &g_links[5] : nullptr, false, {}};
    g_links[5] = {0x1002, nullptr, false, {}};
    Goal(0)->m_parents = Coin() ? &g_links[4] : nullptr;
    Goal(1)->m_parents = &g_links[5];
    Goal(1)->m_children = &g_links[6];
    g_links[6] = {0, nullptr, false, {}};
    Goal(2)->m_parents = &g_links[7];
    g_links[7] = {0, nullptr, false, {}};
    Goal(0)->m_endRunType = static_cast<int>(Next() % 3);
    // Times at the edges (out of time, the last seconds' beeps).
    static const int kTimes[] = {0, -5, 1, 9010, 15010, 10020, 5003, 120000};
    for (int i = 0; i < 2; ++i)
        if (Next() % 3)
            Goal(i)->m_timeLeft = kTimes[Next() % 8];
    for (int i = 0; i < 4; ++i)
        for (uint32_t flag : {0xFB11CB01u, 0x7100155Du, 0xF8E31760u, 0x8E6014F6u, 0x9D65D0E7u, 0x524566E3u})
            if (Coin())
                g_hasFlag.insert({Address(g_params[i]), flag});
    // Distances near whole feet (rounding the float), and exactly a record's.
    if (seed % 5 == 2) {
        static const float kSpots[][3] = {{14.4f, 3.0f, 19.2f}, {8.4f, -2.0f, 11.2f}, {28.8f, 0.0f, 38.4f},
                                          {12.0f, 7.0f, 0.0f}, {0.0f, 1.0f, 36.0f}};
        const float* spot = kSpots[Next() % 5];
        for (int k = 0; k < 3; ++k) {
            reinterpret_cast<float*>(g_object + 0x4C)[k] = 100.0f;
            reinterpret_cast<float*>(g_skater + 0x4C)[k] = 100.0f + spot[k];
        }
        for (int i = 0; i < 4; ++i)
            g_getAnswers[{Address(g_params[i]), 0x78FFB85C}] = Coin() ? 12u : 36u;
        if (Coin()) { // (just under 24 feet: 24 as a float)
            reinterpret_cast<float*>(g_object + 0x4C)[0] = 123.99999237060547f;
            reinterpret_cast<float*>(g_object + 0x4C)[2] = 100.01790618896484f;
            for (int k = 0; k < 3; ++k)
                reinterpret_cast<float*>(g_skater + 0x4C)[k] = 100.0f;
        }
    }
    // Kill scores equal to the entries'.
    for (int i = 0; i < 3; ++i)
        g_getAnswers[{Address(g_cluster[i].Struct()), 0xCD66C8AE}] = 100u * (Next() % 3);
    // (the goals the links made below and the walks reach)
    for (uint32_t id : {0x2000u, 0x2001u, 0x2002u, 0x2100u, 0x3000u, 0x3001u, 0x9999u})
        g_goalsById[id] = Address(Goal(3));
    ResetMemory();
}

void RecordRun()
{
    RecordGoals();
    for (uint32_t k = 0; k < g_heapUsed; k += 4) // (the links made)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + k));
}

void TestGoalRun()
{
    Setup();
    SaveGlobal(0x00684FC8, 4); // the slomo
    SaveGlobal(0x007CCE14, 4); // the network manager
    SaveGlobal(0x007CE500, 4); // the trick objects
    SaveGlobal(0x006F0FB8, 4); // the object manager
    auto* standIns = new StandIns;
    StandInGoalCalls(*standIns);
    StandInRunCalls(*standIns);
    GameVariable<uint8_t*>(0x007CCE14) = g_netManager;
    GameVariable<uint8_t*>(0x007CE500) = g_trickObjects;
    GameVariable<uint8_t*>(0x006F0FB8) = g_objectManager;
    g_flagsA = Params();
    g_flagsA.Integer(0x11111111, 1).Integer(0x22222222, 0).Integer(0x44444444, 2);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        for (uint32_t seed = 0; seed < 220; ++seed) {
            MakeRunState(seed);
            GameVariable<float>(0x00684FC8) = 0.5f + (seed % 5) * 0.75f;
            CGoal* g = Goal(seed % 7 == 3 ? 1 : 0);
            Record({0xEEEE0000 | seed});
            Record({Call(o, 0x00554A80, &CGoal::RunCallbackScript, g, seed & 1 ? 0xD4E0306Au : 0xB4E103FDu)});
            Call(o, 0x00554AC0, &CGoal::AddParent, Goal(2), 0x2000u + seed % 3);
            Call(o, 0x00554AC0, &CGoal::AddParent, Goal(2), 0x2100u);
            Call(o, 0x00554B40, &CGoal::AddChild, Goal(1), 0x3000u + seed % 2);
            Call(o, 0x00554B40, &CGoal::AddChild, Goal(1), 0x3000u);
            Call(o, 0x00554B40, &CGoal::AddChild, Goal(1), 0x3001u);
            RecordRun();
            if (seed % 3 == 0) {
                g_goalsById[0x1002] = Address(Goal(3)); // (so the walk ends)
                Call(o, 0x00554BD0, &CGoal::Uninitialize, Goal(0), (seed & 2) != 0, static_cast<int>(seed));
            }
            Record({Call(o, 0x00554C70, &CGoal::Lose, g), Call(o, 0x00554CD0, &CGoal::IsExpired, g),
                    Call(o, 0x00554D10, &CGoal::IsExpiredAtEndOfRun, g, (seed & 4) != 0),
                    Call(o, 0x00556230, &CGoal::ShouldExpire, g)});
            RecordRun();
            Record({Call(o, 0x00554DD0, &CGoal::Update, g)});
            Call(o, 0x00554F40, &CGoal::Expire, g);
            for (uint32_t gap : {0x6A900000u, 0x6A900001u, 0x6A900003u})
                Record({Call(o, 0x00554F90, &CGoal::GotGap, g, gap)});
            if (seed % 4 == 1) {
                Record({Call(o, 0x00555100, &CGoal::NextWaypoint, g)});
                for (int k = 0; k < 3; ++k)
                    g_trace.push_back(static_cast<uint32_t>(g_waypoint[k].count));
            }
            Call(o, 0x005552D0, &CGoal::KillCluster, g, 0x0C100000u + seed % 4, static_cast<int>(seed % 3) * 100);
            if (!g_noSkater)
                Record({Call(o, 0x005553C0, &CGoal::DidTrickString, g)});
            Record({Call(o, 0x00555540, &CGoal::GetRootGoalId, Goal(0)), Call(o, 0x00555590, &CGoal::HasWon, g)});
            Call(o, 0x005555E0, &CGoal::MarkWon, Goal(0));
            Call(o, 0x00555650, &CGoal::SetStageFlag, g, static_cast<int>(seed % 40));
            Call(o, 0x00555670, &CGoal::SetNetStageFlag, g, static_cast<int>(seed % 33));
            for (int bit : {0, 3, static_cast<int>(seed % 40)})
                Record({static_cast<uint32_t>(Call(o, 0x00555690, &CGoal::StageFlagSet, g, bit)),
                        static_cast<uint32_t>(Call(o, 0x005556B0, &CGoal::NetStageFlagSet, g, bit))});
            if (!g_noSkater) {
                Call(o, 0x005556D0, &CGoal::SetStartHeight, g);
                Record({Call(o, 0x00555710, &CGoal::CheckHeightRecord, g)});
            }
            Record({Call(o, 0x00555800, &CGoal::CheckDistanceRecord, g), Call(o, 0x00555930, &CGoal::UnBeat, g)});
            if (!g_noSkater) {
                Call(o, 0x00555970, &CGoal::EndOfRunDone, g, reinterpret_cast<Script::CScript*>(0x5C415C41));
                Record({Call(o, 0x005559E0, &CGoal::RunDone, g), Call(o, 0x00555A20, &CGoal::RunStarted, g)});
            }
            RecordRun();
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "goal run: the original reached its calls");
    CompareTraces(traces[0], traces[1], "goal run (0x00554A80-0x00555A5F): links, update, expiry, waypoints, records");
}

void RestoreGoalRun() { Restore(); }

// --- The goal's life and tricks (goal_life.cpp, goal_tricks.cpp) ------------------------------------------------

alignas(16) uint8_t g_profile[0x80];
alignas(16) uint8_t g_mapping[0x10];
alignas(16) uint8_t g_comboTexts[0x10];
alignas(16) uint8_t g_trickStruct[0x10];
alignas(16) uint8_t g_trickParams[0x10];
alignas(16) uint8_t g_catStruct[0x10];
alignas(16) uint8_t g_rewards[0x10];
alignas(16) uint8_t g_ownRewards[0x10];
alignas(16) uint8_t g_slotObject[0x10];
alignas(16) uint8_t g_playerInfos[2][0x40];
alignas(16) uint8_t g_clusterObject[0x40];
alignas(16) Mem::ListNode g_ring[3];
Params g_freeCombos, g_flagEntries[2];
CArray g_comboArray, g_catArray, g_flagArray, g_flagNames, g_rewardGoals, g_rewardPoints;
uint32_t g_comboElements[4], g_catElements[2], g_flagElements[3], g_flagNameElements[3], g_rewardGoalElements[3],
    g_rewardPointElements[3];
uint32_t g_specials[12][2];
int g_maxSpecials, g_difficulty, g_strings, g_playerIndex;
bool g_noCluster;

void StandInLifeCalls(StandIns& s)
{
    using Profile = Obj::CSkaterProfile;
    using Career = Obj::CSkaterCareer;
    s.At({0x0055EB40, AddressOf(&Game::CGoalPed::Construct)}, 4, [](Regs& r) {
        Record({0xEB40, r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00584130, AddressOf(&Career::GetLevel)}, 0, [](Regs& r) {
        Record({0x4130, r.ecx});
        return 7u;
    });
    s.At({0x005845C0, AddressOf(&Career::AddCash)}, 4, [](Regs& r) {
        Record({0x45C0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00584620, AddressOf(&Career::IncrementCounter)}, 0, [](Regs& r) {
        Record({0x4620, r.ecx});
        return 0u;
    });
    using GetSt = CStruct* (*)(uint32_t);
    using GetAr = CArray* (*)(uint32_t);
    using GetIn = int (*)(uint32_t);
    s.At({0x00479020, AddressOf(static_cast<GetSt>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        switch (r.args[0]) {
        case 0x8856C817: return Address(g_comboTexts);
        case 0x67F6E334: return Address(g_rewards);
        case 0x4764231D:
        case 0xF481D0CD:
        case 0xD72D5CF7:
        case 0xA1B2D162: return Address(g_freeCombos.Struct());
        default: return Address(g_trickStruct);
        }
    });
    s.At({0x00479070, AddressOf(static_cast<GetAr>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return Address(&g_catArray);
    });
    s.At({0x00478F60, AddressOf(static_cast<GetIn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 4u;
    });
    s.At({0x004763A0, AddressOf(&Script::CreateString)}, 0, [](Regs& r) {
        Record({0x63A0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xC5000000u + g_strings++;
    });
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0]});
        return Address(g_profile);
    });
    s.At({0x0057B630, AddressOf(&Obj::CPlayerProfileManager::GetCurrentProfile)}, 0, [](Regs& r) {
        Record({0xB630, r.ecx});
        return Address(g_profile);
    });
    s.At({0x005860A0, AddressOf(&Profile::GetMaxSpecials)}, 0, [](Regs& r) {
        Record({0x60A0, r.ecx});
        return static_cast<uint32_t>(g_maxSpecials);
    });
    s.At({0x00586630, AddressOf(&Profile::GetSpecialTrick)}, 8, [](Regs& r) {
        Record({0x6630, r.ecx, r.args[1]});
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        out[0] = g_specials[r.args[1] % 12][0];
        out[1] = g_specials[r.args[1] % 12][1];
        return r.args[0];
    });
    s.At({0x00586710, AddressOf(&Profile::SetSpecialTrick)}, 12, [](Regs& r) {
        auto* trick = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x6710, r.ecx, r.args[0], trick[0], trick[1], trick[2] & 0xFF, r.args[2] & 0xFF});
        return 1u;
    });
    s.At({0x00586060, AddressOf(&Profile::AddMaxSpecials)}, 4, [](Regs& r) {
        Record({0x6060, r.ecx, r.args[0]});
        return 1u;
    });
    s.At({0x00586110, AddressOf(&Profile::GetInfoStructure)}, 4, [](Regs& r) {
        Record({0x6110, r.ecx, r.args[0]});
        return Address(g_mapping);
    });
    s.At({0x00564A10, AddressOf(&Mdl::Score::Reset)}, 0, [](Regs& r) {
        Record({0x4A10, r.ecx});
        return 0u;
    });
    s.At({0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, [](Regs& r) {
        Record({0x1B40, r.ecx});
        return 4321u;
    });
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0x72C0, r.ecx, r.args[1] & 0xFF, *reinterpret_cast<uint32_t*>(r.args[0])});
        g_playerIndex = 0;
        return Address(g_playerInfos[0]);
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Record({0x7010, r.ecx, r.args[1] & 0xFF});
        return ++g_playerIndex < 2 ? Address(g_playerInfos[g_playerIndex]) : 0u;
    });
    s.At({0x00559050, AddressOf(&Game::CGoalManager::GetDifficultyLevel)}, 4, [](Regs& r) {
        Record({0x9050, r.ecx, r.args[0] & 0xFF});
        return static_cast<uint32_t>(g_difficulty);
    });
    s.At({0x00557E40, AddressOf(&Game::CGoalManager::ShowGoalPoints)}, 0, [](Regs& r) {
        Record({0x7E40, r.ecx});
        return 0u;
    });
    struct ManagerCall {
        uint32_t address;
        uint32_t ours;
    };
    for (ManagerCall call : {ManagerCall{0x005588D0u, AddressOf(&Game::CGoalManager::UnlockGoal)},
                             ManagerCall{0x00558030u, AddressOf(&Game::CGoalManager::WinGoal)},
                             ManagerCall{0x0055DBE0u, AddressOf(&Game::CGoalManager::AddActiveGoal)}}) {
        uint32_t address = call.address;
        s.At({address, call.ours}, 4, [address](Regs& r) {
            Record({address, r.ecx, r.args[0]});
            return 0u;
        });
    }
    s.At({0x00557FC0, AddressOf(&Game::CGoalManager::ActivateGoal)}, 8, [](Regs& r) {
        Record({0x7FC0, r.ecx, r.args[0], r.args[1] & 0xFF});
        return 1u;
    });
    s.At({0x005889E0, AddressOf(&Obj::CTrickObjectManager::GetCluster)}, 4, [](Regs& r) {
        Record({0x89E0, r.ecx, r.args[0]});
        return g_noCluster && (r.args[0] & 1) ? 0u : Address(g_clusterObject);
    });
    s.At({0x00588D20, AddressOf(&Obj::CTrickObjectCluster::SetGraffitiColor)}, 4, [](Regs& r) {
        Record({0x8D20, r.ecx, r.args[0]});
        return 1u;
    });
    s.At({0x00588F40, AddressOf(&Obj::CTrickObjectCluster::ClearGraffitiColor)}, 4, [](Regs& r) {
        Record({0x8F40, r.ecx, r.args[0]});
        return 1u;
    });
}

void MakeLifeState(uint32_t seed)
{
    MakeRunState(seed);
    g_strings = 0;
    g_maxSpecials = static_cast<int>(Next() % 5);
    g_difficulty = static_cast<int>(Next() % 4) - 1;
    g_noCluster = Coin();
    static const uint32_t kCombos[] = {0xC0000001, 0xC0000002, 0xC0000003, 0xC0000004, 0xC0000005};
    for (auto& special : g_specials) {
        special[0] = Next() % 5 ? 0x7A000000 + Next() % 4 : 0; // the trick (sometimes none)
        special[1] = kCombos[Next() % 5];     // its keys
    }
    for (uint32_t& combo : g_comboElements)
        combo = kCombos[Next() % 5];
    MakeArray(g_comboArray, g_comboElements, 1 + Next() % 4, Script::ESYMBOLTYPE_NAME);
    for (uint32_t& cat : g_catElements)
        cat = Address(g_catStruct);
    MakeArray(g_catArray, g_catElements, 2, Script::ESYMBOLTYPE_STRUCTURE);
    // The skater's trick mapping: combos to tricks or to slots; the combos' texts; the tricks' names.
    uint32_t mapping = Address(g_mapping);
    for (uint32_t combo : kCombos) {
        uint32_t way = Next() % 3;
        g_typedAnswers[{mapping, combo}].clear();
        if (way == 0)
            g_typedAnswers[{mapping, combo}][0x6950] = 0x7A000000 + Next() % 4; // (a checksum: the trick)
        g_getAnswers[{Address(g_comboTexts), combo}] = Address("Up + Square");
    }
    // (integers for slots: the stand-in answers both getters from one table, so a slot is a small number)
    if (Coin())
        g_typedAnswers[{mapping, kCombos[4]}][0x6630] = 2;
    *reinterpret_cast<uint8_t**>(g_skater + 0x17C + 2 * 4) = g_slotObject;
    *reinterpret_cast<uint8_t**>(g_skater + 0x17C + 0 * 4) = g_slotObject;
    *reinterpret_cast<uint8_t**>(g_slotObject + 4) = g_trickParams;
    g_getAnswers[{Address(g_trickParams), 0xA1DC81F9}] = Address("Slot Trick");
    g_getAnswers[{Address(g_catStruct), 0xA1DC81F9}] = Address("Premade Cat");
    g_getAnswers[{Address(g_trickStruct), 0x7031F10C}] = Address(g_trickParams);
    // Free key combos of a trick type.
    g_freeCombos = Params();
    g_freeCombos.Flag(kCombos[Next() % 5]).Flag(kCombos[4]);
    // Rewards.
    for (int i = 0; i < 4; ++i) {
        uint32_t p = Address(g_params[i]);
        if (Coin())
            g_getAnswers[{p, 0x79704516}] = Address(&g_comboArray);
        else
            g_getAnswers.erase({p, 0x79704516});
        for (uint32_t text : {0x0C5D7E6Bu, 0x4BC5229Du, 0x63981D6Fu, 0xDA441D9Au, 0x1B3B4DD8u, 0x01F238B1u,
                              0x89E9C617u, 0x988330D3u, 0xB0D7628Fu, 0xE0206064u}) {
            if (Coin())
                g_getAnswers[{p, text}] = Address("Do a \\t1 with \\k1");
            else
                g_getAnswers.erase({p, text});
            if (Next() % 4 == 0)
                g_hasComponent.insert({p, text});
        }
        g_getAnswers[{p, 0x0B502200}] = Next() % 2;   // premade_cat_index
        g_getAnswers[{p, 0xBC678826}] = Next() % 3;   // should_remove_trick
        g_getAnswers[{p, 0xEF6FB249}] = 0x7A000000 + Next() % 4; // trick_name
        g_getAnswers[{p, 0x3D929B66}] = std::vector<uint32_t>{0x61A1BC57, 0x439F4704, 0x255ED86F, 0xA549B57B,
                                                              0xEF24413B}[Next() % 5];
        g_getAnswers[{p, 0x9982E501}] = 0x60A10000 + i;
        g_getAnswers.erase({p, 0x0232844A});
        g_getAnswers.erase({p, 0xBAFFB052});
        g_typedAnswers[{p, 0x0232844A}].clear(); // Reward_Goal: one goal or an array
        if (Coin()) {
            if (Coin())
                g_typedAnswers[{p, 0x0232844A}][0x6950] = 0x60A10007;
            else
                g_typedAnswers[{p, 0x0232844A}][0x6860] = Address(&g_rewardGoals);
        }
        g_typedAnswers[{p, 0xBAFFB052}].clear(); // reward_points: a number or by difficulty
        if (Coin()) {
            if (Coin())
                g_typedAnswers[{p, 0xBAFFB052}][0x6630] = 250;
            else
                g_typedAnswers[{p, 0xBAFFB052}][0x6860] = Address(&g_rewardPoints);
        }
        if (Coin())
            g_hasComponent.insert({p, 0x79BBFFCB}); // dont_deactivate_upon_expiration
        if (Coin())
            g_getAnswers[{p, 0xCC3C4CC4}] = Coin() ? Address(&g_flagArray) : Address(&g_flagNames); // goal_flags
        else
            g_getAnswers.erase({p, 0xCC3C4CC4});
        for (uint32_t flag : {0x2D4C5829u, 0x4599B58Cu, 0x54D3CAC1u, 0xC309CAD1u, 0xC63432A7u, 0xB6432D23u,
                              0xEFC35592u, 0xD76C173Eu, 0x5BC07EAAu, 0x8A3A324Eu, 0x04AB5F14u, 0xC6B4258Fu,
                              0x4E863E93u, 0xDE569045u})
            if (Coin())
                g_hasFlag.insert({p, flag});
        if (Coin())
            g_getAnswers[{p, 0xDE569045}] = Next() % 4;
        if (Coin())
            g_getAnswers[{p, 0xFC9630AB}] = Next() % 3; // custom_score_record
        Goal(i % 2)->m_bestScore = static_cast<int>(Next() % 3);
    }
    for (uint32_t& goal : g_rewardGoalElements)
        goal = Coin() ? 0x60A10000 + Next() % 4 : 0;
    MakeArray(g_rewardGoals, g_rewardGoalElements, 1 + Next() % 3, Script::ESYMBOLTYPE_NAME);
    for (uint32_t& points : g_rewardPointElements)
        points = Next() % 500;
    MakeArray(g_rewardPoints, g_rewardPointElements, 1 + Next() % 3, Script::ESYMBOLTYPE_INTEGER);
    if (Coin())
        g_getAnswers[{Address(g_rewards), 0x60A10000}] = Address(g_ownRewards);
    else
        g_getAnswers.erase({Address(g_rewards), 0x60A10000});
    if (Coin())
        g_hasFlag.insert({Address(g_ownRewards), 0xDE569045});
    // The goal flags: names, or one structure per skater.
    for (uint32_t& name : g_flagNameElements)
        name = 0xF1A60000 + Next() % 8;
    MakeArray(g_flagNames, g_flagNameElements, 1 + Next() % 3, Script::ESYMBOLTYPE_NAME);
    for (int i = 0; i < 2; ++i) {
        g_flagElements[i] = Address(g_flagEntries[i].Struct());
        g_getAnswers[{g_flagElements[i], 0x5B8AB877}] = 0x5CA7E000 + i;
        g_getAnswers[{g_flagElements[i], 0xCC3C4CC4}] = Address(&g_flagNames);
    }
    MakeArray(g_flagArray, g_flagElements, 2, Script::ESYMBOLTYPE_STRUCTURE);
    // The goal's node in a ring of three.
    for (int i = 0; i < 3; ++i) {
        g_ring[i].next = i == 2 ? &Goal(0)->m_node : &g_ring[i + 1];
        g_ring[i].prev = i == 0 ? &Goal(0)->m_node : &g_ring[i - 1];
    }
    Goal(0)->m_node.next = &g_ring[0];
    Goal(0)->m_node.prev = &g_ring[2];
    for (int i = 0; i < 2; ++i)
        *reinterpret_cast<uint8_t**>(g_playerInfos[i] + 0x14) = g_skater;
    g_skater[0x320] = static_cast<uint8_t>(Next() % 2);
    *reinterpret_cast<uint32_t*>(g_skater + 0x104) = Next() % 8;
    for (int i = 1; i < 4; ++i)
        Goal(i)->m_node.next = Goal(i)->m_node.prev = &Goal(i)->m_node;
    // (goal 2 in a list with goal 3)
    Goal(2)->m_node.next = Goal(2)->m_node.prev = &Goal(3)->m_node;
    Goal(3)->m_node.next = Goal(3)->m_node.prev = &Goal(2)->m_node;
    // (a made goal always has its links' first entries)
    static GoalLink s_noLinks[4];
    std::memset(s_noLinks, 0, sizeof(s_noLinks));
    for (int i = 0; i < 2; ++i) {
        if (!Goal(i)->m_parents)
            Goal(i)->m_parents = &s_noLinks[i * 2];
        if (!Goal(i)->m_children)
            Goal(i)->m_children = &s_noLinks[i * 2 + 1];
    }
    // (a skater's flags structure for SetFlag's sub-structure calls)
    for (int i = 0; i < 2; ++i)
        Goal(i)->m_goalFlags = g_flagsA.Struct();
    g_getAnswers[{Address(g_flagsA.Struct()), 0x33333333}] = Address(g_skaterB.Struct());
    g_hasComponent.insert({Address(g_skaterB.Struct()), 0x11111111});
    g_getAnswers[{Address(g_skaterB.Struct()), 0x11111111}] = Next() % 2;
    g_getAnswers[{Address(g_skaterB.Struct()), 0xCF6CD3C1}] = Next() % 3;
    g_modeProperties[0x1DED1EA4] = Next() % 2;
    g_modeProperties[0x5A2C589A] = Next() % 2;
    g_noSkater = false;
}

void TestGoalLife()
{
    Setup();
    SaveGlobal(0x00684FC8, 4);
    SaveGlobal(0x007CCE14, 4);
    SaveGlobal(0x007CE500, 4);
    SaveGlobal(0x006F0FB8, 4);
    SaveGlobal(0x007CE4B4, 4);
    SaveGlobal(0x00690FC8, 24); // (the random streams)
    auto* standIns = new StandIns;
    StandInGoalCalls(*standIns);
    StandInRunCalls(*standIns);
    StandInLifeCalls(*standIns);
    StandInGameModeIsTrue(*standIns);
    GameVariable<uint8_t*>(0x007CCE14) = g_netManager;
    GameVariable<uint8_t*>(0x007CE500) = g_trickObjects;
    GameVariable<uint8_t*>(0x006F0FB8) = g_objectManager;
    GameVariable<uint8_t*>(0x007CE4B4) = g_objectManager; // (the profile manager: its calls stood in)
    g_flagsA = Params();
    g_flagsA.Integer(0x11111111, 1).Integer(0x22222222, 0).Integer(0x44444444, 2);
    g_flagEntries[0] = Params();
    g_flagEntries[1] = Params();

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        Mth::InitialRand(0x600D);
        for (uint32_t seed = 0; seed < 240; ++seed) {
            MakeLifeState(seed);
            CGoal* g = Goal(seed % 9 == 4 ? 1 : 0);
            Record({0xEEEE0000 | seed});
            *reinterpret_cast<uint8_t**>(g_skate + 0x20) = g_career;
            // Made and destroyed (a fresh goal over the fourth's memory).
            if (seed % 6 == 0) {
                CGoal* made = Goal(3);
                using Make = CGoal*(__thiscall*)(CGoal*, CStruct*);
                Record({Address(o ? Original<Make>(0x00556FE0)(made, reinterpret_cast<CStruct*>(g_params[1]))
                                  : made->Construct(reinterpret_cast<CStruct*>(g_params[1])))});
                RecordRun();
                made->m_children->m_next = nullptr; // (fresh links)
                Record({Address(Call(o, 0x005566A0, &CGoal::Destroy, made, static_cast<uint8_t>((seed / 6) & 1)))});
                RecordRun();
            }
            Call(o, 0x00556F90, &CGoal::AppendDifficultyLevelParams, g);
            Call(o, 0x00557270, &CGoal::Init, g);
            RecordRun();
            Record({Call(o, 0x00557360, &CGoal::Activate, g)});
            RecordRun();
            for (uint32_t flag : {0x11111111u, 0x22222222u, 0x77777777u})
                Record({Call(o, 0x00556330, &CGoal::SetFlag, g, flag, static_cast<int>(seed % 3), 0u),
                        Call(o, 0x00556330, &CGoal::SetFlag, g, flag, static_cast<int>(seed / 3 % 2), 0x33333333u)});
            RecordRun();
            if (seed % 3 == 0) {
                Record({Call(o, 0x00557560, &CGoal::Deactivate, g, (seed & 2) != 0, (seed & 4) != 0)});
            } else {
                Record({Call(o, 0x005576D0, &CGoal::Win, g)});
                for (int k = 0; k < 3; ++k)
                    g_trace.push_back(Address(g_ring[k].next));
            }
            RecordRun();
            Call(o, 0x00557A40, &CGoal::Unlock, g);
            Record({static_cast<uint32_t>(Call(o, 0x00556050, &CGoal::GetRewardPoints, g))});
            Call(o, 0x00555DF0, &CGoal::GetRewardsStructure, g, reinterpret_cast<CStruct*>(g_saveData));
            Call(o, 0x00555FB0, &CGoal::UnlockRewardGoals, g);
            Call(o, 0x005566D0, &CGoal::CreateGoalFlags, g, reinterpret_cast<CStruct*>(g_params[seed % 4]));
            Record({Call(o, 0x00556240, &CGoal::AllSkatersRunDone, g)});
            // Tricks.
            Call(o, 0x005565C0, &CGoal::NoValidKeyCombos, g);
            Record({static_cast<uint32_t>(Call(o, 0x005568B0, &CGoal::PickKeyCombo, g, &g_comboArray))});
            Record({Call(o, 0x00556A00, &CGoal::ReplaceTrickText, g)});
            Call(o, 0x00555A60, &CGoal::RemoveRewardTrick, g);
            Call(o, 0x00555D40, &CGoal::ColorTrickObjects, g, static_cast<int>(seed % 4), (seed & 8) != 0);
            if (g_getAnswers.count({Address(g_params[seed % 9 == 4 ? 1 : 0]), 0x3D929B66}))
                Record({Call(o, 0x00557AC0, &CGoal::AwardSpecialTrick, g)});
            Record({Address(Call(o, 0x005560D0, &CGoal::DestroyNode, Goal(2), static_cast<uint8_t>(seed & 1)))});
            RecordRun();
        }
        // The trick texts on their own.
        for (int k = 0; k < 4; ++k) {
            CArray keyTexts{}, trickTexts{};
            using Build = bool(__cdecl*)(CArray*, CArray*, CArray*, int);
            Record({o ? Original<Build>(0x00555B50)(&g_comboArray, &keyTexts, &trickTexts, k - 2)
                      : CGoal::BuildTrickTexts(&g_comboArray, &keyTexts, &trickTexts, k - 2)});
        }
        g_trace.insert(g_trace.end(), {GameVariable<uint32_t>(0x00690FD4), GameVariable<uint32_t>(0x00690FDC)});
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 30000, "goal life: the original reached its calls");
    CompareTraces(traces[0], traces[1], "goal life and tricks (CGoal: made, started, won, flags, rewards, tricks)");
}

void RestoreGoalLife() { Restore(); }

// --- The goal types of 0x005517C0-0x00552E0F (goal_types.cpp) ----------------------------------------------------

alignas(16) uint8_t g_competitionObject[0x200];
alignas(16) uint8_t g_target[0x100];
alignas(16) uint8_t g_modelComponent[0x210];
alignas(16) uint8_t g_camera[0x40];
alignas(16) uint8_t g_types[3][0x90];
int g_place, g_hidden, g_gapCount;
bool g_noTarget;

void StandInTypeCalls(StandIns& s)
{
    // (the skater's components again, with the target's model)
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        switch (r.args[0]) {
        case 0xB1D5A3E1: return Address(g_endRun);
        case 0xD262802F: return Address(g_scoreComponent);
        case 0x43686585: return Address(g_stateComponent);
        case 0x286A8D26: return Address(g_modelComponent);
        default: return 0u;
        }
    });
    s.At({0x005501A0, AddressOf(&Mdl::CCompetition::PlaceIs)}, 4, [](Regs& r) {
        Record({0x01A0, r.ecx, r.args[0]});
        return static_cast<int>(r.args[0]) == g_place ? 1u : 0u;
    });
    using GetIntName = bool (CStruct::*)(const char*, int*, int);
    s.At({0x00476680, AddressOf(static_cast<GetIntName>(&CStruct::GetInteger))}, 12, [](Regs& r) {
        uint32_t name = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]));
        Record({0x6630, r.ecx, name, r.args[2] & 0xFF});
        Regs copy = r;
        copy.args[0] = name;
        return Answer(copy, 0x6630) ? 1u : 0u;
    });
    using AddSumName = void (CStruct::*)(const char*, uint32_t);
    using AddStName = void (CStruct::*)(const char*, CStruct*);
    using RemoveName = void (CStruct::*)(const char*);
    s.At({0x00477DF0, AddressOf(static_cast<AddSumName>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7DF0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00478770, AddressOf(static_cast<AddStName>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8770, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00477770, AddressOf(static_cast<RemoveName>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x7770, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return g_noTarget ? 0u : Address(g_target);
    });
    s.At({0x00492F30, AddressOf(&Nx::CModel::GetBoundingSphere)}, 4, [](Regs& r) {
        Record({0x2F30, r.ecx});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        out[0] = 1.5f;
        out[1] = -2.25f;
        out[2] = 3.0f;
        out[3] = 4.0f;
        return r.args[0];
    });
    s.At({0x0048C490, AddressOf(&Nx::Engine::SphereVisibleInOneViewport)}, 0, [](Regs& r) {
        const float* p = reinterpret_cast<const float*>(r.args[0]);
        Record({0xC490, CFuncTests::Bits(p[0]), CFuncTests::Bits(p[1]), CFuncTests::Bits(p[2]), CFuncTests::Bits(p[3]), r.args[1]});
        return static_cast<uint32_t>(g_hidden);
    });
    s.At({0x0057FEB0, AddressOf(&Obj::CSkater::GetCamera)}, 0, [](Regs& r) {
        Record({0xFEB0, r.ecx});
        return Address(g_camera);
    });
    s.At({0x00485A00, AddressOf(&Gfx::Camera::GetPos)}, 0, [](Regs& r) {
        Record({0x5A00, r.ecx});
        return Address(g_camera + 0x10);
    });
    s.At({0x00562740, AddressOf(&Mdl::Score::IsGap)}, 4, [](Regs& r) {
        Record({0x2740, r.ecx, r.args[0]});
        return (r.args[0] * 7 + static_cast<uint32_t>(g_gapCount)) % 3 ? 1u : 0u;
    });
    s.At({0x00561660, AddressOf(&Mdl::Score::GetTrickName)}, 4, [](Regs& r) {
        Record({0x1660, r.ecx, r.args[0]});
        return 0x6A900000u + r.args[0] % 5;
    });
}

void TestGoalTypes()
{
    Setup();
    for (uint32_t global : {0x00684FC8u, 0x007CCE14u, 0x007CE500u, 0x006F0FB8u, 0x007CE4B4u})
        SaveGlobal(global, 4);
    auto* standIns = new StandIns;
    StandInGoalCalls(*standIns);
    StandInRunCalls(*standIns);
    StandInLifeCalls(*standIns);
    StandInGameModeIsTrue(*standIns);
    StandInTypeCalls(*standIns);
    GameVariable<uint8_t*>(0x007CCE14) = g_netManager;
    GameVariable<uint8_t*>(0x007CE500) = g_trickObjects;
    GameVariable<uint8_t*>(0x006F0FB8) = g_objectManager;
    GameVariable<uint8_t*>(0x007CE4B4) = g_objectManager;
    g_flagsA = Params();
    g_flagsA.Integer(0x11111111, 1).Integer(0x22222222, 0).Integer(0x44444444, 2);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        *reinterpret_cast<uint8_t**>(g_skate + 0x360) = g_competitionObject;
        for (uint32_t seed = 0; seed < 150; ++seed) {
            MakeLifeState(seed);
            *reinterpret_cast<uint8_t**>(g_skate + 0x20) = g_career;
            *reinterpret_cast<uint8_t**>(g_skate + 0x360) = g_competitionObject;
            g_place = static_cast<int>(Next() % 5);
            g_hidden = static_cast<int>(Next() % 3 == 0);
            g_noTarget = Next() % 5 == 0;
            g_gapCount = static_cast<int>(Next() % 4);
            *reinterpret_cast<int*>(g_score + 0x5C) = g_gapCount;
            *reinterpret_cast<void***>(g_target) = g_skaterVtable;
            for (int i = 0; i < 24; ++i) // (the position and the whole matrix)
                reinterpret_cast<float*>(g_target + 0x4C)[i] = static_cast<float>(Next() % 200) * 0.25f - 20.0f;
            if (seed % 7 == 3) { // (the camera exactly max_distance_to_target away)
                for (int i = 0; i < 4; ++i)
                    reinterpret_cast<float*>(g_target + 0x4C)[i] = 0.0f;
                reinterpret_cast<float*>(g_camera + 0x10)[0] = 30.0f;
                reinterpret_cast<float*>(g_camera + 0x10)[1] = 0.0f;
                reinterpret_cast<float*>(g_camera + 0x10)[2] = 40.0f;
            }
            if (seed % 7 != 3)
                for (int i = 0; i < 4; ++i)
                    reinterpret_cast<float*>(g_camera + 0x10)[i] = static_cast<float>(Next() % 300) * 0.5f - 60.0f;
            *reinterpret_cast<uint8_t**>(g_modelComponent + 0x208) = g_modelComponent;
            for (int i = 0; i < 4; ++i) {
                uint32_t p = Address(g_params[i]);
                g_getAnswers[{p, 0x856FAB92}] = seed % 7 == 3 ? 50 : Next() % 150; // max_distance_to_target
                g_getAnswers[{p, 0xD262ED09}] = 0x7A26E700;    // film_target
                if (Coin())
                    g_getAnswers[{p, 0x184E30FD}] = Next() % 20; // total_time_required
                else
                    g_getAnswers.erase({p, 0x184E30FD});
                g_getAnswers[{p, 0xC808F9FB}] = Next() % 4;
                g_getAnswers[{p, 0x735AF2DD}] = Address(g_skaterA.Struct()); // (the gaps found)
                g_getAnswers[{p, 0xF1E59083}] = Next() % 5;
                g_getAnswers[{p, 0xE75C4BBF}] = 3;
            }
            for (uint32_t gap = 0; gap < 5; ++gap)
                if (Coin())
                    g_hasComponent.insert({Address(g_skaterA.Struct()), 0x6A900000u + gap});
            for (const char* name : {"gotBronze", "gotSilver", "gotGold", "reward_given", "win_record"})
                if (Next() % 3 || (std::strcmp(name, "win_record") && std::strcmp(name, "gotBronze"))) // (the others' locals: unset without them)
                    g_getAnswers[{Address(g_saveData), Crc::GenerateCRCFromString(name)}] = Next() % 3;
                else
                    g_getAnswers.erase({Address(g_saveData), Crc::GenerateCRCFromString(name)});
            Record({0xEEEE0000 | seed});
            // A competition goal (made over goal 2's memory, its links fresh).
            auto* comp = reinterpret_cast<Game::CCompetitionGoal*>(g_types[0]);
            std::memcpy(g_types[0], Goal(0), 0x70);
            for (uint32_t k = 0x70; k < 0x78; ++k)
                g_types[0][k] = static_cast<uint8_t>(Next());
            if (seed % 10 == 0) {
                using Make = Game::CCompetitionGoal*(__thiscall*)(void*, CStruct*);
                Record({Address(o ? Original<Make>(0x005517C0)(comp, reinterpret_cast<CStruct*>(g_params[1]))
                                  : comp->Construct(reinterpret_cast<CStruct*>(g_params[1])))});
                for (uint32_t k = 0; k < 0x78; k += 4)
                    g_trace.push_back(*reinterpret_cast<uint32_t*>(g_types[0] + k));
                comp->m_children->m_next = nullptr;
                comp->m_parents->m_next = nullptr;
                Record({Address(o ? Original<Game::CCompetitionGoal*(__thiscall*)(void*, uint8_t)>(0x00551A00)(comp, seed & 32 ? 1 : 0)
                                  : comp->Destroy(seed & 32 ? 1 : 0))});
                std::memcpy(g_types[0], Goal(0), 0x70);
            }
            comp->m_node.vtable = g_goalVtable;
            auto compCall = [&](uint32_t address, auto ours, auto... args) {
                using R = decltype((comp->*ours)(args...));
                return o ? Original<R(__thiscall*)(void*, decltype(args)...)>(address)(comp, args...) : (comp->*ours)(args...);
            };
            Record({compCall(0x00551810, &Game::CCompetitionGoal::Expire),
                    static_cast<uint32_t>(compCall(0x005518A0, &Game::CCompetitionGoal::IsPaused))});
            if (seed & 1)
                compCall(0x00551820, &Game::CCompetitionGoal::SetActive);
            else
                compCall(0x00551830, &Game::CCompetitionGoal::SetInactive);
            if (seed & 2)
                compCall(0x00551880, &Game::CCompetitionGoal::PauseCompetition);
            if (seed & 4)
                compCall(0x00551890, &Game::CCompetitionGoal::UnPauseCompetition);
            Record({compCall(0x00551840, &Game::CCompetitionGoal::HasWon),
                    static_cast<uint32_t>(compCall(0x005518A0, &Game::CCompetitionGoal::IsPaused))});
            compCall(0x005518C0, &Game::CCompetitionGoal::LoadSaveData, reinterpret_cast<CStruct*>(g_saveData));
            compCall(0x00551970, &Game::CCompetitionGoal::GetSaveData, reinterpret_cast<CStruct*>(g_saveData));
            compCall(0x00551A30, &Game::CCompetitionGoal::MarkWon);
            Record({compCall(0x00551A90, &Game::CCompetitionGoal::Win), compCall(0x00551850, &Game::CCompetitionGoal::UnBeat)});
            for (uint32_t k = 0; k < 0x78; k += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(g_types[0] + k));
            // A film goal.
            auto* film = reinterpret_cast<Game::CFilmGoal*>(g_types[1]);
            std::memcpy(g_types[1], Goal(0), 0x70);
            for (uint32_t k = 0x70; k < 0x88; ++k)
                g_types[1][k] = static_cast<uint8_t>(Next());
            film->m_filming = Coin();
            film->m_timeOnCamera = Next() % 4000;
            film->m_timeNeeded = Next() % 4000;
            film->m_checkpoints = static_cast<int>(Next() % 3);
            film->m_checkpointsPassed = static_cast<int>(Next() % 4);
            if (Coin())
                film->m_timeOnCamera = film->m_timeNeeded; // (exactly enough)
            static const int kFilmTimes[] = {-5, 0, 100, 3000, 90000};
            film->m_timeLeft = kFilmTimes[Next() % 5];
            if (seed % 5 == 1) { // (filming, short of time already: a negative time left)
                film->m_filming = true;
                film->m_timeOnCamera = 0;
                film->m_timeNeeded = 1000;
                film->m_timeLeft = -5;
                g_virtualAnswers[CGoal::kVtIsActive / 4] = 1;
                g_virtualAnswers[CGoal::kVtIsPaused / 4] = 0;
            }
            film->m_node.vtable = g_goalVtable;
            auto filmCall = [&](uint32_t address, auto ours, auto... args) {
                using R = decltype((film->*ours)(args...));
                return o ? Original<R(__thiscall*)(void*, decltype(args)...)>(address)(film, args...) : (film->*ours)(args...);
            };
            Record({filmCall(0x005528D0, &Game::CFilmGoal::TargetInView), filmCall(0x00552A10, &Game::CFilmGoal::Update)});
            filmCall(0x00552AF0, &Game::CFilmGoal::Checkpoint);
            if (seed % 3 == 0)
                Record({filmCall(0x00552800, &Game::CFilmGoal::Activate)});
            for (uint32_t k = 0; k < 0x88; k += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(g_types[1] + k));
            // A find gaps goal.
            auto* gaps = reinterpret_cast<Game::CFindGapsGoal*>(g_types[2]);
            std::memcpy(g_types[2], Goal(0), 0x70);
            gaps->m_node.vtable = g_goalVtable;
            if (o)
                Original<void(__thiscall*)(void*)>(0x00552B90)(gaps);
            else
                gaps->CheckGaps();
            if (seed % 4 == 0)
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00552D20)(gaps) : gaps->Activate()});
            RecordRun();
        }
        // The constructors and destructors of film and find gaps.
        for (int k = 0; k < 2; ++k) {
            std::memset(g_types[1], 0x3C, sizeof(g_types[1]));
            using MakeFilm = Game::CFilmGoal*(__thiscall*)(void*, CStruct*);
            auto* film = reinterpret_cast<Game::CFilmGoal*>(g_types[1]);
            Record({Address(o ? Original<MakeFilm>(0x005527D0)(film, reinterpret_cast<CStruct*>(g_params[2]))
                              : film->Construct(reinterpret_cast<CStruct*>(g_params[2])))});
            film->m_children->m_next = nullptr;
            film->m_parents->m_next = nullptr;
            Record({Address(o ? Original<Game::CFilmGoal*(__thiscall*)(void*, uint8_t)>(0x005528A0)(film, static_cast<uint8_t>(k))
                              : film->Destroy(static_cast<uint8_t>(k)))});
            auto* gaps = reinterpret_cast<Game::CFindGapsGoal*>(g_types[2]);
            using MakeGaps = Game::CFindGapsGoal*(__thiscall*)(void*, CStruct*);
            Record({Address(o ? Original<MakeGaps>(0x00552B30)(gaps, reinterpret_cast<CStruct*>(g_params[3]))
                              : gaps->Construct(reinterpret_cast<CStruct*>(g_params[3])))});
            gaps->m_children->m_next = nullptr;
            gaps->m_parents->m_next = nullptr;
            Record({Address(o ? Original<Game::CFindGapsGoal*(__thiscall*)(void*, uint8_t)>(0x00552B60)(gaps, static_cast<uint8_t>(k))
                              : gaps->Destroy(static_cast<uint8_t>(k)))});
            RecordRun();
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "goal types: the original reached its calls");
    CompareTraces(traces[0], traces[1], "goal types (competition, film, find gaps: 0x005517C0-0x00552E0F)");
}

void RestoreGoalTypes() { Restore(); }

} // namespace

SELFTEST(Early, TestGoalBasics, RestoreGoalBasics);
SELFTEST(Early, TestGoalRun, RestoreGoalRun);
SELFTEST(Early, TestGoalLife, RestoreGoalLife);
SELFTEST(Early, TestGoalTypes, RestoreGoalTypes);
