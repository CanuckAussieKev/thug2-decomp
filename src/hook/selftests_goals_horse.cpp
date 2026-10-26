// Self-tests for the HORSE game (Mdl::CHorse, 0x005600D0-0x005608BF) and the horse / network / race goals
// (0x005608C0-0x0056123F): the original against ours on the same stand-in
// game state (skaters by id, their names and scores, the Horse restarts, the script strings), the game's sprintf
// stood in for by ours.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/string.h"
#include "game/mth/random.h"
#include "game/obj/proxim.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_net_types.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/skate/game_mode.h"
#include "game/skate/horse.h"
#include "game/skate/victory_conditions.h"
#include "game/skater/end_run.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Mdl::CHorse;

uint32_t Address(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
bool Coin() { return (Next() & 1) != 0; }

alignas(16) uint8_t g_horseBuffer[sizeof(CHorse) + 0x10];
alignas(16) uint8_t g_skaters[8][0x40];
alignas(16) uint8_t g_scores[8][0x40];
bool g_present[8];
int g_totals[8];
int g_restarts;
bool g_debugHorse;
uint32_t g_rnd;

CHorse* Horse() { return reinterpret_cast<CHorse*>(g_horseBuffer); }
int SkaterIndex(uint32_t address)
{
    for (int i = 0; i < 8; ++i)
        if (address == Address(g_skaters[i]) || address == Address(g_scores[i]))
            return i;
    return -1;
}

void StandInHorseCalls(StandIns& s)
{
    StandInGoalMemory(s);
    StandInPrintf(s);
    StandInContains(s);
    s.At({0x006234FE}, 0, [](Regs& r) { // sprintf (the game's C runtime): ours, not recorded
        std::vsnprintf(reinterpret_cast<char*>(r.args[0]), 0x200, reinterpret_cast<const char*>(r.args[1]),
                       reinterpret_cast<va_list>(&r.args[2]));
        return 0u;
    });
    s.At({0x00566600}, 4, [](Regs& r) {
        Record({0x6600, r.ecx, r.args[0]});
        return r.args[0] < 8 && g_present[r.args[0]] ? Address(g_skaters[r.args[0]]) : 0u;
    });
    s.At({0x00581580, AddressOf(&Obj::CSkater::GetDisplayName)}, 0, [](Regs& r) {
        Record({0x1580, r.ecx});
        static const char* const kNames[] = {"Tony", "Bob", "Eric", "Chad", "Rodney", "Elissa", "Bam", "Lyn-Z"};
        int i = SkaterIndex(r.ecx);
        return Address(kNames[i < 0 ? 0 : i]);
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, r.ecx});
        int i = SkaterIndex(r.ecx);
        return Address(g_scores[i < 0 ? 0 : i]);
    });
    s.At({0x00561B40}, 0, [](Regs& r) {
        Record({0x1B40, r.ecx});
        int i = SkaterIndex(r.ecx);
        return static_cast<uint32_t>(g_totals[i < 0 ? 0 : i]);
    });
    using GlobalString = const char* (*)(uint32_t);
    s.At({0x00478FD0, AddressOf(static_cast<GlobalString>(&Script::GetString))}, 0, [](Regs& r) {
        Record({0x8FD0, r.args[0]});
        static char texts[4][32];
        static int next;
        char* text = texts[next++ & 3];
        std::snprintf(text, sizeof(text), "str%08X", r.args[0]);
        return Address(text);
    });
    using GlobalIntegerNamed = int (*)(const char*);
    s.At({0x004790C0, AddressOf(static_cast<GlobalIntegerNamed>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x90C0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_debugHorse ? 1u : 0u;
    });
    s.At({0x0046B6C0, AddressOf(&Obj::GetRestartNodes)}, 0, [](Regs& r) {
        Record({0xB6C0, r.args[0], r.args[2]});
        return static_cast<uint32_t>(g_restarts);
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        ++g_rnd;
        return r.args[0] ? (g_rnd * 7919u) % r.args[0] : 0u; // (the original divides by zero for 0 - never asked)
    });
}

// The original's string getters: the string returned through the hidden pointer, the first stack argument (RET 8);
// its text's hash.
// (ours: the string freed before the hash is recorded, as there)
uint32_t HashOf(Str::String text) { return Hash(text.c_str()); }
uint32_t OriginalStringHash(uint32_t address, CHorse* h, uint32_t argument)
{
    alignas(Str::String) uint8_t made[sizeof(Str::String)];
    Original<void(__thiscall*)(CHorse*, void*, uint32_t)>(address)(h, made, argument);
    auto* text = reinterpret_cast<Str::String*>(made);
    uint32_t hash = Hash(text->c_str());
    text->~String();
    return hash;
}

void RecordHorse()
{
    for (uint32_t k = 0; k < sizeof(CHorse); k += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_horseBuffer + k));
}

void TestGoalsHorse()
{
    Setup();
    auto* standIns = new StandIns;
    StandInHorseCalls(*standIns);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        g_rnd = 0;
        for (uint32_t seed = 0; seed < 300; ++seed) {
            ResetMemory();
            g_seed = seed * 2654435761u + 31;
            g_hasFlag.clear();
            for (int i = 0; i < 8; ++i) {
                g_present[i] = Next() % 4 != 0;
                g_totals[i] = Next() % 3 == 0 ? 0 : static_cast<int>(Next() % 5) * 1000;
                std::memset(g_skaters[i], 0, sizeof(g_skaters[i]));
                *reinterpret_cast<uint32_t*>(g_skaters[i] + 0x14) = static_cast<uint32_t>(Next() % 8);
            }
            g_restarts = 1 + static_cast<int>(Next() % 4);
            g_debugHorse = Next() % 6 == 0;
            for (uint32_t k = 0; k < sizeof(g_horseBuffer); ++k)
                g_horseBuffer[k] = static_cast<uint8_t>(Next());
            CHorse* h = Horse();
            static const char* const kWords[] = {"HORSE", "skate", "PIG", "e", "A", "ice", "Ollie", "UP"};
            std::snprintf(h->m_word, sizeof(h->m_word), "%s", kWords[Next() % 8]);
            std::snprintf(h->m_panel, sizeof(h->m_panel), "%s", "panel");
            h->m_skater = static_cast<int>(Next() % 8);
            h->m_scoreToBeat = Next() % 3 == 0 ? 0 : static_cast<int>(Next() % 5) * 1000;
            for (int& letters : h->m_letters)
                letters = static_cast<int>(Next() % 6);
            h->m_restart = static_cast<int>(Next() % 5);
            Record({0xEEEE0000 | seed});
            auto plain = [&](uint32_t address, void (CHorse::*ours)()) {
                if (o)
                    Original<void(__thiscall*)(CHorse*)>(address)(h);
                else
                    (h->*ours)();
                RecordHorse();
            };
            if (seed % 7 == 0) {
                Record({Address(o ? Original<CHorse*(__thiscall*)(CHorse*)>(0x005600D0)(h) : h->Construct())});
                RecordHorse();
                plain(0x00560100, &CHorse::Init);
            }
            plain(0x00560180, &CHorse::StartRun);
            {
                Params params;
                static const uint32_t kStatuses[] = {0x0FD80EBA, 0x006BE86F, 0xBAA12466, 0xAE7D29B0, 0x23DB4AEA,
                                                     0xA71DD8EB, 0x12345678};
                if (Coin())
                    g_hasFlag.insert({Address(params.Struct()), kStatuses[Next() % 7]});
                if (Coin())
                    h->m_status = kStatuses[Next() % 7];
                Record({o ? Original<bool(__thiscall*)(CHorse*, CStruct*)>(0x00560190)(h, params.Struct())
                          : h->StatusEquals(params.Struct())});
            }
            {
                int player = static_cast<int>(Next() % 8);
                Record({o ? OriginalStringHash(0x00560260, h, static_cast<uint32_t>(player))
                          : HashOf(h->GetWordForPlayer(player))});
            }
            Record({static_cast<uint32_t>(o ? Original<int(__thiscall*)(CHorse*)>(0x00560310)(h) : h->GetRestart())});
            plain(0x00560340, &CHorse::SetNextRestartPoint);
            Record({o ? Original<bool(__thiscall*)(CHorse*)>(0x00560380)(h) : h->Ended()});
            Record({static_cast<uint32_t>(o ? Original<int(__thiscall*)(CHorse*)>(0x005603D0)(h) : h->GetNextValidSkaterId())});
            {
                static const uint32_t kTexts[] = {0x5078BC7C, 0x2523853D, 0x2A13F850, 0x478970CD,
                                                  0x5F7D73E9, 0x9196D920, 0xDE802177, 0x1234};
                for (int k = 0; k < 3; ++k) {
                    uint32_t which = kTexts[Next() % 8];
                    if (static_cast<uint32_t>(h->m_skater) >= 8)
                        break;
                    if ((which == 0x9196D920 || which == 0x2523853D) && !g_present[h->m_skater])
                        continue; // (the current skater is read through null)
                    if ((which == 0x478970CD && !g_present[1]) || (which == 0xDE802177 && !g_present[0]))
                        continue;
                    if ((which == 0x9196D920 || which == 0x2523853D) &&
                        (*reinterpret_cast<uint32_t*>(g_skaters[h->m_skater] + 0x14) >= 8 ||
                         !g_present[*reinterpret_cast<uint32_t*>(g_skaters[h->m_skater] + 0x14)]))
                        continue;
                    Record({which, o ? OriginalStringHash(0x00560440, h, which) : HashOf(h->GetString(which))});
                }
            }
            if (static_cast<uint32_t>(h->m_skater) < 8 && g_present[h->m_skater])
                plain(0x00560720, &CHorse::EndRun);
            plain(0x005608B0, &CHorse::SwitchPlayers);
            {
                static const char* const kNewWords[] = {"PIG", "SKATE", "horse"};
                const char* word = kNewWords[Next() % 3];
                if (o)
                    Original<void(__thiscall*)(CHorse*, Str::String)>(0x005601F0)(h, Str::String(word));
                else
                    h->SetWord(Str::String(word));
                RecordHorse();
            }
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "horse: the original ran");
    CompareTraces(traces[0], traces[1], "horse (0x005600D0-0x005608BF)");
}


// --- The horse, network and race goals (0x005608C0-0x0056123F) -----------------------------------------------------

alignas(16) uint8_t g_goalBuffer[0x90];
alignas(16) uint8_t g_params[0x10];
alignas(16) uint8_t g_flags[0x10];
alignas(16) uint8_t g_localScore[0x40];
alignas(16) uint8_t g_players[3][0x20];
alignas(16) uint8_t g_playerSkaters[3][0x340];
alignas(16) uint8_t g_endRuns[3][0x20];
alignas(16) uint8_t g_fakeManager[0x40];
alignas(16) uint8_t g_spots[4][0x10];
uint32_t g_spotElements[4];
CArray g_spotArray;
uint32_t g_mixSalt;
int g_numPlayers;
uint32_t g_mode;
uint32_t g_playerIndex;

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = (a ^ 0x9E3779B9u) * 0x85EBCA6Bu ^ b * 0xC2B2AE35u ^ g_mixSalt;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return h ^ (h >> 13);
}

void StandInNetTypeCalls(StandIns& s)
{
    StandInGetters(s);
    StandInStructAdds(s);
    StandInStructChanges(s);
    StandInScripts(s);
    MakeGoalVtable(s);
    // CGoal's own code the subclasses call: recorded, answered from the seed.
    struct Base {
        uint32_t address;
        uint32_t ours;
        uint16_t pop;
        bool answer;
    };
    using Base2 = bool (Game::CGoal::*)(bool, bool);
    const Base kBase[] = {
        {0x00556FE0, AddressOf(&Game::CGoal::Construct), 4, false},
        {0x00556110, AddressOf(&Game::CGoal::DestroyBody), 0, false},
        {0x00556230, AddressOf(&Game::CGoal::ShouldExpire), 0, true},
        {0x00554CD0, AddressOf(&Game::CGoal::IsExpired), 0, true},
        {0x00554F40, AddressOf(&Game::CGoal::Expire), 0, false},
        {0x00557360, AddressOf(&Game::CGoal::Activate), 0, true},
        {0x00557560, AddressOf(static_cast<Base2>(&Game::CGoal::Deactivate)), 8, true},
        {0x00554DD0, AddressOf(&Game::CGoal::Update), 0, true},
        {0x00557270, AddressOf(&Game::CGoal::Init), 0, false},
        {0x00556240, AddressOf(&Game::CGoal::AllSkatersRunDone), 0, true},
        {0x00553FD0, AddressOf(&Game::CGoal::GoalFlagEquals), 12, true},
        {0x00554070, AddressOf(&Game::CGoal::ResetGoalFlags), 0, false},
        {0x00553F30, AddressOf(&Game::CGoal::AddTime), 4, false},
        {0x00554A80, AddressOf(&Game::CGoal::RunCallbackScript), 4, true},
        {0x00553F10, AddressOf(&Game::CGoal::SetTimeLeft), 4, false},
        {0x00554280, AddressOf(&Game::CGoal::GetGoalId), 0, false},
        {0x00576B80, AddressOf(&Game::CCompleteGoalsVictoryCondition::ConditionComplete), 0, true},
        {0x005586A0, AddressOf(&Game::CGoalManager::SetGoalFlag), 16, true},
        {0x00558750, AddressOf(&Game::CGoalManager::NextWaypoint), 4, true},
        {0x00558180, AddressOf(&Game::CGoalManager::LoseGoal), 4, true},
        {0x005332D0, AddressOf(&GameNet::Manager::ClearGameOver), 0, false},
        {0x005332C0, AddressOf(&GameNet::Manager::SetGameOver), 0, false},
        {0x005332B0, AddressOf(&GameNet::Manager::IsGameOver), 0, true},
        {0x005331E0, AddressOf(&GameNet::Manager::FinalScoresReceived), 0, true},
        {0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame), 0, true},
        {0x00510F30, AddressOf(&Obj::CSkaterEndRunComponent::RunDone), 0, true},
        {0x00510EA0, AddressOf(&Obj::CSkaterEndRunComponent::EndRun), 4, false},
    };
    for (const Base& b : kBase) {
        Base base = b;
        s.At({base.address, base.ours}, base.pop, [base](Regs& r) {
            Record({base.address & 0xFFFFF, r.ecx});
            for (uint32_t i = 0; i < base.pop / 4u; ++i)
                g_trace.push_back(r.args[i] & ((base.address == 0x00557560 || base.address == 0x00510EA0) ? 0xFFu : ~0u));
            if (base.address == 0x00556FE0)
                return r.ecx; // (the constructor returns this)
            if (base.address == 0x00554280)
                return 0x2000u + (Mix(0x4280, g_seed) & 3);
            return base.answer ? Mix(base.address, r.ecx ^ (base.pop ? r.args[0] : 0)) & 1 : 0u;
        });
    }
    s.At({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, [](Regs& r) {
        Record({0x32A0, r.ecx});
        return g_mode;
    });
    s.At({0x005591B0, AddressOf(&Game::GetGoalManager)}, 0, [](Regs&) {
        Record({0x91B0});
        return Address(g_fakeManager);
    });
    s.At({0x00566520}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return Address(g_skaters[0]);
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, r.ecx});
        return Address(g_scores[0]);
    });
    s.At({0x00539B00, AddressOf(&GameNet::Manager::CountPlayers)}, 0, [](Regs& r) {
        Record({0x9B00, r.ecx});
        return static_cast<uint32_t>(g_numPlayers);
    });
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0x72C0, r.ecx, r.args[1] & 0xFF});
        g_playerIndex = 0;
        return g_numPlayers > 0 ? Address(g_players[0]) : 0u;
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Record({0x7010, r.ecx, r.args[1] & 0xFF});
        return static_cast<int>(++g_playerIndex) < g_numPlayers ? Address(g_players[g_playerIndex]) : 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        for (int k = 0; k < 3; ++k)
            if (r.ecx == Address(g_playerSkaters[k]))
                return Address(g_endRuns[k]);
        return 0u;
    });
}

void TestGoalsNetTypes()
{
    Setup();
    auto* standIns = new StandIns;
    StandInGoalMemory(*standIns);
    StandInPrintf(*standIns);
    StandInContains(*standIns);
    StandInNetTypeCalls(*standIns);
    standIns->At({0x006234FE}, 0, [](Regs& r) {
        std::vsnprintf(reinterpret_cast<char*>(r.args[0]), 0x100, reinterpret_cast<const char*>(r.args[1]),
                       reinterpret_cast<va_list>(&r.args[2]));
        return 0u;
    });
    static const uint32_t kModes[] = {0xEC200EAA, 0x10F84276, 0xFC13B531, 0x846504FC, 0x4C10DE52};
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        for (uint32_t seed = 0; seed < 900; ++seed) {
            ResetMemory();
            g_seed = seed * 2654435761u + 77;
            g_mixSalt = Next();
            g_getAnswers.clear();
            g_typedAnswers.clear();
            g_hasFlag.clear();
            g_hasComponent.clear();
            for (uint32_t& answer : g_virtualAnswers)
                answer = Next() & 1;
            g_mode = Coin() ? kModes[4] : kModes[Next() % 5];
            g_numPlayers = static_cast<int>(Next() % 4);
            for (int k = 0; k < 3; ++k) {
                std::memset(g_players[k], 0, sizeof(g_players[k]));
                *reinterpret_cast<uint32_t*>(g_players[k] + 0x14) = Address(g_playerSkaters[k]);
                g_playerSkaters[k][0x320] = Next() % 4 == 0;
            }
            std::memset(g_skaters[0], 0, sizeof(g_skaters[0]));
            *reinterpret_cast<int*>(g_scores[0] + 0x20) = static_cast<int>(Next() % 4) * 500 - 500;
            for (uint32_t k = 0; k < sizeof(g_goalBuffer); ++k)
                g_goalBuffer[k] = static_cast<uint8_t>(Next());
            auto* goal = reinterpret_cast<Game::CGoal*>(g_goalBuffer);
            goal->m_node.vtable = g_goalVtable;
            goal->m_params = reinterpret_cast<CStruct*>(g_params);
            goal->m_goalFlags = reinterpret_cast<CStruct*>(g_flags);
            static const int kTimes[] = {-5, 0, 1, 30000};
            goal->m_timeLeft = kTimes[Next() % 4];
            static const int kEndRuns[] = {0, 1, 2};
            goal->m_endRunType = kEndRuns[Next() % 3];
            uint32_t p = Address(g_params);
            g_getAnswers[{p, 0xA32FAF13}] = Next() % 3;
            g_getAnswers[{p, 0xCD66C8AE}] = Next() % 3 * 500;
            if (Coin())
                g_getAnswers[{p, 0x1DAAE95B}] = 1 + Next() % 3;
            if (Coin())
                g_hasComponent.insert({p, 0xA4038EC9});
            for (uint32_t flag : {0xA4BA22BAu, 0x38221DF4u})
                if (Coin())
                    g_hasFlag.insert({p, flag});
            g_getAnswers[{p, 0xBFECC45B}] = Address("race_goal");
            // the horse spots / race waypoints
            uint32_t numSpots = Next() % 4;
            std::memset(&g_spotArray, 0, sizeof(g_spotArray));
            g_spotArray.m_size = numSpots;
            for (uint32_t k = 0; k < 4; ++k) {
                g_spotElements[k] = Address(g_spots[k]);
                uint32_t spot = Address(g_spots[k]);
                g_getAnswers[{spot, 0x2E0B1465}] = 0xF1A60000 + k;
                g_getAnswers[{Address(g_flags), 0xF1A60000 + k}] = Next() % 3 == 0;
                if (Coin())
                    g_getAnswers[{spot, 0xCD66C8AE}] = Next() % 3 * 500;
                g_getAnswers[{spot, 0x906B67BA}] = Next() % 20;
            }
            if (numSpots == 1)
                g_spotArray.m_single = g_spotElements[0];
            else
                g_spotArray.m_elements = g_spotElements;
            g_getAnswers[{p, 0x92C6E839}] = Address(&g_spotArray);
            g_getAnswers[{p, 0xA77B1F07}] = Address(&g_spotArray);
            Record({0xEEEE0000 | seed});
            auto recordGoal = [&] {
                for (uint32_t k = 0; k < sizeof(g_goalBuffer); k += 4)
                    g_trace.push_back(*reinterpret_cast<uint32_t*>(g_goalBuffer + k));
            };
            uint32_t kind = seed % 3;
            if (kind == 0) {
                auto* h = reinterpret_cast<Game::CHorseGoal*>(goal);
                if (seed % 6 == 0) {
                    Record({Address(o ? Original<Game::CHorseGoal*(__thiscall*)(void*, CStruct*)>(0x005608C0)(h, goal->m_params)
                                      : h->Construct(goal->m_params))});
                    recordGoal();
                    goal->m_node.vtable = g_goalVtable;
                }
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560930)(h) : h->ShouldExpire()});
                Record({o ? Original<bool(__thiscall*)(void*)>(0x005608F0)(h) : h->IsExpired()});
                if (o)
                    Original<void(__thiscall*)(void*)>(0x00560970)(h);
                else
                    h->Expire();
                if (numSpots) {
                    if (o)
                        Original<void(__thiscall*)(void*)>(0x005609B0)(h);
                    else
                        h->Land();
                }
                recordGoal();
                uint8_t flags = static_cast<uint8_t>(Next() & 1);
                Record({Address(o ? Original<Game::CHorseGoal*(__thiscall*)(void*, uint8_t)>(0x00560980)(h, flags) : h->Destroy(flags))});
                recordGoal();
            } else if (kind == 1) {
                auto* n = reinterpret_cast<Game::CNetGoal*>(goal);
                if (seed % 6 == 1) {
                    Record({Address(o ? Original<Game::CNetGoal*(__thiscall*)(void*, CStruct*)>(0x00560AC0)(n, goal->m_params)
                                      : n->Construct(goal->m_params))});
                    recordGoal();
                    goal->m_node.vtable = g_goalVtable;
                }
                Record({static_cast<uint32_t>(o ? Original<int(__thiscall*)(void*)>(0x00560B00)(n) : n->HasComponentA302D7CD())});
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560B10)(n) : n->TimerBeeps()});
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560BC0)(n) : n->Activate()});
                bool force = Coin(), tree = Coin();
                Record({o ? Original<bool(__thiscall*)(void*, bool, bool)>(0x00560BE0)(n, force, tree) : n->Deactivate(force, tree)});
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560B70)(n) : n->Update()});
                recordGoal();
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560C20)(n) : n->ShouldExpire()});
                recordGoal();
                if (o)
                    Original<void(__thiscall*)(void*)>(0x00560E20)(n);
                else
                    n->Expire();
                uint8_t flags = static_cast<uint8_t>(Next() & 1);
                Record({Address(o ? Original<Game::CNetGoal*(__thiscall*)(void*, uint8_t)>(0x00560B40)(n, flags) : n->Destroy(flags))});
                recordGoal();
            } else {
                auto* r = reinterpret_cast<Game::CRaceGoal*>(goal);
                if (seed % 6 == 2) {
                    Record({Address(o ? Original<Game::CRaceGoal*(__thiscall*)(void*, CStruct*)>(0x00560E60)(r, goal->m_params)
                                      : r->Construct(goal->m_params))});
                    recordGoal();
                    goal->m_node.vtable = g_goalVtable;
                }
                r->m_laps = 1 + static_cast<int>(Next() % 3);
                r->m_lap = 1 + static_cast<int>(Next() % 3);
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560EF0)(r) : r->Activate()});
                recordGoal();
                r->m_lap = 1 + static_cast<int>(Next() % 3);
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560F00)(r) : r->IsExpired()});
                Record({o ? Original<bool(__thiscall*)(void*)>(0x00560F20)(r) : r->ShouldExpire()});
                if (o)
                    Original<void(__thiscall*)(void*)>(0x00560F40)(r);
                else
                    r->Expire();
                uint32_t id = 0x2000 + Next() % 4;
                Record({o ? Original<bool(__thiscall*)(void*, uint32_t)>(0x00560F90)(r, id) : r->NextRaceWaypoint(id)});
                recordGoal();
                uint8_t flags = static_cast<uint8_t>(Next() & 1);
                Record({Address(o ? Original<Game::CRaceGoal*(__thiscall*)(void*, uint8_t)>(0x00560F60)(r, flags) : r->Destroy(flags))});
                recordGoal();
            }
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "net goal types: the original ran");
    CompareTraces(traces[0], traces[1], "horse / net / race goals (0x005608C0-0x0056123F)");
}

void RestoreHorse() { Restore(); }

} // namespace

SELFTEST(Early, TestGoalsHorse, RestoreHorse);
SELFTEST(Early, TestGoalsNetTypes, RestoreHorse);
