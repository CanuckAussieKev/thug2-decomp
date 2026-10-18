# Handoff (2026-10-19)

State when the session was handed over (the user changed Claude accounts). Read CLAUDE.md, then this, then the
"- [ ] Next:" line of docs/decomp-progress.md.

## Where things stand
- **main** has everything: every agent branch is merged and pushed. No agents are running.
- **Decomp:** 2047 / 8382 functions (21.45%, docs/PROGRESS.md). Last ranges finished: Time-of-day (batches 110-113)
  and Front-end UI: screen elements & text (batches 114-121). Both built clean, all 2139 argument sizes match,
  all 219 early self-tests pass; both ran in a workspace game with no errors. **Neither has been judged by the user
  yet** - what to check: docs/pending/time-of-day.md and docs/pending/frontend-ui.md ("For the user to check").
- **C:\Games\THUG2 (main install):** main's dinput8.dll (built at d60f98c) and all 56 ported levels (THPS3 14, THPS4 11,
  THUG1 16, THAW 15) with their scripts, peds and vehicles; `debug_collision=0`, nothing disabled.
- **Level ports:** docs/level-port-compromises.md is the state per game, the crash workarounds and the exact-assets
  plan. Every level loads; the scripts / peds / vehicles haven't been played by the user (lists in
  docs/pending/thps3-scripts.md, thps4-scripts.md, thug1-scripts.md, thaw-scripts.md).

## Waiting on the user
- Playtests: the four games' scripts / peds / cars / moving objects; THAW glass sound; Minneapolis and Santa Monica
  (fixed crashes); time of day in a career level; the front-end menus; a hosted network game.
- **The exact-assets work** (originals' particles, sounds, car models, ped / animal skeletons and animations instead of
  THUG2 stand-ins): proposed, not started - the user's go is needed. Order: docs/level-port-compromises.md section 8.

## How work is done
- Parallel agents: one git worktree + branch each, one workspace game folder each (C:\Games\THUG2-1 ... -4), the rules
  in docs/agent-rules.md (pass the private Morten1337 repository ban on to every agent). The lead merges, builds
  (tools\verify_build.cmd + out\build\verify\thug2_check.exe), folds docs/pending/<name>.md batches into
  docs/decomp-progress.md with batch numbers (the next is **122**), regenerates PROGRESS.md
  (`python -I tools/progress_report.py`), commits, pushes, then installs main into C:\Games\THUG2 under its lock
  (tools/game: copy the DLL over both the game's and the lock's `.backup` copy; converters + `make_menu_level.py`).
- Game test scripts: tools/game (README there). User rules: launch only when allowed, never capture the whole screen,
  never close a game you didn't start, hand interactive tests to the user.
- The workspaces may still hold older branch data (THUG2-1: THUG1, THUG2-2: THAW, THUG2-3 / -4: decomp builds); refresh
  one with the converters' `--thug2 <folder>` and `make_menu_level.py --game <folder>` before using it.
- `.claude/worktrees/` holds old agents' worktrees, all merged; they can be removed (`git worktree remove`).

## Notes not folded into decomp-progress.md
docs/pending: collision-debug, levelselect, mainmenu, pedscars, thps3, thps4-lighting, thug1, thaw and the four
*-scripts records are level-port / tool records (not decomp batches) - kept as the detail behind
docs/level-port-compromises.md.
