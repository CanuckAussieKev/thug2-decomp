// The goal manager's script commands, 0x00559980-0x0055A6FF (see goal_manager_cfuncs.h): goal names, selection,
// counters, goal points, prosets, the goals' timers and records, their pros' streams, the end of run, the
// temporary special tricks.

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module_calls.h"

namespace GoalSystem {

namespace {

using namespace GoalNames;

constexpr uint32_t kName = 0xA1DC81F9;            // Name
constexpr uint32_t kGoalType = 0x6D11ED74;        // Goal_Type
constexpr uint32_t kLevel = 0x651533EC;           // level
constexpr uint32_t kIsClassicMode = 0x5A2C589A;   // is_classic
constexpr uint32_t kGoalName = 0xBFECC45B;        // goal_name
constexpr uint32_t kLevelPrefix = 0x15F818D0;     // level_prefix
constexpr uint32_t kTotal = 0x3DD01EA1;           // total
constexpr uint32_t kGoalPoints = 0x42C20492;      // goal_points
constexpr uint32_t kProsetNotPrefix = 0xC60CE720; // proset_not_prefix
constexpr uint32_t kType = 0x7321A8D6;            // type
constexpr uint32_t kProStuff = 0x18653764;        // pro_stuff
constexpr uint32_t kShowGoalPoints = 0x888EF05E;  // GoalManager_ShowGoalPoints

// strcpy / strcat (no bounds, as the original).
void Copy(char* dest, const char* text)
{
    while ((*dest++ = *text++) != 0) {
    }
}
void Append(char* dest, const char* text) { Copy(dest + std::strlen(dest), text); }

CMatchRules* GameMode() { return g_gameMode; }
// The level structure being played (the skate module's +0x380 names it).
Scripts::CStruct* LevelStructure() { return Scripts::ReadSubStruct(SkateCalls::SkateField<uint32_t>(0x380)); }

uint32_t GoalName(Scripts::CStruct* params)
{
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    return name;
}

// The goal the command names, the manager fetched first (most commands) / after the name.
CObjective* ManagerThenGoal(Scripts::CStruct* params)
{
    CGoalManager* manager = GetObjectiveBoard();
    return manager->m_goals.Get(GoalName(params));
}
CObjective* NameThenGoal(Scripts::CStruct* params)
{
    uint32_t name = GoalName(params);
    return GetObjectiveBoard()->m_goals.Get(name);
}

} // namespace

// 0x00559980: the goal's id and name made from the level's name, _classic (classic, or a classic_mode_goal), _goal_
// and Goal_Type - into the script's goal_id / goal_name.
bool __cdecl CFunc_GoalManager_CreateGoalName(Scripts::CStruct* params, Scripts::CScript* script)
{
    const char* type;
    params->GetString(kGoalType, &type, true);
    const char* level;
    LevelStructure()->GetString(kLevel, &level, 1);
    char name[128];
    Copy(name, level);
    if (GameMode()->IsTrue(kIsClassicMode) || params->HasBareFlag(kClassicModeGoal))
        Append(name, "_classic"); // "_classic"
    Append(name, "_goal_");     // "_goal_"
    Append(name, type);
    script->m_params->PutNameKey(kGoalIdChecksum, Checksums::HashFromText(name));
    script->m_params->AddString(kGoalName, name);
    return true;
}

// 0x00559AF0: the level's name into the script's level_prefix.
bool __cdecl CFunc_GoalManager_GetLevelPrefix(Scripts::CStruct*, Scripts::CScript* script)
{
    const char* level;
    LevelStructure()->GetString(kLevel, &level, 1);
    script->m_params->AddString(kLevelPrefix, level);
    return true;
}

// 0x00559B40: the graffiti mode (the unnamed integer; 1 without one).
bool __cdecl CFunc_GoalManager_SetGraffitiMode(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    int mode = 1;
    params->FetchIntValue(0u, &mode, 0);
    manager->m_taggingStyle = mode;
    return true;
}

// 0x00559B80
bool __cdecl CFunc_GoalManager_HasWonGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    return static_cast<uint8_t>(manager->HasBeatenGoal(GoalName(params))) != 0; // (AL)
}

// 0x00559BC0
bool __cdecl CFunc_GoalManager_GoalIsSelected(Scripts::CStruct* params, Scripts::CScript*)
{
    return ManagerThenGoal(params)->IsSelected();
}

// 0x00559C10
bool __cdecl CFunc_GoalManager_ToggleGoalSelection(Scripts::CStruct* params, Scripts::CScript*)
{
    CObjective* goal = ManagerThenGoal(params);
    if (goal->IsSelected())
        goal->Deselect();
    else
        goal->Select();
    return true;
}

// 0x00559C80
bool __cdecl CFunc_GoalManager_DeselectAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->UnpickEveryObjective();
    return true;
}

// 0x00559CA0
bool __cdecl CFunc_GoalManager_GoalsAreSelected(Scripts::CStruct*, Scripts::CScript*)
{
    return GetObjectiveBoard()->CountPickedObjectives() > 0;
}

// 0x00559CC0: an object of the goal's counter got (if it's active). False without Name.
bool __cdecl CFunc_GoalManager_GotCounterObject(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name;
    if (!params->ReadHashValue(kName, &name, 0))
        return false;
    CObjective* goal = GetObjectiveBoard()->m_goals.Get(name);
    if (Virtual<bool>(goal, CObjective::kVtIsActive))
        goal->PickedUpCountable();
    return true;
}

// 0x00559D20
bool __cdecl CFunc_GoalManager_CounterGoalDone(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params)->TallyObjectiveFinished();
}

// 0x00559D70
bool __cdecl CFunc_GoalManager_AddGoalPoint(Scripts::CStruct*, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    ++manager->m_objectiveScore;
    ++manager->m_totalGoalPoints;
    Scripts::LaunchScript(kShowGoalPoints, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x00559DB0: the unnamed integer's goal points spent.
bool __cdecl CFunc_GoalManager_SpendGoalPoints(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    int points;
    params->FetchIntValue(0u, &points, 1);
    manager->m_objectiveScore -= points;
    return true;
}

// 0x00559DF0: at least the unnamed integer's goal points.
bool __cdecl CFunc_GoalManager_HasGoalPoints(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    int points;
    params->FetchIntValue(0u, &points, 1);
    return manager->m_objectiveScore >= points;
}

// 0x00559E30
bool __cdecl CFunc_GoalManager_ClearGoalPoints(Scripts::CStruct*, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    manager->m_objectiveScore = 0;
    manager->m_totalGoalPoints = 0;
    return true;
}

// 0x00559E50: the goal points (with total: all ever got) into the script's goal_points.
bool __cdecl CFunc_GoalManager_GetNumberOfGoalPoints(Scripts::CStruct* params, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    int points = params->HasBareFlag(kTotal) ? manager->m_totalGoalPoints : manager->m_objectiveScore;
    script->m_params->PutInt(kGoalPoints, points);
    return true;
}

// 0x00559E90: a goal of the proset named by the unnamed string is beaten.
bool __cdecl CFunc_GoalManager_HasBeatenGoalWithProset(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* prefix;
    params->GetString(0u, &prefix, 1);
    return GetObjectiveBoard()->HasWonProsetGoal(prefix);
}

// 0x00559EC0: the unnamed string without its last character, then NOT_ - into the script's proset_not_prefix.
// (An empty string reads the byte after its end, as the original.)
bool __cdecl CFunc_GoalManager_GetProsetNotPrefix(Scripts::CStruct* params, Scripts::CScript* script)
{
    const char* prefix;
    params->GetString(0u, &prefix, 1);
    char text[128];
    text[0] = 0;
    int length = 0;
    if (prefix[1]) {
        do {
            text[length] = prefix[length];
            ++length;
        } while (prefix[length + 1]);
    }
    text[length] = 0;
    Append(text, "NOT_"); // "NOT_"
    script->m_params->AddString(kProsetNotPrefix, text);
    return true;
}

// 0x00559F70: the saved state given to the level's goals.
bool __cdecl CFunc_GoalManager_LevelLoad(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->RestoreGoalData();
    return true;
}

// 0x00559F90
bool __cdecl CFunc_GoalManager_UnlockAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->OpenUpEveryObjective();
    return true;
}

// 0x00559FB0: stubbed in the game (a message only).
bool __cdecl CFunc_GoalManager_DeactivateCurrentGoal(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard();
    DebugOut::Printf("CGoalManager::DeactivateCurrentGoal stubbed\n"); // "CGoalManager::DeactivateCurrentGoal stubbed\n"
    return true;
}

// 0x00559FD0
bool __cdecl CFunc_GoalManager_SetStartTime(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->ResetCurrentTime();
    return true;
}

// 0x0055A020
bool __cdecl CFunc_GoalManager_UpdateComboTimer(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->UpdateCurrentTime();
    return true;
}

// 0x0055A070
bool __cdecl CFunc_GoalManager_SetStartHeight(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->SetLaunchAltitude();
    return true;
}

// 0x0055A0C0
bool __cdecl CFunc_GoalManager_CheckHeightRecord(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params)->TestHighestAir();
}

// 0x0055A110
bool __cdecl CFunc_GoalManager_CheckDistanceRecord(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params)->TestLongestJump();
}

// 0x0055A160: the goal's pro's start stream (with the command's parameters).
bool __cdecl CFunc_GoalManager_PlayGoalStartStream(Scripts::CStruct* params, Scripts::CScript*)
{
    ManagerThenGoal(params)->m_ped->PlayOpeningVoiceLine(params);
    return true;
}

// 0x0055A1B0: the goal's pro's wait stream.
bool __cdecl CFunc_GoalManager_PlayGoalWaitStream(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->m_ped->PlayIdleVoiceLine();
    return true;
}

// 0x0055A200: the goal's pro's win stream (with the command's parameters).
bool __cdecl CFunc_GoalManager_PlayGoalWinStream(Scripts::CStruct* params, Scripts::CScript*)
{
    ManagerThenGoal(params)->m_ped->PlayVictoryVoiceLine(params);
    return true;
}

// 0x0055A250: the goal's pro's stream stopped.
bool __cdecl CFunc_GoalManager_StopCurrentStream(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->m_ped->CutVoiceLine();
    return true;
}

// 0x0055A2A0: every locked pro goal unlocked.
bool __cdecl CFunc_GoalManager_TurnPro(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->UnlockAllProGoals();
    return true;
}

// 0x0055A2C0
bool __cdecl CFunc_GoalManager_PauseGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->Pause();
    return true;
}

// 0x0055A310
bool __cdecl CFunc_GoalManager_UnPauseGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->UnPause();
    return true;
}

// 0x0055A360 (the goal taken to be a competition)
bool __cdecl CFunc_GoalManager_PauseCompetition(Scripts::CStruct* params, Scripts::CScript*)
{
    reinterpret_cast<CContestObjective*>(NameThenGoal(params))->FreezeContest();
    return true;
}

// 0x0055A3B0
bool __cdecl CFunc_GoalManager_UnPauseCompetition(Scripts::CStruct* params, Scripts::CScript*)
{
    reinterpret_cast<CContestObjective*>(NameThenGoal(params))->ThawContest();
    return true;
}

// 0x0055A400
bool __cdecl CFunc_GoalManager_GoalIsLocked(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = GoalName(params);
    return static_cast<uint8_t>(GetObjectiveBoard()->IsGoalLocked(name)) != 0; // (AL)
}

// 0x0055A430: an active competition's goal_id into the script's. False if none.
bool __cdecl CFunc_GoalManager_IsInCompetition(Scripts::CStruct*, Scripts::CScript* script)
{
    CObjective* competition = GetObjectiveBoard()->GetActiveCompetition();
    if (!competition)
        return false;
    script->m_params->PutNameKey(kGoalIdChecksum, competition->GetObjectiveKey());
    return true;
}

// 0x0055A470: the goal's pro's animations of `type` as the script's pro_stuff (none: removed).
bool __cdecl CFunc_GoalManager_GetGoalAnimations(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = GoalName(params);
    const char* type = nullptr;
    params->GetString(kType, &type, 0);
    CObjective* goal = GetObjectiveBoard()->m_goals.Get(name);
    uint32_t animations = goal->m_ped->GetObjectiveAnimSets(type);
    script->m_params->RemoveComponent(kProStuff);
    if (animations)
        script->m_params->PutNameKey(kProStuff, animations);
    return true;
}

// 0x0055A500
bool __cdecl CFunc_GoalManager_PlayGoalStream(Scripts::CStruct* params, Scripts::CScript*)
{
    GetObjectiveBoard()->PlayObjectiveVoiceLine(params);
    return true;
}

// 0x0055A520
bool __cdecl CFunc_GoalManager_EndRunCalled(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params)->EndOfRunStarted();
}

// 0x0055A570
bool __cdecl CFunc_GoalManager_ClearEndRun(Scripts::CStruct* params, Scripts::CScript* script)
{
    NameThenGoal(params)->EndOfRunDone(script);
    return true;
}

// 0x0055A5C0
bool __cdecl CFunc_GoalManager_FinishedEndOfRun(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params)->RunDone();
}

// 0x0055A610
bool __cdecl CFunc_GoalManager_StartedEndOfRun(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params)->RunStarted();
}

// 0x0055A660
bool __cdecl CFunc_GoalManager_AddTempSpecialTrick(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params)->AwardSpecialTrick();
}

// 0x0055A6B0
bool __cdecl CFunc_GoalManager_RemoveTempSpecialTrick(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->RemoveRewardTrick();
    return true;
}

} // namespace Game
