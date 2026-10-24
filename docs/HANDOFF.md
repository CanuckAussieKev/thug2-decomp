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
- **2026-10-21, early:** C (picking, c4fd5af), play fixes (help table, group vs floor, low flat pieces = structure,
  screenshot mouse cross), 3d view options (b1c7534) and 3e scaling (4100b97) merged, installed, pushed. Running: phase 4
  chains, phase 5 triggers, and a check of face flag 0x80 (the view agent found the camera's feeler IGNORES 0x80 - our
  converters say 0x80 = camera collides and the THPS4 converter inverts it: possibly wrong; docs/pending/face-flag-0x80.md).
  Phase 6 (full Inspector + apply_layout.ts) starts after 4 and 5 merge.
- **Running (2026-10-21, evening):** ported non-car Vehicles rebuilt as moving GameObjects in all 4 games (ported-movers.md,
  workspace THUG2-2); THUGPRO hacks review -> scratchpad	hugpro-hacks.json -> a decisions page; decompiling "Engine script
  commands" 0x445b50-0x44e6b0 (246 fns) in thirds: EC1 0x445b50-0x4489b0 (THUG2-1), EC2 -0x44b7e0 (THUG2-3), EC3 -0x44e6b0
  (THUG2-4); records docs/pending/engine-cmds-1..3.md; brief crash-check runs allowed in their workspaces.
- **UX round DONE (2026-10-21):** UX1-UX9 + goals + occlusion picking fix + THAW rail speed-up fixes all merged, installed,
  pushed (main 20ebef7; 503 self-tests, 149 TS tests). Nothing play-tested. Pages: entity map KzL5HQAt54n5g7JtfmEbtv,
  checklist DUcbR4CuAAULq98JWcxX46. No agents running.
- **UX round (2026-10-21, later; user: "do it all" incl. research ideas):** running UX1 play/camera/prefs, UX2 transforms/
  snapping/repeat (RMB-held flying default, Space cycles modes), UX3 selection/panels/command search+keymap, UX4 view
  modes/assets/previews, UX5 problem checker/budget meter/notes, goals on Create-a-Goal (editor-goals.md). QUEUED after
  merges: UX6 (after UX1) skater trail, validate gaps by skating, test from restart, simulate without skater; UX7 (after
  UX2) adjust-last-action, placement ghost, live measurements, align/distribute, linked copies, replace selection,
  edge-to-edge snapping, auto-rail along edge; UX8 (after UX1+UX3) context hints, named selection sets, radial menus +
  controller mode, wheel drill-through, follow-selection / teleport / walk; UX9 (after UX4) gap wizard, skateability
  overlay, raise/lower ground with auto-ramps. Research: docs/editor-ux-research.md.
- **2026-10-21, end: every editor feature merged, installed, pushed** (main d062c85; 420 self-tests, 127 TS tests): career-mode
  guard (free skate / single session only), bake of triggers / level props / per-axis + object scale / exit triggers,
  bake backups + --uninstall, ladders, trick-object rails, grindable-on-flat-ground, carrying, Outliner markers, trigger
  handles, stretch + object scaling (collision + matrix components decompiled: batch 193, 35.97%). No agents running.
  Not done: walls can't be scaled / copied; object collision scale and start restart not bakeable. Everything awaits
  the user's play test (checklist page DUcbR4CuAAULq98JWcxX46).
- **Account switch (2026-10-21): the main pages are now on this account** - editor plan / entity map
  claude.ai/artifact/KzL5HQAt54n5g7JtfmEbtv (source scratchpad\main\editor-plan.html), play-test checklist
  claude.ai/artifact/DUcbR4CuAAULq98JWcxX46 (source scratchpad\main\playtest-checklist.html; its db `results` starts empty -
  the old account's ticks couldn't be read). Running: career-mode guard (resumed), apply_layout.ts triggers + level props,
  ladders / trick-object rails / flat-ground rails, stretch + object scaling (object collision decomp), editor polish
  (objects follow scenery, Outliner markers, walls hover / scale / copy, trigger handles + exit firing, undoable locks).
- **2026-10-21, later: the whole editor build plan is merged and installed** (main 3094544): phase 4 chains, phase 5
  triggers, phase 6 Inspector / level props / apply_layout.ts, invisible walls, shadows + Parts, shared collision (moved
  pieces take their faces from collision-only sectors), editor gaps kept out of career saves, face flag 0x80 fix (THPS3 +
  THUG1 dj/ph/sc/skateshop reconverted), [Graphics] OcclusionCulling (default on; the user's main ini has 0). No agents
  running. Open: goals / cash / stat points earned in an edited copy still reach the career (proposal: editor and edited
  copies only outside career mode); apply_layout.ts doesn't bake triggers or level props; all of it awaits the user's play
  test (checklist page claude.ai/artifact/SQqRbUco71P4fa9AG4eHuk).
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

## 2026-10-22 (usage limit)
Stopped mid-work (resume from their worktree branches): shared node fields (worktree-agent-a8c820ba40a96d402, load checks), skater SK1 (a86a1577a57f1e90c, 0x500020-0x508773), SK3 (aa16173e077dcc91b, 0x510823-0x5197F2), SK5 (ad7146704ab7493b5, 0x521B90-0x529F62), SK6 (a79a8b861713be8f4, 0x529F62-0x533F20); prompt template out/scripts/skater_prompt.txt. Merged + installed today: SK2, SK4, THP8 phase 1, THPS2 phase 3, crash handling, THUGPRO join path, HD textures (THPS3 at 2x installed). Open: user to play-test skater physics / tricks; East LA register wording ('LA crash fix' is only the latest commit title); THP8 phase 2 order.

## 2026-10-22 (second usage limit)
Merged + installed: skater SK1-SK6 (the whole skater range), OC5, THP8 phase 2, shared node fields (all four games reconverted), net self-test fix. Progress ~50%.
Stopped mid-work (resume from their branches; prompt template out/scripts/objects_prompt.txt): OC1 worktree-agent-ac9022c3107f3faca (0x41A6D0-0x423643), OC2 a6dab05a26426d059 (0x423643-0x42CECD), OC3 af955367eb79d2242 (0x42CECD-0x4351B2), OC4 af2babd08ddaf146e (0x4351B2-0x43D929); level crash fixes afa88fcdadc979145 (tok, z_ho, shp, vn, hh crash / hang with main's conversion - docs/pending/level-crashes-2.md if present). Leftover: Obj::CTrickComponent declared in both trick_component.h (SK4) and trick_queue.h (SK5) - merge into one.

## 2026-10-22 (resumed)
OC1-OC4 resumed in their own worktrees (same branches as above; prompts out/scripts/OC1..4.txt): each finishes its uncommitted batch, merges main, completes its range (find_gaps 0); OC4 checks whether its range is already done. Workspaces THUG2-1..4. Then merge one at a time (build + checks after each), fold records, install. Level crash fixes branch (afa88fcdadc979145) resumed too, testing in the main install C:/Games/THUG2 (user allowed, under the lock) - reinstall main there after merging.
- Done (2026-10-22): OC1-OC4 and level crashes 2 merged into main (each built + thug2_check on both exes: 0 MISMATCHED, 698 early self-tests; check_code_writes clean; 318 TS tests), OC1-4 records folded as batches 263-293, progress 58.86%. The whole object components range 0x41A6D0-0x445B50 is done. Installed in C:\Games\THUG2 (DLL + pdb from 8fd4a7a, overlay regenerated; self-tests still off); not pushed.
- Open from the agents: two Mth::Quat classes (quaternion.h from OC1, quat.h from OC3) - merge into one; OC3's AddRigidBodyPusher clears the pushers' reach (0x006939B0, floats) as ints (same bits, harmless); mth/matrix.cpp's axis rotations differ from the original in the last bits (all of them for very large angles).
- Cleanup (2026-10-22, the user's go-ahead): the old lead session (c861af32, pid 12600) closed; every worktree under .claude/worktrees removed (branches kept). Stopped agents' uncommitted work was first committed on their own branches as "WIP: a stopped agent's uncommitted work ..." (not built or checked): worktree-agent- a0d3be3e (shared node fields), a1d14a60, a1e8853a (THPS2 phase 2 / test view), a5a6e4b7 (level select), a5cdedef + af572f68 (THUGPRO join), a6516fa1 (HD textures compare_shots.ts), a674e0bd + af544254 (THUGPRO chat / trick objects / guards), a6f8f51b (ped models), a79a8b86 (SK6 walk), a7aa31c4 + aeaab07a (THPS3 doors), a86a1577 (SK1 acid drop), aa16173e (SK3 - a leftover mutation only: discard), aadfca87 (thp8.md), ab18fdb4 (level_port components), aba4779e (read_dump.ts), ac53978e (THPS2 phase 3), ad714670 (SK5 walk). Not yet checked against main: some may be lost finished work. Also unmerged commits on a3023e3b, a52c54cc, ad8f7af2 (ped models).
- Pushed (2026-10-22): main at 9c1eccf on GitHub (origin/main up to date).
- Branch cleanup (2026-10-22, the user's go-ahead): every unmerged agent branch checked against main - all finished work was already merged (often reworked) except nodes.nameValue's #checksum byte order (a real bug: unnamed Name= / TriggerScript= never matched) and components.ts's unshared count, salvaged in fd32847; SK3's leftover mutation and a TEMP debug log discarded. All worktree-agent-* branches deleted, locally (182) and on GitHub (36, all merged). Discarded unmerged tips, for recovery while git keeps them: a0d3be3efba6b8b9b a47b283; a1d14a608e92edab3 a2dc014; a1e8853a230496423 d2fe10c; a3023e3ba138d15ca bbebfde; a52c54ccda9adf924 30766dc; a5a6e4b7a58f09823 8238fd6; a5cdedeffe9b6f264 259cee3; a6516fa1a9a16ac7c 8391d5e; a674e0bded23672e7 4fa71b9; a6dab05a26426d059 036173f; a6f8f51b1b5f6e04b 7c0b5ec; a79a8b861713be8f4 c3f6a95; a7aa31c4fba599da5 a6fe5c0; a86a1577a57f1e90c e267b34; aa16173e077dcc91b 47569a7; aadfca87c2041185c 0744cc9; ab18fdb446c09a21c 9a30ae4; aba4779ec4022d236 44a2f7a; ac53978ebb791f756 9ad6649; ad7146704ab7493b5 8423da7; ad8f7af2ef7728992 7301f4f; aeaab07a6f041156e 18869b4; af544254c6bfbc70e 86435b1; af572f68bbe26ff25 55c77c3; 
- Installed (2026-10-22): main dd78262 in C:\Games\THUG2 - DLL + pdb, all six games' levels reconverted (THPS2 10, THPS3 14 with 2x HD textures, THPS4 11, THUG1 16, THAW 15, THP8 15; first install with the nameValue fix), overlay regenerated. Only main is left as a branch, locally and on GitHub (merged anteara-mod / thaw-inventory / thaw-tp4 deleted).
- Cleanup (2026-10-22): one Mth::Quat (quat.h / quat.cpp; quaternion.h / .cpp folded in) and one Obj::CTrickComponent (trick_component.h; trick_queue.h folded in - its file constants kButtons / kEvents renamed kButtonsName / kEventRing: the class's own kButtons = 22 had taken over the `buttons` name in the member functions, caught by the trick CallMemberFunction self-test); AddRigidBodyPusher clears the reach as a float. Built, thug2_check 0 MISMATCHED, 698 early self-tests passed. Still open: mth/matrix.cpp's axis rotations differ from the original in the last bits. NOT run in the game yet.
- Batch 294 (2026-10-22): the maths library's x87 rounding fixed (rotations, Euler angles, perspective, sine table, FastAtan2, determinant / inverse, axis-angle) - bit for bit with the originals; 704 self-tests pass. NOT run in the game yet; not installed.
- Batches 295-298 (2026-10-22): the Win32 platform layer 0x5F2E00-0x5F6640 done (100%; D3DX9 libjpeg and a data table split off the old range; 31 C++ library template functions in data/library_functions.csv). NOT run in the game yet; not installed.
- Installed (2026-10-22): main 5cb798e in C:\Games\THUG2 (DLL + pdb, overlay regenerated) - batches 294-298 included.

## 2026-10-22: online & network game modes (0x533F20-0x54EF60)
The user asked for the whole "Online & network game modes" range (457 functions, 20 done before) end to end. Four agents
in worktrees, split by size (prompts out/scripts/NET1..4.txt, template net_prompt.txt): NET1 0x533F20-0x53A910
(THUG2-1, docs/pending/net-1.md), NET2 0x53A910-0x541870 (THUG2-2, net-2.md), NET3 0x541870-0x548560 (THUG2-3, net-3.md),
NET4 0x548560-0x54EF60 (THUG2-4, net-4.md). Offline self-tests only; no online or LAN runs. Then merge one at a time,
fold records, install.
- Done (2026-10-22): NET1-NET4 merged into main (one at a time; each built + thug2_check on both exes, 0 MISMATCHED; final
  735 early self-tests, check_code_writes 20 writes / no overlaps, 318 TS tests), folded as batches 299-319. The range is
  455/457: the two left (0x005480A1, 0x005480B4) are PartyMod's LoadLibrary cave in THUG2PM.exe's padding, not game code.
  Merge fixes: NET1's gamenet_players.cpp / selftests_net_players.cpp -> gamenet_player_lists.cpp /
  selftests_net_player_lists.cpp (NET2 had the names); NET1's AddNewPlayer (0x0053A310) -> AddJoiningPlayer;
  server_list.cpp uses Manager::InLanMode; TestNetLists -> TestNetManagerLists. Every game-name use is source now:
  online_identity.cpp writes only the key; the net tests use hook/online_identity_testkit.h so they pass with
  OnlineGameName set (NET1's in-game failure in THUG2-1). Progress 65.02%. NOT installed (the install copy was refused
  by the permission check - the user installs: copy out\build\lead\dinput8.dll + .pdb, make_menu_level.ts); not pushed.
  Nothing tested online: what to try by hand is in docs/pending/net-1..4.md.
- Installed (2026-10-22): main 052848a in C:\Games\THUG2 (DLL + pdb, overlay regenerated; self-tests still off). Not pushed.

## 2026-10-22 (account change): park editor & graffiti in progress
- **main 4410186, pushed after this note.** Installed in C:\Games\THUG2 at 4410186 (DLL + pdb, overlay regenerated;
  self-tests off) - batches 320-328 included; NOT run in the game yet.
- **Online & network game modes: done** (NET1-NET4 merged, batches 299-319; 455/457 - the two left are PartyMod's
  loader cave). See the "2026-10-22: online & network game modes" note above.
- **Park editor & graffiti (0x588180-0x595030): 11% -> batches 320-328 done** by the lead, small batches, each built,
  thug2_check on both exes, check_code_writes, register scan, call audit, mutations (records in decomp-progress.md):
  320-326 the trick objects / graffiti (src/game/obj/trick_objects.cpp, tests src/hook/selftests_park_graffiti.cpp),
  327-328 the model viewer's object (new src/game/sys/viewer_object.cpp / .h, Obj::CViewerObject; tests
  src/hook/selftests_viewer_object.cpp). Batches 323+324 share one commit.
- **Next (batch 329, already read, not written):** 0x00589FD0 CViewerObject::ReloadAnimation(file, anim) - with an
  animation component: key = anim ? GetAnimSet() + anim : Crc(file); the asset unloaded if there; CAssMan::LoadAnim(file,
  anim, GetAnimSet(), 0, 0); PlayAnimation(anim); RET 8. 0x0058A2E0 SetRotation(z, x): m_matrix = identity, then
  D3DXMatrixMultiply(m, m, RotZ(z)) and (m, m, RotX(x)) - Mth::CreateRotateZMatrix / X, D3DX 0x005BFA8C stays a call.
  0x0058A360 SetAnimSpeed(speed): +0x118 = speed, > 1 -> 1, then < 0 -> 0 (NaN kept); with an animation component:
  FirstChannel47E850(speed, 0, 0, 0), a pooled CStruct with AddFloat(0xF0D90109, speed) passed to the component's
  vtable +0x30 with 0xBD4EDD44 and 0, then the struct cleared and deleted; byte +0x124 = 0. Then 0x0058A4A0, 0x0058A720,
  0x0058A8B0, 0x0058AB30, the constructor 0x0058ADB0 (all viewer object), then the park editor proper.
- Lead's per-batch check script: `sh out/scripts/batch_checks.sh <tag> <addr>...` (lead build + both exes + code
  writes + register scan + audit; out/ isn't committed - recreate from this description if missing). Mutation specs in
  out/mut/*.ts use BUILD out\scripts\build_check.cmd (out/build/verify).

## 2026-10-23 (new account): park editor & graffiti - four agents
The user asked for "Park editor & graffiti" (0x588180-0x595030) end to end. 243 functions left, split by size between
four agents in worktrees (prompts out/scripts/PE1..4.txt, template park_prompt.txt; numbered tags PE<n>-<k>):
PE1 0x588180-0x58CD40 (THUG2-1, docs/pending/park-1.md; starts with batch 329's viewer object notes above), PE2
0x58CD40-0x58FBD0 (THUG2-2, park-2.md; the park editor CFunctions), PE3 0x58FBD0-0x592740 (THUG2-3, park-3.md), PE4
0x592740-0x595030 (THUG2-4, park-4.md). Then merge one at a time (build + checks after each), fold, install, push.
- **Done (2026-10-23): Park editor & graffiti 100% (289/289).** PE4, PE2, PE1, PE3 merged one at a time (each: lead
  build, thug2_check on both exes 0 MISMATCHED - 771 early self-tests at the end -, check_code_writes no overlaps,
  register scan 5 known RISK lines, call audit clean), folded as batches 329-341; find_gaps empty over the range.
  PE1's rail mutations re-run on main's renamed code: 40/42, the 2 equivalents PE1 recorded. Progress 67.85%.
  **Installed** in C:\Games\THUG2 at a11ce7a (DLL + pdb, overlay regenerated; self-tests off); pushed. NOT played:
  the per-agent "try in CREATE-A-PARK" lists are in docs/pending/park-1..4.md. PE2 fixed a real crash already on main
  (deleting a piece: DestroyMetasInAreaAndRebuild's corner buffer). Disable lines: park-1..4.md.
- Optional cleanups left by the agents: other code still calls the new park functions through GameFunction addresses
  (park_io.cpp, park_scene.cpp helpers, core/singleton.cpp) - hooked in the game, direct calls would be tidier;
  ParkGeneratorData (park_scene.h) is the generator's layout, separate from CParkGenerator; SMetaDescriptor::isReal is
  misnamed (the precise intersect test skips those pieces). The lead's mutation re-run converted the file to LF first
  (the checkout is CRLF and the patterns use \n; not confirmed that mutate.ts needs it) - out/scripts/filter_spec.mjs
  drops patterns that no longer match the code.

## 2026-10-23: the park cleanups from the note above - done (batch 342)
- Every call from src/game into the rewritten park editor / graffiti / viewer object code (0x00588180-0x0059E100)
  is a direct C++ call now (205 GameFunction calls; core/singleton.cpp's and sys/main.cpp's trick object manager and
  park editor singletons included). CParkGenerator holds its own fields (ParkGeneratorData / Data() gone);
  SMetaDescriptor::isReal -> setsFloor. Tests that stood in for a game address now stand in for ours too.
  Built, thug2_check on both exes 0 MISMATCHED / 771 early self-tests, code writes, register scan (5 known), audit
  clean. NOT run in the game yet; not installed or pushed.
- tools/mutate.ts needs no LF conversion (it reads with universal newlines and restores the bytes).
- Still open (wider): 942 GameFunction calls elsewhere in src/game reach rewritten functions at their game addresses.

## 2026-10-23: GameFunction calls into rewritten code -> direct calls (four agents)
The user asked for the "942 GameFunction calls" cleanup (953 counted by the new `node tools/find_game_calls.ts`, which
lists calls under src/game whose address has a REPLACE line; `--list [filter]` per call). Four agents in worktrees,
split by calling directory (prompts out/scripts/GC1..4.txt, template gfc_prompt.txt; no game runs):
GC1 skater/core_* + skate + ed + park + script (~231, docs/pending/gamecalls-1.md), GC2 the other skater files + sfx +
file + sio (~247, gamecalls-2.md), GC3 obj + net + sys + core + nx + mc (~265, gamecalls-3.md), GC4 front + gfx + pcm
(~210, gamecalls-4.md). Then merge one at a time (build + thug2_check both exes + code writes after each), fold, install.
Found while sampling: folded identical functions mean a REPLACE name can be wrong for some callers (skater.cpp's light
lookup calls 0x00532EB0 "Manager::GetMatchClient") - the agents write the equivalent instead of calling a misnamed one.
- Stopped once by the weekly limit and resumed. Branches (worktrees under .claude/worktrees): GC1
  worktree-agent-acc27e85c77dda5ca, GC2 worktree-agent-a9f64278608fad003, GC3 worktree-agent-aeaa04eadf21befa3, GC4
  worktree-agent-ad5c087c3daa7a0b6.
- GC4, GC2, GC1 merged (each built + checked on main). GC1 found GetContactPoint (0x00427A00) is void but the
  original returns `out` in EAX - GC3 asked to fix it. Follow-up batch: calls through address-taking helpers
  (walk_moves.cpp Call(0x...), trick_member_functions Call / CallOn ...) that find_game_calls.ts doesn't count.
- Queued (user said yes): name the script checksums used as bare hex in src/game (decompile every .qb with
  NeverScript, checksum every symbol, match literals used as checksums - e.g. core_physics.cpp's 0xEF161C2A =
  Ground_stick_angle). After GC3 merges.
- THPS2 quarter pipes (user report + screenshots 2026-10-23 23:14): steep non-vert ramp faces are walls in THUG2
  (Wall_Non_Skatable_Angle 25 / the converter's WALL_RIDABLE), coarse vert steps > Ground_stick_angle 30, coplanar
  duplicate faces without VERT. Agent fixing the converter in a worktree (record docs/pending/thps2-quarterpipes.md,
  workspace THUG2-1).
- Done (2026-10-24): GC1-GC4 merged (GC4, GC2, GC1, GC3; each built + thug2_check both exes 0 MISMATCHED / 771 early
  self-tests, code writes, register scan 5 known, audit 0), folded as batches 343-357 (GC3-4 = 357: GetContactPoint
  returns `out`). find_game_calls.ts: 0 calls into rewritten code left. Still open: calls through address-taking
  helpers in some files (GC2 listed ~95 in gamecalls-2.md: walk_moves.cpp Call(0x...), trick_member_functions,
  physics_control CameraCall, skater.cpp's reset loops) and the 35 named-constant GameFunction calls. NOT run in the game.
- THPS2 quarter pipes merged (tools/level_port/ramp_collision.ts; docs/pending/thps2-quarterpipes.md; automation `trace`).
- THPS2 quarter pipes 2 merged + installed (2026-10-24 01:43): no vert under 40 deg, steep ramp faces vert, one vert band
  per ramp (native THUG2's pattern); seam theory ruled out; the user's ~50% manual breaks not reproduced (likely the
  low-vert cause) - waiting on the user's play test (docs/pending/thps2-quarterpipes.md). Main not pushed.
- THPS2 quarter pipes 1+2: tested in the game by the user (2026-10-24) - all three areas feel good.
- THP8 network levels + the whole town merged (2026-10-24; docs/pending/thp8-network-unified.md): the 7 multi-zone
  network areas as levels thp8_net_* (level select 125-131), BEAVER VALLEY = all 12 zones (IsBigLevel); limits raised in
  C++ (node name hash 65,536 slots / 32-bit indices, rail name table, script heap +16 MB, CComponent/CStruct/CVector/
  CArray/HashItem pools; `[memory] original_sizes=1` undoes) + a `memory in <level>` log line. Load-checked in THUG2-2
  only; LAN hosting and play NOT tested. Not done: CTF flag names, THP8's two-player levels.
- Installed (2026-10-24 04:34): main c209dea in C:\Games\THUG2 - DLL + pdb, THP8 reconverted (22 levels), overlay regenerated. Main not pushed.
- THP8 levels 2 merged + installed (2026-10-24 09:03; docs/pending/thp8-network-unified.md): per-zone levels removed
  (user: our own split), 13 network levels thp8_net_* (BEAVER VALLEY + 12) in free skate and the host list, TRAINING /
  SKATE SHOP / BEDROOM kept; other zones' low-detail stand-ins drawn instead of void; zone seam walls (invisible
  edge walls THP8 switches off at run time) left out in multi-zone levels. Not confirmed in the game: whether the
  walls the user hit in BEAVER VALLEY are gone (no spot given). Main not pushed.
- Running (2026-10-24): "Game script commands" 0x59F610-0x5B6560 (574 fns, ~21 done) end to end, four agents in
  worktrees (prompts out/scripts/GS1..4.txt, template gs_prompt.txt): GS1 0x59F610-0x5A5360 (THUG2-1), GS2
  -0x5AAEF0 weather / memcard / textures / profiles (THUG2-3), GS3 -0x5B0A40 appearance / skater queries / cutscenes
  (THUG2-4), GS4 -0x5B6560 tricks / input / theme (THUG2-1, shared). Records docs/pending/gamescript-1..4.md. One crash
  check each at the end. Also running: THP8 round 3 (the user's 11 screenshot spots) on THUG2-2.
- THP8 levels 3 merged + installed (2026-10-24 12:36): narrow holes under kerbs floored (spots 1-2, confirmed in a
  workspace run), network barriers drawn (faint blue stand-in for THP8's shimmer), low-detail stand-ins kept off the
  level's own ground (factory floor overhead). Round 4 running: house roofs catching on the invisible shell (spots 3-4).
- THP8 levels 4 merged + installed (2026-10-24): invisible building shells trimmed 6 in under roofs (spots 3-4), eave
  gaps floored, invisible trigger faces non-collidable (road wedges, the 12:42 spot), fill collision split under the
  2,048-face line-test buffer. Confirmed by one workspace trace run (roof + road); the other 220 shells offline only.
- Running: loading console ([features] loading_console, docs/pending/loading-console.md, workspace THUG2-3 shared).
- Loading console merged + installed (2026-10-24; DLL only): [features] loading_console=1 (default), bottom-left panel
  on the loading screen (files, load scripts / commands, log lines; "last activity", orange after 10 s idle); the
  loading screen's 29 ms timer callback 0x004B6FF0 rewritten (src/game/gfx/loading_bar.cpp). Checks: 773 early
  self-tests, 0 MISMATCHED. Orange idle state and BEAVER VALLEY not seen in the game yet.
- GS1-GS3 merged (each built + checked: 0 MISMATCHED, 801 early self-tests, scans clean), folded as 358-383 (GS3
  358-365, GS2 366-372, GS1 373-383); progress 71.83%. Installed (DLL + overlay with the Stone Arch Bridge sign for
  Minneapolis). NOT run in the game beyond the agents' crash checks. GS4 still running; collision-event logger running
  (docs/pending/collision-events.md, workspace THUG2-2). automation.cpp's fopen C4996 fixed (_fsopen).
- Collision events merged + installed (2026-10-24; docs/pending/collision-events.md): [debug] collision_events=1 /
  collision_event_screenshots=1 log `collision-event:` lines (reason, face, probes); tools/game/collision_events.ts
  groups them (--col names the converter object). User's 14:50 road spot: a 40 m x 4 m hole in BEAVER VALLEY's road
  collision (uber frig 0x004F8420 flips the velocity) - THP8 round 5 fixing it. Asked the user about an on-screen notice.
- GS4 merged (813 early self-tests, 0 MISMATCHED), folded as 384-390: the whole "Game script commands" range done;
  progress 73.16%. THP8 levels 5 merged: the suburb road's collision piece restored (named after a story-only trigger
  node and dropped with it), narrow slits floored, a drawn-ground-without-collision audit per level. Installed
  (DLL + THP8 + overlay). Follow-up: GS2's WrapAngle returns float where the original kept x87 precision.
- THP8 levels 6 merged + installed (2026-10-24): round 4's "invisible 0x40 = pass-through" reverted (51 bodies made
  solid again, incl. the roundabout's skatable skirt; the user's tree clip was reached through the open skirt). Not
  confirmed in the game. The THP8 agent wrote into THUG2-2 while the collision-events agent held its lock (overlay only).
- CLAUDE.md "Agent cost": decomp keeps the strongest model; sonnet for mechanical work; fresh agent per round; effort levels.
- Collision events follow-up merged + installed (2026-10-24): on-screen notice, screenshots on by default with the
  switch, `logging on` line per level, collision-event-detail lines (state, input, deciding ray, face corners, open
  edge, file identity, approach, camera), slope only on real slopes, 1 s repeat window with repeats=. 815 early
  self-tests. Offered the user an offline z-fighting scanner (not started).
- THP8 render audit merged (2026-10-24, 91df04a; tools only, nothing installed): tools/level_port/zfight.ts,
  texture_check.ts, tools/thp8_levels/render_audit.ts (textures / zfight / spot). Findings + 10 proposed converter fixes
  in docs/pending/thp8-render-audit.md (grey lawns = NormalMapBlended falls back to a flat grey 32x32; stand-in instance
  masters stacked at the origin; no overlay_bias in THP8; doubled fun-park periphery). Fix round not started.
  Running: renderer decomp 0x4B2280-0x4D6D40 (docs/pending/renderer-fx.md, RFX-n, workspace THUG2-1).
- THP8 fix round 1 merged + installed (2026-10-24; levels + overlay, DLL unchanged): stand-in instance placements, z bias
  (biasOverlays), flat/dither base textures replaced by the real one (grey lawns -> grass), dither layers dropped,
  dither-only materials untextured, cross-zone duplicate sectors dropped (doubled fun-park periphery). Before/after in
  docs/pending/thp8-render-audit.md "Fix round 1". Lead load check in THUG2-2 with the current DLL: BEAVER VALLEY loads,
  log clean (the agent's crash was THUG2-2's stale 10-19 DLL). Odd: THUG2-2 with self-tests ON stopped logging after
  "early self-tests: running 815" (level not loaded in 86 s) - not diagnosed. Not run in the main install.
  Running: THP8 vert ramp + tree wedge (docs/pending/thp8-vert.md, THUG2-4); HD menu UI 4x + THPS3 x4 (docs/pending/hd-ui.md, THUG2-3).
- THP8 vert fix merged + installed (2026-10-24, docs/pending/thp8-vert.md): gap fill no longer floors cells under steep
  up-facing ramp faces (flat shelves in BEAVER VALLEY's memorial vert wall base; shelves 11,370 -> 546). Vert flag 0x8
  was fine. Tree wedge (super_small_tree_30): collision is THP8's own; suspected engine path CheckAirForwardCollision
  0x00508780 -> 0x004FFDB0 leaving the skater 10 in past the move inside a thin pole - unconfirmed, no change.
- THP8 fix round 2 merged + installed (2026-10-24): repeated coplanar same-material twins (F_ = floater copies, both
  drawn by THP8) left out of the scene (BEAVER VALLEY alley sheet / sign fixed), stand-ins lying on drawn level triangles
  dropped (Capitol / Skate Central backdrop), audit counts zero-gap copies as flicker. Fix 8 (two-sided flag) not found.
  Left in BEAVER VALLEY: 95 coplanar pairs / 480 m2 (pirate shop wall, citypark outside wall, factory elevator panels).
  Running: pole tunnel diagnosis (docs/pending/pole-tunnel.md, THUG2-4); HD UI (THUG2-3); renderer decomp (THUG2-1).
- Pole tunnel merged + installed (2026-10-24, docs/pending/pole-tunnel.md): THP8 collision primitives' sides were wound
  inwards (cylinderTriangles; 6,256 of 54,040 primitive triangles, 391 cylinders/capsules/spheres) - THUG2's line test
  0x0040B420 is one-sided, so skaters passed into trunks and were held inside. Engine original == ours (trace A vs C
  identical); new early self-test "pole tunnel". Unlooked: uber-frig at (-2393.2,196.5,12222.4) BEAVER VALLEY plaza.
- HD menu UI merged + installed (2026-10-24, docs/pending/hd-ui.md, main f8964ef): run-time switch [features] hd_ui=
  (default 0; set to 1 in the main install's ini at the user's request); make_menu_level.ts --hd-ui 4 puts 517 images +
  8 fonts (Real-ESRGAN anime x4) in thug2-decomp-data\hd_ui, read only with hd_ui=1. DLL installed (817 early self-tests,
  0 MISMATCHED). NOTE: later make_menu_level runs into the main install need --hd-ui 4 or the HD files are dropped.
  THPS3 at 4x only in THUG2-3. Renderer decomp ~41% (RFX-1..9 committed).
- THP8 fix round 3 merged + installed (2026-10-24, overlay made with --hd-ui 4): one-sided material copies per mesh for
  back-to-back blended meshes (cut-outs / foliage names never; 13 in BEAVER VALLEY: factory paint shells, capitol roof
  pools, glass, turbine), repeatedSurfaces keyed on pass-0 texture + partial triangles, biasOverlays(lookAlikes) on
  (595 materials biased in BEAVER VALLEY - if poke-through appears, lookAlikes is one flag in thp8_levels.ts).
  BEAVER VALLEY flicker 1,667 -> 496 m2. Left: Z_Inschool_g_back_periph05's partner (284 m2), instance-only pairs.
- Main install (2026-10-24 21:20): THPS3 reconverted at --hd-textures 4 (1.6 GB) + overlay --hd-ui 4. Future installs into
  the main install: THPS3 with --hd-textures 4, make_menu_level with --hd-ui 4. Running: MISCELLANEOUS level-select
  section + THP8 after THAW (docs/pending/levelselect-misc.md, THUG2-2); HD UI part 2 - globe, arrows, skipped UI textures
  (docs/pending/hd-ui.md, THUG2-3); renderer decomp ~41%.
- Installed (2026-10-24 ~21:50): MISCELLANEOUS level-select section (docs/pending/levelselect-misc.md; DLL rebuilt for
  the new list name in gamenet_manager / thugpro_compat / level_editor_layout / level_editor_nodes), THPS2 at
  --hd-textures 4 --hd-model realesrgan-x4plus-anime (1.37 GB). Main install now: THPS2 + THPS3 4x, HD UI. Gap noted:
  gamenet_manager's net lookup list lacks the thps2 / thp8 arrays. Next: DXT compression of upscaled 32-bit textures.
