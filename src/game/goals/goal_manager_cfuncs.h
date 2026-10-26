#pragma once

// The goal manager's script commands (GoalManager_..., 0x005591C0-0x0055B98F and 0x0055D3F0-0x0055E97F;
// goal_manager_cfuncs*.cpp): each on the skate module's goal manager (goal_manager.h), most on one goal - the one
// named by the command's Name (asserted). All are cdecl (params, script) and return whether the script's `if` should
// be taken (true where nothing is asked).

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Game {

// --- goal_manager_cfuncs.cpp: 0x005591C0-0x0055997F ---
bool __cdecl CFunc_GoalManager_EditGoal(Script::CStruct* params, Script::CScript* script);              // 0x005591C0
bool __cdecl CFunc_GoalManager_ActivateGoal(Script::CStruct* params, Script::CScript* script);          // 0x00559230
bool __cdecl CFunc_GoalManager_DeactivateGoal(Script::CStruct* params, Script::CScript* script);        // 0x00559290
bool __cdecl CFunc_GoalManager_ClearLastGoal(Script::CStruct* params, Script::CScript* script);         // 0x005592F0
bool __cdecl CFunc_GoalManager_WinGoal(Script::CStruct* params, Script::CScript* script);               // 0x00559310
bool __cdecl CFunc_GoalManager_LoseGoal(Script::CStruct* params, Script::CScript* script);              // 0x00559350
bool __cdecl CFunc_GoalManager_DeactivateAllGoals(Script::CStruct* params, Script::CScript* script);    // 0x00559390
bool __cdecl CFunc_GoalManager_ActivateAllGoals(Script::CStruct* params, Script::CScript* script);      // 0x005593D0
bool __cdecl CFunc_GoalManager_UninitializeAllGoals(Script::CStruct* params, Script::CScript* script);  // 0x005593F0
bool __cdecl CFunc_GoalManager_UninitializeGoal(Script::CStruct* params, Script::CScript* script);      // 0x00559410
bool __cdecl CFunc_GoalManager_InitializeAllGoals(Script::CStruct* params, Script::CScript* script);    // 0x00559490
bool __cdecl CFunc_GoalManager_InitializeAllSelectedGoals(Script::CStruct* params, Script::CScript* script); // 0x005594B0
bool __cdecl CFunc_GoalManager_ActivateAllSelectedGoals(Script::CStruct* params, Script::CScript* script);   // 0x005594D0
bool __cdecl CFunc_GoalManager_GoalIsActive(Script::CStruct* params, Script::CScript* script);          // 0x005594F0
bool __cdecl CFunc_GoalManager_SetGoalTimer(Script::CStruct* params, Script::CScript* script);          // 0x00559540
bool __cdecl CFunc_GoalManager_ZeroGoalTimer(Script::CStruct* params, Script::CScript* script);         // 0x005595D0
bool __cdecl CFunc_GoalManager_PauseAllGoals(Script::CStruct* params, Script::CScript* script);         // 0x00559630
bool __cdecl CFunc_GoalManager_UnPauseAllGoals(Script::CStruct* params, Script::CScript* script);       // 0x00559650
bool __cdecl CFunc_GoalManager_SetGoalFlag(Script::CStruct* params, Script::CScript* script);           // 0x00559670
bool __cdecl CFunc_GoalManager_HasSeenGoal(Script::CStruct* params, Script::CScript* script);           // 0x00559720
bool __cdecl CFunc_GoalManager_AllFlagsSet(Script::CStruct* params, Script::CScript* script);           // 0x00559770
bool __cdecl CFunc_GoalManager_GoalFlagSet(Script::CStruct* params, Script::CScript* script);           // 0x005597C0
bool __cdecl CFunc_GoalManager_CanRetryGoal(Script::CStruct* params, Script::CScript* script);          // 0x00559850
bool __cdecl CFunc_GoalManager_GetGoalParams(Script::CStruct* params, Script::CScript* script);         // 0x00559870
bool __cdecl CFunc_GoalManager_CanStartGoal(Script::CStruct* params, Script::CScript* script);          // 0x005598D0
bool __cdecl CFunc_GoalManager_NextRaceWaypoint(Script::CStruct* params, Script::CScript* script);      // 0x005598E0
// 0x00559930: GoalManager_NextHorseSpot and GoalManager_NextTourSpot.
bool __cdecl CFunc_GoalManager_NextWaypoint(Script::CStruct* params, Script::CScript* script);

// --- goal_manager_cfuncs2.cpp: 0x00559980-0x0055A6FF ---
bool __cdecl CFunc_GoalManager_CreateGoalName(Script::CStruct* params, Script::CScript* script); // 0x00559980
bool __cdecl CFunc_GoalManager_GetLevelPrefix(Script::CStruct* params, Script::CScript* script); // 0x00559AF0
bool __cdecl CFunc_GoalManager_SetGraffitiMode(Script::CStruct* params, Script::CScript* script); // 0x00559B40
bool __cdecl CFunc_GoalManager_HasWonGoal(Script::CStruct* params, Script::CScript* script); // 0x00559B80
bool __cdecl CFunc_GoalManager_GoalIsSelected(Script::CStruct* params, Script::CScript* script); // 0x00559BC0
bool __cdecl CFunc_GoalManager_ToggleGoalSelection(Script::CStruct* params, Script::CScript* script); // 0x00559C10
bool __cdecl CFunc_GoalManager_DeselectAllGoals(Script::CStruct* params, Script::CScript* script); // 0x00559C80
bool __cdecl CFunc_GoalManager_GoalsAreSelected(Script::CStruct* params, Script::CScript* script); // 0x00559CA0
bool __cdecl CFunc_GoalManager_GotCounterObject(Script::CStruct* params, Script::CScript* script); // 0x00559CC0
bool __cdecl CFunc_GoalManager_CounterGoalDone(Script::CStruct* params, Script::CScript* script); // 0x00559D20
bool __cdecl CFunc_GoalManager_AddGoalPoint(Script::CStruct* params, Script::CScript* script); // 0x00559D70
bool __cdecl CFunc_GoalManager_SpendGoalPoints(Script::CStruct* params, Script::CScript* script); // 0x00559DB0
bool __cdecl CFunc_GoalManager_HasGoalPoints(Script::CStruct* params, Script::CScript* script); // 0x00559DF0
bool __cdecl CFunc_GoalManager_ClearGoalPoints(Script::CStruct* params, Script::CScript* script); // 0x00559E30
bool __cdecl CFunc_GoalManager_GetNumberOfGoalPoints(Script::CStruct* params, Script::CScript* script); // 0x00559E50
bool __cdecl CFunc_GoalManager_HasBeatenGoalWithProset(Script::CStruct* params, Script::CScript* script); // 0x00559E90
bool __cdecl CFunc_GoalManager_GetProsetNotPrefix(Script::CStruct* params, Script::CScript* script); // 0x00559EC0
bool __cdecl CFunc_GoalManager_LevelLoad(Script::CStruct* params, Script::CScript* script); // 0x00559F70
bool __cdecl CFunc_GoalManager_UnlockAllGoals(Script::CStruct* params, Script::CScript* script); // 0x00559F90
bool __cdecl CFunc_GoalManager_DeactivateCurrentGoal(Script::CStruct* params, Script::CScript* script); // 0x00559FB0
bool __cdecl CFunc_GoalManager_SetStartTime(Script::CStruct* params, Script::CScript* script); // 0x00559FD0
bool __cdecl CFunc_GoalManager_UpdateComboTimer(Script::CStruct* params, Script::CScript* script); // 0x0055A020
bool __cdecl CFunc_GoalManager_SetStartHeight(Script::CStruct* params, Script::CScript* script); // 0x0055A070
bool __cdecl CFunc_GoalManager_CheckHeightRecord(Script::CStruct* params, Script::CScript* script); // 0x0055A0C0
bool __cdecl CFunc_GoalManager_CheckDistanceRecord(Script::CStruct* params, Script::CScript* script); // 0x0055A110
bool __cdecl CFunc_GoalManager_PlayGoalStartStream(Script::CStruct* params, Script::CScript* script); // 0x0055A160
bool __cdecl CFunc_GoalManager_PlayGoalWaitStream(Script::CStruct* params, Script::CScript* script); // 0x0055A1B0
bool __cdecl CFunc_GoalManager_PlayGoalWinStream(Script::CStruct* params, Script::CScript* script); // 0x0055A200
bool __cdecl CFunc_GoalManager_StopCurrentStream(Script::CStruct* params, Script::CScript* script); // 0x0055A250
bool __cdecl CFunc_GoalManager_TurnPro(Script::CStruct* params, Script::CScript* script); // 0x0055A2A0
bool __cdecl CFunc_GoalManager_PauseGoal(Script::CStruct* params, Script::CScript* script); // 0x0055A2C0
bool __cdecl CFunc_GoalManager_UnPauseGoal(Script::CStruct* params, Script::CScript* script); // 0x0055A310
bool __cdecl CFunc_GoalManager_PauseCompetition(Script::CStruct* params, Script::CScript* script); // 0x0055A360
bool __cdecl CFunc_GoalManager_UnPauseCompetition(Script::CStruct* params, Script::CScript* script); // 0x0055A3B0
bool __cdecl CFunc_GoalManager_GoalIsLocked(Script::CStruct* params, Script::CScript* script); // 0x0055A400
bool __cdecl CFunc_GoalManager_IsInCompetition(Script::CStruct* params, Script::CScript* script); // 0x0055A430
bool __cdecl CFunc_GoalManager_GetGoalAnimations(Script::CStruct* params, Script::CScript* script); // 0x0055A470
bool __cdecl CFunc_GoalManager_PlayGoalStream(Script::CStruct* params, Script::CScript* script); // 0x0055A500
bool __cdecl CFunc_GoalManager_EndRunCalled(Script::CStruct* params, Script::CScript* script); // 0x0055A520
bool __cdecl CFunc_GoalManager_ClearEndRun(Script::CStruct* params, Script::CScript* script); // 0x0055A570
bool __cdecl CFunc_GoalManager_FinishedEndOfRun(Script::CStruct* params, Script::CScript* script); // 0x0055A5C0
bool __cdecl CFunc_GoalManager_StartedEndOfRun(Script::CStruct* params, Script::CScript* script); // 0x0055A610
bool __cdecl CFunc_GoalManager_AddTempSpecialTrick(Script::CStruct* params, Script::CScript* script); // 0x0055A660
bool __cdecl CFunc_GoalManager_RemoveTempSpecialTrick(Script::CStruct* params, Script::CScript* script); // 0x0055A6B0

// --- goal_manager_cfuncs3.cpp: 0x0055A700-0x0055B98F ---
bool __cdecl CFunc_GoalManager_GetTrickFromKeyCombo(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_UnlockGoal(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_QuickStartGoal(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_InitializeGoal(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GoalIsExpired(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_ExpireGoal(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_AddTime(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetNumberCollected(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetNumberOfFlags(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_IsPro(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_ResetGoalFlags(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_ColorTrickObjects(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetNumberOfTimesGoalStarted(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GoalExists(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetCanStartGoal(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetLastGoalId(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_ClearTetrisTricks(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_AnnounceGoalStarted(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetEndRunType(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetShouldDeactivateOnExpire(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_ResetCareer(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_UpdateFamilyTrees(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SuspendGoalPedLogic(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetDifficultyLevel(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetDifficultyLevel(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_RestartStage(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_CanRestartStage(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetGoalChaptersAndStages(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_AdvanceStage(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_FilmGoalCheckpoint(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_StartFilming(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetSponsor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetSponsor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetTeam(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetTeamMember(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_KillTeamMembers(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetTeamName(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_RunLastStageScript(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GoalInitialized(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_UnloadLastFam(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_StopLastStream(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_HideAllGoalPeds(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_SetTrickBeatPattern(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_ResetTrickBeatTimer(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetGoalManagerParams(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetActiveRewardPoints(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GoalManager_GetGoalRewardPoints(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Goal_CollectActive(Script::CStruct* params, Script::CScript* script);

// --- goal_manager_events.cpp: 0x0055D3F0-0x0055D954 ---
bool __cdecl CFunc_GoalManager_AddGoal(Script::CStruct* params, Script::CScript* script);               // 0x0055D3F0
bool __cdecl CFunc_GoalManager_RemoveGoal(Script::CStruct* params, Script::CScript* script);            // 0x0055D450
bool __cdecl CFunc_GoalManager_RemoveAllGoals(Script::CStruct* params, Script::CScript* script);        // 0x0055D4D0
bool __cdecl CFunc_GoalManager_RestartLastGoal(Script::CStruct* params, Script::CScript* script);       // 0x0055D4F0
bool __cdecl CFunc_GoalManager_LevelUnload(Script::CStruct* params, Script::CScript* script);           // 0x0055D510
bool __cdecl CFunc_GoalManager_NumGoalsBeatenInLevel(Script::CStruct* params, Script::CScript* script); // 0x0055D530
bool __cdecl CFunc_GoalManager_AddGoalChoices(Script::CStruct* params, Script::CScript* script);        // 0x0055D590
bool __cdecl CFunc_GoalManager_GetGoalList(Script::CStruct* params, Script::CScript* script);           // 0x0055D600
bool __cdecl CFunc_GoalManager_ReplaceTrickText(Script::CStruct* params, Script::CScript* script);      // 0x0055D7D0
bool __cdecl CFunc_GoalManager_GetGoalAttemptInfo(Script::CStruct* params, Script::CScript* script);    // 0x0055D8E0
bool __cdecl CFunc_GoalManager_GetViewGoalsInfo(Script::CStruct* params, Script::CScript* script);      // 0x0055D910

// --- goal_manager_update.cpp: 0x0055E860-0x0055EB3F ---
bool __cdecl CFunc_GoalManager_HasActiveGoals(Script::CStruct* params, Script::CScript* script);            // 0x0055E860
bool __cdecl CFunc_GoalManager_UnBeatAllGoals(Script::CStruct* params, Script::CScript* script);            // 0x0055E8A0
bool __cdecl CFunc_GoalManager_AddViewGoalsList(Script::CStruct* params, Script::CScript* script);          // 0x0055E8C0
bool __cdecl CFunc_GoalManager_GoalShouldExpire(Script::CStruct* params, Script::CScript* script);          // 0x0055E8E0
bool __cdecl CFunc_GoalManager_UpdateAllGoals(Script::CStruct* params, Script::CScript* script);            // 0x0055EAC0
bool __cdecl CFunc_GoalManager_GetTimeLeftInNetGame(Script::CStruct* params, Script::CScript* script);      // 0x0055EAE0
bool __cdecl CFunc_GoalManager_GetUnlimitedTimeInNetGame(Script::CStruct* params, Script::CScript* script); // 0x0055EB20

} // namespace Game
