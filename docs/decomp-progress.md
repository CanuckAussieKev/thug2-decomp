# Decompilation progress

Restart rule: continue the first batch not marked done. A batch is done when it builds cleanly and is committed
(one commit per batch). In-game testing happens later; a batch that breaks the game gets reverted by its commit.
Batches marked (untested) haven't been run in-game yet.

## Core utilities (0x401000-0x406460, 130 functions)

- [x] Batch 0: `Crc::GenerateCRCFromString` 0x401B90 (commit 55ac891)
- [x] Batch 1 (untested): 0x402320-0x402600 -> `Mth::Vector` + `Mth::Line` ctor (14 functions,
      `src/game/mth/vector.cpp`), with self-tests.
- [x] Batch 2 (untested): CRC buffer/extend (0x401AA0, 0x401BE0), `Mth::Matrix` copy + X/Y/Z rotation builders
      (0x404090, 0x404DD0-0x404E70), `Vector::operator-=` (0x406050), `Vector::ProjectToPlane` (0x406080).
      Skipped 0x401020: compiler-generated array-constructor helper, not game code.
- [x] Batch 3 (untested): `Util::BitStream` ctor/SetInputData/SetOutputData/GetByteCount/Flush
      (0x401050-0x4010C0, 0x401180), `Str::LowerCase`/`UpperCase` (0x402080, 0x4020B0), `Mth::Clamp` (0x4022C0),
      Vector(x,y,z) ctor (0x4022F0), Vector/Matrix `operator[]` (0x402310, 0x404080).
      Deferred until their callers are understood: 0x401D80 (assigns IDs from a global counter at 0x690FA0),
      0x403210 / 0x403220 (each zero one field of an unknown class).
- [x] Batch 4 (untested): random streams `Mth::InitialRand/Rnd/Rnd2/PlusOrMinus` (0x403DA0-0x403E80, state shared
      with the exe at 0x690FC8-0x690FDC via `GameVariable`), `Mth::Sign` (0x405FD0), Vector RotateY90 /
      DegreesToRadians / RadiansToDegrees / FeetToInches / NegateXRotateX90 (0x405EB0-0x405F80),
      `Crc::GenerateCRCCaseless` (0x401B30), `Mem::Clear` (0x401C90). Skipped 0x401AF0 (empty loop, no effect).
- [x] Batch 5 (untested, HIGHER RISK): memory wrappers `Mem::New` (0x4015D0), `Mem::Free` (0x401000),
      `Mem::Calloc` x3 (0x401CC0, 0x401D00, 0x401D40) over the game's manager (ptr at 0x7D1800, allocate
      0x5BAFC0, free 0x5BABA0, both __thiscall); `Str::String` ctor/copy-ctor/dtor/assign x2/CopyRange
      (0x401D80-0x401FA0, live-count global 0x690FA0); `Str::FindCaseless` (0x401FF0); `Dbg::Printf` (0x401C30).
      Memory + String have NO self-tests (the game's memory manager doesn't exist when self-tests run).
      If the game fails at startup, revert this batch first.
- [x] Batch 6 (untested): `Mth::BBox` ctor/ctor(point)/AddPoint (0x4033E0, 0x403410, 0x403450), `Mth::Capsule`
      ctors (0x403230, 0x4032A0) and Volume (0x403390), with self-tests.
      Deferred: 0x4032E0 / 0x403330 (capsule containment tests; they call the big distance functions
      0x402640 / 0x402D60, whose calling conventions aren't confirmed yet).
- [x] Batch 7 (untested): `Mth::FastACos` (0x403EF0), `Mth::InitSinLookupTable` (0x403F70, table in globals
      at 0x690FE0, 119 x {sin, cos, dsin, dcos} over 0..pi/2), `Mth::KeepWithin` (0x404010),
      `Mth::CreateRotate2D` (0x4058B0), `Matrix::RotateX/Y/ZLocal` (0x405670, 0x405710, 0x4057B0; originals
      multiply through the statically linked D3DXMatrixMultiply, ours use `Mth::Multiply`), with self-tests.
      Deferred: 0x405600 (Matrix invert - needs 0x4043A0 / 0x4041E0 / 0x404120), 0x405950 (needs 0x404C60),
      0x4058E0 (unknown class, fields at +0x80..+0x90).
- [x] Batch 8 (untested): `BitStream::WriteBits/ReadBits/ReadSignedBits` (0x4010D0, 0x4011B0, 0x401260; early
      self-tests) and LZSS (Okumura-style, public-domain design): `Lzss::InsertNode` 0x401290, `DeleteNode`
      0x4013E0, `Decode` 0x4014F0, `InitTree` 0x401600, `Encode` 0x401700. LZSS state in exe globals
      0x690F7C-0x690F9C, decoder window 0x68FF68. LZSS late self-tests compare the whole original pipeline
      (all five functions via OriginalCode) against ours, byte for byte, plus round trips.
- [x] Batch 9 (untested): `Matrix::operator*=(float)` (0x404120), `Matrix::SetFromEulerAngles` (0x4048D0),
      `Mth::CreateRotateMatrix` axis-angle (0x404C60), `Matrix::Rotate(axis, angle)` (0x405950, previously
      deferred), with self-tests.
- [x] Batch 10 (untested), worked from the disassembly: `Matrix::Determinant` (0x4041E0, row-0 cofactor
      expansion; Ghidra showed it as empty), `Matrix::Adjugate` (0x4043A0), `Matrix::Invert` (0x405600; no
      zero-determinant check, scaling skipped when det == 1), `Mth::FastAtan2(y, x)` (0x4049B0; hardware FPATAN
      when |x| < |y|, else 8-term polynomial), `Matrix::GetEulerAngles` (0x404AB0; calls the CRT's x87 asin at
      0x623560, gimbal-lock threshold 0.005), with self-tests. Not done: 0x404BA0 (debug print of a matrix).
- [x] Batch 11 (untested): `Line::DistanceSqrToPoint` (0x402D60), `Mth::ClosestPointsBetweenLines` (0x402F00,
      epsilon 1e-5, returns bool in AL), `Capsule::ContainsPoint` (0x403330, previously deferred), with
      self-tests covering segment ends, skew/parallel/degenerate lines and clamping.
- [x] Batch 12 (untested): `Line::DistanceSqrToLine` (0x402640, 1.8 KB region-based segment/segment distance;
      parallel threshold 1e-6; branch order and NaN behaviour kept), `Capsule::IntersectsSegment` (0x4032E0,
      previously deferred; returns int because the original sets all of EAX). Self-tests: 200 random pairs plus
      parallel / anti-parallel / point-like cases, tolerance scaled to input size.
- [x] Batch 13 (untested): `BBox::ContainsPoint` (0x4025A0, returns int), `BBox::CountAxesContaining`
      (0x4034D0), `BBox::LineIntersect` (0x403550, faces tested +X -X +Y -Y +Z -Z), `BBox::GetClosestSurfacePoint`
      (0x403C00), with self-tests (random and face-aimed segments, points on faces and corners).
- [x] Batch 14 (untested): `BBox::GetCenter` (0x403BB0), `Mth::CreateRotateMatrix(axis index)` (0x404EC0),
      `Mth::CreateLookAtMatrix` (0x404F50; right vector deliberately not normalised, as in the original),
      `Mth::CreateOrthographicMatrix` (0x405080), `Mth::CreatePerspectiveMatrix` (0x4050F0), with self-tests.
      Register scan: 105 functions, 3,453 call sites, clean.
- [x] Batch 15 (untested): `Str::DehexifyDigit` (0x4020E0, base-32 digits), `Str::CompareCaseless` (0x402120),
      `Str::NextWord` (0x402170, word tokenizer with \_ and \n escapes), `Mth::Rect` ctor (0x404060; Ghidra's
      signature database calls it MFC CRect), `Vector::SetDirection` / `ProjectOnto` / `GetMaxAxis`
      (0x4061A0, 0x406200, 0x406260), with self-tests. Register scan: 112 functions, 3,489 call sites, clean.
- [x] Batch 16 (untested): `Str::FormatWithSeparators` (0x402200; static buffer 0x690FA4, separator from the
      language setting at 0x7CEFA0), `Matrix::operator=(MatrixProduct)` (0x404100), `Matrix::OrthoNormalizeAbout`
      (0x405170), `Mth::FastSin` / `FastCos` (0x405FF0, 0x406020; lookup table + interpolation, no range
      reduction), with self-tests. Register scan: 117 functions, 3,519 call sites, clean.
- [x] Batch 17 (untested): `Mth::SlerpInterpolator` (layout: start matrix +0x00, end +0x40, axis +0x80, angle
      +0x90, small-rotation flag +0x94) - ctor 0x405E90, `SetMatrices` 0x405990 (threshold is a game variable
      at 0x67C648, 2 degrees), `InvertDirection` 0x4058E0 (the previously deferred "unknown class" function);
      `Matrix::GetRotationAxisAngle` 0x405310 (with the near-180-degree special case), `Matrix::RotateLocal`
      0x405850, `Matrix::Print` 0x404BA0, `Vector::Print` 0x405FA0, with self-tests (debug prints untested).
      Register scan: 124 functions, 3,537 call sites, clean.
- [x] Batch 18 (untested): `SlerpInterpolator::GetMatrix` (0x405A20) and `Mth::GetAngleInPlane` (0x4062D0;
      the third argument Ghidra showed as unused is the reference row index), with self-tests.
      Register scan: 126 functions, 3,555 call sites, clean.

**Core utilities block: DONE - 126 of 130 functions rewritten.** The other 4 are deliberately left as original
code (nothing gained by replacing them):
- 0x401020: compiler-generated "construct an array of objects" helper, not game code.
- 0x401AF0: an empty counting loop with no effect.
- 0x403210 / 0x403220: one-line setters that zero a field (+0x10 / +0x20) of classes not identified yet.
  Revisit when their classes turn up in another subsystem.

## Next block
Per docs/subsystems.md: file system (PIP/PRE archives, 0x5B6560-0x5BDAD0), then the script command libraries.
- [x] Safety tool (`tools/ghidra/scan_register_assumptions.py`): scans every call site of every replaced function
      for original callers that read ECX/EDX after the call without writing them first (ignoring push-to-reserve,
      lea reg,[reg], or reg,-1 and zeroing idioms; callers that are themselves replaced count as safe).
      First run (batches 0-13): one real risk - 0x4060F0 relied on ProjectToPlane preserving EDX. Fixed by also
      rewriting 0x4060F0 as `Vector::RotateToPlane` (with self-tests). Now: 100 functions, 3,445 call sites, clean.
      RUN IT AFTER EVERY BATCH (snapshot first; it reads REPLACE lines from src/).
- [x] Infrastructure (untested): late self-test stage. A one-shot hook on ParseNetworkParams (0x4E1220, called
      once from WinMain before the main loop) removes itself, runs late self-tests, then calls the real function.
      `OriginalCode` scopes temporarily restore an original function so it can be compared after hooking.
      Late tests now cover batch 5: Mem::New/Free/Calloc x3 and all Str::String functions.

## Notes
- Ghidra's pseudocode can be wrong for x87 float code: 0x4023A0 decompiled as an empty function but is
  `LengthSqr` (result left in ST0). Always read the disassembly for float functions.
- Float self-tests use a tolerance: originals run on x87, ours on SSE, so the last bits can differ.
- Globals the original code also uses must stay in the exe's memory: use `GameVariable<T>(address)`
  (`src/hook/game.h`). Self-tests that touch such state save and restore it.
- Functions that need the game running (memory, anything touching game objects) go in the late self-tests
  in `selftest.cpp`; wrap each call to an original in `OriginalCode original(address);`.
- RISK - return values Ghidra calls void: Ghidra's pseudocode can show a function as returning nothing while
  the original leaves a meaningful value in EAX that callers use. Lzss::Decode was exactly this: it keeps the
  output pointer in EAX, the file loader pads the data after it, so returning nothing corrupted memory and
  gave a black screen at startup (found by bisecting with thug2-decomp.ini, 2026-10-15). The register scan
  now also lists, for every replaced function, which original callers read EAX/AX/AL after the call: check
  that against the C++ return type (void must have no readers; bool only AL readers). Self-tests should also
  compare return values, not only output buffers.
- RISK - register preservation: the exe was built with whole-program optimisation, so some callers rely on a
  callee leaving a "volatile" register (ECX/EDX) untouched. Example: 0x403C00 keeps using ECX after calling
  0x4034D0. Our compiled replacements may clobber those registers. Before replacing a function, check whether
  it preserves ECX/EDX and whether any caller we are NOT replacing uses them after the call. Checked so far:
  0x4034D0 (only caller replaced too), 0x4025A0 (other callers tail-call it or return straight after).
- Never REPLACE an address PartyMod patches at runtime (it would overwrite our jump or we'd overwrite its).
  Function entries PartyMod jmp-patches: 0x5F56E0, 0x5BDCC0, 0x5F3A50, 0x4A0780, 0x4A07C0, 0x4B2440, 0x4B2330.
  It also patches many call sites and bytes inside functions (see its src/*.c); check before replacing anything
  in 0x44F000-0x452400, 0x4B2E00-0x4B9200, 0x4CE000-0x4D9000, 0x4E2400-0x4E2800, 0x500000-0x530000,
  0x533600, 0x545B00-0x54DD20, 0x5A6E70-0x5A8A10, 0x5B7C00, 0x5BDA00-0x5BDD00, 0x5F3A50-0x5F4600, 0x5F86D0-0x618300.
  Our ParseNetworkParams hook (0x4E1220) is fine: PartyMod patches the call to it at 0x4E26DC, not its entry.
- Member functions are replaced via `REPLACE(addr, &Class::Method)`; constructors via a small adapter that
  placement-news the object (see `LineAdapter` in vector.cpp).
