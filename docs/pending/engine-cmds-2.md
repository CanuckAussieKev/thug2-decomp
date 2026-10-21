# engine-cmds-2: 0x004489B0-0x0044B7E0 (engine script commands, the middle third)

Agent "engine-cmds-2". Branch worktree-agent-a07bbb52849257dad, game workspace C:\Games\THUG2-3.

## The range

81 Ghidra functions, 2 rewritten before this agent (CScript's pooled new / delete 0x00449970 / 0x004499B0); 79 left:
the maths commands, the free-memory display, .qb loading, the languages, composite objects, nodes, the script
utilities (Change, Wait, the comparisons, FormatText's formatter, structures, Block, event handlers, the warning
level, integer casts) and the array commands.

Shared files touched (small, additive): CMakeLists.txt (new sources and src/hook/selftests_ec2.cpp).

## Batches

- [x] Batch EC2-1 (NOT run in the game yet; maths, DisplayFreeMem, Test): src/game/script/math_cfuncs.{h,cpp},
  src/game/script/debug_cfuncs.{h,cpp} (new).
  - Sin / Cos / Tan 0x00448AE0-0x00448B60 (degrees: the float times pi/180 unrounded into FSIN / FCOS / FPTAN - kept
    as x87 inline asm so the results match bit for bit), ASin / ACos 0x00448BA0 / 0x00448BE0 (the C runtime's asin /
    acos, times 180/pi), ATan 0x00448C20 (FPATAN with 1), Sqrt 0x00448C60; results go to the calling script's
    parameters as sin / cos / tan / asin / acos / atan / sqrt.
  - NormalizeVector 0x00448CA0 (norm = the vector times 1/length - all four components, w too; length summed from z
    down in double, rounded to a float; a zero vector left alone), LinearMap 0x00448D70 (result=<name>: the
    fraction (value - lowerbound) / (upperbound - lowerbound) stays unrounded, clamped to 0..1, mapped onto From..To),
    Clamp 0x00448E60 (max first, then min: min wins when min > max; NaN passes through), RotateVector 0x00448EF0 (the
    identity times Rx, Ry, Rz in front - D3DXMatrixMultiply(&m, R, &m) - then the point (w = 1) through it).
  - DisplayFreeMem 0x004489B0: for each heap, UpdateDisplayFreeMemory id=<CRC of the heap's name>
    free_mem=<its region's MemAvailable> min_free_mem=<the region's least>; one pooled structure, cleared each time.
    Test 0x00449110: prints the bytes of a hand-made symbol table entry (three times, flags 1 and 2 set in
    between; flag 4 then cleared, unprinted) and of a component.
  - Checksum names found from the name entries in the game's .qb files; LinearMap's input (0x08296457)
    stays unnamed.
  - Test TestMathCFuncs (src/hook/selftests_ec2.cpp): every command over several inputs (out-of-range, NaN, zero
    vectors, reversed / empty LinearMap ranges, cancelling LinearMap ranges, Clamp's NaN cases, each rotation alone
    and all three), DisplayFreeMem over two heaps, with the pool giving a structure and empty; the structure changes,
    heap iteration, pool, RunScript and Printf recorded; _CIasin / _CIacos answered by std::asin / std::acos in both
    runs. 37 deliberate bugs: 36 caught (2 after strengthening: LinearMap's upper clamp and its unrounded fraction; the
    rotation-order bug moved from the X rotation, where it is equivalent - the identity commutes - to the Y one);
    1 equivalent (RotateVector's w: rotations have no translation row).
- [x] Batch EC2-2 (NOT run in the game yet; nodes, composite objects, .qb files, languages, proximity, ResetEngine):
  src/game/obj/object_cfuncs.{h,cpp} (new), src/game/script/debug_cfuncs.{h,cpp}.
  - Nodes: AppendNodeParams 0x00449280 (params={...} appended to the node's own structure - GetNode asserting),
    GetNode 0x00449850 (node = the node's structure; false if none), GetNodeName 0x00449890 (nodename = the id of
    the script's object; false with no object or id 0), GetTriggerNodeName 0x004498C0 (nodename = the script's
    m_node; false if 0).
  - The COIM heap: InitCOIM 0x004492E0 (Obj::InitCoim(0xBB9A31B5, size, 0xB0847D54, the float 0x03D6DE47 squared
    and truncated - the square unrounded), ResizeCOIMHeap 0x00449380 (newsize).
  - Composite objects: CreateCompositeObject 0x00449540 (Components=[...], params={...} optional; finalized),
    GetUniqueCompositeObjectID 0x004495A0 (UniqueID = the first free id from PreferredId, default 8 - the manager
    re-read each step), GetObjScreenCoords 0x00449610 (the first viewport's camera 0x004A06D0 projects the object's
    position, 0x004A1090; screen_x / screen_y; false with no object), RunScriptOnComponentType 0x004496B0 (vtable
    slot 12 - CallMemberFunction by name - on every component of the type), TransformLocalToWorld 0x00449740
    (world_offset = local_offset through the object's display matrix +0xC0; no null check),
    DestroyParticlesByGroupID 0x004497D0 (the renderer's particle manager 0x006FA7C0, if any, destroys the group
    - 0x0049ADE0 - and every live object whose particle component's system (+0x1C, its +0x0C) has the group is
    marked dead, vtable slot 6).
  - LoadQB 0x004493B0 / UnloadQB 0x00449450 (the LevelSpecific table 0x00696900, 15 checksums: noted in the first
    free slot - all taken: not noted, loaded anyway; unloading clears every matching slot), ScreenShot 0x004494A0
    (FileName, default "screen"; 0x0048C100), ToggleMetrics 0x004494E0 (0x0048C0F0 toggles the byte 0x006FA7D6),
    German / French / Spanish / Italian / English 0x004494F0-0x00449530 (the language 0x007CEFA0 is 5 / 3 / 4 / 6 /
    2), ProximCleanup 0x004498F0 (sound components' +0xBC and stream components' +0x28 cleared, then
    Proxim_Cleanup), Inside 0x00449950 (the proximity manager's m_entering - no null check), ResetEngine
    0x00449960 (Mlp::Manager::StopLoop).
  - Test TestObjectCFuncs: every command and branch (missing objects / nodes, dead objects, the other group, no
    particle manager, the .qb table free / full / two matching slots, all five languages over eight values, the
    proximity flag both ways), with the object and component lists, the projection (a fastcall stand-in leaving its
    result in ST0), the structure changes and the file / screenshot calls recorded; the components, objects and
    main loop manager compared after. 43 deliberate bugs, all caught (one InitCOIM input changed so that rounding
    instead of truncating shows).
  - The verify build is configured with CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded (/Z7) in this worktree: the
    shared compile PDB kept getting corrupted during mutation runs (C1033 / C2471).
- [x] Batch EC2-3 (NOT run in the game yet; RunScriptOnObject, Orient, Change, SetEventHandlerOnObject, Wait, the
  comparisons, SetScriptString, GlobalExists): src/game/obj/object_cfuncs.{h,cpp}, src/game/script/script_cfuncs.{h,cpp}
  (new).
  - RunScriptOnObject 0x00449A10: UniqueID removed from the caller's parameters; the object (Obj::FindObject, the
    tracker) gets its own script's object set if it has none (0x0041D1D0 - CSmtPtr::Set) and runs the script as an
    interrupt (CObject::CallScript); false with no object.
  - Orient 0x00449AC0: angles (Angles, default (0,0,0,1); `degrees` scales all four) to the object's m_matrix
    (0x0041A730), offset as its position row; with bone, times the bone's matrix from RelativeTo's skeleton
    component (0x005480F0 its skeleton, 0x004A36E0 the bone's matrix; no RelativeTo: crashes, as in the original);
    with RelativeTo, times that object's display matrix with its position; then m_pos = the position row, the row
    reset, the display matrix = the matrix. No check that the object exists.
  - SetEventHandlerOnObject 0x00449F80: ex / scr / Group (default "default") / Exception / params; the object's
    script made from the script pool (CScript::operator new + Construct) if it has none; CScript::SetEventHandler.
  - Change 0x00449D00: <name>=<value> (the first component) or globalname(guess)=<name> newValue=<value>; scripts and C
    / member functions refused; the global removed (CleanUpAndRemoveSymbol) and made again (its +0x08 kept); the
    value copied into a fresh component (its flag bit kept); a name with `resolve` takes the global it names (type and
    value); integers, floats and names stored, structures copied into a new pooled structure, arrays into a new
    array (CopyArray); any other type (strings, vectors, pairs...) leaves the global made again but EMPTY - kept.
  - Wait 0x0044A0D0: seconds / second (x 1000) and frame / frames (x 16.666666) as ms of frame time (WaitTime) or,
    with the flag 0xC847F2B9 (name unknown), the game clock (WaitRealTime); gameframe / gameframes / game: Updates
    (Wait); one_per_frame: WaitOnePerFrame; a bare number: ms. Each truncated (_ftol).
  - IntegerEquals / ChecksumEquals 0x0044A200 / 0x0044A260 (A, B; missing 0), StringEquals 0x0044A2C0 (by checksum:
    case-insensitive), SetScriptString 0x0044A320 (index 0, string ""), GlobalExists 0x0044A380 (name; type names
    Structure / struct, checksum / name, float, array, string, vector, Integer / int, pair; none: type 0, never true).
  - Test TestScriptCFuncs: RunScriptOnObject over an object with no script, a script without / with an object, no
    object and no params; Orient (defaults, degrees with an offset, RelativeTo, a bone, RelativeTo missing);
    SetEventHandlerOnObject with every argument and with the script made; Change over every value type, resolve
    (resolved, structure, unresolvable), refused types, a string global, globalname / newValue, nothing; Wait over
    every unit with and without the real-time flag, an integer, a negative; the comparisons both ways and with missing
    arguments; GlobalExists over 8 symbol types x 13 type names. The symbol table, pools, smart pointers, the
    skeleton and the waits recorded; the objects and new symbols / structures / arrays compared after. 43 deliberate
    bugs: 42 caught, 1 equivalent (Orient's w angle: 0x0041A730 reads x, y, z only).
- [x] Batch EC2-4 (NOT run in the game yet; FormatText's worker, EnableScriptPrintf, IfDebugOn, StructureContains,
  ClearStruct, ScriptExists, IsArray, Block, IsTrue, CleanUpAndRemoveSymbol): src/game/script/script_cfuncs.{h,cpp}.
  - FormatText 0x0044A480 (cdecl (results, params) -> int; called from 0x0044BE2D and 0x0044C8F5 in the next third):
    the unnamed format string; "\%" a literal '%'; "%x" (one character) or "%%name" ([A-Za-z0-9_]*) names a
    parameter, found with FindLastComponent (the last one wins), copied into the global value component 0x00696D2C
    (its flag bit kept) and ResolveNameComponent'd; integers "%d" / integer_width "%0*d" / UseCommas
    (Str::FormatWithSeparators), floats "%.<decimalplaces, 3>f", strings and local strings copied, pairs
    "(%.3f,%.3f)", vectors "(%.3f,%.3f,%.3f)", names their text (FindChecksumNameOrUnknown; result 2); other types
    skipped. The text (buffer 0x00696940, unbounded as in the original) goes into textname / ChecksumName (its CRC).
    0 (printed) with no format string or a missing parameter. sprintf -> snprintf with the buffer's size (the same text
    unless the original would have overflowed it).
  - EnableScriptPrintf 0x0044A900 (default 1; the flag 0x0067D6D4), IfDebugOn 0x0044A930 (IfDebugOn and IfDebugOff:
    a compiled-out call with the script - kept as an empty inline - then true), StructureContains 0x0044A940
    (Structure={...} <name>: a component or a flag of that name; false with no structure), ClearStruct 0x0044A9B0
    (struct=<global>: Script::GetStructure(...)->Clear()), ScriptExists 0x0044A9E0 (LookUpSymbol is a script), IsArray
    0x0044AA20, Block 0x0044AA50 (type, else UntilEvent - only if params aren't empty; CScript::Block), IsTrue
    0x0044AAB0 (non-zero integer), CleanUpAndRemoveSymbol 0x0044AAE0 (FindSymbol; false if none).
  - Test TestFormatCFuncs: FormatText over no format, every value type, integer_width, UseCommas, decimal places
    (given and default), one-letter and long names (digits, underscores), the escape and a lone backslash, a
    structure (skipped), a missing parameter, an empty format, a repeated parameter, the value's flag bit set, a name
    that resolves to an integer; the game's sprintf stood in by vsnprintf for the original; the buffer and value
    compared after each call. The other commands over each branch. 31 deliberate bugs, all caught.
- [x] Batch EC2-5 (NOT run in the game yet; event handlers, stopping scripts, the warning level, integer casts,
  arrays, AddParams, RemoveComponent, GetScriptString): src/game/script/script_cfuncs.{h,cpp},
  src/game/script/array_cfuncs.{h,cpp} (new).
  - SetEventHandler 0x0044AB20 (ex, scr, Group - default "default" -, Exception, params), SetExceptionHandler
    0x0044ABD0 (SetExceptionHandler / SetException: the same, always an exception), ResetEventHandlersFromTable
    0x0044AC70 (the global array named, Group), ClearEventHandler 0x0044ACD0, ClearEventHandlerGroup 0x0044AD00
    (default "default"), OnExceptionRun / OnExitRun 0x0044AD30 / 0x0044AD60 (none: 0), PrintEventHandlerTable
    0x0044AD90, StopAllScripts 0x0044ADA0, FirstTimeThisIsCalled 0x0044ADB0 (the flag 0x0067D6D5),
    SetScriptWarningLevel 0x0044ADD0 (the level 0x006F3534: none 0 - the default -, 0xD6454EFA 1, 0x4E10B054 2 -
    "warn" / "assert" are guesses; other names leave it).
  - CastToInteger 0x0044AE30 / StringToInteger 0x0044AEB0 work on the CALLING script's parameter of the given name
    (float truncated / each character - '0', no checks; the string is read after its component was removed - freed
    memory, kept); false without a name or such a parameter.
  - ArrayContains 0x0044AF50 (integer, string - _stricmp against GetLocalString - and name arrays; the type read once,
    the size every pass), GetArraySize 0x0044B030 (Index1 / index2 / index3 lead into element arrays; array_size),
    Get2DArrayData / Get3DArrayData / GetNDArrayData 0x0044B0F0 / 0x0044B300 / 0x0044B530 (arrayName: the array in the
    calling script's parameters, else the global one; ND reads `indices` from the script's parameters too; val added
    by the element array's type: integer, float, string / local string - both through GetLocalString -, pair, vector,
    structure, array, name; other types nothing; an empty indices array runs off the end, as in the original).
  - AddParams 0x0044B720 (AppendStructureIfAny), RemoveComponent 0x0044B740 (name=, else the unnamed name asserted),
    GetScriptString 0x0044B790 (index -> string).
  - Test TestArrayCFuncs: every command and branch; hand-built arrays of every element type, arrays of arrays two and
    three deep, single-element (in place) and empty arrays, the script's own parameters for the casts and array
    lookups, global arrays, a script-typed row (nothing added), the warning level over all names; the game's
    _stricmp answered by ours. 40 deliberate bugs, all caught (3 by crashes in ours).
- After EC2-5 the range 0x004489B0-0x0044B7E0 is done: inspect survey shows every function REPLACED, find_gaps 0 gaps.
