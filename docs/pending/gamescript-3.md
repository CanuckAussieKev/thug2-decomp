# Game script commands 0x005AAEF0-0x005B0A40 (appearance, profiles, skater queries, cutscenes, records, goals) - gamescript-3 agent

Branch worktree-agent-aa49e53ab2c711d2c. Tests in src/hook/selftests_career_cfuncs.cpp (TestCareerCFuncs ...), shared
test pieces in src/hook/skate_cfunc_testkit.h. Shared files touched: CMakeLists.txt (new files listed),
src/game/skate/records.h / .cpp (three record commands added).

- [x] Batch GS3-1 (NOT run in the game yet; the career, goals, flags, the run, records - 32 CFunctions):
  src/game/skate/career_cfuncs.h / .cpp (new), src/game/skate/skate_module_calls.h (new: the skate module's fields
  and its / the game mode's calls without source, for this range's commands), src/game/skate/records.cpp.
  - 0x005AE990 CareerStartLevel, 0x005AE9C0 CareerLevelIs, 0x005AE9F0 GetCareerLevelScore (level_score /
    level_score_total; get_total adds total_score / total_score_potential), 0x005AEAA0 SetLevelTotalPoints,
    0x005AEAE0 GetClassicLevelGoalCount (classic_goal_count; get_total: total_goals_complete / total_goals_possible -
    so the career's +0x5EE / +0x5EF are the classic goals done / possible in all), 0x005AEB80 SetClassicTotalGoals,
    0x005AEC20 GetCurrentChapter / 0x005AEC50 SetCurrentChapter (the career's +0x634 is the chapter, +0x638 the
    highest), 0x005AEC80 CareerReset (a part checksum or 0), 0x005AECC0 SetGoal (only when the game mode's is_career),
    0x005AED10 UnSetGoal, 0x005AED40 GetGoal, 0x005AED70 JustGotGoal, 0x005AEDA0 SetFlag, 0x005AEDD0 UnSetFlag
    (level=, default the current), 0x005AEE30 GetFlag (level 0 means the current level: level 0 can't be asked
    for), 0x005AEEA0 JustGotFlag, 0x005AEED0 / 0x005AEF00 / 0x005AEF30 the global flags, 0x005AEF60 ProfileEquals
    (the script's skater object, else - only in a game of more than one player - skater 0), 0x005AEFC0 IsCareerMode,
    0x005AEFE0 EndRun / 0x005AEFF0 ShouldEndRun (also the game mode's elimiskate end, 0x00553910) / 0x005AF020
    EndRunSelected (the skate module's +0x46), 0x005AF030 AllSkatersAreIdle / 0x005AF040 FirstTrickStarted /
    0x005AF050 FirstTrickCompleted (the skate module's), 0x005AF060 CalculateFinalScores (server: the final scores
    sent; client: whether they came); records.cpp: 0x005AEBD0 UpdateArcadeScores (the level's arcade table from
    the parameters), 0x005AFAF0 UpdateInitials, 0x005AFB30 NewRecord (the skate module's +0x384).
  - Quirk kept and noted: parameters read with the (compiled-out) assert leave an uninitialised local when missing
    in the original; ours 0.
  - Test TestCareerCFuncs: original vs ours on a stand-in skate module, career (its methods run for real, sparse goal
    / flag bits), game mode, network manager and records; the skate module's / game mode's calls, MatchesParams,
    ResetPart, the arcade table's ReadFromStructure, UpdateInitials and the structure adds stood in for by recording
    code. 39 of 39 deliberate bugs caught (2 after making the career's goal and flag bits sparse).
- [x] Batch GS3-2 (NOT run in the game yet; split screen options, competitions, horse, score options, bonuses - 24
  CFunctions): src/game/skate/competition_cfuncs.h / .cpp (new).
  - 0x005AF090 ApplySplitScreenOptions (the split screen preferences' game_type loaded into the game mode; horse:
    horse_time_limit as the game mode's default_time_limit and the skate module's time limit (+0x370), horse_word
    given to the horse game - a Str::String passed by value, destroyed by the callee; the time_limit preference is
    read and not used), 0x005AF200 StartCompetition (gold / silver / bronze, their scores and bail by name strings,
    extra_params; in the skater info heap), 0x005AF320 StartCompetitionRun, 0x005AF340 EndCompetitionRun (the local
    skater's total score), 0x005AF3A0 PlaceIs, 0x005AF3D0 RoundIs, 0x005AF400 EndCompetition, 0x005AF440
    CompetitionEnded, 0x005AF450 StartHorse, 0x005AF470 HorseEnded, 0x005AF480 HorseStatusEquals, 0x005AF4A0
    StartHorseRun, 0x005AF4C0 EndHorseRun, 0x005AF4E0 SwitchHorsePlayers, 0x005AF500 ApplyToHorsePanelString
    (create_horse_panel_message on the current horse skater), 0x005AF660 GetHorseString, 0x005AF700
    IsCurrentHorseSkater (an integer or checksum), 0x005AFB40 TrickOffAllObjects (not with 0x00684FA8 set),
    0x005AFB80 SetScoreDegradation, 0x005AFBC0 SetTrickScoreTracking, 0x005AFC00 SetScoreAccumulation (score_frozen
    only when off and freeze_score), 0x005AFC70 AddBonus / 0x005AFCF0 GetBonusValue (only a local skater; no null
    check, as the original), 0x005AFD60 LocalSkaterExists.
  - Test TestCompetitionCFuncs: original vs ours with the skate module's, game mode's, preferences', competition's,
    horse game's and score's calls stood in for; structures and strings from a stand-in heap (the memory manager's
    allocate / free / contexts, the structure pool recorded), the heap compared after. 39 of 39 deliberate bugs
    caught. Found while testing: a by-value Str::String passed through a forwarding template was copied (two
    allocations) - the call is made directly.
