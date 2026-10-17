# lfx-b: park editor 0x598A60-0x59C900 (records for the lead to fold into decomp-progress.md)

The range is the park editor (Create-A-Park), not lighting: `Ed::CParkManager` (the park: metapieces on a
58 x 58 grid, floor heights, piece sets, the compressed map that's saved; instance 0x007CE604, 0x177CC bytes),
`Ed::CCursor` (0x007CE608, 200 bytes) and the piece menu (0x007CE60C). Names come from the strings and the
checksums' names in the game's .qb files (a 0x2B-entry table built from all 470 .qb: "parked_make_piece_menu",
"riser_floor", "Sk4Ed_Fence_20x20", "gap_adjust"...). Source: src/game/ed/ (park.h types and the calls into
un-rewritten park code; cursor.cpp, park_manager.cpp, ...). Tests: src/hook/selftests_park.cpp - every function
runs on a stand-in park (manager, cursor, metapieces, node lists, clipboards) twice, original then ours, with every
game function it calls stood in for by recording code with scripted results (standins.h), and the calls, results
and memory are compared. tools/ghidra/audit_game_function_calls.py now knows GridDims (6 bytes, passed in 8).

- [x] Batch lfx-b 1 (NOT run in the game yet): the cursor and three park manager functions (16 REPLACE).
  - CCursor: 0x598A60 HighlightIntersectingMetas, 0x598B40 IsOverGap, 0x598B60 Move, 0x59A1F0 Update (stick,
    turning, the slide to the target, the piece / paste shown), 0x59A5A0 ClearAreaSelection, 0x59A610
    IsAreaSelectionInvalid, 0x59A6D0 CopyAreaToClipboard, 0x59A8D0 UpdateAreaSelectionHighlight, 0x59BAC0
    DestroyCursorMeta, 0x59BAF0 SwitchToClipboard, 0x59BC00 ChangePiece, 0x59C7A0 Construct, 0x59C860
    CreateInstance.
  - CParkManager: 0x599880 DestroyMetasInArea (returns metas << 16 | pieces), 0x599960 HighlightMetasWithinArea,
    0x59AE90 DestroyConcreteMeta.
  - Findings: cursor modes 1 Regular, 2 gap_regular, 3 gap_adjust, 4 area select (copy), 5 paste, 6
    rail_placement (from parked_set_helper_text_mode and the piece flags 0x800 / 0x8000 / 0x10000). GridDims
    (x, y signed, z, w, h, l) is passed by value in two dwords (the high half of the second is stale stack).
    0x597AC0 (the gap lookup) shrinks the box it's given to 1x1x1 - IsOverGap passes the cursor's own box, so
    the cursor's w/h/l become 1 (kept). The original's x87 sums are kept at double precision past the float
    stores (the turning angle, the slide). Original quirks kept: SwitchToClipboard writes through a null set
    when there's no clipboard set; ChangePiece reads a null metapiece for an unknown checksum; CopyAreaToClipboard
    asks the global manager (not the cursor's) for the world box.
  - Test: TestParkCursor (13 functions, ~90 cases) and TestParkManagerMetas; 58 deliberate bugs caught, 4 after
    strengthening (turn to exactly 0, the rotation counter from 0, the angle step overshooting by under 1, an
    area-select state of 2). Register scan: only the known 0x4EFF92 alarm; IsOverGap / CopyAreaToClipboard
    read as AL, DestroyMetasInArea as EAX. No MISMATCH in the call audit.
- [x] Batch lfx-b 2 (NOT run in the game yet - batch 1 was, see below): the rest of the park manager, the floor and
  the piece menu (23 REPLACE, 1 SOURCE_ONLY). src/game/ed/park_manager.cpp, park_map.cpp, park_floor.cpp,
  piece_menu.cpp (+ piece_menu.h).
  - CParkManager, making and taking down: 0x598E00 Construct (the generator, the shell block names
    "floor_wall_block%d"...), 0x599BF0 CreateInstance, 0x599C70 DeleteInstance, 0x59B020 DestroyConcreteMetas,
    0x59BD40 Destroy (the map brought up to date first, the gap manager deleted, the park pools freed).
  - Metapieces: 0x599A20 AddMetaPieceToPark (pieces placed round the metapiece's middle, the grid of 120 x 48 x
    120 cells centred on the origin; restart pieces of players 1-8 become restart points; the park's cost),
    0x59B090 RelocateMetaPiece (deleted and made again, its gap moved with it).
  - The map: 0x598FC0 ReadFromStructure (goals, rails - the rail editor's flag 0x67F000 cleared outside the "edit"
    level -, MaxPlayers, Park_editor_map), 0x599140 AccessDisk (CustomParks\custom%d.prk; a premade park keeps its
    name from premade_parks_info), 0x599EB0 / 0x59B750 the readers (older parks: 0x2C-byte gaps and another dead
    piece checksum; the two bodies are one function here), 0x59BE20 ResetPark ("unnamed park").
  - The floor: 0x5993E0 GenerateNodeArray (18 default restart points round the park's middle - players 1 and 2,
    four more, six of type 3, six of type 4 -, each dropped onto what a feeler finds, then the node array),
    0x599CD0 CanChangeFloorHere (+ 0x599DD4: its last block, which Ghidra split off - registered SOURCE_ONLY to the
    same function), 0x599E20 DestroyMetasBelow, 0x59B280 UndoFloorChange, 0x59B2C0 MoveRiserList, 0x59B320
    RebuildFloorPiece (riser_above%d / riser_floor / riser_floor_below on top, riser_below walls where a
    neighbour is lower; what's already right is kept), 0x59BED0 RebuildInnerShell (the four strips round the park,
    Sk4Ed_Fence_20x20 pieces where not blocked, no floor height outside), 0x59C520 ChangeFloorHeight, 0x59C600
    RebuildFloor (takes a second, unused argument: RET 8).
  - The piece menu (CUpperMenuManager, 0x7CE60C): 0x598DA0 DeleteInstance, 0x59A9E0 SetPieceSet (the
    parked_make_piece_menu items for clipboards / sectors / pieces with their sector names, the set menu, the
    focus events to the object tracker, the slider over the 7 entries round the selected one).
  - Findings: CMapListNode's first word is the floor change's per-metapiece step (m_dy); GapInfo is two ends,
    their sides, the name, score and how many ends are placed (2 = a whole gap) - 0x596980 packs it into the map.
    The readers' gap is a stack local the original leaves partly uninitialised (+0x4C, and +0x50 for older
    parks): zeroed here. AddMetaPieceToPark's positions have w unset on the stack in the original: 1 here. Quirks
    kept: SetPieceSet reads a null metapiece for an entry with no abstract metapiece; the feeler's smart pointer is
    released by its destructor.
  - Tests: TestParkManagerBuild, TestParkFloor, TestParkPieceMenu (stand-ins for every call; the gap read in is
    compared on the parts the map gives). Deliberate bugs: 84 of 91 caught at first, then the rest after
    strengthening (the collision flag during deletion, +0x50 and +4 of the read map, the shell's last column and
    first row, a top piece at the wrong height, a metapiece exactly at the park's edge, the floor request's
    index); one was equivalent (the feeler's explicit release - its destructor does it, so the call went), two
    mutations didn't build and were rewritten. Menu: 21 bugs, 20 caught; the last (`count < 7` for `<= 7`) is
    equivalent (with exactly 7 entries the clamping gives the same window). One more equivalent: the left shell
    edge's `lowest = 0` as 1 (only its sign is used) - tested as -1 instead.
  - Register scan: only the known 0x4EFF92 alarm; AL readers of CanChangeFloorHere / ChangeFloorHeight (bool). No
    MISMATCH in the call audit; find_gaps shows nothing left in 0x598A60-0x59C900, 0 thunks.
