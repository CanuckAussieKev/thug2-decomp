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

namespace Game {

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
uint32_t GoalName(Script::CStruct* params)
{
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    return name;
}

} // namespace

// 0x005591C0
bool __cdecl CFunc_GoalManager_EditGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name = GoalName(params);
    Script::CStruct* edits;
    params->GetStructure(kParams, &edits, 1);
    CGoal* goal = manager->m_goals.Get(name);
    if (goal)
        goal->EditParams(edits);
    return goal != nullptr;
}

// 0x00559230
bool __cdecl CFunc_GoalManager_ActivateGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name = GoalName(params);
    bool dontAssert = params->ContainsFlag(kDontAssert);
    return manager->ActivateGoal(name, dontAssert);
}

// 0x00559290
bool __cdecl CFunc_GoalManager_DeactivateGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    CGoal* goal = manager->m_goals.Get(GoalName(params));
    if (goal)
        Virtual<bool>(goal, CGoal::kVtDeactivate, false, true);
    return goal != nullptr;
}

// 0x005592F0
bool __cdecl CFunc_GoalManager_ClearLastGoal(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->m_lastGoal = 0;
    return true;
}

// 0x00559310
bool __cdecl CFunc_GoalManager_WinGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    return manager->WinGoal(GoalName(params));
}

// 0x00559350
bool __cdecl CFunc_GoalManager_LoseGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    return manager->LoseGoal(GoalName(params));
}

// 0x00559390
bool __cdecl CFunc_GoalManager_DeactivateAllGoals(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    manager->DeactivateAllGoals(params->ContainsComponentNamed(kForceAll));
    return true;
}

// 0x005593D0
bool __cdecl CFunc_GoalManager_ActivateAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->ActivateAllGoals();
    return true;
}

// 0x005593F0
bool __cdecl CFunc_GoalManager_UninitializeAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->UninitializeAllGoals();
    return true;
}

// 0x00559410: with the flag 0x687DE558 the goal's whole chain, else the goal (one not in the level is read through
// null, as the original).
bool __cdecl CFunc_GoalManager_UninitializeGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name = GoalName(params);
    if (params->ContainsFlag(kWholeTree)) {
        manager->UninitializeGoalTree(name);
        return true;
    }
    manager->m_goals.Get(name)->Uninitialize(false, 0);
    return true;
}

// 0x00559490
bool __cdecl CFunc_GoalManager_InitializeAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->InitializeAllGoals();
    return true;
}

// 0x005594B0
bool __cdecl CFunc_GoalManager_InitializeAllSelectedGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->InitializeAllSelectedGoals();
    return true;
}

// 0x005594D0
bool __cdecl CFunc_GoalManager_ActivateAllSelectedGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->ActivateAllSelectedGoals();
    return true;
}

// 0x005594F0
bool __cdecl CFunc_GoalManager_GoalIsActive(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    CGoal* goal = manager->m_goals.Get(GoalName(params));
    return goal ? Virtual<bool>(goal, CGoal::kVtIsActive) : false;
}

// 0x00559540: the timer set to `time` seconds - without one, started again from the goal's own time.
bool __cdecl CFunc_GoalManager_SetGoalTimer(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name = GoalName(params);
    int time = -1;
    params->GetInteger(kTime, &time, 0);
    CGoal* goal = manager->m_goals.Get(name);
    if (goal) {
        if (time == -1)
            goal->ResetTimer();
        else
            goal->SetTimeLeft(time);
    }
    return goal != nullptr;
}

// 0x005595D0
bool __cdecl CFunc_GoalManager_ZeroGoalTimer(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    CGoal* goal = manager->m_goals.Get(GoalName(params));
    if (goal)
        goal->SetTimeLeft(0);
    return goal != nullptr;
}

// 0x00559630
bool __cdecl CFunc_GoalManager_PauseAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->PauseAllGoals();
    return true;
}

// 0x00559650
bool __cdecl CFunc_GoalManager_UnPauseAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->UnPauseAllGoals();
    return true;
}

// 0x00559670: the goal's flag (the unnamed checksum) set to the unnamed integer - of `skater`'s flags, if given.
bool __cdecl CFunc_GoalManager_SetGoalFlag(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name = GoalName(params);
    uint32_t flag;
    params->GetChecksum(0u, &flag, 1);
    int value;
    params->GetInteger(0u, &value, 1);
    uint32_t skater = 0;
    params->GetChecksum(kSkater, &skater, 0);
    CGoal* goal = manager->m_goals.Get(name);
    if (!goal)
        return false;
    return Virtual<bool>(goal, CGoal::kVtSetFlag, flag, value, skater);
}

// 0x00559720
bool __cdecl CFunc_GoalManager_HasSeenGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    CGoal* goal = manager->m_goals.Get(GoalName(params));
    return goal ? goal->HasSeen() : false;
}

// 0x00559770 (a goal not in the level is read through null, as the original)
bool __cdecl CFunc_GoalManager_AllFlagsSet(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    return manager->m_goals.Get(GoalName(params))->AllFlagsSet();
}

// 0x005597C0: the goal's `flag` is set (of `skater`'s flags, if given).
bool __cdecl CFunc_GoalManager_GoalFlagSet(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name = GoalName(params);
    uint32_t flag;
    params->GetChecksum(kFlag, &flag, 1);
    uint32_t skater = 0;
    params->GetChecksum(kSkater, &skater, 0);
    return manager->m_goals.Get(name)->GoalFlagSet(flag, skater);
}

// 0x00559850: there is a last goal to retry.
bool __cdecl CFunc_GoalManager_CanRetryGoal(Script::CStruct*, Script::CScript*)
{
    return GetGoalManager()->m_lastGoal != 0;
}

// 0x00559870: the goal's parameters added to the script's.
bool __cdecl CFunc_GoalManager_GetGoalParams(Script::CStruct* params, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    CGoal* goal = manager->m_goals.Get(GoalName(params));
    script->m_params->AppendStructureIfAny(goal->GetParams());
    return true;
}

// 0x005598D0
bool __cdecl CFunc_GoalManager_CanStartGoal(Script::CStruct*, Script::CScript*)
{
    return GetGoalManager()->m_canStartGoal;
}

// 0x005598E0: the race goal's next waypoint (the goal taken to be a race).
bool __cdecl CFunc_GoalManager_NextRaceWaypoint(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name = GoalName(params);
    CGoal* goal = manager->m_goals.Get(name);
    return static_cast<CRaceGoal*>(goal)->NextRaceWaypoint(name);
}

// 0x00559930
bool __cdecl CFunc_GoalManager_NextWaypoint(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    return manager->m_goals.Get(GoalName(params))->NextWaypoint();
}

} // namespace Game
