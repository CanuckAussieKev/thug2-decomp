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
