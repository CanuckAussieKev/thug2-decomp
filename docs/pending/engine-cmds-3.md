# Engine script commands 0x0044B7E0-0x0044E6B0 (script utilities, arrays, music & streams, sounds) - engine-cmds-3 agent

Branch worktree-agent-a4915cf1938012168. Tests in src/hook/selftests_ec3.cpp (TestScriptUtilCFuncs ...).
Shared files touched: CMakeLists.txt (new files listed).

- [x] Batch EC3-1 (NOT run in the game yet; the script's own parameters, random values, arrays, goto, checksums and
  text - 16 CFunctions): src/game/script/script_util_cfuncs.h / .cpp (new).
  - 0x0044B7E0 RemoveParameter (the component and the flag), 0x0044B820 RemoveFlag, 0x0044B850 GetRandomValue
    (integer: a..b inclusive, either order; float: a + Rnd(resolution (1000)) * (b - a) / (resolution - 1), x87 in
    double, a and b not swapped), 0x0044B9A0 SizeOf (ArraySize via AddComponentOfType), 0x0044B9E0
    GetNextArrayElement (index read from the script's own parameters; -1 / negative = the first; false at the end;
    element by type, other types print 0x00647094 and are still true), 0x0044BBD0 GetRandomArrayElement (strings
    not supported: the error), 0x0044BD30 GotParam, 0x0044BD60 Goto, 0x0044BDC0 GotoRandomScript (only an array of
    names), 0x0044BE20 FormatText (0x0044A480 - the middle agent's range - via GameFunction), 0x0044BE40
    MangleChecksums (a + b, each a checksum or an asserted integer), 0x0044BEC0 AppendSuffixToChecksum
    (ExtendCRCWithString), 0x0044BF20 GetUpperCaseString (128-byte buffer), 0x0044BFA0 AddFlag, 0x0044BFD0
    AppendSuffix (" %d" - a space before the number; the text cut to the limit (15) less the number's length - with
    a number longer than the limit the cut lands before the result, in the number's buffer: the two buffers kept
    in the original's order so that quirk stays harmless; our first version wrote before its own buffer and the
    /GS check failed), 0x0044C120 IsObjectScript.
  - Original quirks kept and noted: uninitialised locals in MangleChecksums / AppendSuffixToChecksum /
    GetUpperCaseString / AddFlag when a parameter is missing (here 0 / null).
  - Test TestScriptUtilCFuncs: original vs ours over hand-built structures and arrays (every element type, index
    edges, empty / missing arrays, objects or not, AppendSuffix's cut cases); the structure-changing methods,
    SetScript, Rnd, Printf, sprintf and 0x0044A480 stood in for by recording code. 30 of 31 deliberate bugs caught;
    the miss (AppendSuffix's `>` as `>=`) is equivalent (cutting at the text's own end changes nothing).
- [x] Batch EC3-2 (NOT run in the game yet; ForEachIn, array building and changing, printf text, Dump - 11 functions):
  src/game/script/script_util_cfuncs.h / .cpp.
  - 0x0044C290 ForEachIn (with params: a structure per element - the element's components then the params - run
    now on the script's object; without: the element itself as the parameters; no array crashes, as in the
    original), 0x0044C430 GetElement (by the name "Index"; integers, structure copies handed over by pointer
    (AddComponentOfType 11), arrays copied; other types print 0x00647110; true without an array, false out of
    range), 0x0044C5A0 PermuteArray (by name strings "Array" / "PermuteSource" / "NewArrayName" /
    "MakeNewFirstDifferFromOldLast": size * 10 random swaps of a copy or the array itself; a first element equal
    to the old last one swapped with a random other), 0x0044C730 GotoPreserveParams, 0x0044C840 FormatPrintfText
    (helper of ScriptAssert: "%s\n" of the text put together by 0x0044A480 with TextName=PrintfText, "" if that
    fails; "%d\n" of an integer; the third argument unused), 0x0044C9B0 Dump (builds "name=%a" with a copy of the
    parameter as a, or "name=[!! DOES NOT EXIST !!]", and frees it: the printing is compiled out), 0x0044CB70
    ScriptAssert (the message only; the assert is compiled out), 0x0044CBC0 CreateIndexArray (index_array),
    0x0044CCC0 AppendStruct (a copy of params added by pointer to the global structure `struct` as `field`),
    0x0044CDB0 StringToCharArray (char_array of one-character strings), 0x0044CEC0 SetArrayElement (the script's
    array; NewValue resolved if it names a global, copied; the old element's string / pair / vector / structure /
    array freed; float arrays take integers; other types untouched).
  - Test TestScriptArrayCFuncs: original vs ours with the pools, structure / array / component methods, RunScript,
    CopyArray, CreateString and FormatText stood in for (allocations from one arena reset per run, so pointers match),
    every case above incl. a global symbol in the stand-in table. 39 of 40 deliberate bugs caught (3 after
    strengthening: MakeNewFirstDifferFromOldLast on 2-element arrays and with distinct others, the resolved
    global); 1 equivalent (PermuteArray's two Rnd calls swapped: the swap is symmetric).
  - Build note: the verify build's compile PDB (CMakeFiles\thug2_code.dir\vc140.pdb) got corrupted (C2471) after
    ~12 mutation builds; out\scripts\mutbuild.cmd retries after deleting it.
