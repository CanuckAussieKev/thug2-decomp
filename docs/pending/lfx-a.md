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
- [x] Batch A4 (NOT run in the game yet): set-up and saving - src/game/ed/park_io.cpp
  - 0x005960D0 SetUpAbstractMetas (Ed_standard_metapieces: single / multiple / "ack" = made of palette metapieces;
    special_type gap_placement, area_selection, rail_placement, restart_1/multi/horse, king_of_hill, the four
    flags; a single-piece palette entry for every piece used), 0x00596980 WriteCompressedMap (header, floor
    heights, metapieces as Ed_Save_Map index + cell + height + turn, whole gaps 0x30 bytes, CRC), 0x00596CD0
    SetDefaultBounds (ed_default_inner_dim), 0x00596D50 SetUpRoadMask (Ed_Roadmask: quirk kept - read from
    element 4, past the array's end, after four fixed rectangles), 0x00596E50 SetUpLights (Ed_Light_Info),
    0x00596FB0 UpdatePercentBar, 0x005975A0, 0x005975B0 WriteIntoStructure (Park_editor_map, Park_editor_goals
    via the GoalEditor module, rails via RailEditor, MaxPlayers), 0x00597720, 0x005987A0 ClearCompressedMap
    ("Unnamed park"; quirk kept: CRC before the size and gap count are set), 0x00597BD0 AddGap (x87 float
    order kept: x stored as float midway, z not), 0x00597FC0 RemoveGap, and source for 0x00591450
    (PrintMapList, no REPLACE).
  - Test: an "io" group (1000 cases) with stand-in script structures (FakeDesc) answering the CStruct getters,
    real CArray::GetElement on fake arrays, the gap length returned in ST0 by patched bytes. 59 deliberate bugs:
    52 caught (3 after strengthening: separate stand-ins for the gap's halves, flag 0x1000 in the test
    metapieces, a non-matching metapiece in front of the gap's own), 1 didn't build, 3 equivalent (moving the
    gap count after an already-taken CRC; rounding case 1's z to float - always an exact integer there; float
    vs double division of the gap length - differs only on double-rounding cases the random lengths don't hit).
- [x] Park editor types unified (lfx-a + lfx-b + lfx-c): main (with lfx-c) and lfx-b's branch merged into lfx-a;
  one definition of each type in src/game/ed/park.h (lfx-b's layouts, with lfx-a's fields and methods and
  lfx-c's offset accessors added); src/game/park/park_editor.h now only includes it; my old headers
  (ed/park_manager.h, ed/cursor.h, ed/menu.h) are gone, CUpperMenuManager is in ed/piece_menu.h. My sources were
  renamed (ed/park_accessors.cpp, ed/cursor_tools.cpp, ed/park_lists.cpp, ed/upper_menu.cpp, park_io.cpp).
  - Names settled: where lfx-b already called one of my functions through its address, its name was kept
    (GetIntersectingMetas 0x597740, GetAreaMetaPieces 0x597A00, BuildRiserList 0x5984A0, RestoreFloorHeights
    0x5952B0, ClearFloorChangeMarks 0x595D40, IsAreaBlocked 0x595B30, HasFloorLockedMeta 0x5983D0,
    SetMetaInCells 0x598150, ClampToPark 0x5971E0, OnModeChanged 0x5954D0, ForgetGapAt 0x598940,
    RemoveOldestClipboard 0x597190, GetAreaSelectDims). Corrected: 0x5987A0 is ClearCompressedMap (not the piece
    sets), 0x595EE0/0x595FC0 are AddMetaToCells/RemoveMetaFromCells (the cell lists, not the floor), the
    CParkManager +0x351C / compressed map +0x08 value is the shell size (sk5ed%d_shell; GetShellSize 0x5950B0,
    SetShellSize 0x597720) and the map's +0x18 is the theme (the park editor's +0x78). GapInfo keeps lfx-a's
    field names (m_pos, m_rot, m_leftExtent, m_rightExtent, m_text, m_score, m_numCompleteHalves, m_tabIndex,
    m_cancelFlags); GapDescriptor and PieceSet are aliases. lfx-c's GridDims width/height/length became w/h/l,
    its 0x59CD20 is DestroyMetasInAreaAndRebuild (lfx-b's 0x599880 keeps DestroyMetasInArea); the two
    ChangeFloorHeight overloads (0x59C520, 0x59D080) are registered with static_casts.
  - CMapListNode::DestroyList (0x591BE0, source only) is __fastcall (the list in ECX, as the original's thiscall);
    MapListHolder frees only a non-null list (as 0x591C60); GetIntersectingMetas' last argument is a uint32_t
    passed on whole to CMetaPiece::Intersects (the original pushes the whole register).
  - lfx-b's tests: their stand-ins (Rec) now also patch our functions at those addresses (from the replacement
    table, plus DestroyList/Free), since lfx-b's code calls the rewritten functions directly now.
  - Checks: 1595 replacements, 0 MISMATCHED; 190 early self-tests pass; check_code_writes no overlaps;
    audit_game_function_calls no MISMATCH; scan_register_assumptions only the known 0x4EFF92.
- [x] The unified build (commit "Park editor: one set of Ed:: types...") tested in the game 2026-10-18 under the
  lock: Free Skate -> the level loads and plays, pause menu fine; Create-A-Modes -> Create-A-Park opens (piece
  and set menus, memory bar), the cursor moves, a Player 1 Restart is placed (space) and stays when the cursor
  moves away, the raise key runs. Log: 1552 of 1552 replacements installed, early (190) / late / engine-started
  self-tests all pass, no CRT reports or run-time check failures. (scratchpad keys.ps1 got a `space` key.)
  Not exercised in the game: gaps, clipboards, area selection, test play, saving.
