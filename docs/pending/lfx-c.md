# lfx-c: 0x0059C900-0x0059F610 (Lighting / FX / park memory, third part)

Records for the lead to fold into docs/decomp-progress.md.

- [x] Batch lfx-c 1 (NOT run in the game yet): the skate game's script commands at the end of the range and two
  helpers - src/game/script/skate_cfuncs.cpp, src/game/core/dump_text.cpp (24 functions, all REPLACE).
  - CFunc_ControllerDebounce 0x0059EDF0 (button not "pressed" for `time` s: per-button times at 0x007CE758,
    indexed like the button table 0x0067D6D8 via 0x0044E5A0; x87 unsigned clock + time*1000 truncated through
    64 bits), ControllerPressed 0x0059EE80 (the helper of CFunc_ControllerPressed 0x0059F9A0: the controller's
    input state at +0x18, one byte per button - right +4, left +5, up +6, down +7, triangle +8, circle +9,
    x +0xA, square +0xB, L1 +0xC, R1 +0xD, L2 +0xE, R2 +0xF, black +0x16, white +0x17, z +0x18; diagonals
    need both; debounced while the time is later than now (unsigned, strict)).
  - FakeLights 0x0059F0F0 (id= changes one light, 0x004901D0; prefix= lists nodes 0x0045BF90 with a 16-bit
    count and sets lights up 0x0048FE40; flag 0x80804991 = not a prefix), UpdateFxLighting, NodeExists,
    LoadNodeArray (kills the session's spawned scripts, notes the name at 0x007CE6D8 unbounded, loads the .qb),
    ResetCamera ("TODO: ScriptResetCamera not implemented" per viewport with a camera), Set{Movement,Rotate}Velocity
    (viewer speeds, defaults 400 / 120), ClearViewerObject, CasArtist (global cas_artist), SpawnScript (all the
    options; NotSessionSpecific is an integer; at the caller's node; notes the caller's .qb file unless flag
    0x01917DF2 - and, as in the original, calls SetSpawnerFile on a null result), UnBlockInterruptedScript,
    KillSpawnedScript (Name= first, else Id=), Pause/UnPauseSpawnedScripts, DisplayLoadingScreen (default
    "images\xxxxx", freeze, blank, a float = the bar's time), HideLoadingScreen, LastBroadcastedOmnigonWas /
    OmnigonAllowed (checksum 0xAE94C183 vs 0x007CEBC8), SetLevelName (sprintf with the name as the format -
    kept), DumpScripts (compiled out).
  - DumpText 0x0059ED60: the pool-usage dump's (0x00448330) line buffer 0x007CE7B8-0x007CEBA8, '\n' -> CR LF,
    flushed with File::Write to the handle at 0x007CEBB8 - which nothing ever sets (a dead log in this build).
    The fit check is made before the CR LF expansion and skipped for an empty buffer (both can overrun - kept).
  - Test: TestSkateCFuncs (src/hook/selftests_lfxc.cpp, Early): every command against the original on
    hand-built structures, every game function they call stood in for by recording code (StandIns), all 20
    buttons over 24 input patterns with debounced entries, the dump buffer through two flushes and an overrun.
    33 deliberate bugs caught, 3 of them after strengthening (the debounce table recorded before ControllerPressed
    overwrote it).
  - Unknown checksum names: 0x80804991 (FakeLights), 0x01917DF2 (SpawnScript), 0xAE94C183 (Omnigon),
    0x2AB71EEF (DumpScripts).
  - Tested in the game 2026-10-18 (lfx-c's own run, under the game lock): starts, Free Skate -> Los Angeles
    loads and plays, the pause menu works; log clean (all self-tests passed, no CRT reports / RTC failures).
- [x] Batches lfx-c 1-4 tested in the game 2026-10-18 (lfx-c's own runs under the game lock, the build of
  commit "lfx-c batch 4"): Free Skate -> Los Angeles loads and plays, pause menu fine; Create-A-Modes ->
  Create-A-Park loads the park editor (the piece sets RESTARTS / TEAM FLAGS / TOOLS / CLIPBOARD / TABLES-BENCHES
  built by Initialize, the memory bar drawn), the cursor moves and a restart point is placed; no crash; log clean
  (932 of 932 replacements installed, all early / late / engine-started self-tests passed, no CRT reports or
  run-time check failures). Not exercised: gaps, clipboards, raising / lowering, area selection.
- [x] Batch lfx-c 2: the park editor's memory budget - src/game/park/park_memory.cpp
  (17 functions, all REPLACE): GetParkHeap 0x0059E100 (0x007CE688), GetMainHeap 0x0059E180 (the bottom-up
  heap), ClearStats 0x0059E110, SetCountsDirty 0x0059E170, RemoveDma / AddPieceDma / RemovePieceDma
  0x0059E130 / 0x0059E190 / 0x0059E1E0 (a piece's DMA pieces from the global structure 0x9547DA8D by type, 1 if
  missing; piece flag 0x100 = don't count, 0x4 = counted), the pools of CMapListNode / CClipboardEntry /
  CClipboard (0x0059E210 / 290 / 310: PoolPairs at 0x007CE5F0 / 0x007CE5C4 / 0x007CE5B4, the pair's second word
  = per-slot "made here" bytes), Marker 0x0059E390 (memory markers: OUTER SHELL MEM / NETANIMS MEM debug output),
  ShutDown 0x0059E4E0, GetMemoryValue 0x0059E5C0 (runs script 0x3CB03619 once with the name's checksum and an
  index, reads 0x083FDB95), GetMemoryStats 0x0059E730 (the editor's memory bars: main / top-down / park heap,
  component / vector / array pools, DMA pieces; x87: free amounts rounded to float before dividing, max taken
  before rounding for the heaps and DMA, after for the pools; returns `out` - 2 callers read EAX),
  MainHeapHasRoom 0x0059EAD0 (< 95% after count*size*40000 bytes), PiecesFit 0x0059EB30 (DMA, park and main heap
  sizes per floor piece, < 90%), Init 0x0059EC00 (the "Park Editor" heap in its own AllocRegion of
  park_heap_size - top-down heap for level 0xCD3682C1, else bottom-up - and the three pools; "pool is at %p").
  - Test: TestParkMemory (Early): GetMemoryValue with its structures / script / result stood in; everything else
    with GetMemoryValue, Region::MemAvailable, Heap::LargestFree, the manager's Allocate/Free/Push/Pop/AddHeap/
    DeleteHeap and the park manager's calls stood in; 14 scenarios crossing every comparison of GetMemoryStats
    (fragmentation at exactly the pad, a free amount a float can't hold, a little free), PiecesFit at its exact
    limits, the pools / Init / ShutDown for both levels. 42 deliberate bugs caught (7 after strengthening);
    1 equivalent (fragmentation `<` vs `<=`: the other two conditions exclude equality).
  - tools/ghidra/audit_game_function_calls.py: Ed::GridDims (6 bytes) counts 8 on the stack.
- [x] Batch lfx-c 3 (NOT run in the game yet): the park editor's manager and cursor, first part -
  src/game/park/park_editor.cpp (8 functions, all REPLACE). Ed::GridDims (x, signed y, z, width, height,
  length; 6 bytes, 8 on the stack), the grid of 58 x 58 16-byte cells at manager +0x352C (+0 the floor height),
  the piece sets (0x154 bytes from +0x1076C: name checksum, name, 40 {type, text name}, count, selected, visible,
  clipboard set).
  - CCursor::Destroy 0x0059C900 (static: drops its piece, ~CCursor 0x005988A0, freed), ChangePieceInSet
    0x0059C950 (wraps; clipboard sets select a clipboard), ChangeSet 0x0059C9B0 (the next visible set; the
    menu number = visible sets before it; a clipboard set walks to its clipboard, drops the piece, mode 5).
  - CParkManager::RebuildPark 0x0059CB10 (new park 0x1A4E0EF9 or not loaded: empty grid, "unnamed park";
    else from the saved map; flag 0x0067F398 off around the floor's rebuild), RebuildArea 0x0059CC70 (with
    checkMemory: false when the park editor singleton 0x00590160 says it's full), DestroyMetasInArea 0x0059CD20
    (not the floor's, optionally not restart points / only wholly inside / not locked; the scene updated over
    the area: corner + size floats), ClearArea 0x0059CEC0 (flattens: pieces moved down by their height, the
    floor set to 0 under them and in the area), ChangeFloorHeight 0x0059D080 (undone if out of memory).
  - Test: TestParkEditor (src/hook/selftests_lfxc2.cpp, Early): 12 variants of manager / cursor / pieces /
    clipboards, every call stood in for. 39 deliberate bugs caught (none needed strengthening); 1 equivalent
    (the float sum of the corner and size: rounding through double then float is the same as float addition).
  - (A mutation run once showed "SELFTEST FAIL: PC async handle (synchronous I/O)" - not lfx-c's test; it
    passed on rerun: probably timing-dependent.)
- [x] Batch lfx-c 4 (NOT run in the game yet): the park editor's cursor and the manager's set-up -
  src/game/park/park_cursor.cpp (11 functions, all REPLACE). The gap descriptor (0x54 bytes at cursor +0x70:
  two halves' cells, turns, left / right extents, name, score, halves done, id; the name counter at +0xC4).
  - CCursor::RemoveClipboardsTooBig 0x0059D120 (clipboards wider or longer than the park are unlinked,
    ~CClipboard 0x0058C980, freed through CClipboard's pools), PlacePiece 0x0059D240 (fits, height <= 17, a
    restart point needs a free slot of its type - "No free slots exist for this type of restart point"; a copy of
    the type turned and added with the bottom-up heap as default), DestroyUnderCursor 0x0059D380, AddGapHalf
    0x0059D410 ("gap", "gap2"...; prints the area with 0x00590210), RemoveGapHalf 0x0059D600 (pieces with flag
    0x1000 looked at again), ChangeGap 0x0059D6B0 (turn resets the extents; extents 0-6 kept inside the park,
    by the turn: 0/2 along z, 1/3 along x), DestroySelection / FlattenSelection 0x0059D8E0 / 0x0059D980,
    RaiseArea 0x0059D9F0 (at most 10 x 10), RaiseLower 0x0059DAA0 (the area widened by the pieces standing on it
    - any height: the query's height is 16 -, the highest setting the top; not above 17 or below -16; the
    original writes FloorHeight's level flag over its copy of the park's size).
  - CParkManager::Initialize 0x0059DCD0 (once: Init, the park's map object 0xA8 bytes 0x0058CCC0 - in the
    top-down heap for level 0xCD3682C1 -, generator set-up, RebuildPark(0), and the piece sets: each set's
    pieces run from its `first` in Ed_standard_metapieces to the next set's (processed last to first); a
    clipboard set gets Clipboard0-5; hidden sets aren't visible). As in the original, a set whose pieces
    aren't all in the manager's list reads a null piece.
  - Test: TestParkCursor (src/hook/selftests_lfxc3.cpp, Early): 14 variants (and 36 gap changes per variant),
    the clipboard pools, hand-built Ed_piece_sets / Ed_standard_metapieces structures, an empty symbol table;
    every call stood in for (cdecl stand-ins don't record ECX; pointers to locals are recorded as one marker).
    59 deliberate bugs caught, 10 after strengthening (exact height / DMA-slot / -16 limits, a failed AddGap,
    gaps reaching the park's edge along x, the cursor's +0x55 flag set beforehand, dirty piece sets).
  - Pitfall found: a crash inside an original with its own SEH frame (here Initialize, reading a flag through a
    null symbol table) kills thug2_check outright (the game's frame handler runs first, without its CRT) instead
    of being logged as a SELFTEST CRASH.
