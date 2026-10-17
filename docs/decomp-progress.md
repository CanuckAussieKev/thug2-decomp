# Decompilation progress

Restart rule: continue the first batch not marked done. A batch is done when it builds cleanly and is committed
(one commit per batch). In-game testing happens later; a batch that breaks the game gets reverted by its commit.
Batches marked (untested) haven't been run in-game yet.

**2026-10-16 (user):** the end goal is to compile the game itself, not only to inject a DLL - so no game
function is skipped. Functions noted below as "left as original", "too small" or "patched by PartyMod, keep
original" are TO-DOs: write them; register unhookable ones with `REPLACE_SOURCE_ONLY` (compiled, counted,
argument-checked, not hooked). Compiler-generated helpers still need source in some form (a C++ construct
that the compiler emits, or a plain function).

## Core utilities (0x401000-0x406460, 130 functions)

- [x] Batch 0: `Crc::GenerateCRCFromString` 0x401B90 (commit 55ac891)
- [x] Batch 1 (untested): 0x402320-0x402600 -> `Mth::Vector` + `Mth::Line` ctor (14 functions,
      `src/game/mth/vector.cpp`), with self-tests.
- [x] Batch 2 (untested): CRC buffer/extend (0x401AA0, 0x401BE0), `Mth::Matrix` copy + X/Y/Z rotation builders
      (0x404090, 0x404DD0-0x404E70), `Vector::operator-=` (0x406050), `Vector::ProjectToPlane` (0x406080).
      0x401020 is compiler-generated (`vector constructor iterator'): the compiler emits it - see batch 48.
- [x] Batch 3 (untested): `Util::BitStream` ctor/SetInputData/SetOutputData/GetByteCount/Flush
      (0x401050-0x4010C0, 0x401180), `Str::LowerCase`/`UpperCase` (0x402080, 0x4020B0), `Mth::Clamp` (0x4022C0),
      Vector(x,y,z) ctor (0x4022F0), Vector/Matrix `operator[]` (0x402310, 0x404080).
      Deferred until their callers are understood: 0x401D80 (assigns IDs from a global counter at 0x690FA0),
      0x403210 / 0x403220 (each zero one field of an unknown class) - done in batch 48.
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

**Core utilities block: DONE - 129 of 130 functions rewritten** (0x403210 / 0x403220, first left as
original, done in batch 48). The last one, 0x401020, is the compiler's `vector constructor iterator' (calls a
constructor on each element of an array): building the game from source makes the compiler emit it again, so it
needs no source of its own (docs/library_ranges.md).
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
      deliberate FastSin error); the overlap check. At engine start 7 of the 28 have been
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
- [x] Batch 41 (NOT run in the game yet; early test passes offline): the script expression evaluator
      (src/game/script/eval.*). CExpressionEvaluator (0x1F8 bytes: values[20] CComponent, value top +0x140,
      operators[20] {op, open parentheses} +0x144, operator top +0x1E4, has-operators +0x1E8, token +0x1EC,
      error +0x1F0, +0x1F4 set by the constructor): constructor 0x46D380, Clear 0x46D1C0, ClearIfNeeded 0x46D220,
      SetTokenPointer 0x46D250, SetError 0x46D260, PushOperator 0x46D2B0, Input(value) 0x46D310,
      OpenParenthesis 0x46D370, Input(operator) 0x46EB70 (precedences at 0x67D908), CloseParenthesis 0x46EBF0,
      GetResult 0x46EC40, and Execute 0x46D450 (5.5 KB): operators 5 [], 7 ==, 8 . (dot product / structure
      member), 10 -, 11 +, 12 /, 13 * (and vector cross product), 18 <, 20 >, 50 ||, 51 && (bitwise on the
      raw integer values) over integers, floats, checksums, strings, local strings, pairs, vectors,
      structures (+ merges, - removes names / arrays of names / another structure's parameters) and arrays
      (+ concatenates as the first array's type); the result goes through the global component 0x6F2348;
      errors are the game's own strings (after an error the value top ends at -1, as in the original).
      Component helpers IsReference 0x46D160, MakeReference 0x46D180, MakeOwning 0x46D1A0, GetBool 0x46D280.
      Mixed integer/float arithmetic is done in double then rounded, matching the x87 at 53-bit precision.
      Still called through the game: array element to component 0x47A910, < 0x479410, > 0x4795A0.
      17 functions. Early test: ~110 expressions fed through the original evaluator (all its functions
      forced original with OriginalCode) and ours - every operator/type pair, every error, precedence,
      parentheses, both stack overflows, order-sensitive dot products, the 200-character string limit;
      results, error pointers, stack state, the evaluator's memory and the arenas compared (24 deliberate
      bugs caught, 6 after strengthening the test; 2 more were equivalent to the original).
- [x] Batch 42 (NOT run in the game yet; early test passes offline, plus a late test that runs in the game):
      the rest of what the evaluator calls, checksum names and script function registration.
      LessThan 0x479410 / GreaterThan 0x4795A0 (members of the first component; names standing for symbols
      looked up; numbers by value, NaN never; strings and local strings by their LENGTHS; structures by one
      being a subset of the other), CopyArrayElementToComponent 0x47A910 (cdecl; local strings and other
      types leave the component empty). src/game/script/names.*: the checksum-name lookup (on when 0x7CEFAC
      is set; table 0x6F232C of 0x10000 buckets x 10 {checksum, name}, names block 0x6F2330/next 0x6F2334)
      - AllocateChecksumNameLookupTables 0x46CF00 (untested: 10 MB), AddChecksumName 0x46CF60 (a full bucket
      runs into the next), FindChecksumName 0x46D0C0, FindChecksumNameOrUnknown 0x46D110 ("Unknown(0x%08x)"
      in 0x6F22C8; params.h's FindChecksumName now calls it), ParseChecksumNames 0x46CD90 (.chk lines
      "0x<8 hex> <name>

"), LoadChecksumNames 0x46CFE0 (untested: needs a .chk file);
      RegisterCFunctions 0x46EFD0 (type 8), RegisterMemberFunctions 0x46F020 and RegisterMemberFunctionTable
      0x46F060 (type 9). 12 functions. Tests: the expression test now forces these originals too and has ~60
      more expressions (comparisons, every array element type); an early checksum-names/registration test;
      and a late test (in the game - the original uses the game's C runtime, which the offline checker
      doesn't start) for adding a known checksum and the Unknown(...) text. 23 deliberate bugs caught, 5 after
      strengthening the tests.
- [x] Batches 41-42 tested in the game (2026-10-16, by the user): game runs fine; log - 501/501 replacements,
      early 39/39, late 12/12 (including the checksum-names late test), engine-started 2/2.
- [x] Batch 43 (NOT run in the game yet; early test passes offline): the .qb token toolkit and the script
      pools (src/game/script/qb.*; the token formats are listed in qb.h). SkipToken 0x46F690 (sizes of every
      token; 0x00/0x10/0x19/0x1D/0x2A/0x31 and unknown tokens don't step), Read4Bytes 0x46F7E0, Read2Bytes
      0x46F800, Write4Bytes 0x46F810, Write2Bytes 0x46F840, SkipEndOfLines 0x46F850, SkipToEndOfStatement
      0x46FB30 (brackets {} [] () balanced; ends at 0x01/0x02/0x00), SkipToEndOfScript 0x46F920,
      CalculateScriptChecksum 0x46F960 (CRC of the tokens without line ends, up to '$'), PreProcessScript
      0x46FF00 (statement-start names of C functions -> 0x43 + function, member functions -> 0x46 + table
      entry or 0x44); the .qb files' own checksum names (table 0x6F2368, 0x1000 inline buckets {checksum,
      name, next}): AddScriptChecksumName 0x46FA60, FindScriptChecksumName 0x46F9B0, FreeScriptChecksumNames
      0x46F9F0; ForgetStoredRandomsOf 0x46F870 (the stored-random list 0x6F2358 - see batch 45);
      CStoredRandom::Shuffle 0x46F8A0; CreateScriptPools 0x46F520 with the nine per-class pool creators
      0x46F090..0x46F4A0 (CScriptCacheEntry 0x14 x300 0x6F3560, CComponent 0x10 x86000, CStruct 8 x19800,
      CVector 0x10 x9500, CPair 0xC x800, CArray 0xC x7000, CSymbolTableEntry 0x14 x12500 0x6F362C, CScript
      0xF4 x300 0x6F3548, CStoredRandom 0x60 x100 0x6F2578) and AllocatePermanentStringHeap 0x476210
      (190000 bytes, 7000 strings). 26 functions. Early test: a token stream of every kind, statements
      with nested brackets, pre-processing against a stand-in symbol table, the .qb name table, the cache,
      shuffles with a fixed random state, two pools and the string heap in a 128 KB arena (24 deliberate
      bugs caught, 2 after adding cases; one more would loop forever on the new case). CreateScriptPools
      itself is only exercised by starting the game.
- [x] Batch 43 tested in the game (2026-10-16, by the user): game runs.
- [x] Batch 44 (NOT run in the game yet; early test passes offline): symbol bookkeeping for the .qb
      loader (qb.cpp). AllocateScriptChecksumNames 0x470010 (the .qb name table, 0xC000 bytes, from the
      script heap or - with its argument - the debug heap), FreePair 0x44C1D0 / FreeVector 0x44C230 /
      FreeStoredRandom 0x470070 / FreeSymbol 0x4700D0 (pool pair frees), CleanUpAndRemoveSymbol 0x4711D0
      (unlink; entries with byte-0 bit 2 set are only unlinked; frees the value by type), the pending list
      (0x6F2588 {symbol, owned} x count 0x6F3528): RemoveSymbolLater 0x4727B0, ReplaceSymbolLater 0x4727F0,
      ProcessPendingSymbols 0x472830 (still reads a freed symbol's type, as the original does; the script
      cache invalidation 0x475FD0 is still the game's), and the flag 0x6F2564 set/clear 0x46FFF0/0x470000.
      11 functions. Early test: every symbol type removed, a kept one, pending removal + replacement, pooled
      and loose frees, both heap choices (14 deliberate bugs caught, 2 after giving the test a separate
      debug heap and pooled pairs/vectors).
- [x] Batch 45 (NOT run in the game yet; early test passes offline): the .qb parser core (src/game/script/
      parse.*). ParseValue 0x4706F0, AddParameter 0x470130 (ECX token, EDX name, two caller-popped stack
      arguments: a naked stub), ParseArguments 0x470F30 (a call's arguments, to the line end / ')'),
      ParseStructure 0x471860 (also merges nested {}), ParseArray 0x471B00 (element type: the last kind
      seen, but integers don't override floats; the element loops were code Ghidra never disassembled - read
      with capstone), ParseExpression 0x4712B0 (drives the global evaluator 0x6F2370 through the global
      component 0x6F2568; "a -1" is a subtraction; C functions are called inline with the arguments that
      follow and give their result byte; names after '.' aren't looked up), ResolveRandomJumps 0x470C60
      (jumps 0x2E/0x49; Random 0x2F/0x37 weighted - signed 16-bit weights, second random stream for 0x37;
      RandomNoRepeat 0x40 and RandomPermute 0x41 with per-script stored state), the stored randoms (list
      0x6F2358 newest / 0x6F235C oldest / count 0x6F2360, at most 100; CStoredRandom laid out: owner, table,
      count, type, index, order, last, next, previous): NewStoredRandom 0x470BB0, FindStoredRandom 0x46F7A0
      (EBX/EDI/ESI: stub), SkipValue 0x46FD60 (EAX: stub), ParseRandomRange 0x46FBD0 (EAX, BL = second
      stream, one caller-popped argument: stub; integers when both ends are within 1e-5 of whole numbers
      after the game's 0.1 nudge, else a float), ResolveNameComponent 0x479730, GetCurrentScript 0x4728C0
      (0x6F3530), GetNextScript 0x4728D0. Found: the 0x6F2358 list of batch 43 is the stored randoms
      (ForgetCachedScriptsOf renamed ForgetStoredRandomsOf). The original parser never moves past some
      tokens in some places (e.g. a hex integer as a parameter, unknown tokens in arrays/expressions) and
      loops forever; kept as it is. 18 functions (4 with assembly stubs). Early test: a small .qb assembler
      builds ~60 token streams (structures with every parameter kind, arrays of every type, ~35 expressions,
      call arguments, <name>/<...> references with and without caller parameters, all random block kinds over
      four passes with an owner change, jumps, scripts) parsed by the original parser (all of it forced
      original) and ours, plus the four stubs called with their register arguments; results, positions,
      the stored randoms and the arena compared. 41 deliberate bugs: 40 caught (6 by hanging, 14 after
      adding cases), 1 equivalent to the original (the 0.1 vs 0.5 nudge can't change a result).
- [x] Batches 44-45 tested in the game (2026-10-16, by the user): game runs.
- [x] Batch 46 (NOT run in the game yet; early test passes offline): loading .qb files (src/game/script/
      load.*). ParseQB 0x472420 (filename, data, unused, useQbNames, temporary): frees the .qb name table
      (and makes a new one with useQbNames), pre-scans the checksum names (0x2B), records the file's own name
      and sets the loading-file checksum 0x6F2364; then on the script heap defines each "name = value" and
      each script, replacing old definitions (temporary: the old symbol set aside and new symbols listed for
      ProcessPendingSymbols to undo); an unchanged script (same checksum) is kept; 0x45 section markers
      0x70CB0238 / 0x0FA33D9B bracket a debug section (on the debug heap while checksum names are kept, else
      skipped). ParseSymbolDefinition 0x472240 (ECX token, name and file name on the stack, caller pops: stub)
      and CreateScriptSymbol 0x46FE40 (EAX size, EBX code, three caller-popped arguments: stub; scripts are
      stored as {checksum, size, stored size, code}, LZSS-compressed when that's smaller, built in a 20000-byte
      buffer). RestartChangedScripts 0x46ED20 (symbols' byte-0 bit 0 = new; restarts the scripts running or
      calling a new script - CScript::Restart 0x4747F0 is still the game's), ScriptRefersTo 0x472C50 (a
      CScript's current script +0xC8, call depth +0x30, frames +0x94 of 0x30 bytes), LoadQBData 0x46ED90,
      LoadQB 0x46EDE0 (pre-compiled files - extension from 0x5B6040 - still go to the game's 0x46FBA0; others
      are loaded on the top-down heap, parsed and unloaded). 7 functions (2 with stubs). Early test: three
      generated .qb files (every definition kind, compressible and incompressible scripts, debug sections
      with names off and on, names new in each file, a C function called while loading) through seven
      loads (plain, again, temporary + pending, names on, useQbNames, LoadQBData), ScriptRefersTo and the
      restart pass over fake scripts, and both stubs - original vs ours in a 512 KB arena. 24 deliberate bugs
      caught (4 after adding cases). LoadQB itself (files, pre-compiled format) is only run by the game.
- [x] Fix (2026-10-16, after the user saw debug assertion / run-time check failures with batch 46): 0x475FD0
      (drop a script from the script cache) is a thiscall method of the cache object 0x6F3558 that pops its
      argument; batches 44 and 46 called it as cdecl without the object, leaving ECX garbage and the stack 4
      bytes off - caught in the Debug build's ESP check. Now RemoveFromScriptCache (qb.cpp) calls it on the
      cache. The tests had the cache off; they now use a stand-in cache (an empty hash table), and the old
      call crashes TestScriptLoading at 0x475FD6. Debug builds now also log debug-CRT reports and run-time
      check failures (with a call stack) to thug2-decomp.log before showing the usual dialog.
- [x] Batch 47 (NOT run in the game yet; early test passes offline): the rest of the file-system block
      except the input-device code. PC memory cards (src/game/mc/memcard.*): saves are Win32 files under
      Save\. Mc::Manager (0x90 bytes: slots[4][2] {port, slot}, the one Card at +0x80) - Init 0x5BC8D0,
      GetCard 0x5BC730 (any port/slot), CreateManager 0x5BC900 / ReleaseManager 0x5BC950 (instance 0x7D1890,
      references 0x7D1894; Calloc on the default heap); Mc::Card - Open 0x5BC9B0 (thiscall, 3 args; mode 1
      read / 2 write / 3 both on existing files, 4 a new file, 6 write created if needed, 5 nothing; '/' ->
      '\' into the path buffer 0x7D1810; no sharing), RemoveFile 0x5BC770 (DeleteFileA), ConvertFilename
      0x5BC740 (a copy in 0x7D1898), GetFreeBlocks 0x5BC790 (16 KB blocks), GetFileList 0x5BCAE0
      ("Save\<pattern>", non-directories appended with local date/time and size), AlwaysTrue 0x5BC7F0 (folded);
      Mc::File (0x94: a Lst::Node, name +0x14, second..year +0x3C-0x42, size +0x44, handle +0x8C, card
      +0x90) - Seek 0x5BC800, Flush 0x5BC840, Write 0x5BC850, Read 0x5BC880, Close 0x5BC8B0. Also: node
      destructors for two more node types 0x5BC990 / 0x5BCD30 (ListNode::UnlinkAs), the owner destructor
      0x5BCD00 (Mem::DestroyOwner), Region::DestroyBody 0x5BC4D0, PoolManager::GetPool 0x5BC3B0 (only pools
      0 and 1 exist: past them is the path buffer). Source only (REPLACE_SOURCE_ONLY, new this batch - the
      game is to be built from source, so nothing is skipped): the folded stubs ReturnTrue 0x5B9760 (+ its
      jump 0x5B6570), ReturnFalse 0x5B9E50, ReturnZero 0x5BA1D0, IgnoreArgument 0x5BAB90 (src/game/core/
      stubs.*), and the jumps 0x5B9DF0 (-> AsyncInit) / 0x5BA590 (-> Allocator::DestroyBody). 27 functions.
      Early test: in a scratch folder (never the game's Save\), every open mode, reads/writes/seeks/flush/
      close (also failing ones), a second open of an open file, deletes, the converted path, the manager's
      references after each create/release, the node/owner/region destructors, the pool lookup, the stubs;
      original vs ours (handles cleared before comparing). The listing's original uses the game's sprintf
      (can't run offline): ours is checked against what Windows lists. 28 deliberate bugs caught (7 after
      strengthening the test). Left in the block: the input-device code 0x5BCC60-0x5BDA20 (PartyMod
      reworks input - read its source first).
- [x] Batch 47 tested in the game (2026-10-16, by the user): game loads fine.
- [x] Batch 48 (NOT run in the game yet; early test passes offline): the leftovers - functions skipped before
      the goal became building the whole game, found with the new `tools/ghidra/find_gaps.py` (un-rewritten
      functions between rewritten ones, and jumps to rewritten functions). Mth: Sphere() 0x403210 (a vector and
      a 32-bit value cleared - assumed a sphere until its users are known) and Capsule() 0x403220 (radius 0;
      Line got a default constructor). Script: CArray::SetType 0x46CAF0; CreatePermanentString 0x476260 (string
      in EAX, only CreateString calls it: registered source-only); BeginPermanentStrings 0x476300 /
      EndPermanentStrings 0x476310 (on while the startup scripts load, 0x5A59D0; the lookup table freed after);
      SetScriptString 0x476370 / GetScriptString 0x476390 (128-character slots at 0x6F3590; the string is
      sprintf's format, as in the original). Sys::WriteDefaultSetting 0x4E1810 (src/game/sys/registry.*):
      WinMain's default DWORD settings under HKCU\Software\Activision\Tony Hawk's Underground 2\Settings,
      written only when missing; without that key the original creates a key "Settings\<name>" - kept. Ten
      jumps to rewritten functions (functions of their own whose body is just the call), source-only:
      0x41CB80 -> CArray::GetElement, 0x4504F0 -> ReturnFalse, 0x472E60 -> GenerateCRCFromString, 0x476620 ->
      CStruct::GetString, 0x477130 -> CStruct::Clear, 0x478460 -> AddVector, 0x478C80 -> AddArray, 0x47AE10 ->
      ReturnZero, 0x5144C0 -> IgnoreArgument, 0x54FFE0 -> String's destructor. 19 functions. Early test:
      constructors (bytes + returned pointer), SetType, the string switches, slot addresses, the jumps, and the
      registry defaults with the key present under a stand-in HKEY_CURRENT_USER (RegOverridePredefKey onto a
      scratch key - never the real settings). Late test (in game - the originals use the game's sprintf):
      SetScriptString, and the registry defaults without the key. 9 deliberate bugs caught.
      Not leftovers (planned work): 0x46EEC0 (unloads a .qb's scripts - needs CScript), 0x46FBA0 (pre-compiled
      .qb), CScript 0x4728F0-0x472E20, 0x47AC10-0x47AD80, the input-device code 0x5BCC40-0x5BCCC0.
      Also: 0x4E1220 (ParseNetworkParams) and 0x44EEF0 (MainLoop) are hooked only by the test runner - they
      still need source (progress counts only src/game, so they were never counted).
- [x] Compiled-out debug calls restored (2026-10-16, at the user's request): every call the rewritten functions'
      originals make to the empty function 0x5480A0 (61 sites) is back in the source, under its use's name, as
      an empty inline function: Mem::PushMemProfile / PopMemProfile around allocations (the script pools in
      CreateScriptPools, with names such as "CComponent and Reserve CComponent"; .pre loads and unpacking),
      Script::PrintContents on a typed getter's miss with assert set, Mem::DebugTrackBlock on the heap's
      blocks before/after each change. No behaviour change. New code must keep doing this
      (tools/ghidra/find_stubs.py lists the empty functions).
- [x] Batch 49 (NOT run in the game yet; early test passes offline): SIO, part 1 (src/game/sio/device.*) -
      the controller ports. Sio::Device (0xF0 bytes, vtable 0x658A6C; PS2-format control data at +0x1C,
      motor levels +0x5C / maxima +0x7C / saved +0x9C, pause count +0xBC, actuators-disabled +0xC0,
      capabilities +0xC4, state +0xD4 (0 idle, 1 detect, 2 ready, 5 settle) and the state after settling
      +0xD8, plugged in +0xE0): constructor 0x5BD920 (allocates its list node; reads the never-set +0xD0 for
      the PS2 DMA buffer; rand() & 63 retry delay), destructor 0x5BCD00 (was batch 47's "owner destructor"),
      Acquire 0x5BCC70, Detect 0x5BD8B0 (analogue pad, type 7, two motors), Settle 0x5BCCC0, IsPluggedIn
      0x5BCC60 (returns the byte as is), ActivateActuator 0x5BDA20 (x87 max * percent * 0.01f, truncated; the
      same * 256 for the motor; with PartyMod's change - no ReadData call), Enable/Disable/Reset/Pause/Unpause/
      StopActuators 0x5BCCB0 / 0x5BDAD0 / 0x5BDB30 / 0x5BDB60 / 0x5BDBD0 / 0x5BDC60, Process 0x5BDCC0
      (source-only: PartyMod's processController, which the game runs; the original's state machine is in the
      comment). A pad is present if the port's DirectInput joystick reads (0x5F2E00) or it has key bindings
      (0x5F2F80 checks the first 23 of the port's table at 0x7D6770). Keyboard: ReadKeyboard 0x5BDD60
      (DirectInput state double buffer at 0x7D1C20, re-acquires when lost), GetKeyboardChars 0x5BDD30 /
      ClearKeyboardChars 0x5BDE20 (+ their jumps 0x5BCC40 / 0x5BCC50). The manager singleton (0x7D1E20,
      references 0x7D1E24): CreateManager 0x5BE670 (returns the instance - two callers use it; its constructor
      0x5BE1F0 is next batch), ReleaseManager 0x5BDCF0. The destructors 0x5BDE30 / 0x5BDE40 (first named
      Spt::Class; batch 50 found they're Lst::Search's - vtable 0x658AA4 is the list iterator's). Also the jump 0x48C1B0 -> 0x5480A0. 25 functions.
      Early test: every method over states x capabilities x motor counts x pause depths x pad present or not,
      motors traced through stand-in DirectInput force-feedback effects (0x7D6668; per-port scales at
      0x7D42D0 set - zero offline, they hid the levels), the keyboard through a stand-in DirectInput keyboard
      (lost / not acquired / failing re-acquire), typed characters, the manager's references and returned
      instance, the destructors. The original ActivateActuator is tested as PartyMod leaves it (its 7-byte NOP at
      0x5BDA31 applied during the test - unpatched it runs the menu code). Late test (game's rand, memory
      manager up): constructor and destructors. 34 deliberate bugs caught (10 after fixing the test).
      Also: the library boundary was wrong - game code runs to 0x5BEAAD (D3DX9 starts at 0x5BEB1A after 18
      import thunks; docs/library_ranges.md, find_gaps / find_stubs fixed). thug2_check now links PartyMod
      (SDL2 delay-loaded) and its output is unbuffered (a crashing test used to swallow everything).
      Noticed: PartyMod's device struct puts start_or_a_pressed (uint32) at +0xF0 - past the 0xF0-byte
      device, so its processController writes 4 bytes beyond the allocation every frame. Not confirmed
      harmful; worth checking when the heap code is next looked at.
      Next batch (SIO part 2): ReadData 0x5BD200 and the key-name function 0x5BCD50 (screen-element smart
      pointers), KeyInput 0x5BDE70.. 0x5BDF30, the manager's constructor/destructor/task 0x5BE110-0x5BE300,
      0x5BE3F0-0x5BE5E0, 0x5BE6C0-0x5BEAA0.
- [x] Batch 50 (NOT run in the game yet; early test passes offline): SIO, part 2. Sio::Manager (0x1C bytes,
      vtable 0x658AD4: the device list's head node at +4, a Tsk::Task at +0x18): constructor 0x5BE1F0 (the
      task - vtable 0x6476F0, code ProcessTask, data the list; a device for ports 0 and 1 appended and set
      acquiring; then 0x450D60 unless 0x4505F0 - network-related player state, not rewritten),
      DestroyBody 0x5BE300 (deletes the devices virtually, then the task; a call to the folded ReturnZero;
      0x450420 unless 0x4505F0; unlinks the head) and Destroy 0x5BE6C0, ProcessTask 0x5BE140 (the task's
      code) / ProcessDevices 0x5BE3F0 (Process each device, telling the first), GetDevice(port, slot)
      0x5BE490, GetDeviceByIndex 0x5BE4F0, PauseDevices 0x5BE550 / UnpauseDevices 0x5BE5E0; CreateManager now
      constructs it directly. The device list node's deleting destructor 0x5BE110 (Sio::DestroyDeviceNode).
      KeyInput 0x5BDE70 (typed keys from the window messages - PartyMod calls it for SDL text input): wanted
      while typing (0x7CE46E) or with text entry on (0x7CCDF8) outside a menu (0x7CE46F); a negative lParam
      (release) is ignored; Enter -> 0x19, Backspace -> 0x18, Escape -> 0x1A (not its auto-repeats), ';' ->
      ':'; -1 % ~ \ | [ ] } " dropped; the 16-slot queue stops at 15 (the last is overwritten).
      New src/game/lst/search.h: Lst::Search<T> (the list iteration every loop here uses); its destructors
      0x5BDE30 / 0x5BDE40 moved from batch 49's "Spt::Class" into lst/search.cpp. 12 functions.
      Early test: KeyInput over 20 keys x 4 lParams x the three flags x queue fill levels (each queued key
      traced), the manager's lookups, pause/unpause-all (motors traced) and destructor over 0, 1 and 3
      stand-in devices (their and the task's deletion recorded), processing an empty list, the node
      destructor. Late test: the constructor, original vs ours (devices, list, task), then the freeing
      destructor (the player-state calls skipped meanwhile). Processing a real device isn't tested offline
      (Process is PartyMod's SDL code). 22 deliberate bugs caught (3 after strengthening the test); the
      constructor is only compared in the game.
- [x] Batch 51 (NOT run in the game yet; early test passes offline): SIO, part 3, and what it needs.
      Smart pointers (new src/game/obj/smtptr.*): Obj::CRefCounted keeps the list of smart pointers to it at
      +4; AddSmartPointer 0x4637C0 (to the front; the pointer's prev isn't touched), RemoveSmartPointer
      0x4637E0, CSmtPtr::Release 0x416DD0; Obj::CSmtPtr<T> (12 bytes: object, prev, next) with the inline
      assignment and destructor. Screen elements (new src/game/front/screen_element.*): CScreenElement
      (flags +8, parent +0x4C) IsHidden 0x4A8F40 (hidden flag 0x400000 on it or any parent); the element
      lookup 0x4AAE20 (not rewritten: it ignores the manager it's called on, 0x701440, and searches the hash
      tables of the one at 0x6F0FC0). Sio::SpecialKeyInput 0x5BDF30 (virtual keys: F5 runs "refresh_chosen"
      while menu_network_select_refresh is shown; else, with typed keys wanted, the arrows in a menu queue
      0x14-0x17 and F1-F4 outside one 0x1B-0x1E). Sio::GetButtonKeyName 0x5BCD50 (a pad button's key name for
      the menus: three tables - in a menu (not typing, not the park editor's piece_menu_hmenu unless quitting),
      typing, else - from port 0's key bindings 0x7D6770 or fixed DirectInput codes; the name from the
      platform's table (0x5F3120: 30 bytes per code at 0x7D42F8) through wcstombs into 0x7D1AD8, printed into
      0x7D19D8; button 0x12 "a+b"; in a menu buttons 0x13/0x14 become the typed characters of Z/C and S/X;
      it looks select_skater_hmenu up twice without using the result - kept). 6 functions.
      Early test: smart pointer add/remove/release on a list of four, IsHidden through a parent chain, the
      F-key handler over a stand-in screen element manager (hand-built hash tables; the server list absent,
      shown, hidden, in a hidden parent) x flags x keys x a full queue, and our key names against worked
      expectations (menu/typing/play tables, the "+" pair, the letter pairs, both menu flags, quitting).
      Late test (the original uses the game's wcstombs/sprintf): key names, original vs ours, 6 element
      scenes x 16 flag combinations x buttons -1..31. 29 deliberate bugs caught (3 after strengthening).
      Left in SIO: ReadData 0x5BD200 (the original pad read - dead code with PartyMod, still to be written).
      Then the timer module (Tmr) 0x5BE6F0-0x5BEAA0.
- [x] Batch 52 (NOT run in the game yet; early test passes offline): the game clock (Tmr, new
      src/game/sys/timer.*), 0x5BE6F0-0x5BEAA0: Init 0x5BEA50 (Reset, then the C runtime clock and the time
      stamp counter noted), Reset 0x5BE9B0 (the vblank rate from the refresh rate 0x684FB4 the first time;
      frame length 1, history 1,1,1,1; the timing block 0x7D1E68 saved to 0x7D1E30), CountVblank 0x5BE700,
      OncePerRender 0x5BE740 (render frame +1; the frame's vblanks into a 4-entry history), UpdateFrameLength
      0x5BE7F0 (each entry 0-4, outside 0-10 counts as 1, * 0.25, at least 1; the uncapped average; the slow
      motion asked for takes effect), WaitForVblank 0x5BE920 (runs the idle function 0x48C1C0 until the
      vblank count moves), GetVblanks / GetRenderFrame (64-bit), GetTime 0x5BE970 (ms from the low 32 bits:
      1000 / rate * vblanks, truncated through 64 bits like _ftol), GetFrameSeconds 0x5BE910,
      GetSlomoFrameSeconds 0x5BEAA0 (returns the unrounded x87 product - double), Get/SetSlomo, SetFlags
      0x5BE7C0 (two flags at 0x7D1EE8, meaning unknown), ReadCpuTimeStamp 0x5BE6F0 (source-only: 3 bytes;
      not "ReadTimeStampCounter" - a winnt.h macro). 15 functions. The subsystem map (data/subsystems.csv,
      docs/subsystems.md) now has the clock and the import thunks; the D3DX9 range starts at 0x5BEB1A.
      Early test: reset (first time / later), init (but the clock and TSC), 32 frames of 0-13 vblanks with
      the counters near their 32-bit limits, slow motion asked mid-frame, the frame length over unusual
      histories (negative, huge, wrapping sums), milliseconds at 60/50/59.94 Hz up to 2^32+5 vblanks, waiting
      with a stand-in idle function, the flags. 23 deliberate bugs: 22 caught (1 after strengthening), 1
      equivalent (the vblank count isn't in the block Reset saves).
- [x] Batches 49-52 tested in the game (2026-10-16, by the user): game works; 637 of 637 replacements
      installed, early and engine-started tests pass, no run-time check failures. One late test failed -
      "sio (late): Device constructor and destructor" - a test bug: after destroying, it compared the two
      devices' node pointers (each its own). Fixed; the comparison now clears them first.
- [x] Batch 53 (NOT run in the game yet - and PartyMod never runs it; early test passes offline): SIO's last
      function, Device::ReadData 0x5BD200, the original pad read (Process in state kReady; PartyMod's Process
      replaces it). Returns at once while quitting. Looks up select_skater_hmenu (twice) and piece_menu_hmenu
      (ignored while quitting); menu mode = in a menu (0x7CE46C or 0x7CE46F), not the piece menu, not typing;
      both off while 0x696D95 or 0x6F1158. The front end's port (0x553E90 on the object at 0x7CE4B4) decides
      who reads: typing -> pad mode 2; in a menu -> mode 1 on port 0 (unless the skater select is up and the
      front end listens elsewhere), mode 2 off the skater select, mode 3 on port 1 if it's the front end's,
      else the device just reports plugged in; the piece menu -> port 0 only, mode 0; playing -> mode 0. (Pad
      mode: 0x5F6610 -> 0x7D6A3C; also 0x5F6620(0), 0x5F6630(1).) Then the window messages are pumped
      (0x5F56E0), and if a pad is present its state (Sio::PadState: buttons +4, eight pressures, four sticks;
      read by 0x5F5BC0) becomes PS2 control data: two bytes of button bits (set while up; the keyboard
      driving the menus masks some), pressures with a 0x20 dead zone, the d-pad as pressures and bits, the
      sticks to 0-255 (y inverted, signed division), the triggers' bits - mapped differently in the park
      editor (level checksum 0x7ECA21E5) and again while its element 0x31631B98 is up. "Pressed" if any bit
      is down; the keyboard takes over the menus after start, a pressed trigger, a typed Enter (0x19) or,
      while playing, Enter held (0x5F30E0(0x1C)). 1 function, 1.7 KB. SIO (0x5BCC40-0x5BE6C0) is now done.
      Early test: the platform's pad read, message pump and key check redirected (a jump written over their
      first bytes for the test, then restored) to scripted stand-ins, so original and ours see the same pad
      and every call is recorded; 8 menu scenes x 13 flag sets x 3 ports x 2 front-end ports (levels, key
      bindings, typed keys, pad states and keyboard control cycling), then 7 pad states x 4 levels x 3 menu
      states x 4 keyboard combinations, and no element manager. 39 deliberate bugs: 37 caught, 2 equivalent
      (the first skater select check is repeated before it's used; the quit check before the piece menu
      can't fail after the one at the start). The test first looked fine while every read returned at once:
      it wrote a third port's key bindings - there are two tables, the third overwrote the quit flag.
      tools/mutate.py now names the failing test on each CAUGHT line and reports mutations that don't build.
- [x] Fix (NOT run in the game yet): since batch 46 our LoadQB (0x46EDE0) called Pip::Load directly, but
      PartyMod replaces that call (0x46EE80) with its pipLoadWrapper, which applies its binary script patch
      for scripts\engine\menu\keyboard.qb - so that patch wasn't applied. LoadQB now loads ordinary .qb files
      through pipLoadWrapper (guarded for when PartyMod's patch list isn't set up). The overlap check
      only looked at a function's first 16 bytes; it now also fails on any PartyMod patch inside a rewritten
      function's body unless the patch is listed with how our source does it (3 so far), skips commented-out
      patches and reads "addr - n". A new Ghidra script (since retired) lists PartyMod's patches by game
      function: 113 patches in 48 functions (35 KB), 2 in data.
- [x] Batch 54 (PartyMod round, platform agent; NOT run in the game yet). Its record:

      - [x] PartyMod round, platform (NOT run in the game yet; early tests pass offline): four functions PartyMod
            changes, with its changes built into the source (src/game/sys/platform.cpp, src/game/mc/memcard_cfuncs.cpp).
            - 0x5F3A50 Platform::SetActuators (source only; PartyMod jumps from its start): calls PartyMod's
              set_actuators(port, left, right) (SDL rumble on the controller playing as the port). The original's
              DirectInput force-feedback logic (ranges 0x7D42D0, one-motor kind 0x7D42C8, DIEFFECT parameters,
              magnitude * 10000) is kept in a comment. Sio::SetActuators (device.cpp) still calls 0x5F3A50 (which
              reaches PartyMod in the game): pointing it at Platform::SetActuators makes TestSio fail offline, since
              that test's original run uses the unpatched DirectInput version - TestSio would need PartyMod's jump
              written at 0x5F3A50 first.
            - 0x5F56E0 Platform::ProcessWindowEvents (source only; PartyMod jumps from its start): calls PartyMod's
              handleEvents (SDL event pump). The original's PeekMessage/Translate/Dispatch loop on the window
              0x7D6A28 is in a comment.
            - 0x5F44F0 Platform::InitInput(instance, window, config) (cdecl, bool): false without a window, true if
              DirectInput (0x7D69D0) is already set; else clears the ports' joystick/effect state, the key bindings
              (-1) and the graphics options, then PartyMod's writeConfigValues (in place of the settings loader
              0x5F34D0(config)), PartyMod's initManager (in place of DirectInput8Create + EnumDevices, which PartyMod
              NOPs), then the key-name table 0x5F3C10, and returns true.
            - 0x5A6E40 Mc::GetMemCardSpaceAvailable (CFunc): SpaceAvailable = 0 and FilesLeft = 1000000 into the
              calling script's params (CScript +0x14), then the card (0x7D1890's GetCard(0,0)); false without one;
              else SpaceAvailable = PartyMod's calcAvailableSpaceFudged(card) (clamped free blocks) in place of
              Card::GetFreeBlocks.
            Tests (src/hook/selftests_platform.cpp): each original runs with PartyMod's patch bytes written in for the
            test (jumps at 0x5F3A50 / 0x5F56E0, calls at 0x5F4591 / 0x5F45A2 with the 45-byte NOP, the call at
            0x5A6E7E), and PartyMod's functions, the struct adds, the card lookup and 0x5F3C10 are redirected (a jump
            over their first bytes, our own and the game's alike) to recording stand-ins. Rumble over 5 port/level
            cases (incl. values beyond 16 bits); input set-up with/without a window and DirectInput over dirty
            globals (the stand-ins record what's set up when they're called: order checked); the CFunc with and
            without a card. 17 deliberate bugs, all caught. Register scan, call audit and the overlap check
            clean. tools/check/main.cpp now defines an empty thug2_decomp_before_exit (main.c is linked into
            thug2_check for calcAvailableSpaceFudged).
            Left undone (stopped early on the coordinator's request - budget): 0x44FDC0 (Bink movie player, the
            movie-skip NOP at 0x450121), 0x452910 (music update; the shuffle PartyMod patches is really the code at
            0x452200, a separate function Ghidra merged into 0x452910's body - 0x452910 tail-jumps to it), 0x4E1EF0
            (the game's main: all singletons, MainLoop, teardown; fastExit at 0x4E2492), 0x5A8730
            (LoadFromMemoryCard; PartyMod NOPs the save checksum's JNZ at 0x5A8A01), 0x545880 (online/MOTD state
            machine, 18 states: wait 500 ms not 15000 at 0x545B4E, script 0x588ED87C not 0xD4E84C8D and
            [+0xFE09] = 1 in the state at 0x545CD0), 0x54DCB0 (RetrieveTopStats: sprintf "http://%s/thug2pc/%s"
            with PartyMod's "gamestats.<OnlineDomain>" in place of gamestats.gamespy.com - needs net.c to keep
            that string in a global; request struct 0x118 bytes; disassembly read, not written).

- [x] Batch 55 (PartyMod round, gfx agent; NOT run in the game yet). Its record:

      - [x] PartyMod gfx, part 1 (NOT run in the game yet; branch pm/gfx): the screen's shape and blur, with
        PartyMod's replacements built in. New src/game/gfx/screen.cpp (namespace Nx), all `REPLACE_SOURCE_ONLY`
        because PartyMod's jumps or byte writes sit on their first bytes:
        - 0x004A0780 SetScreenAspectRatio: PartyMod's `setAspectRatio` (stores the aspect it wants - 4:3 in the front
          end, the window's shape in a level - and ignores the argument). The original stored the argument in
          0x00701340 (kept as a comment).
        - 0x004A07C0 GetScreenAngleFactor: PartyMod's `getScreenAngleFactor` (aspect / (4/3) for screens wider than
          4:3, unless the constant 0x0067E544 isn't 1; also updates 0x00701340). The original returned that constant.
        - 0x004B2330 SetScreenBlur: **depends on PartyMod's `DisableBlur` setting** (on by default): on, the blur is
          forced to 0 (`setBlurWrapper`); off, the original (store the amount in 0x00787328 if 0x00786D40, the device
          can blur). gfx.c now records the setting in `blur_disabled` (a THUG2_DECOMP-marked edit).
        - 0x004B2440 SetLetterbox: PartyMod's `setLetterbox` (fits the 640x480 layout to the aspect it wants, pillar-
          or letterboxed). The original's squash to 3/4 height is given as a comment.
        - 0x0048C170 CFunc_SetScreenBlur: the original (unnamed integer -> 0x006FA72C and SetScreenBlur). PartyMod's
          `restoreBlur` (DisableBlur off) writes this function's first five bytes with the bytes already there in
          THUG2PM.exe (checked: 51 8B 4C 24 08) - so no change in behaviour, but it would wipe a hook's jump, hence
          source only. Note its SetScreenBlur call reaches PartyMod's blur override when DisableBlur is on.
        - All nine patch addresses are in the overlap check's HANDLED list.
        - Tests (src/hook/selftests_gfx.cpp, Early): TestGfxScreen - the originals as PartyMod leaves them (its jumps
          written over their starts for the test) vs ours over front end / level / no skater (stand-in skater and
          career), four window and back-buffer shapes (wider and narrower than wanted), the angle-factor constant 1
          and not 1, letterbox on/off from both previous states; SetScreenBlur with DisableBlur on (patched) and off
          (the unpatched original), the device able to blur or not, four amounts. TestGfxScriptSetScreenBlur - the
          script command over stand-in parameter structures (the amount, none, only a named integer, an unnamed
          float), both settings, device able or not: return value, 0x006FA72C and 0x00787328. 11 deliberate bugs
          caught (each by the test meant to catch it). Not tested: the in-game path where PartyMod's real patches are
          applied (same functions, so only the setting plumbing differs).
        - Left undone (budget): the five large rendering functions PartyMod patches inside. They're hookable
          (`REPLACE`) and their disassembly is read; notes for whoever picks them up:
          - 0x004B2D80 (the per-frame world render: per viewport, viewport rect, set_camera, scene passes, sky
            pass with fog 20/21, then 2D): PartyMod NOPs both letterbox viewport adjustments (0x004B2E65, 58 bytes;
            0x004B33CA, 35 bytes) and calls `setDisplayRegion` there (still behind the `if (letterbox active)` test),
            and calls `draw2DWrapper` instead of 0x004DFB90. ~30 callees, all cdecl except 0x4A0D20, 0x485B10,
            0x485A00/10, 0x4D6530, 0x48E370, 0x49AB50 (RET 4), 0x496640, 0x4A10A0 (thiscall). Passes a dword with
            garbage upper bytes to 0x490DE0 (only AL matters there - check 0x4B8E40).
          - 0x004DF390 (the blur pass): PartyMod turns the JZ at 0x004DF63A into JMP - the letterbox adjustment of
            the blur quad never runs. Mind the precisions: v1.u = (float)(1 - unrounded t/W) but 1 - tH uses the
            float-rounded tH.
          - 0x004DB200 (set_camera(matrix, position, screen angle, aspect, scale, render at infinity), 6 cdecl
            args): PartyMod's far plane default is 96000 (patch at 0x004DB35D, original 32000). Uses FPTAN/FPATAN
            and D3DX through dispatch pointers (0x5C036B, 0x5BFA8C, 0x5C0509).
          - 0x004B9030 (screen flash quad): the DrawPrimitiveUP call at 0x004B91E7 goes through PartyMod's
            `reorder_flash_vertices` (rotates the first three vertices); u/v are uninitialised without a texture.
          - 0x0044FA80 (movie frame quad): `movie_setTexture` at 0x0044FB8C, `movie_drawPrim` (stdcall) at
            0x0044FC88 + NOP; also sprintf's an unused "e:\movietest\movie%04d.bmp" with counter 0x00696D9C.
          Testing them offline needs a stand-in D3D device (a vtable of recording stubs) and detours over every
          callee; all five are plain D3D state/draw sequences, so that's feasible.

- [x] Batch 56 (PartyMod round, glyph agent; NOT run in the game yet). Its record:

      - [x] PartyMod round, glyph/fonts/window (NOT run in the game yet; branch pm/glyph): the script fonts and the
        loading bar's colours, with PartyMod's patches built in. src/game/gfx/font.cpp, src/game/gfx/loading_bar.cpp,
        tests in src/hook/selftests_glyph.cpp.
        - `Nx::LoadFont` (0x48D7E0, cdecl `(name, charSpacing, spaceSpacing, uint32_t* colors, bool buttonFont)`):
          a loaded font (hash table 0x6FA9F0 of 16 `FontSlot`s {char name[24]; CFont*} at 0x6FA830, looked up via
          0x48D760) is just updated - looked up again before each of its three setters (0x4E3F90 SetSpacings,
          0x4A10A0 SetColorTable if colours, 0x487D50 MarkAsButtonFont; each a jump through the font's vtable) - and
          returns. Otherwise the first free slot gets `Nx::LoadFontFile` (0x4B40A0), the setters, the name
          (unbounded copy) and a hash entry; with every slot taken nothing is loaded. Either way the meta button map
          (0x6FA808, 32 chars, flag 0x6FA828) is then built once from a script array: 0-9 as digits, larger values as letters from a, 0 past the
          array's end.
        - `Nx::CFunc_LoadFont` (0x48D950): `LoadFont <name> [buttons_font] [char_spacing] [space_spacing]
          [color_tab]`; spacings are floats truncated to int; colours are up to 16 arrays of bytes packed lowest
          first (a 5th byte wraps onto the 1st, as SHL's count does). Returns true. The original leaves colour
          entries the script doesn't give (and the name, if missing) as stack garbage; ours zeroes them / uses null.
        - `Nx::UpdateLoadingBarColors` (0x4B6B80; called by the SetLoadingBar* CFuncs): start/end/border colours
          from 0x67DDCC/D0/D4, width = screen width (0x786D6C) * bar width (0x67DDBC) / 640, border ARGB with alpha
          0x80, and a per-pixel gradient table of {r, g, b} ints. Nothing in the exe reads that table.
        - PartyMod built in:
          - glyph.c:74/77 (DWords at 0x48D983 and 0x48D8F8): the button font name ("ButtonsXbox") and the meta
            button map ("meta_button_map_xbox") become PartyMod's PS2 / GameCube ones by its `GlyphStyle` setting.
            `Nx::ButtonFontName()` / `MetaButtonMapName()` pick them from PartyMod's own variables; glyph.c now keeps
            the style in a global `glyphStyle` (was a local of patchButtonGlyphs; noted in PartyMod's UPSTREAM.md).
          - window.c:150 (DWord at 0x4B6C64): the gradient table goes in PartyMod's `resbuffer[100000]` instead of
            0x701688 (which overflows at high resolutions). PartyMod replaced the instruction's base, which is the
            table + 4, so the patched original writes from 4 bytes *before* its buffer; ours writes from the buffer's
            start (same contents, no out-of-bounds write).
          - The three addresses are in HANDLED in the overlap check.
        - Tests (Early): `TestLoadingBar` - 7 cases (rising, falling, ~3000-entry high resolution, short, zero and
          negative width, flat, rounding of both float conversions) against the original with PartyMod's DWord
          written in; compares the globals, the whole table (original's at resbuffer-4, ours at resbuffer) and that
          ours leaves the 4 bytes before the buffer alone. `TestLoadFont` - for each glyph style 0/1/2 (PartyMod's
          two DWords written into the original): CFunc_LoadFont with a hand-built parameter structure (new font with
          3/1/0/5-byte colours and truncated spacings; the same font again; the button font flag; 20 colours) and
          LoadFont directly (no colour table, updates, filling all 16 slots and beyond, map missing then present,
          slots already full, map not reloaded once loaded); stand-in fonts (vtable recording every setter call),
          a stand-in hash table and symbol table; compares slots, hash items, meta map and call log. Not covered:
          a colour-table entry the script leaves out (garbage in the original), hash-bucket chaining (needs the
          pool manager), the real font loader (stood in).
        - 35 deliberate bugs: 34 caught (1 after strengthening: reloading an already loaded meta map), 1 equivalent
          (`(8*j) & 63` vs `& 31` as a shift count - the CPU masks it the same way).
        - Register scan, call audit, overlap check: clean.
        - **Left undone** (the coordinator cut the round short), with what was worked out from the disassembly:
          - 0x4CECB0 `SFont::QueryString(text, float* width, float* height)` (thiscall, RET 0xC; reached via
            0x4B3D10 `mov ecx,[ecx+8]; jmp`). Font layout: +0 height (unsigned -> float), +4 base, +8 SChar[] (0x18
            bytes: u0 v0 u1 v1 floats, w h baseline uint16), +0xC map[256], +0x10C special map[32], +0x134/+0x136
            int16 char/space spacing, colour table from +0x138 (indexed [idx*4+0x134], idx >= 1). Escapes: `\\`
            (counted as a char), `\s<d>`/`\S` special glyph, `\b<d>`/`\m<d>` (meta: 0x48D6A0 maps through the meta
            map, then DehexifyDigit on a 1-byte local) buttons, `\c`/`\u` skip 2, `\l` skip 1. PartyMod: the
            non-meta DehexifyDigit call (0x4CED4B) -> `dehexifyDigitWrapper`; the glyph-or-key-name test at
            0x4CED6D (originally glyph for 4-7 and 10-13) -> `shouldUseGlyph(index)`; key-name path sums
            GetButtonKeyName's characters' widths (signed char map index) + spacing and stops counting the rest.
          - 0x4CFD00 `SText::Draw()` (thiscall, no args; 2383 bytes of x87): same escapes plus `\c<d>` colours
            (from the font's table unless +0x40 overrides); per-character effects from +0x18 (10-byte records:
            int16 x, y, xscale, yscale /16384, rotation /16384); rotation by +0x38 about pivot +0x30/+0x34; vertices
            (xyzrhw, ARGB, uv; 0x1C bytes) into 0x780FE8, count 0x780FE4, flushed through vtable +0xC/+4 at 0x400.
            Button glyphs switch to the button font 0x780FDC for one char; key names are drawn char by char via
            0x4CFB20. Original quirks: the per-char rotation centre uses pivot X for both axes. PartyMod patches the
            same two places as QueryString (0x4CFF0C call, 0x4CFF36 test). Needs a bit-exact float test.
          - 0x4D8650 (D3D init, called from 0x4B25F0 and 0x4D89C0): Direct3DCreate9, GetAdapterDisplayMode,
            present parameters at 0x786A7C, resolution table 1280/1024/800/640 x 1024/768/600/480 tried from
            index 3 - setting 0x7D643C, device creation via 0x4D7C80 over 4 flag pairs, exit(0) on failure,
            conversion factors 0x786D80/84 (= w/640, h/480), CheckDeviceFormat, ShowCursor loop, Tmr::SetFlags.
            PartyMod: no ShowWindow (0x4D869C), no SetWindowPos (NOP at 0x4D8891), no ShowCursor loop (0x4D8989),
            Windowed = 1 when its `isWindowed` (0x4D8720), table slot 3 = its `resX`/`resY` (0x4D8776/0x4D8792).
            Testable offline with a stand-in IDirect3D9 (detour the 0x5BEB14 thunk) and IAT stand-ins.

- [x] Batch 57 (PartyMod round; NOT run in the game yet): three small skater control functions, with PartyMod's
      PS2 controls (its settings file, now thug2-decomp.ini; `inputsettings.isPs2Controls`, read through Skater::Ps2Controls) built in
      (new src/game/skater/controls.*). Skater::Component::PlatformStructure 0x51F4C0 (thiscall, `this`
      unused: the structure for this platform from a control script's parameters - "xbox" 0xCE42EE1A on
      platform 7 (PC), 0xCB0E600C on platform 5, else 0xE594F0A2 asserted; PartyMod's byte at 0x51F4C8 turns
      the 7 into a 5, so PC skips the Xbox one). TryWalkAcidDrop 0x527510 / TryAcidDrop 0x527600 (bool: two
      held buttons, input +0xC0 and +0x120 - PartyMod NOPs the first check, nollie, at 0x527546 / 0x527636;
      the physics' acid drop search 0x502240 fills the velocity; then the trick name 0x427CD0, or (on foot,
      0x527600) a sound 0x434BF0 and a state call 0x5260E0; velocity y 0, mode 2, state 0x2EDA83EA). The skater
      component's fields are reached by offset (layout unknown beyond them). The overlap check: the
      three patches are HANDLED; 0x51F4C8 sits inside the 16-byte window but past the 6 bytes MinHook copies,
      listed in its new PAST_TRAMPOLINE exceptions. Early tests: the platform lookup over stand-in parameter
      structures (each of the three present or not) x platforms 5/7/1 x PS2 controls off/on; the acid drops
      over 128 combinations of the stopping flags, buttons, a found drop, on foot or not, x off/on, with the
      five callees redirected to recording stand-ins - the original run with PartyMod's bytes written in for
      "on". 15 deliberate bugs caught (3 after fixing the test's stand-in components: the type is byte 0,
      shifted left one).
- [x] Batch 58 (PartyMod round; NOT run in the game yet): Net::RetrieveTopStats 0x54DCB0 (the script command;
      new src/game/net/stats.*): the stats index (0x90 of the stats object at the online manager 0x7CCE14's
      +0x1C) = the script's integer 0x7360C9EF, else the next; a request (0x118 bytes: timeout 0x25800 ms, the
      URL "http://<host>/thug2pc/<file>" - files hs_at / bc_at / hs_mo / bc_mo / ratings, 32 bytes each at
      0x67F2A0 - the reply callback 0x54DB20, checksums, the ratings file getting 0x7346E8E7 instead of
      0xD932B07B) handed to 0x54A910 on the manager's +0x14; +0x1FC cleared; returns true. PartyMod's DWord at
      0x54DD14 swaps gamestats.gamespy.com for gamestats.<OnlineDomain>: net.c now keeps that string in
      its own gamestats host variable (THUG2_DECOMP; noted in UPSTREAM.md), which ours uses when set. Early test
      (in selftests_skater.cpp): index given or not x every file x PartyMod's host or the original's, the
      original with PartyMod's DWord written in and its C runtime sprintf redirected to a stand-in (it can't
      run offline), the request recorded by a stand-in sender. 7 deliberate bugs caught.
- [x] Batch 59 (PartyMod round; NOT run in the game yet): Nx::DrawFlash 0x4B9030 (new src/game/gfx/flash.*): a
      screen flash - a rectangle over its viewport (fractions from 0x4A06D0, times the screen size 0x786D6C /
      0x786D70; x87: products rounded once, sums to float) at its depth (+0x1C), colour packed from +0xC-0xF
      (alpha +0xF), u/v 0-1 if it has a texture (+0x20, its image directly or through +0x10 by flag bit 0;
      untextured the original leaves u/v unset - ours 0), the renderer's state calls (texture 0x4DA730,
      0x4DAB60, blend 0x4DA760(5), 0x4DAA80, format 0x4D9DB0(0x144)), the draw/triangle counters
      (0x786EBC/C0), then the device's DrawPrimitiveUP (vtable +0x14C) as a 2-triangle strip. PartyMod's
      reorder_flash_vertices (its call at 0x4B91E7) rotates the first three vertices first - the original's
      order is wrong - done inline here. Early test (selftests_pc.cpp - renamed from selftests_skater.cpp:
      it holds this round's tests): 4 viewports x 4 screen sizes x untextured / two textures, the original with
      PartyMod's call written in, the draw recorded by a stand-in D3D device and the state calls by stand-ins.
      10 deliberate bugs caught (1 after adding a viewport where rounding the sum once or twice differs -
      found by search). tools/mutate.py now rebuilds the restored code at the end (it used to leave the last
      mutation's build behind).
- [x] Batches 53-59 and the LoadQB fix tested in the game (2026-10-16, by the user): game runs; 648 of 648
      replacements installed (31 source only), early 61 / late 16 / engine-started 2 self-tests all pass.
- [x] Batch 60 (PartyMod round; NOT run in the game yet): Nx::DrawMovieFrame 0x44FA80 (new src/game/gfx/movie.*;
      cdecl texture, x, y, width, height, alpha): nothing without a texture; the render states (blend
      0x4DA760(0), 0x4DAB60(4, 0x10001), format 0x144 via 0x4D9DB0 and SetFVF, cull none, texture stage
      states, 0x4DAA80, no texture), a leftover debug dump name "e:\movietest\movie%04d.bmp" formatted (counter
      0x696D9C advanced, never written), the texture set, a quad (position + size x 640 / 480, colour 0x808080
      with alpha*255 truncated) drawn as a 2-triangle strip unless the device is lost (TestCooperativeLevel),
      then stage 0's colour op back to modulate. PartyMod reroutes the texture set (0x44FB8C) to its
      movie_setTexture (notes the movie's size) and the draw (0x44FC88 + a NOP) to its movie_drawPrim (fits the
      quad to 4:3 inside the back buffer) - ours calls both. Early test (selftests_pc.cpp): back buffers
      16:9 / 4:3 / tall x device lost or not x three shapes (and no texture), the original with PartyMod's two
      calls written in, a stand-in D3D device and texture (GetLevelDesc), the renderer's calls and the C
      runtime sprintf stood in. 11 deliberate bugs: 9 caught, 2 equivalent under PartyMod (its fit replaces
      every vertex's x/y, so ours' positions never reach the draw).
- [x] Batch 61 (NOT run in the game yet): the PlayMovie script command 0x59F0C0 (Nx::PlayMovie in
      src/game/gfx/movie.cpp: the unnamed string, queued by 0x44F830; returns false without one) with a feature
      flag: thug2-decomp.ini [features] skip_intro_movies=1 skips the start-up movies (ATVI, Beenox_Shift,
      Dolby_Digital, Intel_intro, NSlogo, intro, introf - matched on the file name, any folder, any case),
      returning true as if queued; logged. Early test (TestPlayMovie): flag off (no ini offline) - a name, a path,
      no name - original vs ours, the queue 0x44F830 recorded. The flag-on path isn't tested (the flag is read
      once from the ini).
- [x] Batch 62 (skaterB; PartyMod round; NOT run in the game yet): two skater air functions with PartyMod's PS2 controls
      built in (new src/game/skater/rotation.*, `Skater::CorePhysicsComponent` - the component the air update
      0x50CA00 runs on; fields by offset: object +0xC (matrix +0x6C, velocity +0xAC), display matrix +0x18,
      input +0x150C (buttons of 0x20 bytes from +0x20: {down, time, ..., +0x10 pressed}), trick +0x1518
      (rotation total +0x13A4), +0x1520 ([+0x18] shows the total), state flags +0x1528 ({bool, time} pairs,
      cleared with time = 0x44ECE0() + [0x696D60])) and the network keyboard task with PartyMod's F1:
  - Spin 0x504060 (thiscall, void): tap mode (+0x2DA: L1/R1 taps queue +-pi in +0x30C, turned at the stat
    0x580790(0xF7A2ACC1) per frame length +0x8C); else L1/R1 held past the spin delay (0x580400(0xA5A02449);
    held time read unsigned) turn at the stat 0x4957DB43 - PartyMod's 2-byte NOPs at 0x504239 / 0x50430B drop
    the delay check (the delay is still read); then (flag +0xC0 clear) the second pair (input +0x60/+0x80, held
    time signed) ramped between 0xA092B2BE and 0x5D756349; with no turn, face the ramp (flags +0x30 and +0x68:
    acos(clamp(forward.y)) vs 0x61224F2E degrees, 0x406630's offset angle, Sign * 0x9DB33213, the last bit
    exactly); the turn: +0x3A5 = turn > 0, RotateYLocal on both matrices, total += degrees, shown truncated
    (0x564BE0) unless flag +0x30 and not +0x78 and |total| < 360 - 0x50C5CC2F + 0.1. Script floats come back
    unrounded on x87: GameFunction<double(...)> keeps that.
  - BreakVert 0x507120 (thiscall, 1 bool arg `force` - all callers pass 0, RET 4, void): the jump button held
    past 0x580440(0x9D2F8CC8) with none of 4 others, or both triggers (input +0xC0 / +0x120; PartyMod's JNZs at
    0x507184 / 0x5071AE make it either), or +0x2D0, or force. Triggers or +0x2D0: if 0x505F70 (thiscall bool)
    only copy the matrix; else velocity x/z -= wall normal (+0x58/+0x60) * 24, RotateXLocal(0xD757F4BB deg),
    flags 30/38/48 cleared, 80 set (if not exactly 1), face along velocity (no display copy). Otherwise:
    velocity x/z -= normal * |v| (length squared stored as float first - the original stores it in the
    argument's slot) * 0x13F33B41, y *= 0x848C5CD6, the same pitch, flags 30/38/48/B0 cleared, face along
    velocity, display matrix = object's.
  - GameNet::KeyboardTask 0x5334D0 (cdecl task callback, in chat.cpp): Enter (0x19) - unless ObjectExists
    (0x462340) for ids 0x31631B98 / 0xF53D1D83 / 0x3B56E746 / "quit_dialog_anchor": run 0x3B4548B8 and clear
    the typed keys (the parameters are leaked, as in the original); F1-F4 taunts (0xB53D0E0F = 0x0E5FD359 /
    0xBEEA3518 / 0x4525ADBD / 0xA36DBEE1, script 0x2C43B5BA) at most every 2000 ms (0x7CCE20). PartyMod's byte
    at 0x533652 (unconditional, patchInput) makes F1 0x1F, since its SDL keyboard input sends 0x1B for Escape.
  - The overlap check's HANDLED list: 0x504239, 0x50430B, 0x507184/5, 0x5071AE/F, 0x533652. PartyMod's runtime
    patches stay in PartyMod's folder (as for batch 57: they still apply to the original when a replacement is
    disabled in thug2-decomp.ini).
  - Early tests (src/hook/selftests_skaterB.cpp): spin over 6000 and break vert over 4000 random cases (every
    branch input: tap mode, buttons with held times around the script times incl. negative, flags, queued
    turns, tilts, totals, velocities incl. zero, frame lengths incl. 0) x PS2 controls off/on - the original
    with PartyMod's bytes written in for "on" - with the callees (clock 0x44ECE0, 0x580400/0x580440/0x580790,
    0x564BE0, 0x406630, 0x505F70) redirected to recording stand-ins; break vert's original also gets our math
    helpers (CreateRotateXMatrix, D3DXMatrixMultiply 0x5BFA8C - stdcall, Normalize, OrthoNormalizeAbout)
    since those differ from the x87 originals in the last bit (spin compares exactly without that). The
    keyboard task: no key, Enter x each dialog / none, F1 (0x1F), F2-F4, 0x1B, another key, at 2000 / 2001 ms,
    on a stand-in memory manager (arenas compared), the original with PartyMod's byte.
  - 52 deliberate bugs: 49 caught (5 after strengthening the tests: held times exactly at the jump time and
    the ramp start - a stand-in clock that stops - a ramp end before its start, a queued turn just over one
    frame's step, a tilt limit just above the angle, a total within the snap's 0.1), 3 equivalent (float vs
    double rounding of queued -+ pi: exact in double for these magnitudes; `queued <= 0` inside `queued != 0`;
    `speed >= 0` once speed is known non-zero).
  - Checks: build clean, thug2_check 0 MISMATCHED and all early tests pass; register scan only the known
    0x4EFF92 false alarm; call audit no MISMATCH; the overlap check "No overlaps", all handled;
    find_gaps nothing new next to these (its 1 thunk, 0x4B2350, was there before).
  - Shared files touched: CMakeLists.txt (src/hook/selftests_skaterB.cpp after selftest_runner.cpp;
    src/game/skater/rotation.cpp after src/game/obj/smtptr.cpp), the overlap check (HANDLED,
    after the 0x527636 line), src/game/net/chat.cpp/.h (KeyboardTask). PartyMod's folder unchanged.
- [x] Batch 63 (gfx; PartyMod round, gfx part 2; NOT run in the game yet): new src/game/gfx/render.* (namespace Nx), tests in
      new src/hook/selftests_gfx2.cpp (Early).
      - 0x004DB200 Nx::SetCamera (set_camera; cdecl matrix, position, horizontal angle in degrees, aspect, zoom, bool
        infinite): copies the camera position / orientation rows 2 (backward) and 1 (up) to 0x786CC4/0x786CDC/0x786CE8,
        world = identity (0x786B90), D3DX look-at (0x5C0985) into 0x786BD0, near 2, far **96000 (PartyMod's float at
        0x4DB35D; original 32000)** or the distance-clipping setting (0x7D6444, unsigned) x 18 when 0x7D643A,
        perspective (0x5C0ACD) w = 4 tan(angle/2), h = w / (aspect / 1.1), zoom != 1 (NaN too) scales x/y (D3DX
        scaling + multiply), infinite far plane writes _33 = -0.999999 / _43 = 1e-6, SetTransform world/view/projection,
        the view's half-angle tangents/sines/cosines (0x786E18-0x786E2C), the heading rotation (atan2 - pi/2, D3DX
        RotationY into 0x786C50), then the frustum update 0x4CDAA0 (thiscall on 0x775B80). The original's FPTAN / FPATAN
        are inline asm so the roundings match: the first tangent takes the half-angle unrounded (double), the second its
        float - different results for e.g. 30.0071 degrees. The original reuses its argument slots as scratch.
      - 0x004DF390 Nx::DrawScreenBlur (the motion blur pass; void): if 0x7CC244, fog off, render states, linear min/mag
        filters on 4 samplers (saved / restored), 0x4DEB70 if 0x705720, the quad (screen size 0x786A7C/80, u/v inset by
        (1 - amount/255)/2 over the texture's size - u rounded from the unrounded quotient, v from the float), colour
        0x80808080 or amount<<24|0x808080, blend 5 if the blur level 0x78732C > 2; draws the other texture into the
        current one (level > 2), then the current one onto the back buffer (cleared to the fog colour 0x78733C with fog
        and distance clipping, else 0x786CC0); swaps 0x786D68, fog back on. **PartyMod's JMP at 0x4DF63A**: the
        letterbox adjustment of the quad never runs (the original's is in a comment - it adds a pixel count to v).
      - Both REPLACE (hookable); both PartyMod patch addresses are in the overlap check's HANDLED list. PartyMod's
        runtime patches stay in PartyMod's folder (they only touch the originals' bodies, which our hooks bypass; the tests
        write the same bytes into the originals).
      - Tests: TestSetCamera - 5 orientations (all four headings, tilted) x 6 angles x 3 aspects x zoom 1 / 1.37 / NaN x
        infinite or not (bool with garbage upper bytes to the original) x distance clipping off / on / past 2^31; the
        original with PartyMod's 96000 written in; stand-in device (SetTransform recorded) and frustum update; every
        camera global compared. TestDrawScreenBlur - off / on x both textures current x 5 amounts (incl. past 2^31) x
        blur levels 1-4 x letterbox / fog / clipping / extra pass flags, screen and texture sizes, samplers linear or
        not; original with PartyMod's JMP; stand-in device, textures, surfaces and renderer state calls.
        41 deliberate bugs: 38 caught (1 after adding angles where the first tangent's rounding matters), 3 equivalent
        (texture size as signed - sizes < 2^31; u's rounded via float or double - float subtraction/division double-
        rounded to float is exact).
      - 0x004B2D80 Nx::RenderWorld (the frame's 3D; void, no arguments): frame counter 0x786AC4; nothing but the
        full-screen viewport while the device is lost (0x786D04). Otherwise the time 0x5BE970 -> 0x786D00, render
        states, 0x4DBBD0, 0x4DEA80, then per viewport (count 0x701378; 0x4A06D0; skipped without a camera at +0x18):
        0x6FA7B8 = index, the device viewport from its fractions (x/width from the int width, y/height from the height
        as a float; truncated), **PartyMod's setDisplayRegion when letterboxed (its calls at 0x4B2E65; the original
        moved y down H/8 and took H/4 off the height - in a comment)**, set_camera (angle 0x485B10, position 0x485A00,
        matrix 0x485A10, aspect 0x4A0D20 - in that call order), the world scenes (list 0x6FA7EC, ends at a node whose
        scene is -1; the next node taken before each; non-sky: 0x4D8ED0, meshes prepared once via flag 0x100 /
        0x4D6530, pass 0x55 via 0x4DC700), 0x490DB0, 0x4C7520, 0x48E370, pass 1, the sky (set_camera from the origin,
        infinite; fog range 20-21 set on the device and put back; fog off; sky scenes drawn at pass 3 with linear
        min / no mip filter unless distance clipping and fog are both on), 0x443870 before or after depending on
        0x786AB5, set_camera again, pass 0x82, non-sky pass 0x46, pass 0x102, then the later passes (0x4A0F30,
        0x49AB50(index > 0), 0x496640, 0x4A10A0 virtual, 0x4920E0, 0x49C840, 0x4DCC70(0), 0x491910(index, 0)).
        After the viewports: full-screen viewport (**PartyMod's setDisplayRegion when letterboxed, 0x4B33CA**),
        0x4DE980, the blur pass (ours), fog off, **the 2D through PartyMod's draw2DWrapper (0x4B341D; the original
        called 0x4DFB90 - PartyMod adds black bars)**, fog back, full-screen viewport again. The original passes the
        saved fog flag as a dword with garbage upper bytes (0x490DE0 reads only AL).
      - All three PartyMod patch addresses are in the HANDLED list.
      - Test TestRenderWorld (Early): 0-3 viewports (one without a camera) x 64 flag combinations (letterbox, fog on
        as 1 / 0x100 / off, distance clipping, fog setting, 0x786AB5, blur on/off) + the device lost, the scene list
        full (world unprepared / sky / world prepared / sky) or empty, three back-buffer shapes (PartyMod's black
        bars); the original with PartyMod's three calls, far plane and blur JMP written in; every callee stood in
        by a recorder (25 game functions, the device's viewport / render state / transform / sampler / draw calls);
        set_camera and the blur pass run for real (ours vs original). 38 deliberate bugs, all caught (3 after fixing
        the test - it dirtied the letterbox flag after setting it, and sampler 0's filters were equal; 3 by the crash
        the bug causes).
      - Test helpers in selftests_gfx2.cpp are copies of selftests_pc.cpp's CodePatch / JumpTo (a shared header
        would be nicer - left for the merger to avoid conflicts).
- [x] Batch 64 (movie; PartyMod round; NOT run in the game yet): movie playback 0x44FDC0 with its helpers, the playlist's
      next track 0x452200, and Sio::SetActuators pointed at Platform::SetActuators.
  - Nx::PlayMovieFile 0x44FDC0 (cdecl name; also reached by the jump 0x44F830: REPLACE_SOURCE_ONLY). Bink's sound
    system (BinkSetSoundSystem(BinkOpenDirectSound, the DirectSound object 0x7D6A38); "Bink can't use DSound"
    if it fails), the sound manager singleton (0x47BA60(false) / released 0x47C6C0 on every exit), Bink volume =
    max(music 0x450440, effects 0x47AFD0) * 0.01f * 16384.0f truncated (x87 53-bit); path
    "data\movies\bik\" + the name after its last backslash (the search never looks at name[0]: "\lead" keeps
    its backslash; '/' isn't a separator) + ".bik"; music paused 0x450650, streams stopped 0x450690(-1);
    BinkOpen (null: release, return); BeginMovie; the screen saver timeout read (SPI_GETSCREENSAVETIMEOUT) and,
    if nonzero, set again every 201st loop pass (resets its timer); fog off (0x490DE0) and back after. Loop
    (only with a texture; Sio manager referenced): app inactive -> BinkPause(1), Sleep(100) + window pump until
    active, loop (not unpaused there); quit request -> end; device lost -> reset 0x44F250, still lost ->
    loop, else BinkPause(0); BinkWait -> loop; BinkDoFrame, LockRect -> BinkCopyToBuffer(bits, pitch,
    height, 0, 0, 0x67D7C4 | 0x80000000) + UnlockRect; DrawMovie; end at frameNum >= frames - 1 (unsigned: a
    0-frame movie never ends by itself); window pump every 2nd frame; then the pads: ports 0-3, start (byte 2
    bit 3) or X (byte 3 bit 6) pressed after being seen up (control data bits are set while up) ends it.
    PartyMod NOPs the JNZ at 0x450121 that read the pads only every 5th frame - ours reads every frame (the
    original's line in a comment). Cleanup: the playing flag 0x696D95 off, fog back, EndMovie, BinkClose,
    0x696D90 = null, stage 0 alpha op MODULATE, sound manager released. Bink is called through the game's
    import table (0x6452E8-0x645320) until it's linked; SystemParametersInfoA / Sleep directly.
  - Nx::BeginMovie 0x44F8C0 (saves cull mode, multisample AA and sampler 0 min filter in 0x696D88/84/80; cull
    none, AA off, lighting off, 0x4DAB60(1/2, 0), min filter linear; the movie size (unsigned -> float) in
    0x67D7D0/D4 and the back buffer's in 0x67D7C8/CC; CreateTexture(w, h, 1, 0, X8R8G8B8, MANAGED): the texture
    + playing flag on, or null), Nx::EndMovie 0x44F9F0 (states back, 0x4DAB60(1/2, 1), Release, 0x4DA730(0,0,0)),
    Nx::DrawMovie 0x44FCC0 (BeginScene, Clear target black, DrawMovieFrame(0, 0, 0x67D7D8, 0x67D7DC, 1),
    EndScene, Present, BinkNextFrame), Nx::ButtonPressedAgain 0x44F840 (EAX data, ECX mask, ESI seen-up word,
    returns AL: naked stub, REPLACE_UNCHECKED).
  - Pcm::PlayNextTrack 0x452200 (new src/game/pcm/music.*). Ghidra has no function there: it's the target of
    0x452910's tail jump (JMP at 0x452A83), 0x452200-0x452676 (RET, then int3 padding); no other references
    (scanned the exe for calls/jumps/pointers). The overlap check now has an extra-bodies table for it.
    Logic: music on (DirectSound object, platform != 2, 0x67D7E0) and the playlist's byte +0xCA8 clear, no
    stream playing (0x453250 == 1); 0x69BAAC = 0; 0x451400; playlist 1: the script's integer 0xF18AB1F9 picks
    the track if < count (signed: negative indexes before the array); the special track 0x69806C (flag
    0x69BAA0) with 1; empty playlist: nothing. Reshuffle (flag 0x67D7F4): last = order[count-1] (with an empty
    order that's 0x696D9C, the movie dump counter), the on tracks (128-bit off masks at +0xC88/+0xC90, tested
    with __allshl semantics: shifts by the low byte, 0 from 64) into the order 0x696DA0 (count 0x67D7F8),
    Printf "shuffling random song order num_songs=%i", 2000 swaps of two random places, if order[0] is the last
    track: swap it with place Rnd(n-1)+1 (or 0 when n <= 1 - that draw is the game's even under PartyMod),
    position 0x69BAC4 = 0, played masks (+0xC98/+0xCA0) cleared. Else position+1, reshuffle next time at
    position >= n-1. Then the track at the position plays unless off or already played (the original repeats
    that same test `count` times - written once): played bit set, 0x4520D0(track, 0), current track 0x69BA98,
    script 0x300CAD34 run with {0x8D02705D: track} (a new CStruct, cleared and deleted).
    PartyMod: the two swap draws (calls at 0x4523A7/0x4523B4) go to its our_random: draws the game's Rnd(n)
    (thrown away) then returns rand() % n (srand(time) at start-up) - Pcm::ShuffleRandom. our_random is
    declared void but returns the value; the test (original with PartyMod's calls written in vs ours) shows
    its EAX is rand() % n. ShuffleRandom returns the game's 0 for n == 0 instead of dividing by zero
    (PartyMod would crash on a playlist with tracks but all of them off; the original wouldn't).
  - Sio::SetActuators (device.cpp) now calls Platform::SetActuators (PartyMod's set_actuators) instead of
    0x5F3A50. TestSio and TestSioManager write PartyMod's jump at 0x5F3A50 -> set_actuators (as in the game)
    and jump from set_actuators to a recorder, so both runs trace the motor levels there (the DirectInput
    effect stand-ins are no longer reached in TestSio). TestDetour moved up in selftest.cpp for that.
  - PartyMod's runtime patches (patchMovieSkip, patchPlaylistShuffle) are left in PartyMod's folder, as batch 60
    did: they only touch the originals' bodies, which our replacements bypass, and keep PartyMod's behaviour
    if a replacement is disabled in thug2-decomp.ini.
  - Tests (selftests_pc.cpp): TestPlayMovieFile - 8 runs (sound set-up fails + no movie; the texture
    can't be made; plays out with waits, failed locks and port 0's buttons held from the start (no skip);
    start pressed on port 2; X on port 3 with the app inactive and the device lost for two resets; 450 frames
    with the screen saver reset twice and quit requested; 0 frames (quit); 1 frame) - the original with
    PartyMod's NOP vs ours, Bink through stand-ins in its import slots, SystemParametersInfoA stood in at its
    import slot and in user32 itself, the D3D device/texture, the renderer, sound, Sio, window pump,
    DrawMovieFrame and Dbg::Printf stood in (game addresses and our functions). TestButtonPressedAgain - the
    register stub vs the original over masks/bytes/seen bits/null data. TestPlayNextTrack - music off three
    ways, +0xCA8, stream states 2/0, playlist 1 (in range, past, negative), the special track, empty, 45 tracks
    of a 20-track playlist (3 + 1 off, one played early, one switched off mid-round), 210 of a 100-track
    playlist (off in both halves, track 63 too), one track on (n = 1), a negative count - the original with
    PartyMod's two calls written in, rand reseeded and the game's random streams reset for both runs.
    TestPlayMovie now also stands in for ours' direct call to PlayMovieFile.
    74 deliberate bugs: all caught, 2 after strengthening (the app-inactive path wasn't reached - the
    every-other-frame pump re-activated the app first; track 63 wasn't off anywhere). stub_order is caught by a
    crash (swapped stub arguments). Two early runs were reported caught only by "PC async handle" (flaky,
    probably parallel worktrees' temp files): rerun, caught by the movie test.
  - Checks: build clean, thug2_check 0 MISMATCHED / 72 early tests pass (after merging main), register scan (only the known
    0x4EFF92), call audit (run against this worktree - the script hard-codes C:\Dev\thug2-decomp\src): no
    MISMATCH (the two "?" are tail jumps: 0x490DE0 -> 0x4B8E40, 0x44F250 -> 0x44EE90, both plain RET),
    the overlap check: no overlaps, 3 new HANDLED entries (0x450121, 0x4523A7, 0x4523B4).
  - Not done, next to it: the SingletonPtr destructors 0x44FD40/0x44FD50 (MOV [ECX],vtable; JMP release) and
    their scalar deleting destructors 0x44FD60/0x44FD90 (vtables 0x6472B4/0x6472B8, shared with other
    functions), 0x44F870/0x44F890, and the Pcm API around 0x450220-0x4504B0 and 0x452680/0x452910 (the
    caller). find_gaps' one thunk (0x48C1A0 -> SetLetterbox) predates this batch.
Shared files touched: CMakeLists.txt (src/game/pcm/music.cpp after movie.cpp), the overlap check
(HANDLED + the extra-bodies table), src/hook/selftest.cpp (TestSio/TestSioManager, TestDetour moved up),
src/hook/selftests_pc.cpp, src/game/sio/device.cpp/.h. No changes to PartyMod's folder.
- [x] Batch 65 (misc; PartyMod round, misc (NOT run in the game yet; early tests pass offline; branch pm/misc): three functions
  PartyMod patches, with its changes built in (the original's logic kept in comments).
  - 0x004E1EF0 `Sys::Main(argc, argv)` (src/game/sys/main.cpp; cdecl, returns 0): Config::Init 0x5B60B0, standard
    heaps, the language start-up (German 0x4E0FE0, else 0x4E0DA0), the argument dump through Dbg::Printf, "demo" as
    the only argument sets 0x7CC978, SetThreadExecutionState(continuous | system | display), InitialRand(0x6680BF3),
    sin table; then while the quit flag (0x7D6A2C) is clear: bottom-up heap pushed (virtual slot 0), symbol table,
    hash item pool (0, 10000), Pre manager, timer, async file system, the 23 singletons (system then game; their
    holders' vtables 0x64AFB0-0x64B000 on the stack are only for exception unwinding), three tasks into the main
    loop manager's lists (+0x7C: Mdl's and Sio's tasks; +0x04: 0x44E7F0's), six modules registered and started
    (in two different orders), the engine start 0x48C1D0(350), MainLoop. **PartyMod: fastExit() after MainLoop**
    (in place of the original's PopMemProfile call at 0x4E2492) - the process ends there. The teardown after it
    (stop all modules, five unregistered - 0x476040 never is -, the three task lists emptied, releases in reverse,
    Pre::DestroyManager, heap popped) is kept as code: reached only if fastExit returns (in the test). The memory
    profile push/pops (empty 0x5480A0 calls) are kept with their names.
  - 0x00545880 `Net::ServiceTask(task)` (src/game/net/service.cpp; cdecl task callback, the network manager is
    the task's +0x1C): 18-state machine at manager +0x1B8 - wake-up timer (state 0), GameSpy availability check
    (3-5; "mototrax"), news (6/A start a suspended download thread 0x545540 - its id via 0x533240, ResumeThread;
    7 waits; B ends), message of the day (8 thread 0x545620, C waits 15 s, 9 shows it), lobby list (D-11).
    **PartyMod's MOTD fix**: news timeout 500 ms (original 15000, 0x545B4F), script 0x588ED87C (original
    0xD4E84C8D, 0x545CDE) and +0xFE09 = 1 (original 0, 0x545CE8). Note 0x6450E0 is ResumeThread (not
    CloseHandle): the thread handles are never closed (as in the original).
  - 0x005A8730 `Mc::LoadFromMemoryCard(params, script)` (src/game/mc/memcard_cfuncs.cpp; CFunc): Name / Type /
    integer 0x8C6808D4 ("no apply"), path per platform (0x7CEFA8: 1-3 console folders, 5, 4/7 the card's
    ConvertFilename of the type's pattern), open, size must equal the type's (0x5A7340), read, header {crc, ?, ?,
    size, version}: size <= 500000 and the type's version (0x5A6210); two structures read (0x47A0C0), the second's
    bytes minus 0x69 for type 0xCA41692D; applied by 0x5A81B0 (EAX type, ESI contents, stack script, params) or
    kept in 0x7CECD4/0x7CECD8 (type 0x61A1BC57 also applied with params null); RunScript 0x0EB9402D when applied.
    Failure checksum 0xFBF9C22A into the script's params. Music/streams paused unless platform 4. **PartyMod:
    the save CRC isn't checked** (its NOP of the JNZ at 0x5A8A01; the CRC test kept in a comment). The type
    helpers take register arguments (EAX/ECX/ESI/EDI): called through inline asm.
  - Tests (src/hook/selftests_misc.cpp, Early): a generic recording stand-in (`StandIns`: PUSHAD thunk calling a
    std::function handler, RET n) written over every callee - the game's address and our own version alike, and
    kernel32's CreateThread / ResumeThread / SetThreadExecutionState (plus the game's import slots) - with
    PartyMod's patch bytes written into the original. TestServiceTask: every state incl. out of range, wake
    times around now, all GameSpy answers, message counts 0/3/-2, both timeouts on both sides of their limits
    (and wrapped), finished flag, lobby empty or not; manager memory compared. TestLoadFromMemoryCard: 10
    platforms, no card / converted name / file, missing name/type, wrong size, short read, header size
    500000/500001/negative, wrong version, the obfuscated type (with and without bytes to decode), kept or
    applied, null structures. TestMain: 7 command lines (demo, demox, dem, extra args, argc 0), languages, quit
    already set, 1 and 2 loops; every call with its arguments, the demo flag, 0x6FA7D0. 50 deliberate bugs (20 service, 15 memory card, 15 main), all caught by the test meant to catch
    them (s_motd's first run showed a one-off failure of TestAsyncHandlePc instead; rerun alone, caught by
    TestServiceTask; five clean baseline runs after).
  - The overlap check's HANDLED list: 0x4E2492, 0x545B4F, 0x545CDE, 0x545CE8, 0x5A8A01.
  - PartyMod's folder unchanged: its runtime patches stay (bypassed by our functions; still in effect if a
    replacement is disabled in thug2-decomp.ini).
  - Register scan (only the known 0x4EFF92 false alarm) and call audit (126 GameFunction uses, no mismatch)
    clean; the overlap check clean.
  - Not done (find_gaps now lists them between 0x4E1950 and 0x4E1EF0): the ~50 small functions at
    0x4E1970-0x4E1EE0 - the singleton holders' methods (11-byte `this->ptr = arg; return this` constructors,
    33-byte destructors / vtable entries of the holder vtables 0x64AFB0-0x64B000) that main's stack holders use
    only for exception unwinding. They belong with a SingletonPtr<T> class; next to pick up.
- [x] Batch 66 (text; PartyMod round, menus/text; NOT run in the game yet): new src/game/gfx/text.* and src/game/gfx/device.*
      (namespace Nx), tests in new src/hook/selftests_text.cpp (Early).
      - Layouts: `SFont` (+0 height, +4 base, +8 SChar[], +0xC map[256], +0x10C special map, +0x134/+0x136 int16
        char/space spacing, colours from +0x138 - "\c<i>" reads +0x134 + 4i, past the table for i > 16), `SChar` (0x18:
        u0 v0 u1 v1, uint16 w h baseline), `STextEffect` (10 bytes: int16 x, y, xscale, yscale /16384, rotation
        radians*16384), `TextVertex` (0x1C), `SText` (vtable 0x64A89C: [0] dtor 0x4CFAF0, [1] BeginDraw 0x4CEF00 - buffer
        = 0x779FD8, [2] Draw, [3] EndDraw 0x4CEF10; +0x10 z, +0x14 font, +0x18 effects, +0x1C text, +0x20 x/y, +0x28
        scales, +0x30 pivot, +0x38 angle, +0x3C RGBA colour, +0x40 fixed colour). Vertex buffer 0x780FE8, count
        0x780FE4 (flushed at 0x400 via EndDraw + BeginDraw), button font 0x780FDC, texture font 0x780FE0.
      - Escapes (the jump tables): `\\` (the backslash is a character), `\c<d>` colour (QueryString skips 2), `\s<d>` /
        `\S` special glyph, `\b<d>` / `\m<d>` buttons (`\m` through the meta button map 0x48D6A0, then DehexifyDigit on a
        local), `\u` / `\U` skip 2, **only capital `\L` skips 1 (`\l` is drawn as an l)**, other letters are drawn. An
        escape that places a glyph (or a key name) ends the escape chain; a backslash as its digit is then a character.
      - 0x004CECB0 `SFont::QueryString(text, float* width, float* height)` (thiscall, RET 0xC; reached via 0x4B3D10):
        width = sum of (glyph / space / character width + this font's char spacing); key names add each character's
        width through the map indexed by *signed* char; the digit itself isn't counted; height = the font's (unsigned).
      - 0x004CFD00 `SText::Draw()` (thiscall, no args, also virtual [2]): screen position/pivot through 0x786D80-8C,
        corners relative to the pivot in whole pixels (_ftol) turned by the text's angle (x87 FCOS/FSIN, as inline asm;
        left/top offsets go through floats, right/bottom don't), per-character effects (offset, scale, rotation about
        the centre - quirks kept: the centre is negated and offset by the pivot's **x** on both axes, the corners are
        turned by sin/cos swapped (x' = dx sin - dy cos, y' = dy sin + dx cos), the effect's x offset carries into the
        next character); quads (LT, LB, RB, RT); button glyphs switch to the button font for one character (EndDraw /
        BeginDraw before, texture font 0x780FE0, and after); key names drawn by DrawKeyChar; spaces advance (space +
        char spacing) and skip the rest; xscale or yscale 0 draws nothing.
      - 0x004CFB20 `SText::DrawKeyChar(char, float* x, float y)` (thiscall, RET 0xC; only caller Draw): one plain quad,
        top/bottom kept at double precision until stored, x advanced by width + spacing.
      - 0x004D8650 `InitDirect3D()` (cdecl void; callers 0x4B25F0, 0x4D89C0 read nothing back): resets the screen
        conversion, Direct3DCreate9(31) via 0x5BEB14 (null: MessageBoxA - text "Error", caption the long message, the
        wrong way round - and return), GetAdapterDisplayMode (failure: return), present parameters 0x786A7C (hwnd
        0x7D6A28, discard, 1 back buffer, auto depth D24X8, X8R8G8B8, 2x multisample if 0x7D6434), sizes
        1280x1024 / 1024x768 / 800x600 / 640x480 from index 3 - setting(0x7D643C) (unsigned clamp to 3), 0x4D7C80
        (cdecl w, h, hardware vertex processing, multisample) tried for {hw, ms} {sw, ms} {hw, -} {sw, -} at each size
        down, the game's exit(0) (0x624AFE) if none works, scale = back buffer / 640, / 480 (unsigned), offset y =
        _ftol(scaleY * 16), GetCreationParameters + CheckDeviceFormat(D24S8 texture) -> 0x786AB9, clears
        0x786D1C-3C and 0x786D64, then 0x4D7D70, 0x4D8420, 0x4D7B60, 0x786D90 = 0x5BA1D0() (returns 0), 0x5BE7C0.
      - PartyMod built in (original logic in comments): glyph.c - `dehexifyDigitWrapper` for `\b` (0x4CED4B / 0x4CFF0C)
        and `shouldUseGlyph` decides glyph or key name (0x4CED6D / 0x4CFF36; originally glyph for 4-7 and 10-13);
        window.c - no ShowWindow (0x4D869C), Windowed = its `isWindowed` (0x4D8720), the 640x480 slot = its
        `resX`/`resY` (0x4D8776 / 0x4D8792), no SetWindowPos (0x4D8891), no ShowCursor loop (0x4D8989). All 28 patch
        addresses are in the overlap check's HANDLED list. PartyMod's folder is unchanged: its runtime patches only
        touch the originals' bodies (bypassed by the hooks; the tests write the same bytes into the originals), and the
        variables/functions used are already global.
      - Tests (Early): `TestQueryString` - 17 texts covering every escape x 2 fonts x PartyMod's three input states
        (keyboard, controller, keyboard + PreferGlyphs), key names stood in (0x5BCD50; empty, plain, chars > 0x7F),
        the meta map filled, the original with PartyMod's bytes. `TestDrawText` - the texts plus a 300-character one
        (buffer flushes) x 8 setups (screen scales/offsets, rotated text, pivots - one far away, past 2^24 - effects
        with and without rotations, fixed colour, zero scales) x the three input states, stand-in vtable (BeginDraw /
        EndDraw record the buffer); and DrawKeyChar directly, with the buffer one quad from full or not.
        `TestInitDirect3D` - 36 scripts (no Direct3D, display mode failing, settings -1..7 x multisample x windowed,
        devices failing 0-4 times or always (the game's exit, stood in by a throw - a returning exit would unbalance the
        original's stack: it doesn't pop exit's argument), PartyMod's sizes incl. negative (unsigned), another created
        width, the depth check both ways); Direct3D, the import table's MessageBoxA and the callees stood in; the
        original with PartyMod's bytes.
      - 61 deliberate bugs: 59 caught (4 after strengthening: a backslash as the digit, DrawKeyChar's own flush, a far
        pivot for the float-rounded offsets, MessageBox text/caption; one equivalent mutation replaced), 2 not
        observable: rounding the pivot sum (pivot + y) to float before scaling (the later float rounding and _ftol
        swallow it), and the sine rounded to double before the product in the effect rotation.
      - Register scan (only the known 0x4EFF92 false alarm; nothing reads EAX after the four), call audit (no MISMATCH),
        overlap check (no overlaps, all handled): clean. find_gaps: nothing new (the neighbours 0x4CEED0-0x4CFAF0 -
        SText ctor/dtors, BeginDraw/EndDraw, 0x4CF0F0 - are more than 0x800 apart from these and still to do;
        the SetLetterbox thunk 0x48C1A0 is from batch 55).
      - Note for the game: Direct3D sets the x87 to 24-bit precision on its thread (no D3DCREATE_FPU_PRESERVE in
        0x4D7C80), so in the game the originals' x87 code (all of these, like earlier batches') rounds differently from
        our double-precision code; the tests compare at 53 bits.
      - Also: tools/check/main.cpp (from main) catches the game's own /GS report; this agent's earlier runs hit it
        through the returning exit stand-in (fixed by the throw).
- [x] Batch 67 (skaterA; PartyMod round; NOT run in the game yet) Two skater core physics functions with PartyMod's PS2 controls
      built in (new src/game/skater/core_physics.*; `Skater::CorePhysicsComponent` - the class skaterB's
      rotation.* uses - now declared there, rotation.h includes it):
      - LipPushOff 0x004FCBA0 (thiscall, two unused stack arguments - a script member function, in the table
        a static initialiser writes at 0x00682970; RET 8; returns int 0/1): the facing (object +0x8C) flattened
        to x, z and normalised (only if its length > 0; w copied, not scaled); the side vector (z, 0, -x, 0).
        Input +0x80 held: 0. Both triggers (+0xC0, +0x120; PartyMod's JZ -> JNZ at 0x004FCC59 makes it either):
        velocity (object +0xAC) += facing * physics float 0x1F96C224 (0x00580400). +0x60 held longer than
        physics int 0x3080E9E7 (0x00580440, timer 0x0046B920): += facing * 0xDC818B7E. +0x20 held longer than
        0x9B25142A: -= side * 0x1AB3809F. +0x40: += side * the same (through Mth's operator* / +=). x87 detail
        kept: in the three inlined pushes the x component's product isn't rounded to float before the add
        (computed in double). Slot map (entry ESP = R): R-0x20.. facing (x, 0, z, w), R-0x10.. side; the +0x40
        branch's product goes into the facing's slot (hidden return of 0x004024A0), its scale into the pushed
        argument slot. The handoff's "slot left from an earlier call's arguments" is that scale; nothing reads
        a stale value.
      - CheckForGround 0x00509BD0 (thiscall, void): the feeler line (+0x208 = pos + up * physics 0xE4D79235,
        +0x218 = pos + up * -200) and the feeler check 0x005003A0(0x10, 0). Hit, not skatable (+0x228 clear):
        if the distance along up < 0.001, position = hit point + hit normal; then leave. Hit, skatable: the
        facing rotated into the hit's plane and into +0x68's (Mth::RotateToPlane); leave if facing . normal > 0
        and the rotated facings' dot is in (0, cos(physics angle 0x4138283E with input +0x20 held, else
        0xEF161C2A)); if above the hit, leave if the distance > max(tan(acos(clamped dot)) * frame movement,
        physics 0x899BA3D0) (the original's FCOS / CRT acos + FPTAN on x87: ours in double - only compared).
        Kept: timed flag +0x40 = +0x229, 0x004FE860(normal by value), position = hit point, timed flag +0x98
        set when flags +0x18 == 0, distance > 2 and the dot > 0.99, 0x005196A0(+0xFA byte), +0x1514's +0x1C,
        0x004FEA50, 0x005021D0, the two trigger checks 0x00434BF0 (0x0064BBA4 / 0x0064BBB0 - with an early
        return when +0x1530's +0x26 is set) when +0x1AC != +0xFC, 0x005232A0(0) unless +0x1518's +0x13B0,
        0x0043A930, +0x1514's +0x1D. Leaving: 0x004FDED0(1), the object's 0x00460A20 / 0x004609F0 events,
        0x004FEBA0; flags +0x30 set: timed flag +0x48 on, BreakVert(false) unless both triggers - PartyMod only
        flips the first JZ (0x00509E47), so under its PS2 controls the call is skipped only when +0x120 is held
        without +0xC0 (ported as is) - then +0x48 off once input +0x20's release and press times both exceed
        script int 0x6BB5B751; flags +0x30 clear: both triggers (PartyMod's 0x0050A257: either) -> the acid
        drop search 0x00502240 and, if found, 0x00502F20; then the trigger check 0x0064BB80.
      The overlap check: the six patch bytes are HANDLED. PartyMod's run-time byte patches in
      PartyMod's input.c are left in place (like batch 57's): they only touch the originals, which the game
      no longer runs while ours are installed, and keep PartyMod's behaviour if ours are disabled in the ini.
      Early tests (new src/hook/selftests_skaterA.cpp): a generic recording stand-in harness - each callee's
      first bytes get a thunk to one naked entry that records the call (`this`, stack arguments with arena
      pointers made relative and stack pointers marked, data behind input pointers), fills output buffers,
      returns seeded values (int / bool / float in ST0) and pops what the real callee pops, keeping every
      register but EAX; our own BreakVert is stood in through its code address too. The original's
      RotateToPlane is pointed at ours (not bit-identical, tested separately). LipPushOff: 3000 variants
      (facing flat / zero, buttons, timers) x PS2 off / on; CheckForGround: 6000 variants (hit or not,
      skatable, angles, distances, flags, triggers, a stand-in symbol table for 0x6BB5B751) x PS2 off / on
      (at least 300 must land). The whole arena and the call trace compared. 54 deliberate bugs: 50 caught,
      4 equivalent at float precision (the order of the distance's double sum, sqrt in float vs double,
      `<` vs `<=` against an irrational cosine, the clamp at -1 - tan(acos(-1)) is ~0 like tan(acos(1))).
- [x] Batch 67, cont. (PartyMod round; NOT run in the game yet) DoAirPhysics 0x0050CA00 (thiscall, void; the air update - the
      caller at 0x0051094F is in code Ghidra never disassembled) in core_physics.cpp, with PartyMod's three
      trigger checks built in (0x0050CAC4 air recovery: flags +0x80 or the triggers -> 0x00504B40; 0x0050CF89:
      the triggers or +0x2D0, not wall riding, rising -> BreakVert; 0x0050DA64: the acid drop - all "both" in
      the original, "either" under PS2 controls). Read in full from the disassembly (stack map entry ESP = R:
      R-0x80 the input + 0x20, R-0x78 the ride's carried offset, R-0x68 the velocity copy - later reused for
      the wall direction and the hit normal -, R-0x34 gravity from 0x004FF470, R-0x7C the object): timed flags
      +0x40 off / +0xB0 on; 0x00504910, 0x004FFB90, Spin, 0x00504A60, 0x00504B40, 0x00504DF0, 0x004FE730
      (+0x320 != 0); the countdown +0x14CC (0x00507500, facing into +0x14F0's plane at 0); wall ride +0x78:
      flags +0xE4 added to the velocity copy, or 0x0042BBF0(0.1) when falling below +0x37C; what the skater
      rides (+0x152C's +0x18: 0x00416F00 / 0x00427C70) carries it by its +0x5C; the move: position +=
      v t + (0, g, 0, 1) t^2/2 (rounded as the x87 does: x's product and the z, w sums unrounded) + carried,
      velocity += (0, g, 0, 1) t; flags +0x48 (on a wall): feeler up +1 to up * -23, off it BreakVert and +0x48
      off once input +0x20's times pass script int 0x6BB5B751, +0x48 off after script int 0x26495947 ms;
      wall feelers across the wall normal +0x278 (* 30), stepped (+0x298, halved; then 10 steps of 3 down);
      a hit still along the wall (sqrt|wall.xz . normal.xz| > 0.02) moves onto it (height +0x26C kept as the
      maximum), pushed off by script float 0x78384871, 0x004FE860(flattened normal), velocity into its plane;
      the display matrix (+0x18) copy or 0x004FEA50; 0x00508780 from the last position + carried, return on
      flags +0x18 == 2 / 5; the landing feeler: steep (normal y < 0.1) or fast (vel y > 10) landings wait for
      the ride's 0x004FC400 > 500 after 0x004FFDB0; landing on something skatable is LandFromAir (a private
      helper, part of 0x0050CA00: 0x004FDED0(0), 0x005021D0, +0x372-0x374, 0x004FC470, 0x00518CF0(speed /
      0x00580790's value, flags +0xE1), 0x005232A0, 0x00506BA0, trigger check 0x0064BB98; then unless the
      trigger took over: standing landing (no x/z velocity, +0x40 held) clears vel y, else velocity into the
      ground's plane - from a wall ride along +0x490 unless 0x004FF590 says it turns up, at least script float
      0x59484878 (0x0042BBF0) - and 0x004FC370(10); timed flag +0x30 off, +0x2D0, the ground normal into
      +0x58/+0x68/+0x78 and the object's up, OrthoNormalizeAbout(1), the display matrix; event 0x532B16EF);
      not skatable: velocity and facing into its plane, OrthoNormalizeAbout(2), pushed off by physics float
      0x23410C14, timed flag +0xB0; no hit: 0x0050C930 unless +0x30; then 0x00504EE0, 0x004FFB10, 0x00503710 and
      the acid drop (0x00420250 since +0x380 < 250 when +0x380 > +0x4EC -> 0x00502240's flag). Calls Spin and
      BreakVert (skaterB's) directly. Early test TestDoAirPhysics: 12000 variants (flags, ride, buttons, wall
      ride and feelers, landing skatable or not, from a wall ride or not, the clock offset 0x00696D60 held at 0
      so times can meet exactly) x PS2 off / on, 42 stand-ins (our Spin / BreakVert through their code
      addresses), the original's vector / matrix calls (RotateToPlane, ProjectToPlane, SetDirection, Normalize,
      Length, LengthSqr, OrthoNormalizeAbout) pointed at ours, four script symbols; coverage counted (landings,
      from wall rides, wall hits, push-offs, acid drops). 60 deliberate bugs: 59 caught (4 after
      strengthening: 500 and 250 among the stand-ins' ints, the clock offset at 0, x-only zero velocities), 1
      equivalent (x's position product rounded first: the sum adds an exact 0). The two earlier tests still
      pass with the new int values.
- [x] PartyMod round status (2026-10-16): 38 of the 48 patched functions have source (batches 54-60, 62-67).
      Left (WinMain / MainLoop since done, batches 68-69): WinMain 0x4E2680 and MainLoop 0x44EEF0 (both host the test runner's hooks, src/hook/selftest_runner.cpp,
      with ParseNetworkParams 0x4E1220 - plan that first; Sys::Main calls MainLoop through the game's address so
      the runner's hook still fires); the 8 GameSpy functions (0x5F8690 and up: library code - PartyMod's OpenSpy
      host names become configuration of the linked SDK). Afterwards: move PartyMod's helpers our code calls
      (shouldUseGlyph, dehexifyDigitWrapper, setDisplayRegion, draw2DWrapper, movie_setTexture/movie_drawPrim,
      fastExit, set_actuators, the window settings) into the game's source where it makes sense, and drop
      the PartyMod folder's runtime byte patches once nothing needs the originals (they stay for now so a replacement
      disabled in the ini still runs PartyMod's patched original).
      Neighbours left by the agents (find_gaps): SText ctor/dtors 0x4CEED0/0x4CEEF0/0x4CFAF0, BeginDraw 0x4CEF00,
      EndDraw 0x4CEF10, 0x4CF0F0, 0x4CFA80; ~50 SingletonPtr holder methods 0x4E1970-0x4E1EE0 (vtables
      0x64AFB0-0x64B000) and 0x44FD40-0x44FD90; 0x44F870, 0x44F890; Pcm 0x450220-0x4504B0, 0x452680, 0x452910;
      the thunk 0x48C1A0 -> SetLetterbox; 0x4B2350/0x4B2360.
      Learned: Direct3D drops the x87 to 24-bit precision when it creates the device, so in the game the
      originals' float code rounds differently from ours (offline tests compare at 53 bits).
      TestPcAsyncFileHandle failed once in two agents' runs under load (several thug2_check runs at once) and
      passed on rerun. thug2_check never shows a dialog: CRT reports, run-time check failures and the game's own
      /GS handler (0x6287DD) print and exit 3 - a stand-in for a function that never returns (exit) must not
      return. tools/mutate.py stops before its final rebuild if a spec line isn't unique. Reusable harnesses:
      StandIns (selftests_misc.cpp), the recorder in selftests_skaterA.cpp; selftests_gfx2.cpp has its own copy
      of CodePatch/JumpTo.
- [x] Batches 60-61 tested in the game (2026-10-16, by the user): game runs; 650 of 650 replacements installed
      (31 source only), early 62 / late 16 / engine-started 2 self-tests pass, shadow checks no mismatches; the
      six intro movies skipped with skip_intro_movies=1. (That DLL predates batches 62-67.)
- [x] Batches 62-67 tested in the game (2026-10-16, by the user): game runs; 672 of 672 replacements installed
      (32 source only), late 16 / engine-started 2 self-tests pass, shadow checks no mismatches. Early: 80 of 81 -
      TestPlayMovie assumed the skip_intro_movies flag off (true offline, not in the game with the ini's flag on).
      Fixed: Nx::SkipIntroMovies() is settable; the test runs flag off (original vs ours) and then ours flag on
      (intro skipped, "credits" queued; caught a deliberate bug that ignores the flag).
- [x] Test stages from game source (NOT run in the game yet): RunStageOnce(stage) (hook/selftest.h) - stop if
      replacements failed, then that stage's tests once (EngineStarted also logs the shadow summary). The runner's
      ParseNetworkParams / MainLoop hooks are REPLACE_FALLBACK(_UNCHECKED): installed only while no game source is
      hooked at the address (absent, source only, disabled, or failing the argument-size check); a fallback is
      disabled only by `*`, its file or its full name. Game source calls RunStageOnce itself (batches 68-69).
- [x] Batch 68 (winmain agent; NOT run in the game yet): WinMain 0x004E2680 and ParseNetworkParams 0x004E1220 -
      src/game/sys/winmain.cpp (`Sys::WinMain`, `Sys::ParseNetworkParams` / `Sys::ReadNetworkParams`).
      - WinMain: GameSpy's back-end check ("thps6pc", waits 5 ms at a time; "The backend is not available"), PartyMod's
        initPatch, ParseNetworkParams, an already running copy (FindWindowExA by the title "THUG2") brought to the
        front (SW_RESTORE, BringWindowToTop, SetForegroundWindow; returns 0), PartyMod's createSDLWindow, the window
        events, no window -> 0, srand(time) on the game's C runtime, the command line split at spaces into at most 20
        arguments (argv[0] is the first word, not the program name), the CD ("THUG CD2"), DirectSound, the 72 default
        bindings (k0_/gp0_/gp1_ 0..23 through Sys::WriteDefaultSetting), Platform::InitInput with the settings key,
        the 256 key names from MapVirtualKeyA (acute accent -> ', ; -> :) and F1-F15, Sys::Main (called directly),
        then the original's shutdown (DirectSound, 0x004908C0, Direct3D, InvalidateRect, minimise, _cexit,
        Mem::ShutDownManager, _exit(0)) - never reached with PartyMod (Main's fast exit ends the process).
      - PartyMod, built in (each with the original in a comment): the 0x004E26A0 call into its cave 0x005480A1
        (LoadLibraryA of PartyMod's DLL by name, then on to GSIStartAvailableCheck) loads nothing - PartyMod is in our DLL and
        attached in DllMain; 0x004E26DC networkParamsWrapper -> initPatch() then ParseNetworkParams; 0x004E2767
        createSDLWindow in place of 0x005F5860 (the game's window: class/title string 0x0064B190, 100,100 640x480,
        WindowProc 0x004E16B0, icon 0x66, full screen); 0x004E277D ShowWindow left out; 0x004E2C16 the DirectInput
        release 0x005F39E0 left out. The title string 0x0064B190 is 25 characters in the original ("...nd 2", most
        likely "Tony Hawk's Underground 2"; the retail exe isn't here to check) - PartyMod's exe patch leaves "THUG2"
        and puts its DLL's name in the rest.
      - ParseNetworkParams: runs RunStageOnce(TestStage::Late) first (the Late stage, where the runner's hook ran it
        before), then options found in an upper-case copy, values read from the command line itself: -HOST (needs
        the trailing space) -> 0x007CC260; -HOSTNAME -> 0x0067EFBC [15] (default "Beenox"); -EMAIL -> 0x007CCA90
        [255]; -PPASS -> 0x007CCB90 [15]; unless hosting, -CONNECT (value from 9 on: one character after the option
        skipped) -> 0x007CC980 [255], inet_addr or gethostbyname -> 0x0067EFB8, joining 0x007CCA80; -MAXPLAYERS
        2..8 -> 0x0067EFD0; -GROUP -> 0x007CC264; -NTEAMS (>4 -> 0) -> 0x007CCBE0; -SKILLLEVEL (>4 -> 4) ->
        0x0067EFD4 (all unsigned: negative is too big); -NAME -> 0x007CCBC4 [15]; -PASSWORD -> 0x007CCBD4 [9],
        moved to 0x007CCDFC when joining. Values: spaces skipped, up to a space or between quotes.
        Differences (documented in the source): ours looks at the first 1023 characters (the original's strcpy into
        1024 bytes overran its stack); our atol is the UCRT's (VC7.1's wraps round on overflow, the UCRT's may
        not - numbers that overflow aren't tested).
      - Uses our C runtime (strstr, _strupr_s, atol, snprintf, mbstowcs_s) except srand/time/_cexit/_exit (the game's:
        its rand state and clean-up); ws2_32 linked for inet_addr/gethostbyname (CMakeLists.txt).
      Tests (src/hook/selftests_winmain.cpp):
      - TestParseNetworkParams (Early): 41 command lines (one with no space after each option) x hosting/joining set beforehand or not, from patterned
        globals; the game's _strupr/atol stood in by VC7.1's "C"-locale behaviour (independent of our CRT),
        inet_addr/gethostbyname by recording stand-ins (found, not found, address bytes with top bits set); every
        written global compared.
      - TestParseNetworkParamsLate (Late, in the game only - not run yet): the same cases without name lookups,
        the original on the game's own C runtime.
      - TestWinMain (Early): 12 cases (back end up / down / waiting, running already, no window, the window changed
        by the events, empty/space-led/19/20/21/25-word command lines) with every callee and Win32 import stood in
        by recording code (StandIns), PartyMod's patch bytes written into the original, the original's LoadLibraryA
        counted (once per case, PartyMod's DLL; ours never); _exit's stand-in throws back to the test. Covers the key
        name renames (also with bits above the character), mbstowcs widening (VC7.1's behaviour expected - the UCRT
        matches), the window read again at each use.
      63 deliberate bugs: 59 caught (2 after strengthening: the -GROUP / -NTEAMS value offsets, caught once a command
      line has no space after the option), 4 equivalent (the spaces skipped before atol, which skips them too;
      the max-players clamp at > 7 -> 8; the window read into a local right before InitDirectSound).
      Checks: thug2_check 0 MISMATCHED, 83 early tests pass; register scan: only the known 0x004EFF92 alarm, WinMain
      returns int; call audit: no MISMATCH (0x005F3BF0 "?" - it tail-jumps to wcscpy, cdecl checked by hand);
      the overlap check: no overlaps, every WinMain patch handled.
      Fallback fix (src/hook/hook.cpp): a game-source replacement left out for a wrong argument size no longer
      keeps the runner's fallback at its address out, so that stage (and StopIfReplacementsFailed's message) still
      comes. Not testable offline (InstallReplacements doesn't run in thug2_check).
      Note on disabling: Sys::WinMain calls our ParseNetworkParams and Sys::Main directly (the repo's convention),
      so disabling only 0x004E1220 in thug2-decomp.ini doesn't bring back the original's parse while WinMain is
      ours; disable winmain.cpp for both (then the runner's fallback runs the Late stage as before).
      Shared changes: StandIns and its helpers moved from selftests_misc.cpp into src/hook/standins.h (namespace
      TestStandIns, header-only) for reuse; the overlap check's HANDLED list gets 0x004E26DC, 0x004E2767,
      0x004E277D, 0x004E2C16; CMakeLists.txt: winmain.cpp, selftests_winmain.cpp, ws2_32.
      Left: the window procedure 0x004E16B0 (not a function in Ghidra, so find_gaps doesn't list it; unused with
      PartyMod's SDL window), 0x004E2C90 (91 bytes, no callers), the SingletonPtr methods 0x004E1970-0x004E1EE0.
- [x] Batch 69 MainLoop (NOT run in the game yet; pm/mainloop): `Mlp::Manager::MainLoop` 0x44EEF0 (src/game/sys/mlp.cpp,
      mlp.h), the game's main loop - the last but one of PartyMod round's functions (WinMain is in pm/winmain).
      Sys::Main now calls it directly (no longer through the game's address).
      Convention: a plain thiscall (the manager in ECX, no arguments, plain RET) - no naked stub needed. Its only
      caller is Sys::Main (0x4E248D, replaced). EAX at its RETs is the entry value of m_done on one path and the quit
      flag's byte on the other: nobody reads it (register scan: nothing reads EAX/AL after it), so `void`.
      What it does, per frame (one pass of a do-while(!m_done), m_done cleared first and put back as it was on entry):
      the screen saver's timeout set again every 201 frames (SystemParametersInfoA GET/SET, restarts its idle time);
      the skip-render (0x7D1EC0) and 0x6F1158 flags cleared; keyboard read, window events (PartyMod's handleEvents),
      network service 0x536640; inactive window (0x7CCBE4) without text entry (0x7CCDF8): Sleep(100) + window events
      until active, then straight to the loop test (no frame time used); inactive with text entry: render skipped;
      quit flag (0x7D6A2C): out at once; device lost (0x786AB4): unless TestCooperativeLevel says still lost,
      release the default-pool objects (0x4E0320/0x4D67F0/0x4D7AF0), Reset, and on success restore them
      (0x4E0440/0x4D68C0/0x4D8420) and clear the flag - render skipped either way. Then the 60 Hz frame timing on
      QueryPerformanceCounter: wait (Sleep(0)) until a frame after the frame's start; more than 20 frames late:
      start again one frame back; more than 1.5 frames late (or the window inactive): render skipped, counted
      (0x696D70 in all, 0x696D74 in a row) - unless four in a row were skipped already and the window is active.
      The frame's start moves on by one frame. Then Tmr::CountVblank / OncePerRender; profiling (m_profileFrames
      +0xC4: "\nProfiling.....Start of main loop\n\n", count down, m_profiling +0xC8 for the frame); the system
      task list (+0x7C) then the logic list (+0x04) run with the profiling flag (0x4070A0); BeginScene 0x48BF10 and
      EndScene/Present 0x48BF20 unless the render is skipped (re-read after the tasks), around the frame's render
      0x48C330 and 0x565DA0; Tmr::UpdateFrameLength. The +0x40 list isn't run by the loop.
      PartyMod: its only patch inside is the call at 0x44F1C0 -> renderWorldWrapper; built in as
      RenderFrameLetterboxed (render, then the letterbox on in the front end - level byte 0 of the career, or no skate
      module - and off in a level), the original's plain call in a comment. HANDLED entry added in
      the overlap check.
      Found by the test: the frame length is `frequency / 60` UNSIGNED (0x623D50 is _aulldiv; I first wrote a signed
      division - the original hung where ours didn't on a negative frequency).
      Test (src/hook/selftests_mainloop.cpp, TestMainLoop, Early): the original (PartyMod's renderWorldWrapper call
      written in at 0x44F1C0) and ours, with every callee stood in for by recording code - SystemParametersInfoA,
      QueryPerformanceFrequency/Counter and Sleep too (import slot and API), a stand-in D3D device (TestCooperativeLevel,
      Reset), PartyMod's setLetterbox - and a scripted clock; the loop ends the way the game ends it (m_done set by a
      task, the quit flag). Five cases: 450 jittered frames (waits, late, pauses, screen saver twice, profiling,
      tasks changing the skip flag, letterbox from 2 to 1); quit in frame 5 with a frequency over 2^32 in a level;
      device lost/reset failing/reset working plus the inactive window both ways; exact edges (1.5 frames, 20 frames);
      quit already set. Trace = every call with the frame number, the packed state and the profiling fields, the
      clock values read, the final state and a hash of the manager. Also checks ours calls RunStageOnce(EngineStarted)
      first (stood in, so the engine-started tests don't run offline).
      Mutations: 49 deliberate bugs, all caught (43 at first, the other 6 after adding the edges case, the
      inactive-with-six-in-a-row frame, an absolute clock in the device case and letterbox 2 in the front end).
      Not equivalent but not testable: none known. (A signed division would only differ for a negative frequency,
      which the edges/quit cases don't use - the hang showed it once; the code follows the original.)
      Shared harness: uses main's src/hook/standins.h (namespace TestStandIns, from pm/winmain; my own copy of the
      same extraction was dropped in the merge). TestMain also stands in for our MainLoop now (Sys::Main calls it
      directly).
      Test-runner mechanism: one flaw fixed in hook.cpp IsDisabled - disabling the game source in thug2-decomp.ini by
      address (0x0044EEF0) or by name ("MainLoop" is a substring of "MainLoopHook") also disabled the runner's
      fallback, so with MainLoop disabled the EngineStarted stage never ran. Fallbacks are now turned off only by
      `*`, their file (selftest_runner) or their exact name. (Offline this can't be exercised: thug2_check doesn't
      install hooks - check in the game: with `disable=0x0044EEF0` the log should still show the engine-started
      self-tests, run from MainLoopHook.) The mechanism otherwise looks right: a fallback is superseded only by an
      installable (not source-only, not disabled) game replacement; if that one then fails its argument-size check
      the game stops anyway (main's merge from pm/winmain now keeps the fallback in that case, so the stage and the
      stop message still come).
      Neighbours left (find_gaps; Mlp::Manager's other methods): 0x44EE90 (via the thunk 0x44F250), 0x44F220,
      0x44F240, and the singleton's create/release 0x44F260 / 0x44F2B0.
      Checks: builds without warnings; thug2_check 0 MISMATCHED, early 82/82; register scan only the known 0x4EFF92
      false alarm; call audit no MISMATCH (0x4D7AF0, 0x48BF10, 0x48BF20 "?" - they end in tail jumps to plain-RET
      functions, checked by hand); the overlap check "No overlaps". docs/PROGRESS.md not regenerated (merge).
- [x] Batches 68-69 tested in the game (2026-10-16, by the user): game runs; 673 of 673 replacements installed (32
      source only; the two runner fallbacks correctly not installed), early 84 / late 17 (from our ParseNetworkParams)
      / engine-started 2 (from our MainLoop) self-tests pass, shadow checks no mismatches. (The fallback path itself,
      disable=0x0044EEF0, not tried yet.)
- [x] PartyMod's runtime patches dropped (2026-10-16, at the user's request; NOT run in the game yet): no byte
      patches into rewritten game code any more - the patch functions and the patches inside createSDLWindow,
      patchButtonGlyphs, patchPs2Buttons and loadGfxSettings are in #ifndef THUG2_DECOMP blocks (PartyMod's UPSTREAM.md).
      Still applied: GameSpy's host names (12 code + 2 data addresses) and the IsPS2 CFunction wrap. The 8 functions
      PartyMod replaced whole (0x4A0780, 0x4A07C0, 0x4B2330, 0x4B2440, 0x48C170, 0x5F3A50, 0x5F56E0, 0x5BDCC0) are now
      REPLACE (were source only: PartyMod's jumps went over our hooks, so its C versions ran). The overlap check
      skips #ifndef THUG2_DECOMP blocks.
- [x] Batch 70 (NOT run in the game yet; early test passes offline): the script cache (src/game/script/cache.*), the
      whole class. Script::CScriptCache (0x24 bytes, an Mdl::Module: vtable 0x648468 {Destroy 0x4761E0, Start and
      Stop = the empty 0x5480A0}; +0x14 the entry table (Lst::HashTable, 8 bits, on the script heap), +0x18 / +0x1C
      the newest / oldest unused entry, +0x20 "free unused entries at once" - only ever cleared) and
      CScriptCacheEntry (0x14: name, code, usage, older, newer; pool 0x6F3560). Functions: CreateScriptCache 0x476040
      / ReleaseScriptCache 0x475A00 (the singleton 0x6F3558 with references 0x6F355C), Construct 0x475DD0,
      DestroyBody 0x476090, Destroy 0x4761E0, GetScript 0x475E80, ReleaseScript 0x475D60, Invalidate 0x475FD0,
      AddToUnused 0x475A40, RemoveFromUnused 0x475A70, DeleteEntry 0x475B60, MakeRoom 0x475BD0, the entry's
      operator new 0x475AC0 / delete 0x475B00 (the pool pair), and Lst::HashTable::GetItem(const key&, bool)
      0x462050 (the non-inlined GetItem; second argument always 1, unused). 15 functions.
      Callers switched to ours: RemoveFromScriptCache (qb.cpp) now calls CScriptCache::Invalidate (ScriptCache()
      returns CScriptCache*); Sys::Main creates/releases the cache with CreateScriptCache / ReleaseScriptCache (and
      selftests_misc.cpp's Main test maps 0x476040 / 0x475A00 to them).
      Conventions: everything on the cache is thiscall and callee-pops (GetScript, ReleaseScript, Invalidate,
      AddToUnused, RemoveFromUnused, DeleteEntry RET 4; MakeRoom RET 8, its second argument unused); Construct and
      DestroyBody take ECX only; new/delete and Create/Release are cdecl. Still the game's: Mdl::Module's
      constructor / destructor body 0x44F7E0 / 0x44F790 (also used by File::AsyncLoaderModule).
      Early test (src/hook/selftests_scache.cpp, TestScriptCache): a stand-in manager with a default heap and a
      192 KB script heap in a 256 KB arena, two entry pools (120 and 30), a stand-in symbol table (a C function -
      pre-processing rewrites calls to it -, compressed and stored scripts, a name naming a script, an integer) and
      LZSS data made by the test; original vs ours: references with and without a cache, uses/releases through
      the unused list (middle and both ends), invalidation of an unused and of a used script (new version), the
      table lookup, five ~40 KB scripts dropping the oldest unused entries for heap room, 110 small ones hitting
      the 100-entry limit, the other pool current, free-at-once, loose and pooled entry new/delete, destruction
      through the vtable (slot 0 jumps to ours for our run), and two scripts sized to the margin (the largest
      free block less 128 bytes, and one byte more). Results, the cache's lists, pool counts and the
      whole arena compared. 40 deliberate bugs caught, 1 after adding the margin-sized scripts (14 of them by
      crashing inside TestScriptCache, 1 by hanging).
      Checks: builds without warnings; thug2_check 0 MISMATCHED (722 replacements, 15 not checked - none new),
      early 85/85; register scan only the known 0x4EFF92 false alarm (0x475E80 and 0x462050 return values: yes);
      call audit no MISMATCH; the overlap check "No overlaps"; find_gaps: nothing left in 0x475A00-0x476210,
      but registering 0x462050 makes 0x4620A0-0x462710 (object/event CFunctions, not cache) show as a gap; the one
      thunk (0x48C1A0 -> SetLetterbox) was there before.
## What the cache does (script runtime semantics - for the QuickJS port)
- Scripts live in the symbol table compressed ({checksum, size, stored size, code}, LZSS when stored < size).
  A script only runs from the cache: GetScript decompresses it (into the script heap) and pre-processes it
  (PreProcessScript: statement-start names of C functions / member functions are turned into direct calls), once;
  later uses share that code.
- Use counting: every GetScript takes a use, ReleaseScript gives one back. CScript (0x473A80, its clear/reset)
  releases its current script (+0xC8) and every script on its call stack (frames +0x94, 0x30 bytes each, count
  +0x30) - so a script is "in use" while it runs or waits on a script it called. Other users: 0x4E4F50 (a recursive
  "does script X (or a script it calls) do Y" scan - gets then releases), 0x474540, 0x474B40, 0x475750,
  0x45C950 (not rewritten yet).
- A name naming another symbol is followed; the cache key is the script's own name (so an alias shares the
  entry). Names that aren't scripts give null (no entry). Release looks the name up directly (no alias step).
- Unused code stays cached (LRU list, newest first) and is only dropped when room is needed for another script:
  while the script heap's largest free block - 128 is smaller than the new script, and while 100+ entries of the
  current entry pool are in use. Nothing guards running out of unused entries while the heap is still too full:
  the original then dereferences null.
- Reloading a script (CreateScriptSymbol, the pending symbol changes in ProcessPendingSymbols) calls Invalidate:
  the cached copy is freed and the new version decompressed right away, with the old use count. The old code is
  gone even though scripts may still be running it; the loader restarts those (RestartChangedScripts). A script
  that was unused ends up with a count of 0 but not on the unused list, so it's never dropped again (quirk).
  Invalidating a name whose symbol is no longer a script would crash (GetItem null) - it's only called while the
  symbol is still a script.
- The cache is created at start-up (Sys::Main) as an engine module (registered, started - its Start does
  nothing) and released at shutdown; it isn't unregistered.
## Left in 0x475000-0x476FFF (all CScript / script manager, not the cache)
0x475050, 0x4750B0 (38 callers), 0x4752F0, 0x4753C0, 0x475420 (spawn script, 36 callers), 0x475670, 0x4756C0,
0x4756F0, 0x475700, 0x475750 (CScript: run a cached script - GetScript then 0x4748E0), 0x475790 (RunScript, 505
callers), 0x4758F0, 0x475920 (the script manager's update: runs every script, flags 0x6F3544 / 0x6F3545). These
belong with the CScript batch.
- [x] Batch 71 (qb agent; NOT run in the game yet; early tests pass offline): the pre-compiled .qb format and
      unloading a .qb (src/game/script/precompiled.*, load.cpp). 9 functions.
      - ParsePrecompiledQB 0x46FBA0 (filename, data, unused): the image relocated in place against its own start
        and its symbols defined, through RelocatePrecompiledSymbols 0x46CA50 (file, base, callback, argument,
        flag: symbols from +0x1C until exactly the file's size at +4 - no other end check; each relocated, then
        passed to the callback) with DefinePrecompiledSymbol 0x478DE0 (a function Ghidra never made - only its
        address is pushed: sets the symbol's file checksum +8, CleanUpAndRemoveSymbol on the symbol of the same
        name if any, links the file's own entry into the table; the flag - the third argument != 0, pushed as
        all of EAX with only AL set - isn't read).
      - Relocation, each returning the end of the item's data: RelocateSymbol 0x46C970 (entry +1 byte = type),
        RelocateStruct 0x46C7F0 (the last component's end, or the structure's own end when empty),
        RelocateComponent 0x46C6B0 (type = byte 0 >> 1; next relocated when set; a script component's size is
        SkipToEndOfScript's), RelocateArray 0x46C830 (more than one element: an element table follows the
        array, relocated only when non-null; strings padded once after the last), RelocateArrayElements 0x46C7B0
        (a thiscall CArray method, RET 4: written as a free function with an adapter, struct.h unchanged).
        Integers, floats, names, C functions, member functions, 0x0B and unknown types are left alone; arrays
        of scripts (7), C functions, member functions and 0x0B have only their table relocated.
        Quirk kept: a script symbol's stored size is read from right after its entry (+0x14+8), not through
        its value.
      - UnloadQB 0x46EEC0 (file checksum; 0 does nothing): lists the symbols whose +8 is that file (array on the
        top-down heap), CleanUpAndRemoveSymbol on each in table order, then every script for which CScript
        0x472C90 (all the scripts it's running / returning to still defined?) is false gets 0x473A80 (clear)
        and 0x473010 (tells its object it stopped) - those three still the game's, through GameFunction -
        and finally Pip::Unload(checksum) (pre-compiled files stay loaded until then). Callers: 0x5A59B0
        (UnloadQB-like C function, then 0x479070), 0x5A59D0 (reload: unload then LoadQB), 0x466C20.
      - LoadQB / LoadQBData now call our ParsePrecompiledQB directly.
      Early tests (src/hook/selftests_qb.cpp): TestPrecompiledQB - a generated image with every symbol type
      (strings of lengths 0/3/5, a script whose value is another block, unknown types 0/0x14), structures with
      every component kind (three script components, with and without a line number after '$', nested and
      empty structures, arrays as components), arrays of every element type with 0, 1 and more elements and a
      null table, names that replace an earlier record of the file and symbols already in a stand-in table (and
      share buckets): each relocation function on every item (fresh copy each time; return values and the whole
      image), the walk with a recording callback / none / an empty file, ParsePrecompiledQB (both flag values),
      the callback alone, and LoadQBData's pre-compiled branch - original vs ours. TestUnloadQB - symbols of
      several files (kept and not, five types, several buckets) in a stand-in heap, four fake scripts with the
      CScript methods and Pip::Unload stood in for (recording), unloading 0, a file, a file with no symbols,
      another file - original vs ours, calls and the whole arena. 41 of 45 deliberate bugs caught (1 after
      strengthening: a replaced table symbol that isn't kept, FreeSymbol stood in for); equivalent: `size == 1`
      -> `>= 1` in the else of `> 1`, the ignored flag (twice: ParsePrecompiledQB, LoadQBData's `unused`),
      linking only when no symbol of the name is left (none ever is after the removal).
      Findings (the script runtime's spec):
      - A pre-compiled image is symbols ready to use: the file's own CSymbolTableEntry records are linked into
        the table and point into the file, which stays loaded (on the script heap) until UnloadQB. Records must
        have bit 2 (4) of byte 0 set or CleanUpAndRemoveSymbol, when a later definition replaces them, frees
        them into the pools as if they were pool memory. Nothing marks the symbols new (byte 0 bit 0 comes from
        the file), so whether RestartChangedScripts restarts scripts that use them depends on the file.
      - Defining a symbol from a pre-compiled file never calls RemoveFromScriptCache (CreateScriptSymbol does):
        a cached decompressed copy of a replaced script would stay - worth checking against the script cache
        agent's findings.
      - The shipped data\**\*.qb.xbx files (95, magic 0xB075EF6F; "xbx" is the PC's pre-compiled extension from
        0x5B6040, platform 4) do NOT fit this exe's layout: each entry starts 00 04 <type> 00 (the type in byte 2,
        e.g. 0x0C for an array, 0x07 for scripts) and components 00 <t> 00 00 with values that don't match
        this build's enum. Read as this code reads them, every entry would be a local string. So either nothing
        loads them on PC (LoadQB's callers pass "....qb" names; the LoadQB C function takes the name from the
        script) or they're loaded differently - check in the game (e.g. log LoadQB's pre-compiled branch)
        before relying on the format. The +0x08..+0x1B header bytes (1C 08 02 04 10 04 08 0C ...) aren't read.
      - Batch 71's question, answered 2026-10-17: LoadQB's pre-compiled branch needs a name ending in the platform
        extension from 0x005B6040 (0x007CEFA8: 1-3 ps2, 4 xbx, 5 ngc, 6 xen, else ???). Every shipped .qb.xbx -
        pre-compiled images, magic 0xB075EF6F, laid out for the Xbox build - has a token-format .qb twin next to it, and
        the exe only builds .qb names ("%08x.qb", "runnow.qb", "scripts\animview\%s.qb"), so the PC game loads the .qb
        files; the branch is unused unless a script names a .xbx file itself.
      Next to it, not done: 0x46C470-0x46C6A0 (13 small functions of a class that wraps a CStruct at +4 -
      0x46C610 GetX-with-default, 0x46C650 add, 0x46C680 destructor... - not part of this format); CScript
      0x472C90 / 0x473A80 / 0x473010 (with CScript). find_gaps reports one thunk, 0x48C1A0 -> SetLetterbox
      0x4B2440 - not from this batch.
- [x] Batch 72 (gapsB; gaps next to Main / MainLoop / WinMain; NOT run in the game yet; branch wt/gapsB): 63 REPLACE lines
      (2 source only).
      - SingletonPtr (src/game/core/singleton.h / .cpp, namespace Spt): Neversoft's `Spt::SingletonPtr<T>` - a vtable and
        the instance; constructed with T's Create(create) (a reference, the instance made with `create`), destroyed
        with T's Release. The game's are locals (Sys::Main, the movie player, the network code) with inline
        constructors; out of line are, per instance, the 11-byte destructor (`MOV [ECX],vtable; JMP Release`, called
        from the exception-unwind funclets) and the 33-byte scalar deleting destructor (the vtable's only entry).
        All 24 instances in the exe (found by scanning for that destructor shape against the known Release functions -
        no others): 0x004E1980-0x004E1AF0 / 0x004E1B00-0x004E1EC0 (vtables 0x0064AFB0-0x0064B000, 21 instances: Pre,
        0x00463730, Mdl, Mc, 0x0044E7F0, the async loader, the script cache (Script::CreateScriptCache / ReleaseScriptCache from main's cache batch),
        and 14 more of Main's game singletons), 0x0044FD40/0x0044FD60
        (the sound manager 0x0047BA60, vtable 0x006472B4), 0x0044FD50/0x0044FD90 (Sio, 0x006472B8), and
        0x004B2230/0x004B2240 (Mlp, 0x0064A2EC). No identical-code folding among them (each has its own vtable);
        callers are only the unwind funclets and each scalar destructor. The one out-of-line constructor 0x004E1A90
        (not a function in Ghidra; 0x004E1A90-0x004E1AB4, called by 0x004F75F0) is SingletonPtr for 0x00590160's
        singleton; it builds the instance pointer through 0x004E1970 - a store-and-return-this constructor the linker
        folded with other one-pointer classes' (17 calls in the network code 0x00597740-0x0059D410): written as
        `Spt::PointerMember::Construct`. Each instance is a Singleton type (Type, kVtable, Create, Release); those
        without source are `GameSingleton<create, release, vtable>`, named by the Create address (Main's naming)
        except Mdl::Manager and the sound manager.
      - Mlp::Manager (src/game/sys/mlp.*): Construct 0x0044EDC0 (the three task lists 0x00406E80, +0xB8/+0xBC and m_done
        cleared; the EH frame only unwinds a half-built manager), DestroyBody 0x0044EE30 (task lists 0x004070E0, the
        last first), ResetDeviceIfLost 0x0044EE90 (static: uses no manager; the device-lost reset that MainLoop has
        inline - MainLoop now calls it; also source only at its jump 0x0044F250, which PlayMovieFile called - it now
        calls ours directly), ProfileFrames 0x0044F220 (CFunc ProfileTasks: +0xC4 frames, +0xC8 cleared; EAX left as
        the argument, nobody reads it), StopLoop 0x0044F240 (CFunc ResetEngine: m_done = true; 4 bytes, source only),
        Mlp::CreateManager 0x0044F260 / ReleaseManager 0x0044F2B0 (the singleton 0x00696D68, references 0x00696D6C,
        unsigned; Calloc(0xCC, current heap, 1)). Sys::Main calls CreateManager/ReleaseManager directly now.
      - Sys::WindowProc 0x004E16B0 (src/game/sys/winmain.*): bounds 0x004E16B0-0x004E180A (code to 0x004E17EB, then its
        jump table 0x004E17EC (4 targets) and index table 0x004E17FC (15 bytes, messages 2..0x10)). WM_DESTROY/WM_CLOSE:
        PostQuitMessage(0) + quit flag; WM_ACTIVATE (LOWORD 1/2): ShowWindow(game window 0x007D6A28, SW_MAXIMIZE),
        active; else SW_MINIMIZE, inactive, device lost, InvalidateRect(0, 0, TRUE); WM_PAINT: ValidateRect;
        WM_KEYDOWN -> Sio::SpecialKeyInput, WM_CHAR -> Sio::KeyInput; WM_SYSKEYDOWN F10 and WM_SYSCOMMAND SC_KEYMENU
        (whole wParam compared) and WM_POWERBROADCAST swallowed (0); the rest DefWindowProcA (a tail jump). Unused with
        PartyMod's SDL window; hooked anyway (harmless).
      - 0x004E2C90 turned out to be a component's InitFromStructure: Obj::CFlipRotateComponent (src/game/obj/flip_rotate.*),
        the factory 0x00481FB0's type 0xDF5C091A = "FlipRotate" (checksums matched by CRC: 0x0C7A712C "flipped",
        0x6EA3704A "rotated"). Written with its small neighbours: Construct 0x004E2C50, DestroyBody 0x004E2C80, Destroy
        0x004E2CF0, InitFromStructure 0x004E2C90 (base component 0x00481710/0x00481730/0x00481740 via GameFunction).
      Tests (src/hook/selftests_gapsB.cpp, Early): TestSingletonPtrs (all 24 destructors and scalar destructors with
      flags 0/1/2/3/0xFFFFFFFE, the constructor 0x004E1A90 with create 0/1/0x100/0xFF, 0x004E1970; Release, Create
      0x00590160 and Mem::Manager::Free stood in, ours found through the replacement table), TestMlpManager (Construct
      and DestroyBody on dirty memory, ProfileTasks, ResetEngine, the device reset in 6 cases through 0x0044EE90 and
      0x0044F250 with a stand-in device; Create/Release through 15 steps: none, out of memory, made, again, the last
      reference, no instance, a count over 2^31, the count wrapping), TestWindowProc (27 messages x 11 wParams, every
      Win32 call stood in through the import slots and the API), TestFlipRotateComponent (constructor, 75 parameter
      combinations, destructors).
      Mutations: 70 deliberate bugs, all caught at first (each by the test for its function; the device-reset ones also by
      TestMainLoop, which calls ResetDeviceIfLost now; movie_reset by TestPlayMovieFile hanging). None equivalent.
      Checks: thug2_check 0 MISMATCHED, early 91/91 (after merging main); register scan only the known 0x004EFF92 alarm (FlipRotate's
      Construct returns this); call audit no MISMATCH (singleton.cpp's GameFunction calls take template addresses the
      audit can't read: all 38 Create/Release functions checked by hand - plain RET, cdecl); the overlap check: no
      overlaps, nothing to handle.
      Shared files changed: src/game/sys/main.cpp (Mlp::CreateManager/ReleaseManager called directly; merged with main's script-cache calls),
      src/game/gfx/movie.cpp (calls Mlp::Manager::ResetDeviceIfLost in place of GameFunction 0x0044F250),
      src/hook/selftests_misc.cpp (TestMain stands in for our Mlp create/release too), src/hook/selftests_pc.cpp
      (TestPlayMovieFile stands in for our ResetDeviceIfLost too), src/game/sys/mlp.cpp (MainLoop calls
      ResetDeviceIfLost), CMakeLists.txt (singleton.cpp, flip_rotate.cpp, selftests_gapsB.cpp).
      Left (find_gaps, new neighbours): Mdl::Manager 0x0044F300-0x0044F7E0 (17 functions, the module manager, next to
      0x0044F2B0); 0x004B2270-0x004B2320 (next to the Mlp SingletonPtr); FlipRotate's Update 0x004E2D20 (1343 bytes).
- [x] Batch 73 (gapsA; gaps next to batches 55/63-66; NOT run in the game yet; branch wt/gapsA): 34 addresses filled in -
      28 REPLACE, 6 REPLACE_SOURCE_ONLY (jumps). Tests in new src/hook/selftests_gapsA.cpp (Early, StandIns harness).
      - Text (src/game/gfx/text.*): `SFont` layout finished - specialMap is 0x20 bytes ([0x1F] doubles as the default
        glyph), +0x12C `format`, +0x130 `next` (font list 0x780FD8), +0x178 `texture` (IDirect3DTexture9), +0x17C
        (passed with it to 0x4DA730), size 0x180. SText: 0x4CEED0 `Construct(float priority)` (base drawable 0x4DFA70
        with "not listed" = 1, vtable 0x64A89C, returns this), 0x4CEEF0 `DestroyBody` (vtable, then the base
        destructor 0x4DFAA0), 0x4CFAF0 `Destroy(flags)` (deleting destructor), 0x4CEF00 `ResetVertices` (BeginDraw:
        buffer pointer 0x780FE8 = 0x779FD8), 0x4CEF10 `DrawVertices` (EndDraw: makes the 256-quad index buffer
        0x780FF0 once (flag 0x781BF0), then if count > 0 (signed): the texture font's (0x780FE0) or own font's
        texture, 7 texture stage states, FVF 0x144, cull none, unless 0x7D1EC0: counters 0x786EBC / 0x786EC0 and
        DrawIndexedPrimitiveUP; stage states back; count and texture font cleared - count <= 0 leaves both).
        0x4CF0F0 `SFont::Load(name, fromMemory)` (cdecl; "fonts/<name>.fnt.xbx" or the bytes at name): header, the
        character map (format 1: one character or special -1-index per glyph, specials past the map written past it;
        format 2: up to four characters, 0xFF ends), the default glyph given to unmapped characters (not 'A'/'a' -
        glyph 0 counts as unmapped) and specials, CreateTexture A8R8G8B8 managed (failure: return null, file left
        open), palette RGBA -> ARGB with PS2 alpha doubled (>= 0x80 opaque), 0x5F45E0 expands the indices, LockRect
        (failure: null), pixels copied packed (pitch ignored), glyph UVs (right/bottom from the unrounded left/top),
        listed, closed, spaceSpacing = width of 'I'. 0x4CFA80 `SFont::Unload` (off the list, frees glyphs and font -
        not the texture). selftests_text.cpp fills 0x20 special-map entries now (was 0x28: past the array).
      - Device (src/game/gfx/device.*): 0x44F870 `Nx::SetRenderState(state, value)`, 0x44F890 `Nx::ClearDevice(...)`
        - cdecl wrappers returning the HRESULT (32 and 4 callers; movie.cpp keeps its own inline calls).
      - Music (src/game/pcm/music.*): `Stream` (0x74 bytes, three at 0x69B938: +0 name, +4 id, +0x10 channel, +0x30
        listener). 0x450220 StreamIdFinished, 0x450260 FindPlayingStream (by id, then by name, -1), 0x4502D0
        FreeStreamId, 0x450310 ShuffleUserSoundtrack (game's Rnd; at most 600; no PartyMod change here), 0x4503D0
        UseUserSoundtrack, 0x450410 UseStandardSoundtrack, 0x450420 StopUserSoundtrack (source only: jump to
        0x4531A0), 0x450430 MusicIsPaused (also registered source only at 0x44D680, the CFunction MusicIsPaused - a jump to it), 0x450440 /
        0x450450 music / sfx volume (FLD), 0x450460 RandomTrackOrder (int, MOVZX), 0x450470 SetMusicLooping (so
        0x69BAA0 is the loop flag, renamed from "special track set"), 0x450480 / 0x4504B0 Get/SetTracksOff
        (playlist 0's off masks), 0x4504E0 SetUserSoundtrackVolume (float; 0x453400 only for playlist 1), 0x452680
        PlayNextTrackInOrder (first track on and not played; all played: masks cleared, nothing plays), 0x452910
        Update (stream system 0x453940, 0x453BD0, pause on failure, owners/listeners of ended streams, every 64
        frames unless volume 0 (playlist != 1) or paused: in order or PlayNextTrack), 0x4B2350 GetNumSoundtracks
        (source only, jump to 0x4542D0), 0x4B2360 GetSoundtrackName (wide name through user32 wsprintfA "%ls" per
        character, 99 max, unconvertible / 0xA1-0xAC bytes -> '~', "~" if none), and the thunks 0x48C150 / 0x48C160
        to them (source only).
      - 0x48C1A0 -> SetLetterbox: REPLACE_SOURCE_ONLY in src/game/gfx/screen.cpp.
      - Tests (Early): TestSTextDrawing, TestLoadFont (7 font files: both formats, defaults, past-the-map special,
        another format, texture create / lock failures, memory and file; Unload at head / middle / end / missing),
        TestDeviceWrappers, TestMusicStreams (lookups, getters/setters, shuffles incl. the last-first swap, names),
        TestPlayNextTrackInOrder, TestMusicUpdate (20 cases). File, memory manager, device / texture vtables,
        platform music functions stood in for.
      - 81 deliberate bugs: 76 caught, 3 more after strengthening (format-1 code 0, the in-order round restart with
        tracks over 63 played, frame 32), 2 equivalent: the default fill's `c < 0x1F` vs `<= 0x1F` (entry 0x1F is the
        default itself, never 0 there) and '~' for an empty conversion (wsprintf "%ls" never gives one).
      - Register scan (nothing new beyond the known 0x4EFF92; EAX readers of these all return values), call audit (no
        MISMATCH), overlap check (no overlaps): clean.
      - Not done: 0x4B2270-0x4B2320 (9 small Nx platform glue functions - a flag toggle, an EndScene-style device
        call, a release, four jumps into 0x4D3CA0 / 0x4D8C90 / 0x4D8CC0 / 0x4DB570, a matrix call, a string getter -
        left by gapsB next to 0x4B2350; they need their callees looked at, for a later batch). find_gaps after the
        merge: none of this batch's neighbours left, 0 thunks.
- [x] PartyMod no longer a separate thing (2026-10-17, at the user's request; **NOT run in the game yet**; builds
      without warnings, thug2_check 0 MISMATCHED - 826 replacements, 15 not checked, none new -, early 98/98).
      Everything of PartyMod (v1.0.4) the game still used is C++ in the game's source, by subsystem; the MIT
      licence notice for the moved code (from PartyMod's folder/LICENSE) is in THIRD_PARTY_NOTICES.md (README
      updated). The settings are in thug2-decomp.ini next to the exe ([Graphics], [Miscellaneous], [Keybinds],
      [Gamepad], [Logging] alongside [replacements] / [selftests] / [features]), with the same keys and defaults;
      same SDL2 2.30.9 (now linked to thug2_code; thug2_check still delay-loads it).
      Where it went:
      - src/game/sys/config.* (Config::): the settings file - Init, ExecutableDirectory, GetBool/GetInt/GetString.
      - src/game/sys/debug_log.* (DebugLog::): PartyMod's [Logging] debug log (Level, LogToFile, LogFile;
        console with [Miscellaneous] Debug=1).
      - src/game/sys/sdl_events.* (Platform::InitEvents / RegisterEventHandler / PumpSdlEvents): the event pump
        (handlers in the same order: quit, window focus, input); std::vector instead of the stretchy buffer.
      - src/game/sys/sdl_window.* (Sys::CreateSdlWindow, Windowed(), WindowWidth(), WindowHeight()): the window
        (ResolutionX/Y, Windowed, Borderless; the max-resolution check; focus events -> 0x007CCBE4 / 0x00786AB4).
      - src/game/sys/pc_setup.* (Sys::InitPcSetup = initPatch, Sys::FastExit = fastExit): the start-up in the
        same order - config, log/console, script patches, events (+quit), srand(time) on our C runtime for the
        music shuffle, the online hosts, the glyph settings, the graphics settings. WinMain calls InitPcSetup and
        CreateSdlWindow as before; Platform::InitInput calls Nx::WriteGraphicsSettings and Sio::InitSdlInput.
      - src/game/sio/sdl_input.* (Sio::InitSdlInput = initManager, ProcessSdlDevice = processController,
        SetSdlActuators = set_actuators, UsingKeyboard(), TheInputSettings()): SDL controllers, players, the
        keyboard and its menu keys, typed text (Sio::KeyInput called directly), the DirectInput key table
        0x007D6770, the bindings. Sio::Device fields instead of PartyMod's struct; its four 32-bit stores
        (+0x14, +0xE0, +0xEC and +0xF0 - the last one 4 bytes past the end of the 0xF0-byte device) kept as
        32-bit stores (Store32), so the bytes written are the same.
      - src/game/sys/platform_cfuncs.* (Sys::CFunc_IsPS2, Sys::InstallIsPS2): see IsPS2 below.
      - src/game/gfx/settings.* (Nx::LoadGraphicsSettings, WriteGraphicsSettings, BlurDisabled()).
      - src/game/gfx/screen.cpp: Nx::CurrentLevel, DesiredAspectRatio; SetScreenAspectRatio /
        GetScreenAngleFactor / SetScreenBlur / SetLetterbox now contain PartyMod's code (they called it).
        Mlp's own CurrentLevel copy dropped (uses Nx::CurrentLevel); Mlp::RenderFrameLetterboxed is public (the
        main loop test writes a call to it into the original, as PartyMod's renderWorldWrapper).
      - src/game/gfx/render.cpp: Nx::SetDisplayRegion, Nx::Draw2DWithBlackBars (draw2DWrapper).
      - src/game/gfx/movie.cpp: Nx::SetMovieTexture, Nx::DrawMovieQuad (movie_setTexture / movie_drawPrim).
      - src/game/gfx/glyphs.* (Nx::LoadGlyphSettings, PreferGlyphs(), GlyphStyle(), ButtonForTag =
        dehexifyDigitWrapper, ShouldUseGlyph); font.cpp returns the style's names itself.
      - src/game/gfx/loading_bar.cpp: the 100000-byte gradient buffer (resbuffer), LoadingBarGradientBuffer().
      - src/game/mc/memcard_cfuncs.cpp: Mc::FreeDiskBlocks (calcAvailableSpaceFudged, fastcall as before).
      - src/game/net/online_hosts.* (Net::SetUpOnlineHosts, GamestatsHost()): see GameSpy below.
      - src/game/script/patched.* (Script::InitScriptPatches, LoadPatchedScript = pipLoadWrapper) and
        src/game/util/bps.* (Bps::Apply, PatchChecksum): the keyboard.qb BPS patch (its 380 bytes now an array
        in patched.cpp), with PartyMod's reuse of the patched copy on later loads.
      Deleted: PartyMod's whole folder - patch.c/h (patch primitives), patches.c (incbin output, now the array),
      incbin, util/hash (map, only for the script patch list - now a vector), util/sb (event list - now a
      vector), cfunc.c (wrap_cfunc), networkParamsWrapper, the attach function (DllMain no longer calls anything),
      getModuleInfo / base_addr / mod_size (only for findPattern), every #ifndef THUG2_DECOMP block, UPSTREAM.md.
      Removed too: src/proxy/dinput8.cpp's redirect of THUG2PM.exe's DLL name (0x0064B198) to dinput8.dll (the
      exe's own patch makes the *original* WinMain load PartyMod's DLL by that name, which only runs when
      WinMain's replacement is disabled in the ini). Comments reworded. thug2_decomp_before_exit stays (Sys::FastExit calls it).
      IsPS2: the CFunction table's IsPS2 entry (0x00681468: name 0x006524FC, function 0x0068146C) points at the
      shared 3-byte "return false" stub 0x005B9E50, which 12 other CFunctions share (IsNGC too) - it can't be
      REPLACEd for IsPS2 alone (and is already Stubs::ReturnFalse, source only). So the game's own
      Sys::CFunc_IsPS2 (false; with PS2 controls true for the vehicle grind script 0x47F15883 - CScript +0xC8 -,
      as PartyMod's isPs2Wrapper_DD) is put into the table entry by Sys::InstallIsPS2, from Sio::InitSdlInput
      under PS2 controls only (where wrap_cfunc did it; it checks the entry's name is "IsPS2" first). No REPLACE
      line (nothing to count). Early test TestIsPS2 (selftests_platform.cpp): through the table entry, original
      vs ours with PS2 controls off; on, against the wrapper's answers; the entry's neighbours unchanged. 12 of 12
      deliberate bugs caught (setting ignored/inverted, wrong checksum/field/null handling, not installed, wrong
      entry...). When the CFunction table becomes source, the entry simply names CFunc_IsPS2.
      GameSpy host names: src/game/net/online_hosts.cpp writes the same 14 addresses (12 code operands, the 2
      natneg data words) with hook's new WriteGameCode (hook.h) - commented "until the GameSpy SDK is linked,
      then this becomes its configuration". tools/check_code_writes.py (replaces the old overlap check)
      checks those 14 writes and the IsPS2 entry stay outside every REPLACE'd function's body and first 16
      bytes: "No overlaps". The Ghidra script that listed the PartyMod folder's patches retired.
      Tests: pointed at the moved functions (the original runs still get PartyMod's jumps/calls written in, now
      to our functions - so for those patched parts the comparison is our code against itself; the unpatched
      parts are still original vs ours). Two reference copies live in the tests instead, since the game has no
      separate function for them: PartyMod's reorder_flash_vertices (selftests_pc.cpp, the flash test's
      original run) and networkParamsWrapper (selftests_winmain.cpp: Sys::InitPcSetup then 0x004E1220).
      PartyMod round's test file renamed selftests_pc.cpp.
      Behaviour changes (all meant to be none in practice):
      - The "Input text '%s' > 1 byte!!" debug log line now gets its argument (PartyMod passed none: it printed
        stack garbage, at Level 3 only).
      - The window's `resolution_setting = 0` (PartyMod set its own pointer variable, not the setting) dropped.
      - Two calls in the park editor check are typed thiscall instead of PartyMod's fastcall with a
        null EDX (0x004AAE20 ignores ECX/EDX; 0x004637E0 doesn't read EDX) - same code.
      - Calls PartyMod made through game addresses that are now our own functions call ours directly: Pip::Load,
        Pip::GetFileSize, Crc::GenerateCRCFromString, Str::DehexifyDigit (REPLACE_SHADOW: its shadow check no
        longer sees these calls), Sio::KeyInput, Mth::Rnd (already so in ShuffleRandom). Same unless one of them
        is disabled in the ini.
      - The CRC-32 table for the BPS check is computed instead of listed (the standard one).
      - The window title is "THUG2" and the settings are in thug2-decomp.ini (no separate settings file).
      - Kept as it was, deliberately: the out-of-range read in RemoveController.
      Checks: register scan - only the known 0x4EFF92 false alarm; call audit no MISMATCH; check_code_writes "No
      overlaps"; dinput8.dll imports SDL2.dll as before.
      **Check in the game** (the user):
      - [ ] Starts; thug2-decomp.log has no failures; with [Logging] LogToFile=1 the debug log is written
            (with [Miscellaneous] Debug=1 a console opens).
      - [ ] Window: ResolutionX/Y honoured (and 0 = desktop size), Windowed=1, Borderless=1, full screen; alt-tab
            away and back (focus: the device comes back).
      - [ ] Keyboard: default binds and a custom [Keybinds] one; menus with Enter/Escape/arrows/E/R/1/2/-; typing a
            name on the on-screen keyboard (Enter, Backspace, Escape); chat F1.
      - [ ] Controller: joins as player 1 on a button press; a second controller rumbles and becomes player 2
            (split screen); unplug/replug; [Gamepad] binds and MovementStick/CameraStick.
      - [ ] PS2 controls (UsePS2Controls=1, the default): L1/R1 spin without delay, L2/R2 nollie/switch, acid
            drops, break vert, a vehicle grinds (IsPS2); and with UsePS2Controls=0 the Xbox layout.
      - [ ] Rumble (bails, the menu option).
      - [ ] Glyphs: keyboard -> key names; controller -> button glyphs; PreferGlyphs=1; GlyphStyle 0/1/2.
      - [ ] Letterbox/widescreen: front end 4:3 with black bars, levels full width; split screen not broken; the
            loading bar at high resolution; screen flashes; DisableBlur=0 blur.
      - [ ] Movies: intro/credits fitted 4:3 with bars; skipping with start/X.
      - [ ] Music: shuffle order differs between launches.
      - [ ] Online: the menu, the MOTD/news download, top stats (OpenSpy / OnlineDomain).
      - [ ] Memory card: free space shown, saving/loading.
      - [ ] Quitting: the process ends at once; thug2-decomp.log ends with the shadow summary and "quitting".
- [x] Batches 70-73, the patch removal and PartyMod move tested in the game (2026-10-17, by the user): game runs;
      792 of 792 replacements installed (32 source only), early 98 / late 17 / engine-started 2 self-tests pass, shadow
      checks no mismatches.
- [x] PartyMod references renamed to a generic name (2026-10-17, at the user's request; NOT run in the game yet;
      the name was put back later, see "PartyMod credited again" below): settings read from
      thug2-decomp.ini ([Graphics], [Miscellaneous], [Keybinds], [Gamepad], [Logging]); PartyMod's debug log goes into
      thug2-decomp.log ("pc: ...", [Logging] Level filter; LogToFile/LogFile no longer read); window title "THUG2"
      (WinMain's already-running check now finds a first copy, as the original intended); the exe's DLL-name redirect
      in DllMain removed; the MIT notice only in THIRD_PARTY_NOTICES.md, per-file headers point there.
- [x] The rename build tested in the game (2026-10-17, by the user): game runs, settings read from thug2-decomp.ini, "pc:" lines in the log.
- [x] Batch 74 (CScript, the script interpreter; NOT run in the game yet): 81 functions in 0x4728F0-0x475920 plus
      0x449970 / 0x4499B0. Done as a phase-1 class map (src/game/script/cscript.h: layout static_asserted, 0xF4 bytes;
      every method thiscall callee-pops, the free functions cdecl, no register arguments), then four parallel groups
      (worktrees wt/csA-wt/csD), merged in 285f07e: the groups call each other directly; CSmtPtr::Set replaces the two
      local SetObject helpers; the tests stand in for our methods as well as the game addresses. **What the
      runtime does (the QuickJS port's spec) is in docs/script-runtime.md** - not repeated here.
      Checks for every group: thug2_check 0 MISMATCHED, all early tests pass; register scan only the known
      0x4EFF92 RISK; call audit no MISMATCH; check_code_writes no overlaps; find_gaps nothing left in range (the 5
      sound functions aside). tools/check_partymod_overlap.py is no longer in the repo; PartyMod's only code patch
      (0x4E26A0) is far from this range.
      - 74A, life cycle (cscript_core.cpp, 22): Construct 0x4738E0, Destroy 0x474420, pooled new/delete 0x449970 /
        0x4499B0, ClearScript 0x473A80, RunOnExitScripts 0x473880, ReportStopped 0x473010, CopyParamsIfOwned 0x4739D0,
        SetScript 0x474540 (+ 0x4747A0 / 0x4747C0), SetScriptCopy 0x474720, Restart 0x4747F0, AllScriptsStillDefined
        0x472C90, GotScript 0x472940, SetSpawnerFile 0x472F90, ObjectDestroyed 0x473BE0, the OnException/OnExit
        accessors 0x472ED0/EE0/EF0, ClearOnExitScripts 0x472F00, FindScriptWithUniqueId 0x4734B0. load.cpp's four
        GameFunction wrappers (0x4747F0, 0x472C90, 0x473A80, 0x473010) replaced by the methods. Header fix:
        SetScriptCopy takes an `SStructScript*` {name, code} (from CStruct 0x476900), not a symbol entry.
        Test TestCScriptCore (selftests_csA.cpp): stand-in heaps/pools/symbols/cache/table/tracker, every function's
        branches, the whole state of every live script after each call. 63 mutations: 55 caught at once, 7 after
        strengthening, 1 equivalent (Destroy's final m_object.Release - ClearScript already released it).
        Findings: m_refCounted's vtable null for life (memset after its constructor); ClearScript releases the cache
        use even for code it never took from the cache; Destroy leaves a busy handler table to its runner.
      - 74B, the interpreter (cscript_exec.cpp, 13): Update 0x4750B0, ExecuteCommand 0x474B40, ExecuteIf 0x475050,
        ExecuteSwitch 0x473C60 ("not checked" by thug2_check: its RET is behind the jump table), SkipToEndOfSwitch
        0x472A70, ExecuteBegin/Repeat/Break 0x473220 / 0x472AB0 / 0x472B50, ParseFunctionParams 0x472950, ReadName
        0x4729E0, CallMemberFunction 0x472980 (free) / 0x4731A0 / 0x4731E0. Test TestScriptInterpreter
        (selftests_csB.cpp): 16 scripts (+8 called / interrupt scripts) for up to 24 Updates each, every token except
        the endless ones; per-Update result and full state traced. 60 mutations: 49 caught at once, 10 after
        strengthening, 1 equivalent (ExecuteRepeat storing mpEnd into m_pc). Findings: `obj:` with no such object
        retries on object 0 (0x462120) or hangs; a missing name after `obj:` crashes outside 0xFB11E6CD; `return`
        after an on-exit script that waited parses its arguments from the on-exit script's code.
      - 74C, waits / call stack / interrupts / events (cscript_wait.cpp, 22): UpdateWait 0x473050, StopWaiting
        0x4728F0, IsBlocked 0x472900, Wait / WaitOnePerFrame / WaitTime / WaitRealTime 0x472BD0 / 0x472BF0 / 0x472C10
        / 0x473310, Block 0x472FD0, SetWaitOnComponent 0x4739A0, ComponentDestroyed 0x472D20 (a naked stub keeping ECX
        for 0x472E70, REPLACE_UNCHECKED) and 0x472E70, ReceiveEvent 0x473800, UnblockInterruptedScript 0x472F40,
        CallScript 0x4748E0, ReturnFromScript 0x473E70, Interrupt 0x475750, the six event handler methods 0x472EA0,
        0x472EC0, 0x4734E0, 0x4735D0, 0x473690, 0x4736C0 (the table's own methods 0x45F820-0x4601A0 / 0x460470 stay
        the game's - their meanings are in cscript_wait.cpp). Test TestCScriptWait (selftests_csC.cpp): every wait
        type's edge cases (exact frame, NaN, huge/unsigned ms), call stack to depth 5 with the heap move, every
        handler method with and without a table. 70 mutations: 65 caught at once, 4 after strengthening, 1
        equivalent (growing the stack at depth >= 2 instead of == 2).
      - 74D, the script manager (cscript_manager.cpp, 24): UpdateSpawnedScripts 0x475920, DeleteSpawnedScripts
        0x4753C0, SpawnScript 0x475420 / 0x475670, KillSpawnedScript 0x4756C0, StopAllScriptsUsingThisObject
        0x4756F0, DeleteFinishedScripts 0x475700, RunScript 0x475790 / 0x4758F0, RunScriptOnce 0x4752F0, the net
        messages 0x473350 / 0x473420, Pause/Unpause 0x472D80 / 0x472DC0, ScriptExists / GetSpawnId /
        FindSpawnedScriptWithId 0x472DE0 / 0x472E00 / 0x472E20, the kill/stop walks 0x473FA0, 0x474040, 0x4740D0,
        0x474190, 0x474250, 0x4742B0, 0x474350. Callers switched to ours: chat.cpp (x2), service.cpp, music.cpp,
        memcard_cfuncs.cpp, sio/device.cpp; their tests stand in for ours too. Tests TestScriptManagerWalks,
        TestUpdateSpawnedScripts, TestSpawnAndRunScript, TestScriptNetMessages (selftests_csD.cpp). 56 mutations:
        55 caught at once, 1 after strengthening. Findings: the Kill* walks read `next` before stopping, the Stop*
        walks after (the plan said before for all); m_spawnerFile is the spawner's .qb file checksum, so
        KillSpawnedScript id=<file> kills what a file's scripts spawned; spawning a C function from script without
        flag 0x01917DF2 crashes in SetSpawnerFile (CFunc_SpawnScript 0x59F330 uses the null result).
      Not done: the 5 sound functions 0x47AC10-0x47AD80 (not script code; for src/game/sfx/volume.*): 0x47AC10 volume
      type from a checksum (0xF221474C -> 1, 0x9706104B -> 2, 0x0CA64C41 -> 3, else 0); 0x47AC50 sVolume::
      PercentageAdjustment(float) (thiscall RET 4; 5 channels if type 2 else 2, each `pct * ch * 0.01f`); 0x47ACF0
      IsSilent (channels == 0.0f, then `type != 3`; NaN -> false); 0x47AD60 the constructor (5 floats zeroed, type
      untouched, EAX = 0); 0x47AD80 find a stream by checksum (stdcall RET 4; table 0x6F7A68 of 0x3C-byte entries,
      two passes on +0 then +4, a hit only if 0x47C7E0(index); a second-pass hit also calls 0x478F60(0x6D2E270E, 0)
      and, if 0, 0x478F60(0xD7BB618D, 0); returns the index or -1). Full notes: docs/pending/cscript-plan.md in git
      history.
      Test-template todo: the arena-image half-pointer normalisation (.claude/skills/decomp-batch/test-template.cpp
      ~line 93, `high <= (base + size) >> 16`) works in whole 64 KB segments, so the segment holding the arena's end
      can also normalise statics placed right after the arena (csB hit this with its fake objects and used `<`,
      which instead skips the arena's last partial segment). Full addresses can't be compared (the pass exists for
      stray 16-bit halves); the template now says to allocate arenas with VirtualAlloc (64 KB aligned, alone in
      their segments) when it bites. Changing the existing static arenas was tried and broke csB - left as is.
- [x] Batch 74 (CScript) tested in the game (2026-10-17, by the user): game runs; 873 of 873 replacements installed
      (32 source only), early 105 / late 17 / engine-started 2 self-tests pass, shadow checks no mismatches.
- [x] Batches 75-78 (events, the tracker, objects, the menu front end; NOT run in the game yet): four parallel
      worktrees (wt/evt, wt/trk, wt/obj, wt/menu), merged in ca73875, 532db1d, 6d271c4 / 0bfdb41 and d5ac8c3; each
      calls the others directly after the merges (one CEvent / CEventListener in event.h, CTracker in tracker.h,
      CObject in object.h), and each test stands in for the others' code next to the game addresses. **The event
      model, objects' scripts and the front end's place in the frame are in docs/script-runtime.md (sections 2, 11,
      12; open questions 13)** - not repeated here. Checks for every batch: thug2_check 0 MISMATCHED, all early
      tests pass; register scan only the known 0x4EFF92 risk; call audit clean (the "?" tail-jumping callees checked
      by hand); check_code_writes no overlaps.
- [x] Batch 75, events and the handler table (src/game/obj/event.*, 21 functions, 0x45F7F0-0x460792): CEvent
      Construct 0x45F7F0 / MarkHandled 0x45F9B0 / Destroy 0x460150; CEventListener Construct 0x45FAA0, Destroy
      0x45FAC0, DeletingDestructor 0x460170, RegisterWithTracker 0x45FB30; CEventHandlerTable Construct 0x45F820 /
      Destroy 0x45FB70, AddHandler 0x45FBC0, AddHandlers 0x4601A0, RemoveType 0x45FDF0, RemoveGroup 0x45FE60 /
      0x4600E0, MarkGroup 0x45F840, RemoveMarked 0x45FED0, Compact 0x45FF50, Register / UnregisterWithTracker
      0x45FA10 / 0x45FA60, **PassEvent 0x460470**, PrintTable 0x45F880. CScript's GameFunction wrappers for them are
      gone. Test TestEvents (selftests_evt.cpp, Early): every function on stand-in tables / scripts / objects in an
      arena, all outside calls recorded. 48 mutations caught (7 after strengthening: removed entries in MarkGroup /
      Register / Unregister, RemoveGroup(all_groups) on a filled table, Compact's `changed`, the spawn path).
      Checksums: 0xC377C572 null_script, 0x8B713E0E all_groups, 0x1CA1FF20 default, 0x7031F10C params, 0x923FBB3A
      group, 0x80367192 exception. Findings: ReportStopped removes **every** handler (the spec said it sent an event
      0x8B713E0E - corrected); exception handlers run OnExceptionRun then replace the script (SetScript + Update);
      object flag 0x10000 makes normal handlers spawn; entries' flag bits 2-7 uninitialised [quirk]. Left: the
      composite object instance manager 0x45EFD0-0x45F7A0 (18 functions; a different subsystem); 0x4607A0-0x460840
      went to wt/obj.
- [x] Batch 76, the object tracker (src/game/obj/tracker.*, smtptr.*, 41 functions, 0x462000-0x4639E0): CTracker
      (0xB21C bytes, singleton 0x6F0FC0) Construct 0x462CF0, Destruct 0x462E40 (the alias and listener tables leak,
      original), ScalarDelete 0x463780, CreateTracker 0x463730, LookUpObject 0x462120, GetObjectByAlias 0x462190,
      GetUniqueId 0x4621D0, Add/RemoveObject 0x462980 / 0x462A20, RemoveAliasesOf 0x462590, AddAlias 0x462A90,
      RemoveEventListener 0x462000, Add/RemoveListener 0x463370 / 0x463450, ForgetScript 0x462B90, WaitForEvent
      0x462260, **LaunchEvent 0x462F00**, LogHandled / LogRead / LogFrame 0x462B10 / 0x462B40 / 0x462B70; CEventLog
      AddEntry 0x4627C0, Print 0x4620A0, Destroy 0x462CB0; CScriptNode / CScriptList 0x462530, 0x462550, 0x462710,
      0x462620, 0x4626B0, 0x4624D0; CFunctions WaitForEvent 0x462310, ObjectExists 0x462340,
      TerminateObjectsScripts 0x462380, SetObjectProps 0x462440, PrintEventLog 0x462480, AssignAlias 0x462C40,
      LaunchEvent / FireEvent 0x4634D0; CRefCounted Construct 0x4637B0, ClearSmartPointers 0x463820, Destruct
      0x463870, ScalarDelete 0x463880. Callers switched: cscript_core / cscript_wait, net/chat.cpp. Test TestTracker
      (selftests_trk.cpp, Early): objects / aliases / ids, listeners, broadcasts (deleted receiver, radius edges),
      targeted events, WaitForEvent (all 40 slots, reuse), the log, all seven CFunctions, CRefCounted. 53 + 10
      mutations: 49 caught at once, the 4 misses and 6 more after strengthening, 1 equivalent (ClearSmartPointers'
      second `mp_object = nullptr`). Findings: 0x36B2EE74 is the "no object" id; m_refCounted is what LaunchEvent's
      smart pointers point at; one event releases at most one WaitForEvent waiter; Block(type) registrations outlive
      the block. Original crashes kept (a targeted event with no such target, unchecked objects in AddAlias /
      SetObjectProps / TerminateObjectsScripts, a radius broadcast's source, unchecked allocations). Skipped:
      0x4638B0, 0x463910, 0x4639A0, 0x4639C0 - methods of the class at 0x463A00 (+0x34xx fields), to go with it.
- [x] Batch 77, objects and their managers (src/game/obj/object.*, objman.*, 75 functions + 2 source-only thunks,
      0x4607A0-0x461FE0 and 0x45DB90): CObject (0x4C bytes, vtable 0x6477E8) construction / destruction, SetProperties
      0x461710, CallMemberFunction 0x4608F0, PassEventToScript 0x461140, MarkAsDead 0x460A50, the script functions
      (CreateScriptIfNeeded, SwitchScript, CallScript, SpawnScript, SpawnAndRunScript, ClearEventHandler), SelfEvent
      0x4609F0 / BroadcastEvent 0x460A20, ids / types / tags, the member functions of table 0x683550 (Obj_LeaveScript,
      Obj_KillSpawnedScript, Obj_Visible / Invisible, Obj_GetId, GetTags, GetSingleTag, 0xEEEB7220, RemoveTags, the
      four flag functions, SetTags, Obj_SpawnScript, Obj_SwitchScript); FindObject 0x460AD0, Unlock / DestroyIfUnlocked
      0x460AC0 / 0x460AA0, CCompositeObject::GetComponent 0x45DB90; CBaseManager (Construct, Register / Unregister /
      ReassignObjectId, SetObjectPriority, GetObjectByID 0x4619F0, ForEach 0x461CC0), CGeneralManager (KillObject,
      FlushDeadObjects and its task), CObjectSearch 0x461F30 / 0x461ED0, ReleaseTracker 0x461FA0,
      CTracker::RegisterEventListener 0x461FE0. Thunks 0x420060 and 0x4AAD30 REPLACE_SOURCE_ONLY. singleton.cpp's
      Singleton463730 became ObjectTracker (CreateTracker / ReleaseTracker); cscript_exec.cpp calls FindObject and
      GetComponent directly. Test TestObjects (selftests_obj.cpp, Early; three runs: tags / flags / member functions;
      scripts / events / death / destruction; managers, nested and restarted walks, searches, the dead list). 72
      mutations: 71 caught (66 at first), 1 equivalent (NextItemOfType's null return). Findings: an object's own
      script isn't spawned and runs inside CCompositeObject's update 0x45D920; the object spawn functions set a
      spawned script's m_object; MarkAsDead blocks the object's script for good and defers the delete to the
      manager's task; GetSingleTag / 0xEEEB7220 / RemoveTags edge cases; the flag word +0x24 is never initialised.
- [x] Batch 78, menu input mapping (src/game/front/front_end.*, src/game/skate/competition.*, 20 functions,
      0x54EF60-0x55002F): Mdl::FrontEnd (0x164 bytes, vtable 0x64D724, instance 0x7CE464) AddKeyboardHandler
      0x54EF60, UpdateButtonEventMappings 0x54EF80, KeyboardTask 0x54F200, **Update 0x54F380** (runs
      UpdateSpawnedScripts), GrabControlEvent 0x54F600, SetAnalogStickActiveForMenus 0x54F900, PauseGame 0x54F960,
      InputLogic 0x54FB40, LogicTask 0x54FCC0 (task code Ghidra never made a function), UpdateInputMappings 0x54FD00,
      constructor 0x54FE00, CreateFrontEnd 0x54FF80; the CFunctions SetMenuAutoRepeatTimes 0x54F0F0,
      SetMenuPadMappings 0x54F150, SetButtonEventMappings 0x54F1B0, SetAnalogStickActiveForMenus 0x54FDC0;
      CCompetition's Leader constructor 0x54FFD0, destructor body 0x54FFF0, EditParams 0x550010, StartRun 0x550020.
      Names found by CRC guessing: inactive / active / use_as_first, block_menu_input, pad_start / pad_back /
      pad_choose, the anchors, device_num, controller, handle_keyboard_input. Test (selftests_menu.cpp, 8 Early
      tests): 66 mutations, all caught (1 after adding a negative-index GrabControlEvent case). Findings: pad events
      are launched targeted with target "none", so only event listeners and WaitForEvent waiters get them; quirks
      kept in SetButtonEventMappings' `null` handling and the Space key's leaked params. PartyMod's UIControls
      setting isn't read by any of this. Left: the front end's destructor 0x54EE70, v_start_cb 0x54EEA0, v_stop_cb
      0x54EF30, the auto-repeat 0x54ECE0, Release 0x54EBE0.
- [x] Batches 75-78 tested in the game (2026-10-17, by the user): game runs; 1028 of 1028 replacements installed
      (35 source only), early 116 / late 17 / engine-started 2 self-tests pass, shadow checks no mismatches.
- [x] Batch 79, the composite object manager and the COIM heap (src/game/obj/compositemgr.*, 18 functions,
      0x45EFD0-0x45F7A0; NOT run in the game yet): Obj::CCompositeObjectManager (CGeneralManager + 0x408 bytes,
      vtable 0x6476D4, singleton 0x6F0FB8) Construct 0x45F6E0 (its logic task: priority -900, code 0x45F6D0, on
      Mlp::Manager's logic list; registers the member function tables 0x683550 and 0x683268), DestroyBody 0x45F200,
      scalar delete 0x45F300, RegisterComponent 0x45F270 (81 types, all from 0x4E4860), CreateComponent 0x45F2A0, the
      per-type component lists (static 0x6F0DB8[128]: GetFirstComponentByType 0x45F2D0, Add/RemoveComponentByType
      0x45F3E0 / 0x45F420), CreateCompositeObject 0x45F330, CreateCompositeObjectFromNode 0x45F3A0, **Update 0x45F4A0
      (the object walk)**, **Pause 0x45F5D0**, ReleaseCompositeObjectManager 0x45F1E0 (the last reference forgets the
      instance without deleting it [quirk]); the COIM heap (the remains of CCompositeObjectInstanceMgr): DestroyCoimHeap
      0x45EFD0, CreateCoimHeap 0x45F0B0, InitCoim 0x45F160, ResizeCoimHeap 0x45F1C0 (the values InitCOIM stores are
      read by nothing). PauseGame, Sys::Main and singleton.cpp (Spt::CompositeObjectManager) call ours. Test
      TestCompositeObjectManager (selftests_coim.cpp, Early; three runs on stand-in managers / objects / components:
      construction, component types and lists, making objects, the walks - restart, nested Update / Pause, an object
      unregistered and another registered mid-walk - the COIM heap). 47 mutations: 45 caught (6 after strengthening),
      2 equivalent (DestroyCoimHeap's loop re-check and erase range). Findings (docs/script-runtime.md section 2):
      the main loop runs the system task list, then the logic list, then renders; task lists are sorted highest
      priority first (equal: newest first) and a run restarts when a task is added; the object walk (-900) updates
      every composite object - its own script first - **before** the front end's -2000 task runs
      UpdateSpawnedScripts, so a script spawned during the walk gets its first Update the same frame; objects made
      mid-walk update the same frame; **the pause mask was documented backwards**: 0x406AC0 runs a task only when
      `(mask & kinds) == 0`, so PauseGame's 0xC skips kinds 4 / 8 (front_end.cpp's comment and the spec fixed);
      pausing works through each object's paused flag, and objects created while paused aren't paused [quirk].
- [x] Batch 80, CCompositeObject, CBaseComponent and the node manager (0x45D050-0x45EFD0, 70 functions; NOT run in
      the game yet). Semantics: docs/script-runtime.md sections 2 and 12.1.
  - [x] 80A (src/game/obj/composite.*, 44 functions; GetComponent 0x45DB90 moved there from object.cpp): CObject's
        vtable slot 2 0x45D850 (a member function table entry: its pointer on `this` + adjustment, else by name);
        CBaseComponent (vtable 0x647618) constructor / destructor / Suspend 0x45D760 / CallMemberFunction(entry)
        0x45D720; CCompositeObject (0x108 bytes, vtable 0x647650) constructor 0x45E510 (sets CObject flag 0x20),
        destructor, **Update 0x45D920**, Hide 0x45D990, Suspend 0x45D9E0, SetTeleported 0x45E4C0,
        ShouldUpdateScripts 0x45E500 (slot 7: PAUSE_WITH_OBJECT = while paused), MarkAsDead 0x45D7C0, AddComponent,
        CreateComponentsFromArray, CreateComponentFromStructure, InitFromStructure, Finalize, CallMemberFunction(name)
        0x45E450 (components first, then CObject's); 23 script member functions of table 0x683268 (Obj_GetPosition,
        Obj_SetPosition, Obj_ForceUpdate, Obj_GetVelocity, GetSpeed, Hide / Unhide / IsHidden, Suspend / Unsuspend,
        Obj_GetDistanceToObject, Obj_GetType, Obj_HasComponent, Pause / Unpause, Obj_PrintDetails and 8 unnamed - names
        by checksumming candidates). Test TestCompositeObjects (selftests_cobj.cpp): 72 mutations, 70 caught (62 at
        first), 2 equivalent in practice (the order of the sums of squares in GetSpeed / Obj_GetDistanceToObject: no
        float input in 2M random tries changes a result). Quirks kept: no type -> the parameter structure's address as
        the type; an unmakeable component type gives a null component that is added anyway (crash); AddComponent
        doesn't clear the component's next pointer.
  - [x] 80B (composite.cpp, 7 functions): Obj_GetOrientation 0x45DF70, Obj_GetDistToNode 0x45E020,
        Obj_GetOrientationToObject 0x45E140, Obj_SetOrientation 0x45E650, Obj_GetQuat 0x45E940,
        FindNearestObjectOfType 0x45ED30, [0x37185D8A] 0x45EF30. Test RunMathTest: 42 mutations, 41 caught (26 at
        first), 1 equivalent (FindNearestObjectOfType's null check after the first search). **Mth::Vector::Normalize
        (vector.cpp) changed** to keep the length and 1/length in double, as the x87 original does (ours rounded both
        to float: 1-ulp differences; the vector tests still pass). **Mth::FastACos returns a rounded float** where the
        original leaves ST0 unrounded; 0x45E140 uses a local FastACosUnrounded, other callers of FastACos /
        Vector::Length may differ by an ulp.
  - [x] 80C (src/game/script/node_manager.*, 19 functions, 0x45D050-0x45D6E0): SkateScript::CNodeManager (singleton
        0x6F0D84 / 0x6F0D88; Create 0x45D690, Release 0x45D6E0 - singleton.cpp and main.cpp call ours), constructor
        0x45D370, AddNodeArray 0x45D520 (hashed; position= -> pos= with z negated; links= indices -> node names),
        RemoveNodeArray / RemoveNodeArrayOfFile / HasNodeArray, HashNodeIn, HashNodes, ResolveLinks, the last array /
        name, the node walk (StartWalk / EndWalk / NextNode), the owner pointer, MakeNodeArrayName (levels\nj\nj.qb ->
        nj_nodearray), NodeArrayChecksum. The hash helpers before the range (0x45CE40 / 0x45CE60 / 0x45CEF0 /
        0x45CFC0, 0x45C900, 0x45BE40) stay GameFunction. Test RunNodeTest: 39 mutations, all caught (5 by a crash, 1
        after strengthening).
  - Findings: composite objects and their three subclasses (vtables 0x64EC44, 0x64EF48, 0x64F138) keep CObject's
    +0x14 - a targeted event reaches only the object's own script, never components; only screen elements override
    it (0x4A8100, 0x4AA560, 0x4AE6A0). Suspending an object also suspends its components (the base component's
    Suspend sets their flag). Skipped: **0x45E9E0** (member function 0x0168B09C, probably Obj_GetCollision: 848 bytes
    of x87 building a CFeeler 0x417760 / collision 0x4172F0) - with the feeler code.
- [x] Batch 81, leftovers and cleanups (20 functions; NOT run in the game yet): the front end's last pieces
      (src/game/front/front_end.*, 8 functions, 0x54EBE0-0x54EF30): ReleaseFrontEnd 0x54EBE0, DestroyBody 0x54EC20,
      PadsPluggedIn 0x54ECA0, RemoveKeyboardHandler 0x54ECD0, **AutoRepeat 0x54ECE0**, Destroy 0x54EE70, Start
      0x54EEA0, Stop 0x54EF30 (PadMapping's two unknown words are the auto-repeat state and its ms timer; held
      directions in priority left / right / up / down; while the startup script runs, AutoRepeat returns 0 for
      controller 0; quirks kept: the destructor never deletes the keyboard task, Start puts each handler on its own
      index's controller). Tests TestFrontEndAutoRepeat, TestFrontEndModule: 41 mutations, 40 caught, 1 equivalent.
      Cutscene pieces (src/game/obj/cutscene.*, 10 functions): CutsceneNameFromPath 0x4638B0 (path in EDX: naked
      stub), CCutsceneData NameChecksum 0x463910 / IsDone 0x4639A0 / UpdateVblanks 0x4639C0 / SetSubtitles 0x463A10 /
      CheckSkip 0x464120 / UpdateSubtitles 0x4644A0, CCutsceneDetails (vtable 0x647920) slots 9 0x464580, 11
      0x463A50, 13 0x463A60. Tests (selftests_left.cpp) TestCutsceneNameFromPath, TestCutsceneData,
      TestCutsceneDetails: 48 mutations, 47 caught, 1 equivalent (NaN `<` vs `!>=`). Lst::Head::AddNode 0x45A540
      (src/game/lst/head.*; objman.cpp calls it): TestHeadAddNode, 5 mutations caught. CreateCompositeObjectManager
      0x45F7A0 (compositemgr.cpp): TestCreateCompositeObjectManager, 5 mutations caught. Cleanups: core_physics.cpp
      calls CObject's SelfEvent / BroadcastEvent directly; event.cpp / tracker.cpp use CObject's members
      (OBJECT_FLAG_HAS_POSITION 0x20 and OBJECT_FLAG_SPAWN_HANDLERS 0x10000 added; the tracker test doesn't cover a
      negative alias count); cscript.h's CMemberFunctionEntry is 0x18 bytes {mName, mComponentType, mCode,
      mThisAdjustment, mVirtualBaseOffsets[2]}.
- [x] Batches 79-81 tested in the game (2026-10-17, by the user): game runs; 1136 of 1136 replacements installed
      (35 source only), early 125 / late 17 / engine-started 2 self-tests pass, shadow checks no mismatches.
- [x] Widescreen front end (2026-10-17, at the user's request; NOT run in the game yet): PartyMod fits the
      front end to 4:3 (DesiredAspectRatio: CurrentLevel() == 0 -> 4:3), so the main menu was pillarboxed - by
      design, not a regression. New thug2-decomp.ini [features] widescreen_menus (default 1): in the front end
      the 3D view uses the window's shape (SetScreenAspectRatio / GetScreenAngleFactor via Desired3DAspectRatio;
      RenderWorld keeps the full-screen viewport instead of SetDisplayRegion; Draw2DWithBlackBars draws no bars)
      while the 2D menu layout stays 4:3 and centred (SetLetterbox's scale/offset and the 2D pass's region still
      use DesiredAspectRatio). widescreen_menus=0 is PartyMod's behaviour. Test TestWidescreenMenus (worked-out
      values: front end on/off, a level on/off on 1920x1080; 2 deliberate bugs caught); TestRenderWorld runs with
      it off (it compares with the original plus PartyMod's patches). The RenderWorld / bar parts are only
      checked in the game.
- [x] Batch 82, sprite draw and the widescreen front end's edges (branch wt/sprite; **NOT run in the game yet**;
      disable with `[replacements] disable=sprite.cpp`): **Nx::SSprite::Draw 0x004DFCF0** (src/game/gfx/sprite.*;
      thiscall, no arguments, no return value, only reached through SSprite's vtable 0x0064AD28: [0] destructor
      0x4DFCC0, [1] BeginDraw 0x4DF990, [2] Draw, [3] EndDraw 0x4DFA40; same drawable base as SText). Fields: +0x14
      texture, +0x18/+0x1C position, +0x20/+0x22 size, +0x24/+0x28 scale, +0x2C/+0x30 hot spot, +0x34 rotation,
      +0x38 RGBA, +0x3C/+0x3D flipV/flipU. One indexed quad (DrawIndexedPrimitiveUP, indices 0x67EFA8, FVF 0x144).
      Quirks kept: scale 0 or alpha 0 draws nothing, negative scale mirrors; the flips go through a 16-bit integer
      (values between 0 and 1 become 0); an upright sprite as wide / tall as the 640x480 layout or more fills the
      back buffer, otherwise it's mapped through 0x786D80-0x786D8C - except a y edge exactly at 0 (it ignores
      PartyMod's 36-pixel offset at 1080p); rotated sprites (corners by 0x4882D0, a Vector RotateZ not in Ghidra's
      database, not rewritten) are mapped and truncated to whole pixels. New with widescreen_menus (front end only):
      an upright sprite's layout edge <= 0 is drawn from screen x 0 and one >= 640 to the back buffer's width (each
      edge on its own; y unchanged) - also for sprites turned to upright or on their side (the menus' bottom band is
      10 strips with rot_angle 180, thememenu.qb); other angles unchanged. Side effect: a front-end sprite sliding in
      from off the layout's left is stretched from screen x 0 while it moves. Text and the loading bar unchanged.
      Tests (selftests_sprite.cpp): TestSpriteDraw (original vs ours, the setting off, 1792 sprites x 4 back buffers,
      every device call and vertex) and TestSpriteWidescreenEdges (worked-out x values at 1920x1080); 48 deliberate
      bugs, all caught (3 after strengthening).
      Finding: the network menu's backdrop panel (dark purple with the city) is **not a sprite** but a 3D model
      screen element - make_mainmenu_3d_plane (mainmenu_net.qb) makes an **Element3d** with "UI_bg\UI_bg.mdl" (pos
      (320, 225), scale (2.1, 2.2, 2.1), CameraZ -500) drawn through the 3D path, so the sprite rule doesn't cover
      it. PartyMod's 4:3 viewport cropped its right ~8 layout units; now it shows whole, ending 17 px past the 4:3
      area on the right while its left end sits exactly at the 4:3 area's left edge (x 240) - what looks "offset".
      Why it stops at 240 and how Element3d projects with the wide 3D view needs its draw code (not decompiled).
      What to look at in the game (a wide window, widescreen_menus=1): the main menu and the network "ACTIONS" menu -
      the top band should start at the screen's left edge, the bottom band (180-degree strips) and the streak lines
      reach both edges; menu text, highlight bars and boxes exactly where they were; widescreen_menus=0 identical to
      before (PartyMod's pillarbox); in a level the HUD sprites unchanged.
- [x] PartyMod credited again (2026-10-17, at the user's request): every reference by the generic name in the source,
      tools and docs is "PartyMod" again; comments introducing code that does what PartyMod did read "Originally from PartyMod
      Fixes:"; identifiers renamed (partyModMotors, PartyModReorderFlashVertices, WritePartyModGlyphs...); per-file
      headers read "Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md". Runtime
      strings other than self-test messages unchanged (window title "THUG2", the log's "pc:" prefix, thug2-decomp.ini).
- [x] Sprite edge rule fix (2026-10-17, from the user's in-game screenshots and a debug_sprites=1 log; NOT run in
      the game yet): the menus' bottom band is 64x64 strips turned by pi + 0.001 (3.14259), so the rule's corner
      line-up test (0.01 layout units) rejected them and they kept their 4:3 ends (screen x 44 / 67 .. 1756 / 1780).
      Now: 'upright' is decided from the angle (within 0.01 rad of a multiple of 90 degrees), and the two leftmost /
      rightmost corners in layout space are the sides moved to the screen's edges (a turned sprite's own left corners
      are on the right). TestSpriteWidescreenEdges gained the pi + 0.001 case (on / off / in a level); 2 deliberate
      bugs caught. [features] debug_sprites=1 (new, off by default) logs front-end sprites reaching past the layout.
- [x] Batch 83, Element3d and the widescreen front end's backdrop (branch wt/el3d, 24 functions; **NOT run in the game
      yet**; disable with `[replacements] disable=element3d.cpp` - the class and the rule; the matrix helpers are in
      matrix.cpp, the aspect / angle getters in screen.cpp, the layout getters in screen_element.cpp; the rule alone
      is off with `[features] widescreen_menus=0`, which also turns off the rest of the widescreen front end):
      **Front::CElement3d** (src/game/front/element3d.*; vtable 0x649FF4, 0x510 bytes): constructor 0x4A6060,
      destructor body 0x4A6240, scalar deleting destructor 0x4A6D00, SetProperties 0x4A6630, SetMorph 0x4A6BA0, Update
      0x4A6D30, ClearModels 0x4A5E30, ApplyViewport 0x4A5E80, AddModel 0x4A6540 (`RET 0x14`: a Vector by value),
      ScaleToScreenDims 0x4A62B0, ApplyAngles 0x4A6BD0, **ScreenToWorld 0x4A5EE0** (EAX = centre, ESI = out matrix,
      then cdecl args: naked stub, REPLACE_UNCHECKED). CScreenElement GetLayoutWidth 0x4A87A0 / GetLayoutHeight
      0x4A87B0 (return the x87 product unrounded: `double`; the layout's fields named up to sizeof 0x178).
      Mth::Matrix Ident 0x41A640, Scale 0x41BA30, Translate 0x48A230, RotateX/Y/Z 0x41A6A0/0x41A6D0/0x41A700,
      `Vector * Matrix` 0x41F470 (x87 summation order). Nx GetScreenAspect 0x4A0790, Set/GetScreenAngle 0x4A07A0 /
      0x4A07B0 (0x701348, from camera_fov).
      How it draws: its models (up to 25) are ordinary composite objects drawn by the 3D pass with the world - not
      clipped by the 2D pass's 4:3 viewport. ScreenToWorld maps the layout centre into the camera's frame
      (x' = (x - 320) * aspect * 0.75 * z / -448, y' = (y - 224) * z / 448, * 0.9 on back buffers wider than 640,
      z' = CameraZ, -500 for the backdrops), so in the widescreen front end 3D elements away from x 320 sit further out
      than 4:3 layout positions would (the goals menu's sign) - the original's own behaviour, unchanged. With
      ParentParams it follows an object (gone -> spawns KillElement3d) or a node. Models: angles in AngleOrder
      (default Y X Z) about the pivot, scaled, hover sin(2 pi t / period) * Amp, hidden below alpha 0.0001.
      Quirks kept: ScaleToScreenDims' vertical fit uses the width in its perspective factor; uninitialised bounds /
      look-at target (zeros here); NaN comparisons follow the x87 flag tests.
      **Widescreen rule (front end only, widescreen_menus=1)**: the backdrop panel (UI_bg.mdl from
      make_mainmenu_3d_plane, used by the network, goals, Create-A-Graphic, high scores, options, dialog and keyboard
      menus; also catmenu's cat_bg.mdl) is a hard-edged model sized for 4:3, so Update stretches the models' x column
      by k when the element is on the layout facing the camera, every model corner is in front of the camera and its
      projected left / right corners reach within 5% of the 4:3 area's edges or past them; k = the smallest stretch
      bringing a corner to each window edge + 1% (never below 1). Signs, icons, boards, decks are left alone. The
      panel's picture is stretched by about 1.33 on 16:9. A model without geometry yet leaves the element unchanged.
      **Sprite change (sprite.cpp, same branch)**: the edge rule now extends a side only for sprites that overlap the
      4:3 layout (left only if the right edge > 0, right only if the left edge < 640); a sprite wholly outside (the
      bottom band's end strip, about layout -87..-10, which used to be stretched to screen 0..217 over the next strip:
      the bottom-left "shade") is drawn where it is.
      Tests (selftests_el3d.cpp): TestElement3dMath, TestElement3dClass (original vs ours on stand-ins, setting off:
      constructor, SetProperties, SetMorph, Update in 7 layout cases + following node / object / object gone,
      destructors; element and arena compared after each step), TestElement3dWidescreenRule (worked-out k on
      1920x1080 and 7 unchanged cases), TestElement3dWidescreenUpdate; 73 mutations, 69 caught (9 after
      strengthening), 4 equivalent (float vs double `* 0.9`, an unused w, slot release order, a w rebuilt by
      OrthoNormalizeAbout). TestSpriteWidescreenEdges gained 8 wholly-outside cases; 8 mutations caught.
      To look at in the game (16:9 window, widescreen_menus=1): the network ACTIONS menu, Select Goals,
      Create-A-Graphic, High Scores - the dark purple panel should reach both screen edges (no skate shop showing at
      the sides), its picture stretched by about a third; menu boxes, text, signs, skater / board / deck models
      unchanged; the bottom-left band no longer shaded. widescreen_menus=0 and levels exactly as before. If the
      panel's edge shows a seam, say which menu and window size.
      Left: 0x41A680 (another class's Vector getter, between Ident and RotateX); 0x4A57D0 / 0x4A5D30 (another element
      class); the rest of the screen element code 0x4A74E0-0x4A8790 and CScreenElement itself (0x4A8F90, 0x4AAB30,
      0x4A96B0, 0x4A9AA0); the camera lookups 0x4A09C0 / 0x4A06D0, getters 0x485A00 / 0x485A10 / 0x485B10, model
      bounding box 0x493CD0 / 0x48DE80, viewport mask 0x494970, node lookups 0x45C1B0 / 0x45C050 (GameFunction).
- [x] Batch 84, engine net library 0x456910-0x457EB0 (branch wt/netA, 47 functions, nothing skipped; **NOT run in
      the game yet**): src/game/net/net_latency.* (**Net::LatencyTest** 0x38: ctor 0x456970, InputLatencyValue
      0x4569B0), net_lists.* (the library's Lst::Node / Head / Search destructors, **BannedConn**, the Dispatcher's
      handler-list array destructor 0x457260), net_app_view.* (**Net::App** methods on `AppView`, a view of App's
      fields at their offsets - App's constructor is batch 85's; Conn's 0x455C80 is below the range, hence
      `ConnView`), Lst::Head::AddNodeFromTail 0x457350 (head.cpp). Functions: Encrypt 0x456910 / Decrypt 0x456CC0
      (16-bit checksum = CRC halves added, XOR with its low byte; Encrypt's args are (data, out, length,
      &outLength) - Ghidra's order was wrong, found by a test crash), task getters 0x456A50-0x456A80, ReportError,
      IsSendDue (with wrap), VirtualSlot5Jump 0x456B30 (naked `jmp [vtable+0x14]`, linker-folded), Bind 0x456BA0,
      NewQueuedMsg 0x456D90, SendTo / Send / SendMessageTo, Init 0x457110 / Deinit 0x457490 (App vtable slots 4/5),
      TerminateConnection, BanConnection, NextConnection, DispatchQueuedSequencedMessages 0x457560, the destructor
      body 0x457710, GetConnectionByAddress / ByHandle, ImportantMessagesDue 0x457970, MessagesDue 0x457AC0,
      FreeMessages 0x457C80.
      Layouts: App (vtable 0x6475B4; +0x08 time, +0x0C Dispatcher, +0x1428 SOCKET, +0x143C connected, +0x1440
      Net::Manager*, +0x1444..+0x1450 four tasks, +0x145C scrambled-packet buffer, +0x1E84 flags (1 broadcast, 8 local,
      0x20 secure), +0x1E8C Lst::Head<Conn>, +0x1EA0 banned list, +0x1EB8 name); Conn (0x3D20: three outgoing queues
      +0x00/+0x14/+0x28, handle +0x3C, ip +0x40, port +0x48, next expected sequence per group +0x478, received-ahead
      lists +0x87C, LatencyTest +0x1CA4, node +0x3CF8, alias +0x3D0C); queued messages (SeqQueuedMsg 0x10, ctor
      0x455040; both kinds freed by 0x4550F0); MsgHandlerContext (0xA4, ctor 0x454A20).
      Quirks kept: Bind's first argument is never read and its Manager +0x128 call only sets INADDR_ANY again;
      MessagesDue treats an important / sequenced link as due when the time is *before* its +0x24 time (JC);
      DispatchQueuedSequencedMessages bumps the sequence before dispatch and reuses one context (fields carry over);
      handler results 1 next, 3 return false untouched, 5 keep queued, else delete.
      Tests (selftests_netA.cpp): TestNetLatency, TestNetLists, TestNetSockets (Winsock stood in at the import slots
      and in ws2_32 itself - no real socket), TestNetQueues; original vs ours, call traces and object memory
      compared. 54 mutations: 45 caught at first, 7 after strengthening, 2 rewritten to build, 1 equivalent
      (HandlerLists::Destroy's order).
- [x] Batch 85, engine net library 0x457EB0-0x4598C0, Net::App (branch wt/netB, 19 functions incl. two task callbacks
      Ghidra never made functions, 0x4593C0 / 0x4594A0; **NOT run in the game yet**): src/game/net/net_app.*:
      EnqueueMessage 0x457EB0 (93 callers), StreamMessageToConn 0x458740, StreamMessage 0x458AB0, FirstConnection,
      SendEnqueuedMessages 0x458C10, SendData 0x458CA0, IsConnectionBanned, GetBandwidthUsed 0x458DB0,
      BandwidthExceeded 0x458E60, GetFreeHandle, TerminateInvalidConnections, ProcessStreamMessages 0x4590C0, the
      tasks ReceiveNetworkData 0x4593C0 (tail-jumps to slot 2: "not checked" by thug2_check) / NetworkMetrics
      0x4593E0 / SendNetworkData 0x4594A0, the constructor 0x4594C0, deleting destructor 0x459660, ProcessData
      0x459690 (slot 1), NewConnection 0x4597C0. `struct App : AppView` adds only methods (same size); the tasks are
      +0x1444 process (0x456CA0, priority 0), +0x1448 send (-2000, built by the Client's / Server's constructors),
      +0x144C receive (1000), +0x1450 metrics (-2500). Vtables: App 0x6475B4 = [0x459660, 0x459690, purecall
      ReceiveData, purecall SendData(Conn*), 0x457110 Init, 0x457490 Deinit]; **Client** 0x6475CC (constructor
      0x45A250) = [0x45B8E0, 0x459690, 0x45A300, 0x459FD0, 0x459EC0, 0x45B820]; **Server** 0x6475EC (constructor
      0x45B830) = [0x45B8E0, 0x459690, 0x45BBD0, 0x45B920, 0x45B7A0, 0x45B820] (netB's record had Client and
      Server swapped; batch 86 settled it from the code). Net::MsgDesc (0x20, ctor 0x4549F0: id, stream piece, length,
      data, priority, queue 0/1/2 = default / important / sequenced, group, singular, delay, forced sequence,
      compression 0 / 1 RLE / 2 LZSS) and StreamDesc (0x44) are in net_app.h; Conn's other fields (send / receive
      buffers, status, flags 1 local / 2 remote / 4 foreign, metrics +0x1CDC / +0x2CE8, bandwidth) by offset.
      Findings: StreamMessageToConn / ProcessStreamMessages leave the stack MsgDesc's singular and compression
      uninitialised (a stray compression value would garble every piece, so in practice 0) - ours zeroes them;
      the stream start message's length is 3 bytes past the copied data. Quirks kept: a sequenced broadcast gives
      each link the queue number (2) as its priority; default-queue messages to a single handle also go to a
      not-ready connection when the id is 9; GetConnectionByHandle's result and allocations are never checked.
      Tests (selftests_netB.cpp): TestNetAppMessages, TestNetAppConnections (stand-in memory manager, app and
      connections in a 64 KB-aligned arena; the game's own queue / link / stream helpers run; arena and call log
      compared, stream timing per frame); 68 mutations, all caught (10 after strengthening), 2 equivalent dropped.
- [x] Batch 86, engine net library 0x4598C0-0x45BDA0 - the library from 0x456910 up is now complete (branch wt/netC,
      36 functions incl. 6 message handlers Ghidra never made functions; **NOT run in the game yet**):
      net_dispatcher.* (**Net::Dispatcher** at App+0x0C: 255 handler lists, one per message id; **MsgHandler** 0x20:
      code `int __cdecl(MsgHandlerContext*)`, data, packet-flags mask; AddHandler 0x45A5C0, Init 0x45A640, Deinit
      0x45A690, DispatchMsg 0x45A750 (Neversoft's DispatchMessage, renamed for the windows.h macro),
      DispatchMsgHandlers 0x45A830), net_client.* (**Net::Client**, ctor 0x45A250: StreamMessageToServer 0x459DC0,
      EnqueueMessageToServer 0x459E50, Init 0x459EC0, ConnectToServer 0x459F40, SendData 0x459FD0, ReceiveData
      0x45A300, HandleTimestamp 0x45AA70, HandleConnectionAccepted 0x45ABC0 / Refused 0x45ABD0; the shared deleting
      destructor 0x45B8E0 and the jump thunks 0x45B820 -> Deinit / 0x45B910 -> ~App, REPLACE_SOURCE_ONLY),
      net_server.* (**Net::Server**, ctor 0x45B830: Init 0x45B7A0, SendData 0x45B920, ReceiveData 0x45BBD0,
      HandleTimestamp 0x45AA30, HandleConnectionRequest 0x45ADB0, HandleDisconnRequest 0x45AE50),
      net_app_messages.cpp (App::BundleMessages 0x4598C0 and the handlers App::Init registers: latency test /
      response 0x45AD60 / 0x45AB60, compressed 0x45ABF0 (id 0x12 - batch 84's record called it "disconnect"),
      sequenced 0x45AF10, ack 0x45B0B0, stream 0x45B2F0), Lst::Head::AddUniqueSequence 0x45AEB0 (head.cpp; game code
      0x567257 / 0x567465 calls it too).
      Client vs Server from the code: 0x45A250's Init registers 4 "connection accepted" and 5 / 6 "refused /
      terminated", it connect()s to a server address, and chat sends "to the server" through it (GameNet manager
      +0x08); 0x45B830's Init registers 3 "connection request" and 0xB "disconnect request" and its ReceiveData makes
      connections for unknown senders. Message ids: 1 latency test, 2 response, 3 connection request, 4 accepted,
      5 refused, 6 terminated, 7 sequenced, 8 ack, 9 timestamp, 0xB / 0xC disconnect request / accepted, 0xE / 0xF
      stream start / data, 0x12 compressed. Handler results: 0 error, 1 continue, 2 halt, 3 connection destroyed,
      4 done, 5 keep.
      Integration: the three ranges call each other directly (Dispatcher derives from batch 84's HandlerLists, Conn
      is `using Conn = ConnView`, Client / Server derive from `struct App : AppView`; App::Init registers the C++
      handlers; ProcessData calls DispatchMsgHandlers); each test file stands in for the others' C++ functions.
      Quirks kept: a message without a length field copies a whole 0x514-byte buffer and advances by the previous
      length; 32 timestamps to ack with no bounds check; Client::ReceiveData looks the sender up by the server's
      stored address; 16-bit packet size checks wrap; an unknown compression method uses the buffer as is; a full
      stream start skips the checksum check; ConnectToServer loops forever on WSAEWOULDBLOCK; failed allocations
      crash. Difference: Client::ReceiveData's sockaddr is zero-initialised (uninitialised in the original; only the
      foreign-packet callback sees it).
      Tests (selftests_netC.cpp, 9 Early): original vs ours on identical stand-in worlds (App, three Conns, WS2_32
      through its import slots): the dispatcher, DispatchMsgHandlers (14), BundleMessages (26), every handler,
      Client / Server construct / destroy / init / connect / enqueue, ReceiveData (10 each), SendData (19 each);
      90 mutations: 81 caught at once, 5 after strengthening, 4 equivalent.
      **To test batches 84-86 in the game**: the network menu; host a LAN game and join it from another machine or a
      second instance (connecting), chat, a client joining mid-game (level data and big messages are streamed),
      play a game mode, disconnect / leave and rejoin; online play if available; check thug2-decomp.log. Disable
      with `[replacements] disable=net_app.cpp,net_app_view.cpp,net_lists.cpp,net_latency.cpp,net_client.cpp,
      net_server.cpp,net_dispatcher.cpp,net_app_messages.cpp` (head.cpp also holds AddNodeFromTail and
      AddUniqueSequence, but disabling it also disables Lst::Head::AddNode).
      **Subsystem map**: the net library starts at **0x4544E0**, not 0x456910 (data/subsystems.csv and
      subsystems.md split: Movies (Bink) & streamed audio 0x44E6B0-0x4544E0, 167 functions / 22 KB; net library
      0x4544E0-0x45BDA0, 205 / 26 KB). Evidence: the Pcm / Bink stream code runs through 0x454490 (71 bytes, ending
      0x4544D7: a handle setter calling the GetLastError helper 0x454370) - 0x452910-0x454490 use BinkOpen /
      BinkSetVolume / BinkClose / BinkGoto, the pcm.wad / music .bik strings and the stream mutexes, and every
      caller is in 0x450xxx-0x453xxx (plus 0x4677F0 / 0x54F380 / 0x5A8F70 calling the shuffle 0x452910). From
      0x4544E0 everything is net: 0x4544E0 is the Metrics constructor (0x100C bytes, zeroes +0x800..+0x1008), called
      only by Conn's constructor 0x455C80; 0x454520 the Metrics getter (+0x1008) used by GetBandwidthUsed /
      BandwidthExceeded; 0x454530-0x4549C0 Net::Manager getters called from game code (0x52Exxx-0x53Fxxx,
      0x5A0xxx) (0x4548F0 inet_addr, 0x454910 gethostbyname / "localhost"); then MsgDesc 0x4549F0,
      MsgHandlerContext 0x454A20, QueuedMsg / SeqQueuedMsg 0x454FA0 / 0x455040, the link constructors, Conn's
      methods 0x455970-0x456790.
- [x] Several copies of the game (2026-10-17, at the user's request, for testing LAN play on one PC; NOT run in the
      game yet): thug2-decomp.ini [features] allow_multiple_instances=1 skips WinMain's already-running check (a window
      titled "THUG2" -> bring it to the front and quit - the original's; PartyMod's "THUG2 - PARTYMOD" title made it
      never match, our "THUG2" title made it work again). Nothing else stops a second copy: the C runtime's
      CreateMutexA (0x6409D0) is unnamed, and AppView::Bind (0x456BA0) already retries on port 0 when the game's port is
      taken (so one copy hosts on the fixed port, the other gets a free one). LogOpen: when thug2-decomp.log is held by
      another copy (fopen_s files aren't shared), it writes thug2-decomp-2.log .. -9.log.
- [x] Batches 83-86 and allow_multiple_instances tested in the game (2026-10-17, by the user): two copies on one
      PC, hosting and joining a LAN game works; 1261 of 1261 replacements installed (37 source only), late / engine-
      started self-tests pass. Two early self-tests failed in the game: TestWinMain (it compared with the original
      while allow_multiple_instances=1 skipped the check - fixed: Sys::AllowMultipleInstances() is settable, the test
      turns it off) and, in the first copy only, netB's 'net app: queued and streamed messages' (6 arena words
      differ only in a stale high half next to the 0xCD fill - likely the 3 bytes the original's stream start reads
      past the copied data, which hold whatever is there in the game; the test needs to make them identical in both
      runs or mask them).
- [x] Batch 87, engine net library 0x454FA0-0x455970 (branch wt/netE, 28 functions, nothing skipped; **NOT run in
      the game yet**): net_messages.* - QueuedMsg / SeqQueuedMsg constructors 0x454FA0 / 0x455040 (they take the id
      and length back from CompressMessage 0x454E30, which can change them to 0x12 and the compressed size), their
      destructor body 0x4550F0, the link constructors MsgNormalLink / MsgImpLink / MsgSeqLink / StreamLink
      0x4555F0-0x455680 and scalar deleting destructors 0x4557C0-0x455850, the Lst::Node<T> deleting destructors
      0x455130-0x455230 (StreamLink / MsgNormal / MsgImp / MsgSeq / App nodes, vtables 0x6474C8-0x6474D8),
      AppNode::DestroyBody 0x455110, Lst::Head<App>'s deleting destructor 0x4556B0 (the link structs moved from
      net_access_c.h into net_messages.h; MsgSeqLink +0x16 = m_StreamMessage); the Manager's app code (now
      net_manager_apps.cpp, see batch 89): constructor 0x4556E0, destructor body 0x4555A0, CreateNewAppServer
      0x455270 / CreateNewAppClient 0x4553F0 (Server 0x2308 bytes, Client 0x1F18), DestroyApp 0x455550,
      CreateManager 0x455880 / ReleaseManager 0x4558D0 (singleton 0x6EFDD8; singleton.cpp's `NetManager`, main.cpp
      calls them directly); net_conn_times.cpp - Conn::UpdateCommTime 0x455920, GetTimeElapsedSinceCommunication
      0x455940, SetSendInterval 0x455960 (+0x6C last communication time).
      Findings: CreateNewApp* bind to the configured local IP (+0x134 via inet_addr) instead of the address argument
      when +0x2E5 (DHCP) is set and the connection type is neither 1 nor 3; a local app (flag 8) isn't bound; a
      local server, or flags bit 0, counts as connected (+0x143C); allocations unchecked. The constructor clears
      message flags 0-254 only (ids 8 ack / 9 timestamp: no length field). Quirks kept: DestroyApp checks the app
      for null only after using it; the compression argument is passed as a whole dword.
      Tests (selftests_netE.cpp, 3 Early: TestNetMessages, TestNetManager, TestNetConnTimes - stand-in memory
      manager, compressor, Server / Client constructors, app vtable, Bind, Winsock, timer; call traces, memory and
      lists compared): 63 mutations, 60 caught at once, 2 after strengthening, 1 equivalent (CreateManager's
      `Instance() = nullptr` on a failed allocation).
- [x] Batch 88, engine net library 0x455970-0x456910, Net::Conn (branch wt/netF, 41 functions incl. three pieces
      Ghidra never made functions; **NOT run in the game yet**): net_conn.* - constructor 0x455C80 and destructor
      0x456790, the getters / setters 0x455970-0x455A40 (GetSendInterval 0x455970 REPLACE_SOURCE_ONLY, 4 bytes;
      IsForeign / IsLocal / IsRemote = flags +0x64 bits 4 / 1 / 2; SetBandwidthType: 0 broadband 400000, 1 modem
      4200, 2 LAN 800000 bytes/s, others keep the bandwidth), GetAveLatency 0x455B40 (0 for local), Invalidate /
      IsValid 0x455C10 / 0x455C20 (status +0x60: bit 0 ready, bit 2 invalid), GetResendThreshold 0x455C30 (status
      bit 1: 1000 ms, else twice the average latency, at least 500, signed), DestroyReceivedSequencedMessages
      0x455E50, DestroyLinks 0x455F30, DestroyImportantMessages 0x456210, ReleaseQueuedMessages 0x456370,
      GetHighestResendCount 0x456540, GetNumQueuedStreamPieces 0x4565E0, ClearResendCounts 0x456630,
      FlagMessagesForResending 0x4566D0, DestroyMessageQueues 0x456770; the list types (Lst::Search<T> destructor
      bodies 0x4559D0-0x455A00 / deleting destructors 0x455A80-0x455B10, Lst::Node<T> destructor bodies
      0x455B50-0x455BB0, Lst::Node<Conn>'s 0x455BD0 in net_lists.cpp, Lst::Head<MsgSeqLink>'s constructor
      **0x455C60**); and batch 85's two by-address pieces **0x457010** (Lst::Head<MsgHandler>'s constructor) and
      **0x456CA0** App::ProcessNetworkData (++frames, tail jump to vtable slot 1; "not checked" like 0x4593C0).
      Conn (0x3D20): three queues +0x00 / +0x14 / +0x28, handle +0x3C, ip +0x40, id +0x44 (running global
      0x67D800), port +0x48, write / read buffers +0x4C / +0x54 (0x514 / 0xA28), status +0x60, flags +0x64, App
      +0x68, last comm / send time +0x6C / +0x70, send interval +0x74, sequence ids out / expected per group +0x78 /
      +0x478, next stream id +0x878, 256 received queues +0x87C, received / sent streams +0x1C7C / +0x1C90,
      LatencyTest +0x1CA4, Metrics in / out +0x1CDC / +0x2CE8, force-send +0x3CF4, app node +0x3CF8, alias +0x3D0C,
      bandwidth type +0x3D10, timestamps +0x3D14 / +0x3D18, bandwidth +0x3D1C (400000).
      Correction: **netC's GetNumPendingMessages is really GetHighestResendCount** (the most resends of any
      important / sequenced message, not a count; the old name wraps it). Quirks kept: the constructor zeroes only
      the first 64 groups' sequence ids (both arrays); the bandwidth type stays uninitialised.
      Tests (selftests_netF.cpp, TestNetConn, Early: four connections built by the original / our constructor on
      dirty memory in a 64 KB-aligned arena, filled through the game's link constructors, every function run, the
      whole arena compared): 56 mutations, 54 caught at once, 1 after strengthening, 1 equivalent (`<` vs `<=`
      against the 500 ms minimum).
      **Net clean-up** (wt/netF after merging netE; no behaviour change): **Net::Conn** is a real struct
      (net_conn.h, every field named and static_asserted; ConnView gone); **Net::App** is one struct in net_app.h
      (AppView folded in, sizeof 0x1F08; the Client adds m_serverAddress, 0x1F18); net_access_c.h's by-offset
      accessors became members; chat.cpp's and cscript_manager.cpp's MsgDesc copies are net_app.h's Net::MsgDesc.
      Renames: App::m_counters[6] -> m_totalBytesOut / m_totalBytesIn / m_lostPackets / m_latePackets /
      m_dupePackets / m_frameCount, m_unknown1E88 -> m_foreignPacketHandler, kFlag10 -> kAcceptForeign (+
      kMeasureLatency 4), Conn::m_unknown44 -> m_id, m_unknown878 -> m_nextStreamId, CreatedTime / LastCommTime ->
      Conn::m_lastCommTime; IsNew / MarkNewConnection wrap IsValid / Invalidate.
- [x] Batch 89, engine net library 0x4544E0-0x454FA0 (branch wt/netD, 44 functions, nothing skipped; **NOT run in
      the game yet**): net_metrics.* - **Net::Metrics** (0x100C: packets[256] {size, time}, count +0x800, messages
      and message data per id +0x804 / +0xC04, total bytes +0x1004, bytes/s +0x1008): constructor 0x4544E0,
      GetBytesPerSec 0x454520, GetTotalMessageData 0x454530, GetTotalNumMessagesOfId 0x454540, AddMessage 0x454550,
      CalculateUsage 0x454D00, AddPacket 0x454D90; net_manager.* - **Net::Manager** (0x37C; netE's ManagerView folded
      in, net_manager_view.cpp renamed net_manager_apps.cpp): Connect / DisconnectFromInternet 0x454580 / 0x454590,
      the getters and setters 0x4545A0-0x4549B0 (dial-up number / user / password, gateway / subnet mask / local IP
      (copy only if different), local address, connection type 0x454770 (bandwidth 1: 4200, 2 / 4: 400000, 3:
      300000), device type, DHCP, SetFlag2E6 (name unknown, pref checksum 0xE5ADE642), DNS servers, host / domain
      name, GetLocalAddress 0x4548F0, GetLocalIP 0x454910, message flags, modem state), LoadNetworkDrivers 0x4549C0
      (returns true), AddAppTasks 0x454CB0 (the app's receive / send / process / metrics tasks), GetDialupNumber
      0x454DE0; layout +0x00 / +0x14 server / client lists, +0x28 message flags[256], +0x128 connection type, +0x130
      device type, +0x134 local IP, +0x144 local address, +0x148 DNS[3], +0x178 gateway, +0x188 subnet mask, +0x198
      dial-up index, +0x19C numbers[3], +0x1FC user, +0x24C password, +0x29C host, +0x2BC domain, +0x2DC apps made,
      +0x2E0 last error, +0x2E4 online, +0x2E5 DHCP, +0x2E6 flag, +0x2E8 modem state (5), +0x374 baud rate (?),
      +0x378 bandwidth (400000) - names from the network preferences GameNet copies in (0x535040);
      net_descs.cpp - StreamDesc::Construct 0x4549D0 / DestroyBody 0x454E00, MsgDesc::Construct 0x4549F0,
      MsgHandlerContext::Construct 0x454A20; net_compress.* - RleCompress 0x454A60, RleDecompress 0x454C20,
      CompressMessage 0x454E30 (cdecl, RLE or LZSS). Every caller calls them directly now (Conn's Metrics typed).
      Correction: **MsgDesc's constructor DOES set singular and compression** - batch 85's note that
      StreamMessageToConn / ProcessStreamMessages leave them uninitialised was wrong (comments fixed).
      Findings / quirks kept: RleCompress codes a 1-byte input to nothing (returns 0), runs 2-0x81 bytes (0x80 +
      n - 2), literals 1-0x80 (n - 1), the output size argument is never read; RleDecompress reads past the end on
      a literal longer than the input left; CompressMessage's 16-bit size wraps from 0xFFFD and overflows its
      copy, an unknown method "succeeds" with a 3-byte message, the compressed size is compared with the data's
      length; SetHostName / SetDomainName(null) clear the field and then crash; GetLocalIP reads Winsock's
      per-thread hostent after the second lookup.
      **netB's flaky test fixed** ("net app: queued and streamed messages", failed in the game in the first copy):
      not the bytes past the stream start - the six differing words are the stream starts' QueuedMsg headers (id
      0x0E, a 0xCD pad byte, length 0x0130); the test's half-pointer normalisation took the length 0x0130 for a
      stale pointer half because our run's arena landed at 0x012A0000 (segments 0x12A-0x130). Fix: both runs use
      one arena (selftests_netB.cpp Arena()); nothing masked, the whole arena still compared.
      Tests (selftests_netD.cpp, 6 Early: Metrics, Manager (Winsock and the Mlp task list stood in), descriptors,
      RLE compress (~140 inputs + round trips) / decompress, CompressMessage): 96 mutations, 95 caught at once, 1
      after strengthening; equivalent, not run: the setters' copy-only-if-different guard, SetDeviceType / SetDHCP's
      compare. After merging all three: thug2_check 0 MISMATCHED, 157 early tests pass, call audit / code writes /
      register scan clean (only the known 0x4EFF92 RISK).
- [x] **The whole engine net library 0x4544E0-0x45BDA0 is done** (batches 84-89, 205/205 functions; 87-89 NOT run
      in the game yet). To test: the network menu (settings shown / saved), host / join a LAN game
      (allow_multiple_instances=1 for two copies on one PC), chat, a client joining mid-game (streamed level data
      and big messages), disconnect / leave and rejoin; check thug2-decomp.log. Disable everything net with
      `[replacements] disable=net_app.cpp,net_app_view.cpp,net_lists.cpp,net_latency.cpp,net_client.cpp,
      net_server.cpp,net_dispatcher.cpp,net_app_messages.cpp,net_messages.cpp,net_manager_apps.cpp,
      net_conn_times.cpp,net_conn.cpp,net_metrics.cpp,net_manager.cpp,net_descs.cpp,net_compress.cpp` (the callers
      call each other directly, so disabling only unhooks the game's own callers; head.cpp also holds
      AddUniqueSequence).
- [x] Batch 90: Mouse menus, stage 1 ([features] mouse_menus, default 1) - NOT run in the game yet. Record:
      A new feature, not original game code and not PartyMod's: the mouse drives the front end's menus.
      `thug2-decomp.ini` `[features] mouse_menus=1` (the default); **`mouse_menus=0` turns it off completely** (no SDL
      mouse handler registered, `FrontEnd::InputLogic` doesn't call it, the cursor stays hidden as before).

      Code: `src/game/front/mouse_menus.{h,cpp}` (new), two calls added - `Sio::InitSdlInput` -> `MouseMenus::Init()`
      (registers an SDL event handler), and `Mdl::FrontEnd::InputLogic` (0x0054FB40) -> `MouseMenus::Update(frontEnd)` at
      its start, with the first controller's handler only (once a frame). Tests: `src/hook/selftests_mouse.cpp`.

      ## How it works
      - **Input**: SDL_MOUSEMOTION (only with a real movement: xrel/yrel non-zero), SDL_MOUSEBUTTONDOWN (left, right),
        SDL_MOUSEWHEEL; the window's size is read from the event's window. Nothing happens until the mouse has moved,
        clicked or scrolled once.
      - **When**: in the front end (`Nx::CurrentLevel() == 0`) or while the game is paused in a level
        (`FrontEnd::m_paused` - the pause menu is built from the same screen elements and gets the same broadcast pad
        events, so it should work the same way), and never in the park editor unless paused (its element 0x5999551E
        exists - the same test as Sio's InParkEditor). The OS cursor is shown exactly then (SDL_ShowCursor), hidden
        otherwise.
      - **Window -> layout**: window pixels -> back buffer pixels (window size vs 0x00786D6C/70), then the inverse of
        SetLetterbox's `back buffer = layout * scale + offset` (0x00786D80/84, 0x00786D88/8C). Note the original's +16 *
        scaleY vertical offset (the root window is 640 x 448 at layout y 0).
      - **The element tree**: the screen element manager (0x00701440) holds the root at +0x44 (SetRootScreenElement
        0x004AC900 -> 0x004AB730); an element's first child is +0x58, the next sibling +0x64, the parent +0x4C (the
        order 0x004A9600 counts and the menus index - an item's index is its place in that list). The layout pass
        (0x004A9D30, run from the root every frame by the manager's update 0x004ABD00) stores each element's absolute
        values: top left +0x134/+0x138 (justification already applied - 0x004A8C30: local top left = pos - (just + 1) / 2
        * dims * scale, then parent's position + parent's scale * that, rotated by the parent's angle +0x14C), absolute
        scale +0x13C/+0x140, alpha +0x114 (parents' included). Base dims are +0x160/+0x164 (GetScreenElementDims
        0x004AD650 returns them times the local scale +0xA4/+0xA8; GetScreenElementPosition 0x004ADA80 returns the
        *local* +0xE0/+0xE4, not useful here). So an element's rectangle = (+0x134, +0x138) + (+0x160 * +0x13C, +0x164 *
        +0x140). Rotation is ignored.
      - **Menus**: types vmenu 0x130EF802 / hmenu 0xCCDED1E1 (CBaseMenu, vtable 0x0064A048, 0x1A8 bytes; constructors
        0x004A75C0 / 0x004A7630). Fields: +0x178 selected index (-1 none), +0x17C selected item's id, +0x180 has the focus,
        +0x190 vertical, +0x1A0 movement allowed, +0x1A1 wraps. The menu's event handler (0x004A8100, vtable +0x14) moves
        the selection on pad_up / pad_down (vertical) or pad_left / pad_right (horizontal) when focused and +0x1A0, through
        0x004A7A40 -> 0x004A7850 (index += direction; past an end: wrap, or bounce back; skips items tagged
        `tag_not_focusable` (0xF33A3321, set by `not_focusable`) or with `block_events` (flag 0x200000) on them or a
        parent (0x004A8F00); unfocus/focus events to the old/new item). While the manager has pad-event priorities (+0x4E8
        > 0 and the event among its list +0x5DC/+0x69C) only elements with priority (flag 0x10000000 up the chain,
        0x004A8F20) of the top priority (0x004AB7C0) get pad events. Flags (+0x08): 0x400000 hide, 0x200000 block_events,
        0x10000000 priority > 0 (SetProperties 0x004A96B0).
      - **Hit-testing** (`FindItemAt`): a pre-order walk from the root; an element counts if its parent is a menu that
        takes pad events (has the focus + the priority test above), it is focusable (as 0x004A7850 decides), not hidden
        (subtrees of hidden elements are skipped), alpha >= 0.01, and the point is inside its area and inside every
        clipping ancestor (window elements 0xA13ADBB7 - 0x004B2060 makes a viewport - and v/hscrollingmenus, when their
        dims are non-zero). An item's area is its rectangle widened to its menu's width (vertical menu) or height
        (horizontal). Later finds win: the deepest, and of overlapping siblings the last in the tree.
      - **Hover**: when the item under the cursor changes (only on frames the mouse moved), the menu's selection is
        stepped to it with the same broadcast events the pad sends (launched like InputLogic: target/source 0x36B2EE74,
        `{device_num = the first controller's device, controller = 0}`, skipped when that mapping is inactive or menu
        input blocked): pad_up/pad_down or pad_left/pad_right, re-reading +0x178 after each event, the shorter way round
        when the menu wraps, stopping when it got there, didn't move, went the wrong way (bounce) or past the target, or
        the menu was destroyed (held through a smart pointer). Up to 4 passes, so selecting an option row can focus its
        inner hmenu and then select the hmenu item under the cursor.
      - **Clicks**: left = select the item under the cursor (always, not only on change) then pad_choose if it is now
        selected; on a vertical menu's item that has its own pad_left / pad_right handler (its script's event handler
        table), a click in the item's left 30% sends pad_left, right 30% pad_right, the middle pad_choose. Right click =
        pad_back, wheel = pad_up / pad_down (up to 5 per frame) - both only while some menu takes pad events (wheel: a
        vertical one).

      ## Not covered / known limits
      - Menus of the other element type 0x118F6383 (made by 0x004AED90, 0x180 bytes - name unknown) aren't treated as
        menus; nor are custom script-driven "menus" that read pad events without a vmenu/hmenu.
      - Rotated elements (+0x14C) and 3D-anchored ones (flag 0x80000) are hit-tested as unrotated rectangles.
      - Scrolling menus: clipping by the scrolling menu's own rect is a guess (its dims are taken as the visible window).
      - Right click / wheel do nothing on screens without a focused menu (e.g. "press any button" screens).
      - The park editor's and Create-a-Graphic's cursors are not mouse-driven. CAG's tool menus are ordinary menus, so
        hovering them moves their selection like the pad would.
      - Each hover step is a real pad event: menus play their focus sounds / animations per step (as holding the pad).

      ## Stage 2 should replace
      - The offset reads (`Field<>` in mouse_menus.cpp: +0x08, +0x4C, +0x58, +0x64, +0x114, +0x134..+0x164, +0x178,
        +0x180, +0x190, +0x1A1; the manager's +0x44, +0x4E8, +0x5DC, +0x69C) with CScreenElement / CBaseMenu /
        CScreenElementManager fields.
      - `MenuTakesPadEvents` with the menu's own test (the start of 0x004A8100), `ItemFocusable` with 0x004A7850's.
      - The stepping (pad events one by one) with a direct "focus this item" (the menu's `focus` event with
        child_id 0x229D3DE4 only works on an unfocused menu - a decompiled CBaseMenu could set the selection directly and
        send the unfocus/focus events itself).
      - Exact z-order (draw priority) for overlapping items instead of tree order.

      ## What to try in the game
      1. Start the game (mouse_menus on by default). The cursor should appear in the front end menus. Without touching
         the mouse, the pad/keyboard should work exactly as before (the cursor at rest does nothing).
      2. Move the cursor over main-menu items: each should become selected (with the usual focus sound/highlight). Move
         between items quickly, top to bottom and back; on a wrapping menu, from the first item to the last.
      3. Left click an item: it opens as with Enter / A. Right click: goes back as with Escape / B. Wheel: moves the
         selection up/down.
      4. An options screen with left/right values (e.g. the game options or the skater/board select): click the left or
         right end of an option row - its value should change; click the middle - as pad_choose.
      5. Menus inside scrolling lists (e.g. level select, long lists): hover items near the list's top/bottom edge;
         scrolled-out items must not be selectable.
      6. Dialog boxes (e.g. "Quit?"): hovering/clicking should only act on the dialog, not the menu behind it.
      7. The on-screen keyboard (naming a skater/park): hover keys, click to type.
      8. Start a level: the cursor must disappear. Pause (Esc/Start): the cursor appears; hover/click the pause menu
         items; unpause: the cursor disappears again.
      9. Park editor: the cursor stays hidden while editing; in its pause menu the mouse works.
      10. With `mouse_menus=0`: no cursor anywhere, mouse does nothing (as before).
      11. Read thug2-decomp.log: no RUN-TIME CHECK FAILURE / DEBUG CRT REPORT.
      Turn it off with `[features] mouse_menus=0` if anything misbehaves.

      ## Tests
      `TestMouseMapping` (the mapping through our Nx::SetLetterbox for 16:9, 4:3, 5:4 back buffers in the front end,
      16:10 / 16:9 in a level, a back buffer shown in a different-size window; widescreen front end on and off; refusing
      empty sizes), `TestMouseHitTest` (a stand-in tree of 29 elements laid out with the original's justification
      0x004A8C30: overlaps, nesting, hidden / not_focusable / block_events (own and the menu's) / alpha 0 items, a hidden
      menu, an unfocused menu over a focused one, a clipping window, uniformly and vertically scaled menus, right/bottom
      justification, item areas widened across the menu (both sides, both menu directions); plus
      ElementRect, ChildCount, IsMenu, AnyMenu, OptionSide), `TestMouseStepping` (a stand-in menu moving as 0x004A7850:
      down / up / wrap both ways / ties / skipped items / a target it can't focus / locked / bounce / no selection / out of
      range / destroyed mid-way (both directions) / a menu that wraps when told it doesn't, events recorded).

      Mutations (`tools/mutate.py`, 40 deliberate bugs in mouse_menus.cpp/.h: mapping, rectangles, every hit-test
      condition, AnyMenu, ChildCount, every stepping branch, OptionSide): all caught - 33 at once, 7 after strengthening
      the tests (non-proportional window, vertical-only scale, block_events on the menu, widened areas on the left and
      below, destroyed menu going round, unexpected wrap). One more (dropping a `next == current` check) was equivalent
      - `moved <= 0` covers it - so the check was removed.
      Checks: thug2_check 0 MISMATCHED, all 150 early self-tests pass; call audit clean; check_code_writes no overlaps;
      register scan only the known 0x4EFF92 false alarm. No REPLACE registrations added (nothing of the game rewritten).
- [x] Batch 91: Main-menu backdrop ([features] menu_backdrop: 0 skateshop, 1 view 1 (default), 2 view 2) - NOT run in the game yet. Record:
      At the user's request: the front end shows a place in Los Angeles (level 15) instead of the skate shop, the camera
      held at a view the user picked with /where. Setting: thug2-decomp.ini `[features] menu_backdrop = 0` (the original
      skate shop, nothing changes), `1` (view 1, the default), `2` (view 2). The feature's code is clearly marked "NOT
      ORIGINAL - the menu_backdrop feature" (src/game/front/menu_backdrop.*); the game functions it needed are rewritten
      (Gfx::Camera, LoadScene) with the feature's calls marked "Not original".

      ## What level 15 is
      `LevelNum_LA = 15` (scripts/game/Levels.qb): **Los Angeles**, structure `level_la`, load script `load_la`
      (`load_level level_la`), scene `"LA"`, sky `"LA_Sky"`, collision LAcol, nodes levels\LA\LA.qb, scripts LA_scripts.qb,
      startup LA_Startup, goals LA_goals, setup LA_setup, peds LAped.pre, ambient track la_bg.

      ## How the front end gets its level and camera (findings)
      - The front end is the level `level_mainmenu` (`load_mainmenu`; start-up and every return go through
        CheckForInvalidLevels, which turns load_skateshop / load_cas into load_mainmenu). Its `level_number` is
        `LevelNum_Skateshop` = 0: load_level calls `CareerStartLevel level = <level_number>` (0x5AE990), which sets the
        career's level byte (skate module 0x7CE478 -> +0x20 -> +0x630) that Nx::CurrentLevel() reads - so "level 0" means
        "the front end" because the front end's structure says so, independent of which scene is loaded. Its scene is
        "mainmenu" (mainmenuscn / mainmenucol), its nodes mainmenu.qb (the skater's restart trg_playerrestart, lights,
        LevelGeometry / LevelObject sectors of the shop, the UI_bg / options_clouds ghosts), its scripts
        mainmenu_scripts.qb + 12 more QBs (the menus), `frontend_level = 1`, startup `mainmenu_startup`. Dozens of
        scripts test `LevelIs load_mainmenu`, so the front end has to stay that level.
      - Loading another level as the front end isn't workable (the menus live in the mainmenu QBs, the skater spins at a
        mainmenu node, scripts test LevelIs load_mainmenu). Replacing only the scene name would leave mainmenu.qb's
        LevelGeometry / LevelObject nodes pointing at sectors that don't exist. So the backdrop is **added**: the
        mainmenu level loads exactly as before, then LA's sky and scene are loaded as extra scenes.
      - Scenes: `LoadScene` (0x445130) -> Nx::CEngine::sLoadScene 0x48CCB0(name, texdict, add_super_sectors, is_sky,
        is_dictionary, is_net); a scene registers itself (0x48C560) in the scene list (head 0x6FA7E0, first node
        0x6FA7EC; node +4 = the CScene, +0xC = next) with **its name's checksum at CScene +0x84** and its texture
        dictionary at +0x88. UnloadAllLevelGeometry (0x48C810, from the level cleanup) unloads every scene in that list,
        so the extra ones go with the front end.
      - The front end's camera: `skatercam0` (a composite object with CCameraComponent, vtable 0x645E38) plays skater
        camera animations from the skate module's movie manager (+0x368; `PlaySkaterCamAnim` 0x5AD830 ->
        0x57AB60; the playing one's name: 0x57B030 thiscall). The main menu uses `mainmenu_camera02` (targeting the
        spinning skater), the menus without the skater `play_no_skater_cam` = `mainmenu_camera03`, the network menu
        `SS_MenuCam`, the level select `level_select_cam` (skater hidden); Create-A-Skater uses virtual cameras about the
        skater's bones (name 0), the skater select `SS_SkaterChoosing` / skater_select_cam*, Create-A-Graphic `CAGR_cam1`.
        CCameraComponent::Update (0x41B810 / 0x41B8A0) copies the object's position and matrix into its Gfx::Camera with
        the camera's setters SetPos 0x485A70 / SetMatrix 0x485A90 - the only callers of those besides a debug camera at
        0x4A1910-0x4A22A7. The viewport's camera: 0x4A09C0(0). The renderer (RenderViewport) and Element3d read it back.
      - Gfx::Camera (vtable 0x6487E0, derived from Obj::CObject, 0xC4 bytes): +0x4C old position, +0x5C position, +0x6C
        matrix (row 2 backward, row 1 up), +0xAC horizontal angle in degrees at 4:3 (the cameras' default is
        Nx::GetScreenAngle = camera_fov), +0xB0 the angle as drawn (widened by GetScreenAngleFactor), +0xB4, +0xB8 12
        and +0xBC 12000 (near / far by their values), +0xC0 1.0. The captured "fov 88.180" is +0xB0 for a 72-degree
        camera on a 16:9 window (2 atan(tan 36 x 4/3) = 88.18), so the view uses the default angle.

      ## What was changed
      - **Gfx::Camera** (src/game/gfx/camera.*, rewritten): constructor 0x485BA0, destructor body 0x4859F0, scalar
        deleting destructor 0x485A20, GetPos 0x485A00 / GetMatrix 0x485A10 (source only, 4 bytes), StoreOldPos 0x485A50,
        SetPos 0x485A70, SetMatrix 0x485A90, UpdateAdjustedHFOV 0x485AA0 (x87), GetAdjustedHFOV 0x485B10, SetHFOV
        0x485B20, CallMemberFunction 0x485B40. SetPos / SetMatrix / SetHFOV end with the feature's HoldCamera.
      - **LoadScene** (src/game/gfx/scene.*, rewritten, 0x445130): as the original (including its never-used description
        structure); at the end the feature's OnSceneLoaded.
      - **render.cpp** (RenderViewport): the scene struct's name field (+0x84); the three scene loops skip
        `Front::MenuBackdrop::HideScene(name)`; HoldCamera on the viewport's camera before it's read (in case nothing
        set it this frame, e.g. while objects are paused).
      - **menu_backdrop** (src/game/front/menu_backdrop.*, the feature): after "mainmenu" (any case) is loaded with the
        setting on, LoadScene is called for "LA_Sky" then "LA" (is_net passed on). "Holding" = setting on, career level
        0, the "LA" scene in the scene list, and the playing skater camera animation is mainmenu_camera,
        mainmenu_camera02, mainmenu_camera03, SS_MenuCam or level_select_cam. While holding, the viewport-0 camera gets
        the view (position w 1; matrix row 2 = back, row 1 = up, row 0 = up x back, normalised; row 3 kept; the
        default field of view) whenever its setters run and before each render, and the "mainmenu" scene isn't drawn.
        In the front end with the backdrop loaded but another camera (CAS, skater select, CAG...), "LA" and "LA_Sky"
        aren't drawn and the camera isn't touched: those screens look as before, in the skate shop.

      ## What of level 15 runs / is suppressed
      Only its geometry and textures: no LA.qb nodes (so no peds, cars, goals, lights, trigger scripts, restarts),
      no LA_scripts / startup / setup / goals scripts, no collision, no level sounds or ambient track, no TOD (the
      front end turns it off), the front end's own music. Nothing had to be suppressed - none of it is loaded.

      ## Tests (src/hook/selftests_menulvl.cpp, all Early; offline all 152 pass, 0 MISMATCHED)
      - TestCameraMethods: original vs ours, 12 seeds, the setting on but not in play: constructor (distance clipping on /
        off, negative / huge settings), getters, SetPos / StoreOldPos / SetMatrix (its result) / SetHFOV, the x87
        UpdateAdjustedHFOV over 6 angles and 3 divisors and factors (bit-exact), GetAdjustedHFOV, the member function
        (radians, missing - the params pointer's bits -, another name), destructor body, scalar delete 0 / 1; every
        callee stood in (CObject, Mem::Manager::Free, GetFloat, the angle factor / default angle as float stand-ins), the
        camera compared word by word after each step.
      - TestLoadScene: original vs ours on a model CStruct (7 names incl. sky ones in odd case x 16 flag combinations x
        setting 0/1/2, the front end's name only with 0), every call recorded (flags, the description structure, the
        CRC, the texture path, the engine load's arguments); with the setting on and "MainMenu": the original's calls,
        then LA_Sky (levels\LA_Sky\LA_Sky.tex, sky, no super sectors) and LA (levels\LA\LA.tex, super sectors),
        is_net passed on, every structure freed.
      - TestMenuBackdropViews: worked-out matrices (view 1 row 0 = (-0.777805, 0.000184, -0.628506), row 1 = up, row 2 =
        back; view 2 row 0 = (0.719350, -0.000148, 0.694647)), determinant +1, normalisation of unnormalised input.
      - TestMenuBackdropDecisions: the scene name checksums equal Crc("LA" / "LA_Sky" / "mainmenu"), the menu cameras by
        name (and the skater / CAS / CAG ones not), the hold / hide truth tables.
      - TestMenuBackdropState: on stand-in game state (skate module, career level byte, scene list, movie manager,
        viewport camera): Holding / HideScene / the setters holding the camera over setting x level 0/15 x backdrop
        loaded or not x camera animation x viewport or other camera; the field of view only updated when it differs.
      - Mutations: 54 + 28 deliberate bugs; all caught except `upd_halfangle_double` (computing hFov x pi/360 as a float
        multiply instead of x87 - equivalent: the product of two floats is exact in 53 bits, rounded once either way);
        3 caught after strengthening (destructor's vtable, the divisor, SetHFOV's update). One unrelated flake seen once:
        "net app: queued and streamed messages" failed in one mutation run and passed in 3 clean runs after.
      - Register scan clean (Construct: 4 EAX readers, returns this), call audit no mismatch, check_code_writes no
        overlaps, find_gaps: nothing skipped around 0x4859F0-0x485BA0.

      ## What to check in the game (menu_backdrop=1, then 2, then 0)
      - thug2-decomp.log: "menu_backdrop: loading the scenes LA_Sky and LA behind the front end (view 1)" each time the
        front end loads; no crash / "pool" dump (memory: the front end now also holds LA's ~5.5 MB of scene and textures).
      - Main menu: behind the spin menu, Los Angeles from the captured view (view 1 looks along (-0.62, -0.18, 0.77), view 2
        along (0.68, -0.18, -0.71)), with LA's sky; no skate shop. The spinning skater is NOT visible (it stays at the shop's
        node; the camera no longer follows it) - that's the design of a held view. Check the field of view looks like it
        did in the level (the view uses the default 72 degrees, widened for the window as in a level).
      - Options, network (ACTIONS, LAN/Internet select), memory card, level select: also LA (their backdrop panels and 3D
        signs / level icons in front of it as before).
      - Create-A-Skater, the pro skater select, Create-A-Graphic, face mapping: the skate shop as before (LA not drawn).
        Going back to the main menu returns to LA.
      - Starting a level (incl. Los Angeles itself), going back to the main menu, net games: levels look as before;
        the front end loads LA again each time.
      - menu_backdrop=0: the original skate shop everywhere, nothing loaded extra (no log line).

      ## What might go wrong
      - LA's geometry is shown with **every sector** on: sectors its node array would hide or create later (alternate /
        broken states, goal objects) may show, and moving objects (LevelObject sectors) sit at their rest positions; no
        peds, cars, lights or particles.
      - A front-end screen using another camera animation than the five listed shows the skate shop (safe), and the main
        menu's first ~10 frames after loading (before play_no_skater_cam) too. Screens whose camera isn't one of those
        but should be LA: add its name to kMenuCameras.
      - If something writes the camera other than through SetPos / SetMatrix (inlined code), Element3d models could be
        placed for the shop camera in that frame; the renderer re-holds the camera before drawing, so the picture is
        right.
      - The skater (and the shop's ghost objects near the origin) could appear tiny in the distance in view 2 if LA's
        geometry doesn't hide the origin (~6,300 units away).
      - Memory: the front end's heap now also holds LA (out-of-memory would show as the pool dump crash at 0x448386).
      - Disable: `[features] menu_backdrop=0`; the rewritten functions: `[replacements] disable=camera.cpp` /
        `scene.cpp` (the feature's hooks live in them and in render.cpp).
- [x] Batch 92: mouse menus fixes (the rotating main menu, theme menus' item areas, multi-frame selection, [features] debug_mouse) - NOT run in the game yet. Record:
      - [x] Batch 92: Mouse menus, stage 1b - the main menu's spin menu, item areas from what is drawn, selections that take
            several frames, `[features] debug_mouse` - NOT run in the game yet. Record:
            Follows batch 90 (`src/game/front/mouse_menus.{h,cpp}`, tests `src/hook/selftests_mouse.cpp`). The user reported
            that left click didn't choose on the main menu (the rotating menu) nor on the network lobby's ACTIONS box (Host
            Game / Join Game / Refresh next to the GAMES list). Read from the scripts (qb_scripts.prx / mainmenu_scripts.prx
            unpacked; mainmenu_scripts.qb, mainmenu_net.qb, themes.qb) and the menu code (0x004A8100 CBaseMenu's event handler,
            0x004A7B80 its layout, 0x004A76F0 its SetProperties, 0x004A9D30 the layout pass).

            ## Why the main menu didn't click
            - It isn't element type 0x118F6383 (built by 0x004AED90 in CreateScreenElement 0x004ACB70; no script creates that
              type - the scripts use VMenu, hmenu, ContainerElement, SpriteElement, TextElement, textBlockElement,
              WindowElement, vscrollingmenu, HScrollingMenu, Element3D). The main menu is `make_spin_menu`: an ordinary
              **VMenu** `main_vmenu` (alias current_menu), **1 x 1** (`dims = (1, 1)`, `padding_scale = 0`: every item at the
              same point, 0x004A7B80 sizes it from its children), at `spin_menu_pos` (46, 214). Its items
              (`make_spin_menu_item`) are 1 x 1 ContainerElements, each turned by `rot_angle = spin_menu_angles[i]` (0, 22.5,
              ... 337.0 degrees), holding a TextElement at (0, 0) `just [left top]` ("         Story Mode" - leading spaces
              put the words out on the wheel). pad_up / pad_down run `spin_the_menu`, which turns the whole vmenu
              (`DoScreenElementMorph rot_angle = spin_menu_angle`, +-22.5 per step) and the 3D wheel. 8 items are listed twice
              (16 positions, "copy" ids) with `blank` filler items (tag `blank`; their focus script steps on) and disabled
              ones (`active = 0`, also stepped over).
            - So stage 1's item rectangle (the item's own +0x134 + dims * scale) was a 1 x 1 dot at the wheel's centre: the
              visible words weren't part of any item; rotation was ignored too.
            - And the selection only moves one step per 0.15 s: after each step `spin_menu_input_timer` sets
              `disable_pad_handling` (0x004A76F0: clears the menu's +0x1A0, "movement allowed") for spin_menu_spin_time,
              so stage 1's same-frame stepping stopped after one step and the click's "is it selected now" test failed.
            - The front item's own text is faded to alpha 0 (`do_roundbar_highlight`) under the separate "roundbar" label.

            ## Why the network lobby's ACTIONS box didn't click
            - `make_server_list_menu` (mainmenu_net.qb): `actions_menu` is a plain VMenu; its items come from
              `theme_menu_add_item ... no_bg centered`: each item is a ContainerElement `dims = (200, 20)` whose TextElement is
              at `text_pos = (85, -5)` with `just [center center]` (and the highlight bar at (90, -7), centred): the text's
              centre is 5 pixels **above the top** of its own container, so ~3/4 of the visible label lies outside the item's
              rectangle - over the item above it. Stage 1 hit-tested the container only: clicking the first item's label
              ("Host Game") mostly hit nothing (nothing sent), and clicking a later label mostly selected / chose the item
              above it. Every theme_menu_add_item menu has the same offset (text_pos (95, -5) by default).
            - Ruled out: the menu takes pad events normally (focus by `FireEvent focus target = actions_menu`, no pad-event
              priorities), pad_choose is handled by the focused item's own handlers (the menu's handler 0x004A8100 has no
              pad_choose case), no clipping window above it, and Host Game is `not_focusable` only online without
              CanHostGame (then the pad can't choose it either). The lobby's `maybe_launch_lobby_keyboard` (online) only
              reacts to typed text.

            ## Fixes (general)
            - **Item areas from what is drawn** (`ItemContains`): an item's area is itself plus every descendant that isn't
              hidden (whatever its alpha - so the faded front spin item and invisible highlight bars count; parts with no
              area count only if they are the item itself). The unturned parts make one rectangle, widened to the menu's
              width / height as before unless the menu is turned. Overlaps are settled as before (later in the tree wins), which
              gives each theme item exactly its visible label's band.
            - **Rotation** (`ElementBox`, `BoxContains`): an element's contents lie along its absolute angle (+0x14C,
              radians: 0x004A9D30 adds the parent's +0x14C to the local +0xB4 and turns each child's scaled local top left by
              the parent's angle) from its top left. Turned parts are hit-tested as turned boxes.
            - **Selections that take several frames** (`StartTarget` / `AdvanceTarget`, the `Pending` state in Update): a
              hover or click aims at the item; each frame (`Update`, before new input) the selection is stepped again until it
              is there, then a click's event (pad_choose, or pad_left / pad_right on an option's ends) is sent. Gives up after
              300 frames, after 45 frames in a row without the selection changing, or when the menu / item is gone or moved;
              sends nothing on frames the menu doesn't take pad events or sits on an item it passes over by itself (blank /
              active = 0 - its focus script moves on). A right click or the wheel cancels it.
            - **Spin (turned) menus** (`IsRotatingMenu`: the menu or one of its items turned): hovering does **not** move the
              selection - the wheel would turn the hovered item away from under the cursor and the next small movement would
              select another, chasing round. A left click on a visible item turns the wheel to it (one step per 0.15 s, as
              holding the pad) and then chooses it; clicking the front item (its label - the faded text under the roundbar)
              chooses it at once. The wheel turns it like pad_up / pad_down. Items tagged `blank` or `active = 0`
              (`ItemPassedOver`) are never targets.
            - **`[features] debug_mouse=1`** (default 0): on each left click the log gets the cursor in layout coordinates, the
              item found (id, type, rect, angle, alpha, flags, index, its whole area) and its menu (id, type, selected index of
              count, focus, takes pad events, movement allowed +0x1A0, wraps, turned) - or, when no item is found, every
              element under the cursor (by its own box) with the nearest menu above it; then each event sent, or "NOT sent"
              with the reason (pad mapping inactive / menu input blocked / no device), "on its way" for a selection still
              moving and its outcome (selected / given up). Right clicks are logged too.
            - mouse_menus=0 unchanged (none of this runs).

            ## Tests
            `TestMouseItemShapes`: a stand-in theme menu laid out like the ACTIONS box (labels above their containers: each
            label's band selects its own item, across the menu's width; an alpha-0 sprite counts; a hidden sprite and its
            child, and an empty container far away, don't), a stand-in spin menu (1 x 1 vmenu turned -22.5 degrees, six 1 x 1
            items turned 0..112.5 degrees with 150 x 20 texts; the front text at alpha 0): points along each item's label, beside
            it, past its end, and inside the turned texts' surrounding rectangles but outside the texts; the disabled and blank
            items aren't found; a turned menu isn't widened; `ItemBounds` (theme item; a turned text's corners);
            `IsRotatingMenu`, `Rotated` (whole turns, epsilon), `BoxContains` (negative sizes, a quarter turn),
            `ItemPassedOver`. `TestMouseTargets`: frame by frame against a stand-in menu that ignores events for 9 frames
            after each move (the spin menu: 3 steps in 19 frames, 21 events; the short way round a wrapping 16), one that never
            moves (gives up at frame 47), one too slow for the frame limit (gives up at frame 301 at item 34), waiting while the
            menu moves by itself (no events), the menu destroyed, no selection; `StartTarget`. The batch 90 tests still pass
            (the hit-test layout helper now turns children by the parent's angle).
            Mutations (`tools/mutate.py`, 35 deliberate bugs in mouse_menus.cpp: box maths, angle field, Rotated, item parts,
            hidden/empty parts, turned hit vs bounds, widening, passed-over tags, rotating-menu test, every target branch):
            all 35 CAUGHT - 32 at once, 3 after strengthening (a named tag holding `blank`; "already there" / "no selection"
            count no frame). `passed_blank` and `visit_passed` are caught by a crash, not a failed check: a blank item then
            reaches ItemFocusable, whose ContainsTagFlag looks the unnamed `blank` up as a global (LookUpSymbol), which crashes
            offline (no symbol table) - the reason FindItemAt tests ItemPassedOver first.
            Not covered by tests (game side, needs the game): the pending state's bookkeeping in Update / AimAt / Click, the
            debug log, ClearPending on right click / wheel.

            ## What to try in the game
            1. Main menu: hover the wheel's items - nothing should turn. Click an item below or above the front one: the
               wheel turns to it step by step (as holding down / up) and then opens it. Click the front item's label (the
               roundbar): it opens at once. Click a dim filler line between groups: nothing. Wheel: turns it.
            2. Network: Multiplayer -> LAN (or Internet) -> the lobby: hover Host Game / Join Game / Refresh - the highlight
               must follow the label under the cursor (not the one above it); click Host Game: the host options open. Try the
               other theme_menu_add_item menus (game options, pause menu...): hover / click the labels.
            3. Earlier batch 90 checks (options rows' left/right ends, scrolling lists, dialogs, keyboard) still behave.
            4. If something still doesn't click: set `[features] debug_mouse=1` in thug2-decomp.ini, click it, and read the
               "mouse:" lines in thug2-decomp.log (what was under the cursor, the menu's state, what was sent or why not).
            Turn the mouse off with `[features] mouse_menus=0`.
            Checks: thug2_check 0 MISMATCHED, all 167 early self-tests pass; check_code_writes "No overlaps"; call audit no
            MISMATCH; register scan only the known 0x004EFF92 false alarm. No game function added or replaced (our own feature).
- [x] Batch 93: front-end objects moved into the menu backdrop (the wheel of loogies, the skater) - NOT run in the game yet. Record:
      - [x] Batch (wt/feobj): menu_backdrop - the main menu's wheel and skater moved into the backdrop (NOT run in the game
            yet). Record:
            At the user's request (after testing batch 91: with the backdrop on, the spinning wheel and the skater who spins
            it were gone - they stay at the skate shop, ~6,300 units from the held view in Los Angeles). Part of the
            menu_backdrop feature, marked "NOT ORIGINAL": src/game/front/frontend_objects.{h,cpp}. With `menu_backdrop=0`
            nothing is moved and every hook returns at once.

            ## What the wheel and the skater are (mainmenu.qb / mainmenu_scripts.qb, unpacked from the .prx archives)
            - **The wheel of loogies** ("wol"): 48 **level objects** of mainmenu.qb's node array (`Class = LevelObject`,
              CreatedAtStart, SuspendDistance 0, lod_dist1 200 / lod_dist2 201): wol_inner_ring, wol_inner_star, wol_tag,
              wol_outer_ring, wol_pie_1..16, wol_lightbulb_1..28, all near the shop's origin ((0, -12.7, -7) etc.). Each is
              a composite object (gameobj components: suspend, motion, objectproximity, sound, + a model because of
              `Class = LevelObject`). The model component's InitFromStructure (0x00420D10) for a level object: 0x0041FFA0
              finds the sector of that name in **any** loaded scene (0x0048C510) and clones its geometry as an **instance**
              (geom vtable +8 with instance = 1); the model component's Update (0x004219F0 -> 0x004215F0) draws it with
              the object's display matrix and position. Instances are drawn by the renderer's instance passes (0x004D1760 ->
              CInstance::Render 0x004D1130, from SetPass), **not** by the scene loops menu_backdrop hides - so hiding the
              "mainmenu" scene doesn't hide them; they were invisible only because they are at the shop. Turned by
              `spin_menu_spin_geometry` (`Obj_Rotate absolute = (0,0,1)*angle time = ...` on each part: the motion
              component's rotation, 0x00425A80 -> 0x004258B0, multiplies the matrix by a rotation about the object's
              **own** axis - it commutes with moving the object), shown / hidden by `spin_menu_visible` (Hide / Unhide).
            - **Visibility of level objects**: the suspend component (0x00433F50; its model at +0x2C) decides every frame
              whether the model is drawn (0x004342F0: off beyond lod_dist2 (201 ft) **from the camera**, else a frustum
              test 0x0048C490) - with the camera held in LA the shop's parts were out of range too. So the objects must
              really be moved (in the logic, not only when drawn): then distance and frustum are right.
            - **The skater**: the local skater (FindObject("Skater")), placed by `SpinMenuSkaterIdle` (MakeSkaterGoto from
              spin_menu_skater_script): SkaterInit, `Obj_MoveToNode name = trg_playerrestart Orient NoReset` ((84.8, -63.7,
              61.7)), SetRollingFriction 10000, SetSpeed 0, **PausePhysics**, then animations only (FrontEnd_Wheel_Idle /
              Spin / Spin2Wheel...). PausePhysics is the skater physics control component's (type skaterphysicscontrol
              0xD9151D4D, vtable 0x0064BFE0; member functions 0x00517A80 / 0x00517AA0, table 0x006846F0): its byte +0x18
              = paused; while paused its Update (0x00517AC0) only copies a matrix into the display matrix. The options menu
              (in_options_menu, `ShadowSkaterOptionsTricks`, `ShadowSkaterFreestyleAI`) runs the skater **with physics**
              (SetSpeed 1, Jump...) on the shop's collision - it must not be moved then (LA has no collision loaded: it
              would fall).
            - **The camera**: the main menu plays `mainmenu_camera02` on skatercam0 with `targetid` = the skater
              (spin_menu_camera_init); the camera component (0x0041B840: CCameraComponent, vtable 0x00645E34, its
              Gfx::Camera at +0x18) copies its object's m_pos / m_matrix into the Gfx::Camera (0x0041B810 / 0x0041B8A0)
              - so the camera object still holds the original menu camera's pose after HoldCamera replaced the
              Gfx::Camera's.

            ## The framework (frontend_objects.*)
            - Table (kObjects): {name (with %d for numbered ones), count, kind, offset}: "Skater" (kind Skater: moved only
              while its physics is paused) and the 48 wol_ parts. Offsets (default 0) are in the backdrop view's frame
              (right, up, back) for fine-tuning.
            - Placement: the rigid transform taking the menu camera to the backdrop view: rot = C^T V (C, V the camera's
              and the view's frames: row 2 = back, row 0 = up x back, row 1 = back x row 0, orthonormalised), trans = view
              pos - camera pos * rot (+ offset). Applied to position, old position (points), velocity, orientation rows 0-2
              and display matrix rows 0-2 (directions); every w and row 3 pass through. In double, rounded once.
            - The menu camera's pose: read from the object owning the viewport camera's camera component, while nothing is
              moved; taken when the same for two logic updates in a row (or after 30 samples if it keeps moving), kept per
              camera animation (mainmenu_camera02 / 03, SS_MenuCam, level_select_cam...) until the front end / backdrop goes.
              Each menu camera thus gets its own placement (objects keep their place for whichever menu camera plays).
            - Moving = the fields written, then CCompositeObject::SetTeleported(true) (each component's Teleport: the model,
              shadow, sound and collision components' Teleport is their Update - the drawn instance follows at once).
            - When: MenuBackdrop's view set, career level 0, the backdrop's scene loaded, a menu camera playing, not held
              off, the camera pose known. Checked at the start of every object walk (CCompositeObjectManager::Update, which
              also samples the camera) and of every render (RenderWorld).
            - What the game writes is taken in: at those two points and inside each moved object's CCompositeObject::Update
              (after its script and own update; after its components). Per field: unchanged (x y z bit-equal): only w /
              row 3 taken; changed nearer to where we put it than to its real place: kept, the real place worked back
              (Obj_Rotate turning the wheel); nearer its real place: that is its new real place, moved again (+ teleport)
              (Obj_MoveToNode, a component copying its own matrix into the display matrix every frame).
            - Restored (fields written back - bit-exact unless the game changed them in the backdrop - plus teleport; then
              forgotten): when a condition fails (another camera, level != 0, backdrop unloaded, setting off), when the
              menu camera or view changes (then placed again for the new one), when a skater's physics is unpaused
              (checked before its components run), before the script C functions Cleanup, UnloadAllLevelGeometry and
              UnloadScene (hook in the interpreter's C function calls, cscript_exec.cpp), and at the start of every
              LoadScene. After a level change nothing moves until the backdrop is unloaded or loaded again
              (MenuBackdrop::OnSceneLoaded -> OnBackdropLoaded). An object no longer found under its id, found replaced
              by another object, or dead, is forgotten without being touched.
            - Hooks added ("Not original - the menu_backdrop feature"): CCompositeObject::Update (composite.cpp) x2,
              CCompositeObjectManager::Update (compositemgr.cpp), RenderWorld (render.cpp), CFunc_LoadScene (scene.cpp),
              the two C function calls of CScript's statement execution (cscript_exec.cpp), MenuBackdrop::OnSceneLoaded.
              MenuBackdrop now exports BackdropLoaded, PlayingMenuCamera, ViewportCamera.

            ## Tests (src/hook/selftests_feobj.cpp, all Early; offline all 169 pass, 0 MISMATCHED)
            - TestFrontEndPlacement: a stand-in menu camera (180, 40, 300) looking along -(0.30, 0.20, 0.93): its frame,
              the placement's rotation / translation for views 1 and 2 against values worked out independently (python):
              view 1 rot row 0 (-0.537644, -0.058288, -0.841155), trans (-1963.1665, -370.8876, 6119.1508), the restart
              node -> (-1954.9877, -439.7046, 6017.8390), wol_pie_1 -> (-1963.4713, -383.3855, 6120.2317); view 2 rot row 0
              (0.461354, -0.058554, 0.885282), trans (-1813.6369, -370.8924, 5716.2086), restart -> (-1830.7417, -439.7155,
              5816.3962); determinant 1; the camera lands on the view, 100 in front stays 100 in front, its axes become the
              view's; inverse; offsets; w passed through.
            - TestFrontEndObjectStates (stand-in composite objects, a stand-in lookup): the table (49, one skater);
              nothing while off / not level 0 / no backdrop / no menu camera; pose sampling (not from render steps, two
              equal in a row, a moving camera taken at its 31st sample, samples in a row only); moved (skater + 2 parts; a
              table id that isn't a composite object and an id not in the table untouched; every field, teleported);
              steady (no rewrite); restored bit-exactly and moved again for each trigger (another camera, level, backdrop,
              setting, LoadScene, Cleanup, UnloadAllLevelGeometry, UnloadScene; hold-off until backdrop gone / reloaded;
              poses kept or forgotten); other C functions nothing; view 2 / view 1; camera 03 with its own pose and back to
              02; game writes (a position written near the shop moved there and restored there exactly; a 22.5-degree turn
              about the object's own axis kept and restored as the original turned the same way; w-only changes); skater
              physics unpaused -> restored, not moved while unpaused, moved when paused again; a skater without the
              component never moved; destroyed / replaced / dead objects forgotten untouched (also when restoring).
            - TestFrontEndObjectHooks: CCompositeObject::Update (ours) on moved stand-ins with a recording component: a
              component's write near the shop moved before the update ends; a skater unpaused restored before its
              components run (they see the real position).
            - TestFrontEndObjectsGameState: Update() on stand-in game state (skate module + career level, scene list, the
              movie manager's playing animation 0x0057B030, the viewport camera 0x004A09C0, FindObject and the manager's
              GetFirstComponentByType stood in, the camera component -> camera object): each condition missing, sampling
              through render / logic updates, another camera, a level, view 2, the setting off, no skate module.
            - Mutations: 64 deliberate bugs (table, composite check, physics flag, classification, w / row 3 handling,
              each field written / restored, teleports, the placement's frame / rotation / translation / offset / inverse,
              the state machine's triggers, hold-off, pose sampling, the C function hook, the glue, the two hooks in
              CCompositeObject::Update): 55 caught at once, the other 9 after strengthening (only row 2 changed, a little
              move kept without teleport and worked back as a point, restore without a hook first, velocity as a
              direction, w kept when written again, a dead object still found, samples in a row across cameras, a turning
              camera). One unrelated failure seen once in a mutation run ("PC async handle (synchronous I/O)"); clean runs
              pass.
            - Register scan clean (only the known 0x004EFF92 PUSH ECX), call audit no mismatch, check_code_writes no
              overlaps. No game function rewritten (only feature calls added to rewritten ones).

            ## What to check in the game (menu_backdrop = 1, then 2, then 0)
            - thug2-decomp.log: "menu_backdrop: the front-end objects placed for camera FCC018D3 at (x, y, z)" once the main
              menu is up (FCC018D3 = mainmenu_camera02; 8BC72845 = mainmenu_camera03 for the options / network menus) -
              those numbers are the shop camera's real pose (tell me if a "(the camera still moving)" appears).
            - Main menu: the wheel and the skater in front of Los Angeles, where they were on screen in the skate shop
              (same size and place relative to the menu). Spin the menu (up / down): the wheel turns, the skater does his
              spin animations, nothing jumps or drifts. Lighting on the skater may differ (the shop's two level lights
              don't reach LA) and his shadow may be missing or odd (no ground under him in LA - no LA collision loaded).
            - Options / network menus (camera 03 / SS_MenuCam): whatever of the wheel / skater the original showed from
              those cameras appears, placed for that camera. In the options menu the skater may do tricks (physics on): he
              then stays in the shop (not moved), i.e. not visible - expected.
            - Create-A-Skater, skater select, CAG: the shop as before, the skater there (moved back before the first frame
              drawn with that camera); back to the main menu: in LA again.
            - Start any level, Los Angeles included, and go back: the skater starts at the level's restart as always, and
              the front end shows the wheel / skater in LA again.
            - menu_backdrop = 0: everything exactly as before (nothing moved; no "placed" log line).

            ## Risks
            - The menu camera's field of view: the held view uses the default angle; if a menu camera animation sets
              another, sizes differ slightly. Fine-tune with the table's offsets.
            - A component writing absolute values every frame in the backdrop's region, or a script's absolute
              orientation-only write (Obj_SetOrientation), would be taken as a change in place (no such writer known for
              these objects).
            - The front-end camera is captured from the camera object: if a menu camera's pose isn't steady (bone-
              targeted), it is taken after 30 samples where it is then.
            - Level-change paths that don't go through the script C functions (a net client told by the server: 0x00565A00
              calls Cleanup directly) restore at the next LoadScene instead (the wheel parts die with the cleanup; the
              skater is put back before the next level's restart).
            - Particles / sounds of the shop (ATM sparks...) are not moved.
- [x] Mouse menus (batches 90 + 92) tested in the game (2026-10-17, by the user): hover / click / right click / wheel
      work, including the rotating main menu, the network menu and the in-level pause menu (the pause-menu cursor, missing
      in the first test, works with batch 92's build).
- [x] Batch 94: front-end objects fix (in the game batch 93 never placed anything: no 'placed' line after start-up) -
      camera sampled every frame, fallback pose from the camera setters, [features] debug_backdrop. NOT run in the game yet. Record:
      - [x] Batch (wt/feobj2): menu_backdrop front-end objects - fix for "LA shows, but no wheel and no skater" (batch 93),
            and [features] debug_backdrop (NOT run in the game yet). Record:

            ## What the game's log showed
            The backdrop loaded (one "loading the scenes LA_Sky and LA" line after start-up) but no "the front-end objects
            placed for camera ..." line: the menu camera's pose was never captured, so nothing was ever moved. (The user saw
            LA, so the held view itself - setting, level 0, LA loaded, a menu camera playing - was on in the renderer.)

            ## The cause (from reading the code paths; not proven in the game - debug_backdrop will show it)
            - **Not the hold-off.** The suspected nested LoadScene sequence is fine: load_level's LoadScene "mainmenu" (its
              start: OnLevelChange -> held off) -> MenuBackdrop::OnSceneLoaded -> LoadScene "LA_Sky", LoadScene "LA" (each
              held off again) -> then, after both, OnBackdropLoaded (no longer held off). level_mainmenu has no sky and no
              outer shell, so no LoadScene follows. A new test runs exactly this (the real CFunc_LoadScene, its callees
              stood in) and it passes on the old code too.
            - **The capture depended on two things the game may not give:**
              1. Samples were taken **only by the composite object manager's update walk** (Update(true)); the renderer's
                 step never sampled. Anything that keeps the walk's step from sampling while the menu camera plays (its
                 task not running in the front end, or running before the camera's animation starts every frame...)
                 means no capture, ever.
              2. The pose came **only from the camera object** owning the viewport camera's camera component (found
                 through the composite manager's per-type component list). If the viewport camera isn't owned by such a
                 component while a skater camera animation plays, there was no sample and the sample count was reset on
                 every step - no capture, not even the 30-sample fallback, and no log line. This matches the log.
            ## The fix
            - The camera is sampled **once a frame by whichever step comes first** - the object walk's or the render's
              (frames counted by the renders; Step now returns whether it took a sample).
            - When no camera object is found, the pose comes from **what the game gave the viewport camera's setters**:
              Gfx::Camera::SetPos / SetMatrix (camera.cpp) now call MenuBackdrop::NoteCameraPos / NoteCameraMatrix before
              HoldCamera, which keep the position and matrix given to the viewport camera while it is held (before the view
              replaces them). Both must have been noted; forgotten when the backdrop is loaded again.
            - [features] **debug_backdrop** = 1 (default 0; FrontEndObjects::DebugBackdrop()): logs every change of the
              decision's inputs ("menu_backdrop debug: setting 1, level 0 1, LA loaded 1, menu camera FCC018D3, held off 0,
              applied 0, pose known 0 (0 known), camera from its object | the setters | nowhere, N sample(s) in a row,
              sampling | non-sampling step, N moved"), each hold-off / backdrop reload, "applying for camera X", each object
              moved (id, from, to), restored (physics unpaused) or forgotten, and every restore with its reason (setting off,
              not the front end, backdrop not loaded, held off, not a menu camera, another menu camera, another view, camera
              pose forgotten, level change). CLAUDE.md lists the setting.

            ## Tests (src/hook/selftests_feobj.cpp; offline all 172 pass, 0 MISMATCHED)
            - New TestFrontEndObjectsLoadSequence: the front end's load as the game does it - CFunc_LoadScene("mainmenu")
              (ours; structures, texture dictionary and scene file stood in, each loaded scene put in a stand-in scene list),
              which loads LA_Sky and LA through LoadScene - then frames with mainmenu_camera02: run once with walks and
              renders, once with **renders only** (debug_backdrop on). The renders-only run **fails on the old code** (no
              sample was ever taken) and passes now; the scenes loaded are mainmenu, LA_Sky, LA, and it isn't held off.
            - TestFrontEndObjectsGameState extended: sampling once a frame (two walk steps in a frame count once; the
              render's step samples too), renders only, and the setters' pose (no camera component: nothing until both
              position and matrix are noted, a position alone isn't a pose, then moved with that pose; another camera than
              the viewport's, or not held: not noted; forgotten when the backdrop loads). These **fail on the old code**.
            - Mutations: 15 for the new code - all caught (one after strengthening: a position alone isn't a pose; one
              equivalent: `input_pose_matrix_only` - Update always sets both pointers or neither; `note_matrix_not_held`
              shows as a crash in the camera tests, a caught difference).
            - Register scan clean (only the known 0x004EFF92), call audit no mismatch, check_code_writes no overlaps.

            ## What to check in the game
            - With the defaults: the main menu shows the wheel and the skater in LA; thug2-decomp.log has "menu_backdrop:
              the front-end objects placed for camera FCC018D3 at (...)".
            - If not: set `[features] debug_backdrop=1`, start the game to the main menu, quit, and send the "menu_backdrop
              debug" lines: they show which input stays wrong (e.g. "camera from nowhere" = neither source gives the
              camera's pose; "menu camera 00000000" in the sampling step = no menu camera seen; "held off 1").
- [x] Front-end objects, second fix (2026-10-17; NOT run in the game yet): the user's debug_backdrop=1 log showed every
      condition true but 'camera from nowhere' - the camera component doesn't set the viewport camera through SetPos /
      SetMatrix in the game, so neither source gave the menu camera's pose. Now the renderer's hold point
      (MenuBackdrop::NoteAndHoldCamera, render.cpp) notes the camera's pose whenever it isn't at the held view (the game
      moved it this frame) before holding it. Not covered by an offline test (no camera component offline) - checked
      in the game.
- [x] Batch 95: front-end objects - the menu camera's field of view (placement depth-scaled by tan(menu/2)/tan(view/2)),
      objects at their real place during their own update (fixes the spin scattering the wheel), 'Original behaviour:'
      notes at every backdrop hook. NOT run in the game yet. Record:
      - [x] Batch (wt/feobj3): menu_backdrop front-end objects - the wheel group offset/shrunk (field of view) and the
            wheel scattering on the first spin; "Original behaviour:" comments at every hook (NOT run in the game yet).
            Record:

            ## 1. The group offset towards the screen centre and drawn smaller - the field of view
            - The placement kept each object's camera-space position, but the held LA view is drawn with the cameras'
              default angle (Nx::GetScreenAngle, 72 degrees at 4:3) while the menu camera used its own. From 10.png: the
              wheel centre lands at x 610 where the menu's text ring is centred at x 430 (2D layout 4:3 centred in 1920:
              240..1680). With the logged camera (59.8, -12.7, 181.7) and the wheel at (0, -12.75, -7) the wheel is ~17.6
              degrees off the view axis: at 72 degrees that is x ~ 645 (as seen), at the ring's 430 the menu camera's angle
              works out at ~47 degrees. So the menu camera is narrower; the same pose mapping then puts everything closer to
              the centre and smaller.
            - Where the angle comes from: the game gives the viewport camera its angle through Gfx::Camera::SetHFOV
              (callers 0x004A0AD0, 0x00467BA0, the skater camera 0x004F9AE0, the camera-animation code 0x00587A50 /
              0x00587F70 - PlaySkaterCamAnim's `fov` parameter, else a script float) or by writing it directly; HoldCamera
              then puts the default back. Now noted: MenuBackdrop::NoteCameraFov from SetHFOV (before HoldCamera), and at
              the renderer's hold point (NoteAndHoldCamera) whenever the camera's angle isn't the held view's; kept with the
              menu camera playing then; GameCameraFov(menuCamera) returns it (0: none noted). Forgotten with the pose.
            - Fix (keeps the user's LA framing - the held view's angle is unchanged): the placement scales the camera-space
              **depth** by k = tan(menuFov/2) / tan(viewFov/2) (FrontEndObjects::DepthScale): what the menu camera saw at
              (right x, up y, d ahead) goes to (x, y, k d) in front of the held view. On screen x / (k d tan(view/2)) =
              x / (d tan(menu/2)): the same place; an object of width w (not resized) at k d appears w / (d tan(menu/2)): the
              same size. Orientations only turn (rot); points use point = C^T diag(1, 1, k) V, back through its inverse. The
              menu camera's pose now carries both angles (equal twice in a row to be taken), and the log line says
              "field of view F (the view's V: depth x k)".
            - Worked: DepthScale(50, 72) = 0.641817, DepthScale(46.6, 72) = 0.592764; test points (10, 5, 100 ahead) and
              (-30, 12, 250 ahead) of the stand-in menu camera land at the same right / up and 0.641817 x the depth in front
              of views 1 and 2, with equal screen ratios (1e-4), and come back through the inverse.

            ## 2. The first spin scattering the wheel - rotations at the moved place
            - Obj_Rotate (spin_menu_spin_geometry) only sets the motion component's target; the turning happens inside each
              part's update. The previous code let the update run on the moved pose and then tried to work out the real pose
              from what changed ("changed near where we put it: kept, real pose worked back"). That is only right if every
              change commutes with the placement (a turn about the object's own axis); the scattering (pies seen edge on,
              bulbs thrown along an arc) shows the parts' turns didn't - turned about the wrong axis.
            - Fix: **each moved object is at its real place for its own update**. CCompositeObject::Update (composite.cpp)
              calls FrontEndObjects::BeforeUpdate once it isn't paused: what was done to the object since (outside its
              update - a spawned script's Obj_MoveToNode...) is taken in, then its real pose is written back (no teleport);
              its script, own update and components (motion's turning, Obj_MoveToNode, physics) run as in the original;
              AfterComponents takes wherever it is then as its real place and moves it into LA again (teleported: the model,
              shadow and collision follow). Nested updates (Obj_ForceUpdate) count; an update that ended early (the object
              died in its script) is noticed at the next step and the object moved from its real place. The suspend
              component's visibility pass (0x004342F0, from a walk over the suspend components and from 0x00564D30) runs
              outside the object updates, so it still sees the parts near the held camera. A skater whose physics is
              unpaused during its update is simply left at its real place.
            - The old "what changed" reconciling still handles writes made outside an object's update.

            ## 3. "Original behaviour:" comments
            At every menu_backdrop hook in rewritten game functions (render.cpp: Hidden, NoteAndHoldCamera, RenderWorld's
            Update; composite.cpp: BeforeUpdate / AfterComponents; compositemgr.cpp: Update; camera.cpp: SetPos / SetMatrix /
            SetHFOV; scene.cpp: LoadScene's OnLevelChange / OnSceneLoaded; cscript_exec.cpp: the two C function calls), a
            one-line "Original behaviour: ..." next to the "Not original" marker.

            ## Tests (src/hook/selftests_feobj.cpp; offline all 172 pass, 0 MISMATCHED)
            - TestFrontEndPlacement: + the depth-scaled placement (worked values above, both views, inverse, directions
              unchanged).
            - TestFrontEndObjectHooks: components now see the real place; a part turned during its update by 22.5 degrees
              twice **about its own z (Rz * m, the motion component's) and about the world's z (m * Rz)** ends up at the
              placement of the original turned the same way (matrix and display matrix, 2e-6) and is restored to it - the
              world-axis case fails with the old code (the hook removed: caught); nested updates; an update ended early; a
              position written from outside the update is where the next update starts.
            - TestFrontEndObjectsGameState: + the field of view: SetHFOV(50) while held noted (and the camera held at 72),
              another menu camera gets 0, the placement uses DepthScale(50, 72); written directly and noted at the hold
              point; kept when the camera is at the view's angle; forgotten; not noted when not held.
            - TestFrontEndObjectStates adjusted (outside-update checks through AfterComponents; the unpaused skater left at
              its real place by its update).
            - Mutations: 27 for the new code - all caught (2 after strengthening: the writes since taken in before the real
              place is written, the early-ended update); 1 equivalent: `samepose_no_fov` (the pose taken is the latest
              sample's, angle included). `fov_note_not_held` shows as a crash in the camera tests (a caught difference).
            - Register scan clean (only the known 0x004EFF92), call audit no mismatch, check_code_writes no overlaps.

            ## What the user should check (menu_backdrop = 1, debug_backdrop = 1 helps)
            - thug2-decomp.log: "the front-end objects placed for camera FCC018D3 at (...), field of view F (the view's 72:
              depth x k)". F should be the menu camera's angle (expected ~45-50). If it says 0.000 (not noted) or 72, the
              angle isn't the cause / isn't reached - send the line.
            - Main menu: the wheel centred in the menu's rotating text ring, the same size as in the shop's main menu, the
              skater's hand on its right edge.
            - Spin the menu up / down several times: the wheel turns as one piece (pies stay in the wheel's plane, bulbs on
              the rim), the skater plays his spin animations; no parts flying apart.
            - Options / network menus and back: the group re-placed for those cameras; CAS / skater select: the shop as
              before.
- [x] Batch 96: menu_level - the front end as a level of its own in Los Angeles (`menu_backdrop = 3`; NOT run in the
      game yet). Record:
      At the user's request ("making our own level"), with NeverScript (tools/neverscript, violetvandal's THUG2 fork:
      454/470 of the game's .qb round-trip byte-identical). Marked "NOT ORIGINAL": src/game/front/menu_level.{h,cpp},
      tools/menu_level/make_menu_level.py; hooks with "Original behaviour:" notes in pip.cpp (Load, LoadFileInto),
      file.cpp (Open, Exist) and render.cpp (Hidden, RenderViewport).

      ## What a level is (Levels.qb, unpacked from qb_scripts.prx)
      - A structure (level_Mainmenu) read by the script `load_level`: scnpre / level (LoadScene <level>), sky, colpre
        (LoadCollision <level>), pre (mainmenu_scripts.pre: scripts and the .ska camera animations), qb (the node
        array: NodeArrayManLoad; its array is named after the file: mainmenu.qb -> mainmenu_NodeArray), level_qb /
        more_level_qbs, level_number, frontend_level, lighting. All script-driven.
      - **The wheel's meshes are in the shop's scene** (wol_* sector checksums in mainmenu.scn / mainmenu_net.scn /
        mainmenu.col, not in LA.scn). Level objects find their sector in any loaded scene and draw it as an instance
        (batch 93's notes), so the shop's scene is loaded as an extra scene and only not drawn.
      - Files: Levels.qb is in qb_scripts.prx, mainmenu.qb in mainmenu_scripts.prx - archives win over the data folder,
        so the overlay is checked before archives (Pip::Load / LoadFileInto skip FindPreEntry, the pre manager and the
        cache for an overlay file; File::Open opens the overlay's file - its handle isn't an archive's, so Read / Seek /
        Close fall through to the disk's (GetContainedFile null -> not handled)).
      - The menu cameras: mainmenu_camera02 at (180, 40, 300) in the shop (batch 95's log: the menu camera's pose),
        aimed at the skater + mainmneu_cam_offset (-25, 51, -40); SS_MenuCam (173, 40, 300) and mainmenu_camera03
        (-40, 20, 150) play without a target - the animations are in the shop's coordinates.

      ## How
      - make_menu_level.py (needs out/neverscript/ns.exe) unpacks the two files from the archives (LZSS as
        Lzss::Decode), decompiles, edits, compiles into the overlay (default <game>\thug2-decomp-data):
        level_Mainmenu -> scnpre LAscn.pre, level "LA", colpre LAcol.pre, + sky "LA_Sky", extra_scene "mainmenu",
        menu_level_yaw / menu_level_offset (level_name stays "mainmenu"); load_level loads `<extra_scene>` after the
        level's scene (non-net, non-park-editor branch). mainmenu.qb: Pos / MidPosition / EndPosition moved, Angles
        turned (exactly: Rz*Ry*Rx * RotateY(yaw) split back; plain yaw just adds) - 111 points.
      - The move: p * RotateY(yaw) + offset, taking the shop camera (180, 40, 300) to the backdrop view's place
        (menu_backdrop.cpp kViews, read from the C++) and the camera's heading (to the target) to the view's: view 1:
        turn 108.679 degrees, shift (-2034.805, -382.621, 6070.784). The wheel lands 359 units ahead of the view, the
        skater's restart (TRG_Playerrestart) at (-2003.5, -446.4, 5970.7). `--lift` raises everything (ground fit).
      - In the game: MenuLevel::Active = menu_backdrop 3 and the overlay folder exists (logged once). The renderer gives
        the viewport camera the move while the level is the front end and the camera is within 3000 units of the shop's
        origin (LA's front end is ~6,300 away, so a moved camera isn't moved again); its matrix is turned when the game
        wrote a new one (compared with the last we wrote). The "mainmenu" scene isn't drawn in the front end.
        menu_backdrop's hold and the front-end objects stay off (ViewFor(3) is null).

      ## Tests (src/hook/selftests_menulvl.cpp TestMenuLevel; offline all 173 pass)
      - Overlay names (case, '/', "data\" and development paths stripped, "metadata\" kept); the move takes the shop
        camera to view 1 (0.01); a quarter turn takes (0 0 1) to (1 0 0) (the heading convention); the shop test; off
        at settings 0-2 (nothing hidden, no overlay, the camera untouched).
      - Mutations: 6, 5 caught; `setting` (Active ignoring the setting) missed: offline there is no overlay folder, so
        Active is false at every setting anyway.

      ## Unknowns for the game run
      - Whether an instance drawn from a sector follows the object's node position (the batch-95 run showed moved
        objects drawing moved, so likely).
      - The skater's height on LA's ground (y -446; physics paused on the main menu; the options menu's shadow skater
        uses LA's collision now); `--lift`.
      - Cameras that follow the skater (CAS, skater select) - moved twice if their animation is relative; scripts that
        name shop places; the shop's lights / particles / bouncy boards now float in LA.
- [ ] Next: small net follow-ups: App::m_manager (+0x1440) could become `Manager*` (it is `void*`), name the
      Manager's +0x2E6 flag (pref checksum 0xE5ADE642) and check +0x374 (modem baud rate?).
      Widescreen follow-up: if the user still sees the bottom-left band overlap, don't draw sprites wholly outside the
      layout in the widescreen front end (they were never visible in the original's 4:3 screen). Then the gaps
      batches 79-81 left: **0x45E9E0**
      (Obj_GetCollision, the last of table 0x683268's 30 entries) with the collision feeler 0x417760 / 0x4172F0; the
      rest of the cutscene system up to 0x4680B0 (0x463AA0 quaternion + translation to matrix, 0x463BA0,
      0x463C70-0x464360, the pool pair 0x4645A0 / 0x4645E0, 0x464640-0x4680B0: loading 0x466620, the details' slots
      0 / 2 / 5 / 7 / 8, the big updates 0x4648D0 / 0x465C00); the task-list area 0x406850-0x406E00 (~25 functions,
      incl. Tsk::Task 0x406890 / 0x406C90 -> 0x406A80 and the list run 0x406AC0 the frame order rests on); 0x4AAD40
      (CGeneralManager slot 6); the rest of the screen element code 0x4A74E0-0x4A8790 and CScreenElement itself
      (batch 83's Left). The earlier gaps: the module manager Mdl::Manager 0x44F300-0x44F7E0,
      0x4B2270-0x4B2320 (9 glue functions), CFlipRotateComponent::Update 0x4E2D20 and its base 0x481710-0x481740,
      0x46C470-0x46C6A0 (a CStruct wrapper class), the sound functions 0x47AC10-0x47AD80. GameSpy's host names become
      configuration when the SDK is linked.
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
- (Historical, until 2026-10-16: PartyMod patched the exe at run time, so its patched addresses couldn't be
  REPLACEd. Now nothing patches rewritten code; the only run-time writes are the GameSpy host names and the
  IsPS2 table entry - `python -I tools/check_code_writes.py`.)
- Member functions are replaced via `REPLACE(addr, &Class::Method)`; constructors via a small adapter that
  placement-news the object (see `LineAdapter` in vector.cpp).

## PartyMod (2026-10-15)
- [x] Step 1 done, TESTED IN-GAME 2026-10-15 (game runs with PartyMod's DLL removed; log shows both PartyMod lines and all self-tests passing): PartyMod (v1.0.4) in its own folder (MIT, notice kept), built as a static lib with SDL2 2.30.9 fetched by CMake; our DllMain calls its attach function and redirects the exe's LoadLibraryA of its DLL to our own name. Builds cleanly.
- [x] Step 2 done (2026-10-16/17): every PartyMod-patched game function rewritten with its behaviour built in
  (batches 54-69), the byte patches dropped, then PartyMod's folder folded into the game's source and deleted (see
  "PartyMod no longer a separate thing" above). Left: GameSpy's host names become SDK configuration when it's linked.
