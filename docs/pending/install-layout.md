# Tools: an install root or the game folder itself (no batch - tooling only)

The user (2026-10-21): some players delete what's outside a stock install's `Game` folder (launcher, docs, readme) and
keep the game at the top - the tools should take either layout, for every game.

- **tools/lib/install.ts** (new, shared): `findInstall(dir, game)` / `resolveInstall(dir, game)` /
  `installError(dir, game)` / `findCaseless(dir, ...parts)`; games `thug2`, `thps3`, `thps4`, `thug1`, `thaw`. The
  folder itself is tried first, then its `Game` folder; markers matched without regard to case: THUG2 `THUG2PM.exe` /
  `THUG2.exe` / `Data\pre\qb_scripts.prx`; THPS3 `Data\Levels`; THPS4 `data\levels` + `data\skeletons` (THPS3 has a
  Data\Levels too); THUG1 `Data\pre\qb.pre`; THAW `data\worlds\worldzones` / `data\pak\qb.pak.wpc`. `resolveInstall`
  exits (SystemExit) with "<dir> doesn't look like a THAW install: expected ... in it or in its Game folder"; it is
  idempotent and cached, so the converters' inner functions resolve whatever folder they're handed.
- **tools/game/game_exe.ps1**: `Find-Install` / `Get-InstallError` with the same markers; `Resolve-GameFolder` uses it.
  load_check.ps1 now resolves `-Game` too (its log path assumed the game folder); "NO GAME IN" messages say what was
  expected.
- **CMakeLists.txt** (shared, small): THUG2_GAME_DIR documented as the folder holding the exe; the post-build step's
  step-in now tests for the exe or Data\pre\qb_scripts.prx (was: Data).
- Call sites (path-resolution lines only): the four converters' `--thug2` and source-game joins (thps3_levels,
  thps4_levels + thps4_scripts, thug1_levels, thaw_levels + thaw.ts), their thugpro_compare / thugpro_inventory /
  thugpro_edits / thugpro_new / thugpro_extract / thugpro_new_extract / migrate_layouts, make_menu_level,
  level_select_icons, anteara, edit_level (resolveGameFolder), apply_layout (through it), install_manifest (gameRoot),
  make_automation_tables, pick_census, boot_level, extract_members.
- src/: nothing walks up from the exe expecting a Game parent (checked).
- Tests: tools/lib/install.test.ts (both layouts per game, every marker in either case, the error, a game's markers
  not taking another's folder), edit_level.test.ts on temp folders. Checked read-only against the real installs: every
  converter's selfcheck with the root and with the Game folder (THPS4, THUG1, THAW) and `--thug2 C:\Games\THUG2`.
