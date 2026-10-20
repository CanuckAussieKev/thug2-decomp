# Handoff (2026-10-19, late)

State when the session was handed over (the user changed Claude accounts at 100% usage). Read CLAUDE.md, then this,
then the "- [ ] Next:" line of docs/decomp-progress.md.

## Where things stand
- **main** (pushed; installed in C:\Games\THUG2 at 65a062f with the overlay regenerated) has:
  - batches up to 155;
  - the Default-level fix (docs/pending/defaultlevel.md);
  - CREATE 100% CAS (docs/pending/cas100.md);
  - NeverScript `-lenient`;
  - the leaked-source ban.
- **Decomp:** 28.27% before the object-system merge (docs/PROGRESS.md). The next batch number is **156**.

## Update 3 (2026-10-20): the main install changed
- **The main install changed.** C:\Games\THUG2 is now a stock install (the user copied it from
  C:\Games\THUG2-THPSX-ORIGINAL).
  - The game is in **C:\Games\THUG2\Game** (THUG2.exe: an unwrapped no-CD exe, never modified).
  - Our files went in on 2026-10-20: the DLL, the ini from the old install (self-tests off, anteara_mod=0), the
    converted levels, and the overlay rebuilt with make_menu_level.py.
- **The old main install is C:\Games\THUG2_PM_BAK** (THUG2PM.exe; the user's backup - don't write to it). Its Backup
  folder holds the user's Anteara backups (scripts_2010, scripts_2011, scripts_unified) and its Save folder their old
  saves.
- C:\Games\THUG2-THPSX-ORIGINAL is the stock install's untouched backup. The lead's test files there were removed;
  install_manifest check shows it as installed.
- **Tools and defaults:**
  - The tools' defaults point at C:\Games\THUG2\Game, and they step into a stock install's Game folder (Python
    --game/--thug2, tools/game/*.ps1, CMake's copy step).
  - Analysis tools and thug2_check default to C:\Games\THUG2_PM_BAK\THUG2PM.exe.
  - Workspaces THUG2-1..4 still hold THUG2PM.exe. Refreshing one now copies the stock install's Game folder (a
    THUG2.exe) - both work.

## Update 2 (2026-10-19)
- **Anteara Mod is part of the project**, merged from branch anteara-mod.
  - It's switched by `[features] anteara_mod=1` (default 0); the main install's ini has `anteara_mod=0`.
  - tools/anteara (README) holds only the mod's own changes. make_menu_level.py builds the mod's script set into
    the overlay's `anteara` folder from the game's loose Data\scripts.
  - skip_intro_movies also skips the mod's start-up movie.
  - The record is docs/pending/anteara.md (not a decomp batch).
- **A stock no-CD THUG2.exe runs the project unmodified.** Tested in C:\Games\THUG2-THPSX-ORIGINAL (the user's stock
  install, with our files added):
  - tools/game finds either exe;
  - `install_manifest.ts check` names the executables;
  - the early self-tests put THUG2PM.exe's bytes in the 10 differing spots while they run.
- **The build copies SDL2.dll and gamecontrollerdb.txt into the game folder.** Both are byte-for-byte PartyMod's,
  downloaded from upstream and checked by SHA-256.
- Installed in C:\Games\THUG2 at 3cc9d3b, with the overlay regenerated.

## Update (later the same night): items 1 and 2 below are DONE
- Object system: merged and folded as batches 156-170, so progress is 31.27%. It ran 70 s in Boston in THUG2-4
  (2790 replacements, every self-test passed, no exceptions). It is installed in C:\Games\THUG2. Its cutscene code
  (OS-11..15) hasn't run in the game yet: the user should play a story cutscene.
- scan_register_assumptions lists 2 RISK entries, both from before this merge:
  - 0x004E69D0 InitEditedGoalStub is handled (it keeps ECX);
  - 0x00477F70 CStruct::AddVector(uint32_t, VectorValue): its caller at 0x004EFF92 pushes ECX after the call. Look
    at this.
- Anteara merge: finished. The mod is in C:\Games\THUG2\Backup\scripts_unified (NOTES.md there); THUG2-3 is restored
  and unlocked.
- All workspaces are free; no agents are running.

## In flight - pick these up (as of the handoff; see the update above)
1. **Object system & script runtime agent - its range is finished, not merged yet.**
   - Branch `worktree-agent-ac4285dede37d3675` (worktree `.claude/worktrees/agent-ac4285dede37d3675`), pushed. It has
     batches OS-1..OS-15, covering every function in 0x45EFD0-0x47DC40 (find_gaps.py lists nothing there). It already
     merged main once.
   - Not done: its final game run (it ran once, at OS-11, in THUG2-4: clean) and its final report.
   - To do:
     1. Merge the branch, build, run thug2_check, scan_register_assumptions and check_code_writes.
     2. Do a game run in THUG2-4 (a cutscene if possible).
     3. Fold the records: `node tools/fold_pending.ts docs/pending/objsys.md OS "objsys agent"` (from 156).
     4. Regenerate PROGRESS.md, commit, push, and install into C:\Games\THUG2 (see below).
2. **Anteara Mod merge agent (about 75% done when the session ended).** This is the user's old script mod: the 2010
   and 2011 backups are in C:\Games\THUG2\Backup\scripts_2010 / scripts_2011.
   - The user asked for one unified scripts folder, C:\Games\THUG2\Backup\scripts_unified:
     - 2011 as the base;
     - every 2010 feature brought back, the Polish skejt23.qb left out;
     - the 2010-only menu features in a "2010" submenu of the Anteara pause menu;
     - 2011's bugs fixed (lost randomrange in sk4_scripts / skater_vehicle / skater_fx, the missing
       SPECIALGRIND_SCORE, cas_skater_f's stray byte);
     - calls that never worked because the mod's old compiler stored their checksums byte-reversed, fixed;
     - the recovered original names used;
     - a NOTES.md listing everything;
     - .qb files plus qdir.txt plus src\*.ns.
   - The work is in the session scratchpad
     `C:\Users\kevin\AppData\Local\Temp\claude\C--Dev-thug2-decomp\c861af32-ae53-4deb-9c82-1d00b291c5a6\scratchpad\anteara_merge\`
     (`unified\qb`, `unified\qdir.txt`, make_unified.py and the checks). Built and checked:
     - all 218 files compile and decompile again;
     - the 195 unchanged files match 2011;
     - the merge calls nothing undefined.
   - In the game: loaded in THUG2-3 with no errors (2010's intro movie and theme colours show).
   - **Left:**
     1. Screenshots of the pause / Anteara / "2010" menus.
     2. Restore THUG2-3: `install_ws3.ps1 -Action restore` in that folder, then release its lock
        (`tools/game/game_lock.ps1 -Action release -Game C:\Games\THUG2-3`). **THUG2-3's lock is still held (owner
        anteara) and the mod is still installed in its overlay.**
     3. Copy to Backup\scripts_unified and write NOTES.md.
   - If the scratchpad is gone, restart from the inputs below.
   - **Inputs** (also scratchpad):
     - `anteara\final\2010|2011` - decompiles with names restored (non-ASCII text lost - the agent restored it from
       the raw decompiles);
     - `anteara\ns\retail` - retail decompiles;
     - `anteara\diff_data.json` - the 2010 vs 2011 comparison, published at
       https://claude.ai/artifact/8QmkVoLZ5jAukTxCmtYP8J.
   - Recovered original names (exact checksum matches):
     - natas_spin (#70322399), natas_extra_tricks (#6f9f0796), freeman_bound (#3c97b174), anteara_pipe (#fcaa1c10);
     - make_skater_lol (#5d84ddbd), ped_chase_count (#a3c76f73), anteara_explode (#33f42e93), bone_breast
       (#b5a27768);
     - getup_ped_chase (#33a24b3f), ped_chase_end (#a90ad5a5), skater_bail_chase (#eaf66a13);
     - #07b12bd2 is unknown (only a collision matched).

     FREEMAN_BOUND (flag 323) is defined and set by 2011's "No Out Of Bounds"; it isn't a bug.
3. **Flaky early self-tests (to investigate).** In single game runs (not offline), agents saw early self-tests fail
   once and pass on re-runs:
   - "skate net registration" and "skate net handlers" (selftests_tod4);
   - "anim controllers: partial", "text elements: updates" and "texture / sprite / window elements: updates";
   - "text blocks: construction, fonts, lines".

   They probably depend on the order or state they run in. The main install has self-tests off, so the user doesn't
   see them.

## Waiting on the user
- **CREATE 100% CAS** (GAME OPTIONS, after GAME PROGRESS; `[features] create_100_cas=0` turns it off): save, quit and
  load, then check VIEW STATS still allows 10/10. The stat challenges' done state may not be saved. Also check that
  story levels show their goals done.
- **Default level:** gone from the level select in debug mode. Its scene and node array were never shipped: not in
  the PC discs' cabinets and not on the Xbox disc (checked via archive.org dumps). TEST LEVEL is a different level.
- Older playtests: the network fix (batch 122) and the four games' ported scripts / moving objects, time of day, the
  exact-assets plan (docs/level-port-compromises.md section 8), and wireframe tier 2 (on hold: "don't start tier 2
  yet").

## How work is done
- Parallel agents: one git worktree + branch each, one workspace each (C:\Games\THUG2-1 ... -4), the rules in
  docs/agent-rules.md. Pass on both bans: private Morten1337 repositories, and leaked Neversoft source.
- Builds in parallel: each build uses its own PDB server (`set _MSPDBSRV_ENDPOINT_=<name>`) and `-j 2`. Never kill
  mspdbsrv.
- The lead builds out\build\lead (a .cmd: VsDevCmd -arch=x86, endpoint thug2lead,
  `cmake --build out\build\lead -j 2`). Mutation runs can filter tests with THUG2_SELFTESTS (decomp-batch skill).
- The lead merges, builds and checks, folds the docs/pending batches (`node tools/fold_pending.ts`), regenerates
  PROGRESS.md, commits and pushes. It then installs main into C:\Games\THUG2: copy out\build\lead\dinput8.dll over it,
  then `python -I tools/menu_level/make_menu_level.py --game C:\Games\THUG2`. The user's own manual-test folder: no
  test runs there, and its self-tests stay off.
- Game test scripts: tools/game (README there). Launch only when allowed (workspaces under the lock are allowed),
  never capture the whole screen, never close a game you didn't start, and hand interactive tests to the user.
- `.claude/worktrees/` holds finished agents' worktrees (defaultlevel a99dc09a…, cas100 a778032d…, merged). They can
  be removed with `git worktree remove`.
