#pragma once

// The goal manager's script commands (GoalManager_..., 0x005591C0-0x0055B98F and 0x0055D3F0-0x0055E97F;
// goal_manager_cfuncs*.cpp): each on the skate module's goal manager (goal_manager.h), most on one goal - the one
// named by the command's Name (asserted). All are cdecl (params, script) and return whether the script's `if` should
// be taken (true where nothing is asked).

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace GoalSystem {

// --- goal_manager_cfuncs.cpp: 0x005591C0-0x0055997F ---
bool __cdecl CFunc_GoalManager_EditGoal(Scripts::CStruct* params, Scripts::CScript* script);              // 0x005591C0
bool __cdecl CFunc_GoalManager_ActivateGoal(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00559230
bool __cdecl CFunc_GoalManager_DeactivateGoal(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00559290
bool __cdecl CFunc_GoalManager_ClearLastGoal(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005592F0
bool __cdecl CFunc_GoalManager_WinGoal(Scripts::CStruct* params, Scripts::CScript* script);               // 0x00559310
bool __cdecl CFunc_GoalManager_LoseGoal(Scripts::CStruct* params, Scripts::CScript* script);              // 0x00559350
bool __cdecl CFunc_GoalManager_DeactivateAllGoals(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00559390
bool __cdecl CFunc_GoalManager_ActivateAllGoals(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005593D0
bool __cdecl CFunc_GoalManager_UninitializeAllGoals(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005593F0
bool __cdecl CFunc_GoalManager_UninitializeGoal(Scripts::CStruct* params, Scripts::CScript* script);      // 0x00559410
bool __cdecl CFunc_GoalManager_InitializeAllGoals(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00559490
bool __cdecl CFunc_GoalManager_InitializeAllSelectedGoals(Scripts::CStruct* params, Scripts::CScript* script); // 0x005594B0
bool __cdecl CFunc_GoalManager_ActivateAllSelectedGoals(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005594D0
bool __cdecl CFunc_GoalManager_GoalIsActive(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005594F0
bool __cdecl CFunc_GoalManager_SetGoalTimer(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00559540
bool __cdecl CFunc_GoalManager_ZeroGoalTimer(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005595D0
bool __cdecl CFunc_GoalManager_PauseAllGoals(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00559630
bool __cdecl CFunc_GoalManager_UnPauseAllGoals(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00559650
bool __cdecl CFunc_GoalManager_SetGoalFlag(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00559670
bool __cdecl CFunc_GoalManager_HasSeenGoal(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00559720
bool __cdecl CFunc_GoalManager_AllFlagsSet(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00559770
bool __cdecl CFunc_GoalManager_GoalFlagSet(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005597C0
bool __cdecl CFunc_GoalManager_CanRetryGoal(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00559850
bool __cdecl CFunc_GoalManager_GetGoalParams(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00559870
bool __cdecl CFunc_GoalManager_CanStartGoal(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005598D0
bool __cdecl CFunc_GoalManager_NextRaceWaypoint(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005598E0
// 0x00559930: GoalManager_NextHorseSpot and GoalManager_NextTourSpot.
bool __cdecl CFunc_GoalManager_NextWaypoint(Scripts::CStruct* params, Scripts::CScript* script);

// --- goal_manager_cfuncs2.cpp: 0x00559980-0x0055A6FF ---
bool __cdecl CFunc_GoalManager_CreateGoalName(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559980
bool __cdecl CFunc_GoalManager_GetLevelPrefix(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559AF0
bool __cdecl CFunc_GoalManager_SetGraffitiMode(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559B40
bool __cdecl CFunc_GoalManager_HasWonGoal(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559B80
bool __cdecl CFunc_GoalManager_GoalIsSelected(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559BC0
bool __cdecl CFunc_GoalManager_ToggleGoalSelection(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559C10
bool __cdecl CFunc_GoalManager_DeselectAllGoals(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559C80
bool __cdecl CFunc_GoalManager_GoalsAreSelected(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559CA0
bool __cdecl CFunc_GoalManager_GotCounterObject(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559CC0
bool __cdecl CFunc_GoalManager_CounterGoalDone(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559D20
bool __cdecl CFunc_GoalManager_AddGoalPoint(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559D70
bool __cdecl CFunc_GoalManager_SpendGoalPoints(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559DB0
bool __cdecl CFunc_GoalManager_HasGoalPoints(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559DF0
bool __cdecl CFunc_GoalManager_ClearGoalPoints(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559E30
bool __cdecl CFunc_GoalManager_GetNumberOfGoalPoints(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559E50
bool __cdecl CFunc_GoalManager_HasBeatenGoalWithProset(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559E90
bool __cdecl CFunc_GoalManager_GetProsetNotPrefix(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559EC0
bool __cdecl CFunc_GoalManager_LevelLoad(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559F70
bool __cdecl CFunc_GoalManager_UnlockAllGoals(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559F90
bool __cdecl CFunc_GoalManager_DeactivateCurrentGoal(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559FB0
bool __cdecl CFunc_GoalManager_SetStartTime(Scripts::CStruct* params, Scripts::CScript* script); // 0x00559FD0
bool __cdecl CFunc_GoalManager_UpdateComboTimer(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A020
bool __cdecl CFunc_GoalManager_SetStartHeight(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A070
bool __cdecl CFunc_GoalManager_CheckHeightRecord(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A0C0
bool __cdecl CFunc_GoalManager_CheckDistanceRecord(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A110
bool __cdecl CFunc_GoalManager_PlayGoalStartStream(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A160
bool __cdecl CFunc_GoalManager_PlayGoalWaitStream(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A1B0
bool __cdecl CFunc_GoalManager_PlayGoalWinStream(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A200
bool __cdecl CFunc_GoalManager_StopCurrentStream(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A250
bool __cdecl CFunc_GoalManager_TurnPro(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A2A0
bool __cdecl CFunc_GoalManager_PauseGoal(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A2C0
bool __cdecl CFunc_GoalManager_UnPauseGoal(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A310
bool __cdecl CFunc_GoalManager_PauseCompetition(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A360
bool __cdecl CFunc_GoalManager_UnPauseCompetition(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A3B0
bool __cdecl CFunc_GoalManager_GoalIsLocked(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A400
bool __cdecl CFunc_GoalManager_IsInCompetition(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A430
bool __cdecl CFunc_GoalManager_GetGoalAnimations(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A470
bool __cdecl CFunc_GoalManager_PlayGoalStream(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A500
bool __cdecl CFunc_GoalManager_EndRunCalled(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A520
bool __cdecl CFunc_GoalManager_ClearEndRun(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A570
bool __cdecl CFunc_GoalManager_FinishedEndOfRun(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A5C0
bool __cdecl CFunc_GoalManager_StartedEndOfRun(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A610
bool __cdecl CFunc_GoalManager_AddTempSpecialTrick(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A660
bool __cdecl CFunc_GoalManager_RemoveTempSpecialTrick(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055A6B0

// --- goal_manager_cfuncs3.cpp: 0x0055A700-0x0055B98F ---
bool __cdecl CFunc_GoalManager_GetTrickFromKeyCombo(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_UnlockGoal(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_QuickStartGoal(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_InitializeGoal(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GoalIsExpired(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_ExpireGoal(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_AddTime(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetNumberCollected(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetNumberOfFlags(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_IsPro(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_ResetGoalFlags(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_ColorTrickObjects(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetNumberOfTimesGoalStarted(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GoalExists(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetCanStartGoal(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetLastGoalId(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_ClearTetrisTricks(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_AnnounceGoalStarted(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetEndRunType(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetShouldDeactivateOnExpire(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_ResetCareer(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_UpdateFamilyTrees(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SuspendGoalPedLogic(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetDifficultyLevel(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetDifficultyLevel(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_RestartStage(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_CanRestartStage(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetGoalChaptersAndStages(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_AdvanceStage(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_FilmGoalCheckpoint(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_StartFilming(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetSponsor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetSponsor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetTeam(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetTeamMember(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_KillTeamMembers(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetTeamName(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_RunLastStageScript(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GoalInitialized(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_UnloadLastFam(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_StopLastStream(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_HideAllGoalPeds(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_SetTrickBeatPattern(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_ResetTrickBeatTimer(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetGoalManagerParams(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetActiveRewardPoints(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GoalManager_GetGoalRewardPoints(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Goal_CollectActive(Scripts::CStruct* params, Scripts::CScript* script);

// --- goal_manager_events.cpp: 0x0055D3F0-0x0055D954 ---
bool __cdecl CFunc_GoalManager_AddGoal(Scripts::CStruct* params, Scripts::CScript* script);               // 0x0055D3F0
bool __cdecl CFunc_GoalManager_RemoveGoal(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0055D450
bool __cdecl CFunc_GoalManager_RemoveAllGoals(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0055D4D0
bool __cdecl CFunc_GoalManager_RestartLastGoal(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0055D4F0
bool __cdecl CFunc_GoalManager_LevelUnload(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0055D510
bool __cdecl CFunc_GoalManager_NumGoalsBeatenInLevel(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055D530
bool __cdecl CFunc_GoalManager_AddGoalChoices(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0055D590
bool __cdecl CFunc_GoalManager_GetGoalList(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0055D600
bool __cdecl CFunc_GoalManager_ReplaceTrickText(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0055D7D0
bool __cdecl CFunc_GoalManager_GetGoalAttemptInfo(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0055D8E0
bool __cdecl CFunc_GoalManager_GetViewGoalsInfo(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0055D910

// --- goal_manager_update.cpp: 0x0055E860-0x0055EB3F ---
bool __cdecl CFunc_GoalManager_HasActiveGoals(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0055E860
bool __cdecl CFunc_GoalManager_UnBeatAllGoals(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0055E8A0
bool __cdecl CFunc_GoalManager_AddViewGoalsList(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0055E8C0
bool __cdecl CFunc_GoalManager_GoalShouldExpire(Scripts::CStruct* params, Scripts::CScript* script);          // 0x0055E8E0
bool __cdecl CFunc_GoalManager_UpdateAllGoals(Scripts::CStruct* params, Scripts::CScript* script);            // 0x0055EAC0
bool __cdecl CFunc_GoalManager_GetTimeLeftInNetGame(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0055EAE0
bool __cdecl CFunc_GoalManager_GetUnlimitedTimeInNetGame(Scripts::CStruct* params, Scripts::CScript* script); // 0x0055EB20

} // namespace Game
