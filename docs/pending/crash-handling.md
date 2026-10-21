# Crash handling (agent, 2026-10-22) - continuing a stopped agent's work

The user: "One of the instances encountered a buffer overflow. Make sure the agents can handle that and close it
properly." Workspace games hung on a modal box instead of logging and exiting.

## What the box was
THUG2's own VC7.1 CRT: `__security_error_handler` 0x006287DD (called by report_failure 0x00622F9E when one of the
game's functions finds its /GS stack cookie overwritten) shows "Buffer overrun detected!" through `__crtMessageBoxA`
0x00634205 and waits; R60xx runtime errors (`_NMSG_WRITE`, 0x0062D9C0 on) use the same box. No crash dump is written
for it (no exception), so none of the dumps show it; thug2_check already redirected 0x006287DD offline for this reason.

## Done
- `src/hook/crash.h/.cpp` (crash code moved out of log.cpp, formats kept): SetErrorMode(FAILCRITICALERRORS |
  NOGPFAULTERRORBOX | NOOPENFILEERRORBOX), WerSetFlags(QUEUE), `_set_abort_behavior` + SIGABRT handler,
  invalid-parameter and purecall handlers, Debug CRT report mode/file -> the log and a hook (warning: logged; assert:
  logged, carries on; error: ends), `_RTC` hook (logs, ends), unhandled-exception filter (reports if the vectored
  logger hadn't, "  unhandled: ...", ends with the exception code), vectored logger also for 0xC0000409 / 0xC0000374 /
  0xC0000417 from any module. Every fatal report ends with "  the process ends (exit code ...)" after the crash hook.
- The game's CRT, data only (no code written): `user_handler` 0x007DB104 -> GameSecurityError (BUFFER OVERRUN
  DETECTED + stack scan, exit 0xC0000409); cached MessageBoxA 0x007DB55C -> GameCrtMessageBox (GAME CRT MESSAGE,
  exit 3). Same addresses in the stock THUG2.exe (its 172 differing bytes are elsewhere).
- hook.cpp StopIfReplacementsFailed: no box when `[features] automation=1`.
- Automation `crash <kind>`: access rtc crterror invalidparam purecall abort overrun crtbox fastfail hang.
- thug2_check: the eight loggable kinds each in a child process (the game image loaded): exit code + report checked,
  60 s limit (a dialog = FAIL). Mutation: without InstallGameCrtHandlers, overrun and crtbox hang on the game's box and
  are killed - 2 FAILED. Self-tests `selftests_crash.cpp` (classification, names, the game's __crtMessageBoxA calling
  the cached pointer with its own arguments) and automation parse.
- Tools: `tools/game/game_health.ps1` (exit code != 0, WerFault for the pid, own #32770 dialog, IsHungAppWindow for
  -HangSeconds while idle - a long load works the CPU: the first THUG2-3 cold start, 106 s, was a false positive before
  the CPU rule); run_game / game_cmd close and record (exit 5, `<folder>\thug2-decomp-crashes.txt`), load_check
  reports it per level, game_lock release closes a workspace's leftover game. No PrintWindow on a hung window (it
  blocks). symbolize.ts: last report of any kind, "dinput8.dll+0x" items; tools/game/read_dump.ts (the stopped agent's
  file, reviewed: the stack scan found nothing - WER dumps hold no module code; now checked against the modules' files
  with matching PE timestamps, `--image` for a build's DLL, "?" when unchecked).

## The dumps (C:\Users\kevin\AppData\Local\CrashDumps) - all access violations, none ours
| dump | workspace | where | cause |
| --- | --- | --- | --- |
| THUG2.exe.42008 / 18244 / 21844 / 14816 | THUG2-3 (hd-textures; its ini had debug_mode=1) | 0x00482BA2 FrameIndex(null) from our CStandardAnimController PlayAnim / NewSource | an animation missing from the set (SourceFrameOf null); the original crashes the same way. Now logged first ("anim: no animation ...") |
| THUG2PM.exe.14104 | THUG2-2 (THPS2 port) | CStruct::GetChecksum this=null from IsAlive 0x005A5380 -> 0x005A3D10 | GetNode(name, assert) found no node (a trigger script's node missing from the NodeArray); original crashes too. Now logged ("nodes: no node named ...") |
| THUG2PM.exe.448 | THUG2-2 | CFunc_GetArraySize array=null (trigger script) | its parameters had no array; original crashes too. Now logged with the script's name |
| THUG2.exe.43820 / 17452 | THUG2-1 | 0x004C62CA: CreateVertexBuffer failed, its null buffer used | game code (D3D buffer creation unchecked) - a too-big or zero-size buffer / out of video memory in that agent's level; not changed |

The "buffer overflow" box itself left no dump; next time it's a BUFFER OVERRUN DETECTED report with the return
addresses (the overwritten function's caller first).

## Tested
- thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 mismatches, 539 early self-tests pass, crash handling 8/8.
- check_code_writes: no overlaps. tsc / biome clean on the TS.
- THUG2-3 under the lock (debug_mode set to 0 for the runs; ini put back at release): `crash overrun` -> BUFFER OVERRUN
  DETECTED logged, exit 0xC0000409, no dialog, no dump; `crash access` -> EXCEPTION + unhandled, exit 0xC0000005;
  `crash crtbox` -> GAME CRT MESSAGE, exit 3; `crash hang` -> "hasn't responded for 20 s, idle", closed;
  `crash fastfail` -> exit 0xC0000409, nothing logged (as expected), no dialog, no dump. Each recorded by run_game
  (exit 5) with the log's end. `game_lock release` with a game left running: it was closed (killed - it had no window
  yet), DLL and ini put back.
- The hang run showed PrintWindow on a hung window blocks (run_game waited until the game was killed by hand): run_game
  now only takes the trouble picture for a dialog (not re-run after that fix).
- NOT run in the main install (self-tests off there; nothing to see without a fault).

- [ ] Next: when a BUFFER OVERRUN DETECTED report shows up, symbolize it: the first game address after
  0x0062880A is the function whose frame was overwritten - look at what of ours it calls with a buffer.
