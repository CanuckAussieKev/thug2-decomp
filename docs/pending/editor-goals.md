# Level editor: goals - THUG2's Create-a-Goal goals in the edited copy (NOT ORIGINAL - the user's request, 2026-10-21)

Branch `worktree-agent-ad7ec10dafadc1631` (main 1b3384b merged in). **NOT run in the game yet** (no game runs for
editor work): built and checked offline only.

## How THUG2's Create-a-Goal works (studied: THUG2PM.exe + the game's scripts decompiled with NeverScript)

- **Scripts**: goal_editor.qb (CreateGoalEditor, InitialiseCreatedGoals, EditedGoal_ExtraParams_<type>,
  EditedGoal_Pro_Node / _Restart_Node / _Object_Node, cag_key_combos, GoalEditor_ObjectHeight 27), goal_editor_menu.qb
  (CREATED GOALS list, SELECT GOAL TYPE - goals_menu_info: SKATE letters, COMBO letters, High Score, High Combo,
  Skate-Tricks, Combo Skate-Tricks, Tricktris, Trick to the Beat, Checkpoint / Race, Gap -, EDIT GOAL: time limit,
  score, laps / per-checkpoint time, gaps / required trick, key combo sets / spin / sizes, name / ped name / goal text /
  win message, control, test, delete), levels.qb (load: AddGoalEditorNodesToLevelNodeArray and InitialiseCreatedGoals
  only in create-a-goal mode, net games, split screen), memcard.qb (CreatedGoals: its own save type, VERSION 5, SAVE
  GOALS in create-a-goal's pause menu), goal_utilities.qb (goal_success -> GoalEditor:FlagGoalAsWon unless testing).
- **The goal editor**: composite object `GoalEditor` (startup.qb, permanent) with component goaleditor 0x81F01058:
  50 created goals of 0x338 bytes from +0x18 (current +0xA108), max 10 complete per level. Member functions (table
  0x00684270, filled by 0x00643960): EditedGoalAddGap 0x004E8490, ...RemoveGap 0x004E84E0, ...GotGap 0x004E8530,
  AddKeyComboSet 0x004E8590, RemoveKeyComboSet 0x004E8630, GetKeyComboSet 0x004E8680, RemoveGoalSpecificFlag
  0x004E8720, SetGoalSpecificParams 0x004E8780, FlagGoalAsWon 0x004E87A0, FindUnfinishedGoal 0x004E87C0, GoalExists
  0x004E8900, NewEditorGoal 0x004EA7C0, RemovedCreatedGoal 0x004E8920, GoalHasAllPositionsSet 0x004E8950,
  SetCurrentEditorGoal 0x004E8980, SetEditorGoalType 0x004E89A0, SetEditorGoalDescription 0x004E89F0,
  SetEditorGoalWinMessage 0x004E8A40, GetCurrentEditedGoalInfo 0x004EBFF0, GetEditedGoalsInfo 0x004EC010,
  GetNumEditedGoals 0x004E8AA0, GetCurrentEditedGoalId 0x004E8B30, WriteEditedGoalNodePositions 0x004E8B90,
  AddEditedGoalToGoalManager 0x004EC1D0, SetEditorGoalName 0x004E8BB0, SetEditorPedName 0x004E8C20,
  MaxEditedGoalsReached 0x004E8C90, EditGoal 0x004EA810, NukeAllGoals 0x004E8CF0, GetMaxGoalsPerLevel 0x004E8D30 (10),
  SetGoalScore 0x004E8D50, SetGoalTimeLimit 0x004E8DB0, ...
- **The created goal (CEditGoal, 0x338)**: +0 level, +4 view_goals_text, +0x36 ped_name, +0x4B goal_description, +0xAF
  win_message, +0x114 type, +0x118 13 points of 0x1C {kind 1 pro / 2 start / 3 letter / 4 checkpoint, x, y, z, w,
  height, angle}, +0x284 max / +0x285 min / +0x286 set / +0x287 cursor, +0x289 won, +0x28A used, +0x28C
  level_goal_index, +0x290 time_limit, +0x294 control_type, +0x298 score, +0x29C laps, +0x2A0 per-checkpoint time,
  +0x2A4..+0x2C8 the trick goals' settings, +0x2CC 10 key combo sets {set, perfect, spin/180, taps} (+0x2F4 count),
  +0x2F8 10 gap numbers (+0x302 count), +0x303 required_trick_name. Functions on it: InitEditedGoal 0x004E69D0 (ours),
  SetType 0x004E6DA0, SetSpecificParams 0x004E7590 (0x004E74F0, 0x004E73F0), ReadFromStructure 0x004E96D0,
  WriteIntoStructure 0x004EB260, AddToGoalManager 0x004EB6B0 (params 0x004EA850, specifics 0x004E9E00; AddGoal
  0x0055BA10 as CreatedGoal<n>), WriteNodePositions 0x004E7A80 (0x004E79C0: Pos + height, Angles y = angle - pi, a
  letter's Type / model), placing a point 0x004E77C0 (the editor camera's cursor +0x3C pos, +0x88 height, +0x78 angle).
- **Nodes**: AddGoalEditorNodesToLevelNodeArray 0x005AAA80 -> 0x004E8F50: TRG_EditedGoal0..9 _Pro / _Restart /
  _Object_0..10 from the templates. The goal manager leaves `edited_goal` goals out of the level's saved goal state
  (0x005542B0, called by LevelUnload 0x0055C090): created goals never reach the career save.

## What was done

- **Goals panel** (src/game/front/level_editor_goals*.{h,cpp}): the Inspector's left-panel **Goals** section (type
  chooser + New goal - placed where the view looks: the pro facing you, the start beside it, letters / checkpoints ahead,
  dropped to the ground; list with Select) and a selected goal point's **Goal** section (complete / why not; the point's
  height; the goal's points as buttons; race Add checkpoint / Remove last; name, ped name, goal text, win message, time
  limit, score (high score / combo), control (as Create-a-Goal offers it), laps / per-checkpoint time, the level's gaps
  as checkboxes + required trick, key combo sets / spin / combo size / single combo / block size / total / acceleration /
  trick time / stop adding / max tricks; Copy to Create-a-Goal; Delete goal). Every type of Create-a-Goal. At most 10.
- **Points** are owned overlay nodes (GenericNode type `ed_goal`, `<goal>_pro`, `_start`, `_s.._e` / `_c.._o2`,
  `_cp1..`): moved, turned (Z / X: the heading the pro / skater faces), dropped (G), undone like every point node; labels,
  heading arrows, the letters' height line and the race's checkpoint chain drawn over the view. Goal-level changes are
  module undo steps (whole goals state). A point node deleted with Delete: the goal keeps its last place (Restore points).
- **Stored**: `goal` records in the level's layout (the edited copy's file; Ctrl+S, autosave, Reset level as everything
  else) - chosen over a side file because the edited copy is the layout: it loads, resets and is deleted with it, and
  only ever with the edited copy. Format in level_editor_goals.h (texts cleaned of `"`, `#`, `|`).
- **Played** (editor not running, the level is its edited copy, not a refused mode): play nodes TRG_EditedGoal<20+i>_Pro
  / _Restart / _Object_k in the overlay with the EditedGoal_*_Node fields, placed as WriteNodePositions does; the goal's
  record filled here (FillImage) and handed to the game's own AddToGoalManager 0x004EB6B0 (mark unbeaten), then
  GoalManager_InitializeGoal. Numbers 20..29 (CreatedGoal2x): never Create-a-Goal's 0..9 - the goal editor's table is
  untouched (FlagGoalAsWon finds no CreatedGoal2x there). Opening the editor takes them out (deactivated if running,
  RemoveGoal, pro killed, play nodes removed); leaving it puts them back. A new level: any left in the goal manager
  removed (Goals::ForgetLevel from OnSceneLoaded).
- **Saves**: the career never gets them (edited_goal, the goal manager's own rule); the CreatedGoals save never does
  unless the user presses **Copy to Create-a-Goal**: the record copied into a free goal-editor slot of this level (free
  index 0..9, as NewEditorGoal picks), in memory - Create-a-Goal mode in that level then lists it and SAVE GOALS saves it.
- Small hooks: level_editor.cpp (Start / Stop / Update / overlay / TakeSelect / screenshot line), level_editor_ui.cpp
  (two sections), level_editor_layout.{h,cpp} (File::goals, Write / Read / Capture / Apply / ResetToLevel /
  OnSceneLoaded), level_editor_node_create.cpp (KindOf: an `owned` spec is never one of NodeCreate's kinds - so a goal's
  Restart node keeps its own fields), CMakeLists.txt (+3). No new keys.

## Checks (offline)

- Build (own out\build\verify, PDB endpoint thug2goals, dependencies from the lead build's sources): no errors /
  warnings. thug2_check on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe: 0 MISMATCHED, early self-tests all passed (478
  after merging main). check_code_writes: no overlaps.
- Self-tests src/hook/selftests_editor_goals.cpp: TestEditorGoalsTypes (points' kinds / counts / spin against the game's
  SetType 0x004E6DA0; names, labels, node names against EditedGoalNodeName), TestEditorGoalsImage (FillImage byte for byte
  against the game's ReadFromStructure 0x004E96D0 reading the same goal - its structure getters stood in - every type,
  two variants), TestEditorGoalsRecords, TestEditorGoalsLayout.
- Mutations (`node tools/mutate.ts out/scripts/mut_goals.ts`): **48/48 caught** (2 after strengthening the tests: swapped
  type checksums, text after a quoted value).

## In-game test checklist (for the user) - NOT run in the game yet

Watch `thug2-decomp.log` (`level editor:` lines; EXCEPTION, RUN-TIME CHECK). Free skate or HIGH SCORE / FREE SKATE.
1. `node tools/game/edit_level.ts TR`. Left panel **Goals**: pick SKATE letters, **New goal**: a pro point, a start
   and S K A T E points ahead, labelled ("SKATE letters: Pro", "S"...), the Inspector shows the goal (Complete).
2. Move points (drag / arrows / G / Z X); undo (Ctrl+Z) a move and the goal's creation.
3. Change the name, ped name, goal text, win message, time limit; letters' height (the line to the floating letter).
4. Leave the editor (Esc): log `goal ed_goal01 (Skate, ...) in play as CreatedGoal20`; a ped stands at the pro point;
   talk to it: the goal starts (its texts), letters appear where placed at their height; collect them: win message.
   No crash on win (FlagGoalAsWon finds nothing).
5. Re-enter the editor: log `... goals taken out of play`; the ped is gone; leave again: it's back.
6. Each other type once: COMBO letters (in one combo), High Score (score set), High Combo, Race (add checkpoints, laps,
   per-checkpoint time: cones / waypoints in order), Gap (tick a level gap - TR or a level with gaps; Required trick),
   Skate-Tricks / Combo Skate-Tricks / Tricktris / Trick to the Beat (tick key combo sets - without one it says "Not
   played: Choose at least one key combo set").
7. Control = Walk on a high score goal: the goal starts walking.
8. Ctrl+S; the layout file has `goal ...` lines. Quit to the menu; level select > EDITED LEVELS > TRAINING (EDITED): the
   goals are there and playable without opening the editor. TRAINING as shipped: no goals, no peds.
9. Story mode in that level / a career save afterwards: no trace of the goals (no extra goals in the goal list).
10. **Copy to Create-a-Goal** on a complete goal: message; then CREATE-A-MODES > create goals > TRAINING: CREATED GOALS
    lists it; Test goal works; SAVE GOALS saves it (optional - only if you want it in the save).
11. Reset level (Ctrl+Shift+Delete): goals gone (and out of play); Ctrl+Z brings them back.
12. Load another level from an edited copy with goals in play: no crash, no leftover goal in the new level.
13. Screenshot sidecar: a `goals` line.

## Known limits / left

- Nothing run in the game: the riskiest parts are the goal manager taking a goal made outside create-a-goal mode in
  free skate / single session (the same code net / split-screen games use), the pro ped made from an overlay node, and
  removing a running goal when the editor opens.
- Key combo sets are added as Create-a-Goal's menu adds them (no per-set spin / taps / perfect - neither has the menu).
- Gap goals keep gap names; their numbers are the level's gap list order when played (a gap no longer there is left out
  and logged).
- No "test goal" button in the editor (leave the editor and talk to the pro). Deleting a point node doesn't change the
  goal (Restore points shows it again).
- The goal editor's own 3D cursor / marker objects aren't used: the editor's point nodes stand in for them.
