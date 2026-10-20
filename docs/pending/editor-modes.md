# Level editor and edited levels only outside career-type modes (NOT ORIGINAL - the user's rule, 2026-10-21)

Branch `worktree-agent-ae1301f605aa94f52`. **NOT run in the game yet** (no game runs for editor work): built and
checked offline only.

## How the game tells its mode (THUG2PM.exe)

- The game mode object: **0x007CE470** (Mdl::Skate's). Its name: 0x005532A0 (`name=` of the mode's structure - what
  screenshot.cpp / automation.cpp print). Its properties: **0x00553220** `IsTrue(property)` = the mode's structure
  (+4)'s integer `property` is non-zero.
- The script commands are thin wrappers over it: **GameModeEquals** 0x005A11D0 (each checksum argument through
  0x00553220), **IsCareerMode** 0x005AEFC0 (`IsTrue(is_career 0x1DED1EA4)`), GetGameMode 0x005A2030; InNetGame
  0x005A0D80, InSplitScreenGame 0x005A1180.
- The modes (gamemode.qb, decompiled with NeverScript): mode_freeskate / mode_freeskate2p (`is_freeskate`),
  mode_singlesession (`is_singlesession`), mode_career (`is_career` - story), mode_classic (`is_classic`),
  mode_creategoals (`is_creategoals`), mode_parkeditor (`is_parkeditor` + `is_creategoals`), the 2 player competitive
  modes (trick attack, horse, king... - none of those flags), and every network mode (`is_net`).
- How a player gets each: STORY MODE -> change_gamemode_career, CLASSIC MODE -> change_gamemode_classic, **HIGH SCORE /
  FREE SKATE -> change_gamemode_singlesession** (vanilla THUG2 has no 1-player freeskate menu item: single session IS
  its free skate, until the pause menu's START HIGH SCORE RUN), 2 PLAYER -> freeskate2p, CREATE-A-MODES ->
  singlesession then creategoals; mode_freeskate only from launch_cas and our SKATE NOW / `-level` / `-edit`
  (start_level.cpp). All set before the level select appears.

## What records career progress (task item 2)

- **Gaps: in every mode.** The gap landed (0x00512A60.. -> the skate module's checklist 0x005656C0 -> GotGap
  0x00578C70, AwardPendingGaps 0x005796C0) has no mode check: free skate and single session credit the career's gap
  checklist, which the save writes. **The gap-save filter of docs/pending/editor-gap-saves.md stays the guard there.**
- **High scores / records:** only the script command UpdateRecords (0x005AEBC0 -> 0x00566F40, the only caller of the
  record tables' MaybeNewEntry / MaybeNewRecord), called by goal_timed_score_run.qb (the single session high score run),
  classic_mode_menu.qb, trick_attack.qb, combo_mambo.qb. Free skate never calls it; single session does at the end of a
  high score run. **New guard:** UpdateRecords is now ours (src/game/skate/records.cpp, REPLACE 0x005AEBC0): while
  `Layout::EditingCopy()` nothing is recorded (+0x384 "a new record" cleared as the original does first; logged
  `records: not updated - the level is the level editor's edited copy ...`).
- Goals, cash, stat points, chapters: story (cash on a goal win 0x005576D0, goal flags) and classic (stat points) -
  refused modes. Create-a-goal saves goals with the level - refused.

## The rule (`Layout::ModeRefusal`, a whitelist)

Offered in: **free skate** (1 or 2 players, `is_freeskate`) and **single session** (`is_singlesession`). Refused (with
the reason in the log): story mode, classic mode, create-a-goal, the park editor, network modes, any other mode ("a
competitive game mode"), no mode at all. A career-type or `is_net` flag wins over free skate / single session.
`Layout::Refused(why)` = a network session (as before) or a refused mode; used everywhere the network guard was:

- `LevelEditor_Available` false -> no LEVEL EDITOR in the pause menu.
- `LevelEditor_Start` / `EnterEditedCopy` refuses: log `level editor: not started - The level editor isn't offered in
  story mode - only in free skate or single session ...` and the same as a panel message on screen.
- `-edit <level>` / edit_level.ts: unchanged (they load the level in mode freeskate); a safeguard in start_level.cpp
  logs `edit level: the level editor isn't offered here (...)` and shows it if the mode refuses.
- `LevelEditor_EditedLevels` returns nothing -> no EDITED LEVELS section (the overlay's level_select_get_section_list
  already adds it only when the command returns true - **no overlay change, make_menu_level.ts needn't be rerun**).
- `LevelEditor_PickLevel`: an `edited_layout` pick loads as shipped (`... an edited copy picked - refused, the level as
  shipped (story mode: edited levels are free skate / single session only)`).
- Tick (a level up), Load, the in-place switch, the node-array field edits, the autosave offer, the editor's trigger
  volumes (Triggers::Guard): nothing put in place.
- **The mode changing with an edited copy loaded (item 4):** a new level load already ends the edited copy
  (OnSceneLoaded) and Tick refuses the layout in the new mode. If the mode changes without a reload, Tick puts the level
  back as shipped at once (ResetToLevel, the career's gap list put back - Career::EditedCopy(false)), logged `level
  editor: the game mode is now story mode - the edited copy put back as the level as shipped ...` (not while the editor
  runs - the mode can't change then).

## Files

- src/game/front/level_editor_layout.{h,cpp}: ModeFlags, ModeRefusal (pure), CurrentMode, CareerModeActive, Refused,
  PanelMessage; the guards; Tick's mode-change revert.
- src/game/front/level_editor.cpp (Available, Start's message), level_editor_triggers.cpp (Guard), start_level.cpp.
- src/game/skate/records.{h,cpp}: UpdateRecords 0x005AEBC0 (new REPLACE; switch off: `[replacements] disable=records.cpp`
  - that also turns off the records' other rewrites - or by its address 0x005AEBC0).
- New self-tests src/hook/selftests_editor_modes.cpp (CMakeLists.txt +1 line): `TestEditorModes` (every gamemode.qb
  mode family classified), `TestEditorModesUpdateRecords` (original vs ours with 0x00566F40 stood in; the edited copy
  records nothing).

## Checks (offline)

- Build (own out\build\verify, own PDB endpoint `thug2modes`): no errors / warnings.
- thug2_check on THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe (read-only): 3288 replacements, 0 MISMATCHED, early
  self-tests all passed (409).
- `node tools/check_code_writes.ts`: no overlaps (26 run-time writes, 3286 rewritten functions).
- Mutations (`node tools/mutate.ts out\scripts\mut_modes.ts` in the worktree): **13/13 caught** (each refused mode
  dropped, single session / free skate refused, the whitelist's fallback allowed, the edited-copy branch off, the flag
  kept, the result false, the call dropped).
- The game-side parts (CurrentMode through 0x00553220, the guards, Tick's revert, the panel message) need the game:
  the checklist below.

## In-game test checklist (for the user) - NOT run in the game yet

Watch `thug2-decomp.log` (`level editor:`, `records:` lines; `EXCEPTION`, `RUN-TIME CHECK`).
1. **Single session (HIGH SCORE / FREE SKATE):** load TR, pause: LEVEL EDITOR is there and opens. The level select
   (pause > CHANGE LEVEL) shows EDITED LEVELS (if you have a layout); TRAINING (EDITED) loads with the layout.
2. **High score run in an edited copy:** in that edited TR, pause > START HIGH SCORE RUN, score something, let it end:
   no new high score / initials entry; log `records: not updated - ...`. The same run in TR as shipped records as usual.
3. **Story mode:** start/continue a story, pause: no LEVEL EDITOR. Pause > change level (chapter map / level select):
   no EDITED LEVELS section.
4. **Classic mode:** same as 3 (no LEVEL EDITOR, no EDITED section in its level select).
5. **Create-a-goal** (CREATE-A-MODES > create goals, a level): no LEVEL EDITOR.
6. **2 PLAYER free skate:** LEVEL EDITOR offered (free skate) - check it behaves (or tell us to refuse split screen too).
7. **-edit:** `node tools/game/edit_level.ts TR` still opens the editor in TR (free skate), as before.
8. **Mode change after an edited copy:** play TR (EDITED) from single session, quit to the main menu, start STORY MODE:
   the story's levels load as shipped (log `... loaded as shipped (story mode: ...)` for a level with a layout), and the
   gap list in story has no editor gaps.
9. **Gaps (unchanged guard):** in an edited copy in single session, land a level gap, save: not saved landed
   (editor-gap-saves' list still applies).

## Known limits

- Mode changes without a level reload are not known to happen in vanilla menus; the Tick revert is a safeguard.
- 2 player free skate offers the editor (is_freeskate) - not tested with two skaters.
- `[replacements] disable=` of records.cpp / 0x005AEBC0 lets an edited copy's high score run record again.
