# CScript group B - the interpreter (branch wt/csB) - NOT run in the game yet

Source: `src/game/script/cscript_exec.cpp` (13 functions, all REPLACE). Test: `src/hook/selftests_csB.cpp`
(TestScriptInterpreter, Early). Both added to CMakeLists.txt. **No changes to cscript.h.**

| addr | function | notes |
|---|---|---|
| 0x004750B0 | CScript::Update | the token loop (below) |
| 0x00474B40 | CScript::ExecuteCommand | one statement |
| 0x00475050 | CScript::ExecuteIf | |
| 0x00473C60 | CScript::ExecuteSwitch | "not checked" by thug2_check (its RET walker doesn't find the plain RET at 0x00473E33 behind the jump table; thiscall, no arguments - like ParseExpression) |
| 0x00472A70 | CScript::SkipToEndOfSwitch | recursive |
| 0x00473220 | CScript::ExecuteBegin | |
| 0x00472AB0 | CScript::ExecuteRepeat | |
| 0x00472B50 | CScript::ExecuteBreak | |
| 0x00472950 | CScript::ParseFunctionParams | |
| 0x004729E0 | CScript::ReadName | |
| 0x00472980 | Script::CallMemberFunction (free, cdecl) | |
| 0x004731A0 / 0x004731E0 | CScript::CallMemberFunction(entry / name, object) | |

## For the merger
- Calls into group C go through four wrappers in cscript_exec.cpp's anonymous namespace, each marked
  `// -> CScript::X (group C)`: `UpdateWaitOf` 0x00473050, `CallScriptOf` 0x004748E0, `ReturnFromScriptOf`
  0x00473E70, `InterruptOf` 0x00475750. Replace each wrapper's uses with the direct method call and delete it.
- Other game code still called by address (not CScript): 0x0045DB90 CObject::GetComponent (thiscall),
  0x00460AD0 FindObject (cdecl, the tracker), 0x0044E4F0 on GameNet::Manager (result unused), the objects' /
  components' virtual member-function calls (CObject vtable +0x08 entry, +0x0C name; CBaseComponent +0x2C -
  its whole EAX is tested, kept).
- The test calls group A / C by address (Construct 0x004738E0, SetScript 0x00474540, Wait 0x00472BD0, Block
  0x00472FD0, ClearScript 0x00473A80, Interrupt 0x00475750): fine before and after the merge (offline the
  address is the original; hooked, ours). In our run it writes jumps from the game addresses of the 13 group B
  functions to ours (so the game's Interrupt runs our Update, as when hooked); after the merge the C functions
  could be added to that list (`OurFunctions()`), and to the original run's OriginalCode guards.
- The test stands in for `CScriptCache::GetScript` / `ReleaseScript` at both the game address and ours (our
  ExecuteCommand calls the C++ method directly), and for Dbg::Printf / FindChecksumNameOrUnknown likewise.
- The half-pointer normalisation of the arena image uses `high < end >> 16` (the template's `<=` takes the
  64 KB after the arena too - statics right after it, here the fake objects, showed as false differences).

## The interpreter (scheduling spec; also at the top of cscript_exec.cpp)
- **Update** clears SCRIPT_RUN_WAIT_UPDATED (m_flags bit 1), then loops while m_pc: current script = this;
  UpdateWait unless bit 1 is set; a wait still on: set bit 1, CLEAR bit 0 (the "level is an interrupt" flag),
  current = null, return BLOCKED (types 3 and 4) / WAITING (others). Else one token / statement. m_pc null:
  clear bit 0, current = null, FINISHED. '$' / return whose ReturnFromScript says "interrupt": return 5 at once
  (bit 0 and the current script left as they are).
- Bit 1 is only set by an Update returning on a wait, so inside the loop it is clear - unless a statement ran a
  nested Update on the same script (Interrupt: OnExitRun at '$' / return, or a C function calling Interrupt)
  that returned waiting. Then the outer loop doesn't count that wait again; it sees it and returns too
  (tested: an interrupt script that waits - the outer Update returns WAITING, the interrupt goes on at the next
  Update, ends "not interrupted" (bit 0 was cleared) and the interrupted script carries on in that Update).
- A wait set by a statement is checked at once in the same Update (Wait n -> this Update returns with n-1 left).
- Token dispatch (table 0x00475264 / 0x00475298, on token-1): 01, 28 (endif), 3D (endswitch) -> 1 byte;
  02 -> 5 bytes; 20/21/22 loops; 24 '$'; 29 return; 2E, 2F, 37, 40, 41, 49 -> ResolveRandomJumps; 3C switch;
  47 if; 48 else (jump to the endif); 25, 26, 3E, 3F -> nothing: the original loops for ever (kept; never at a
  statement start in compiled code); everything else (00, 03-1F, 23, 27, 2A-2D, 30-36, 38-3B, 42-46, > 49) ->
  ExecuteCommand.
- '$': this level's OnExitRun (cleared first) runs as an interrupt, then ReturnFromScript(null). return: the same,
  then `++m_pc` and the arguments parsed FROM WHEREVER m_pc IS NOW - after an on-exit script that waited, that is
  the on-exit script's next line (tested: its `CF_REC` line became return's arguments).
- **Statements** (ExecuteCommand): 44 name / 46 entry -> the script's object (none: the GameNet call, false);
  43 -> the C function; `(expr)`: a name result is the name to run (0: a name read after it), any other result
  is the statement's value (`raw != 0`), nothing run; then a name (16, `<name>` = the parameter's checksum,
  `<...>` = the unnamed checksum parameter; else 0, not moved):
  - `name:` (token 42): FindObject(name). None: true, m_pc NOT moved - the next pass runs the ':' itself: ReadName
    gives 0 and FindObject(0) (0x00462120, the tracker's hash lookup of id 0 - presumably the first skater) is
    tried; with no object 0 the script loops for ever. Found: ResolveRandomJumps after the ':'; 44 name -> the
    object's member function; else a name: a script (CallScript on that object, GetScript of the SYMBOL's name
    - an alias is followed for the code but the call keeps the written name), a member function (entry or by
    name), anything else false (a C function isn't run). A missing name is false in the script 0xFB11E6CD and
    read through a null symbol (a crash) anywhere else - kept, not tested.
  - `name = value` (token 07): the name is re-read from the 4 bytes before the '=' (so `<p> = 1` sets p itself),
    ResolveRandomJumps, an expression or one value; a typed result is added to m_params (replacing), an empty one
    (e.g. a missing `<param>`) is dropped. True.
  - otherwise a call: the arguments, then the symbol: missing -> "WARNING: script %s not found, ignoring in
    default level.\n" (not in 0xFB11E6CD), false; a script -> CallScript (true); a C function -> its result;
    a member function on m_object (none: GameNet call, false); anything else false.
- **if**: 47 <offset> [39 not] condition-statement: false (after negation) and the script still running -> jump to
  the else body / endif. A script call as the condition is true and runs first.
- **switch**: the value = the first component of the switch line (copied). Cases `3E 49 <next> value-line`:
  equal (ComponentsEqual) -> skip following labels `49 xx xx 3E 49 xx xx value` and run the body (its final 49
  jumps to the 3D); else jump to <next>. 3F default: +4 and run. 3D: +1, done. Anything else met while scanning is
  skipped as a token, a nested 3C ... 3D whole. '$' while scanning: endless (kept).
- **Loops**: one stack for all call levels (CallScript doesn't touch m_currentLoop). begin: the second nested
  loop moves the stack from the inline loop to 10 on the script heap (also when the inline loop belongs to a
  calling level - saved loop pointers rebased; tested). repeat: count read the first time only (mpEnd set past
  it; m_pc stays after the 21); no count: endless (until break); `repeat 0` or negative goes round 2^32 times.
  break: past the matching repeat (nesting counted), its arguments parsed with NO script parameters and
  dropped, the loop popped.

## Test (TestScriptInterpreter)
Original vs ours on the same stand-in state; 16 scripts (plus 8 called/interrupt scripts) run with up to 24
Updates each; every Update's result and the script's state (pc, wait, flags, depth, return addresses, loop
pointers, current script, on-exit, object, both parameter structures) traced; C functions and member
functions record their arguments; the stand-ins record GetScript/ReleaseScript/FindObject/GameNet/Printf/
checksum-name calls; then direct calls of the three CallMemberFunctions (null object -> m_object / none), and
the whole arena. Covers every token Update dispatches except the endless ones (25 26 3E 3F at a statement start,
'$' in a switch scan, obj: with no object 0, missing obj: name outside 0xFB11E6CD - crash).

Mutations (tools/mutate.py, 60 deliberate bugs - one per branch, constant, order, argument): 49 caught at
first, 10 more after strengthening (if conditions that are an expression of 5, `(0) name`, an assignment,
`obj:` with a C function / a missing name in 0xFB11E6CD; a C function that stops the script read its result
before ClearScript freed it; an endswitch token in a case jumped over; a switch on an array; a script ending
right after a break; an interrupt of a codeless script ending without ClearScript). 1 equivalent: ExecuteRepeat
also storing mpEnd into m_pc (m_pc is overwritten on every path after). Some are caught as crashes / hangs
(e.g. GetScript of the written alias name -> no code -> CallScript reads null; skipping 0x02 by 1 byte).
Also covered: a test C function stands in for "a script stopped in a statement" (ExecuteIf leaves m_pc null).

Checks: thug2_check 0 MISMATCHED, all early tests pass; register scan: only the known 0x4EFF92 RISK
(Update listed as returning EAX - it does); audit_game_function_calls: no MISMATCH.
