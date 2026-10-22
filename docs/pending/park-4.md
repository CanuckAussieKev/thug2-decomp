# Park editor & graffiti part 4: the park generator 0x00592740-0x00595030 - agent PE4

Branch worktree-agent-ab39f0cb5b415912e. Every function in the range (31; Ghidra's list, no code it missed - the
padding between them is int3) has source: src/game/ed/park_scene.h / park_scene.cpp / park_nodes.cpp (new). Tests in
src/hook/selftests_park_scene.cpp (TestParkGeneratorRestarts, TestParkGeneratorScene, TestParkGeneratorNodes).
Shared files touched (small, additive): CMakeLists.txt (the three new files), src/game/ed/park.h (CParkGenerator: the
GameFunction wrappers of this range's methods became declarations, the rest of the range's methods declared; a forward
declaration of RailNodeInfo; the wrapper `AddCollision` (0x00594410) is `LightPiece` now - it lights the piece, it
doesn't add collision), src/game/ed/park_manager.cpp (its one call of it renamed).

The generator's data (0x4A4 bytes) is laid out in park_scene.h as `ParkGeneratorData` (reached through
`Data(generator)`), not as members of CParkGenerator, so that the neighbouring agent (PE3: 0x0058FBD0-0x00592740, the
rest of CParkGenerator) can add its own without the two clashing - the lead may want to fold the two layouts into
CParkGenerator when merging. Also laid out there: the source piece (CSourcePiece, 0x30) and the placed piece (what
CPiece is, 0x2C), and the parts of PE1's rail sets (0x0058AFC0-0x0058B700: RailSet / Rail / RailPoint, still
GameFunction calls) that this code reads.

- [x] Batch PE4-1 (NOT run in the game yet; the park generator's scene, pieces, restart points and lights - 19
  functions): src/game/ed/park_scene.h / .cpp.
  - 0x00593460 Construct (the two rail sets, the 18 restarts' cells, the node array asset at +0x440; no pieces, piece
    ids from 0x20, 2 players), 0x00592FE0 SetLightPosition, 0x00593010 CalculateLighting (r, g, b: the ambient part
    (red and green rounded to floats, blue not - as the original keeps it on the FPU) plus 255 * the sum over the lights
    of 1 / (dz^2 + dx^2 + 160000) times the falloff colour; above 255 is 255, else _ftol - NaN included),
    0x00594410 LightPiece (was `AddCollision` in park.h: the piece's vertex colours from its source's, lit; then its
    collision's vertex bytes - grey from the source's - lit and averaged; the vector / colour new[]s have constructors
    that do nothing), 0x00593200 AddRestart (the slots of a type from 0x00591E60; the first free one, or one put down
    automatically when this one isn't; with `automatic` and `keepPlaced`, at the position and cells of the last
    placed one of the type; only the low bytes of the two flags count, the automatic byte stored as given),
    0x00593370 CountRestarts, 0x005933D0 NeedsRestarts, 0x005933F0 RemoveRestart (by the cells' x, y, z),
    0x00593500 CreateGapPiece (the piece type 0xC0341DF7 - none: a null source passed on, as in the original),
    0x00593580 DestroyPieces, 0x005935F0 DestroyPiece, 0x00593610 CleanUpOutRefs and 0x00594610 UnloadPieces (the
    piece unlinked unless it's the cursor's (flag 4), its sector deleted, the super sectors updated when 0x0067F398 is
    set, counts dirty, deleted - inline in the original, as 0x00592570 does), 0x00594730 AddPieceToScene (the sector
    cloned from the "sk5ed" scene into the "cloned" one, placed and turned, a gap's piece told the gap manager, lit),
    0x005947D0 RebuildAll (every sector deleted, the rail editor's rails out, the node array dropped (inline
    RemoveNodeArray), every piece cloned again, the rail editor rebuilt, the super sectors updated; it calls
    CParkManager::sInstance and ignores the result - kept), 0x00594910 RebuildMarked (the pieces whose type has flag
    0x4000), 0x00594C70 CleanUp (its argument unused), 0x00594C90 LoadSourcePieces (a source piece for each sector
    of "sk5ed" (its sector table at +0xD0 iterated; an item without a value ends it): its size in whole cells of 120
    from its box ((max - min + 2) * (float)(1/120), at least 1) in x and z, its height as it is but at least 48 when
    between 0 and 48; the "cloned" scene, the bounds (0x00592010), the restarts freed; the argument unused),
    0x00594FD0 LoadPieces (LoadSourcePieces, the types' flags from the pieces' node array (0x005925E0), the source
    rails - on the top-down heap for the park editor's own level 0xCD3682C1).
  - The piece flags as far as this code shows them: 4 the cursor's (never in the generator's list; its sector cloned
    with the other flag), 8 in the list, 0x20 / 0x80 no node, 0x40 a gap's trigger, 0x100 its rails not counted, 0x200
    destroyed at once by DestroyPiece, 0x1000 marked for CleanUpOutRefs. The generator's: 2 sources loaded, 4 pieces
    made (CreatePiece), 8 has pieces, 0x10 super sectors updated, 0x20 node array built, 0x40 the scene changed.
  - Tests TestParkGeneratorRestarts and TestParkGeneratorScene: original vs ours on a stand-in generator (pieces,
    source pieces, restarts, lights, a sector table, boxes, geometry, collision), every game function called stood in
    for by recording code with scripted results (scenes, sectors, geometry, the rail editor, the rail sets, the
    memory manager, CreatePiece / 0x00592570 / the bounds / the source piece's constructor...); the calls, results
    and the whole stand-in memory compared. Covers every branch read: each restart type and slot state, the low bytes
    of the flags, the rounding of the ambient part (3 * 1.6666666 is 5 as a float, 4.99... as a double), a value
    just over / under 100 by the light distance's 160000, the lists (first, middle, last, the cursor's, emptied).
    86 deliberate bugs: 72 caught at once, 10 more after strengthening the tests; the 4 left are equivalent: a clamp
    at >= 255 (255 either way), the first loop's early `return false` (the second loop returns false then too), 1/120
    written as 1.0f / 120.0f (the same float), a height of exactly 48 set to 48.
- [x] Batch PE4-2 (NOT run in the game yet; the node array the park's level runs on - 12 functions):
  src/game/ed/park_nodes.cpp.
  - 0x00592740 NextGeometryNode, 0x005927D0 NextRailNode (from the search start 0x007CE600 - reset when starting a
    piece; a node of the piece with no links is a rail's first - else the first linked one, the rail then a loop; the
    next one is the node whose first link is the previous one's name; nodes once taken are skipped; the node's pos,
    angles, class, type, TerrainType (rail nodes) and NoClimbing), 0x005936E0 BuildSourceRails (the pieces' node array
    into the source rail set: "in_railpoint_pool" / "in_railstring_pool" from the memory script (the string one asked
    first, as in the original), LoadTerrain run, each LevelGeometry node's rails; a rail stops being climbable at a
    later point with NoClimbing (the first point isn't looked at); 0x400 bytes for the taken nodes - 256, no check -
    as in the original; each piece type's rail points and segments counted), 0x00592A10 FindNearestRailPoint,
    0x00592AB0 GetNodeCounts (the node array's size and the memory estimates: 7 * points + 6 * nodes + 0x4F6 +
    segments, nodes + 300 + 2 * points), 0x00592C10 BuildGoalNodeArray, 0x00592C90 RemoveNodeArray (static: `this`
    unused), 0x00592CD0 BuildRails (the metapieces in the park with rails: each piece's source rails copied to it with
    its id as the cluster (0 for the inner shell), climbing rails of the ladders turned by pi and of 0x649FDDCA /
    0x54055089 by pi / 2, the lamp's dropped; "out_..." pools on the top-down heap), 0x00592ED0 WritePieceNodes,
    0x005949F0 WriteRailNodes (names from 0x1E8481, a link to the next point or - a loop's last - to its rail's first),
    0x00593AC0 WriteRestartNodes (the "High Score" restart TRG_G_TrickAttack_RestartNode at the first player 1 one;
    then each restart: names counting from 0x5B8D81 (the CTF ones TRG_CTF_Restart_<colour>), player 1 and horse ones
    twice (the second a Multiplayer one), pos 50 up, (rot + 2) quarter turns, the king of the hill's a GenericNode
    Crown with no RestartName; each flag three GameObject nodes: TRG_Flag_<colour>, TRG_CTF_<colour> (both with the
    flag's model and TRG_Flag_<colour>Script_Parked) and TRG_CTF_<colour>_Base), 0x00594EA0 BuildNodeArray (the counts,
    the placed pieces' rails, the old array dropped, a new one of the right size: the pieces, rails, the rail
    editor's rails (names from 0x3D0901), restarts, the goal editor's nodes; added to the node manager, its owner the
    asset at +0x440, parsed - CFunc_ParseNodeArray).
  - Test TestParkGeneratorNodes: a stand-in node array (LevelGeometry nodes, rails with links by name, a loop, climbing
    nodes with NoClimbing, a nameless piece, nodes of other pieces) whose getters answer from a table; the adders
    recorded (strings by their text, arrays by their contents); real script arrays (Init / SetSizeAndType / Clear on
    the stand-in heap); the rail set stood in for. 101 deliberate bugs: 93 caught at once, 6 more after strengthening
    (a tie and a point between 1e8 and 1e9 away for FindNearestRailPoint, a piece with only flag 0x80, the second
    quarter-turned piece, no rails at all, the rail editor's count); the 2 left are equivalent (clearing the
    climbable bit when it's already clear; resetting the loop flag NextRailNode always writes when it finds one).
  - Shared rail-set wrappers (park_scene.h) are GameFunction calls into PE1's range; their conventions were checked
    against the original (audit_game_function_calls: no MISMATCH; the "?" entries are tail jumps through vtables,
    checked by hand at their call sites).
- Checks: thug2_check on THUG2PM.exe and the stock THUG2.exe (full: 749 early self-tests) - 0 MISMATCHED, all passed on both; scan_register_assumptions: only the known
  RISK lines (0x4EFF92 PUSH ECX, InitEditedGoalStub's callers), no new ones; audit_game_function_calls: no MISMATCH;
  check_code_writes: No overlaps; find_gaps: nothing in 0x00592740-0x00595030.
- Game run: see below (not yet).

## To try by hand (the park editor)
The whole park editor goes through this code: every piece placed or deleted, every rebuild, the restart points, the
lighting of the pieces and the node array a park is played on. Worth trying: open CREATE-A-PARK; place, turn, move and
delete pieces (ladders, lamps, rails, quarter pipes - their rails must be grindable where they were, the ladders'
climbable); raise / lower floor; place player 1 / 2 / horse / king of the hill restarts and the four CTF flags, then
test-play the park (the high score, multiplayer, horse and CTF restarts; the flags and their bases there); place gaps
and check their triggers; load a saved park and a premade one; check the pieces' lighting looks as before (near and
far from the park's lights). If anything breaks: `[replacements] disable=park_scene.cpp,park_nodes.cpp` in
thug2-decomp.ini (either one alone too).
