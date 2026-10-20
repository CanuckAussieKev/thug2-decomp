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

## Update 10 (2026-10-20, night): editor feedback - three agents running, two batches queued
- **Main 5e80683 installed** (phase 3, flaky fix, screenshot key). Play-test checklist page (tick results stored in its
  db; read with ArtifactData list `results`): claude.ai/artifact/SQqRbUco71P4fa9AG4eHuk.
- **Running (worktrees; no game runs - the user tests interactive editor work themselves):**
  - A: Reset level; edited levels as separate offline-only levels (no layout on the original or in any network game);
    rail gizmos at the moved position, movable rail points, rail Ground drop. Record docs/pending/editor-levels.md.
  - B: Add palette preview (model viewer). Record docs/pending/editor-preview.md.
  - C: picking - floors / ceilings / walls not picked by a plain click, no-collision pieces hit by triangles not boxes
    (TR's g_wareNew_spiderweb_01 decal box swallowed clicks), decals skipped, pick filters, lock, click-to-cycle,
    hover outline. Record docs/pending/editor-picking.md.
  Merge all three (conflicts likely in level_editor.cpp / _ui.cpp), install, add their checklists to the page.
- **Later the same night:** B (preview, 18059e2) and A (edited levels, 61cbab5) merged, pushed, installed with the
  overlay. The user asked for the REST OF THE BUILD PLAN to be done without agent testing (they test): 3d view options
  (docs/pending/editor-view.md) and phase 4 chains (editor-chains.md) started off 61cbab5; still to start in order: 3e
  scaling (after C merges - it touches level_editor_scenery.cpp), phase 5 triggers / gaps / kill zones / teleports
  (after 3d - collision drawing / face flags), phase 6 full Inspector + apply_layout.ts bake. Agent A saw "Permission
  denied" creating thug2_check.exe in its worktree's out\build\verify (antivirus?) - mutation runs couldn't build.
- **Queued (user said yes), after A-C merge:** (D) view options - Show collision (invisible walls in a strong colour,
  surface types, Shift+click selects an invisible wall), editor-only hide / isolate; (E) uniform scaling of scenery
  (render vertices, collision, boxes, rails on it), objects if the model code allows; non-uniform later.

## Update 9 (2026-10-21, evening): account switch - level editor phases 0-2 done, phase 3 and the flaky tests stopped mid-task
- **On main and installed in C:\Games\THUG2** (DLL from dd6aa98): level editor phase 0 (Dear ImGui UI, Inter / JetBrains
  Mono, toolbar / Outliner / Inspector / status bar; `editor_ui=0` = old HUD), phase 1 (node browser, read-only
  Inspector, gizmos, picking, Online badge), phase 2 (move / turn point nodes, live where the game allows, undo, layout
  v2 `set` records, Online warning). Record + in-game checklists: docs/pending/editor-ui.md. Plan page (this account):
  claude.ai/artifact/B5EP6Qt7hqJ9RYvnZhkTHt. Not yet run in the game by the user beyond load-crash checks.
- **Stopped mid-task (WIP on their branches, each with a handoff note in docs/pending/):**
  - Level editor phase 3 (create / delete point nodes: overlay node array, Place palette, hide originals, limits -
    10 crowns / 256 restarts / 256+150 lights, crown reorder, `node` / `hide` records): branch
    worktree-agent-ac73f9426e90a064c, note `docs/pending/editor-phase3-handoff.md` (ed77d6e: research only, no code yet - an 8-step plan; key findings: the game won't parse an added node array, so the editor creates proxim / emitter / light objects itself; the overlay array must be skipped by GetLastNodeArray (park editor) and removed with its level; restarts / crowns are hidden by a marker class; hiding a crown renumbers the ones after it, and the last crown can't be hidden (SpawnCrown divides by zero); CTF / team nodes need their exact names).
  - Flaky early self-tests (screen element / text / anim controllers fail intermittently; fix root causes, then 50
    clean runs): branch worktree-agent-a024a86de15fee1b4 (7e4246f: three harness root causes fixed, 14+14 clean full runs; left: the 50/20 verification), note `docs/pending/flaky-selftests-handoff.md`.
  Resume each from its note, finish, check offline, merge (lead), build, install the DLL in C:\Games\THUG2.
- **Process check gotcha:** `Get-Process THUG2*` also matches `thug2_check` (self-test runs) - use
  `Get-Process -Name THUG2,THUG2PM` to see whether the user's game is running.
- **Promised to the user, when phase 3 + the flaky fix are in:** one complete in-game checklist page (tick boxes that
  remember, grouped by level) covering everything not yet play-tested: editor phases 0-3, the four games' THUGPRO
  compatibility / thugpro_new / port_changes, the translator fixes (gaps, glass, teleports, sounds, riding objects),
  Create 100% CAS (classic, gaps, unlockables), the editor quick start / autosave, and older unconfirmed items. The
  user is currently only checking for crashes on load.
- **Open question to the user:** should objects follow their scenery cluster when moved (today sectors + their rails
  follow the cluster; objects and point nodes don't)?
- Unified pages (this account): ported levels field guide claude.ai/artifact/TFm2dJdLUgbddNZS9ezYWj. The per-game
  reports it links were published on the user's other account - offer to republish.

## Update 8 (2026-10-21): the converters' faulty "THUG2 doesn't have X" assumptions fixed
Merged (e83b0bb THAW fixes, 0a68944 shared translator) and installed in C:\Games\THUG2; not run in the game. Record:
docs/pending/translator-fixes.md and docs/pending/thaw.md. Generated member table (tools/extract_members.ts ->
tools/level_port/thug2_members.ts), one skater context, thug2ScriptOk / scriptNeeds fixed, shared TriggerType for all
four games, THAW IsCreated -> IsAlive / InManual / THAW's own glass scripts. Left-out statements 10,136 -> 8,812.
Face flag 0x2000 kept for THAW only (1% of its faces; THPS4 / THUG1 set it on ~100%, a different meaning there).
Open: THPS4 still runs 4 THUG2 game-flow scripts (kill_start_key_binding, GoalManager_HideGoalPoints,
add_multiplayer_mode_goals, goal_wait_and_show_key_combo_text) - LEFT_OUT candidates; ns.exe can't decompile THUG1's
seven *_thugpro.qb files (worth a look); PC-THUG1-7 needs verification.

## Update 7 (2026-10-21): update 6's four branches finished and merged
All four (THPS4 5bb1d44, THAW 957059d, THPS3 d4692b2, THUG1 76cbbcf) are on main and installed in C:\Games\THUG2: every
game's `thugpro_new` (TPN-<game>-n, by reference to the THUGPRO install) and `port_changes` (PC-<game>-n, `op` /
`required`) read through one shared tools/compat/compat.ts API; The Mall's First_Rail_Blocker restored; THPS4 banners
opt-in (`--thugpro-banners`). Not run in the game.
- **Running:** an audit (read-only) of the converters' "THUG2 doesn't have X" assumptions - TriggerType (gaps never fire
  in THUG1 / THAW / likely THPS3 ports), THAW's glass (ShatterAndDie / BloodSplat exist in THUG2; MA_KillStickers
  dropped), suspects TeleportSkaterToNode, Obj_LockToObject, Obj_AdjustSound, DuplicateTrigger. Fixes follow its report
  (shared tools/level_port/script_port.ts).
- PC-THUG1-7 (goal nodes left out, marked required) needs verification.
- Proposed C++ (none done): THUGPRO's TriggerType / particle / televator / SafeKill / LoadZone equivalents, a protocol
  layer for host options (messages 155-159).

## Update 6 (2026-10-20, night): account switch - 4 agents stopped mid-task (thugpro_new + port_changes)
Main (pushed) has everything up to TP-39 + the editor quick start, all installed in C:\Games\THUG2. Four agents were
stopped part-way; each committed its work in progress (WIP) on its branch and wrote a handoff note
`docs/pending/<game>-thugpro-new-handoff.md` on that branch. **Not merged.** Resume each by reading its note, finishing,
checking, then merging (the lead merges; reconvert into C:\Games\THUG2 + make_menu_level.ts after).

| Game | Branch | Workspace |
|---|---|---|
| THAW | worktree-agent-a4244cc241e89291a (2bd6f2f) | C:\Games\THUG2-4 |
| THPS3 | worktree-agent-ab007c78a14463244 (d049871) | C:\Games\THUG2-1 |
| THPS4 | worktree-agent-a33cab85b7cc09963 (817f5db; port_changes done at 8efcfc2) | C:\Games\THUG2-2 |
| THUG1 | worktree-agent-ab52827a8c0463b2e (571bf2a) | C:\Games\THUG2-3 |

State when stopped (details in each branch's handoff note):
- THAW: thugpro_new done (TP-N1..N5, converts); **port_changes NOT started; Mall blocker NOT restored; thaw.md not
  corrected**; checks / docs pending.
- THPS3: thugpro_new + port_changes (PC-1..14) done; lint / test / reconvert / docs pending.
- THPS4: port_changes (PC-1..11) done and checked, `required` not added; thugpro_new module written but not wired in.
  Finding: THUGPRO itself hides the THPS4 banners (planned opt-in `--thugpro-banners`).
- THUG1: thugpro_new (TP-40..43) + port_changes (PC-1..20) done; comparisons / lint / docs pending.
- To reconcile when merging: the four used different register ids for thugpro_new (TP-N1..5, TP-N3, TP-N4-x,
  TP-40..43) - pick one scheme; shared files touched by several (compat.ts portChanges, ported_levels.ts models
  folder copy, make_menu_level.ts) - merge carefully.
- Cross-game finding: the shared script translator treats THUG2's own `TriggerType` as unknown, so gap conditions
  never fire in ported levels; THUGPRO's TriggerType / particle / televator etc. commands would need our own C++
  (proposals in the notes).

What those agents were doing (the user's decisions, 2026-10-20):
- **`thugpro_new`**: Morten confirmed THUGPRO's own content (meshes, models, scripts, nodes) may be included. It goes in
  ONE separate top-level section `thugpro_new` of each `tools/compat/thugpro/<game>_levels.json`, **by reference**
  (archive / file / node names + checksums), read from the user's THUGPRO install (%LOCALAPPDATA%\THUG Pro) at convert
  time - never commit THUGPRO's bytes (the repo goes to GitHub). Missing install / changed checksum: warn and skip.
  Switch `--no-thugpro-new` (hand edits without new content). Host level options are included in their default state;
  switching them needs a protocol layer for THUGPRO's messages 155-159 (later). THUGPRO C++ commands we lack (e.g.
  TriggerType): list + propose our own implementation, not done.
- **`port_changes`**: a second section for OUR OWN deviations from the source games (ids PC-n, README section "Our own
  changes"). All of the converters' deliberate deviations move there as data (crash guards, dropped / made nodes,
  script rewrites...). Every entry in `port_changes` and `thugpro_new` has `op`: `add` | `set` (merge; error if the
  target is missing) | `remove`, plus `id`, `target`, `why`; `required: true` for ones a level needs to load.
  `--compat none` keeps port_changes (not THUGPRO data); `--no-port-changes` = pure source output except required ones.
  Default output must stay byte-identical (except the Mall blocker below).
- **THPS4 skate shop door stays OPEN** (TP-29 off): the shop is our main menu backdrop - nothing may change the backdrop.
  The door becomes PC-1 (`remove` InvisColl01's 10 shop-side faces + Object10's 2 invisible faces).
- **THAW The Mall:** restore `Z_MA_Mall_First_Rail_Blocker` (user: removing it was a miscommunication). It is NOT across
  the entrance doors (thaw.md's note is wrong): a thin invisible wall (12 faces, 0x1000) alongside the start area,
  THUGPRO box x -15767..-14044, y 1367..2278, z -5641..-5600; THUGPRO keeps it. The "closed / cerrado" doors are the
  entrance glass Z_MA_Sk_JOW_Mall_Entrance02/03/04 (sticker ESign08) - keep them breakable.
- **Open finding, not yet given to any agent:** our THAW breakable conversion (`breakableScripts`, thaw_levels.ts)
  wrongly treats `ShatterAndDie` as a THAW-only command - THUG2 has it (and `BloodSplat`) as scripts in its own
  SK4_Scripts.qb / bails.qb. So THAW's `MA_BreakGlass` / `MA_KillStickers` could be kept as THAW has them (as THUGPRO
  does). Ours drops MA_KillStickers, so the Mall entrance's "closed / cerrado" sign stays floating after the glass
  breaks. Proposed: keep THAW's scripts (a port_changes choice), test whether Type=Crate still stops our skater.
- Other open user decisions: none pending except testing. Nothing of today's level work has been run in the game.

## Update 5 (2026-10-20, late): THUGPRO parity for all four ported games, editor quick start
- **All merged and installed in C:\Games\THUG2** (main at b0ebea2 or later; DLL, all four games' levels, overlay).
- **THUGPRO compatibility register** (tools/compat/thugpro, README lists every entry): TP-1..9 THAW, TP-10..19 THPS3,
  TP-20..29 THPS4 (TP-29 skate-shop door walls: off, conflicts with the user's open door), TP-30..39 THUG1. Names, spawns
  (place, types and ORDER - player n starts at the n-th Multiplayer restart, 0x00566100 / 0x0046B790 / 0x00580830),
  flags, moves, added rails, collision face values: applied by default; `--compat none` = original-game output.
  Data is always extracted (`thugpro_compare.ts --extract-thugpro` per game), never typed in.
- **Awaiting the user:** THUGPRO-authored content (meshes, models, scripts, host level options) - listed per game in
  each report and in the register README's awaiting lists. Reports: THAW JFncCnwYThiJz7rkD5weVP, THPS3
  VCdnFhwUc2iCP2QMg8yZPy, THPS4 XuTzB6Yf8mQTg8v8cebeQY, THUG1 GiiFRTv2XvBbNrLxwaReWw (claude.ai/artifact/...).
- **Editor quick start:** `-edit <level>` / `edit_level=`, layout autosave + crash recovery, `node tools/game/edit_level.ts`
  (docs/pending/editor-quickstart.md has the test checklist). Editor UI plan (Dear ImGui, Inter / JetBrains Mono,
  entity CRUD, phases 0-6): artifact AcJjzdPqNTvGKCdMzJkVLr.
- **Nothing of today's level work has been run in the game.** Watch: College / Kyoto load (more nodes), THPS4 pieces
  THUGPRO switched off at start, floating rails waiting on THUGPRO meshes (Foundry wires, Kyoto DDR, SM ferris).

## Update 4 (2026-10-20, evening): everything merged, no agents running
- **Installed in C:\Games\THUG2** (main at fe71f13; DLL and overlay current; THAW levels reconverted under THUGPRO's
  names and frames):
  - the animation / collision range (0x406460-0x41A6D0);
  - the automation channel (workspaces only);
  - CREATE 100% CAS completing story, classic (140 goals), all 603 gaps and every reachable unlockable
    (docs/pending/cas100.md);
  - THAW ports compatible with THUGPRO's (docs/pending/thaw.md; report https://claude.ai/artifact/CT1Y75gqcGt19RbDmZJeno).
- **THUGPRO-only choices** live in tools/compat/thugpro (register TP-n, README there); never as converter constants.
- **Tools are TypeScript now** (docs/typescript.md): every Python tool ported with byte-for-byte parity and deleted;
  Python remains only for tools/ghidra (pyghidra) and tools/capstone_disasm.py. `npm install` once; `node tools/...ts`.
- **Waiting on the user's play-tests:** CREATE 100% CAS (classic, gaps, cheats, save/load size), the THAW levels
  (The Mall teleports, CTF, the renamed levels load), and the earlier ones (cutscene, collision, park-editor
  multiplayer, Caveman, UsePS2Controls=0).
- **Small leftovers:** automation_tables.inc's next regeneration reorders two case-only ties (expected); converting a
  subset of THAW levels wipes the others from the output folder (documented; easy fix); the flaky screen-element /
  text early self-tests; the empty dashboard artifact (BkBifeKWJYfjVcCUscf7q2) awaiting the user's delete-or-keep;
  ~37 finished agent worktrees under .claude/worktrees (their branches are merged and pushed).
- **Later candidates:** THAW TriggerType command (~1 day), THAW time of day / ambient sounds / props; the AddVector
  0x00477F70 register RISK; Line::DistanceSqrTo* (float vs double); the next decompilation range (batch number 193).

## Update 3 (2026-10-20): the main install changed
- **The main install changed.** C:\Games\THUG2 is now a stock install (the user copied it from
  C:\Games\THUG2-THPSX-ORIGINAL).
  - The game is in **C:\Games\THUG2\Game** (THUG2.exe: an unwrapped no-CD exe, never modified).
  - Our files went in on 2026-10-20: the DLL, the ini from the old install (self-tests off, anteara_mod=0), the
    converted levels, and the overlay rebuilt with make_menu_level.py (now make_menu_level.ts).
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
  - tools/anteara (README) holds only the mod's own changes. make_menu_level.ts builds the mod's script set into
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
  then `node tools/menu_level/make_menu_level.ts --game C:\Games\THUG2`. The user's own manual-test folder: no
  test runs there, and its self-tests stay off.
- Game test scripts: tools/game (README there). Launch only when allowed (workspaces under the lock are allowed),
  never capture the whole screen, never close a game you didn't start, and hand interactive tests to the user.
- `.claude/worktrees/` holds finished agents' worktrees (defaultlevel a99dc09a…, cas100 a778032d…, merged). They can
  be removed with `git worktree remove`.
