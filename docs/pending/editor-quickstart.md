# Level editor quick start, autosave and launcher - editor-quickstart agent

Branch worktree-agent-a958414de333bfff8. Not a decomp batch (ours, at the user's request). **NOT run in the game yet**
(all four workspaces were busy; offline checks only). Shared files touched (small, additive): src/hook/log.h / log.cpp
(SetCrashHook), src/game/gfx/movie.h / movie.cpp (SkipIntroMovies also on with -edit), CLAUDE.md (the ini line),
tools/game/README.md (one entry).

## What
1. **`-edit <level>`** (also `--edit <level>`, `--edit=<level>`, ini `[features] edit_level=<name>`): as `-level` -
   free skate in that level - with the intro movies skipped for that run, and the level editor opened once the level
   is up. Leaving the editor is as usual (the pause menu, then free skate). Precedence: command line `-edit`, then
   `-level`, then the ini's `edit_level`, then `start_level`.
2. **Autosave**: while the editor has unsaved changes, `thug2-decomp-layouts\<scene>.autosave.txt` (the same layout
   format and writer) is written every 60 s and on leaving the editor, and from the crash logger. `<scene>.txt` is
   never written automatically. Ctrl+S saves as before and deletes the autosave. Entering the editor with an autosave
   newer than the saved layout (or with no saved layout) that this run didn't write: the HUD says "Unsaved changes
   from a previous session: Ctrl+R restores, Ctrl+Shift+R discards" (20 s; the status line keeps a reminder);
   Ctrl+R puts the current layout back as the level has it and the autosave in its place (undo history cleared),
   Ctrl+Shift+R deletes it. While it is offered, nothing is autosaved over it (so an ignored offer can't be lost by
   new edits - those are then only kept by Ctrl+S, or once Ctrl+R / Ctrl+Shift+R decided). F1 lists the keys.
3. **Crash**: the crash logger (hook/log.cpp CrashLogger) now calls a hook after logging (also past its limit of 4
   logged exceptions, so handled ones earlier can't keep a real crash's autosave from being written); the editor's logs
   `level editor: running at the exception; last action: <...>` and writes the autosave from a text made after each
   change (plain memory, written with CreateFile / WriteFile / MoveFileEx, no allocation, inside __try / __except, a
   re-entry guard). The 60 s autosave stays the fallback. "Last action" is a 128-byte buffer set at: entering /
   leaving, clicks (what is selected), drag ends (where to), turns, drops, Ctrl+D, Delete, Home, browser placement,
   undo / redo, Ctrl+S, Ctrl+R / Ctrl+Shift+R.
4. **Launcher** `node tools/game/edit_level.ts [<level>] [--game <folder>] [--list]`: no level (or `--list`) lists the
   installed levels by game (THUG2's own and the overlay's, then THPS4, THUG1, THPS3, THAW: load name and name);
   a level starts the game detached with `-edit <level>`. Levels come from the overlay's Levels.qb (else the game's
   own Data\scripts\game\Levels.qb) read with tools/level_port/qb.ts; the ported levels' games from the staging
   folders' levels.json. Levels without a scene file (Default, EmptyDefault) are left out, as the DLL would refuse
   them; Load_MainMenu and Load_Sk5Ed (the created park) too. Refuses when a THUG2 / THUG2PM process runs from that
   folder (or one whose path can't be read); never closes one. Notes when the ini has level_editor=0. Unknown names
   get close matches.

## How (the hooks)
- `-edit`: src/game/front/start_level.cpp. The command line is still read at DLL load (OptionValue - now only whole
  words: `-edit` inside a path doesn't count). After `Load` (the same moment `-level` uses) a small state machine in
  StartLevel::Update (already called once a frame from MouseMenus::Update, in the front end and in levels): once
  `Nx::CurrentLevel() != 0`, the Skater exists and the game is unpaused for 90 frames, it checks
  `LevelEditor::Enabled()` and that the overlay's `level_editor_enter` exists (else logs and stays free skate), then
  sends `pad_start` exactly as the Start button does (MouseMenus::SendMenuPadEvent -> root_window's
  handle_start_pressed: PauseGame, create_pause_menu). When FrontEnd::m_paused has been true for 20 frames it runs
  `level_editor_enter` (what the pause menu's LEVEL EDITOR item runs: LevelEditor_Start, the menu taken down, the
  HUD). If the game hasn't paused 120 frames after a press (a level intro camera, menu input blocked), Start is
  pressed again, at most 5 times, then it logs and gives up (free skate; the pause menu's LEVEL EDITOR still works).
  Log lines start with `edit level:`.
- skip_intro_movies: `Nx::SkipIntroMovies()` is `ini || StartLevel::EditRequested()`.
- Autosave: src/game/front/level_editor_layout.cpp (Capture / Apply / ResetToLevel split out of Save / Load;
  Snapshot, WriteAutosave, WriteAutosaveFromCrash, AutosaveOffered, RestoreAutosave, DiscardAutosave, LevelLoads);
  level_editor.cpp (MarkUnsaved replaces `g_unsaved = true`; the snapshot is rebuilt at the start of the frame after a
  change; the 60 s timer in Update; Ctrl+R in the Ctrl block - R alone still moves a selection up, Ctrl zeroes that;
  g_unsaved is dropped when the editor starts in a level loaded since the changes).
- Copies of objects on restore: one already there under the autosave's name is moved, not made twice; copies not in
  the autosave are removed. Copies of sectors already there are hidden and made again from the autosave (a hidden copy
  isn't saved).
- Pure parts and tests (src/hook/selftests_editor.cpp TestEditorAutosave, early): SavedFileName / AutosaveFileName,
  AutosaveNewer (newer, older, equal, no layout, no autosave, 64-bit file times), StartLevel::OptionValue (-edit,
  --edit, --edit=, a quoted name, -level isn't -edit, -edit inside a path, no value). Proved with tools/mutate.ts (5/5
  caught: >= for >, no-layout case, autosave name = layout name, the word-boundary check, the quote handling). Record
  round trips: no new records (the existing layout round-trip test covers the format). tools/game/edit_level.test.ts:
  findLevel, formatList, resolveGameFolder / findExe.

Found on the way: the layout header mentioned Ctrl+O (load the saved layout) but no key was bound - the layout only
loads with its level. The mention is removed; Ctrl+R is the only "load over the current layout" key.

## Checks
- verify build clean (no warnings in the touched files); `thug2_check` on THUG2PM.exe and on the stock THUG2.exe:
  early self-tests all passed (340); `node tools/check_code_writes.ts`: no overlaps; npm typecheck / lint / test pass.
- `node tools/game/edit_level.ts --list` against C:\Games\THUG2 (read only): 17 THUG2 levels, 11 THPS4, 16 THUG1,
  14 THPS3, 15 THAW; Default / EmptyDefault left out. `fuon` -> "did you mean foun?"; `Default` refused.

## Switching it off
- `-edit` / edit_level: just don't pass it (nothing changes without it). The editor: `[features] level_editor=0`.
- No REPLACE entries (nothing to disable with `[replacements] disable=`).

## In-game test checklist (for the user)
Use the main install after the lead installs it (the DLL; the overlay needs no change).
1. `node tools/game/edit_level.ts` - the list shows the levels by game; check names look right.
2. `node tools/game/edit_level.ts foun` - no intro movies, no main menu interaction; Foundry loads in free skate, the
   pause menu flashes up briefly, then the editor is on (HUD, cursor). thug2-decomp.log: `edit level: foun is up -
   pausing (Start) ...`, `edit level: running level_editor_enter`, `level editor: on`.
3. Press Escape (nothing selected): the pause menu; continue: free skate in Foundry, skater below where the camera was.
4. Repeat with `z_ma` (THAW The Mall) and a THUG2 level (`BO` or `TR`). Also `node tools/game/edit_level.ts fuon`
   (refused with a suggestion) and, with the game already running, any level (refused: "ALREADY RUNNING").
5. Autosave after a minute: in the editor move a piece (don't Ctrl+S); wait just over 60 s. The log says
   `level editor: autosaved - ...\thug2-decomp-layouts\<scene>.autosave.txt`; the file exists next to (not instead of)
   `<scene>.txt`.
6. Crash / kill: with unsaved moves made (at least one more after the last autosave), end the game from Task Manager
   (End task). Start again with `edit_level.ts <same level>`: the saved layout (if any) loads with the level; the HUD
   says "Unsaved changes from a previous session: Ctrl+R restores, Ctrl+Shift+R discards"; the status line shows
   "(an autosave: Ctrl+R / Ctrl+Shift+R)". Ctrl+R: the pieces come back where they were at the last autosave (a kill
   loses the moves after it - the crash hook doesn't run on a kill). For the crash path, if you want to try it:
   `debug_editor=1` isn't needed - any real crash while editing logs `level editor: running at the exception; last
   action: ...` and `the unsaved changes were autosaved`.
7. Ctrl+Shift+R: kill again with unsaved changes, start again, press Ctrl+Shift+R - "Autosave discarded", the file is
   gone, the level stays as its saved layout.
8. Ctrl+S deletes the autosave: make a change, wait for the autosave, Ctrl+S - the log says `autosave deleted`.
9. Leaving the editor with unsaved changes and re-entering in the same run: no offer (it's this run's autosave). Loading
   the level again (level select) and entering: the offer appears (the changes went with the level).
10. `[features] level_editor=0` with `-edit foun`: free skate in Foundry, the log says `edit level: the level editor is
    off ...`.
