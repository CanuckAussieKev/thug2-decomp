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
