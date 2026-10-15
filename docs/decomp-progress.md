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

**TESTED IN-GAME 2026-10-15 (commit 0b5ac11):** the game runs with all 126 replacements active; early and late
self-tests all pass. (The first test gave a black screen: Lzss::Decode's return value - see the RISK notes.)

**Core utilities block: DONE - 127 of 130 functions rewritten.** The other 3 are deliberately left as original
code for now:
- 0x401020: compiler-generated "construct an array of objects" helper, not game code.
- 0x403210 / 0x403220: tiny base-class/member constructors (zero a field at +0x10 / +0x20), called from the
  constructors of classes in other subsystems (0x418330; 0x42E3F0 and the object built in 0x48B430). Do them
  together with those classes.
- (0x401AF0 was listed here as "an empty loop with no effect" - wrong. Ghidra's pseudocode missed its third
  argument; it is Crc::ExtendCRCWithBuffer and its caller uses the result. Rewritten 2026-10-15.)

## File system & memory block (0x5B6560-0x5BDAD0, 222 functions)
Contents: async file loader (0x5B6560-0x5B7130), PIP/PRE archives (0x5B7130-0x5B9CB0), small helpers incl. the
item pool (0x5B9DF0-0x5BA560), the MEMORY MANAGER itself (0x5BA5A0-0x5BBE80: allocate 0x5BAFC0, free 0x5BABA0,
context push/pop, named heaps - do this last and carefully), hash table / save paths / skater-select menu /
input (0x5BC2C0-0x5BDA20). 0x5BDA20 is patched by PartyMod (bytes at 0x5BDA31): keep it original.
Pip_Load (0x5B7C00) is wrapped by PartyMod's script hook: keep its cdecl (char*, int, int, int, int) shape.

- [x] Batch 19 (tested 2026-10-15: level loads and plays; self-tests pass): `Mem::Pool` fixed-size item pool - ctor 0x5BA420 (late self-test), `Allocate`
      0x5BA3D0 (335 callers), `Free` 0x5BA400, `Contains` 0x5BA3B0 (bool, AL only), with self-tests.
      Scanner: the PUSH-ECX filter now ignores writes to other stack slots ([ESP + n]); 131 functions,
      4,429 call sites, clean. Return-value pass matches all four.
- [x] Batch 20 (tested 2026-10-15: level loads and plays; self-tests pass): `Lst::HashTable` - ctor 0x49FD90, `PutItem` 0x462740, `FlushItem` 0x5B80D0,
      `FlushAllItems` 0x49FD30, `HandleCallback` 0x5B8060, `IterateNext` 0x588190 (bool, 114 callers read AL);
      `Mem::PoolManager` `SetUp` 0x5BC3C0 / `CreateItem` 0x5BC450 / `FreeItem` 0x5BC4A0 (item pool 0 =
      "HashItem", pointers at 0x7D1808). Late self-test runs the same insert/remove/iterate sequence through
      the original and our code and compares bucket contents. Lookup (GetItem) is inlined at its callers.
      Scanner: 140 functions, 4,719 call sites, clean.
      Not done here: 0x5BC2C0/0x5BC340 create and destroy the memory manager itself (with the manager);
      0x5BC4E0-0x5BC620 are a different class (a two-ended stack-style region allocator, vtable 0x658A58).
- [x] Batch 21 (tested 2026-10-15: level loads and plays; self-tests pass): `Pip` bookkeeping - `FindPreEntry` 0x5B7180, `Unload` 0x5B7200 / by name
      0x5B7270, `GetFileSize` 0x5B7290 / by name 0x5B72E0, `UnloadPre` 0x5B7130, `GetNextLoadedPre`
      0x5B7300, `PreFileIsInUse` 0x5B7370. Tables: 200 individually loaded files {data, checksum, size,
      refcount} at 0x7CF198; 100 loaded .pre archives at 0x7CFE18 (each block starts with the archive's
      name, padded to 16, then {size, version, numFiles} and 16-byte entry headers). Files are freed with
      0x5BBB60 (memory manager, called by address for now). Early self-test on fake archives.
      Scanner: 148 functions, 4,746 call sites, clean.
- [x] Batch 22 (tested 2026-10-15: level loads and plays; self-tests pass): `File` I/O layer - `Open` 0x5B9E60, `Close` 0x5B9EA0, `Read` 0x5B9EE0, `Write`
      0x5B9F60, `Seek` 0x5B9FA0, `GetFileSize` 0x5B9E00, `Exist` 0x5BA010, `OpenOnDisk` 0x5B9CB0 (argument
      in EDX: naked asm adapter), and the archive-manager accessors 0x5B8040/0x5B8050. Each call asks the .pre
      archive manager (0x7D0FC4) first and uses its result only if the "handled" flag (0x7D0FC8) is set.
      OpenOnDisk maps names to data\..., .tex -> .txx and .pre -> .prx. Early self-test opens, seeks and
      reads real files in Data\pre through both versions. Scanner: 158 functions, 4,954 call sites, clean.
- [x] Batch 23 (tested 2026-10-15: level loads and plays; self-tests pass): `Pip::Load` 0x5B7C00 - already-loaded checks (archive entry / loaded-file
      table), then async loader, pre-file manager (0x7D0FCC), CD-cache stubs, or plain File::Open/Read into a
      2 KB-aligned block that is then shrunk. PartyMod patches some *call sites* to its script-patching
      wrapper, which calls 0x5B7C00 by address - that now lands in ours (same cdecl signature). Late
      self-test loads pre/Themes.prx through both and compares bytes, size and slot; async path untested.
      Memory manager (0x5BBAE0/0x5BBB30/0x5BBB60), async loader and pre-file manager called by address.
- [x] Test framework (2026-10-15): MinHook trampolines (Original() works any time, any thread); SELFTEST
      stages Early / Late / EngineStarted (MainLoop 0x44EEF0); crash guard with cleanup; REPLACE_SHADOW
      on 28 pure functions (first N game calls compared against the original; demonstrated with a
      deliberate FastSin error); tools/check_partymod_overlap.py. At engine start 7 of the 28 have been
      called (503 calls checked, no mismatches); the quit-time summary shows coverage after play.
      Tested 2026-10-15 (short play session + menu quit): quit is clean (no assertion) after the fastExit
      fix; 1,283 calls checked across 20 of the 28, no mismatches. Not yet called by the game: CRC
      caseless, Capsule::Volume, FastAtan2/Sin/Cos, Matrix::Determinant, GetAngleInPlane,
      Vector::LengthSqr (our own math calls these directly, so only the game's direct calls count).
- [x] Chat (2026-10-15, tested in a network game): GameNet::SendChatMessage 0x537BD0, CFunc_SendChatMessage
      0x5A16B0, chat commands (/hello in two colours as "Server", /colours). See src/game/net/chat.cpp.
- [x] Batch 24 (untested in play): `Pre::PreFile` (one loaded .pre archive) - ctor 0x5B8820, scalar deleting
      dtor 0x5B9410, LoadContainedFile 0x5B8230, GetContainedFileByHandle 0x5B8310, GetContainedFile
      0x5B8A00, Read 0x5B7F00 (returns bytes read - Ghidra said void), Seek 0x5B7F60; `Pre::Decode`
      0x5B7DF0 (LZSS, 4 KB ring at 0x7CFFB0; hangs on a bad size like the original); Lst::HashTable dtor
      0x5B89D0 and GetItem (inlined in the original). Mem::Allocate/Malloc/FreeMalloc/ReallocateShrink and
      the two heaps are now in mem.h. Late self-test on a fake archive (plain, LZSS-compressed, empty and
      duplicate-name files). Scanner: 170 functions, 4,983 call sites, clean. 0x5480A0 is an empty `ret`
      (profiling marker; PartyMod uses the bytes after it as a code cave) - calls to it are dropped.
- [x] Batch 25 (untested in play): `Pre::PreMgr` - ctor 0x5B8D20, CreateManager 0x5B8E90 (returns the
      instance in EAX - Ghidra said void; the caller keeps it), DestroyManager 0x5B9510, LoadFile 0x5B8750,
      FileExists 0x5B8390, OpenFile 0x5B8AD0 (language redirect of "...scripts.pre" via 0x7CEFA0),
      GetContainedFile 0x5B84A0, IsLoaded 0x5B8540, IsLoadFinished 0x5B7FD0, FinishLoad 0x5B8DB0 (THREE
      stack arguments - RET 0xC; Ghidra showed two), UnloadPre 0x5B9570; the File:: archive hooks Exist,
      Open, Close, Read, Write, Seek, GetFileSize (0x5B85A0-0x5B86E0, 0x5B8C70). File and Pip now call them
      directly. Late self-test: an original and a rewritten manager given the same two archives through
      pending loads, compared through every call, with the manager globals saved and restored.
- [x] Argument-size check (2026-10-15): at install time each replacement's popped argument bytes (from its
      C++ type, kCalleePop) are compared with the original's RET n (found by walking its code with MinHook's
      hde32 decoder); a mismatch is logged as NOT REPLACED and the game stops at startup with a message
      naming it (silently running the original would give false confidence; disable= skips deliberately). Found because the first
      FinishLoad (two arguments) made the test hang. 187 of 190 checked, all match.
- [x] Batch 26 (NOT run yet - built and checked offline only): PreMgr::LoadPre 0x5B8EE0 (pre\<name>.prx,
      async through the async loader with callback 0x5B8E70 -> FinishLoad(handle, data, size), French/German
      localised .prf/.prd or .ppf/.ppd patch, script global block_pre_loading -> m_blockPreLoading, load time
      printout); the script functions LoadPreFile 0x5B9470, UnloadPreFile 0x5B9600, IsLoadPreFinished
      0x5B8160, AllLoadPreFinished 0x5B8190, WaitLoadPre 0x5B81B0, WaitAllLoadPre 0x5B8200, InPreFile
      0x5B8720. Shared helpers: game/script/params.h (GetString, ContainsFlag), game/file/async.h (async
      loader and CD-cache calls). Engine-started self-test loads pre/Themes.prx through an original and a
      rewritten manager.
- [x] tools/check (thug2_check.exe, built with everything): runs the argument-size check against a copy of
      the exe image without starting the game. 199 replacements: 197 match, 2 naked stubs unchecked.
- [x] Batch 27 (NOT run yet - built and checked offline only): Pip::LoadPre 0x5B75A0 (archive read
      with its name in front, optional French/German .ppf/.ppd patch, then REPACKED: every file
      uncompressed - via Lzss::Decode's returned end pointer - with 16-byte aligned data, 0xC00 slack;
      raw archives step header+name+data(4-aligned), repacked ones align the data to 16), Pip::LoadFileInto
      0x5B7B00, script functions LoadPipPre 0x5B7A60 (heap = checksum), UnLoadPipPre 0x5B7410,
      DumpPipPreStatus 0x5B7430. Two latent original bugs kept and commented: with a patch file the PC
      build's disc-cache stub frees the block and carries on with null (crash); with 100 archives loaded it
      writes slot -1. Late self-test: Themes.pre (all compressed) and ParkEdAnims.pre (mixed) loaded by the
      original and ours, repacked blocks compared byte for byte. thug2_check: 204, 202 match, 2 stubs.
- [x] thug2_check runs the early self-tests offline (2026-10-15): it starts a suspended copy of itself,
      reserves 0x400000 there before the Windows loader maps anything, loads THUG2PM.exe's image (imports from
      System32 only - the game folder has our dinput8.dll) and runs the argument-size check and the Early
      stage. All 24 early tests pass. Late/EngineStarted tests still need the game.
- [x] Batch 28 (NOT run in the game yet; early self-tests pass offline): the async file system's base
      class and static side - CAsyncFileHandle Init/SetCallback/GetFileSize/Cancel/IsBusy/Read/Open/Close/
      io_callback, constructor/destructor, the 17 base platform stubs (vtable slots 2-18, names from their
      STUB strings), and the static functions: AsyncInit, AsyncBusy, the 16-handle pool (GetHandle/FreeHandle),
      the double-buffered callback lists (AddCallback/SwapCallbackLists/ExecuteCallbacks), WaitForIO, Open,
      Close. Virtual calls go through the object's own (game) vtable. Pip's "priority 8" was the callback
      mask (bit 3 = read completed). Early self-test with fake handles compares pool order, counters, handle
      fields and the callback sequence (shown to catch a deliberate pool bug).
- [x] Batch 29 (NOT run in the game yet; early self-tests pass offline): CPcAsyncFileHandle (vtable
      0x6583D8) - io_callback override (size/position bookkeeping, temp-buffer copy), PlatInit (returns
      false, as the original), PlatOpen (data\ path rules shared with File::OpenOnDisk via MakeDiskPath;
      overlapped + unbuffered on NT 6+), PlatClose, PlatIsDone (polls OVERLAPPED.Internal != 0x103),
      PlatIsBusy/IsEOF, PlatLoad, PlatRead (2 KB-rounded; blocking spins on IsDone; an immediate
      completion is counted done without a callback - as the original), destructors, the I/O thread
      0x5B9B40 (66 ms poll) and PlatformInit 0x5B9B80. Early self-test opens/reads/closes pre\Themes.prx
      through an original handle and ours, synchronous and overlapped (shown to catch a deliberate bug).
- [x] Batch 30 (NOT run in the game yet; early self-tests pass offline): the async loader engine module
      (vtable 0x658158, 0x18 bytes; instance 0x7CF190, refs 0x7CF194): Create 0x5B6CA0 / Destroy
      0x5B6620 (reference counted), constructor 0x5B6BC0 (Mdl::Module base + a Tsk::Task, vtable
      0x6476F0, running 0x5B6990), destructors 0x5B6660/0x5B6940, Start 0x5B6970 (adds the task to the
      main loop's logic tasks), the task 0x5B6990 and the handle poll 0x5B9780. Early self-test compares
      the reference counting (no module) original vs ours (shown to catch a deliberate bug).
- [x] Batch 31 (NOT run in the game yet; lookup test passes offline, split test is Late): File::AssetBundle
      (vtable 0x65822C, 0x2354 bytes; made by the level loader 0x4678A0): one file {unknown, count,
      count x {offset, size, name crc, type crc}, data} loaded with Pip::LoadFileInto and split into one
      Mem::Malloc block per asset (last first, shrinking the file block), plus the lookups. Constructor
      0x5B6CF0, destructor 0x5B6EA0 (doesn't free assets), SetHeaps 0x5B6D30, Load 0x5B7090, Split 0x5B6EF0,
      IsLoaded/GetAsset/GetEntry/FindEntry/FindAsset/HasAsset/FreeAsset 0x5B6D50-0x5B6E60. Early test of the
      lookups (shown to catch a deliberate bug); Late test splits a handmade bundle, original vs ours.
      Also fixes batch 30: CreateAsyncLoaderModule returns the module (the start-up code 0x4E1EF0 keeps
      it) - caught by the register scan; the refcount test now compares the return value.
- [x] Batches 26-31 tested in the game (2026-10-15, by the user): game runs fine; log - 276/276 replacements
      installed, early 28/28, late 10/10 (incl. TestAssetBundleSplitLate, TestPipLoadPreLate), engine-started
      2/2 (incl. TestLoadPreEngineStarted), shadow checks 1395 calls with no mismatches.
- [x] Batch 32 (NOT run in the game yet; early test passes offline, contexts test is Late): the memory
      manager's lowest layer (src/game/core/alloc.*). Block header {allocator, size, marker 0xCF801FE1 /
      0xDEADDEAD, ?} helpers 0x5BA080-0x5BA0B0; Region (vtable 0x658A58: start, min available, end,
      allocators[up, down]) ctor 0x5BC5F0, Add/RemoveAllocator, MemAvailable, Allocate 0x5BC560 (3 args,
      the 3rd unused - caught by the argument-size check); AllocRegion (0x658A5C) ctor/dtors; Allocator
      base (vtable 0x65845C, 0x64 bytes: region +0x24, direction +0x28 (+1 up / -1 down), top +0x2C,
      context +0x30, name +0x34, context list head +0x38, base context +0x4C) ctor 0x5BA260, dtor
      0x5BA140, Push/PopContext 0x5BA1E0/0x5BA360; list node unlink/dtors 0x5BA0E0/0x5BA100/0x5BA230;
      0x5BA4E0. Early test: arenas compared original vs ours (shown to catch an off-by-one after adding
      an exact-fill step); Late test: context stack + AllocRegion on the real manager.
- [x] Batch 33 (NOT run in the game yet; early test passes offline): Mem::Heap (src/game/core/heap.cpp;
      vtable 0x65848C, 0x98 bytes: +0x6C use fallbacks, +0x70 fallback heaps[5], +0x84 reserves[5]).
      Ctor 0x5BA520, dtor 0x5BA560, Allocate 0x5BA780 (best fit from the context's free list with split,
      else the region, else the fallbacks; 0x7D0FDC busy flag, 0x7D0FE0 debug forced block), LargestFree
      0x5BA4F0, ReallocateDown/Up/Shrink 0x5BA9A0/0x5BAA20/0x5BAAE0 (Shrink returns the block - Ghidra
      said the size), Free 0x5BA5A0 (address-ordered, merges, gives the top back to the region). The
      allocator statistics (+4..+0x20) are named. Early test: up/down/fallback heaps in an arena, the
      original methods vs ours via vtable copies, arena compared (four deliberate bugs all caught; Free's
      manager lookup uses a zeroed stand-in manager offline).
- [x] Batches 32-33 tested in the game (2026-10-15, by the user): works; log - 305/305 replacements, early
      30/30, late 11/11 (incl. TestAllocatorContextsLate), engine-started 2/2, shadow checks clean.
- [x] Batch 34 (NOT run in the game yet; early test passes offline): Mem::Manager (src/game/core/manager.*),
      everything but heap creation and start-up. Layout: heaps registry [48] +0 / count +0xC0, current
      +0xC4 into the context stack [16] +0xC8 (depth +0x108), main region +0x10C, TopDown +0x110,
      BottomUp +0x114, FrontEnd/Script/Network/NetMisc/Internet/Cutscene/Debug region+heap pairs up to
      +0x154, skater/skater-geom regions and heaps [8] +0x160..+0x1DC, named heaps [7] x 0x90 +0x1E0,
      SkaterInfo +0x5D0, Theme +0x5D8. Allocate/AllocateZeroed/ReallocateUp/ReallocateShrink/Free/
      Available, Push/PopContext, CurrentHeapName, First/NextHeap, DeleteHeap, Find/Get/DeleteNamedHeap,
      GetHeap/GetHeapName (by checksum), DeleteNetMisc/Cutscene/ExtraSkater/StandardHeaps,
      DestroyMainHeaps, and the globals Malloc/MallocAvailable/ReallocateShrink/FreeMalloc/MallocSize/
      Realloc (28 functions). mem.cpp now calls the manager directly (ManagerHeapAt removed). Early test:
      a stand-in manager in an arena, original vs ours (five deliberate bugs caught, three after
      strengthening the script).
- [x] Batch 35 (NOT run in the game yet; early test passes offline): creating the memory manager and its
      heaps. Manager is 0x5E8 bytes at 0x7D11F8 (+0x5E0/+0x5E4 unknown); its constructor 0x5BBC00 mallocs
      64 MB with the game CRT (0x624031; bounds 0x7D17EC/E4/E8) and builds the main region and the
      Top Down / BottomUp heaps in static storage (0x7D11E0 / 0x7D1130 / 0x7D1098). SetUpManager
      0x5BC2C0 (bottom-up current), EnsureManager 0x4E1950 (flag 0x7CC979), ShutDownManager 0x5BC340,
      InitStandardHeaps 0x5BBE80 (frontend region 1,000,000 or 1,100,000 by flag 0x7CEFAD; script,
      skater info, theme, skater, skater geom, two named texture heaps), AddHeap 0x5BB4E0, InitNetMisc/
      Internet/Cutscene/ExtraSkater/NamedHeap. Early test: a stand-in manager in a 12 MB arena, originals vs
      ours, then shutdown, compared page by page (five deliberate bugs caught). The constructor itself
      is only exercised by starting the game.
- [x] Batches 34-35 tested in the game (2026-10-15, by the user): game runs; log - 344/344 replacements, early
      32/32, late 11/11, engine-started 2/2, shadow checks clean. The memory manager is entirely ours.
- [x] Batch 36 (NOT run in the game yet; early test passes offline): the script system's structures, read side
      (src/game/script/struct.*). CComponent (16 bytes: type<<1 byte, name checksum, value, next), CStruct
      (8 bytes, head at +4), symbol table 0x6F3624 (0x1000 buckets; entry type +1, name +4, value +0xC,
      next +0x10), ESymbolType (1 int, 2 float, 3 string, 4 local string, 5 pair, 6 vector, 10 struct,
      12 array, 13 name). LookUpSymbol 0x478CF0 (follows aliases), Init, IsEmpty, GetNextComponent,
      FindComponent/FindLastComponent, Search 0x4764B0 (last match wins; int->float; names standing for
      global values; included global structures), ContainsComponentNamed/ContainsFlag (by checksum and
      name), References, IsSubsetOf, and every typed getter (string, integer, float, pair x2, structure,
      array, checksum, checksum-or-string, vector) in its int-assert, bool-assert, checksum and string
      forms - 44 functions. params.h now calls CStruct directly. Early test: a stand-in symbol table and
      handmade structures, every getter over 19 names, originals vs ours (six deliberate bugs caught, one
      after adding an include-last case).
- [x] Batch 37 (NOT run in the game yet; early test passes offline): owning script values. Mem::PoolPair (a
      class's two item pools: current +0, pools +8) for components 0x6F2338, CStruct 0x6F3614, CArray
      0x6F22B4, CPair 0x6F3644, CVector 0x6F3654; NewStruct/DeleteStruct, NewArray/DeleteArray;
      CreateString 0x4763A0 (script heap, or the permanent string cache 0x6F3570.. shared by checksum - its
      EAX-convention helper 0x476260 folded in) / FreeString; CArray (12 bytes; one element stored in place)
      Init/Clear/GetArrayPointer; CleanUpComponent 0x4772A0, CleanUpArray 0x477140, CopyComponent 0x4773E0
      (deep, or by reference -> types 20/21 struct/array pointers), CStruct::Clear 0x477090,
      RemoveComponent (checksum/name), RemoveComponentOfType. Early test: a stand-in manager with a script
      heap, the script pools emptied; every owning type built, copied, removed, cleaned; arenas compared
      (six deliberate bugs caught, one after giving the test array a type).
- [x] Batch 38 (NOT run in the game yet; early test passes offline): adding to script structures.
      NewComponent (0x44C130 + constructor 0x46D140, which leaves byte 1 alone), DeleteComponent 0x44C170,
      AddComponent 0x477920 (replaces same name + compatible type: equal types, int<->float; a bare name
      only replaces the same name; appends), RemoveFlag, and every adder - string, local string, string of
      type, integer, float, checksum, vector (x,y,z and by value), pair, array/structure pointers,
      structure copy, compiled script, of type (1/2/11/12/13) - by checksum and name; AppendStructure,
      AppendStructureIfAny, Assign, ExpandInto (expands included global structures). 32 functions. Early
      test: stand-in manager + symbol table, originals vs ours (six deliberate bugs caught, one after
      adding a named component with a flag's value). Register scan flags 0x4EFF92 'PUSH ECX' after
      AddVector by value: a false alarm (it only reserves a stack slot, overwritten by FSTP [ESP]).
- [x] Batches 36-38 tested in the game (2026-10-16, by the user): game runs.
- [x] Batch 39 (NOT run in the game yet; early test passes offline): script arrays and copies.
      CArray::SetSizeAndType 0x46CC50 (frees the old block; 0 -> type none; one element in place; more ->
      zeroed block on the script heap), Resize 0x46CCF0 (grow only - the original overruns when shrinking;
      growing to 1 leaves a block pointer in place of the value), SetElement 0x46CB10, SetFloat 0x46CB40,
      GetElement 0x46CC30 (folded getter for every 32-bit type, 218 callers), GetString 0x46CBB0 /
      GetLocalString 0x46CB70 (type asserts compiled out), GetFloat 0x46CBF0 (ST0; integers converted);
      CopyArrayElements 0x4787A0 (cdecl dest, src, count, type; raw for int/float/name, deep for strings,
      pairs, vectors, structures, arrays; null elements skipped; other types copy nothing), CopyArray
      0x478A60; CPair/CVector operator new 0x46D410/0x46D3D0 and constructors 0x47AC00/0x47ABF0;
      CStruct::InitCopy 0x478AB0 (copy constructor; counts copies at 0x6F3610, peak 0x67DA30). 15 functions.
      Early test: every element type built, read, resized and deep-copied (also over full arrays), on a dirty
      script heap; originals vs ours (12 deliberate bugs caught, two after dirtying the heap).
- [x] Batch 40 (NOT run in the game yet; early test passes offline): the symbol table and global values.
      CreateSymbolTable 0x478C90 (0x1000 buckets from the current heap), FindSymbol 0x478CC0 (no alias
      following), CSymbolTableEntry::Init 0x4790F0 (byte 0 bit 0 = in use), CreateSymbol 0x479110 (symbol
      pool pair 0x6F362C), LinkSymbol 0x478DC0, UnlinkSymbol 0x478D60, GetNextSymbol 0x478E50 (iterator state
      0x6F363C entry / 0x6F3640 bucket); global getters by checksum and name - GetInteger 0x478F60/0x4790C0
      (floats truncated), GetFloat 0x478F00/0x478F30 (ST0), GetSymbolValue 0x478F90/0x478FB0 (any type),
      GetString 0x478FD0/0x478FF0 (missing -> "" at 0x6469B4), GetStructure 0x479020/0x479040, GetArray
      0x479070/0x479090; ComponentsEqual 0x4791A0 (thiscall on the first component; names standing for
      symbols resolved; int/float compared exactly; strings by strcmp; pairs/vectors by coordinates;
      structures/arrays only when the same one - the array comparison 0x49F4B0 is `return false`);
      CStruct::AddArray 0x478B00/0x478C50 (a copy); the compiled-out type-name stub 0x479190. 23 functions.
      pre.cpp now calls Script::GetInteger. Early test: a symbol table built through both, iterated, unlinked,
      every getter over 8 names, a 20x20 ComponentsEqual matrix (NaN, aliases, int/float), AddArray; arenas
      compared (18 deliberate bugs caught, one after comparing an alias with an equal float).
- [x] Batches 39-40 tested in the game (2026-10-16, by the user): verified.
- [ ] Next: the script parser/loader (0x46F...) and the interpreter (0x46D450); the CArray functions between
      0x46CD90 and 0x46D3D0.
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

## PartyMod port (2026-10-15)
- [x] Step 1 done, TESTED IN-GAME 2026-10-15 (game runs with partymod.dll removed; log shows both PartyMod lines and all self-tests passing): PartyMod v1.0.4 in src/partymod (MIT, notice kept), built as a static lib with SDL2 2.30.9 fetched by CMake; our DllMain calls partymod_attach() and redirects the exe's LoadLibraryA("partymod.dll") to our own name. Builds cleanly.
- [ ] Step 2 (ongoing): turn PartyMod's byte patches into proper rewrites as we reach the systems they touch.
