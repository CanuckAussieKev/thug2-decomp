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
