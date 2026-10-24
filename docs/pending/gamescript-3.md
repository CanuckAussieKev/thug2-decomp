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
- [x] Batch GS3-3 (NOT run in the game yet; skater queries - 25 CFunctions): src/game/skater/skater_query_cfuncs.h /
  .cpp (new).
  - 0x005AC5F0 / 0x005AC660 SkaterSpeedGreaterThan / LessThan (the local skater's velocity length, x87 order in
    double), 0x005AC6D0 / 0x005AC720 SkaterLastScoreLandedGreaterThan / LessThan, 0x005AC770 SkaterGetLastScoreLanded
    (last_score_landed), 0x005AC7B0 AccumulateScores, 0x005AC850 EliminateLastPlacePlayer (all tied: message 0x8D
    with -1 to the players; otherwise each player on the lowest score marked eliminated and 0x8D with its skater's
    id to every player, observers too - sequenced, group 8), 0x005ACAA0 OnlyOnePlayerLeft (teams: at most one team
    with players in; else at most one player in, num_players), 0x005ACC20 AnySkaterTotalScoreAtLeast (teams' sums in
    a team game), 0x005ACD80 OnlyOneSkaterLeft (players still joining: false; the server in host mode 1 / 2 with
    nobody scored: true; else exactly one skater / team scored; not a team game and one skater: false),
    0x005ACF50 / 0x005ACFA0 SkaterTotalScoreGreaterThan / LessThan, 0x005ACFF0 / 0x005AD070
    SkaterCurrentScorePotGreaterThan (point_scale) / LessThan (the base score times the multiplier, truncated),
    0x005AD0D0 SkaterGetScoreInfo (scorepot, totalscore, lastscorelanded; no local skater: the original reads a null
    score - kept), 0x005AD140 InFirstPlace / 0x005AD2D0 InLastPlace / 0x005AD450 InSecondToLastPlace (points: the
    best / lowest other total, or the local total less the lowest; through each skater's SkaterScore component;
    every TotalScore call kept as the original makes them), 0x005AD5C0 GoalsGreaterThan / 0x005AD610 GoalsEqualTo
    (the career's CountTotalGoals against a float), 0x005AD660 ToggleSkaterCamMode (skater=0: the local player's;
    the camera's skatercamera component, 0x004FA7D0), 0x005AD6C0 GetSkaterID (only a local skater), 0x005AD730
    SkaterExists, 0x005AD780 GetSkaterSex, 0x005AD7E0 GetCurrentSkaterID (objID and intId).
  - Quirk noted: ToggleSkaterCamMode without skater= uses an uninitialised index in the original (ours 0, the local
    player's); every use in the game's scripts passes skater=0 (checked over the .qb files).
  - Test TestSkaterQueryCFuncs: original vs ours on four stand-in skaters / scores / network players (present,
    eliminated, local, observer, surveying), team and non-team games, host modes, joining players, ties; the player
    list, the player flags, TotalScore, the components, the camera's toggle, CountTotalGoals, the profile and every
    message sent (all MsgDesc fields) stood in for. 51 of 51 deliberate bugs caught (10 after strengthening: equal
    values at the comparisons' edges, the tie without the scoreless player, one team / two players / one player in,
    a player without a skater, a lone skater who scored, a profile index unlike is_male).
- [x] Batch GS3-4 (NOT run in the game yet; skater camera animations, cutscenes, object animations - 21 CFunctions and
  the cutscene destroy list entry's constructor): src/game/obj/movie_cfuncs.h / .cpp (new).
  - 0x005AD830 PlaySkaterCamAnim (0x0C567FA2 - name unknown -: the movie manager takes over the game's camera;
    SkaterId= or skater= (0); not a local skater: "this isn't a local skater!\n" printed; stop: every movie dropped;
    else disable_skater_scaling run and the animation queued as SkaterCamAnim), 0x005AD960 / 0x005ADA30
    SetSkaterCamAnimSkippable / ShouldPause (also SetCutsceneSkippable / ShouldPause), 0x005ADB00
    GetCurrentSkaterCamAnimName (none: false), 0x005ADB50 GetSkaterCamAnimParams, 0x005ADC10 / 0x005ADCC0
    SkaterCamAnimFinished / Held (also CutsceneFinished / Held; no local skater: true), 0x005ADD70
    KillSkaterCamAnim (also KillCutscene: Current - the EndMovies result returned -, all, or a name; none left: the
    park editor's SwitchCamera in its edit state, else Restore_skater_camera), 0x005ADEC0 ReloadSkaterCamAnim (only
    with the extra memory 0x007CEFAC; AssMan::ReloadAsset in the debug heap), 0x005AE000 CutsceneDestroyListActive
    (0x0049A8E0 on 0x006FA7C0), 0x005AE010 PlayCutscene (ObjectAnim: an object movie of type 0xB46E218F - name
    unknown -; else the Cutscene movie; stop looked for, not used), 0x005AE090 HasMovieStarted, 0x005AE0A0
    IsMovieQueued, 0x005AE0B0 PauseCutscene (1 by default; toggle), 0x005AE120 Cutscene_FrameAdvance, 0x005AE140
    PlayObjectAnim, 0x005AE190 / 0x005AE200 SetObjectAnimSkippable / ShouldPause, 0x005AE270 / 0x005AE2B0
    ObjectAnimFinished / Held, 0x005AE2F0 KillObjectAnim; 0x005ADF60 CutsceneDestroyEntry::Construct (the entry
    AddToCutsceneDestroyList - gamescript-4's range - makes: name, destroy_func, a destroy_params structure).
  - Test TestMovieCFuncs: original vs ours with both movie managers' methods (every argument; the bool ones by their
    byte), FindObject, the skaters, Printf, the park editor, the asset manager and 0x0049A8E0 stood in for, every
    branch (local or not, by id / index / none, movies left or not, the editor's state, the reload's gates). 40 of
    40 deliberate bugs caught (1 after the stand-in memory manager got a distinct value per heap).
- [x] Batch GS3-5 (NOT run in the game yet; player options, the pro's name, selecting the skater, appearance and face
  - 18 CFunctions): src/game/skate/skater_profile_cfuncs.h / .cpp and appearance_cfuncs.h / .cpp (new).
  - 0x005AE370 ... 0x005AE4F0 VibrationIsOn / On / Off, AutoKickIsOn / On / Off, SpinTapsAreOn / On / Off (the skate
    module's +0x394 table, 8 bytes a player; set through 0x00567C30 / 0x00567C00 / 0x00567BD0), 0x005AE520
    GetCurrentProDisplayInfo (string: the current profile's display name), 0x005AE900 SelectCurrentSkater (false if
    it already is; the skater at the manager's current index given the profile's looks); 0x005AE550
    SetPlayerAppearance, 0x005AE5B0 GetPlayerFacePoints (into the script's current_face_points, a new structure
    added if none), 0x005AE710 SetPlayerFacePoints (face_points or the parameters themselves), 0x005AE7A0
    SetPlayerFaceTexture (fullPathIncluded), 0x005AE820 SetPlayerFaceOverlayTexture, 0x005AE880
    ClearPlayerFaceTexture, 0x005AE8C0 PlayerFaceIsValid (the appearance's face, its +0x54; the face texture's code,
    0x00486xxx-0x00487xxx, through GameFunction).
  - Test TestProfileCFuncs: original vs ours with the option setters, the profiles, the profile manager's
    SetCurrentSkater, the skaters, SetAppearance, every face call and the structure adds stood in for; the script's
    own parameters with and without a current_face_points structure (CallCommand's g_scriptParamsOverride, new in
    the test kit). 23 of 24 deliberate bugs caught; the miss is equivalent (writing the points from the scratch copy
    instead of the copy made of it).
- [x] Batch GS3-6 (NOT run in the game yet; model refreshes, appearance parts, stats, profile queries, position and
  velocity - 24 CFunctions): appearance_cfuncs.cpp, skater_profile_cfuncs.cpp, skater_query_cfuncs.cpp.
  - appearance: 0x005AF750 / 0x005AF7D0 / 0x005AF850 / 0x005AF8D0 RefreshSkaterColors / Scale / Visibility / UV
    (RebuildFromProfile with color_model_ / scale_model_ / hide_model_from_appearance / set_uv_from_appearance),
    0x005AFD80 RefreshSkaterModel (the unnamed index is both indexes' default; the first viewport's skater: the
    BailBoard object marked dead and the dead flushed; the model rebuilt through vtable slot 13 in the skater's
    geometry heap - with the compiled-out call 0x0048C1B0 kept as an empty inline -, then its stats, trick mappings
    and looks in its own heap; no_board= (a checksum parameter, not a flag): the board geom hidden and the model
    updated through its vtable), 0x005AFF30 EditPlayerAppearance (the target script run with the profile passed as
    its object, as the original), 0x005B0420 GetActualCASOptionStruct (always false), 0x005B0480 / 0x005B0500
    GetActualPlayerAppearancePart / GetPlayerAppearancePart, 0x005B0580 GetPlayerAppearance (a copy as profile),
    0x005B0670 / 0x005B0780 / 0x005B0920 SetPlayerAppearanceColor / Scale (a missing slot made) / UV (floats
    truncated as _ftol for colour and scale; UV kept as floats).
  - profile: 0x005AF950 AwardStatPoint (classic: the career's points_available, else the current profile's;
    stat_play_win_sound), 0x005AF9F0 AwardSpecialTrickSlot (all / profile= / current), 0x005AFAA0
    UpdateSkaterStats, 0x005AFFE0 GetCurrentSkaterProfileIndex, 0x005B0000 GetCustomSkaterName, 0x005B0030
    GetCurrentPedProName (the global 0x09EEBCB8 - name unknown - looked up through name links: a structure gives its
    name as 0xF054556D; no null check), 0x005B0280 GetStatValue (the skate module's forced value +0x3D0 unless 0:
    stat_value and the overridden flag), 0x005B0360 GetNumStatPointsAvailable, 0x005B03B0 UnlockSkater.
  - skater queries: 0x005B0070 GetSkaterPosition (x, y, z truncated), 0x005B00F0 SetSkaterVelocity (a speed: the
    velocity's direction scaled, all four components; else vel_x / vel_y / vel_z with w 1 through a heap vector
    allocated and freed, as the original).
  - Found while testing: CModelComponent has an Update of its own, so the model's update after hiding the board is
    made through the base's vtable call (slot 1), as the original.
  - Test TestAppearanceCFuncs: original vs ours with stand-in skaters whose model / trick components have recorded
    virtual calls, the bail board and the object manager, the appearance's part look-ups, the career's / profiles' /
    profile manager's calls, real symbol table entries for the ped pro global (a structure, a name of one, a name of
    nothing, an integer), the velocities recorded after each call. 50 of 50 deliberate bugs caught (5 after
    strengthening: a use_default_hsv other than 1, a negative forced stat, a z that rounds differently, the
    velocities recorded after each call).
- [x] Batch GS3-7 (NOT run in the game yet; the profile templates and lists - 15 CFunctions): skater_profile_cfuncs.cpp.
  - 0x005ABBF0 ResetAllToDefaultStats (each master_skater_list skater's stat_names values and points_available
    into its template's info; the scratch structure freed through the pool as the original's inlined delete),
    0x005ABE30 ResetToDefaultProfile (partial=stats_and_tricks: SetFromParams(entry, no info, specials); another
    partial: info only; none: Reset), 0x005ABF00 ResetAllToDefaultProfile, 0x005ABF70 ForEachSkaterName /
    0x005AC0C0 ForEachSkaterProfile (do= run per template with params= and the name / the whole info), 0x005AC1F0
    GetSkaterProfileInfoByName / 0x005AC280 SetSkaterProfileInfoByName (false without the template), 0x005AC320
    PrintCurrentAppearance (compiled out: 0x005BAB90), 0x005AC340 SetNeversoftSkater, 0x005AC3B0 ResetSkaters (the
    skate module's 0x00566980 with node_name and RestartWalking; 0xDD7DF50E - name unknown - read and unused),
    0x005AC410 SetSkaterProfileInfo, 0x005AC470 GetSkaterProfileInfo, 0x005AC4B0 SetSkaterProfileProperty (SetStat's
    answer), 0x005AC520 SetClassicStat (the career's SetInfoInteger), 0x005AC570 ToggleAlwaysSpecial (skaters 0-7's
    +0x168: on, off - on wins -, else toggled).
  - Test TestProfileListCFuncs: original vs ours on a stand-in master_skater_list / stat_names (real arrays of real
    structures), three templates with real info structures, the profile manager, the skaters; the profiles' calls
    (SetFromParams, Reset, SetAppearance, SetStat...), the career's, GetArray, the skate module's and the structure
    adds / appends stood in for. 29 of 29 deliberate bugs caught. The test kit's pool stand-ins now record a
    structure freed straight into its pool as DeleteStruct.
- [x] Batch GS3-8 (NOT run in the game yet; preloading, the random pedestrians, car textures, the temporary
  appearance - 6 CFunctions): src/game/skate/preload_cfuncs.h / .cpp (new), skater_profile_cfuncs.cpp.
  - 0x005AAEF0 RestoreTemporaryAppearance (NoFaceTexture: only the appearance structure, cleared and copied; else
    the whole appearance through 0x004893A0), 0x005AAF90 SyncPlayer2Profile, 0x005AAFA0 PreloadModels (vehicle and
    GameObject nodes not skipped by the skate module's 0x00565BB0; the capture-the-flag types only in a game of more
    than one player; "none" skipped; the model loaded with AllowReplaceTex and dropped; unless nocollision, a path
    holding ".mdl" (lower-cased) loads <base>.col as asset type 8 grouped by the path's checksum; a vehicle also
    loads <base>.qb as type 0xA), 0x005AB300 PreloadPedestrians (not in a network game nor with the global
    0x2CAA92ED - name unknown - set; the eight ped_m / ped_f part lists through gamescript-2's 0x005AAAA0 (the list
    in EAX: an inline asm call) unless no_random; each pedestrian node's .skin model, its second model 0x545F8172 -
    name unknown -, and the model its profile's appearance builds with preload_model_from_appearance), 0x005AB690
    PreselectRandomPedestrians (candidates: not no_random, level_specific naming the skate module's +0x374 or not
    excluded by exclude_from_levels; the nodes' own parts taken; one option of any kind - failing that a light, a
    tan and a dark - then the rest at random among the candidates, num capped by the candidates, numbered by
    random_index; the original's endless loop when no candidate is left is kept), 0x005ABB20 ReplaceCarTextures
    (each car whose geoms use a texture dictionary not met in the walk runs Obj_ReplaceTexture).
  - Deviation (noted in the code): ReplaceCarTextures keeps at most 32 dictionaries as the original's stack array
    does, but doesn't write past it (the original would overwrite its own frame).
  - Test TestPreloadCFuncs: original vs ours on a stand-in node array (vehicles, game objects, a skipped node,
    "NONE", nocollision, a flag, an extension other than .mdl, pedestrians with and without profiles), six CAS
    options covering every candidate rule, three car components; the node walk, Nx's models, the asset manager,
    the CAS look-ups, the appearance and builder, the original's _stricmp / strstr (the C runtime isn't started
    offline), Rnd and the structure changes stood in for; stack pointers recorded by the order first seen. 47 of 47
    deliberate bugs caught (2 after strengthening: a path without .mdl, a car with no new dictionary).
  - tools/ghidra/audit_game_function_calls.py: a Str::String passed by value counts 12 bytes (it flagged the
    horse word's call as a mismatch).

## The range done
- 0x005AAEF0-0x005B0A40: 167 of 167 functions have source (166 CFunctions - SetCutsceneSkippable / SetCutsceneShouldPause,
  CutsceneFinished / CutsceneHeld, KillCutscene share their skater camera twins' code - and the cutscene destroy list
  entry's constructor 0x005ADF60); find_gaps: nothing skipped in the range (the bytes between the functions are
  padding or the functions' own tails); no missed functions in it (find_missed_functions).
- Checks: thug2_check on THUG2PM.exe and the stock THUG2.exe - 0 MISMATCHED, all early self-tests pass (781);
  scan_register_assumptions - nothing new (0x005ADF60 returns this); audit_game_function_calls - no MISMATCH;
  check_code_writes - no overlaps.
- Crash check (workspace C:\Games\THUG2-4, under its lock, this build's DLL, `run_game.ps1 -Level BO`): the game
  started, the level loaded, the skater spawned in free skate, the automation channel's waits done; no crash,
  run-time check, CRT report or exception in the log. (A first try with -FullTests ran out of its time in the in-game
  early self-tests - 781 of them, minutes; thug2_check runs those offline.)

## Untested in the game, and what to try by hand
Nothing here was played; each needs its own scripts to run:
1. Career: start a career level, get a goal and a level flag (SetGoal / JustGotGoal / SetFlag), check the chapter and
   classic goal counts in the career menus; EndRun / ShouldEndRun at a run's end.
2. Competitions (THUG2's classic / competition levels): StartCompetition, the runs and places, EndCompetition.
3. Split screen HORSE: ApplySplitScreenOptions (the time limit and the word), the HORSE panel strings
   (ApplyToHorsePanelString / GetHorseString), switching players.
4. Network games (by hand, never by an agent): elimiskate (EliminateLastPlacePlayer, OnlyOnePlayerLeft), score modes
   (InFirstPlace / InLastPlace / InSecondToLastPlace points), AccumulateScores across games, OnlyOneSkaterLeft.
5. Cutscenes and skater camera animations (the story's cutscenes, the main menu's camera moves PlaySkaterCamAnim
   uses), KillSkaterCamAnim in the park editor (its camera back), object animations.
6. CAS: the face (face points, face texture and overlay), SetPlayerAppearanceColor / Scale / UV, RefreshSkater* and
   RefreshSkaterModel (the board hidden with no_board=), stat points (AwardStatPoint, GetStatValue), unlocking a pro.
7. Levels with pedestrians and cars: PreloadPedestrians / PreselectRandomPedestrians (varied pedestrians), PreloadModels
   (vehicles and game objects present, collision working), ReplaceCarTextures.
8. Records: a new high score's initials (UpdateInitials / NewRecord), arcade scores (UpdateArcadeScores).

## Switching it off
`[replacements] disable=` with the file names: career_cfuncs.cpp, competition_cfuncs.cpp, skater_query_cfuncs.cpp,
movie_cfuncs.cpp, skater_profile_cfuncs.cpp, appearance_cfuncs.cpp, preload_cfuncs.cpp (and records.cpp for the three
record commands - that file also holds the records' class code).
