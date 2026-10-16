# CScript group A - life cycle and resetting (branch wt/csA)

NOT run in the game yet.

## Rewritten (src/game/script/cscript_core.cpp, 22 functions)
- 0x004738E0 Construct, 0x00474420 Destroy, 0x00449970 / 0x004499B0 operator new / delete (the CPoolable pair
  0x006F3548 - `Mem::PoolPair::Allocate` / `Free` do exactly what they do).
- 0x00473A80 ClearScript, 0x00473880 RunOnExitScripts, 0x00473010 ReportStopped, 0x004739D0 CopyParamsIfOwned.
- 0x00474540 SetScript(name, code, params, object), 0x004747A0 / 0x004747C0 its checksum / string overloads,
  0x00474720 SetScriptCopy, 0x004747F0 Restart.
- 0x00472C90 AllScriptsStillDefined, 0x00472940 GotScript, 0x00472F90 SetSpawnerFile, 0x00473BE0
  ObjectDestroyed, 0x00472ED0 / 0x00472EE0 / 0x00472EF0 the OnExceptionRun / OnExitRun accessors, 0x00472F00
  ClearOnExitScripts, 0x004734B0 FindScriptWithUniqueId.
- All hookable (>= 7 bytes); all REPLACE. The noexcept of operator new / delete is cast away in the REPLACE lines
  (otherwise the argument-size check silently treats them as unchecked).
- load.cpp: the four GameFunction wrappers (RestartScript 0x004747F0, ScriptsStillDefined 0x00472C90, ClearScript
  0x00473A80, ScriptStopped 0x00473010) are gone - UnloadQB / RestartChangedScripts call the methods. TestUnloadQB
  (selftests_qb.cpp) now stands in for our methods too (`AddressOf(&CScript::...)` next to each game address).
- Not done (as agreed): the five sound functions 0x0047AC10-0x0047AD80.

Still the game's (GameFunction, local helpers in cscript_core.cpp): CRefCounted's constructor 0x004637B0 and
destructor 0x00463870; the event handler table's 0x0045FA60 (forget a script), 0x0045FE60 (pass an event),
0x0045FB70 (destructor); the tracker's 0x00462B90. Into group C: Interrupt 0x00475750 (`// -> CScript::Interrupt
(group C)`, twice in RunOnExitScripts).

## Header changes (cscript.h)
- NEW `struct SStructScript { uint32_t mNameChecksum; const uint8_t* mpScriptTokens; }` (8 bytes), before
  CMemberFunctionEntry.
- SetScriptCopy's first parameter was `CSymbolTableEntry* symbol` - wrong: the disassembly reads `[arg+0]` as the
  name and `[arg+4]` as the code, and its one caller (0x0048B200) passes an 8-byte block it zeroes and fills with
  CStruct's 0x00476900 (GetScript from a structure). Now `SetScriptCopy(SStructScript* script, ...)`.
- Nothing else; no offsets changed.

## Findings (behaviour kept)
- Construct: CRefCounted's constructor runs first, then the whole 0xF4 bytes are zeroed - m_refCounted's vtable
  stays null for the script's life (kept; harmless unless something calls through it). It goes to the head of
  the list; m_uniqueId = the counter before it's incremented.
- Destroy: if the event handler table is in use (+0x0C != 0), only its "alive" byte (+0x08) is cleared and the
  table isn't freed here (whoever is running it must free it); otherwise destroyed + Manager::Free. Callback
  parameters freed but the pointer isn't cleared. ReportStopped is inlined in the original (same code).
- ClearScript reads the cache pointer once (after RunOnExitScripts), and releases m_scriptName whenever m_pc was
  set - also for a SetScriptCopy copy or code passed to SetScript, which took no use of the cached code: if that
  script is cached, a use someone else took is given back (quirk, kept). The return-address / loop blocks are
  freed with FreeMalloc when not inline.
- RunOnExitScripts: this level's (cleared before the call), then each return address' from the top down; the depth
  is read once, the frame array pointer each time.
- CopyParamsIfOwned / Restart / SetScript call CStruct methods on a possibly-null new structure (pool full) - no
  checks, as in the original.
- SetScript with no such script (no code given and none cached): the copy is freed, ClearScript (again), then
  ReportStopped; m_functionParams / m_params / name / object set before that are freed / cleared again.
- SetSpawnerFile: no check that the bottom-level script is still defined (FindSymbol(...)->unknown8).
- ObjectDestroyed: m_waitType = 0 whenever object != null - even if the script didn't refer to it.

## Scheduling semantics (for the QuickJS port)
- Stopping a script (ClearScript - also the first thing SetScript and Restart do, and Destroy) runs its pending
  OnExitRun scripts first, each as an Interrupt (it runs at once, nested, before anything is freed; the stopping
  script's state is still all there and the on-exit script is pushed on top of it). Order: the current level's,
  then the callers' from the innermost out. Each is cleared before it runs, so it runs once. If one waits, it is
  cut off when ClearScript then frees the stack (nothing resumes it).
- SetScript runs no statement: it parses the script's first line as its default parameters, then adds the given
  parameters over them (AppendStructureIfAny - later wins). The first statement runs at the next Update.
  Parameters taken from the script's own (or its current statement's) parameters are copied first, since
  ClearScript frees them.
- A script that doesn't exist: the script ends at once (no code), the event handler table hears 0x8B713E0E
  ("script stopped") and the tracker forgets it - the same report as being killed.
- Restart = SetScript of the bottom level's script, parameters and object (what it was originally started with):
  the call stack is dropped (with its on-exit scripts run).
- An object going away (ObjectDestroyed) clears it from every level and ends the current wait whatever it was
  (the script carries on at its next Update).

## Test (src/hook/selftests_csA.cpp, TestCScriptCore, Early)
Original vs ours on a stand-in memory manager (separate script and default heaps), CScript pools (pool 1 current,
then no pool -> the heap, then pool 0), a stand-in symbol table, stand-in objects and stored randoms; recording
stand-ins for the cache (GetScript / ReleaseScript), Interrupt (records the script's pc / code copy / depth /
on-exit field at the call), the event handler table, the tracker, CRefCounted's constructor / destructor. Covers:
four scripts made (pools / heap), CopyParamsIfOwned (null, none, not owned, owned / inside function params /
inside params), SetScript (from the cache, owned parameters, new object, both overloads, missing script with and
without table / tracker flag, code given), SetScriptCopy (code ending with a line number), a 3-level call stack and
10 loops on the script heap with on-exit scripts at several levels, AllScriptsStillDefined (8 cases),
GotScript, SetSpawnerFile (both kinds of spawner), ClearOnExitScripts, the accessors, ObjectDestroyed (null, a
level's object, its own), FindScriptWithUniqueId, Restart (with and without a call stack), RunOnExitScripts,
ReportStopped, ClearScript, Destroy (middle / head / tail / last; busy and idle tables; callback parameters;
tracker flag; stored randoms), delete (current pool, other pool with the current missing and present, heap).
The trace (every result, every stood-in call, the whole state of every live script and the objects' smart
pointer lists after each call) and the whole arena are compared.

Mutations: 63 deliberate bugs; 55 caught at once, 7 more after strengthening (a state snapshot after every call,
a script with code but no name for GotScript, waits set before Restart / ClearScript, an object on the bottom frame
before Restart); 3 of the caught ones crash inside TestCScriptCore (heap/pool mix-ups). One equivalent:
`destroy_no_release` (Destroy's final m_object.Release - ClearScript, which Destroy calls first, has already
released it). Not tested: ClearScript reading the cache pointer once (nothing changes it in between).

Checks: thug2_check 0 MISMATCHED, 99 early tests pass; register scan: no new RISK, the value-returning
functions read by callers (new, GotScript AL, GetOnExceptionScript, FindScriptWithUniqueId, Construct) all
return values; call audit: only `? 0x00463870` (CRefCounted's destructor tail-jumps to 0x00463820, a plain
RET - fine); check_code_writes: no overlaps; find_gaps: only other groups' functions. (tools/check_partymod_overlap.py
isn't in the repo any more; PartyMod's patch sites 0x004E26A0 / 0x005480A1 are far from this range.)

## For the merger
- Group C's Interrupt (0x00475750) is called through GameFunction twice in RunOnExitScripts - turn into
  `Interrupt(name, nullptr)`. TestCScriptCore stands in for Interrupt at its game address only: once it's a direct
  call, add `AddressOf(&CScript::Interrupt)` to that stand-in (like TestUnloadQB does for ours).
- Same for the stand-ins of 0x0045FA60 / 0x0045FE60 / 0x0045FB70 / 0x00462B90 / 0x004637B0 / 0x00463870 if any of
  those become ours.
- Other groups can call SetScript / ClearScript / ReportStopped / Destroy / Construct / operator new / delete
  directly after the merge. A CScript is made `static_cast<CScript*>(CScript::operator new(sizeof(CScript)))->Construct()`
  (the 0x48B200 pattern: no C++ constructor runs) and freed `script->Destroy(); CScript::operator delete(script);`.
- cscript_core.cpp has a local `SetObject(CSmtPtr&, CObject*)` (release + assign + AddSmartPointer) - CallScript
  (group C) needs the same; worth sharing (e.g. as CSmtPtr::operator=(T*) in smtptr.h) at the merge.
