// The goal manager's script commands, 0x005591C0-0x0055997F (see goal_manager_cfuncs.h): starting, stopping,
// winning and losing goals, their timers and flags.

#include <cstdint>

#include "game/goals/goal_manager_cfuncs.h"

#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_net_types.h"
#include "game/goals/goal_names.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"

namespace GoalSystem {

namespace {

constexpr uint32_t kName = 0xA1DC81F9;       // Name
constexpr uint32_t kParams = 0x7031F10C;     // params
constexpr uint32_t kDontAssert = 0x9AC8D54F; // DontAssert
constexpr uint32_t kForceAll = 0xCC365298;   // force_all
constexpr uint32_t kWholeTree = 0x687DE558;  // (name unknown: the goal's whole chain)
constexpr uint32_t kTime = 0x906B67BA;       // time
constexpr uint32_t kSkater = 0x5B8AB877;     // skater
constexpr uint32_t kFlag = 0x2E0B1465;       // flag

// The goal the command's Name names (asserted), and that name.
uint32_t GoalName(Scripts::CStruct* params)
{
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    return name;
}

} // namespace

// 0x005591C0
bool __cdecl CFunc_GoalManager_EditGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name = GoalName(params);
    Scripts::CStruct* edits;
    params->ReadSubStruct(kParams, &edits, 1);
    CObjective* goal = manager->m_goals.Get(name);
    if (goal)
        goal->RewriteArgs(edits);
    return goal != nullptr;
}

// 0x00559230
bool __cdecl CFunc_GoalManager_ActivateGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name = GoalName(params);
    bool dontAssert = params->HasBareFlag(kDontAssert);
    return manager->EngageObjective(name, dontAssert);
}

// 0x00559290
bool __cdecl CFunc_GoalManager_DeactivateGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    CObjective* goal = manager->m_goals.Get(GoalName(params));
    if (goal)
        Virtual<bool>(goal, CObjective::kVtDeactivate, false, true);
    return goal != nullptr;
}

// 0x005592F0
bool __cdecl CFunc_GoalManager_ClearLastGoal(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->m_previousObjective = 0;
    return true;
}

// 0x00559310
bool __cdecl CFunc_GoalManager_WinGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    return manager->CompleteObjective(GoalName(params));
}

// 0x00559350
bool __cdecl CFunc_GoalManager_LoseGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    return manager->FailObjective(GoalName(params));
}

// 0x00559390
bool __cdecl CFunc_GoalManager_DeactivateAllGoals(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    manager->DisengageEveryObjective(params->HasEntryNamed(kForceAll));
    return true;
}

// 0x005593D0
bool __cdecl CFunc_GoalManager_ActivateAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->ActivateAllGoals();
    return true;
}

// 0x005593F0
bool __cdecl CFunc_GoalManager_UninitializeAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->ReleaseEveryObjective();
    return true;
}

// 0x00559410: with the flag 0x687DE558 the goal's whole chain, else the goal (one not in the level is read through
// null, as the original).
bool __cdecl CFunc_GoalManager_UninitializeGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name = GoalName(params);
    if (params->HasBareFlag(kWholeTree)) {
        manager->ReleaseObjectiveTree(name);
        return true;
    }
    manager->m_goals.Get(name)->Uninitialize(false, 0);
    return true;
}

// 0x00559490
bool __cdecl CFunc_GoalManager_InitializeAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->PrepareEveryObjective();
    return true;
}

// 0x005594B0
bool __cdecl CFunc_GoalManager_InitializeAllSelectedGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->PreparePickedObjectives();
    return true;
}

// 0x005594D0
bool __cdecl CFunc_GoalManager_ActivateAllSelectedGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->ActivateAllSelectedGoals();
    return true;
}

// 0x005594F0
bool __cdecl CFunc_GoalManager_GoalIsActive(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    CObjective* goal = manager->m_goals.Get(GoalName(params));
    return goal ? Virtual<bool>(goal, CObjective::kVtIsActive) : false;
}

// 0x00559540: the timer set to `time` seconds - without one, started again from the goal's own time.
bool __cdecl CFunc_GoalManager_SetGoalTimer(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name = GoalName(params);
    int time = -1;
    params->FetchIntValue(kTime, &time, 0);
    CObjective* goal = manager->m_goals.Get(name);
    if (goal) {
        if (time == -1)
            goal->ResetTimer();
        else
            goal->SetSecondsRemaining(time);
    }
    return goal != nullptr;
}

// 0x005595D0
bool __cdecl CFunc_GoalManager_ZeroGoalTimer(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    CObjective* goal = manager->m_goals.Get(GoalName(params));
    if (goal)
        goal->SetSecondsRemaining(0);
    return goal != nullptr;
}

// 0x00559630
bool __cdecl CFunc_GoalManager_PauseAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->FreezeEveryObjective();
    return true;
}

// 0x00559650
bool __cdecl CFunc_GoalManager_UnPauseAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->ThawEveryObjective();
    return true;
}

// 0x00559670: the goal's flag (the unnamed checksum) set to the unnamed integer - of `skater`'s flags, if given.
bool __cdecl CFunc_GoalManager_SetGoalFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name = GoalName(params);
    uint32_t flag;
    params->ReadHashValue(0u, &flag, 1);
    int value;
    params->FetchIntValue(0u, &value, 1);
    uint32_t skater = 0;
    params->ReadHashValue(kSkater, &skater, 0);
    CObjective* goal = manager->m_goals.Get(name);
    if (!goal)
        return false;
    return Virtual<bool>(goal, CObjective::kVtSetFlag, flag, value, skater);
}

// 0x00559720
bool __cdecl CFunc_GoalManager_HasSeenGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    CObjective* goal = manager->m_goals.Get(GoalName(params));
    return goal ? goal->HasSeen() : false;
}

// 0x00559770 (a goal not in the level is read through null, as the original)
bool __cdecl CFunc_GoalManager_AllFlagsSet(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    return manager->m_goals.Get(GoalName(params))->HasEveryBit();
}

// 0x005597C0: the goal's `flag` is set (of `skater`'s flags, if given).
bool __cdecl CFunc_GoalManager_GoalFlagSet(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name = GoalName(params);
    uint32_t flag;
    params->ReadHashValue(kFlag, &flag, 1);
    uint32_t skater = 0;
    params->ReadHashValue(kSkater, &skater, 0);
    return manager->m_goals.Get(name)->BitIsOn(flag, skater);
}

// 0x00559850: there is a last goal to retry.
bool __cdecl CFunc_GoalManager_CanRetryGoal(Scripts::CStruct*, Scripts::CScript*)
{
    return GetObjectiveBoard()->m_previousObjective != 0;
}

// 0x00559870: the goal's parameters added to the script's.
bool __cdecl CFunc_GoalManager_GetGoalParams(Scripts::CStruct* params, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    CObjective* goal = manager->m_goals.Get(GoalName(params));
    script->m_params->AppendStructureIfAny(goal->GetParams());
    return true;
}

// 0x005598D0
bool __cdecl CFunc_GoalManager_CanStartGoal(Scripts::CStruct*, Scripts::CScript*)
{
    return GetObjectiveBoard()->m_startAllowed;
}

// 0x005598E0: the race goal's next waypoint (the goal taken to be a race).
bool __cdecl CFunc_GoalManager_NextRaceWaypoint(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name = GoalName(params);
    CObjective* goal = manager->m_goals.Get(name);
    return static_cast<CRaceObjective*>(goal)->AdvanceRaceGate(name);
}

// 0x00559930
bool __cdecl CFunc_GoalManager_NextWaypoint(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    return manager->m_goals.Get(GoalName(params))->NextWaypoint();
}

} // namespace Game
