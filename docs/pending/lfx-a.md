# lfx-a: park editor 0x595030-0x598A60 (records for the lead to fold into decomp-progress.md)

The range docs/subsystems.md calls "Lighting / FX / park memory" is the park editor (Create-A-Park, namespace
`Ed`, src/game/ed/): CParkManager (the park: grid, compressed map, piece sets, road mask, gap manager copy),
CCursor, CUpperMenuManager, the clipboards. Checksum names from the .qb name tables (0x2B entries) and the exe's
strings.

- [x] Batch A1 (NOT run in the game yet): CParkManager's accessors - src/game/ed/park_manager.cpp/.h
  - 0x00595030 GetCompressedMapBuffer, 0x00595050 SetCompressedMapBuffer, 0x00595090 IsMapSavedLocally,
    0x005950B0 GetTheme, 0x005950D0 GetParkName, 0x005950F0 SetParkName, 0x00595140 RebuildParkGeometry
    (CParkGenerator 0x00593610 / 0x005947D0 / 0x00594910), 0x00595170 GetShellChecksum ("sk5ed_shell" /
    "sk5ed%d_shell" theme+1), 0x005951E0 GetMapListNode, 0x00595200 sInstance (0x007CE604), 0x00595210
    GetPieceSet, 0x00595250 GetMapInfo (thiscall, ECX unused, RET 0x1C), 0x005952B0 UndoFloorSlide, 0x00595300
    GetSpecialPieceIndex (restart_1, restart_multi, restart_horse, king_of_hill, the four flags), 0x00595620
    GetAbstractMetaPiece, 0x00595990 CanResizeTo (park memory check 0x0059EB30), 0x005959D0 GetFloorHeight,
    0x00595A90 GridCoordinatesToWorld (returns the vector: hidden result pointer, EAX), 0x00595B30
    IntersectsRoadMask, 0x00595D40 ResetSlideMarks.
  - Layout: CParkManager 0x177CC bytes; 58 x 58 grid of map-list buckets (+0x8C) and floor cells (+0x352C,
    16 bytes: height, levels used, slide mark, slide amount); 40 piece sets of 0x154 (+0x1076C); the compressed
    map (+0x13C90, 0x3A98 bytes, version 20004); flags +0x17728 (1 valid, 2 in sync, 4 newer than park,
    8 not saved locally); road mask +0x1776C; special piece names +0x177A4; gap managers +0x177C4/+0x177C8.
  - Found: a free `__thiscall` function pointer returning a struct passes the result address in ECX (MSVC);
    the test calls the original through a member-function pointer instead.
  - Test (selftests_parkedA.cpp, TestParkManagerAccessors): 200 random parks per group, original vs ours on
    the same memory, results + whole park hashed; CParkGenerator calls, 0x0059EB30 and the original's sprintf
    stood in. 33 deliberate bugs caught (2 after strengthening the road-mask cases).
- [x] Batch A2 (NOT run in the game yet): the cursor, the piece menu, the gap manager copy -
  src/game/ed/cursor.cpp/.h, menu.cpp/.h, park_manager.cpp
  - CCursor (0x007CE608, 200 bytes: +0 park manager, +4 cell dims, +0x44 meta, +0x48 area selection state and
    corners, +0x58 clipboard list, +0x5C current clipboard, +0x6C mode, +0x70 GapInfo): 0x00595330 sInstance,
    0x00595340 HideClipboards, 0x00595360 SetGapNameAndScore ("" -> "x"), 0x005953C0 SetGapCancelFlags,
    0x00595430 GetGapName, 0x00595440 ShowClipboard, 0x00595450 SelectClipboard, 0x005954A0 SetVisibility,
    0x005954D0 UpdateHelperText (parked_set_helper_text_mode: gap_adjust / gap_regular / rail_placement /
    regular), 0x00597100 RestartAreaSelection, 0x00597110 GetAreaSelectDims, 0x00597190 DestroyLastClipboard,
    0x005971E0 ForceInBounds (a pasted clipboard's area is treated as signed), 0x005988A0 DestroyClipboards
    (quirk kept: the list head isn't cleared), 0x00598940 ForgetGapOnPiece, 0x005989C0 AdvanceAreaSelection
    (done when at most 11 x 11), 0x005955C0 CClipboard::Free (pools 0x007CE5B4).
  - CUpperMenuManager (0x007CE60C, 0x10 bytes): 0x00595570 sInstance, 0x00595580 Disable, 0x00597400 Create,
    0x00597490 Enable (parked_make_piece_menu / _set_menu / _set_menu_item set_name per used set).
  - CParkManager: 0x00595BC0 CreateGapManagerCopy, 0x00595C70 UseGapManagerCopy, 0x00595C80
    DestroyGapManagerCopy, 0x00595CB0 HighlightGapPieces. +0x177C4 is the gap manager in use, +0x177C8 a copy.
    CPieceSet's name (+4) is a pointer.
  - Test: TestParkManagerAccessors grew two groups (cursor; menu + instances + gap manager), 1000 random cases
    each, every callee (clipboards, gap manager, scripts, CStruct, memory manager, pools) stood in and
    recorded. 46 deliberate bugs: 45 caught (8 after strengthening: rarer modes, cursor at the park's edges,
    selections near 11 x 11, the gap manager constructor making itself current), 1 equivalent (moving the null
    check of SetGapCancelFlags after `cancel = 0`).
- [x] Batches A1+A2 tested in the game (lfx-a's own run, under the lock): LA-area level (skate shop list order)
  loads and plays, pause menu works; Create-A-Park opens (piece menu, set menu, percent bar), the cursor moves,
  the floor raise/lower keys run, the editor's pause menu opens. Log: all self-tests pass, no CRT reports.
- [x] Batch A3 (NOT run in the game yet): the map of metapieces - src/game/ed/park_map.cpp
  - 0x00595670 CreateConcreteMeta, 0x005958B0 HasPiecesOutside, 0x00595DF0 AddMetaToList, 0x00595E80
    RemoveMetaFromList, 0x00595EE0 AddMetaToBuckets, 0x00595FC0 RemoveMetaFromBuckets, 0x00597740
    GetAreaMetaPieces (both forms: by buckets, or by a metapiece's pieces), 0x00597A00 GetMetaPiecesAt,
    0x00597AC0 GetGapInfo, 0x005983D0 IsColumnUnslidable, 0x005984A0 SlideColumn (recursive floor slide),
    0x00598150 ApplyMetaToFloor. Source (no REPLACE, outside the range) for CMapListNode's pool new/delete
    (0x005914B0), DestroyList (0x00591BE0), VerifyIntegrity (0x00590470), CMapListTemp.
  - Metapiece flag 0x4000 = "unslidable" (a piece with flag 0x0CD75A3F, name unknown); 0x100 unnamed.
  - tools/ghidra/audit_game_function_calls.py: GridDims by value counts as 8 bytes.
  - Test: a "map" group (1000 cases): random metapieces in a 16 x 16 patch, buckets, a real node pool,
    metapiece/generator/gap manager callees stood in, the vtable's GetPiece/AddPiece as thunks. 50 deliberate
    bugs: 47 caught (1 after duplicating a metapiece in a bucket), 2 equivalent (risers can't reach the slide's
    metapiece check - precise lists leave them out; `> y` vs `>= y + 1`), 1 caught by a crash.
