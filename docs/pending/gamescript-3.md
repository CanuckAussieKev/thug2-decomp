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
  - 18 CFunctions): src/game/skate/profile_cfuncs.h / .cpp and appearance_cfuncs.h / .cpp (new).
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
