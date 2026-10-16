# CScript group D - the script manager (branch wt/csD)

NOT run in the game yet. Source: `src/game/script/cscript_manager.cpp`; tests: `src/hook/selftests_csD.cpp`.

## Rewritten (24 functions, all REPLACE - every one is at least 16 bytes)
| addr | function |
|---|---|
| 0x00475920 | UpdateSpawnedScripts |
| 0x004753C0 | DeleteSpawnedScripts |
| 0x00475420 / 0x00475670 | SpawnScript (checksum / string) |
| 0x004756C0 | KillSpawnedScript |
| 0x004756F0 | StopAllScriptsUsingThisObject (= StopScriptsUsingObject(object, 0)) |
| 0x00475700 | DeleteFinishedScripts |
| 0x00475790 / 0x004758F0 | RunScript (checksum / string) |
| 0x004752F0 | RunScriptOnce |
| 0x00473350 / 0x00473420 | SendRunScriptMessage (0x48) / SendSpawnScriptMessage (0x49) |
| 0x00472D80 / 0x00472DC0 | PauseSpawnedScripts / UnpauseSpawnedScript |
| 0x00472DE0 / 0x00472E00 / 0x00472E20 | ScriptExists / GetSpawnId / FindSpawnedScriptWithId |
| 0x00473FA0 / 0x00474040 / 0x004740D0 / 0x00474190 | KillSpawnedScriptsThatReferTo / WithId / WithObjectAndId / WithObjectAndName |
| 0x00474250 / 0x004742B0 / 0x00474350 | StopAllScripts / StopScriptsUsingObject / StopScriptsUsingObjectThatReferTo |

Callers switched to ours: chat.cpp (x2), service.cpp (RunScript, SpawnScript), music.cpp, memcard_cfuncs.cpp
(RunScript), sio/device.cpp (RunScript(string) "refresh_chosen"). Tests that stood in for 0x00475790 / 0x00475420
by address now stand in for ours too: selftests_misc.cpp (service task: both; LoadFromMemoryCard: RunScript),
selftests_gapsA.cpp (music StartTrack), selftests_pc.cpp (TestPlayNextTrack's JumpBoth), selftests_skaterB.cpp
(TestKeyboardTask - the chat code - a second Patch over ours).

## Calls into other groups (GameFunction, `// -> ...` comments, for the merger)
All in the anonymous namespace at the top of cscript_manager.cpp:
- `NewScript()` = operator new 0x00449970 + Construct 0x004738E0 (group A) -> `new CScript` (the original inlines
  CPoolable new in SpawnScript / RunScriptOnce - the same code as 0x00449970, checked).
- `DeleteScript()` = Destroy 0x00474420 + operator delete 0x004499B0 (group A) -> `delete script` (null-checked;
  every original site either checks or can't be null).
- `SetScript()` 0x00474540 (A), `Stop()` = ClearScript 0x00473A80 + ReportStopped 0x00473010 (A - the original
  walks inline ReportStopped; checked byte for byte equal to 0x00473010), `UpdateScript()` 0x004750B0 (B),
  `CallMemberFunctionOf()` 0x00472980 (B).
Still the game's: LookUpSymbol is ours already; InNetGame 0x0044E4F0, GetClient 0x00532EA0, MsgDesc ctor
0x004549F0, EnqueueMessageToServer 0x00459E50, WriteToBuffer 0x00479E20; objects through vtable +0x0C / +0x1C.

## Header changes (cscript.h)
- Only a comment: RunScriptOnce's `results` is NOT null-checked (the header said "if not null").
- No offsets or declarations changed. `ScriptRefersTo` (load.cpp/load.h, 0x00472C50) is reused for the
  "name at any level" test.

## Findings
- **Next pointer, kill walks vs stop walks.** The four Kill* walks (0x00473FA0, 0x00474040, 0x004740D0, 0x00474190)
  read `next` BEFORE stopping; StopAllScripts / StopScriptsUsingObject / ...ThatReferTo (0x00474250, 0x004742B0,
  0x00474350) read it AFTER stopping (the plan says "before" for all). Since stopping never unlinks, this only
  matters if an on-exit script run by ClearScript changes the list; kept as in the original.
- The three Stop* walks don't check SCRIPT_FLAG_SPAWNED: they stop every script (objects' too). A null object
  matches every level without an object (StopScriptsUsingObject(null, ...)).
- UpdateSpawnedScripts: a paused script is still updated when it has no code (so it finishes and goes).
  SCRIPT_FLAG_PAUSE_WITH_OBJECT with no object: runs.
- SpawnScript: the script's object is always null (SetScript(name, null, params, null)); the CFunction gives
  m_node; only the C function case pushes the script heap (around making the temporary script; the script case
  doesn't). RunScript pushes it around new + Construct + SetScript (SetScript's allocations land there).
  m_callbackScript is set only when there is a callback; the callback parameters are a fresh copy
  (NewStruct + AppendStructure) - even with no callback parameters (an empty structure).
  A missing name spawns a script anyway (SetScript stops it; it is deleted at its first update - its callback,
  if any, still runs).
- SpawnScript's `permanent` only goes into the 0x49 message (byte at +0x0C of its 16 bytes; the last 3 bytes are
  uninitialised stack in the original - ours zeroes them). Message layouts: 0x48 = {name, object id (+0x14 of the
  object, -1 none), WriteToBuffer(params, 0x400 bytes, 1, 0)} with length = written + 8; 0x49 = {name, object id
  (always -1 from SpawnScript), node, permanent}, length 0x10. Both: MsgDesc +0x0C = 2, +0x10 = 8 (meanings
  unknown), to GameNet client 0 (manager +0x08). Bytes 0x12-0x13 of MsgDesc are never initialised (ctor).
- RunScript: a member function with an entry goes to the free CallMemberFunction (entry, params, object, null),
  without one to object->vtable[3](name, params, null) - no null check of the object. The network message is
  sent for scripts, C functions and member functions alike (before running), and only if the symbol is one of
  those three types. `nameString` is unused.
- CFunc_SpawnScript (0x0059F330) calls SetSpawnerFile with `this` = SpawnScript's result (confirmed: MOV ECX,EBX),
  unless its flag 0x01917DF2 is given - so a SpawnScript of a C function (result null) from script would crash
  there without that flag (original).
- **Open question 3 (KillSpawnedScriptsWithId and +0xD0).** +0xD0 is the spawner's .qb file checksum
  (SetSpawnerFile: FindSymbol(name)->unknown8, which load.cpp / precompiled.cpp set to the file's checksum). The
  callers: CFunc_KillSpawnedScript (0x0059F450, `id=`); 0x00494E40 / 0x00496590 / 0x004963B0 (front-end screen
  element code) pass the checksum of the script they spawned earlier (0x004963B0 stores the name at +0x4C, kills
  the previous one's id, then SpawnScript(name, {id = +0x50}, ..., id 0)). Those scripts get spawn id 0 and no
  spawner file (spawned from C++), so the kill can only hit scripts whose spawn id, unique id or spawner file
  happens to equal that name checksum - i.e. scripts that script code spawned with `id = <that name>`; the
  element's own script itself is not matched. So the id test with +0xD0 is real (KillSpawnedScript id=<file>
  kills what a .qb file's scripts spawned), and the element callers look like they rely on scripts spawning
  their children with the element script's name as id.

## Tests (Early, all pass; 0 MISMATCHED)
- TestScriptManagerWalks: every kill / stop / pause / find / delete walk, KillSpawnedScript, GetSpawnId over a
  12-script list (spawned or not, paused, pause-with-object, not session specific, tracker-blocked, event tables
  or not, objects at the top and at levels, a 3-deep call stack in a heap block), each match field hit and
  missed, and an empty list. Stand-ins: ClearScript (clears pc), the event table 0x0045FA60 / 0x0045FE60, the
  tracker 0x00462B90, Destroy (unlinks), delete, Update, object vtables. The whole list and every block compared.
- TestUpdateSpawnedScripts: every Update answer (0/1/2/5), callbacks with and without parameters, paused
  with/without code, object paused / running / none, and Updates that spawn a script at the head, stop a later
  one, kill the next spawned one outright (read after "freeing"), kill an earlier one, call DeleteSpawnedScripts
  (deferred); a non-spawned head, a spawned tail, nothing spawned, an empty list, a pending delete from before.
  RunScript stood in (records the UpdatingSpawnedScripts flag too).
- TestSpawnAndRunScript: SpawnScript (both) for scripts, C function, member function name, integer symbol,
  missing name, with/without callback and callback params, every flag, net on/off with InNetGame true/false;
  RunScript (both) for scripts (Update loop 2,1,5,2,0), C functions, member functions by entry and by name, other
  types and missing names, the net flag; RunScriptOnce with and without parameters; ScriptExists. Each with the
  pools present and absent (heap allocation), on an empty and a non-empty list; the real Construct runs.
- TestScriptNetMessages: both messages in and out of a network game, objects and none, buffer sizes 0..0x3F8.
- Mutations: 56 deliberate bugs (every branch, flag, field, call order, the deferred delete, the walks' next
  pointer, the message fields), 56 caught - 55 at once (two of them by a crash/hang: a null object's vtable,
  RunScript of an integer symbol spinning in the Update loop), 1 after strengthening (upd_next_spawned: the
  next SPAWNED script is picked before the Update - a case where the non-spawned next becomes spawned during it).
- Register scan: only the known 0x004EFF92 RISK; ScriptExists (AL), GetSpawnId, FindSpawnedScriptWithId,
  SpawnScript (EAX) return values. Call audit: no MISMATCH. check_code_writes: no overlaps. find_gaps: nothing
  of group D left (the gaps in range belong to groups A-C). No PartyMod patch in range (its only code patch is
  0x004E26A0; tools/check_partymod_overlap.py isn't in this checkout).

## Scheduling semantics (frame level - the spec for the QuickJS port)
1. Once per frame UpdateSpawnedScripts (0x00475920, from 0x0054F380): sets "updating" (0x006F3544), clears the
   one-per-frame-released flag (0x006F352C) - so WAIT_TYPE_ONE_PER_FRAME lets one script through per frame.
2. Walk from the list head (newest script) to the tail (oldest). For the script at the cursor, FIRST pick the
   next SPAWNED script after it (skipping non-spawned ones); the walk continues there no matter what happens.
3. The cursor script is updated iff spawned, AND not (paused AND has code), AND not (pause-with-object AND has an
   object AND object->vtable[7]() == false). Exactly one Update call.
4. If Update returns FINISHED (0): if m_callbackScript != 0, RunScript(callback, callbackParams, the script's
   object, net=false) runs synchronously to completion right there (a callback that waits on frames spins; one
   that blocks never returns); then the script is deleted (Destroy: ClearScript, ReportStopped, unlink, free).
   Results 1 (blocked / real-time wait), 2 (waiting) and 5 (an interrupt returned) leave it for the next frame.
5. New scripts (SpawnScript during the walk - from an Update or a callback) go to the head, which the walk has
   passed: their first Update is next frame. SpawnScript itself runs no statement (SetScript only parses the
   default parameters).
6. A script stopped during the walk (any Kill*/Stop* walk: ClearScript = pc null, on-exit scripts run at once)
   stays on the list; if the walk hasn't reached it yet, it is reached this frame, Update says FINISHED, its
   callback runs and it is deleted; if already passed, the same happens next frame. Non-spawned stopped scripts
   are never deleted here (DeleteFinishedScripts / their owners do it).
7. A script deleted outright during the walk (KillSpawnedScript, or a DeleteSpawnedScripts outside the walk) that
   is the cursor's precomputed next is still visited (use after free in the original - its links are stale).
8. DeleteSpawnedScripts during the walk only sets 0x006F3545; after the walk "updating" is cleared, then the
   pending deletion runs (every spawned script not NOT_SESSION_SPECIFIC - callbacks NOT run) and the flag clears.
9. RunScript (not spawned): the script is made, SetScript, then Update in a loop until FINISHED, all in the
   same call. Each loop turn is one Update, so waits count turns, not frames: `wait n frames` takes n extra turns;
   a timer wait subtracts the (unchanging) slomo frame time per turn, i.e. it ends after about t/dt turns; a
   real-time wait busy-waits on the clock; BLOCKED (and component waits that never say done) never end - the
   game hangs (original). RunScriptOnce: one Update, the script's parameters merged into `results`, deleted
   whatever its state.
