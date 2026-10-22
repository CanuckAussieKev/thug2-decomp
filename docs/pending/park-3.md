# Park editor & graffiti, part 3 (agent PE3: 0x0058FBD0-0x00592740)

The park editor's pieces, metapieces, map lists, the scene side of the park (CParkGenerator, first part) and the park
editor object's state commands. Worktree branch worktree-agent-a806711fa9b514a25. Tests are offline (thug2_check);
nothing here has been run in the game yet unless a batch says so.

Shared-file edits (small, additive): src/game/ed/park.h (CPiece's layout and methods, CSourcePiece, CClonedPiece,
GridDims methods out of line; see each batch), CMakeLists.txt (new files). The park.h methods that called the game's
addresses through GameFunction now call ours directly: a self-test that stands in for one of those addresses for code
that calls it must also stand in for ours (AddressOf(&Ed::CPiece::SetRot) and so on) - every existing test passed
without that.

- [x] Batch PE3-1 (NOT run in the game yet; park editor & graffiti, part PE3-1 - the pieces and cell boxes): new
  src/game/ed/park_pieces.cpp, park_scene.h (the engine's sector / geometry / scene calls the park editor makes, Nx -
  not rewritten, GameFunction). GridDims: 0x005901B0 the constructor (Set, returns this), 0x005901E0 Byte(index),
  0x005901F0 MakeInfiniteOnY (h = 127 - y), 0x00590200 MakeInfinitelyHigh (y -100, h 200), 0x00590210 PrintContents
  ("pos=(%d,%d,%d) dims=(%d,%d,%d)", y signed). Ed::CPiece (now laid out: +0x04 its sector's checksum - a source's
  name -, +0x08 flags: 1 source, 2 cloned, 4 not listed, 8 in the generator's list, 0x20 / 0x100 / 0x200 from
  CreatePiece's flags, 0x40 shown and hidden by SetPiecesVisible, 0x400 / 0x800 / 0x4000 a source's from the piece
  list; +0x0C the generator's list; a cloned piece's +0x10 clone index, +0x14 source, +0x18 position, +0x28 quarter
  turns; vtable 0x0064F3B4: slot 0 the size, slot 1 the scalar deleting destructor): 0x00590240 destructor body,
  0x00590490 scalar deleting destructor, 0x00590250 GetCloneIndex (4 bytes, REPLACE_SOURCE_ONLY; folded with other
  classes' +0x10 getters - several vtables use it), 0x00590260 SetRot(rot, update: the sector's SetYRotation only when
  update is exactly 1), 0x00590290 SetActive (a tail jump to CSector::SetActive), 0x005902B0 SetVisible (the sector's
  viewport mask 0xFF or 0), 0x005904C0 GetCellDims (w h l = (size + 2) / cell size, truncated; the rest of the box as it
  was), 0x00590530 GetDimsTurned(out, rot) (the source's size, x and z swapped for an odd turn; the original asks the
  source three times and leaves w a stack leftover for even turns - one call and the source's w here), 0x00590670
  SetPosition(position, update), 0x005906B0 SetLength(length) (the sector scaled along its own x, z when turned a
  quarter; w a stack leftover in the original, 1 here), 0x00591510 SetAngle(degrees) (the geometry's orientation:
  identity times RotY(degrees * the exe's pi/180), D3DXMatrixMultiply left as a call; the sector isn't null-checked, the
  geometry is), 0x00591600 SetHighlight(on, gap) (the geometry's colour 0x00A064A0 for a gap's piece, 0x006464A0
  otherwise, or cleared through 0x0048D060). Ed::CSourcePiece (0x30 bytes, vtable 0x0064F3C8): 0x005905C0 the
  constructor (size (0,0,0,1), +0x20 1), 0x00590600 GetDims, 0x00590640 the scalar deleting destructor (also the
  cloned piece's). Ed::CClonedPiece (0x2C bytes, vtable 0x0064F3D0): 0x00590620 the constructor, 0x00590710 GetDims
  (the source's size turned; w a stack leftover in the original). Test (selftests_park_pieces.cpp, shared kit
  hook/park_pieces_testkit.h: the sectors, geometry and its vtable, the scenes, the rotation matrix and D3DX's multiply
  stood in for by recording code): every function, all four turns and odd ones, sizes on and next to cell edges, the
  sector / geometry null paths, both colours; the whole arena and the calls compared. 39 deliberate bugs, 38 caught
  (1 after adding flags 2 to the destructor cases); 1 equivalent (SetAngle given the rotation instead of identity times
  it - the same matrix).
- [x] Batch PE3-2 (NOT run in the game yet; park editor & graffiti, part PE3-2 - the metapieces): new
  src/game/ed/park_metapieces.cpp; park.h: CMetaPiece's methods out of line (the GameFunction wrappers gone),
  kConcrete / kAbstract flags, new classes CConcreteMetaPiece (vtable 0x0064F3D8) and CAbstractMetaPiece (0x0064F3E4).
  CMetaPiece: 0x005902F0 IsRestartPiece (the eight restart pieces' names), 0x00590340 GetDescriptor (signed index),
  0x005903A0 SetVisibility (each piece's sector SetActive), 0x00590400 CountPieces (palette entries: 1 for a single
  piece - flag 0x80 -, else its count), 0x00590750 IsCompletelyWithinArea (x and z only), 0x005907B0 SetRot (w and l
  swapped on an odd turn; every descriptor turned about the corner into new storage, its own size in cells from the
  palette (abstract) or GetCellDims (concrete); the original copies two uninitialised stack descriptors over the
  unused inline ones - here they keep what they were), 0x005909C0 GetRelativePosition (y read unsigned; z rounded to
  float before the half length comes off; w a stack leftover in the original, 1 here), 0x00590AD0 Initialize (the
  extra entries copied only when growing), 0x00590B90 GetBounds(max, min), 0x00590CE0 Intersects(area, precise) -
  NB the precise test skips the pieces whose descriptor +4 ("isReal", from the palette's flag 0x0CD75A3F) is SET:
  the field's name is the reverse of what it means there -, 0x00591670 the base's scalar deleting destructor,
  0x005916B0 GetPieceList (an array of structures: each piece's checksum and relative position), 0x005917E0 the
  destructor body, 0x00591800 SetPosition(position, angle) (each piece: identity with its offset in row 3, times
  RotY, through D3DXMatrixMultiply, plus the position (w too); then its angle desc.rot * 90 + angle; y read unsigned),
  0x00591A50 Highlight (cloned pieces only), 0x00591AB0 FinishPieces (cells from the bounds, h at least 1; pieces
  centred - z by the float-rounded size). CConcreteMetaPiece: 0x00590B60 constructor, 0x00590360 GetPiece,
  0x00590ED0 AddPiece (isReal normalised), 0x00591BB0 scalar deleting destructor (also the abstract one's).
  CAbstractMetaPiece: 0x00590FA0 constructor, 0x00590FD0 GetPiece (the generator's source by name), 0x00591020
  AddPiece (a source by name; anything else counted but its descriptor left alone; the byte as given; h at least 1),
  0x00591100 AddMetaPiece (no h clamp), 0x005911D0 GetEntry(out, index) (nested palette metapieces' pieces counted
  one by one, placed by the outer turn - the turn sum not wrapped; turns above 3 not placed; past the end the original
  returns a stack leftover - an empty entry here). Test (selftests_park_metapieces.cpp, three tests: names /
  descriptors / areas / visibility / storage / destructors; pieces added, bounds, positions, world placement,
  intersection - areas at every edge, flagged and unflagged pieces, the precise flag's high bytes - and every turn
  amount; the palette's nested entries for every outer turn, counts, sources, abstract turns, pieces and metapieces
  added, the script array). 70 deliberate bugs, all caught (10 after strengthening: turns by 3, float rounding of z,
  edges, negative heights, a flat metapiece, a descriptor past the end, w kept, the h clamps, bounds' top).
- Merge with PE4 (main): PE4's park_scene.h (the generator's data ParkGeneratorData) kept; PE3's engine-call header
  renamed src/game/ed/park_nx.h. One type per piece: PE4's SourcePiece / ClonedPiece are now aliases of CSourcePiece /
  CPiece, which took PE4's field and flag names (m_sector, m_id, CSourcePiece* mp_source; m_name, m_size,
  m_triggerScript, m_numRailPoints / Segments; kCursor 0x4, kInList 0x8, kGap 0x40, kDestroyNow 0x200...).
  park_scene.cpp's file-local helpers that share names with CParkGenerator members are called qualified (Ed::...).
- [x] Batch PE3-3 (NOT run in the game yet; park editor & graffiti, part PE3-3 - the generator's first part and the
  metapiece lists): new src/game/ed/park_generator.cpp (the generator's data through PE4's Data(generator)); park.h:
  the wrappers of this range declared instead, CMapListTemp::PrintContents / Destroy, CMapListNode::VerifyIntegrity
  __fastcall. Lists: 0x00590470 VerifyIntegrity, 0x00591450 CMapListTemp::PrintContents, 0x005914B0
  CMapListNode::Free and 0x00591BE0 DestroyList (park_lists.cpp's, registered now), 0x00591C60 CMapListTemp::Destroy.
  CParkGenerator: 0x00591C70 DestroyBody (the scenes forgotten, the node array's asset destroyed - 0x00407810, CAsset's
  body, folded -, the two rail sets' empty destructors), 0x00591CF0 HighlightAllPieces, 0x00591D20 SetMaxPlayers,
  0x00591D30 GetMaxPlayersPossible ((main heap free + (players - 1) * 585500) / 585500 + 1 - the original's magic
  division, 32-bit wrap kept), 0x00591D90 GetPieceSource (search only), 0x00591DD0 DeleteSector(name, unused),
  0x00591DE0 DestroyRails (m_rails cleared and its pools freed), 0x00591E00 SetLights, 0x00591E60 GetRestartSlots
  (type 1 slot 1, 2 slot 0, 3 slots 2-7, 4 slots 8-13, 5-8 slots 14-17; other types: left as they were),
  0x00591F50 HasFreeRestartSlot, 0x00591FC0 ClearRestarts, 0x00591FE0 ClearAutomaticRestarts, 0x00592010 SetUpBounds
  (the cloned scene's box -3360,-768,-3360 to 3360,3168,3360), 0x00592090 RebuildCollision, 0x005920D0 UpdateBounds
  (the shell's boxes: +-200000, then the park's bounds' cell centres, y +-100000), 0x00592290 SetPiecesHighlighted
  (the gap pieces' sectors shown / hidden - the old "highlight" name kept), 0x005922D0 SetRestartPiecesActive,
  0x00592320 CloneSector, 0x00592370 DestroySector, 0x005923C0 CreatePiece (flags & 0x324; the clone's dead colour
  branch behind 0x0049F4B0, a folded return-false, kept), 0x00592550 AddPiece, 0x00592570 DeletePiece, 0x005925E0
  ReadPieceFlags (LevelGeometry / GameObject nodes: GameObject -> 0x400, + flag 0x68910AC6 -> 0x800; TriggerScript
  kept, four scripts -> kRebuilt). Test (selftests_park_generator.cpp, three tests: lists, pools, players, sources,
  lights, restart slots, destructor; bounds, collision, visibility, pieces made / added / deleted; the node list's
  flags). 60 deliberate bugs, all caught (6 after strengthening: pools present, flags not preset, a source named 0, a
  script preset). Tests standing in for these addresses in
  selftests_parkedA / lfxc3 / parkmax now stand in for ours too (OursOf).
- Merges with PE2 and PE1 (main): this range's park editor code uses PE2's Ed::CParkEditor (park_editor.h: members
  Construct / Create / LogicTask, kVtable / kListenerVtable and the two script commands added); PE1's rail sets
  (park_rails.h) used through PE4's aliases. PE1's tests standing in for this range's addresses (selftests_park_clipboard
  / _gaps) stand in for ours too (OursOf), as do selftests_parkedA / lfxc3 / parkmax. 0x005902E0 (CMetaPiece::Cast, 3
  bytes, folded with many empty constructors) registered REPLACE_SOURCE_ONLY.
- [x] Batch PE3-4 (NOT run in the game yet; park editor & graffiti, part PE3-4 - the park editor's state commands):
  new src/game/ed/park_editor_state.cpp. 0x0058FBD0 SetParkEditorState (regular_play: memory marker, the park manager
  set up for 0xCD3682C1, then as test_play: the park rebuilt, the cursor deleted, the collision, the node array, the
  gap checklist, the state; off: the state set and stored, the cursor and the piece menu deleted, the park torn down
  (Destroy(0)), the gap manager's copy deleted, the node array removed, unpaused, the menu enabled again if there is
  one, the cursor shown unless placing rails, the menu stick; edit: command 0x27F2117D sets up (marker, Initialize,
  the menu, the cursor), 0xE76F5250 / 0x1A4E0EF9 / 0x2A907CBC reset or rebuild the park (2 players) - the inner
  switch's 0x01625EDA case is dead code, kept -; then the cursor made and kept in the park, the state; other states:
  false), 0x0058FED0 SetParkEditorPauseMode (pause: flag, the script 0x30251D65 and the menu disabled, the cursor
  hidden, the menu stick; unpause: the reverse; neither: nothing), 0x0058FFC0 CParkEditor::LogicTask (the clock
  0x007CE5EC moved on by the time since, read again after Tmr::GetTime; then LogicUpdate), 0x0058FFF0
  CParkEditor::Construct (the module, the listener, the three tasks / handler Calloc'd with their code - ours, aliased
  in the test -, 480 / 120 / 1400 / 10, state off, the park manager made, the listener registered), 0x00590160
  CParkEditor::Create (core/singleton.cpp's GameSingleton for it left calling the game address, hooked). Test
  (selftests_park_editor_state.cpp: every state and edit command with and without the cursor / menu, the cursor placing
  rails; pause / unpause / neither / both; the clock; the constructor over dirty memory; the singleton's five cases).
  41 deliberate bugs, 39 caught (1 after presetting the paused flag); 2 equivalent (the clock set to the time - the
  same unless GetTime moved it; the input handler's index - Calloc'd memory is zero).
- Range done: every function of 0x0058FBD0-0x00592740 has source (82: 79 Ghidra functions plus 0x0058FFC0, 0x00590360,
  0x00591DE0 found in the padding); find_gaps lists nothing in it. check_code_writes: no overlaps; register scan and
  call audit: nothing new.
- Final checks (85f15b9 + this record): thug2_check on THUG2PM.exe and the stock THUG2.exe: 0 MISMATCHED, 771 early
  self-tests passed. All four mutation specs re-run on the final code: PE3-1 38/39 (1 equivalent), PE3-2 70/70, PE3-3
  60/60, PE3-4 39/41 (2 equivalent).
- Game runs (C:\Games\THUG2-3, under the lock, this build's DLL; 5628 of 5628 replacements installed): `-level BO` -
  the level loads, the skater spawns, no exception / run-time check in the log; `-level sk5ed` - the park editor comes
  up (state edit: the piece menu, the cursor, the empty park, the memory bar), no exception. Nothing played by hand.
- For the user to try by hand (CREATE-A-PARK): place, turn (both ways, all four turns) and delete pieces of every set,
  multi-piece palette entries (ramps with rails, the shell / floor pieces), raise / lower the floor under and beside
  pieces, copy / paste an area (turned), place and turn gaps (the gap piece's purple tint, its stretch), restart
  points and CTF flags (the slots per type), pause / unpause (the menu and cursor hidden / back), test play and back,
  save and load a park, resize it (the shell's bounds), max players. Switch off with `[replacements] disable=`
  park_pieces.cpp, park_metapieces.cpp, park_generator.cpp, park_editor_state.cpp.
