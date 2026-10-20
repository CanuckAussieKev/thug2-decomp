# Level editor: edited copies, Reset level, rails (NOT ORIGINAL - the user's requests)

Branch `worktree-agent-a315ad23347b03839`. **NOT run in the game yet** (the user tests the editor by hand). Built and
checked offline only; one early load check in C:\Games\THUG2-1 showed `-level TR` loading Training as shipped and
`level_select_change_level level=Load_TR edited_layout` loading its edited copy (the user's TR layout: 1 piece, 3 rail
points), and the toolbar reading "Training (edited copy)" - then game runs were stopped at the user's request.

## 1. Reset level

- **Ctrl+Shift+Delete**, or the toolbar's **Reset level**: a window "Reset the level?" (Cancel / Reset level) says what
  happens. Old HUD (`editor_ui=0`): the message "Reset the level? ... Ctrl+Shift+Del again" - a second press within
  5 s resets.
- It puts everything the editor changed in this level back as the level has it (`Layout::ResetToLevel`, as an autosave
  restore does): moved scenery and its rails, rail points moved by themselves, moved objects, node moves; hidden scenery
  and nodes shown; copies of objects removed, copies of scenery hidden (the engine can't free them), added nodes
  removed. Then `thug2-decomp-layouts\<scene>.txt` and `<scene>.autosave.txt` are deleted (logged).
- **Undo:** one undo step (`kLevel`) holds the layout as it was (the text Capture/Write make): Ctrl+Z puts it all back
  (not saved - the files come back only with Ctrl+S), Ctrl+Y resets again. The undo steps from before the reset are
  dropped (they were made on what the reset replaced). The confirm text says this.
- F1 (both helps) lists it.

## 2. Edited levels: a level of their own, never online

Design (also at the top of `src/game/front/level_editor_layout.h` and in `level_editor.h`):

- A layout is the level's **edited copy**. The level as shipped always loads as shipped - from the level select's own
  entries, `-level`, the automation's `level`, career, scripts. One edited copy per level (per scene: the file name
  stays `<scene>.txt`, so existing layouts - the user's TR `4B7E41E2.txt` - are the edited copies of their levels).
- **Level select:** a new section **EDITED LEVELS (OFFLINE)** after the games' sections (Up from the first section,
  Up / Down cycles), present only while there is at least one saved layout. Its entries come from the DLL's
  `LevelEditor_EditedLevels`: for each saved layout, the level's own entry (from `level_select_menu_level_info` and the
  ported levels' lists - sign, star, level number) with the text "<TEXT> (EDITED)" and the flag `edited_layout`. The
  scene of an entry: the level structure with `load_script=<its level>` and its `level="TR"` text (the scene's checksum
  is that text's - CFunc_LoadScene names the scene by it); found by walking the script symbols.
- **Choosing:** the overlay's `level_select_change_level` starts with `LevelEditor_PickLevel <...>`: the DLL keeps the
  load script when `edited_layout` was chosen (else clears it). A second after the level is up (`Layout::Tick`, as
  before) the layout is put in place only when the level loaded is the one picked as edited and no network session is
  on; another level ends the request (so TR from career later is shipped). Picking the level already loaded (the level
  select doesn't reload it on a CD build) switches in place: to the edited copy (layout applied) or back to shipped
  (everything put back), once the menus are gone.
- **LEVEL EDITOR** in the pause menu of a level as shipped, or `-edit <level>`: the level becomes its edited copy
  (its saved layout applied first if there is one; the file is made by the first Ctrl+S). The toolbar shows
  "**<Level> (edited copy)**" in blue (tooltip: what that means, whether it's saved yet, the scene); the old HUD's top
  line "EDITING <Level> (EDITED COPY)".
- **Network - never:** `Layout::NetworkActive()` = online (Net::Manager) or a network game (GameNet 0x0044E4F0, host or
  client) or the last scene loaded for one (`isNet`). Then no layout is applied (Tick, Load, the in-place switch),
  `LevelEditor_PickLevel` refuses `edited_layout` (logged), `LevelEditor_EditedLevels` returns nothing,
  `LevelEditor_Available` is false (no LEVEL EDITOR) and `LevelEditor_Start` refuses. The script side also leaves the
  section out when `in_server_options = 1` (the host's level choice) or `InNetGame`.
- Log lines: `level editor: level picked - load_tr (its edited copy | as shipped)`, `level editor: Training loaded as
  shipped - its edited copy is the level select's (EDITED) entry`, `Layout loaded: ...`, `switched to the edited copy
  in place`, `switched back to the level as shipped in place`.

## 3. Rails

- **Where they're shown:** the node layer (`level_editor_nodes.cpp`) now reads rail and ladder points from the rail
  manager (where skaters grind: a moved sector moves its rails' points there, the node array's `pos=` keeps the
  level's). Read again whenever rail points move (`Scenery::RailGeneration`) - so the gizmo lines, the selection dot,
  the Inspector's Position and the move arrows follow the scenery.
- **Moving rail points:** `NodeEdit::Movable` now takes rail nodes the rail manager has. A rail point moves like other
  nodes (drag, arrows, W A S D / Q E nudge, Ctrl / Snap 25, G and the Inspector's / toolbar's Ground, the Position
  field), not turned (no heading field; Z / X do nothing). **By default the whole rail moves** (every point linked to it
  through the rail manager's next / prev, by the same offset); **L** (or the Inspector's "Whole rail (L) - n points")
  toggles to the point by itself. Rail points within 1500 units show a small dot and are picked before segments.
- **Stored** as the layout's existing `rail <name> 0 <dx> <dy> <dz> <hidden>` records (a plain move from where the
  level has it, through Scenery's rail tracks - the same records a sector move writes), not `set` records; the node's
  `pos=` isn't written. Undo / redo (one step for all moved points), **Home** puts the point (its whole rail when Whole
  rail is on) back where the level has it, Reset level too. Moving the scenery still carries its rails (unchanged).
- Ladders (ClimbingNode) are shown where the rail manager has them but aren't movable; paths neither.
- `[features] debug_editor=1`: on entering the editor the log lists rail points away from their nodes' `pos=` (where
  shown) - checks the user's SkWare_Rail01 case.

## Files

- `src/game/front/level_editor_layout.*` - the edited copy (pure: `ApplyEditedCopy`, `RequestAfterLoad`,
  `RequestFromPick`, `NetworkSession`, `EditedText`, `SavedLayoutScene`, `SceneOfLevelText`; game side: `NetworkActive`,
  `EditingCopy`, `HasSavedLayout`, `LevelTitle`, `EnterEditedCopy`, `ResetLevel`, `PutBack`, `CaptureText`, the script
  commands `LevelEditor_PickLevel` / `LevelEditor_EditedLevels`).
- `src/game/front/level_editor.cpp` - Reset level (key, confirm, undo step `kLevel`), the edited copy on Start, net
  refusal, Whole rail (L), rail moves / Home / undo over the moved points, rail point dots, HUD / help lines.
- `src/game/front/level_editor_ui.*` - toolbar label and Reset level button, the confirm window, the Inspector's Whole
  rail checkbox (no heading for rails), the toolbar's Ground enabled for movable nodes, help rows.
- `src/game/front/level_editor_scenery.*` - `RailPoint`, `RailOrigin`, `SetRailPoint`, `RailOf`, `RailGeneration`,
  `ForEachRailPoint`.
- `src/game/front/level_editor_node_edit.*`, `level_editor_nodes.cpp` - rails through those.
- `tools/menu_level/level_select.ts` - the EDITED LEVELS section (dynamic section count) and the `LevelEditor_PickLevel`
  line. **The overlay must be made again** (`node tools/menu_level/make_menu_level.ts --game <folder>`) together with
  this DLL: an older DLL lacks the two script commands the new overlay calls, and an older overlay never picks the
  edited copy (layouts would then never apply - shipped only).
- Shared files: `CMakeLists.txt` (one line: `src/hook/selftests_editor_levels.cpp`).

## Checks (offline)

- Clean build (own dir `out/build/agent1`, own PDB endpoint), no warnings.
- `thug2_check.exe` on THUG2PM.exe and on C:\Games\THUG2-1\THUG2.exe (stock): 0 MISMATCHED, all 363 early tests pass,
  3 new (`src/hook/selftests_editor_levels.cpp`): `TestEditorLevelsApply` (which level gets its layout, picks, the
  request after loads, a run edited TR / TR again / Boston / TR without a pick, a network game over the edited copy's
  level, the network signs), `TestEditorLevelsFiles` (the (EDITED) text, which files are edited copies, the scene of
  `level="TR"` = the user's 4B7E41E2), `TestEditorLevelsRailRecords` (rail records of a point moved by itself round
  trip; the user's TR layout reads).
- Mutation runs (`node tools/mutate.ts`, spec in the worktree's `out/scripts/mut_levels.ts`, 11 mutations) could not
  complete: the antivirus blocks creating `out\build\verify\thug2_check.exe` in this worktree ("Permission denied"), so
  every mutation stopped at its build step. Not run.
- `node tools/check_code_writes.ts`: No overlaps. `npm run typecheck`, `lint`, `test`: clean.

## In-game test checklist (for the user) - NOT run in the game yet

Install this DLL **and** make the overlay again (`make_menu_level.ts --game <folder>`). Watch `thug2-decomp.log`
(`level editor:` lines; `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`).

1. **Shipped by default:** start TR from the normal level select (THUG2 section, TRAINING). SkWare_Rail01 is where the
   game has it; the log says `Training loaded as shipped - its edited copy is the level select's (EDITED) entry`.
2. **The EDITED LEVELS section:** open the level select (pause menu or main menu free skate), press Up from the THUG2
   section: "EDITED LEVELS (OFFLINE)" with one sign, TRAINING, titled "TRAINING (EDITED)". Choose it: Training loads
   with your rail moved (log `level picked - load_tr (its edited copy)` then `Layout loaded: 1 pieces, ... 3 rail
   points`).
3. **Switching in place:** in the edited TR, pause > change level > THUG2 section > TRAINING: back to the rail as
   shipped (on a non-debug run the log says `switched back to the level as shipped in place`; with debug_mode=1 the
   level reloads). Then EDITED > TRAINING (EDITED) again: the rail moves back.
4. **Editor on a shipped level:** load TR as shipped, pause > LEVEL EDITOR: the layout is applied as the editor opens
   (message "Editing the edited copy of Training - its saved layout put in place"); the toolbar shows
   "Training (edited copy)" in blue (hover: the explanation, "Saved layout: yes"). Old HUD: "EDITING Training (EDITED
   COPY)". Same with `node tools/game/edit_level.ts TR`.
5. **A new edited copy:** a level without a layout (e.g. Boston): LEVEL EDITOR says "A new edited copy of Boston -
   made by the first save (Ctrl+S)"; the tooltip "Saved layout: not yet". Move something, Ctrl+S: Boston (EDITED)
   appears in the EDITED section next time; Boston from the THUG2 section stays as shipped.
6. **Rail gizmo where the rail is:** in edited TR's editor, look at SkWare_Rail01: its rail line (and the dots of its
   points), the Inspector's Position for TRG_Ware_Rail01 / 02 / 51, and the move arrows are at the moved mesh, not the
   old spot. Leave the editor and grind it: the grind follows the mesh. (`debug_editor=1`: on entering, the log lists
   those three points with "shown at" = the rail manager's place.)
7. **Moving a rail:** click a rail point (dot) or a segment: arrows appear, the Inspector shows Position and "Whole rail
   (L) - 3 points" (ticked). Drag it: the whole rail moves; nudge with W A S D / Q E; drag an arrow; G (or the Ground
   button) drops the selected point onto the ground and the rail with it. Press L (or untick): drag one point alone -
   only its segments bend. Grind it after leaving: the grind matches the new line.
8. **Undo / Home / save:** Ctrl+Z / Ctrl+Y undo each rail move in one step (all its points). Home puts the point (the
   whole rail when ticked) back where the level has it - for SkWare_Rail01's points that's the level's original spot
   (the scenery stays moved: Home on the scenery puts that back). Ctrl+S: the layout's `rail` lines have the new
   offsets; reload TR (EDITED): the rail is where you left it.
9. **Moving the scenery still carries its rails:** select SkWare_Rail01 (the mesh) and drag it: the rail's line and
   points follow, and grinding matches.
10. **Reset level:** in edited TR's editor, the toolbar's Reset level (or Ctrl+Shift+Delete): the window explains; Reset
    level: the rail and everything else go back as shipped, the message "Level reset as shipped, its saved layout
    deleted (Ctrl+Z undoes)", the log `reset - deleted ...4B7E41E2.txt` (and the autosave if there was one). Ctrl+Z:
    everything comes back ("Reset undone ... (not saved)"), Ctrl+Y resets again. Without saving, quit to the level
    select: no EDITED section (no layouts) - Ctrl+S after the undo brings the file back. Cancel / Esc closes the window
    without resetting. Old HUD (`editor_ui=0`): Ctrl+Shift+Del shows the question, a second press within 5 s resets.
    **Back up `thug2-decomp-layouts\4B7E41E2.txt` before trying this.**
11. **Never online (by hand, if you test online at all):** in a network game (LAN host / client or online) the pause
    menu has no LEVEL EDITOR, the host's level choice has no EDITED LEVELS section, and a level whose edited copy you
    were playing loads as shipped (log: `loaded as shipped (a network session: layouts are offline only)`).
12. **F1:** both helps list Ctrl+Shift+Delete (Reset level), L (whole rail / by itself) and the edited copy wording.

## Known limits

- One edited copy per level (per scene). A level reached through two entries (the same scene) gets one EDITED entry.
- Undo of Reset level restores the changes for this session only; the deleted files come back with Ctrl+S. Copies of
  scenery made again by the undo are new copies (the old ones stay hidden), as an autosave restore does.
- Rail points don't turn (no angles); ladders and paths aren't movable yet; a level whose rails ride an object (the rail
  manager's flag) has no movable rail points.
- The "Up/Down = Game" helper text now always shows outside classic mode (the section count is only known at run time).
- An in-place switch needs the level select's own same-level path (CD builds); with `debug_mode=1` the level reloads,
  which gives the same result.
