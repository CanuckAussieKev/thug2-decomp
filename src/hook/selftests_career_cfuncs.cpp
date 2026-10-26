// Self-tests for the script commands of the career, the run, the records (0x005AE990-0x005AF08F, 0x005AEBD0,
// 0x005AFAF0, 0x005AFB30): each original against ours on the same stand-in skate module, career, game mode and
// network manager; the career's own methods run for real (the original's and ours) and its memory is compared, the
// skate module's calls without source and the structure adds are stood in for by recording code.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/net/gamenet_manager.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/career_cfuncs.h"
#include "game/skate/records.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;

constexpr uint32_t kLevel = 0x651533EC;
constexpr uint32_t kGetTotal = 0x580DF1BA;
constexpr uint32_t kChapter = 0x067E4AD1;
constexpr uint32_t kGoal = 0x032314D1;
constexpr uint32_t kFlag = 0x2E0B1465;
constexpr uint32_t kIsCareer = 0x1DED1EA4;
constexpr uint32_t kSkaterType = 0xBAF9B341;

alignas(16) uint8_t g_skaters[2][0x330]; // stand-in skaters (MatchesParams is stood in for)
alignas(16) uint8_t g_otherObject[0x40];
uint32_t g_multiplayer, g_shouldEndRun, g_idle, g_matches;
bool g_noSkater;
// The records: a game records object with three levels (their arcade score tables stood in for).
alignas(16) uint8_t g_records[0xC];
alignas(16) uint8_t g_levelRecords[3][0x1C];
char g_initials[4] = "KMB";

void StandInCareerCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInGameModeIsTrue(s);
    s.At({0x00553910}, 0, [](Regs& r) {
        Record({0x3910, r.ecx});
        return g_shouldEndRun;
    });
    s.At({0x005651F0}, 0, [](Regs&) {
        Record({0x51F0});
        return g_multiplayer;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return g_noSkater || r.args[0] > 1 ? 0u : reinterpret_cast<uint32_t>(g_skaters[r.args[0]]);
    });
    s.At({0x0057FED0, AddressOf(&Obj::CSkater::MatchesParams)}, 4, [](Regs& r) {
        Record({0xFED0, r.ecx, r.args[0]});
        return g_matches;
    });
    s.At({0x00567580}, 0, [](Regs& r) {
        Record({0x7580, r.ecx});
        return g_idle;
    });
    s.At({0x00567690}, 0, [](Regs& r) {
        Record({0x7690, r.ecx});
        return g_idle ^ 1;
    });
    s.At({0x005676E0}, 0, [](Regs& r) {
        Record({0x76E0, r.ecx});
        return 0x100u | g_idle; // (only AL counts)
    });
    s.At({0x0056BB70}, 4, [](Regs& r) {
        Record({0xBB70, r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00584BA0, AddressOf(&Obj::CSkaterCareer::ResetPart)}, 4, [](Regs& r) {
        Record({0x4BA0, r.ecx, r.args[0]});
        return 1u;
    });
    s.At({0x0057F0C0, AddressOf(&Records::CRecordTable::ReadFromStructure)}, 4, [](Regs& r) {
        Record({0xF0C0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0057EDD0, AddressOf(&Records::CLevelRecords::UpdateInitials)}, 4, [](Regs& r) {
        Record({0xEDD0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
}

void TestCareerCFuncs()
{
    Setup();
    GameVariable<uint8_t*>(0x007CE4BC) = g_records;
    auto* standIns = new StandIns;
    StandInCareerCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        std::memset(g_skaters, 0, sizeof(g_skaters));
        reinterpret_cast<Obj::CObject*>(g_skaters[0])->m_type = kSkaterType;
        reinterpret_cast<Obj::CObject*>(g_skaters[1])->m_type = kSkaterType;
        std::memset(g_otherObject, 0, sizeof(g_otherObject));
        reinterpret_cast<Obj::CObject*>(g_otherObject)->m_type = 0x12345678;
        std::memset(g_records, 0, sizeof(g_records));
        for (int i = 0; i < 3; ++i) {
            std::memset(g_levelRecords[i], 0, sizeof(g_levelRecords[i]));
            reinterpret_cast<uint32_t*>(g_levelRecords[i])[2] = 0x7AB1E000u + i; // (the arcade table: stood in for)
        }
        auto* records = reinterpret_cast<Records::CGameRecords*>(g_records);
        records->m_levelCount = 3;
        records->m_defaultInitials = g_initials;
        records->m_levels = reinterpret_cast<Records::CLevelRecords*>(g_levelRecords);
        g_multiplayer = g_shouldEndRun = g_idle = g_matches = 0;
        g_noSkater = false;
        auto* career = reinterpret_cast<Obj::CSkaterCareer*>(g_career);
        auto call = [&](uint32_t address, CFunc ours, Params& params, void* object = nullptr, bool nullScript = false) {
            CallCommand(original, address, ours, params, object, nullScript);
        };
        using namespace CFuncs;
        Params none;

        // The level: started, asked, scores, totals, classic goal counts, chapter, reset
        {
            Params a; a.Integer(kLevel, 3);
            call(0x005AE990, CFunc_CareerStartLevel, a);
            Params b; b.Integer(0, 3);
            call(0x005AE9C0, CFunc_CareerLevelIs, b);
            Params c; c.Integer(0, 4);
            call(0x005AE9C0, CFunc_CareerLevelIs, c);
            career->m_level = 2;
            call(0x005AE9F0, CFunc_GetCareerLevelScore, none);
            Params d; d.Integer(kLevel, 7).Flag(kGetTotal);
            call(0x005AE9F0, CFunc_GetCareerLevelScore, d);
            Params e; e.Integer(0, 1234);
            call(0x005AEAA0, CFunc_SetLevelTotalPoints, e);
            call(0x005AEAE0, CFunc_GetClassicLevelGoalCount, none);
            Params f; f.Integer(kLevel, 11).Flag(kGetTotal);
            call(0x005AEAE0, CFunc_GetClassicLevelGoalCount, f);
            Params g; g.Integer(0, 77);
            call(0x005AEB80, CFunc_SetClassicTotalGoals, g);
            call(0x005AEC20, CFunc_GetCurrentChapter, none);
            Params h; h.Integer(kChapter, 9);
            call(0x005AEC50, CFunc_SetCurrentChapter, h);
            Params i; i.Integer(kChapter, 4);
            call(0x005AEC50, CFunc_SetCurrentChapter, i);
            call(0x005AEC20, CFunc_GetCurrentChapter, none);
            call(0x005AEC80, CFunc_CareerReset, none);
            Params j; j.Checksum(0, 0x4C10DE52);
            call(0x005AEC80, CFunc_CareerReset, j);
        }
        // Goals (career mode or not), level flags (with levels), global flags
        {
            Params a; a.Integer(kGoal, 17);
            call(0x005AECC0, CFunc_SetGoal, a); // (not career: nothing)
            call(0x005AED40, CFunc_GetGoal, a);
            g_modeProperties[kIsCareer] = 1;
            call(0x005AECC0, CFunc_SetGoal, a);
            call(0x005AED40, CFunc_GetGoal, a);
            call(0x005AED70, CFunc_JustGotGoal, a);
            Params b; b.Integer(kGoal, 40);
            call(0x005AECC0, CFunc_SetGoal, b);
            call(0x005AED10, CFunc_UnSetGoal, a);
            call(0x005AED40, CFunc_GetGoal, a);
            call(0x005AED70, CFunc_JustGotGoal, b);
            Params c; c.Integer(kFlag, 33);
            call(0x005AEDA0, CFunc_SetFlag, c);
            call(0x005AEE30, CFunc_GetFlag, c);
            call(0x005AEEA0, CFunc_JustGotFlag, c);
            Params d; d.Integer(kFlag, 33).Integer(kLevel, 6);
            call(0x005AEE30, CFunc_GetFlag, d);
            Params e; e.Integer(kFlag, 34).Integer(kLevel, 0);
            call(0x005AEE30, CFunc_GetFlag, e);
            call(0x005AEDD0, CFunc_UnSetFlag, c);
            call(0x005AEE30, CFunc_GetFlag, c);
            Params f; f.Integer(kFlag, 61).Integer(kLevel, 9);
            call(0x005AEDD0, CFunc_UnSetFlag, f);
            call(0x005AEE30, CFunc_GetFlag, f);
            Params g; g.Integer(kFlag, 100);
            call(0x005AEED0, CFunc_SetGlobalFlag, g);
            call(0x005AEF30, CFunc_GetGlobalFlag, g);
            Params h; h.Integer(kFlag, 101);
            call(0x005AEF30, CFunc_GetGlobalFlag, h);
            call(0x005AEF00, CFunc_UnSetGlobalFlag, g);
            call(0x005AEF30, CFunc_GetGlobalFlag, g);
            Params i; i.Integer(kFlag, 7);
            call(0x005AEF00, CFunc_UnSetGlobalFlag, i);
        }
        // ProfileEquals: the script's skater, another object, no object, a null script; multiplayer or not
        {
            g_matches = 1;
            call(0x005AEF60, CFunc_ProfileEquals, none, g_skaters[1]);
            g_matches = 0;
            call(0x005AEF60, CFunc_ProfileEquals, none, g_skaters[0]);
            for (uint32_t multi : {0u, 1u}) {
                g_multiplayer = multi;
                g_matches = 1;
                call(0x005AEF60, CFunc_ProfileEquals, none, g_otherObject);
                call(0x005AEF60, CFunc_ProfileEquals, none);
                call(0x005AEF60, CFunc_ProfileEquals, none, nullptr, true);
                g_noSkater = true;
                call(0x005AEF60, CFunc_ProfileEquals, none);
                g_noSkater = false;
            }
        }
        // The mode and the run
        {
            call(0x005AEFC0, CFunc_IsCareerMode, none);
            g_modeProperties[kIsCareer] = 0;
            call(0x005AEFC0, CFunc_IsCareerMode, none);
            call(0x005AEFF0, CFunc_ShouldEndRun, none);
            g_shouldEndRun = 1;
            call(0x005AEFF0, CFunc_ShouldEndRun, none);
            g_shouldEndRun = 0;
            call(0x005AF020, CFunc_EndRunSelected, none);
            call(0x005AEFE0, CFunc_EndRun, none);
            call(0x005AEFF0, CFunc_ShouldEndRun, none);
            call(0x005AF020, CFunc_EndRunSelected, none);
            for (uint32_t idle : {0u, 1u}) {
                g_idle = idle;
                call(0x005AF030, CFunc_AllSkatersAreIdle, none);
                call(0x005AF040, CFunc_FirstTrickStarted, none);
                call(0x005AF050, CFunc_FirstTrickCompleted, none);
            }
            // CalculateFinalScores: a client without and with the final scores, the server
            call(0x005AF060, CFunc_CalculateFinalScores, none);
            g_net[0x44] = 1;
            call(0x005AF060, CFunc_CalculateFinalScores, none);
            g_net[0] = 1;
            call(0x005AF060, CFunc_CalculateFinalScores, none);
        }
        // The records' commands
        {
            career->m_level = 1;
            Params a; a.Integer(kLevel, 2);
            call(0x005AEBD0, Records::CFunc_UpdateArcadeScores, a);
            call(0x005AEBD0, Records::CFunc_UpdateArcadeScores, none);
            call(0x005AFAF0, Records::CFunc_UpdateInitials, none);
            career->m_level = 0;
            call(0x005AFAF0, Records::CFunc_UpdateInitials, none);
            call(0x005AFB30, Records::CFunc_NewRecord, none);
            g_skate[0x384] = 1;
            call(0x005AFB30, Records::CFunc_NewRecord, none);
        }
        RecordState();
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 900, "career commands: the original reached its calls");
    Compare(traces[0], traces[1], "career, goal, flag, run and records commands (0x005AE990-0x005AF060)");
}

} // namespace

SELFTEST(Early, TestCareerCFuncs, Restore);
