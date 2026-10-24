# THUG2 decompilation - notes for Claude

**Goal:** readable C++ for Tony Hawk's Underground 2 (PC) that opens in an IDE, builds, and runs the game -
functional equivalence, not byte-matching. **In the end the game itself is compiled from this source** (no
original exe, no DLL injection). Until then, functions are replaced one at a time in the running game by a
hook DLL (re3/OpenRCT2 style) loaded as a proxy `dinput8.dll` - that's how they're tested, not the goal.
So no game function is skipped: ones too small to hook (3-byte stubs) or that we don't want hooked are
still written and registered with `REPLACE_SOURCE_ONLY` (compiled, counted, argument sizes checked, not
installed); functions PartyMod patched get source with PartyMod's behaviour. Later phases: turn
`GameVariable<T>(address)` globals into real variables, rewrite everything still reached via
`GameFunction`, and link the libraries (D3DX9, GameSpy, CRT) instead of decompiling them.

## Where things are
- Game (the main install, since 2026-10-20): `C:\Games\THUG2\Game\THUG2.exe` - a stock install with an already unwrapped
  no-CD exe (never modified; the game's root is the `Game` folder, the tools step into it). The reference build the
  addresses are named after, `THUG2PM.exe` (PartyMod's patch; SafeDisc removed, base 0x400000; the same layout, 172
  bytes apart), is in `C:\Games\THUG2_PM_BAK` (the old main install, the user's backup - don't change it) and in the
  workspaces; `C:\Games\THUG2-THPSX-ORIGINAL` is the stock install's untouched backup (never write there). Also in that
  folder: our `dinput8.dll`, `thug2-decomp.ini` (`[replacements] disable=` by file/name/address, `*` = all;
  `[selftests] enabled=`; `[features] skip_intro_movies=1` skips the start-up logo/intro movies,
  `widescreen_menus=0` brings back PartyMod's pillarboxed 4:3 front end, `allow_multiple_instances=1` lets a second
  copy run - its log is `thug2-decomp-2.log`, `mouse_menus=0` turns off mouse control of the menus (`debug_mouse=1` logs each click), `menu_backdrop=0`
  brings back the original front end (THUG2's skate shop, the spinning wheel); any other value (default 1) shows
  THPS4's skate shop behind the menus with a standard vertical main menu over its desk - src/game/front/menu_backdrop.h;
  needs the THPS4 levels converted (tools/thps4_levels) and installed by `tools/menu_level/make_menu_level.ts`, which
  makes the data overlay `thug2-decomp-data` (the menu's script in its `front_end` folder; at the top: the skate shop
  as a playable level - SKATE SHOP in the level select -, TEST LEVEL, the THPS4 levels); `level_editor=0` hides the pause menu's
  LEVEL EDITOR - src/game/front/level_editor.h (its UI is Dear ImGui with Inter / JetBrains Mono; `editor_ui=0` brings
  back the old screen-element HUD; `[editor] families=` remembers the Outliner's family chips); the item is in the same tool's overlay; its saved layouts are in
  `thug2-decomp-layouts\<scene>.txt` and load with their level - and `debug_editor=1` logs its picks and moves;
  (the DLL also runs on a stock, already unwrapped no-CD `THUG2.exe` unchanged - the same layout, 172 bytes apart:
  the exe is never modified; tools/game finds either exe, `node tools/game/install_manifest.ts check` names them, and the
  early self-tests put THUG2PM.exe's bytes back in the differing spots while they run - src/hook/selftest_runner.cpp;
  the retail SafeDisc THUG2.exe can't run anything); `screenshots=0` turns off the screenshot key (Print Screen - F12 when Windows' Snipping Tool takes it; `[Keybinds] Screenshot=` an SDL scan code or key name): a PNG + a sidecar .txt (level, mode, skater, camera, editor state, the log's last lines) in `Screenshots` next to the exe (`screenshot_dir=`) - src/game/sys/screenshot.h; `restart_tester=0` turns off F8, which puts the skater at the level's next unvisited restart - src/game/front/restart_tester.h; `loading_console=0` turns off the loading screen's console (a panel of what is loading - files, scripts, log lines - with the time since the load began and "last activity N s ago", orange after 10 s without any: a hang, not slow loading) - src/game/gfx/loading_console.h; `debug_mode=1` makes the script command CD false / NotCD true after start-up: Neversoft's DEBUG OPTIONS in the pause menu, DEVKIT_ONLY levels, the viewer's free camera on F9 and its keys - src/game/sys/debug_mode.h; `start_level=<name>` or the command line `-level <name>` starts straight in that level in free skate (`-at x,y,z[,heading]` / `start_at=` puts the skater there standing; `node tools/game/try_spot.ts <level> <x,y,z,h>` launches it) - src/game/front/start_level.h; `edit_level=<name>` or `-edit <name>` does that, skips the intro movies and opens the level editor there (`node tools/game/edit_level.ts [<name>]` lists the levels / starts one); `anteara_mod=1` (default 0) plays the user's own Anteara Mod (2010+2011 merged, a 2010 submenu in its pause menu) from the overlay's `anteara` folder, which make_menu_level.ts builds from the game's loose Data\scripts and the mod's own changes in tools/anteara - README there; `[Miscellaneous] OnlineGameName=` / `OnlineGameKey=` pick the online identity - the GameSpy game name and its 6-character secret key; empty (the default) keeps THUG2's own, thps6pc / AdLWaZ, OpenSpy's "Beenox" lobby; `OnlineGameName=thugpro` (key left empty: THUGPRO keeps THUG2's) is THUGPRO's, its "General" / "Newbies" lobbies - joining THUGPRO players' sessions may not work, their scripts and DLL differ; test online only by hand, tools and agents never go online - src/game/net/online_identity.h; `[online] thugpro_peer=1` (or OnlineGameName=thugpro) speaks THUGPRO's chat / server-info messages - src/game/net/thugpro_peer.h; `[features] thugpro_parts=` 1 (default) reads THUGPRO's extra CAS parts / boards from the user's THUGPRO install by reference (hidden; 2 also shows them in CAS, 0 off; `thugpro_folder=` another install) - src/game/net/thugpro_compat.h; `automation=1` - workspaces only, never the main install - turns on the test-automation channel: commands from thug2-decomp-commands.txt (run / choose / press / waitfor...), state in thug2-decomp-status.json, online scripts and menus refused - src/game/front/automation.h, tools/game/README.md), `thug2-decomp.log` (read it after every game run). The retail THUG2.exe is
  SafeDisc-encrypted and not used.
- Ghidra 12.1.4 at `C:\ghidra`; project `C:\Games\THUG2-ghidra` (the Ghidra MCP server uses it); a read-only
  copy for scripts at `C:\Games\THUG2-ghidra-snapshot` (refresh: `tools\ghidra\snapshot_project.ps1`).
  `tools/ghidra/inspect_range.py` surveys ranges and dumps disassembly/decompilation/jump tables/strings;
  `find_gaps.py` lists un-rewritten functions between rewritten ones (what was skipped), `find_stubs.py`
  the game's two-instruction functions.
- Progress counts REPLACE lines under src/game only. src/hook's REPLACE_FALLBACK hooks on 0x4E1220
  (ParseNetworkParams) and 0x44EEF0 (MainLoop) only run the self-test stages when our source for those is
  disabled; the source calls `RunStageOnce` itself.
- Build: VS 2026 (MSVC 14.51), CMake + Ninja, x86. The user builds `out/build/x86-debug` in Visual Studio
  (post-build copies the DLL into the game folder; sometimes "Rebuild All" is needed - check the deployed
  DLL's timestamp). I build with `tools\verify_build.cmd` (`out/build/verify`, no copy) and run
  `out\build\verify\thug2_check.exe`: argument-size checks + the Early self-tests, offline. Both builds are
  Debug: debug-CRT reports and run-time check failures are logged to thug2-decomp.log with a call stack.
- Progress and history: `docs/decomp-progress.md` (every batch, findings, what's tested; ends with
  "- [ ] Next:"), `docs/PROGRESS.md` (generated by `node tools/progress_report.ts`), `docs/subsystems.md`,
  `docs/library_ranges.md` (GameSpy, D3DX9, CRT: not decompiled).
- `tools/neverscript/` (violetvandal's THUG2 fork of byxor's NeverScript, see its THUG2.md): QB compiler/decompiler for `.qb` data files; build
  with `tools\build_neverscript.ps1` -> `out\neverscript\ns.exe`. 454/470 of the game's .qb round-trip byte-identical.

## How to work
- **Start with `docs/HANDOFF.md` if it exists** (work in flight, what's next), then the "- [ ] Next:" line of
  `docs/decomp-progress.md`.
- **Decompiling: follow the `decomp-batch` skill** (`.claude/skills/decomp-batch/`): workflow, test template,
  pitfalls. Never trust Ghidra's return types or parameter counts; check calls into un-rewritten game code
  too (`tools/ghidra/audit_game_function_calls.py`); prove tests with `node tools/mutate.ts`.
- **Commit every batch** once it builds and checks pass, then carry on - don't wait for in-game testing. The
  user tests later and reverts or disables a batch that breaks; say "NOT run in the game yet" in the progress
  doc, and record "tested in the game" when they report back.
- **Don't launch the game unless the user says so** (they may be playing; a running game also locks the
  DLL), and never copy DLLs into the game folder - except while holding the game lock: the user allowed agents to
  test in the game through it (acquire / release with `tools/game/game_lock.ps1 -Game <folder>`, which backs
  up the deployed DLL and ini and puts them back on release). Under the lock an agent may copy its own build's
  dinput8.dll into its folder, edit thug2-decomp.ini, launch and close the game. Files we made in the game
  folders (the lock, overlays, layouts, logs) may be deleted; original game files never.
- **C:\Games\THUG2 is the main install: only ever merged (main) work is installed there**, by the lead after
  merging. It's the user's own, for manual testing: its self-tests are off (`[selftests] enabled=0`) and nobody runs
  test games in it - the lead's own test runs too go to a workspace. Parallel work and agents' tests use the workspace copies C:\Games\THUG2-1 ... THUG2-4 (one agent each,
  1280x720 window, allow_multiple_instances=1; the lead assigns them): converters `--thug2 <folder>`,
  `make_menu_level.ts --game <folder>`, games run in the background (keys posted, PrintWindow captures).
- PartyMod (SDL2 window/input, settings in `thug2-decomp.ini`, glyphs, letterbox...) is part of the game's
  source now (MIT: notice in `THIRD_PARTY_NOTICES.md`); its DLL isn't loaded and nothing patches rewritten code.
  The only run-time writes into the exe are the GameSpy host names (`src/game/net/online_hosts.cpp`, until
  the SDK is linked), the online identity when OnlineGameName / OnlineGameKey are set (`src/game/net/online_identity.cpp`:
  only the key, over its 3 stored copies - every use of the game name is source now and reads the setting itself) and the IsPS2 and (debug mode) CD / NotCD CFunction table entries - `node tools/check_code_writes.ts` checks
  none lands in a rewritten function.
- **Tools are TypeScript** (being ported from Python, area by area): `node tools/<x>.ts` (Node 24, no build step;
  `npm install` once; `npm run typecheck` / `lint` / `test`) - conventions, the Python -> JavaScript traps, the parity
  harness and the migration tracker in `docs/typescript.md`. Python remains only for tools/ghidra (pyghidra),
  capstone_disasm.py and the not-yet-ported tools during the migration.
- Python on this machine defaults to cp1252: always `encoding="utf-8"`. Use the Write/Edit tools for C++
  text with backslashes (shell heredocs mangle them).
- Self-tests: by topic in `src/hook/selftests_<topic>.cpp` (each registers its own with SELFTEST; listed in
  CMakeLists.txt) - `selftest.cpp` is too big to share. `selftests_pc.cpp` has the CodePatch / JumpTo
  helpers (write bytes or a jump over game code for a test's length).
- Parallel agents work (one git worktree + branch each, own Ghidra snapshot copy and `data/derived/`, records
  in `docs/pending/<name>.md`) but cost a lot of usage - keep each to a few functions, and merge yourself.
  Give every agent `docs/agent-rules.md`; game-run scripts and the workspace maker are in `tools/game` (README);
  `node tools/fold_pending.ts` folds an agent's batch records into decomp-progress.md.
- Commit messages end with the Co-Authored-By line given in the session.
- **Morten1337's private repositories** (thugpro, thugpro_proxy, thugpro_toolchain, thug2-decomp and the others the
  user's account can reach - cloned with the machine's stored git credentials): since 2026-10-20 the user has his
  permission to use them, including insights gained from their code. **Never copy them 1:1** - code taken from them
  must be modified (rewritten in this project's style and structure), and say where an idea came from in a comment
  (e.g. "approach from Morten1337's thugpro"). Nothing of theirs is pushed anywhere else or published wholesale. His
  public repositories may be used within their licences (thps-formats is MIT: attribute in THIRD_PARTY_NOTICES.md).
  THUGPRO's installed binaries (C:\Users\kevin\AppData\Local\THUG Pro, C:\Games\THUGPRO) may be inspected too; never
  write into those folders. Pass this on to every agent.
- **Naming: keep this project's own established names and conventions** (the user, 2026-10-20). Names only need to be
  accurate and consistent with what we already have - not Neversoft's. Outside names (Morten1337's thug2-decomp dump,
  the Downhill Jam map, the skate7 symbols) may help to understand what code does, but they never decide what we call
  it, and existing code is not renamed to match them.
- **Never read, reference or copy leaked Neversoft / Activision source code** - e.g. https://github.com/thug1src/thug
  (THUG1's source) or any other leak of a Tony Hawk game's code: our code comes from our own decompilation of the game,
  and leaked code would taint it. Pass this on to every agent too.

## Facts about the binary
- Built with VS .NET 2003 (VC 7.1 RTM, 13.10.3077) with whole-program optimisation: some functions take
  arguments in registers (EAX/EBX/ECX/EDX/ESI/EDI) or mix registers with caller-popped stack arguments
  (these get naked asm stubs), and some callers rely on callees preserving ECX/EDX
  (`tools/ghidra/scan_register_assumptions.py` after every batch).
- Code: ~8,365 game functions / 1.6 MB. Script runtime 0x45EFD0-0x47DC40; CFunction table at 0x67F3A0
  (1254 entries, data/derived/cfuncs.csv - generated by tools/extract_cfuncs.ts, not committed).
- PartyMod's exe patch is in one place (WinMain call at 0x4E26A0 -> a LoadLibrary cave at 0x5480A1 that
  loads its DLL; window title string) - only the original WinMain runs it, ours doesn't; our THUG2PM.exe also
  differs from PartyMod's canonical output somewhere unknown.

## Later
- Replace the QB script interpreter with embedded JavaScript (QuickJS): decompile QB bytecode straight to
  readable JS (not via the community "q" text syntax), API from the CFunctions with generated .d.ts typings,
  scripts as generators driven by a scheduler that matches the original's exactly. While decompiling the
  script runtime, document its scheduling semantics - they're the spec. The token format is in
  src/game/script/qb.h. The runtime's behaviour so far: docs/script-runtime.md.
