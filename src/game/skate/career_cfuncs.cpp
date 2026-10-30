// Script commands of the career and the run, 0x005AE990-0x005AF08F (see career_cfuncs.h).

#include "game/skate/career_cfuncs.h"

#include <cstdint>

#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using namespace SkateCalls;

constexpr uint32_t kLevel = 0x651533EC;               // level
constexpr uint32_t kLevelScore = 0xA7BFCDA7;          // level_score
constexpr uint32_t kLevelScoreTotal = 0x756FB474;     // level_score_total
constexpr uint32_t kGetTotal = 0x580DF1BA;            // get_total
constexpr uint32_t kTotalScore = 0x0E145E9C;          // total_score
constexpr uint32_t kTotalScorePotential = 0x851B0904; // total_score_potential
constexpr uint32_t kClassicGoalCount = 0x062BB9BB;    // classic_goal_count
constexpr uint32_t kTotalGoalsComplete = 0x959DCF6E;  // total_goals_complete
constexpr uint32_t kTotalGoalsPossible = 0xA95F9E73;  // total_goals_possible
constexpr uint32_t kChapter = 0x067E4AD1;             // chapter
constexpr uint32_t kGoal = 0x032314D1;                // goal
constexpr uint32_t kFlag = 0x2E0B1465;                // flag
constexpr uint32_t kIsCareer = 0x1DED1EA4;            // is_career
constexpr uint32_t kSkaterType = 0xBAF9B341;          // OBJECT_TYPE_SKATER

// The skate module's calls about the run (no source yet): 0x00567580 every skater idle (in a horse game only the
// current horse skater counts), 0x00567690 the local skater's first trick started, 0x005676E0 completed;
// 0x0056BB70 the final scores sent to every player (true: the final ones).
bool AllSkatersIdle() { return Mdl::Skate::Instance()->AllSkatersIdle(); }
bool FirstTrickStarted() { return Mdl::Skate::Instance()->FirstTrickStarted(); }
bool FirstTrickCompleted() { return Mdl::Skate::Instance()->FirstTrickCompleted(); }
void SendScores(bool final) { Mdl::Skate::Instance()->SendScoreUpdates(final); }

GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }

} // namespace

// 0x005AE990: CareerStartLevel level=<n> - the career's level n started (its goals and flags noted for JustGot*).
bool __cdecl CFunc_CareerStartLevel(Script::CStruct* params, Script::CScript*)
{
    int level = 0;
    params->GetInteger(kLevel, &level, 1);
    Career()->StartLevel(level);
    return true;
}

// 0x005AE9C0: CareerLevelIs <n> - whether the career's current level is n.
bool __cdecl CFunc_CareerLevelIs(Script::CStruct* params, Script::CScript*)
{
    int level = 0;
    params->GetInteger(0u, &level, 1);
    return Career()->GetLevel() == level;
}

// 0x005AE9F0: GetCareerLevelScore [level=<n>] [get_total] - the level's (the current one's) score and total score as
// level_score / level_score_total; with get_total the career's totals too (total_score, total_score_potential).
bool __cdecl CFunc_GetCareerLevelScore(Script::CStruct* params, Script::CScript* script)
{
    Obj::CSkaterCareer* career = Career();
    int level = career->GetLevel();
    params->GetInteger(kLevel, &level, 0);
    script->m_params->AddInteger(kLevelScore, career->m_cash[level]);
    script->m_params->AddInteger(kLevelScoreTotal, career->m_amount[level]);
    if (params->ContainsFlag(kGetTotal)) {
        script->m_params->AddInteger(kTotalScore, career->m_totalCash);
        script->m_params->AddInteger(kTotalScorePotential, career->m_totalAmount);
    }
    return true;
}

// 0x005AEAA0: SetLevelTotalPoints <n> - the current level's total score.
bool __cdecl CFunc_SetLevelTotalPoints(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterCareer* career = Career();
    int points = 0;
    params->GetInteger(0u, &points, 1);
    career->SetLevelAmount(points);
    return true;
}

// 0x005AEAE0: GetClassicLevelGoalCount [level=<n>] [get_total] - the level's (the current one's) classic goals done
// as classic_goal_count; with get_total the totals too (total_goals_complete, total_goals_possible).
bool __cdecl CFunc_GetClassicLevelGoalCount(Script::CStruct* params, Script::CScript* script)
{
    Obj::CSkaterCareer* career = Career();
    int level = career->GetLevel();
    params->GetInteger(kLevel, &level, 0);
    script->m_params->AddInteger(kClassicGoalCount, career->GetCounter(level));
    if (params->ContainsFlag(kGetTotal)) {
        script->m_params->AddInteger(kTotalGoalsComplete, career->m_totalCounter);
        script->m_params->AddInteger(kTotalGoalsPossible, career->m_flag5EF);
    }
    return true;
}

// 0x005AEB80: SetClassicTotalGoals <n> - how many classic goals there are in all.
bool __cdecl CFunc_SetClassicTotalGoals(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterCareer* career = Career();
    int goals = 0;
    params->GetInteger(0u, &goals, 1);
    career->SetFlag5EF(static_cast<uint8_t>(goals));
    return true;
}

// 0x005AEC20: GetCurrentChapter - the career's chapter as chapter.
bool __cdecl CFunc_GetCurrentChapter(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(kChapter, Career()->m_value634);
    return true;
}

// 0x005AEC50: SetCurrentChapter chapter=<n> - the career's chapter (the highest reached kept too).
bool __cdecl CFunc_SetCurrentChapter(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterCareer* career = Career();
    int chapter = 0; // (uninitialised in the original)
    params->GetInteger(kChapter, &chapter, 0);
    career->SetValue634(chapter);
    return true;
}

// 0x005AEC80: CareerReset [<part>] - the career reset (the part named, or all of it), its gap checklists kept.
bool __cdecl CFunc_CareerReset(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterCareer* career = Career();
    uint32_t part = 0;
    params->GetChecksum(0u, &part, 0);
    career->ResetPart(part);
    return true;
}

// 0x005AECC0: SetGoal goal=<n> - the current level's goal n got (only in career mode).
bool __cdecl CFunc_SetGoal(Script::CStruct* params, Script::CScript*)
{
    int goal = 0;
    params->GetInteger("goal", &goal, 1);
    if (GameModeIsTrue(kIsCareer))
        Career()->SetGoal(goal, -1);
    return true;
}

// 0x005AED10: UnSetGoal goal=<n> - the current level's goal n not got.
bool __cdecl CFunc_UnSetGoal(Script::CStruct* params, Script::CScript*)
{
    int goal = 0;
    params->GetInteger("goal", &goal, 1);
    Career()->UnSetGoal(goal, -1);
    return true;
}

// 0x005AED40: GetGoal goal=<n> - whether the current level's goal n is got.
bool __cdecl CFunc_GetGoal(Script::CStruct* params, Script::CScript*)
{
    int goal = 0;
    params->GetInteger("goal", &goal, 1);
    return Career()->GetGoal(goal, -1);
}

// 0x005AED70: JustGotGoal goal=<n> - whether goal n was got since the level started.
bool __cdecl CFunc_JustGotGoal(Script::CStruct* params, Script::CScript*)
{
    int goal = 0;
    params->GetInteger(kGoal, &goal, 1);
    return Career()->JustGotGoal(goal);
}

// 0x005AEDA0: SetFlag flag=<n> - the current level's flag n set.
bool __cdecl CFunc_SetFlag(Script::CStruct* params, Script::CScript*)
{
    int flag = 0;
    params->GetInteger(kFlag, &flag, 1);
    Career()->SetFlag(flag, -1);
    return true;
}

// 0x005AEDD0: UnSetFlag flag=<n> [level=<l>] - level l's (the current level's) flag n cleared.
bool __cdecl CFunc_UnSetFlag(Script::CStruct* params, Script::CScript*)
{
    int flag = 0;
    params->GetInteger(kFlag, &flag, 1);
    int level = -1;
    params->GetInteger(kLevel, &level, 0);
    Career()->UnSetFlag(flag, level);
    return true;
}

// 0x005AEE30: GetFlag flag=<n> [level=<l>] - whether level l's flag n is set (no level, or level 0: the current
// level's - level 0 itself can't be asked for).
bool __cdecl CFunc_GetFlag(Script::CStruct* params, Script::CScript*)
{
    int flag = 0;
    params->GetInteger(kFlag, &flag, 1);
    int level = 0;
    params->GetInteger(kLevel, &level, 0);
    Obj::CSkaterCareer* career = Career();
    if (level)
        return career->GetFlag(flag, level);
    return career->GetFlag(flag, -1);
}

// 0x005AEEA0: JustGotFlag flag=<n> - whether flag n was set since the level started.
bool __cdecl CFunc_JustGotFlag(Script::CStruct* params, Script::CScript*)
{
    int flag = 0;
    params->GetInteger(kFlag, &flag, 1);
    return Career()->JustGotFlag(flag);
}

// 0x005AEED0 / 0x005AEF00 / 0x005AEF30: SetGlobalFlag / UnSetGlobalFlag / GetGlobalFlag flag=<n> - the career's
// global flag n (the cheats are among them).
bool __cdecl CFunc_SetGlobalFlag(Script::CStruct* params, Script::CScript*)
{
    int flag = 0;
    params->GetInteger(kFlag, &flag, 1);
    Career()->SetGlobalFlag(flag);
    return true;
}

bool __cdecl CFunc_UnSetGlobalFlag(Script::CStruct* params, Script::CScript*)
{
    int flag = 0;
    params->GetInteger(kFlag, &flag, 1);
    Career()->UnSetGlobalFlag(flag);
    return true;
}

bool __cdecl CFunc_GetGlobalFlag(Script::CStruct* params, Script::CScript*)
{
    int flag = 0;
    params->GetInteger(kFlag, &flag, 1);
    return Career()->GetGlobalFlag(flag);
}

// 0x005AEF60: ProfileEquals <description> - whether the skater matches the description (is_pro, is_male, stance...):
// the script's own object if it's a skater; otherwise, in a game of more than one player, the first skater; else
// false.
bool __cdecl CFunc_ProfileEquals(Script::CStruct* params, Script::CScript* script)
{
    if (script) {
        Obj::CObject* object = script->m_object.Get();
        if (object && object->m_type == kSkaterType)
            return static_cast<Obj::CSkater*>(object)->MatchesParams(params);
    }
    if (!IsMultiplayer())
        return false;
    auto* skater = static_cast<Obj::CSkater*>(GetSkater(0));
    if (!skater)
        return false;
    return skater->MatchesParams(params);
}

// 0x005AEFC0: IsCareerMode - the game mode's is_career.
bool __cdecl CFunc_IsCareerMode(Script::CStruct*, Script::CScript*) { return GameModeIsTrue(kIsCareer); }

// 0x005AEFE0: EndRun - the run is to end.
bool __cdecl CFunc_EndRun(Script::CStruct*, Script::CScript*)
{
    EndRunFlag() = true;
    return true;
}

// 0x005AEFF0: ShouldEndRun - whether EndRun was asked for, or the game mode says the run is over (elimiskate).
bool __cdecl CFunc_ShouldEndRun(Script::CStruct*, Script::CScript*)
{
    return EndRunFlag() || GameModeShouldEndRun();
}

// 0x005AF020: EndRunSelected - whether EndRun was asked for.
bool __cdecl CFunc_EndRunSelected(Script::CStruct*, Script::CScript*) { return EndRunFlag(); }

// 0x005AF030 / 0x005AF040 / 0x005AF050: AllSkatersAreIdle, FirstTrickStarted, FirstTrickCompleted - the skate
// module's.
bool __cdecl CFunc_AllSkatersAreIdle(Script::CStruct*, Script::CScript*) { return AllSkatersIdle(); }
bool __cdecl CFunc_FirstTrickStarted(Script::CStruct*, Script::CScript*) { return FirstTrickStarted(); }
bool __cdecl CFunc_FirstTrickCompleted(Script::CStruct*, Script::CScript*) { return FirstTrickCompleted(); }

// 0x005AF060: CalculateFinalScores - the server sends every player the final scores (true); a client: whether the
// final scores have come.
bool __cdecl CFunc_CalculateFinalScores(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager* net = NetManager();
    if (net->OnServer()) {
        SendScores(true);
        return true;
    }
    return net->FinalScoresReceived();
}

} // namespace CFuncs

REPLACE(0x005AE990, CFuncs::CFunc_CareerStartLevel);
REPLACE(0x005AE9C0, CFuncs::CFunc_CareerLevelIs);
REPLACE(0x005AE9F0, CFuncs::CFunc_GetCareerLevelScore);
REPLACE(0x005AEAA0, CFuncs::CFunc_SetLevelTotalPoints);
REPLACE(0x005AEAE0, CFuncs::CFunc_GetClassicLevelGoalCount);
REPLACE(0x005AEB80, CFuncs::CFunc_SetClassicTotalGoals);
REPLACE(0x005AEC20, CFuncs::CFunc_GetCurrentChapter);
REPLACE(0x005AEC50, CFuncs::CFunc_SetCurrentChapter);
REPLACE(0x005AEC80, CFuncs::CFunc_CareerReset);
REPLACE(0x005AECC0, CFuncs::CFunc_SetGoal);
REPLACE(0x005AED10, CFuncs::CFunc_UnSetGoal);
REPLACE(0x005AED40, CFuncs::CFunc_GetGoal);
REPLACE(0x005AED70, CFuncs::CFunc_JustGotGoal);
REPLACE(0x005AEDA0, CFuncs::CFunc_SetFlag);
REPLACE(0x005AEDD0, CFuncs::CFunc_UnSetFlag);
REPLACE(0x005AEE30, CFuncs::CFunc_GetFlag);
REPLACE(0x005AEEA0, CFuncs::CFunc_JustGotFlag);
REPLACE(0x005AEED0, CFuncs::CFunc_SetGlobalFlag);
REPLACE(0x005AEF00, CFuncs::CFunc_UnSetGlobalFlag);
REPLACE(0x005AEF30, CFuncs::CFunc_GetGlobalFlag);
REPLACE(0x005AEF60, CFuncs::CFunc_ProfileEquals);
REPLACE(0x005AEFC0, CFuncs::CFunc_IsCareerMode);
REPLACE(0x005AEFE0, CFuncs::CFunc_EndRun);
REPLACE(0x005AEFF0, CFuncs::CFunc_ShouldEndRun);
REPLACE(0x005AF020, CFuncs::CFunc_EndRunSelected);
REPLACE(0x005AF030, CFuncs::CFunc_AllSkatersAreIdle);
REPLACE(0x005AF040, CFuncs::CFunc_FirstTrickStarted);
REPLACE(0x005AF050, CFuncs::CFunc_FirstTrickCompleted);
REPLACE(0x005AF060, CFuncs::CFunc_CalculateFinalScores);
