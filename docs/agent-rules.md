# Rules for parallel agents

What the lead (the main Claude session) passes to every agent it starts. Give the agent this file, its task, its
workspace (C:\Games\THUG2-n) and its pending-doc name. Agents cost a lot of usage: keep each to a clear range or task.

## Every agent
- Repo C:\Dev\thug2-decomp. The agent works in its own git worktree + branch and commits there, never on main; the lead
  merges. Read CLAUDE.md first.
- **Morten1337's private repositories** (thugpro, thugpro_proxy, thugpro_toolchain, thug2-decomp, ...) may be read and
  their insights used (the user has his permission since 2026-10-20) - but **never copied 1:1**: anything taken must
  be rewritten in this project's style, with a comment naming where the idea came from. Clone them with the machine's
  git credentials into your scratch folder; never push them anywhere. THUGPRO's installed binaries
  (C:\Users\kevin\AppData\Local\THUG Pro, C:\Games\THUGPRO) may be inspected; never write there. Don't start sub-agents.
- Naming: follow this project's established names and conventions - accurate and consistent, not Neversoft's. Outside
  names (Morten's dump, symbol lists) may help you understand code; they don't decide our names or rename existing code.
- **Never read, reference or copy leaked Neversoft / Activision source code** - e.g. https://github.com/thug1src/thug
  (THUG1's source) or any other leak of a Tony Hawk game's code. Our code comes from our own decompilation of the
  game; leaked code would taint it.
- Records go in docs/pending/<name>.md, not docs/decomp-progress.md (the lead folds them in with batch numbers). Run
  tools/progress_report.py but don't commit docs/PROGRESS.md (the lead regenerates it).
- Commit messages end with the Co-Authored-By line given in the session.
- Python: `python -I`, always `encoding="utf-8"`. Use the Write / Edit tools for text with backslashes (shell
  heredocs mangle them).
- Merge main into the branch every 2-3 commits so conflicts stay small. Shared files (CMakeLists.txt, shared headers,
  tools/level_port/*.py): small, additive, backwards-compatible edits only, listed in the pending doc.
- Final message: short summary, branch, commits, game-run results, what the user should try or check by hand, and
  (decomp) how to switch the work off (`[replacements] disable=` file names).

## The game: the agent's workspace only
- C:\Games\THUG2 is the main install (the user's decision, 2026-10-19): only merged main work, installed by the lead.
  Since 2026-10-20 it's a stock install: the game is in C:\Games\THUG2\Game (THUG2.exe, an unwrapped no-CD exe, never
  modified). Never write to C:\Games\THUG2_PM_BAK (the old main install) or C:\Games\THUG2-THPSX-ORIGINAL (the stock
  install's untouched backup).
  It's the user's own, for manual testing (its self-tests are off); agents never use it, not even with its lock. All
  test runs happen in the workspaces (make_workspace.ps1 turns their self-tests on). Each agent gets one workspace: C:\Games\THUG2-1 ... THUG2-4 (1280x720
  window, allow_multiple_instances=1). Never touch another workspace.
- Scripts in tools/game (see its README): `game_lock.ps1 -Action acquire|release -Owner <you> -Game <workspace>`,
  `run_game.ps1 -Game <workspace> -Seconds N [-Keys ...]` (posts keys without focus, PrintWindow pictures; never pipe
  it into Select-Object -First), `window_shot.ps1 -ProcessId <pid>`, `boot_level.py <level> <workspace>`.
- Take the workspace's lock for every game run, copy in your own build's dinput8.dll if needed, read
  `<workspace>\thug2-decomp.log` after every run, release the lock (it puts the DLL and ini back).
- Screenshots: the game's window only, never the screen. Never close a game you didn't start. Never delete original
  game files (files we made may be deleted).
- Load checks and idle runs are fine. Anything a player must judge (does a script fire when skated into, does a menu
  look right in use, does the sky change over time): don't fake it with scripted keys; list it for the user. The
  user may take the controls during a run; if the skater moves on its own, it's probably the user.

## Decompilation agents
- Follow the `decomp-batch` skill (.claude/skills/decomp-batch/: SKILL.md, pitfalls.md, test-template.cpp) for every
  batch: read the disassembly, write, build (tools\verify_build.cmd in YOUR worktree), self-test original vs ours,
  prove the tests with tools/mutate.py, the safety scans, commit.
- "Start to finish": every function in the range that isn't REPLACEd yet gets source (REPLACE / REPLACE_SOURCE_ONLY /
  naked stubs) - skip nothing. Batches of 10-40 coherent functions, one commit each, until `find_gaps.py` and
  `progress_report.py` show the range done. Calls out of the range into un-rewritten code stay `GameFunction`.
- Self-tests in the agent's own src/hook/selftests_<topic>.cpp (listed in CMakeLists.txt), not selftest.cpp.
- Ghidra: a private copy of the read-only snapshot (copy C:\Games\THUG2-ghidra-snapshot to
  C:\Games\THUG2-ghidra-snapshot-<name> once) and its own data/derived/. Don't rename in the live project (MCP) -
  read only.
- If the build fails with C1033 / C2471 / LNK1201 / LNK1318: delete out\build\verify\vc140.pdb and thug2_check.pdb.

## Level-port agents
- Ported levels in free skate keep the original's scripts, peds, vehicles, moving objects and particles where
  feasible; goals / career aren't required. Use the originals' own assets (models, particles, sounds, animations);
  THUG2's behaviour only where the original's logic is engine code that can't run (docs/level-port-compromises.md).
  Nothing of either game is committed: the tools read the user's files each time.
- Shared code: tools/level_port/script_port.py (the script translator, all four games), peds_cars.py, nodes.py
  (strip_missing_trick_objects, fix_terrains...). Run every converter's `selfcheck` (thps3_levels, thps4_levels,
  thug1_levels, thaw_levels) before each commit. tools/level_port/script_diff.py compares converted scripts between
  two staging folders.
- Converters: `--thug2 <workspace>` (or `--out <scratch>`); install with `make_menu_level.py --game <workspace>`.
  Converting only some levels rewrites the staging's levels.json with only those: finish with a full convert.
- Known crashes to check output against offline: docs/level-port-compromises.md section 3. Also: a rail with a
  missing TerrainType crashes (nodes.fix_terrains); dropping nodes after links were renumbered shifts every later
  link (use placeholders).
