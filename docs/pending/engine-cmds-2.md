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
