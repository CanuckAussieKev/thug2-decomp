# time-of-day: 0x56C920-0x577300 ("Time-of-day manager & world state")

Agent "time-of-day". Branch worktree-agent-ac9d38ead59b17347, game workspace C:\Games\THUG2-4.

## The range (survey)

137 Ghidra functions, 38 KB, none rewritten before. "World state" turned out not to be a thing: the range is the end
of the goals / game-mode code with the time-of-day manager in the middle. In address order:

| addresses | what | functions |
|---|---|---|
| 0x56C920-0x56EB50 | network game handlers (Mdl::Skate's GameNet side: the `s_handle_*` message handlers for game modes, teams, the king of the hill / goal-attack messages; 0x56DEE0 / 0x56EB50 register them with the net handler table 0x45A5C0, 0x56E2B0 makes the two Tsk tasks) | 8 |
| 0x56F090-0x570F60 | a goal type, vtable 0x64E8A0: the Skatetris goal (30 falling trick "blocks", +0x70 0x20 each, screen elements, the trick checks 0x5624C0 / 0x562510; script names like 0xC93DA683) | 19 |
| 0x5710B0-0x571110 | another goal type, vtable 0x64E938 (constructor, destructor, IsActive-style check, deleting destructor) | 4 |
| 0x571140-0x574910 | **the time-of-day manager** (Gfx::CTODManager, singleton 0x7CE490 / refs 0x7CE494) and its 18 TODManager_* script commands | 53 |
| 0x574960-0x5763D0 | two more goal types: vtable 0x64E9B8 (a timed Skatetris variant: the "combo letters" / spin-tris blocks with a timer, 0x45C bytes) and 0x64EA30 | 19 |
| 0x576400-0x576CA0 | goal / game-mode helpers: win conditions (0x576810 makes them by type: vtables 0x64EB0C..0x64EB84), score checks over the players (0x576A40, 0x576B80 and its thunk 0x576CA0) | 16 |
| 0x576D40-0x5772E0 | a class with an owned object at +0x54 (vtable 0x64EBC4, singleton flag 0x7CE4A8) and setters of the globals 0x7CE498-0x7CE4A4; a camera move helper (0x5772E0) - the start of the viewer code that the next range continues | 18 |

### The time-of-day manager

- Made by Sys::Main (CreateTODManager(true), 0x574540; released by Sys::Main / 0x4E1AF0), updated once a frame by
  Mdl::Skate's update (0x567710 -> 0x574590). Level clean-up (CFunc_Cleanup 0x59FE50, 0x566900) empties its
  dead-objects table (0x572BD0). Obj's CreateFromStructure (0x5A2E50) colours new objects with its level colour
  (0x571140); CFunc_SetSceneColor (0x5A29E0) uses its scene colouring (0x5732D0).
- Two "systems" (a level's script gives system=1 and, for the sky, system=2), each a day of four states (morning,
  afternoon, evening, night) with a parameter set per state (TODParams, 0xA4 bytes: bloom, sun, level / sky colour,
  fog, ambient and two directional lights), a state length and a transition length. Time passes only in career
  mode, unless the script global tod_manager_inactive is 1, a temporary state is up, the manager is inactive or the
  game is paused; while transitioning, each frame blends the two states' parameters and colours one part of one
  light group of the scenes' sectors (groups= in the system's params: parts, wait_frames, light_group,
  reset_light_group), sets the light manager, fog, bloom and the sun, and runs the script
  tod_manager_state_transition half-way through (evening: at once).
- Engine calls it makes (not rewritten, left as GameFunction): fog 0x490DE0/0x490E00/0x490E20, the light manager
  0x48E8F0/0x48E930/0x48E970/0x48E990/0x48ED70, the sun 0x4426D0, the level colour 0x4966B0/0x49AE80/0x491290,
  bloom 0x49E360 (on 0x6FA7CC), the scene list / sky / level scene 0x48C7F0/0x48C7B0/0x58D710+0x48C700, sectors by
  name 0x48C510, model colour 0x420700, Obj's CreateFromNode 0x5A32A0, multiplayer check 0x5651F0, game mode
  0x5532A0.
- Nothing in src/game touched it before (the engine script commands range 0x445B50-0x44E6B0 has no lighting code
  that calls into it; Sys::Main called 0x574540 through its generic Create(address)).

Checksum names: from the game's .qb name tables, plus guesses checked by CRC (system_index, bloom_params, outdoor).
Not recovered: 0xE5B6C568, 0xDF1A695F, 0x8F639F94 (SetSceneColor's sectors-only flag and percent range).

## Batches

- [x] Batch TOD-1 (NOT run in the game yet): the time-of-day manager, 0x00571140-0x00574910 - all 53 functions:
  Gfx::CTODManager (src/game/gfx/tod_manager.{h,cpp}; TODParams 0xA4 bytes, TODSystem 0x188, the manager 0x340 -
  full layouts), its 18 TODManager_* script commands, CreateTODManager / ReleaseTODManager (Sys::Main now calls them
  instead of the generic Create / Release by address), and 0x00572821 (ApplyColor's body after its first test, a
  function Ghidra made: source-only, ApplyColor's source).
  Findings: the day is two "systems" (system=1, and a sky system=2) of four states (morning -> afternoon -> evening
  -> night, a night ending counts a day); time passes only in career mode, unless tod_manager_inactive is 1, a
  temporary state (TODManager_SetTempTOD) is up, TODManager_SetActive 0 or the game is paused. While transitioning,
  each frame blends the two states (Lerp: integers truncated through doubles, floats in double, bloom colours byte
  by byte) and colours one part of one light group: (100 / parts)% of each scene's sector table (sectors of the
  group's light_group; reset_light_group's back to neutral 0x81808080), the level scene, and - for the outdoor group
  (0xE3714DC1 = "outdoor") or no group, on the first part only - the scenes' own colour and every model whose object
  has no sector of its name or an outdoor one. tod_manager_state_transition runs half-way through a transition
  (evening's at once), with level_specific_transition_script. Quirks kept: GetLevelColor / GetTODParams index past
  the end (system 2) when both are sky systems; SetTODInstant to a state that never comes loops forever; GetTODParams
  blends leave +0x14..+0x24 and fog_on unset in the original (stack garbage, ours: the "from" state's - fog_on is
  given back by GetCurrentTODParams); ResetLevelLights(state) reads its parameters into an uninitialised local (ours
  zeroed: only what the state gives is defined); SetSceneColor's colours' fourth byte is garbage (ours 0); the
  x87's own FSIN / FCOS for the light directions; RefreshAll's part size rounded to a float (UpdateSystem's isn't).
  Tests (src/hook/selftests_tod.cpp, stand-ins for every call out: structures, arrays, scripts, memory, engine):
  TestTODBlend (Lerp, GetTODParams, GetLevelColor, UpdateSun, ApplyTODParams, light directions, SetFogActive),
  TestTODStates (Construct, SetParams, ReadGroup, NextState, 90 frames of Update through states and transitions,
  SetTODInstant / AdvanceTODState, RefreshAll, ResetLevelLights, temporary states and bloom, EnableBloom, the other
  script commands, the dead objects, Reset, the destructor, the singleton), TestTODColor (ApplyColorToScene,
  ApplyColor, SetSceneColor on stand-in scenes, sectors and models). 64 deliberate bugs caught, 8 after
  strengthening (a bloom_on of 1, the manager after SetParams, a 128-sector level scene so a percentage off by one
  shows, temporary bloom over more frames, RefreshAll with the manager inactive, EnableBloom with bloom already off).
  What to check in the game: a career level with a day cycle (any THUG2 level - e.g. Boston), left in free skate for
  a few minutes: the sky, level light and fog should change through morning / afternoon / evening / night as before;
  TODManager_GetCurrentTODParams-driven menus (the pause menu's time of day, if shown).

- [x] Batch TOD-2 (NOT run in the game yet): the victory conditions and the viewer module's pieces,
  0x00576610-0x005772E0 - 32 functions (6 Ghidra had missed: 0x005766E0 / 0x00576700 elimination,
  0x00576CB0 / 0x00576CF0 the viewer's script helpers, 0x00576F00 / 0x00577140 its handlers) plus 0x005772A0
  Mth::Matrix::RotateLocal(axis, angle). Game::CVictoryCondition and its 7 types (src/game/skate/victory_conditions.{h,cpp}:
  highest_score, target_score, complete_goals, best_combo, last_man_standing, elimination, collect_all_objects;
  CreateVictoryCondition, the scalar deleting destructors, IsHighestScore, ConditionComplete / IsWinner, the debug
  prints; 0x00576CA0 is a jump to complete_goals' ConditionComplete: source-only); Mdl::Viewer (src/game/sys/viewer.{h,cpp}:
  the input / shift handlers - buttons into command bits, sticks into velocities, the UserViewer* scripts -, the
  viewer object made from the bottom-up or debug heap, runnow.qb reloading, the script commands SetViewMode /
  SetMovementVelocity / SetRotateVelocity, MoveObjectLocal).
  Findings: target_score counts team totals in a team game, else each skater; complete_goals counts, per team or
  per skater, the goals that count (pointer word non-zero and not hidden) won by the skater; last_man_standing wins
  with a score above 0, elimination by not being eliminated (player +0x38); collect_all_objects needs one skater's
  score equal to every player's objects added up (0 objects: not complete). The viewer's input: R2 + select
  (0x200) sets command bit 0x4000.
  Tests (src/hook/selftests_tod2.cpp): TestVictoryConditions, TestViewer, TestMatrixRotateLocalAxis (the CRT's
  sin / cos patched to ours so both use the same). 39 deliberate bugs caught, 2 after strengthening (a skater
  above the objects' total for collect_all_objects; the manager's heaps told apart for the viewer object).
  What to check in the game: a network game mode's end (target score, elimination, scavenger hunt) and the
  viewer (debug builds only - not reachable in retail).

- [x] Batch TOD-3 (NOT run in the game yet): the trick goals, 0x0056F090-0x00570F60, 0x005710B0-0x00571110 and
  0x00574960-0x00576520 - 49 functions (src/game/skate/trick_goals.{h,cpp}): Game::CTetrisGoal (Skatetris: 0x458
  bytes, 30 tricks of 0x20; constructor / destructors, Win, ClearTricks / ClearTricksAndMenu / Init, the combo /
  single combo / TrickTris variants, UpdateAlphas, Expire, Activate / Deactivate, RemoveTrick, AddTricks, CheckCombos,
  CheckTricks, Update), Game::CTimedScoreRunGoal (constructor, destructors, IsRunning), Game::CTrickBeatGoal
  (TrickBeat: 0x47C bytes; the beat pattern and tempo, the beat clock with its sounds and the trick window,
  TrickLanded / ClearLanded / PulseTricks / RemoveLandedTricks, AddTricks, Update), Game::CTrickObjectsGoal
  (CheckScore, GetObjectByCluster, ObjectHit). Only CGoal's first 0x70 bytes are described (GoalBase); CGoal itself
  (0x00550040-0x0056C920) isn't rewritten.
  Findings: a Skatetris trick comes every current_trick_time ms (shortened by acceleration_percent every
  acceleration_interval tricks); the stack turns red past three quarters of max_tricks and is lost when a new block
  would overflow it; key combos come from the skater's trick mapping (a special slot: the skater's own special),
  double taps through the trick's ExtraTricks; TrickTris adds blocks of tricktris_block_size until
  tricktris_total_to_win are done. TrickBeat's beat is 15000 / tempo ms (slowed by slow motion), each of the 16
  steps' sounds played on it, the window beat_window_time / 2 ms either side of a pulse step. Quirks kept: "Perfect "
  is written and then overwritten by the spin text; ClearTricks of TrickBeat stores free = true then false;
  GetObjectByCluster matches an entry without cluster_id against the last one read; RemoveTrick of TrickBeat reads
  two values it doesn't use; the beat's time compared as unsigned.
  Tests (src/hook/selftests_tod3.cpp): TestTetrisGoal, TestTrickBeatGoal, TestSmallGoals. 51 deliberate bugs caught,
  10 after strengthening (UpdateAlphas with no no_alpha_check given - our local now zeroed, as it's only read when
  given -, a stack exactly full and exactly three quarters full, the groups after CheckCombos, TrickTris at exactly
  its total, the trick interval reached exactly, a trick landed twice, the goal's words after AddTricks and the beat
  frames, the time limit with flag 0x10 and no time left).
  What to check in the game: a Skatetris goal (e.g. a career level's Skatetris), a TrickBeat goal, a trick objects
  goal (hitting the marked objects): tricks appear, turn red, disappear when done, and the goals win / lose as before.
