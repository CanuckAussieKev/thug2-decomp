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
  - Checksum names found from the game's .qb name entries (out\scripts\names.py); LinearMap's input (0x08296457)
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
