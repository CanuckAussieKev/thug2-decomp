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
