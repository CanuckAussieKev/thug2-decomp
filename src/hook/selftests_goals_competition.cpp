// Self-tests for the competition (Mdl::CCompetition, 0x00550040-0x005517BF): the original against ours on the same
// stand-in competition, profile manager and memory; the random streams seeded the same for both runs (Rnd2 and
// PlusOrMinus run for real), the debug output, script spawns, arrays, structure adders and getters, the profiles'
// methods and the goal manager stood in for by recording code.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/random.h"
#include "game/skate/competition.h"
#include "game/skate/profile_manager.h"
#include "game/goals/goal_manager.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Mdl::CCompetition;

alignas(16) uint8_t g_competition[0x200];
alignas(16) uint8_t g_profiles[12][0x64];
alignas(16) uint8_t g_profileManager[0x40];
alignas(16) uint8_t g_namesArray[0x10];
bool g_competitionActive;
const char* const kNames[12] = {"Tony", "Bob", "Rodney", "Player", "Eric", "Chad", "Bam", "Mullen", "Lyn-Z",
                                 "Mike", "Paul", "Player"};
const char* const kBoardNames[8] = {"Ann", "Ben", "Cid", "Dee", "Eve", "Fay", "Gus", "Hal"};

int ProfileIndex(uint32_t profile)
{
    for (int i = 0; i < 12; ++i) {
        if (profile == reinterpret_cast<uint32_t>(g_profiles[i]))
            return i;
    }
    return 0;
}

void StandInCompetitionCalls(StandIns& s)
{
    StandInGoalMemory(s);
    StandInStructAdds(s);
    StandInPrintf(s);
    StandInScripts(s);
    StandInArrays(s);
    StandInGetters(s);
    using Manager = Obj::CPlayerProfileManager;
    using Profile = Obj::CSkaterProfile;
    s.At({0x005591B0, AddressOf(&Game::GetGoalManager)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x91B0});
        (void)r;
        return reinterpret_cast<uint32_t>(g_goalManager);
    });
    s.At({0x00558D90}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x8D90, r.ecx});
        return g_competitionActive ? 0xC0C0u : 0u;
    });
    s.At({0x0057B630, AddressOf(&Manager::GetCurrentProfile)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0xB630, r.ecx});
        return reinterpret_cast<uint32_t>(g_profiles[11]);
    });
    s.At({0x00557FB0, AddressOf(&Game::CGoalManager::GetGoalByIndex)}, 4, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x7FB0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[r.args[0] % 12]);
    });
    s.At({0x0057B770, AddressOf(&Manager::GetTemplateName)}, 4, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0xB770, r.ecx, r.args[0]});
        return 0x7000u + r.args[0];
    });
    s.At({0x0057B7E0, AddressOf(&Manager::GetTemplate)}, 4, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0xB7E0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profiles[(r.args[0] - 0x7000) % 12]);
    });
    s.At({0x00586210, AddressOf(&Profile::GetDisplayName)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x6210, r.ecx});
        return reinterpret_cast<uint32_t>(kNames[ProfileIndex(r.ecx)]);
    });
    s.At({0x00586170, AddressOf(&Profile::IsPro)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x6170, r.ecx});
        return ProfileIndex(r.ecx) != 2 ? 1u : 0u;
    });
    s.At({0x00586190, AddressOf(&Profile::HasFlagF3149AC5)}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x6190, r.ecx});
        return ProfileIndex(r.ecx) == 5 ? 1u : 0u;
    });
    s.At({0x0046CB70, AddressOf(&CArray::GetLocalString)}, 4, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0xCB70, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(kBoardNames[r.args[0] % 8]);
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        if (g_gs3Trace) Log("  standin %X", r.ret);
        Record({0x8FD0, r.args[0]});
        return r.args[0] ^ 0x11111111u;
    });
}

void RestoreCompetition() { Restore(); }

void TestGoalsCompetition()
{
    Setup();
    SaveGlobal(0x00690FC8, 24); // (the two random streams)
    SaveGlobal(0x007CE4B4, 4);  // the profile manager
    auto* standIns = new StandIns;
    StandInCompetitionCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        g_getAnswers.clear();
        g_competitionActive = true;
        Mth::InitialRand(0x1234567);
        std::memset(g_profileManager, 0, sizeof(g_profileManager));
        *reinterpret_cast<uint32_t*>(g_profileManager) = 12; // (the template count)
        GameVariable<uint8_t*>(0x007CE4B4) = g_profileManager;
        for (uint32_t i = 0; i < sizeof(g_competition); ++i)
            g_competition[i] = static_cast<uint8_t>(i * 13 + 5);
        auto* c = reinterpret_cast<CCompetition*>(g_competition);
        for (CCompetition::Leader& leader : c->m_leaders)
            leader.m_name.m_capacity = 0; // (the original's String constructor leaves it unset, ours clears it)
        auto record = [&]() {
            for (uint32_t i = 0; i < sizeof(CCompetition); i += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(g_competition + i));
            for (const CCompetition::Leader& leader : c->m_leaders)
                g_trace.push_back(Hash(leader.m_name.c_str()));
        };

        using Construct = CCompetition*(__thiscall*)(CCompetition*, int);
        g_trace.push_back(reinterpret_cast<uint32_t>(original ? Original<Construct>(0x005504B0)(c, 7)
                                                              : c->Construct(7)));
        record();
        c->m_runs = 1; // (Start sorts with the runs so far: not set by the constructor)
        auto start = [&](float b, float s, float g, float bs, float ss, float gs, float bail) {
            using Fn = void(__thiscall*)(CCompetition*, float, float, float, float, float, float, float);
            if (original)
                Original<Fn>(0x00550560)(c, b, s, g, bs, ss, gs, bail);
            else
                c->Start(b, s, g, bs, ss, gs, bail);
            record();
        };
        auto endRun = [&](int score) {
            if (original)
                Original<void(__thiscall*)(CCompetition*, int)>(0x00550EF0)(c, score);
            else
                c->EndRun(score);
            record();
        };
        auto startRun = [&]() {
            if (original)
                Original<void(__thiscall*)(CCompetition*)>(0x00550020)(c);
            else
                c->StartRun();
        };
        auto board = [&]() {
            if (original)
                Original<void(__thiscall*)(CCompetition*)>(0x00550B50)(c);
            else
                c->ShowLeaderBoard();
            record();
        };
        auto sort = [&]() {
            if (original)
                Original<void(__thiscall*)(CCompetition*)>(0x00550200)(c);
            else
                c->SortLeaders();
            record();
        };
        auto queries = [&]() {
            for (int judge = -1; judge < 6; ++judge)
                g_trace.push_back(original ? Original<bool(__thiscall*)(CCompetition*, int)>(0x00550040)(c, judge)
                                           : c->IsTopJudge(judge));
            for (int place = 0; place < 10; ++place)
                g_trace.push_back(original ? Original<bool(__thiscall*)(CCompetition*, int)>(0x005501A0)(c, place)
                                           : c->PlaceIs(place));
            for (int round = -1; round < 5; ++round)
                g_trace.push_back(original ? Original<bool(__thiscall*)(CCompetition*, int)>(0x005501C0)(c, round)
                                           : c->RoundIs(round));
            g_trace.push_back(original ? Original<bool(__thiscall*)(CCompetition*)>(0x005501F0)(c) : c->IsEnded());
        };

        // A competition from the pros (no names given), three runs in each band of points, with bail points.
        start(50.0f, 70.0f, 90.0f, 10000.0f, 20000.0f, 30000.0f, 3.0f);
        queries();
        board();
        startRun();
        endRun(5000);
        queries();
        board();
        startRun();
        c->m_bailPoints = 4.5f;
        g_competitionActive = false;
        endRun(15000);
        board();
        queries();
        startRun();
        g_competitionActive = true;
        endRun(25000);
        queries();
        board();
        if (original)
            Original<void(__thiscall*)(CCompetition*)>(0x005501E0)(c);
        else
            c->End();
        queries();
        endRun(30000); // (ended: nothing)
        sort();
        // Ties: a pro level with the player (the player wins it), others level with each other.
        for (CCompetition::Leader& leader : c->m_leaders) {
            leader.m_scores[0] = 50;
            leader.m_scores[1] = leader.m_id & 1 ? 60 : 40;
            leader.m_scores[2] = leader.m_id == 3 ? 70 : 10;
        }
        sort();
        c->m_ended = false;
        c->m_runs = 0;
        sort();
        // Judges with equal scores.
        for (int i = 0; i < 5; ++i)
            c->m_judgeScores[i] = i == 2 ? 80.0f : 70.0f;
        queries();
        c->m_judgeScores[4] = 95.0f;
        c->m_judgeScores[0] = 5.0f;
        queries();

        // Many runs: points in every band, near 0 and near the top (rounding edges, clamps).
        c->m_ended = false;
        for (int k = 0; k < 90; ++k) {
            c->m_runs = k % 3;
            c->m_bailPoints = k % 5 == 0 ? 1.25f : 0.0f;
            static const int kScores[9] = {-100, -40, 9990, 29990, 30000, 58000, 59990, 60000, 1000000};
            endRun(k < 45 ? kScores[k % 9] : (k - 45) * 1371 - 250);
            queries();
        }

        // Names from the parameters, a first place name, medal points close enough for the random nudges to cross.
        g_getAnswers[{reinterpret_cast<uint32_t>(c->m_params), 0x1CC6F48D}] = reinterpret_cast<uint32_t>(g_namesArray);
        g_getAnswers[{reinterpret_cast<uint32_t>(c->m_params), 0xFAF19012}] = reinterpret_cast<uint32_t>("Champ");
        for (int round = 0; round < 6; ++round) {
            start(89.0f + round * 0.2f, 89.5f, 90.0f, 1000.0f, 2000.5f, 3000.25f, 1.5f);
            startRun();
            endRun(round == 0 ? 45000 : round == 1 ? 999999 : round == 2 ? -100 : round * 777);
            queries();
            startRun();
            c->m_bailPoints = round * 30.0f;
            endRun(round * 1500 + 3);
            queries();
            board();
        }
        g_trace.insert(g_trace.end(), {GameVariable<uint32_t>(0x00690FC8), GameVariable<uint32_t>(0x00690FD4),
                                       GameVariable<uint32_t>(0x00690FD0), GameVariable<uint32_t>(0x00690FDC)});
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 3000, "competition: the original reached its calls");
    Compare(traces[0], traces[1], "competition (0x00550040-0x005517BF): start, runs, leader board, standings");
}

} // namespace

SELFTEST(Early, TestGoalsCompetition, RestoreCompetition);
