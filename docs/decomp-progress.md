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
      - Tested in the game by the user: works (the wheel and skater in LA) - but spinning the menu turned the wheel back
        to front (fixed in batch 97).
- [x] Batch 97: the motion component's Obj_Rotate (0x00425600) rewritten, with the menu_level turn for absolute
      orientations (NOT run in the game yet). Record:
      - Cause of the wheel turning back to front: `spin_menu_spin_geometry` uses `Obj_Rotate absolute = (0, 0, 1) *
        <rot_angle>`; with `absolute` the target orientation is built from the angles alone
        (Mth::Matrix::SetFromEulerAngles, 0x004048D0), not from the node's - the wheel parts' nodes carry the level's
        108.7 degree turn, so every spin slerped them back to the shop's heading. (`relative` turns the orientation
        now about the object's own axes: RotateLocal; 0x00425520 starts the turn: an interpolator from the object's
        matrix (+0x6C of the object at +0x0C) to the target, `time` from the params.) SetFromEulerAngles's convention
        differs from the nodes' (Rz(-z) for (0, 0, z)), so no angles in the script could express "the shop's
        orientation, turned": fixed in code.
      - src/game/obj/motion.{h,cpp}: Obj::CMotionComponent::RotateTo (0x00425600, thiscall, RET 4; its caller
        0x00426770 ignores the result): relative first (returns), then absolute; with the flag 0x26AF9DC9 (name
        unknown) z negated then NegateXRotateX90; DegreesToRadians. Not original: MenuLevel::TurnOrientation on the
        absolute target (rows * RotateY(yaw) in the front end with menu_level active - as the nodes' angles).
      - Test (src/hook/selftests_motion.cpp TestMotionRotateTo): original vs ours on hand-made parameter structures
        (no script pools; an empty symbol table offline), 0x00425520 replaced by a stand-in recording the target:
        relative, absolute, converted, neither, relative + absolute; settings 0 / 1 / 2 (not 3: active in the game).
        Matrices within 2 ulps: the
        same SetFromEulerAngles gives results 1 ulp apart for bit-identical angles depending on the call order (CRT
        sinf / cosf state; the arguments were checked bit-identical with a stand-in for 0x004048D0). Mutations: 7, 6
        caught (`relative_return` after adding the relative + absolute case); `no_reset` equivalent. Register scan:
        nothing for 0x00425600; check_code_writes no overlaps. Offline all 174 pass.
- [x] Batch 98: menu_level made to work in the game - **tested in the game** (Claude launched it at the user's request,
      driving the menus with posted key messages; screenshots checked). Record:
      - The user's first runs were mode 1 (no menu_backdrop line: default 1) - batch 97's fix wasn't reached. With 3:
        - A "debug error" dialog: the self-tests ran with the ini's menu_backdrop=3 active; TestRenderWorld crashed in
          menu_level's hooks and left its stand-ins patched (a crashed test's destructors don't run) - among them a
          no-argument stand-in at 0x004A10A0, which the font loader then hit ("ESP not saved"). The runner now runs
          tests with menu_backdrop 0; gfx2's patches register themselves and a Cleanup undoes them.
        - Crashes in Script::CStruct::GetChecksum (null node): found with a new crash logger (src/hook/log.cpp: a
          vectored handler logging exceptions in the game or the DLL - registers, the top of the stack, return
          addresses that follow a CALL). LA's collision triggers nodes by **name** (0x00434A60 -> 0x0045CFC0, a hash
          of the nodes by Name; the TriggerScript read doesn't check for null): the generated node array starts with
          a stand-in per LA node (Pos, Angles, Name; no class / script; Box01 and ParticleBox_10, which the shop also
          has, unnamed). It still crashed: the node array is read through the **async file system** (0x00408D10 ->
          AsyncOpen), which took mainmenu_scripts.prx's copy - the overlay is now checked there too (AsyncFileHandle::
          Open skips the archive, PlatOpen opens the overlay's path). The overlay logs each file it serves.
      - Cameras: mainmenu_camera02 is placed from the skater with offsets that turn with it - already right in LA;
        PlaceCamera wrongly turned its matrix (the view faced away from the fire truck). Now only cameras still in the
        shop's coordinates are moved (mainmenu_camera03, SS_MenuCam, level_select_cam...). Create-A-Skater /
        Single Session (cas_setup_rotating_camera: offsets turned about the world's up axis by cas_cam_angle):
        the generated mainmenu_cas.qb uses cas_cam_angle - yaw. The shop's camera pose for the move is the steady one
        (59.833, -12.740, 181.704) = skater + targetOffset + positionOffset (the earlier (180, 40, 300) was wrong).
      - The shop's bouncy skateboards and particle effects aren't created (CreatedAtStart off; they hung in LA's air).
      - Checked in the game: main menu (wheel, skater, view 1's fire truck), a spin (the wheel turns in its plane),
        Game Options, Multiplayer, Story Mode Options, Create-A-Skater, Single Session (same framing as the shop's),
        level select, loading Training and quitting back to the LA main menu. No exceptions or debug reports.
      - Not checked: Create-A-Graphic, face mapping, the network lobby, 2 Player, view 2.
- [x] Batch 99: main menu zoom keys; the skate shop as a playable level - **tested in the game** (launched by Claude
      at the user's request). Record:
      - Zoom (menu_level.cpp ApplyZoom, from PlaceCamera): while mainmenu_camera02 plays in the LA front end, Page Up /
        Page Down held zoom in / out 1% a frame, Home resets; the camera's field of view becomes
        2 atan(tan(fov / 2) / zoom) (the game's angle remembered against the one we wrote); the value is logged when
        the key goes up; [features] menu_zoom=<percent> is the starting zoom. Checked: 146% / 208% / 297% presses.
      - The data overlay split: files at the top of thug2-decomp-data are always used; menu_level\ (the LA front
        end) is tried first while menu_backdrop=3. The tool empties the folder and makes it again (marker file).
      - The playable shop (make_menu_level.py): Load_SkateShopLevel / Level_SkateShopLevel - the shop's scene,
        collision, archives and lighting, level_name "skateshop", LevelNum_Default (as the game's test levels: 11 is
        also the created park's number, so the sign can't use it). **Not "Load_SkateShop"**: CheckForInvalidLevels
        turns the obsolete load_skateshop into Load_MainMenu. Its node array levels\skateshop\skateshop.qb: the
        shop's without the shadow skater's TriggerScript and the menus' 3D backgrounds, **TRG_Playerrestart moved
        before SS_player_restart** (the first player restart is where the skater starts; SS_player_restart is below
        the shop - the front end moves the skater by script - so the first try started in the void).
        Level select: a SKATE SHOP sign after TRAINING (Training's flag and sign texture, level_num 21 - the test
        level's slot); level_select_info_add_gaps / _add_highscore skip 21 (GetLevelGapTotals read through a null for
        it - a crash on the sign).
      - menu_backdrop 1/2 doesn't load LA behind the playable shop (OnSceneLoaded: front end only).
      - Checked in the game: SKATE SHOP sign, the level loads with the skater skating in the shop, quitting back to
        the LA main menu; no exceptions.
      - **Open: the element3d class self-test (TestElement3dClass) fails in the game since this commit** (passes
        offline; passes in the game at 9ec3b15 - bisected with a worktree build). It runs before any code this commit
        changed; the differing word ("snapshot 17, element +0x908": original 7A8017BA, ours an arena address) looks
        like a value read uninitialized whose contents depend on code layout. To investigate: which recorded word
        that is, and whether the original reads it uninitialized.
- [x] Batch 100: TestLevel; the level editor (phases 1-3, built by an agent in a worktree) and its HUD - **tested in the
      game** (launched by Claude at the user's request). Record:
      - TEST LEVEL in the level select (its DEVKIT_ONLY entry, no sign texture); SKATE SHOP moved to level_num 18 (the
        level select tells signs apart by it: the title didn't change between the two signs at 21). TestLevel in network
        games crashed: no path memory there (AllocatePathManMemory only outside network games) and Obj_BeachBall07's
        trigger script follows a path (0x00469620 through a null path manager): the overlay's TestLevel.qb gives every
        node whose trigger script follows a path AbsentInNetGames. Checked: hosting a LAN game in TEST LEVEL.
      - Fixes after the first game runs of the editor: pickable objects were 0 of 67 - the agent's model-flag rule (bit 2
        on) excludes peds' animated models (flags 01, placed through their skeletons, no geometry box); now: a model
        cloned from a sector (the component's +0x234), or flags 1 on and 4 off; no geometry box -> a person-sized box
        standing on the object (+-25 across, 0-80 up); objects with a camera component (0xC4E311FA) skipped (the camera
        object was "pickable" at the camera). 39-40 of ~67 pickable in TestLevel. debug_editor logs a census on start
        (each object: flags, cloned, model flags, geometry) and the mouse / ray once a second.
      - HUD (level_editor_hud_create / _set / _destroy in the overlay's gamemenu_pause.qb: 7 text lines top left; C++
        sets changed lines every 10th frame through level_editor_hud_set): title, camera position / yaw / pitch / speed,
        hover and selected (node class from the node table 0x0045C1B0, id, name if known, position, heading), object
        counts and moves, two lines of controls.
      - Checked in the game: LEVEL EDITOR in the pause menu; flying (W through walls, E up); mouse look; hovering a
        pedestrian (yellow box), dragging her (cyan box, moved in the log); Escape back to the pause menu, CONTINUE
        resumes with the skater. A moved ped shows its bind pose while paused (its animation isn't updated paused).
      The agent's record:

      Not original: our own feature, added at the user's request. Branch `worktree-agent-ae56b46d5b1633ebf`.

      ## What was built

      - **Pause menu item** `LEVEL EDITOR` (after YOUR OPTIONS, inside `if ! InNetGame`; the park editor's pause menu
        returns before it) shown while the script command `LevelEditor_Available` is true (`[features] level_editor`, default
        1, and `Nx::CurrentLevel() != 0`). It lives in the data overlay: `tools/menu_level/make_menu_level.py` now also writes
        `thug2-decomp-data\scripts\game\menu\gamemenu_pause.qb` (the game's file from `qb_scripts.prx`, round-tripped through
        NeverScript) with the item and two new scripts:
        - `level_editor_enter`: `LevelEditor_Start` (returns false -> nothing happens), then the goal editor's way of taking
          the pause menu down (`current_menu_anchor` and `helper_text_anchor` destroyed, `pause_menu_gradient off`,
          `menu3d_hide`) and of taking the skater out (`PauseSkaters`, `HideSkaterAndMiscSkaterEffects`); the game stays
          paused (no `UnpauseGame`). root_window's pad_start -> `level_editor_exit`, pad_select -> `null_script`.
        - `level_editor_exit`: `LevelEditor_Stop`, `Skater:AddSkaterToWorld`, `create_pause_menu` (its CONTINUE unpauses
          the skaters and restores the start key binding as ever).
        Checked offline: the overlay generated into the scratchpad and decompiled back (`ns -d`) reads as intended.
      - **Script commands** `LevelEditor_Available` / `LevelEditor_Start` / `LevelEditor_Stop`
        (`src/game/front/level_editor.cpp`), registered by `Front::LevelEditor::RegisterScriptFunctions` from `Sys::Main`
        right after 0x005A5E70 (which registers the game's 1254-entry table 0x0067F3A0 through our
        `Script::RegisterCFunctions` 0x0046EFD0).
      - **Fly camera**: `LevelEditor::PlaceCamera` in `Nx::RenderViewport` (next to MenuLevel::PlaceCamera) - the first
        viewport camera seen after Start has its pose noted (Gfx::Camera m_pos +0x5C, m_matrix +0x6C) and is then given the
        editor's pose every frame; Stop writes the noted pose back. `LevelEditor::Update` at the top of `Nx::RenderWorld`
        (runs while paused; frame time from SDL's performance counter, clamped to 0.1 s): W/A/S/D along the view / its right,
        Q/E (or Ctrl/Space) world down/up, Shift x4; right mouse button held = mouse look (SDL relative mode); the wheel
        changes the speed (x1.25 a notch, 50-20000 units/s, default 800) when it isn't turning an object. No collision.
      - **Picking**: walks `Obj::CompositeObjectManager()` (0x006F0FB8) with our `CBaseManager::ForEach`. Candidates: not
        hidden / being destroyed, no skater physics component (0xD9151D4D - skips the skater), a model component
        (0x286A8D26) whose model (+0x208) has flags +0x44 bit 2 on and bit 4 off (the condition the model component's
        Teleport 0x004219F0 places a model under - others wouldn't follow a move), its geometry 0 (0x00493CD0) and that
        geometry's bounding box (0x0048DE80 -> geom vtable +0x3C), scaled by the model's +0x1C..+0x24 when +0x44 bit 8 is on
        (0x00493090 draws it so). The box is placed with the object's display matrix rows (+0xC0) and position (+0x4C) - the
        model's world matrix as 0x004215F0 builds it. Mouse ray: window pixel -> back buffer (MouseMenus::CurrentMapping) ->
        the 3D viewport's normalised coordinates -> `-back + x tanX right + y tanY up`, tanX = tan(angle / 2),
        tanY = tanX / (aspect * 0.90909094) - the same angle (Camera::GetAdjustedHFOV) and aspect (0x004A0D20) RenderViewport
        passes to Nx::SetCamera, noted by `LevelEditor::NoteView` after the first SetCamera there.
      - **Highlight**: `LevelEditor::Draw` after `Draw2DWithBlackBars` in RenderWorld: the box's 12 edges, clipped to the near
        plane and projected with the same lens, as a pre-transformed line list (FVF 0x144 through the renderer's cached
        SetTexture 0x004DA730 / SetVertexFormat 0x004D9DB0 / SetVertexShader 0x004DAA80; texture stage 0 colour/alpha op set
        to "select diffuse" and put back). Hovered: yellow; selected: cyan. No on-screen label (debug_editor logs the name).
      - **Moving**: left click on the hovered object selects it and starts a drag on the horizontal plane through it (the
        grab offset kept); Shift (also toggled mid-drag) = up/down only, on an upright plane facing the camera. Wheel over the
        selected object (or while dragging) turns it 15 degrees a notch about the world's up (m_matrix +0x6C and
        m_displayMatrix +0xC0 rows). A move writes m_pos and m_oldPos then `CCompositeObject::SetTeleported(true)`
        (0x0045E4C0: every component's Teleport - the model and collision follow at once, as FrontEndObjects does). Click on
        nothing deselects. Nothing is saved.
      - **Leaving**: Escape (the editor's SDL handler; level_editor_exit is run with `Script::RunScript` from the front end's
        input logic, not from the event pump or the render), Enter (the keyboard's Pause binding) or a controller's Start
        (root_window pad_start -> level_editor_exit).
      - **Stand-downs while active** (each a "Not original" hook with its original behaviour noted):
        `Sio::PollKeyboard` gives the game only the Pause key, and only once it has been up since Start (the Enter that chose
        the item would otherwise leave at once); `MouseMenus::Update` drops the mouse's clicks / wheel and runs
        `LevelEditor::InputLogic` instead (FrontEnd::InputLogic now calls MouseMenus::Update whatever the mouse_menus setting
        - Update checks it itself). The editor shows the OS cursor on Start and restores its state on Stop.
      - Settings: `[features] level_editor` (default 1), `debug_editor=1` (logs the camera taken over, the view, hover changes
        with each object's id / name / type / position / flags / box, selections, moves from -> to, turns, speed changes).
        CLAUDE.md's features list updated.
      - Scenery later: picking is one function (`Pick` in level_editor.cpp) over composite objects; scene sectors would be
        another candidate list tested the same way (their world boxes), a hit remembered as a sector.

      ## Tests (`src/hook/selftests_editor.cpp`, Early)

      - TestEditorFly: the frame for yaw/pitch (right-handed: right x up = back; right level; up upwards), yaw/pitch back
        from a frame, W/S/A/D/Q/E directions and diagonal speed, mouse look directions and the +-89 degree clamp, turning
        about up.
      - TestEditorUnproject: for 4 poses x 4 angles x 3 aspects x 5 screen points x 3 depths, a point on our mouse ray put
        through D3DX's look-at (0x005C0985) and perspective (0x005C0ACD) built exactly as Nx::SetCamera builds them lands on
        the same normalised coordinates (so the ray matches the game's projection, handedness and widescreen aspect
        included); our Project agrees; pixels <-> normalised coordinates round trip.
      - TestEditorRays: ray vs sphere / box (hit, miss, behind, inside, a turned and scaled box), box corners, the drag plane
        (hit, parallel, behind), world- vs model-space box test, near-plane clipping, the Pause key gate.
      - TestMain (selftests_misc.cpp) stands in for RegisterScriptFunctions (ours only, not in the original's trace).
      - tools/mutate.py: 14 mutations of level_editor.cpp's maths, all caught (see below).

      ## What to check in the game

      1. Run `python -I tools/menu_level/make_menu_level.py` (writes `C:\Games\THUG2\thug2-decomp-data`), set
         `debug_editor=1`. In a level, pause: LEVEL EDITOR after YOUR OPTIONS. Choose it with Enter and with a mouse click:
         the menu goes, the skater disappears, the view stays where the game camera was and does not jump out at once.
      2. Fly: W/A/S/D/Q/E/Shift, right mouse look (left/right not mirrored, up looks up). Wheel without an object: speed.
      3. Hover objects (benches, cars, peds, level objects): the yellow box should sit on the object. If the boxes are offset
         or huge, check the log's "box" values: the geometry's box may be in world space for some objects (a heuristic picks
         world space when the box holds the object's position but not the origin) or in another unit.
      4. Click-drag: the object follows the cursor on the ground plane; Shift moves it up/down; wheel turns it. Its collision
         should move with it (skate into it after leaving). Click empty space: deselects.
      5. Leave with Escape, with Enter and with a pad's Start: the pause menu returns; CONTINUE: skater back where he was,
         game runs, moved objects stay moved.
      6. `level_editor=0`: no item.

      ## Known risks

      - Not run in the game. The geometry box (geom vtable +0x3C) is assumed to be in the model's space (element3d uses it
        so for front-end models); the model flags test (+0x44 bits 2/4) is inferred from 0x004219F0 and may hide objects
        that should be pickable (debug_editor's hover log shows what is found).
      - Objects whose scripts / components put them back when the game unpauses (peds on paths, cars on rails, the wheel of
        the front end) will move back; suspended objects (far away when paused) stay as they were - the suspend component's
        distance pass doesn't run while paused.
      - The 2D state the lines draw with: Z test / blend states are left as the 2D pass leaves them (the black bars draw the
        same way); if the lines don't show, the render states need setting.
      - Mouse look uses SDL relative mode; the cursor's position after it is SDL's choice.
      - Escape runs level_editor_exit from MouseMenus::Update (the front end's input logic). If that isn't called while
        paused in some mode, Escape does nothing (Enter / Start still work through root_window's pad_start).
      - The HUD is already hidden by the pause menu; goal panels / messages that appear while paused might show.
      - The overlay's gamemenu_pause.qb replaces the game's for every mode; a changed file in a future game data version
        would make the tool stop (it checks the YOUR OPTIONS line is there once).
- [x] Batch 101: the level editor moves scenery (static scene sectors) - **tested in the game** (launched by Claude):
      in LA a click on the fire truck selected its 9-sector cluster Fire_Tender (cyan boxes), W moved it (the pivot
      from (-2339, -333, 6337) to (-2445, -333, 6425), the camera following) and X turned it a quarter turn about its
      centre, the boxes following. Census on entering the editor: LA's scene 1145 sectors, 839 movable. Not checked
      yet: skating into the moved truck (collision after UpdateSuperSectors). debug_editor also lists the scene list's
      nodes. Merged from the agent's branch; its record:

      Branch `worktree-agent-aba8ae8c772a2cee9`, based on main c4f11c5. **NOT run in the game yet.**

      ## What

      The level editor (pause menu -> LEVEL EDITOR, `[features] level_editor=1`) now picks the level's static scenery as
      well as composite objects:

      - Hovering: the game's own collision ray (CFeeler, static collision only) finds the sector under the cursor; sectors
        with no collision are found by their bounding boxes. The nearer of that and the object box hit wins. The hovered
        sector is outlined yellow (orange when it can't be moved); the HUD's Hover line says
        `scenery <name> <checksum>  W x H x D  cluster <name>  (can't move: <why>)`.
      - Left click on scenery selects the sector **and the rest of its trick-object cluster** (the LevelGeometry node's
        `TrickObject Cluster=`), outlined cyan, and starts a drag. Alt+click or a middle click adds / removes one sector.
        A click on nothing deselects; a click on an object selects the object (the two kinds don't mix).
      - Moving: the existing controls - drag (horizontal at the selection's height; Shift: up / down), Page Up / Down
        (R / F) raise / lower, the wheel turns: **scenery by quarter turns (90 degrees a notch)** about the selection's
        centre (the engine can only turn level geometry by quarters, see below). HUD: Selected line
        `scenery <first> + N more at (x, y, z) turned <deg>`, and `Scenery moved: N sectors`.
      - Nothing is saved; a moved sector stays moved until the level reloads (also after leaving the editor).

      Files: `src/game/front/level_editor_scenery.{h,cpp}` (new), `level_editor.{h,cpp}` (picking / selection / drag /
      HUD / drawing generalised to "an object or scenery"), `src/hook/selftests_editor.cpp` (TestEditorScenery),
      CMakeLists.txt.

      ## The fire truck (LA)

      It is **static LevelGeometry**, not an object: in LA's node array (`levels\LA\LA.qb`) nine LevelGeometry nodes share
      `ncomp_LevelGeometry_542 = {Class=LevelGeometry CreatedAtStart TrickObject Cluster=Fire_Tender CollisionMode=Geometry}`:
      `FE_CAB`, `Fire_Tender`, `FE_rearcab`, `dt_tire01`, `dt_tire03`..`dt_tire07` (around x -2600..-2030, z 6150..6600).
      The ladder is separate: `Turntable_Ladder` (ncomp_LevelGeometry_1, no cluster), `FE_Ladder_Section02` and
      `Object03` (cluster `FE_Ladder_Section02`). The truck also has grind rails: RailNodes `TRG_FireTruck01..25`
      (cluster Fire_Tender, Chainlink). So a click on the body selects the 9 cluster sectors; Alt / middle click adds the
      ladder's three. The rails are **not** moved (they live in the rail manager) - grinding stays where the truck was.
      (The node Pos values are pivots only; level geometry vertices are in world space, drawn with no transform.)

      ## Findings (addresses)

      - `Nx::CSector` (vtable 0x00649A40, 0x1C bytes): +0x08 flags (1 active?, 0x40 collision in super sectors, 0x20
        marked for a super-sector update, 0x08/0x10 delete/remove), +0x0C checksum default, +0x10 quarter turns, +0x14
        CGeom, +0x18 CCollStatic. Methods: 0x0049E660 SetGeom, 0x0049E680/0x0049E690 Set/GetVisibility (geom vfunc +0x2C /
        +0x30), 0x0049E6A0 GetBoundingBox, 0x0049E6C0 SetWorldPosition, 0x0049E700 SetOrientation, 0x0049E740
        GetWorldPosition, 0x0049E760 SetYRotation(rot90), 0x0049E7C0 SetScale, 0x0049E890 / 0x0049E8A0 flags set /
        clear, 0x0049E9B0 SetActive (geom vfunc +0x34, collision flag 4), 0x0049E9E0 IsActive, 0x0049EA30
        SetCollidable? (flag 1, collision flag 2), 0x0049EAA0 SetCollision, 0x0049E900 ctor.
      - `CEngine::sGetSector(checksum)` 0x0048C510 (cdecl, walks the scenes); `CScene::GetSector` 0x0049CB00 (thiscall:
        hash table at scene +0xD0 = {bits, entries[1 << bits] of {checksum, sector, next}}); scene list head 0x0048C7F0.
      - `CScene::UpdateSuperSectors(list)` 0x0049D780 (thiscall, RET 4): walks every sector, flag 4 -> add, 0x18 ->
        remove/delete, 0x20 -> update; then 0x00418F60 on the super-sector manager (scene +0xD4). The park editor calls it
        with 0 and the bottom-up heap pushed (0x00592090). Scene +0x274: "needs update" (0x0049E1B0, set by clones).
      - PC geometry `CGeom` vtable 0x0064A368 (base CGeom stubs at 0x00648EC8, "STUB: Plat..." strings around 0x00648C24 "STUB: PlatSetWorldPosition", used by 0x0048DBB0):
        +0x14 / +0x24 box min / max (as loaded - not updated by moves), +0x3C meshes, +0x40 count, +0x44 visibility
        mask, +0x48 active, +0x4C instance (null for level geometry). Vfuncs: +0x2C SetVisibility 0x004B4870, +0x34
        SetActive 0x004B4630, +0x3C GetBoundingBox 0x0054CA80, +0x48 SetWorldPosition 0x004B55D0, +0x4C GetWorldPosition
        0x004B46A0, +0x50 SetOrientation 0x004B5650 (instances only), +0x58 RotateY 0x004B57C0.
      - `sMesh::SetPosition` 0x004D6E00: moves the vertices by the change from the mesh's last position (+0x90 matrix,
        made on demand) in each vertex buffer's CPU copy (+0xCC[i]: {size, ?, D3D buffer, copy, ...}) and uploads it
        (0x004D40E0: Lock, memcpy, Unlock); also the sphere (+0x04). `sMesh::RotateY` 0x004D6FB0 (not a Ghidra
        function): quarter turns about the mesh's position: rot 1 (dx, dz) -> (dz, -dx), rot 3 -> (-dz, dx).
      - `CCollStatic` vtable 0x00645C18 (0x18 bytes: +0x04 checksum, +0x08 flags, +0x0C tri data): +0x14
        SetWorldPosition 0x00410C20 translates by (position - vfunc +0x1C's constant origin 0x00691914) - i.e. it
        treats the position as a change (0x00416400 moves vertices / bbox); +0x18 SetOrientation is a no-op
        (0x005BAB90); +0x58 RotateY 0x0040AE80 -> 0x004164F0 (about a pivot).
      - CFeeler collision data from +0x24: normal, point (+0x34), ..., distance fraction (+0x48), ignore flags
        (+0x4C/+0x4E), the hit collision object (+0x54) and its checksum (+0x58) - written by 0x004131F0.
      - Node commands: 0x005A4DA0 dispatches Create(0)/.../Kill/Visible/Invisible; LevelGeometry Kill / Create
        (0x005A3B80) toggle the sector's visibility mask bit; Create with `CloneFrom`-style key 0xEDC063B9 (0x005A2E50)
        clones a sector (0x0049D510) and places it with SetOrientation (Angles), SetScale, SetWorldPosition (Pos).

      ## How moving works (and why not "promotion" to a composite object)

      The engine already moves level geometry in place - the park editor does it for its pieces (0x00594730:
      SetWorldPosition + SetYRotation on a CSector, then UpdateSuperSectors). That moves the drawn vertices and the
      collision together, with no new objects, no hidden originals and no memory beyond the meshes' position matrices.
      Promotion (a LevelObject cloned from the sector + hiding the original) was not needed. Limits: only quarter turns
      (SetOrientation is instances-only for geometry and a no-op for static collision).

      `CSector::SetWorldPosition` can't be used directly: the collision side takes the position as a change, so a second
      move would double it. The editor keeps its own record per sector (box, turns and position when first touched, the
      placement since) and applies: the turn through CSector::SetYRotation (both turned about the geometry's position),
      then the geometry's SetWorldPosition (absolute) and the collision's SetWorldPosition handed origin + change, sector
      flag 0x20 when 0x40 (as 0x0049E6C0), and `UpdateSuperSectors` (bottom-up heap pushed) once the drag / raise / turn
      ends and on leaving the editor. The record is re-checked before each use (sector, geometry, turns, position) so a
      reloaded level starts afresh.

      Movable only when: PC geometry (vtable 0x0064A368), not an instance, a box no larger than 4000 in any direction
      (ground / streets excluded), and every mesh's vertex buffers have a CPU copy holding exactly that mesh's vertices
      (else "shared vertex buffer" / "no vertex copy" - moving would shift the wrong vertices).

      ## Tests

      - `TestEditorScenery` (Early, offline): quarter turns match the engine's rotation code and TurnAboutUp; placements
        moved / turned about a pivot; the engine state (turns, position) follows a placement from any first state;
        placed boxes' corners, ray hits and bounds; size limits; box growth. Proved with tools/mutate.py (7 mutations:
        quarter-turn direction, turn sign, engine turns, engine position, box axis, size check, box growth - all caught).
      - `tools/ghidra/audit_game_function_calls.py`: no mismatches (the three "?" are tail-jump thunks to vfuncs whose
        RETs were checked by hand: 0x004B55D0 RET 4, 0x004B46A0 RET, 0x00410C20 RET 4).
      - thug2_check: all passed (178 early tests). Built with tools\verify_build.cmd.

      ## What to check in the game

      Use `[features] debug_editor=1` (the log lists each scene's sector counts on entering the editor: "N movable").

      1. LA: fly to the fire truck (around -2400, -350, 6400). Hover the body: HUD "scenery Fire_Tender ... cluster
         Fire_Tender", yellow box. Click: 9 cyan boxes (log lists the members). Drag it: the truck (cab, body, wheels)
         moves smoothly; the ladder stays (Alt+click / middle click its pieces to add them).
      2. Wheel over it: turns 90 degrees a notch about its centre - check it turns in place, not around the world origin,
         and the boxes follow.
      3. PgUp / PgDn: raises / lowers it.
      4. Leave the editor, skate into the moved truck: you should collide with it where it now is, and pass through
         where it was. Grind rails (TRG_FireTruck*) are still at the old place (expected).
      5. Hover the ground: "(can't move: too big)", orange box. Hover an invisible wall: the ray goes through it.
      6. Things to watch: the truck disappearing at some angles (render culling), lighting looking wrong after a turn,
         a crash on the first move (vertex buffers), a freeze when committing (UpdateSuperSectors), skater falling
         through the moved truck (super sectors not updated).

      ## Risks

      - Vertex buffers: moving writes the meshes' CPU vertex copies and re-uploads them (the park editor's path). Checked
        per mesh that each buffer's copy is exactly that mesh's vertices; if level meshes share buffers, sectors are
        reported "can't move: shared vertex buffer" instead (then nothing in a level would move - check the census).
      - Collision: only updated in the super sectors when a move ends (UpdateSuperSectors) - during a drag collision
        lookups use the old cells (harmless: the skater is out of the world). The park editor's update runs with the
        bottom-up heap; in a normal level that heap is the level's - watch for heap errors in the log.
      - Rails, trick-object clusters (gap / trick scoring geometry), particles, lights and shadows tied to the scenery are
        not moved. LevelObjects / Kill-Create of the same sectors by goal scripts keep the moved vertices.
      - Normals are not turned with quarter turns (the engine's RotateY doesn't either) - prelit level geometry mostly
        doesn't care; dynamically lit parts may light oddly.
      - Box picking (sectors without collision) uses the loaded box placed by our record; sectors that had been placed by a
        clone (CloneFrom) before the editor touched them may have boxes in their own space (rare in levels).
      - Selecting by cluster can grab more than expected in levels where a cluster spans many sectors; Alt / middle click
        removes single sectors.
      - Alt+click: Windows may treat a lone Alt press as a menu key (SDL normally swallows it); the middle click does the
        same thing.
- [x] Batch 102: level editor - highlight by tint, scenery groups grow by attached pieces, grind rails move with
      scenery - **tested in the game** (launched by Claude; LA's fire truck) and confirmed working by the user.
      - Tint instead of boxes: CGeom::SetColor (vfunc +0x18, 0x004B48B0 - what SetObjectColor 0x005A3E80 uses for
        graffiti): level geometry -> each mesh's colour (+0x80..+0x88 floats = byte / 255, flag 4 at +0); instances ->
        +0x4D RGBA and flag 4. 0x80 per channel is neutral (SetColor clears instead). The renderer honours it: scenes
        draw through 0x004DC700 (material changes 0x004D1F00, each mesh 0x004D5290); in the pixel-shader mode
        (0x00786AB8 = 1, as on this PC) a mesh with flag 4 goes through 0x004D3D10, which multiplies the material's pass
        colours by 2 x the mesh colour (passes whose flag byte has bit 7 are left alone); the fixed-function path
        passes the colour to 0x004D24D0 (TEXTUREFACTOR, MODULATE2X). Objects' instances: 0x004D1335 copies +0x4D to
        0x00786F20 with 0x00786F1C set. The editor saves the raw mesh / instance state the first time and writes it
        back when no longer highlighted (Scenery::Tint / EndTints) - checked in the game: the truck pulses cyan while
        selected, back to its colours on Backspace. Hover: warmer, over the whole group a click would take. Boxes
        only for geometry that isn't the PC class; objects' models through the model component's model (+0x208) and
        0x00494640 (geometry by index - as 0x00420700 colours level objects). Peds' tint not checked.
      - Group growth (PieceFits / PieceTouches, self-tested and proved with tools/mutate.py, 9 mutations): pieces no
        bigger than the group, touching one of its boxes above that box's bottom, three rounds, each with its cluster;
        only for a free-standing group (every side >= 20, none > 1500: a floor or a building doesn't grow - the first
        game run grabbed every wall standing on a flat ground piece). The truck: 9 cluster sectors + 8 (ladder, turntable,
        small parts) - and the yellow kicker ramp touching it, which then turns with the truck (adjust if it grabs too
        much).
      - Grind rails: Mdl::Skate (0x007CE478) +0x24 = the level's rail manager (CFunc_ParseNodeArray 0x005A34E0 ->
        0x0057E960 fills it from RailNodes and ClimbingNodes): +0x40 nodes (0x54 bytes each: +0x00 / +0x10 segment box,
        +0x20 position, +0x40 next, +0x44 prev, +0x48 flags - 4 active, 0x10 climbing, +0x4C node name, +0x50 terrain),
        +0x48 count, +0x50 rails riding an object (positions in its space, 0x0057CB60). Boxes from the node and its next
        (0x0057C260); skaters test every node's box (0x0057E2D0) - no grid, so moving a rail is its position and the
        boxes of its segment and of those ending at it. The selection takes rail nodes of its sectors' clusters (the
        truck: TRG_FireTruck01..25 in Fire_Tender - 25 found) and ones lying within a selected box; they move and turn
        with the sectors (same placements) and are drawn as cyan lines. Not checked: grinding them where they now are.
- [x] Batch 103: level editor UX - **tested in the game** (launched by Claude; LA's fire truck and the office block
      behind it).
      - Hold-and-drag fixed: the drag plane went through the selection's middle, and with the camera below that (the
        fire truck's middle is above a camera on the street) the ray never met it - nothing moved. Now a horizontal plane
        at the base (an object's feet, the scenery's bottom: Scenery::Bottom); a ray meeting it at under ~12 degrees
        (camera about level with it: one pixel threw the truck 1,200 units) or not at all drags across the view at the
        same height instead; hits further than three times where the drag started are ignored. The camera holds still
        while dragging and turns to the selection when let go (FocusOn). A drag moves only after 4 pixels.
      - Escape during a drag cancels it (back where it started). The Pause key (Escape here) also reached
        level_editor_exit through the menu's pad_start: PassPauseKey holds it back while dragging, and until it has
        been up after a cancel.
      - Ctrl+Z undo (64 steps, per session): drags, key moves (one step per press-and-hold), turns, drops; objects
        put back with their matrices, scenery by selecting the same sectors (Scenery::SetSelection - rails with them)
        and moving back / turning back. Ctrl no longer flies down (Q does).
      - G: onto the ground below (the collision ray from just under the scenery's bottom, or from above an object's
        feet). A ground marker while something is selected: a line from the base to the ground below and a cross.
      - HUD control lines updated.
- [x] Batch 104: level editor - groups on the level's layout, push / pull, undo / redo, reset, hide - **tested in
      the game** (launched by Claude; LA's fire truck).
      - Groups were decided on where pieces are now: moving the truck into another piece made that piece part of it the
        next click. Now on the boxes as loaded (GeomBox) and only among pieces with the same placement (moved together;
        never moved = none): a group moved into something doesn't take it, a piece moved away from its group no longer
        brings it. Hidden pieces aren't taken. Checked: the moved truck re-selects as the same 17 sectors.
      - Push / pull "locked": with the camera about level with the truck's base the drag fell back to across-the-view.
        Now, under ~20 degrees, the mouse moves it in the view's terms (left / right across, up / down away / back along
        the ground, a pixel's span at its distance - 2.5x for away). Checked: 105 pixels up pushed the truck ~190
        units straight away. The wheel pushes / pulls while dragging (a tenth of its distance a notch); Ctrl snaps to a
        25-unit grid.
      - Undo now restores absolute states (Scenery::State: placements, hidden, rails; objects' position and matrices),
        so Ctrl+Y redo, Home (back where the level had it; objects' first places kept per level) and Delete (scenery
        hidden: CSector::SetActive 0x0049E9B0 false - not drawn, collision off - and its rails' active flag 4 cleared)
        undo like anything else. Checked: Home, Delete, Ctrl+Z, Ctrl+Y, Ctrl+Z on the truck.
      - HUD: the camera and counts share a line; three lines of controls, for flying, a selection, and dragging.
- [x] Batch 105: level editor - saved layouts, duplicates, move arrows, multi-select, the browser, the HUD redone -
      **tested in the game** (launched by Claude, holding the game lock the lfx agents share; LA).
      - Split into modules: level_editor.cpp (camera, input, selection, arrows, undo, duplicate, browser, drawing),
        level_editor_objects.cpp, level_editor_scenery.cpp, level_editor_layout.cpp, level_editor_hud.cpp.
      - Names: ParseQB (0x00472420, ours) notes every .qb's 0x2B checksum names into a map (Script::KeepQbNames /
        KeptQbName, on when the editor is enabled) - the HUD and the browser show FE_CAB, Fire_Tender, LA_Bench03...
      - Layouts: thug2-decomp-layouts\<main scene checksum>.txt (text: sector / rail placements and hidden, scenery
        copies as a placement relative to the source as loaded - Compose -, objects' positions and matrices, object
        copies). Ctrl+S writes; Nx::CFunc_LoadScene tells Layout::OnSceneLoaded and, once the level is up (the skater,
        a second), it is put back (not in network games). Checked: saved, relaunched, "Layout loaded: 17 pieces, 18
        copies, 25 rail points". Rails' hidden is relative to how they started (some start off: the first save listed
        19 untouched rails).
      - Scenery copies: CScene::CloneSector (0x0049DF30, thiscall RET 0x14: source checksum, scene, instance 0,
        add-to-collision 1, new checksum 0 - new ids 1, 2, 3...) as the park editor's pieces: own meshes, added to the
        render lists, flag 4 -> UpdateSuperSectors adds the collision. A full clone's box (+0x14/+0x24) isn't copied:
        set to the source's bounds where it is. Copies take no rails (the first try moved the source's). Checked: the
        truck's 17 pieces and a bench copied, moved, saved, reloaded.
      - Object copies: the node's structure copied (InitCopy) with a new Name / Pos through CreateFromStructure's
        0x005A2E50 - LevelObject / GameObject only; deleted with MarkAsDead (Kill needs a node). The first try crashed:
        creating a LevelObject runs scripts that call SetObjectColor on it, whose node command (0x005A3E80) reads the
        node of that name unchecked - a copy had none. Now each copy's structure is a node of ours: a node array
        level_editor_copy_nodes added to the node manager (made again with each node: a one-element script array
        keeps its value in place of the pointer), only the new node hashed (HashNodeIn); taken out and freed when a
        level loads. Checked: a cone placed from the browser, duplicated, undone / redone, deleted / undone, saved,
        and made again by the next launch's layout. Not checked: leaving a level with copies for another one.
      - Move arrows (x red, y green, z blue, a fifth of the distance long; within 8 pixels lights up): dragging one
        moves along that axis (ClosestOnAxis). Checked: dragged the truck 120 along x. Multi-select: Shift+click adds
        objects (moved / turned together about the first) or scenery groups. Undo / redo now also cover copies made
        and deleted (shown / hidden, made again / MarkAsDead). The browser (B): the level's movable scenery (618 in LA)
        and copyable objects; Up/Down, Left/Right, F shows the original, Enter places a copy on the ground where the
        cursor points.
      - HUD: a status panel top left (mode, selection, under the cursor, camera and counts), the keys for the mode at
        the bottom (fly / selected / dragging / along an arrow / adding) with \c1 key colour (the subtitles' code),
        a message line, F1 help panel. Measured: the white2 sprite is ~3.2 across and ~8 down per unit of scale, and
        the HUD container sits 22 lower than its 640 x 480 position (moved up). Lines stay under 90 characters with
        their colour codes.
- [x] Batch 106: the "Lighting / FX / park memory" range 0x595030-0x59F610 decompiled whole by three agents in
      parallel (worktrees, a shared game lock: one game at a time, the deployed DLL and ini put back on release) - it
      is the park editor (Create-A-Park) and the skate game's script commands, not lighting: 169 functions, all
      REPLACE but one source-only (a Ghidra split of 0x599CD0). The three defined the park editor's classes apart;
      lfx-a unified them in src/game/ed/park.h. Tested in the game by the agents: LA loads and plays, Create-A-Park
      opens, the cursor moves, the floor raises / lowers, a restart point placed, Test Play. Not tried in the game:
      gaps, clipboards, area selection, saving parks. The merged build: 1595 replacements, 0 MISMATCHED, 190 early
      self-tests pass. The agents' records:
      ### 0x595030-0x598A60 (lfx-a; also the unification of the three)
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
      ### 0x598A60-0x59C900 (lfx-b)
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
      - [x] Tested in the game (2026-10-18, run by lfx-b under the game lock, verify-build DLL): batch 1 alone, then
        batches 1+2. Main menu -> Free Skate -> Los Angeles plays; Create-A-Modes -> Create-A-Park loads the editor
        (the default park with its fence and restarts), the cursor moves (W/D), A/S raise and lower the floor (a dug-in
        cell with its riser walls appeared), the pause menu comes up, Test Play puts the skater in the park. Log: all
        110 early / 17 late / 2 engine-started self-tests pass, 912 of 912 replacements installed, no run-time check
        failures or CRT reports. Not exercised: the clipboard / gap / rail tools, saving and loading parks, switching
        piece sets (the menu keys weren't found from the keyboard script).
      ### 0x59C900-0x59F610 (lfx-c)
      # lfx-c: 0x0059C900-0x0059F610 (Lighting / FX / park memory, third part)

      Records for the lead to fold into docs/decomp-progress.md.

      - [x] Batch lfx-c 1 (NOT run in the game yet): the skate game's script commands at the end of the range and two
        helpers - src/game/script/skate_cfuncs.cpp, src/game/core/dump_text.cpp (24 functions, all REPLACE).
        - CFunc_ControllerDebounce 0x0059EDF0 (button not "pressed" for `time` s: per-button times at 0x007CE758,
          indexed like the button table 0x0067D6D8 via 0x0044E5A0; x87 unsigned clock + time*1000 truncated through
          64 bits), ControllerPressed 0x0059EE80 (the helper of CFunc_ControllerPressed 0x0059F9A0: the controller's
          input state at +0x18, one byte per button - right +4, left +5, up +6, down +7, triangle +8, circle +9,
          x +0xA, square +0xB, L1 +0xC, R1 +0xD, L2 +0xE, R2 +0xF, black +0x16, white +0x17, z +0x18; diagonals
          need both; debounced while the time is later than now (unsigned, strict)).
        - FakeLights 0x0059F0F0 (id= changes one light, 0x004901D0; prefix= lists nodes 0x0045BF90 with a 16-bit
          count and sets lights up 0x0048FE40; flag 0x80804991 = not a prefix), UpdateFxLighting, NodeExists,
          LoadNodeArray (kills the session's spawned scripts, notes the name at 0x007CE6D8 unbounded, loads the .qb),
          ResetCamera ("TODO: ScriptResetCamera not implemented" per viewport with a camera), Set{Movement,Rotate}Velocity
          (viewer speeds, defaults 400 / 120), ClearViewerObject, CasArtist (global cas_artist), SpawnScript (all the
          options; NotSessionSpecific is an integer; at the caller's node; notes the caller's .qb file unless flag
          0x01917DF2 - and, as in the original, calls SetSpawnerFile on a null result), UnBlockInterruptedScript,
          KillSpawnedScript (Name= first, else Id=), Pause/UnPauseSpawnedScripts, DisplayLoadingScreen (default
          "images\xxxxx", freeze, blank, a float = the bar's time), HideLoadingScreen, LastBroadcastedOmnigonWas /
          OmnigonAllowed (checksum 0xAE94C183 vs 0x007CEBC8), SetLevelName (sprintf with the name as the format -
          kept), DumpScripts (compiled out).
        - DumpText 0x0059ED60: the pool-usage dump's (0x00448330) line buffer 0x007CE7B8-0x007CEBA8, '\n' -> CR LF,
          flushed with File::Write to the handle at 0x007CEBB8 - which nothing ever sets (a dead log in this build).
          The fit check is made before the CR LF expansion and skipped for an empty buffer (both can overrun - kept).
        - Test: TestSkateCFuncs (src/hook/selftests_lfxc.cpp, Early): every command against the original on
          hand-built structures, every game function they call stood in for by recording code (StandIns), all 20
          buttons over 24 input patterns with debounced entries, the dump buffer through two flushes and an overrun.
          33 deliberate bugs caught, 3 of them after strengthening (the debounce table recorded before ControllerPressed
          overwrote it).
        - Unknown checksum names: 0x80804991 (FakeLights), 0x01917DF2 (SpawnScript), 0xAE94C183 (Omnigon),
          0x2AB71EEF (DumpScripts).
        - Tested in the game 2026-10-18 (lfx-c's own run, under the game lock): starts, Free Skate -> Los Angeles
          loads and plays, the pause menu works; log clean (all self-tests passed, no CRT reports / RTC failures).
      - [x] Batches lfx-c 1-4 tested in the game 2026-10-18 (lfx-c's own runs under the game lock, the build of
        commit "lfx-c batch 4"): Free Skate -> Los Angeles loads and plays, pause menu fine; Create-A-Modes ->
        Create-A-Park loads the park editor (the piece sets RESTARTS / TEAM FLAGS / TOOLS / CLIPBOARD / TABLES-BENCHES
        built by Initialize, the memory bar drawn), the cursor moves and a restart point is placed; no crash; log clean
        (932 of 932 replacements installed, all early / late / engine-started self-tests passed, no CRT reports or
        run-time check failures). Not exercised: gaps, clipboards, raising / lowering, area selection.
      - [x] Batch lfx-c 2: the park editor's memory budget - src/game/park/park_memory.cpp
        (17 functions, all REPLACE): GetParkHeap 0x0059E100 (0x007CE688), GetMainHeap 0x0059E180 (the bottom-up
        heap), ClearStats 0x0059E110, SetCountsDirty 0x0059E170, RemoveDma / AddPieceDma / RemovePieceDma
        0x0059E130 / 0x0059E190 / 0x0059E1E0 (a piece's DMA pieces from the global structure 0x9547DA8D by type, 1 if
        missing; piece flag 0x100 = don't count, 0x4 = counted), the pools of CMapListNode / CClipboardEntry /
        CClipboard (0x0059E210 / 290 / 310: PoolPairs at 0x007CE5F0 / 0x007CE5C4 / 0x007CE5B4, the pair's second word
        = per-slot "made here" bytes), Marker 0x0059E390 (memory markers: OUTER SHELL MEM / NETANIMS MEM debug output),
        ShutDown 0x0059E4E0, GetMemoryValue 0x0059E5C0 (runs script 0x3CB03619 once with the name's checksum and an
        index, reads 0x083FDB95), GetMemoryStats 0x0059E730 (the editor's memory bars: main / top-down / park heap,
        component / vector / array pools, DMA pieces; x87: free amounts rounded to float before dividing, max taken
        before rounding for the heaps and DMA, after for the pools; returns `out` - 2 callers read EAX),
        MainHeapHasRoom 0x0059EAD0 (< 95% after count*size*40000 bytes), PiecesFit 0x0059EB30 (DMA, park and main heap
        sizes per floor piece, < 90%), Init 0x0059EC00 (the "Park Editor" heap in its own AllocRegion of
        park_heap_size - top-down heap for level 0xCD3682C1, else bottom-up - and the three pools; "pool is at %p").
        - Test: TestParkMemory (Early): GetMemoryValue with its structures / script / result stood in; everything else
          with GetMemoryValue, Region::MemAvailable, Heap::LargestFree, the manager's Allocate/Free/Push/Pop/AddHeap/
          DeleteHeap and the park manager's calls stood in; 14 scenarios crossing every comparison of GetMemoryStats
          (fragmentation at exactly the pad, a free amount a float can't hold, a little free), PiecesFit at its exact
          limits, the pools / Init / ShutDown for both levels. 42 deliberate bugs caught (7 after strengthening);
          1 equivalent (fragmentation `<` vs `<=`: the other two conditions exclude equality).
        - tools/ghidra/audit_game_function_calls.py: Ed::GridDims (6 bytes) counts 8 on the stack.
      - [x] Batch lfx-c 3 (NOT run in the game yet): the park editor's manager and cursor, first part -
        src/game/park/park_editor.cpp (8 functions, all REPLACE). Ed::GridDims (x, signed y, z, width, height,
        length; 6 bytes, 8 on the stack), the grid of 58 x 58 16-byte cells at manager +0x352C (+0 the floor height),
        the piece sets (0x154 bytes from +0x1076C: name checksum, name, 40 {type, text name}, count, selected, visible,
        clipboard set).
        - CCursor::Destroy 0x0059C900 (static: drops its piece, ~CCursor 0x005988A0, freed), ChangePieceInSet
          0x0059C950 (wraps; clipboard sets select a clipboard), ChangeSet 0x0059C9B0 (the next visible set; the
          menu number = visible sets before it; a clipboard set walks to its clipboard, drops the piece, mode 5).
        - CParkManager::RebuildPark 0x0059CB10 (new park 0x1A4E0EF9 or not loaded: empty grid, "unnamed park";
          else from the saved map; flag 0x0067F398 off around the floor's rebuild), RebuildArea 0x0059CC70 (with
          checkMemory: false when the park editor singleton 0x00590160 says it's full), DestroyMetasInArea 0x0059CD20
          (not the floor's, optionally not restart points / only wholly inside / not locked; the scene updated over
          the area: corner + size floats), ClearArea 0x0059CEC0 (flattens: pieces moved down by their height, the
          floor set to 0 under them and in the area), ChangeFloorHeight 0x0059D080 (undone if out of memory).
        - Test: TestParkEditor (src/hook/selftests_lfxc2.cpp, Early): 12 variants of manager / cursor / pieces /
          clipboards, every call stood in for. 39 deliberate bugs caught (none needed strengthening); 1 equivalent
          (the float sum of the corner and size: rounding through double then float is the same as float addition).
        - (A mutation run once showed "SELFTEST FAIL: PC async handle (synchronous I/O)" - not lfx-c's test; it
          passed on rerun: probably timing-dependent.)
      - [x] Batch lfx-c 4 (NOT run in the game yet): the park editor's cursor and the manager's set-up -
        src/game/park/park_cursor.cpp (11 functions, all REPLACE). The gap descriptor (0x54 bytes at cursor +0x70:
        two halves' cells, turns, left / right extents, name, score, halves done, id; the name counter at +0xC4).
        - CCursor::RemoveClipboardsTooBig 0x0059D120 (clipboards wider or longer than the park are unlinked,
          ~CClipboard 0x0058C980, freed through CClipboard's pools), PlacePiece 0x0059D240 (fits, height <= 17, a
          restart point needs a free slot of its type - "No free slots exist for this type of restart point"; a copy of
          the type turned and added with the bottom-up heap as default), DestroyUnderCursor 0x0059D380, AddGapHalf
          0x0059D410 ("gap", "gap2"...; prints the area with 0x00590210), RemoveGapHalf 0x0059D600 (pieces with flag
          0x1000 looked at again), ChangeGap 0x0059D6B0 (turn resets the extents; extents 0-6 kept inside the park,
          by the turn: 0/2 along z, 1/3 along x), DestroySelection / FlattenSelection 0x0059D8E0 / 0x0059D980,
          RaiseArea 0x0059D9F0 (at most 10 x 10), RaiseLower 0x0059DAA0 (the area widened by the pieces standing on it
          - any height: the query's height is 16 -, the highest setting the top; not above 17 or below -16; the
          original writes FloorHeight's level flag over its copy of the park's size).
        - CParkManager::Initialize 0x0059DCD0 (once: Init, the park's map object 0xA8 bytes 0x0058CCC0 - in the
          top-down heap for level 0xCD3682C1 -, generator set-up, RebuildPark(0), and the piece sets: each set's
          pieces run from its `first` in Ed_standard_metapieces to the next set's (processed last to first); a
          clipboard set gets Clipboard0-5; hidden sets aren't visible). As in the original, a set whose pieces
          aren't all in the manager's list reads a null piece.
        - Test: TestParkCursor (src/hook/selftests_lfxc3.cpp, Early): 14 variants (and 36 gap changes per variant),
          the clipboard pools, hand-built Ed_piece_sets / Ed_standard_metapieces structures, an empty symbol table;
          every call stood in for (cdecl stand-ins don't record ECX; pointers to locals are recorded as one marker).
          59 deliberate bugs caught, 10 after strengthening (exact height / DMA-slot / -16 limits, a failed AddGap,
          gaps reaching the park's edge along x, the cursor's +0x55 flag set beforehand, dirty piece sets).
        - Pitfall found: a crash inside an original with its own SEH frame (here Initialize, reading a flag through a
          null symbol table) kills thug2_check outright (the game's frame handler runs first, without its CRT) instead
          of being logged as a SELFTEST CRASH.
- [x] Batch 107: "Movies (Bink) & streamed audio" (0x44E6B0-0x4544E0) and "Level nodes & composite objects"
      (0x45BDA0-0x45EFD0) finished by four agents in parallel (worktrees, the shared game lock). The movie range is
      mostly not movies: the input manager, the game clock, the module manager, the music front end, playlists,
      streams and the PC's Bink-audio stream platform layer. Merge fixes by the lead: Sfx::sVolume is 0x2C bytes
      (mv-b had 0x18: a run-time check failure in the game); mv-b's and mv-c's same-named files kept as
      stream_commands.* / bink_stream_files.cpp; same-named functions told apart (Pcm::PlayTrackByName 0x450820,
      Pcm::InitPlatform 0x453670, Pcm::PlatformStopMusic 0x453AA0); the nodes test no longer depends on an unset
      stack value. Also: PlayMovieFile waits and pumps window messages while the device is lost (the original spins
      forever - hung with another full-screen program up). Tested in the game (merged): LA loads and plays with
      music, 1719 replacements installed, all 202 early / 17 late / 2 engine-started self-tests pass. The records:
      ### 0x44E6B0-0x450F70 (mv-a)
      # mv-a: 0x44E6B0-0x450F70 (the start of "Movies (Bink) & streamed audio")

      The range turned out to hold four things: the input manager (Inp), the game clock, the module manager (Mdl) and
      the music/streams front end (Pcm) - the movie code itself (0x44F830-0x450220) was already done.

      ## Classes defined here (full layouts)
      - `Inp::RecordedData`, `Inp::Data`, `Inp::Server`, `Inp::Manager` (src/game/sio/inpman.h) - the constructor and most
        methods are in this range (0x44E4F0-0x44E660, outside it, stay GameFunction calls: the manager's destructor
        0x44E530, the server's 0x44E520, the task 0x44E660, the getters/setters 0x44E590-0x44E640).
        front_end.h's `InputControlData` is the first part of `Inp::Data` (left as it is).
      - `Mdl::Module`, `Mdl::Manager`, `Mdl::ModuleListDestructors` (src/game/sys/module.h). Mdl::FrontEnd and
        File::AsyncLoaderModule still spell the module's fields as bytes (left as they are).
      - `Pcm::FamData` (src/game/pcm/music_api.h). Stream fields used by music_api.cpp are a local struct
        (`StreamFields`, anonymous namespace) so music.h's `Pcm::Stream` isn't changed.

      ## Batches
      - [x] mv-a batch 1 (NOT run in the game yet): the input manager and the game clock - 13 functions, all REPLACE.
        - Inp::Data (0x44E840 MaskDigitalInput, 0x44E860 MaskAnalogInput, 0x44E8C0 ConvertDigitalToAnalog (table at
          0x67D730), 0x44E900 HandleAnalogTolerance (dead zone 50), 0x44EA30 OverridePadWithStick (the left stick
          works the d-pad; quirk kept: up's pressure is y + 0x80, growing towards the centre)); Inp::Server (0x44E9E0
          constructor, 0x44EAA0 ServiceHandlers: playback of a recording, else the device's PS2 control data by pad
          type 4/5/7, recording, the per-device button words 0x696D48/50/58, the handlers); Inp::Manager (0x44E700
          constructor; 0x44E7F0 / 0x44E6B0 the singleton).
        - The game clock (src/game/sys/game_clock): 0x44ECE0 GameClockElapsed (235 callers; the unsigned real time since
          the last change times the speed, _ftol truncation), 0x44ED10 PauseGameClock, 0x44ED70 SetGameClockSpeed.
        - Test (selftests_mva.cpp: TestInpManager, TestGameClock): 43 deliberate bugs, 42 caught (4 after splitting the
          manager's constructor test from the singleton's, whose stand-in had covered our constructor). cre1
          (`Instance() = nullptr` dropped on the out-of-memory path) is equivalent: only reached when it's null.

      - [x] mv-a batch 2 (run in the game: see below): the module manager (src/game/sys/module) - 17 functions, all REPLACE.
        Mdl::Module (0x44F7E0 constructor - m_command left unset, as the original; 0x44F790 / 0x44F7B0 destructors),
        Mdl::Manager (0x44F5E0 constructor: the list head and its task, priority -1000; 0x44F680 destructor;
        0x44F3C0 RegisterModule (onto the end); 0x44F3E0 UnregisterModule (`this` unused); 0x44F310 StartModule (not
        when locked); 0x44F530 StopAllModules; 0x44F440 ProcessModules, the task: start / stop / restart by command and
        state), the singleton 0x44F6F0 / 0x44F740, and the module list's Lst destructors (Node 0x44F360 / 0x44F380,
        Head 0x44F410, Search 0x44F300 / 0x44F330). singleton.cpp's SingletonPtr instances for this and the input
        manager now call our Create/Release (selftests_gapsB's table updated to match).
        Test (selftests_mva2.cpp: TestModules): 25 deliberate bugs, 25 caught.
      - [x] mv-a batch 3 (run in the game: see below): the music and streams front end (src/game/pcm/music_api) - 22
        functions, all REPLACE: NoMusicPlease 0x4505F0, StreamsDisabled 0x450620, StreamExists 0x450500,
        Get/SetCurrentTrack 0x450520 / 0x450530, StopMusic 0x450650, PlayTrack 0x450820 (the user soundtrack's next
        track), PlayMusicStream 0x450EF0 (its name unused on PC), PreLoadMusicStream 0x4509E0,
        PreLoadMusicStreamDone 0x450A50, StartPreLoadedMusicStream 0x450A80, StopStreams 0x450690, StopStream 0x4506D0,
        SetStreamVolume 0x450780, SetStreamPitch 0x4507D0 (a stub on PC), PreLoadStreamDone 0x450940,
        StartPreLoadedStream 0x450980, RemoveStream 0x450AE0 (pops the stream's queue head), UpdateStreamVolume
        0x450BE0 (positional streams; quirk kept: the playing check is of stream `slot`, not the one found), Init
        0x450D60 (quirk kept: only the first two of the three playlists emptied), UsingCD 0x450EB0, FamData::Load
        0x450540 (fam\<hex>.fam lip-sync data, < 4 KB).
        Test (selftests_mva3.cpp: TestMusicApi): 400 seeded cases over every function with the whole platform layer
        stood in (2200 seeded cases; extra ones for PlayTrack's user soundtrack and RemoveStream's queue): 40 deliberate
        bugs, 39 caught (8 after adding those cases and steering UpdateStreamVolume's owner ids onto streams). Not
        caught offline: SfxVolume back at 0x18 bytes (the overrun found in the game; the static_assert keeps it).

      ## In-game findings
      - First in-game run (batches 1-3, before commit): crash in Pcm::UpdateStreamVolume (0x450BE0) in the front end -
        Sfx::sVolume is 0x2C bytes, not 0x18 (0x0047BE10 also writes a direction at +0x18 and a float at +0x28), so our
        smaller local was overrun. Fixed (SfxVolume 0x2C). Offline tests couldn't see it (the stand-in for 0x0047BE10
        doesn't write the volume).
      - After the fix: intros skipped - starts, front end, a level loads and plays (music track shown), pause menu; log
        clean (no exceptions, run-time checks or self-test failures).
      - Intros on (skip_intro_movies=0): the game hangs in the first intro movie, the main thread spinning (1 s CPU per s,
        not responding). Sampled with SuspendThread/GetThreadContext: it's in Nx::PlayMovieFile's loop
        `if (DeviceLost()) { ResetDeviceIfLost(); if (DeviceLost()) continue; }` (movie.cpp, 0x0044FDC0 - not this
        range). The original does the same (0x00450053-0x00450066: no Sleep, no window events while the device is lost),
        so once the D3D device is lost during a movie (here: the game started while another full-screen program had the
        display) nothing restores it. A quirk of the original; worth a Sleep + ProcessWindowEvents there as a fix.
        Not caused by this range's code.
      - Final DLL (all three batches + the Sio::Manager change), intros skipped: starts, a level loads and plays (music
        "25 Ta Life" shown), pause menu; log clean. Tested in the game 2026-10-18.
      - Intros on, same DLL, then the same DLL with all 52 of this range's replacements disabled by the ini
        (inpman/game_clock/module/music_api.cpp): both times the game never shows a window and spins one core
        (CPU 1 s per s, "responding") once the intro movies start. So it isn't this range's code. The earlier sample
        put the main thread in PlayMovieFile's device-lost retry loop. That's for whoever owns movie.cpp (0x44FDC0).
      ### 0x450F70-0x452D20 (mv-b)
      # mv-b: Movies (Bink) & streamed audio, 0x450F70-0x452D20

      ## Batch mv-b-1 (NOT run in the game yet): the playlist and music track commands (src/game/pcm/music.cpp)
      - 0x450F70 PauseMusic, 0x4510B0 SetMusicVolume, 0x451130 SetMusicStreamVolume, 0x4511A0 AddTrackToPlaylist,
        0x4512D0 ClearPlaylist, 0x451350 SkipMusicTrack, 0x451380 SetRandomMode, 0x451400 ApplyTrackVolume,
        0x451450 IsTrackPlaying, 0x4514C0 GetNumTracks, 0x451500 GetTrackName, 0x4515A0 SetMusicMode,
        0x4515E0 LoadMusicHeader, 0x451C70 ChangeTrackState, 0x4520D0 PlayTrack - 15 REPLACE. The script commands of
        the same names (0x44D4D0... in cfuncs.csv) call them.
      - Findings: 0x0069BAAC is what the music's channel plays (0 a track, 1 a music stream - 0x450EF0, 2 - 0x4509E0);
        0x0067D7E8 (GetSfxVolume's) is really the music *stream* volume; 0x0069BAA1 is set by the music's init
        (0x450D60). PauseMusic(-1) unpauses only if the music wasn't already paused when last paused (0x0069CAD0).
        Playlist 0's titles are one table by track index at 0x6999F8 (100 chars, "No title provided"). GetTrackName
        with the music off writes "Empty." over playlist 0's first track. Original quirk kept: ChangeTrackState's
        "any track on?" test checks every track against both masks, so only an empty playlist gets marked "nothing to
        play" (unknownCA8). Script::GetInteger(0xD7BB618D) is read and ignored (compiled-out debug print). The
        0x005B9760 `return true` stub is the PS2's file check (AddTrack, LoadMusicHeader).
      - Test: TestPlaylistCommands (src/hook/selftests_mvb.cpp), original vs ours on stand-ins for the platform music
        calls. 40 deliberate bugs caught, 4 after strengthening (title terminator, the "no track" flag, a track one
        past the count, "Empty." length).

      ## Batch mv-b-2 (NOT run in the game yet): the streams and the PC's Bink stream files
      - src/game/pcm/stream.cpp: 0x451010 PauseStream, 0x451620 LoadStreamHeader, 0x451660 IsStreamPlaying,
        0x4516A0 LoadStreamFrameAmp, 0x4516E0 GetStreamFrameAmp, 0x451720 FreeStreamFrameAmp, 0x451770 StartStream,
        0x451960 PlayStream, 0x451A50 PreLoadStream, 0x451B80 AddStream, 0x451E20 PlayStreamFromObject (11 REPLACE)
        + 0x451E7D REPLACE_SOURCE_ONLY (not a function: Ghidra split 0x451E20's body there).
      - src/game/pcm/bink_stream.cpp: 0x452A90 LoadStreamFile (12-slot cache of stream files read into memory with the
        game's C runtime), 0x452C00 BinkStreamView::Construct, 0x452C60 BinkStreamView::Open (BinkOpen from memory,
        video off; BinkSetSoundSystem(DirectSound) once) - 3 REPLACE.
      - Findings: Pcm::Stream's fields filled in (volume, dropoff, priority, option, nameCount, startFrame, usePosInfo,
        dropoffFunction, names[8]; `listener` is the stream's FrameAmp slot). A new stream takes a free channel or the
        one with the lowest priority / oldest start (unsigned frames) if the new priority is at least that. PlayStream
        callers push a 7th argument that's ignored; StreamIdFinished's answer is ignored. 0x0069BAAA blocks new streams
        (nothing sets it). AddStream has no bound on its 8 names (original quirk). PC streams are audio-only Bink files.
      - Tests: TestStreamCommands, TestBinkStreamFiles (selftests_mvb.cpp). 58 deliberate bugs: 54 caught (6 after
        strengthening: equal start frames, usePosInfo cleared, name counts kept in bounds, equal LRU ages); 4 equivalent:
        ChannelStatus != 2 vs == 1 (0x453270 returns only 1 or 2), the start frame as int (compared unsigned anyway),
        SetSilent after the sVolume constructor (it already zeroes), the volume scale's association (same float).

      ## Classes / types
      - Pcm::Stream (music.h, existed): filled in its unknown fields - check against mv-a if it also did (0x450BE0 reads them).
      - Pcm::FrameAmp (stream.h, new): the frame-amplitude slot {checksum, size, data[0x1000]} - 3 of its 4 functions are
        mine (0x450540, its load, is mv-a's and is called by address).
      - Pcm::BinkStreamView (stream.h, new): only the Bink stream's constructor and Open, over offsets (no layout). The
        class (0x1C0 bytes, made at 0x453500) has its other methods in mv-c's range: if mv-c defines it, fold these two
        methods in there and delete BinkStreamView.
      ### 0x452D20-0x4544E0 (mv-c)
      # mv-c: Movies (Bink) & streamed audio, 0x452D20-0x4544E0

      The PC's streamed-audio platform layer: the music and three sound streams, each a Bink file played for its sound
      (binkw32 through DirectSound), serviced by a thread every 5 ms under a mutex (0x006EE01C). The console versions'
      raw pcm.wad stream class (WadStream) is still created in places but its playing is compiled out on PC (its update,
      volume and "ready" calls are linker-folded stubs; nothing opens one with a real file).

      ## Classes defined (src/game/pcm/stream.h)
      - `Pcm::WadStream` (0xA8): constructor 0x454340 and all its own methods are in this range.
      - `Pcm::BinkStream` (0x1C0): 6 of its 8 methods are here (Open, Pause, SetVolume, SetVolumeAndPan, the
        destructor, Update); its constructor 0x452C00 and OpenFromWad 0x452C60 are mv-b's - called by address here.
      - `Pcm::StreamSlot` (0x14; the music's at 0x6EDFF4, three streams' at 0x6EDFB8), `Pcm::WadEntry` (pcm.dat's
        index entries, 12 bytes), `Pcm::StreamVolume` (5 levels + a type).
      - `Nx::Bink` (movie.h): three header fields added (lastFrameNum, frameRate, frameRateDiv) - additive.

      ## Batches
      - [x] Batch mv-c-1 (run in the game: see below): the stream classes, src/game/pcm/bink_stream.cpp.
        BinkStream: Open 0x452D20 (Bink's DirectSound set up once - "Bink can't use DSound" - I/O size 0x40000, BinkOpen
        with BINKIOSIZE, video off, path kept for reopening; E_INVALIDARG if it won't open), Pause 0x452DC0 (a request
        the thread carries out; a lock-out flag 0x69CB98 that's never set), SetVolume 0x452DE0 (0-100 -> 0-32768 by
        327.68; BinkSetVolume even with no Bink), SetVolumeAndPan 0x452FC0 (volume = mean; level of the louder side,
        capped 32768; pan = right/(left+right) * 65536 with the float-rounded total), the destructor 0x452E30
        (BinkClose even of null, the file closed unless shared, the pcm.wad Bink entry's users counted down and last
        use noted), Update 0x452EA0 (thread: pause = close the Bink noting the frame, resume = reopen and BinkGoto;
        frames decoded while the window has focus; plays counted down at the last frame, finished at 0).
        WadStream: constructor 0x454340, StartRead 0x454370 (overlapped ReadFile of 8 KB; ERROR_IO_PENDING = 0,
        errors <= 0 returned raw, else HRESULT_FROM_WIN32), OpenFromWad 0x454450, Open 0x454490 (an invalid handle:
        HRESULT_FROM_WIN32(GetLastError()) with GetLastError called twice), PreloadDone 0x4543F0 (false unless
        preloading), Pause 0x454430, SetChannelVolumes 0x454440 (empty RET 0x14, also used for BinkStreams; source-only).
        Tests (src/hook/selftests_pcmstream.cpp, TestBinkStream / TestWadStream): original vs ours against stand-ins
        for binkw32's import slots, CloseHandle/ReadFile/GetLastError, Dbg::Printf and Mem free; 43 deliberate bugs
        caught, 9 of them after strengthening (volumes with fractions >= .5, a level of exactly 0x8001, a total not
        representable as a float, wad entry 0, an error above 0xFFFF, a non-preloading state with the read done,
        a negative pause).

      - [x] Batch mv-c-2 (run in the game: see below): the slots and the platform layer, src/game/pcm/stream.cpp.
        StreamSlot::Create 0x453500 (new WadStream / BinkStream), Open 0x453090 (on a pcm.wad entry; -1 with no
        stream), SetVolumeAndPan 0x4530E0, Free 0x4538F0; StreamThread 0x453100 (every 5 ms under the lock: each
        stream's Update; skipped on WAIT_TIMEOUT), StopStreamThread 0x4531A0 (the handle isn't closed),
        NewOverlapped 0x4535E0 (a ring of 256; loops for ever if all are in use), Init 0x453670 (slots cleared, user
        soundtracks counted, pcm.wad opened overlapped + unbuffered, pcm.dat's index read and exchange-sorted by
        checksum, the thread started above normal priority), FindWadEntry 0x4533A0 (EBX; binary search; naked stub),
        StreamExists 0x4533E0, MusicStatus 0x453250 / StreamStatus 0x453270 (1 free, 2 playing; -1 = any of the
        three), MusicPreloadDone 0x453220 / StreamPreloadDone 0x4531D0, PauseStreams 0x4532C0, UpdateStreams
        0x453940 (frees what finished - unless E_PENDING - or was asked to stop), UserSoundtrackCount 0x4542D0 /
        Name 0x454300 / Tracks 0x454320 (none on PC).
      - [x] Batch mv-c-3 (run in the game: see below): playing, src/game/pcm/stream_play.cpp.
        LoadMusicTrack 0x453C20 (<root>data\streams\music\%.8x.bik as a Bink stream), LoadMusicTrackByName
        0x453D70, PlayMusicTrack 0x453DB0, StartPreloadedMusic 0x453BF0, PlayUserTrack 0x453850 (always false on PC,
        leaving a WadStream in the music slot), StopMusic 0x453AA0, SetMusicVolume 0x453400, SyncMusicToTime 0x453470
        (cutscenes: BinkGoto when more than 5 frames off; assumes a Bink stream with a Bink open), SetStreamVolume
        0x453DF0 (levels x the sound effects volume / 100; 5 channels or left/right), LoadStream 0x454080, PlayStream
        0x454200, StartPreloadedStream 0x454270, StopStream 0x453B30, StopStreams 0x453BD0.
        Tests (src/hook/selftests_pcmplay.cpp, TestStreamPlatform / TestStreamPlay): original vs ours against stand-ins
        for the stream classes' methods (both addresses), the memory manager, Win32 (mutex, Sleep, files, thread),
        binkw32's BinkGoto slot, the sound manager and the C runtime's sprintf / strrchr. Batches 2 and 3: 77 deliberate
        bugs, 76 caught (14 after strengthening: both classes in one slot, flags differing between them, soundtrack
        index = count, a positive open error, the constructors' start flags, a frame exactly 5 off, 60 random
        five-channel volumes for the mean's rounding); 1 equivalent (the stream buffer's address: Bink streams don't
        use it, and LoadStream only makes Bink streams).

      - [x] Batches mv-c-1..3 tested in the game (worktree build, under the lock): self-tests all pass (194 early, 17 late,
        2 engine-started), 1597/1597 replacements installed; Free Skate into a level, the pause menu (music paused) with
        the playing music track shown ("25 Ta Life - Over the Years": a Bink music stream loaded by LoadMusicTrack and
        run by the stream thread); a second run with intro movies on (Escape pressed through them) into a level. No
        crashes, debug-CRT reports or run-time check failures in thug2-decomp.log.

      ## Findings
      - 0x5B9760 / 0x5BA1D0 / 0x5BAB90 / 0x48DCD0 / 0x454440 are the stream classes' linker-folded stubs on PC: they're
        written as inline members (ReadyToFree / PreloadDone true, Update 0, the volume calls empty).
      - 0x8000000A (E_PENDING) in a stream's status word keeps a finished stream from being freed.
      - 0x453100 (the stream thread's function) isn't a function in Ghidra (only referenced by CreateThread's argument).
      ### 0x45BDA0-0x45EFD0 (nodes)
      # Agent "nodes": Level nodes & composite objects, 0x45BDA0-0x45EFD0

      Range finished: the 41 functions not yet rewritten, all with REPLACE (none source-only).

      - [x] Batch nodes-1 (NOT run in the game yet when committed; see below): SkateScript's node functions, the node
        name hash, the prefix tables, the scan of scripts for calls, Obj_GetCollision.
        - **src/game/script/nodes.cpp / nodes.h** (new, 25): a node's name / links / position / angles / orientation by
          its structure (0x45BE40 GetNodeName, 0x45C020 GetLinkCount, 0x45BE10 GetLink, 0x45C420 GetLinkedNode,
          0x45C4B0 IsLinkedTo, 0x45C050 GetPosition, 0x45C180 GetAngles, 0x45C0B0 GetOrientation) and by name
          (0x45C1B0 GetNode, 0x45BE70 NodeExists, 0x45C3A0, 0x45C3E0, 0x45C460, 0x45C560, 0x45C590, 0x45C520);
          0x45C2D0 GetNearestNodeByPrefix; the instance wrappers 0x45C1D0-0x45C230 (walk, last node array); 0x45C950
          ForEachScriptCall and 0x45CCA0 ForEachNodeScriptCall (the gap list, 0x578AC0, finds every EndGap call in the
          nodes' TriggerScript= this way).
        - **src/game/script/node_prefixes.cpp / .h** (new, 12): the nodes by name prefix: PrefixNodes::Add 0x45BDA0,
          PrefixTable (Construct 0x45C5D0, Destruct 0x45CD30, Add 0x45C650, Compact 0x45C240), PrefixTables (instance
          0x6F0D80: AddNodeArray 0x45C700, RemoveNodeArray 0x45CDF0, GetNodes 0x45BE90), 0x45C900 AddNodeArrayPrefixes
          (on the script heap), 0x45CE40 RemoveNodeArrayPrefixes, 0x45BF90 / 0x45BFB0 GetPrefixedNodes (checksum / text).
        - **node_manager.cpp**: CNodeManager::HashNode 0x45CE60, UnhashNodeArray 0x45CEF0, GetNode 0x45CFC0; the
          manager's own calls to these and to the node / prefix functions are now direct.
        - **composite.cpp**: CCompositeObject::Obj_GetCollision 0x45E9E0 (member function 0x0168B09C).
        - Findings: nodes of class=link (0xC953660E) aren't hashed. A node name's prefixes are the text before each '_'
          (except "TRG", case-insensitive) and the name without trailing digits; "X_" and "X" both become known checksum
          names. GetNodes' static buffer (0x6EFDE0, 1000 names) and the 100- / 1000-byte name buffers aren't bounds
          checked (kept). GetLink reads a null links array if a node has none (kept). GetNearestNodeByPrefix doesn't reset
          its position between nodes: a node without pos= is measured where the previous one was (the first such node:
          ours uses the origin, the original stack garbage). The scan follows script calls, SetEventHandler's scr= and
          EndGap's gapscript= to depth 5; names that aren't calls are passed over alone, so their arguments are scanned
          as statements. Obj_GetCollision: a feeler from pos + up*height + at*distance + right*shift (feet, height and
          length default 3) along right (side) / down (down) / at; returns whether it hit (several sums kept at 53 bits:
          written out in double).
        - Names recovered from the .qb files' checksum tables: link, EndGap, gapscript, shift, objectDistance.
        - Test: src/hook/selftests_nodes.cpp (TestNodes, Early): four original-vs-ours runs on stand-ins (hash with
          collisions and wrap-round, lookups, links, positions, quaternions incl. clamping; prefix tables over three node
          arrays, growth, removal from the middle, a chained bucket; the scan over a script exercising every token case,
          the depth limit, the node walk; Obj_GetCollision's seven parameter sets). 71 deliberate bugs: 65 caught at
          first, 5 more after strengthening the test data; 1 equivalent (an empty name text vs a null one in
          AddNodeArray: the loop doesn't run either way). selftests_cobj.cpp's stand-ins now also cover our versions of the
          functions the node manager calls directly.
        - Checks: 0 MISMATCHED argument sizes, all early self-tests pass, register scan (every function whose result is
          read returns one), audit of GameFunction calls (no MISMATCH), check_code_writes (no overlaps), find_gaps
          (nothing left in the range, 0 thunks).

      ## Classes defined
      - SkateScript::PrefixNodes, PrefixTable, PrefixTables (node_prefixes.h; all their code is in this range).
      - No new layout for Nx's collision feeler: composite.cpp uses a 0xB0-byte buffer with offset accessors
        (CollisionFeeler, anonymous namespace), as park_floor.cpp / level_editor.cpp do.
      - Extended (additively): SkateScript::CNodeManager (three methods, NodeManagerInstance()), Obj::CCompositeObject
        (Obj_GetCollision).
- [x] Batch 108: THPS4's levels in THUG2 (tools/thps4_levels, agent thps4): the 11 THPS4 levels with a scene
      convert (scene, collision with computed face reach bytes and BSP, textures, node arrays, kill zones) and load
      and skate in the game from the level select; grinding not confirmed by hand yet. The record:
      ### THPS4 levels (thps4)
      # thps4 - THPS4 levels in THUG2 (tools/thps4_levels)

      Agent "thps4", branch worktree-agent-a581fc09e82f1f3e3. How to run it: tools/thps4_levels/README.md.

      ## Result

      All 11 THPS4 levels with a scene convert and were run in the game from the level select (signs after SKATE SHOP):
      Alcatraz, Carnival, Chicago (Hoffman), Shipyard (Jnk), Kona, London, San Francisco, College (Sch), Zoo, the
      THPS4 skate shop, and Motox, each with its sky (Motox uses Hoffman's). For each: the level loads with no errors in
      thug2-decomp.log, the skater starts at THPS4's Player1 restart, skates (screenshots after a few pushes), textures
      right, skies right. The game-run check is short (load, push a few times). Grinding is NOT confirmed: two tries
      with scripted key taps (a test restart moved next to a Kona rail, ollie + grind key) didn't land a grind - with
      300 ms taps that proves little either way; it needs a person with a pad. Kill zones not tried. The overlay keeps the
      converted levels installed (screenshots: scratchpad t4\shots\<lvl>-step*.png).

      ## Formats (checked against THUG2's loaders and its files)

      ### THUG2 scene (.scn.xbx) - loader 0x004B36D0, sectors 0x004C6B10, meshes 0x004D6100
      - u32 x3 versions (1, 1, 1), materials (0x004D3390), sectors, i32 hierarchy count + 0x50 bytes each.
      - Per mesh: sphere, box, u32 flags (0x400 -> no skater shadow, 0x20000 on every shipped mesh), material,
        u32 LOD count + per LOD {u32 n, u16 x n} - **read and thrown away** (0x004C6B10 frees them); then
        **8 x {u16 n, u16 x n}** index buffers (the "14 bytes of padding" in io_thps_scene are the 7 empty ones);
        u8 stride, u16 vertex count, **u8** buffer count, per buffer {u8 flag (nonzero: 0x004D4350 converts it),
        u32 size, data}; 2 x u32 vertex shader (0..6 built-in, else an FVF); 3 bytes (normal / colour / uv offsets);
        u8 colour wibble flag (+ a byte per vertex); u32 index sets (> 1: a u32 each); u32 pixel shader:
        1 = {u32, u32 size, data} **skipped** (0x004DA6D0) and the shader made from the material at run time
        (0x004DDC00) - used for 2+ passes; 2..18 = built-in shaders (0x004D3ED0; 2 textured with vertex alpha, 3 textured
        with the pass's fixed alpha (IGNORE_VERTEX_ALPHA), 4 untextured - the ps.1.1 sources are in the exe).
      - Vertex colours: 128 = full (the shaders multiply by 4 / 2).

      ### THUG2 textures (.tex.xbx) - 0x004CAE90
      u32 version 1, count; per texture checksum, w, h, levels, texel depth, palette depth, DXT (1/2 -> DXT1, 5 -> DXT5;
      0: 8 -> P8, 16 -> A1R5G5B5, 32 -> A8R8G8B8), palette size (non-zero: a separate path, made 32-bit), per level
      {u32 size, data}. THPS4 PC's tex.dat is the same format; its 8-bit paletted textures are **Xbox-swizzled**
      (Morton order) - converted to 32-bit, unswizzled.

      ### THUG2 collision (.col.xbx, version 10) - 0x0049CBB0, 0x004169C0, 0x00410BC0
      Layout in tools/thps4_levels/thug2_col.py. Findings beyond io_thps_scene:
      - Float vertices are all stored before the fixed-point ones (offsets per object say where).
      - **The byte after a small face's 3 indices, and the array of a byte per large face after the faces, are not
        padding**: they are each face's "reach" - ceil(distance of its farthest vertex from the object's box minimum x the
        object's scale), where the scale (the object's last u32, a float: io_thps_scene writes 0 "padding") is
        ~254 / the farthest vertex. The ray test 0x00413B50 skips a face whose reach is below the ray's distance from the
        box minimum x scale. With zeros (what io_thps_scene's exporter writes) faces are found by the skater's downward
        test but miss the others: the skater lands in an "in air" pose, can't push. Verified by rebuilding LA's collision
        with this writer (own BSP, reaches): LA plays normally.
      - Empty objects (no faces) are kept, scale 1.0, an empty leaf.
      - BSP: a split word = axis | (point in sixteenths << 2), absolute coordinates; children side by side, written depth
        first; the leaves' face indices in node order (per object, from 0). Rebuilt for the THPS4 geometry.

      ### THPS4 scene (scn.dat), collision (col.dat v8), qb
      - Scene: as io_thps_scene describes (per-sector vertex arrays: positions, normals, weights, uv sets, colours,
        wibble indices; meshes = flags, material, strip); all levels parse to the end.
      - Collision v8: vertices 16 bytes (3f + 4 intensity bytes), first vertex is an index; faces 12 bytes; 20-byte BSP
        nodes (not used). Face flag 0x80 is the reverse of THUG2's (set on 80% of THPS4's faces); 0x2000 is on every face
        and 0x4000 / 0x8000 on some - none of the three in THUG2's files: dropped.
      - QB: the same tokens as THUG2's except the random tokens (0x2F/0x37/0x40/0x41 = u32 count + u32 offsets, no u16
        weights) - why NeverScript stops on THPS4 files ("Byte not recognised ... 0x2f"). qb4.py reads the globals,
        checksum names and script statements; node arrays are written out as NeverScript text and compiled for THUG2.
      - THPS4's node array is plain "NodeArray"; THUG2 wants <file name>_NodeArray. Position= is kept: THUG2's
        AddNodeArray turns it into Pos with z negated, as THPS4 did.

      ## Decisions
      - Level structures are made from THPS4's Levels.qb (name, sky, lighting; mod factors as THUG2's _lo / _hi pairs),
        level_number LevelNum_Default (as the skate shop), no pre files: the overlay serves the loose files
        (LoadScene / LoadCollision / NodeArrayManLoad read them through File / Pip, which check the overlay first).
      - Signs: level numbers 19, 20, 22..29, 31 (unused; no records - the info panel skips them as it does 18 / 21).
      - Kept nodes: Restart, RailNode, LevelGeometry, LevelObject (as LevelGeometry: THPS4 moves them by script), KOTH
        GenericNodes; keys Position, Angles, Name, Class, Type, RestartName, restart_types (added from Type if missing),
        TerrainType, Links (renumbered), Cluster, CollisionMode; flags CreatedAtStart, TrickObject, AbsentInNetGames,
        NetEnabled. Dropped: peds, vehicles, game objects (THPS4 models / goal pickups), waypoints, goal nodes, scripts.
      - Trigger faces: THUG2's collision trigger looks the node up by name and reads its TriggerScript unchecked
        (see make_menu_level.py), so trigger flags are removed except on kill zones: a THPS4 trigger script calling
        something with "kill" in its name (ALC_KillWater, *_KillSkater...) becomes SK3_KillSkater (or _Water), THUG2's own.
      - Skies: THPS4 lights them at 255 - halved for THUG2's 128 = full.
      - make_menu_level.py empties the overlay on each run, so it does the install: when <thug2>\thug2-decomp-thps4\
        levels.json exists it copies the levels in and adds their structures (both Levels.qb) and signs.

      ## Reference bugs (io_thps_scene; nothing of it copied - it has no licence)
      - Collision export: small faces' 4th byte and the large faces' byte array written as 0, the object's last float
        written 0 -> faces culled by THUG2's ray test (above). Ours computes them.
      - Scene import: reads "14 bytes of padding" and a u16 buffer count (really 7 empty index buffers, a u8 count and the
        first buffer's flag byte); the per-LOD index lists are inside the vertex loop (wrong for LOD > 1; unused anyway).
      - THPS4 paletted textures: its importer unswizzles (right); its exporter halves alpha for 32-bit textures (PS2 habit,
        not THUG2 PC's).
      - THUGPRO (thugpro-tools; only its public tools were looked at) wasn't needed: it ships no format code we used.

      ## Left
      - Grinding to be checked by hand (rails converted with links, TerrainType, Type, TrickObject Cluster); gaps, goals, peds, vehicles,
        moving objects (LevelObjects are static), THPS4's music and sounds (THUG2's terrain sounds come with level pres we
        don't load: check the log for missing sounds), net games (load_level uses <level>scn.pre there: not made).
      - Park editor sets (Sk4Ed*) not converted (their 32-bit textures may be swizzled too).
- [x] Batch 109: self-tests that failed only in the game, now fixed (agent flaky) - **tested in the game**: 4 start-ups
      in a row with all 202 early / 17 late / 2 engine-started tests passing. The tests turned any word inside a run's
      own arena into an arena offset, so an equal non-pointer value that happened to fall in the original's arena (the
      DLL moves with ASLR) mapped in one run only. Also: the level editor's P key logs the camera as a menu view.
      The record:
      # flaky: three early self-tests failing now and then in the game

      Three early self-tests passed every time offline (thug2_check) but failed in some game runs:

      1. "SkateScript nodes: prefix tables, prefixed nodes, the nearest by prefix" - entries 51, 59, 62, 177:
         original A001D122 / A003D122, ours 01DD6B72.
      2. "script loading: .qb files, redefinitions, restarts" - entry 5924: original A0051EA9, ours 7A0F10E9;
         entry 23237: original A00575CF, ours 7A0F680F.
      3. "script interpreter: Update and every statement token" - entry 4672: original A0000001, ours 02000001.

      ## Cause: the tests, not our code

      All three were the same bug in how the tests compare. Each test runs the original and ours in its own
      arena (`uint8_t arenas[2][size]`, in the DLL's .bss) and makes the results relative to the arena before
      comparing: a word in [the run's own arena] became 0xA0000000 + offset. Only the run's own arena was used, so a
      value that is **the same in both runs but isn't a pointer** (a checksum, flags, a word in the DLL) got mapped
      in the original's run when it happened to fall inside the original's arena, and stayed raw in ours. The DLL
      loads at a different address each game run (ASLR), and the arenas move with it, so this hit only some runs.
      Offline the DLL sits elsewhere, so it never did.

      The logged numbers prove it:
      - 1: 0x01DD6B72 is CRC("Ped_") - the checksum AddNodeArrayPrefixes gives AddChecksumName for "Ped_" (recorded
        four times). A001D122 and A003D122 put the original's arena at 0x01DB9A50 and 0x01D99A50 in those two runs:
        CRC("Ped_") was 0x1D122 / 0x3D122 into it. In the passing run the DLL was elsewhere.
      - 2: both entries give the same arena start: 0x7A0F10E9 - 0x51EA9 = 0x7A0F680F - 0x575CF = 0x7A0BF240. Two
        values the same in both runs, inside the original's arena that time.
      - 3: the arena image's "top half of a stale pointer" mapping (for uninitialised 16-bit fields) caught the flags
        word 0x02000001 when the original's arena started at 0x0200xxxx.

      ParseQB's NoteQbName (KeepQbNames) doesn't touch the arena (it uses the CRT heap); it played no part. The
      earlier change giving every node a pos= is still right (the original reads an unset stack value for the first
      node without one - undefined) but wasn't the cause of failure 1; its comment no longer claims it was.

      ## Fix

      `src/hook/selftest.h`: `ArenaWord(arenas, size, word)` and `ArenaWordOrHigh(...)` - pure functions of the word
      and the arena pair, so a value that is the same in both runs always maps the same way: a word in either arena
      becomes 0xA0000000 + its offset in that arena (pointers into a run's own arena map as before). The top-half
      mapping needs the pair aligned to 64K so that no 64K block straddles the two arenas (otherwise a stale top half
      from ours could land inside the original's arena as a whole word - seen offline while making this), so
      `ArenaPair<Size>` gives 64K-aligned storage. Used by the three tests:
      - selftests_nodes.cpp: `Rel` uses ArenaWord.
      - selftest.cpp TestScriptLoading and selftests_csB.cpp TestScriptInterpreter: `relocate` uses ArenaWord, the
        arena image ArenaWordOrHigh, arenas from ArenaPair.

      **Other tests have the same weakness** (about 45 other `0xA0000000u + (word - base)` sites in src/hook:
      selftest.cpp, selftests_cobj/coim/csA/csC/el3d/evt/gapsA/lfxc/netB/netF/obj/parkedA/pcmplay/qb/scache/
      skaterA/skaterB/trk). They can fail the same way when a constant they record lands in the original's arena;
      switch them to ArenaWord / ArenaPair if one ever flakes (or as a sweep).

      ## Proof

      - thug2_check: 0 MISMATCHED, early self-tests all passed (202).
      - Mutations (tools/mutate.py, spec in the session scratchpad):
        9 of 9 caught. Nodes test: the "Ped_"-style name given AddChecksumName ('_' -> '-'), PrefixNodes::Add's
        grown size (+8: every later bump pointer moves), the digits prefix's index (+1), GetNearestNodeByPrefix's
        `<` -> `<=`. Loading test: a script block's size word (+1), CreateScriptSymbol's file checksum (0), the block's
        allocation (+4 bytes: pointers move). Interpreter test: ExecuteBreak's depth check (crash in the test, counted
        as a failure), ReadName's 0x2C parameter name (0 -> 1).
      - In the game: NOT run with this build yet. The lead stopped game runs (the user was playing). The four runs
        made before then didn't use this build: the DLL copy failed because a game was already running, so they
        prove nothing. Still to do: 4 consecutive runs with all early, late and engine-started self-tests passing.
- [x] Batch 110 (TOD-1; time-of-day agent, merged 2026-10-19; run in the game: docs/pending/time-of-day.md; NOT judged by a player yet): the time-of-day manager, 0x00571140-0x00574910 - all 53 functions:
  Gfx::CTODManager (src/game/gfx/tod_manager.{h,cpp}; TODParams 0xA4 bytes, TODSystem 0x188, the manager 0x340 -
  full layouts), its 18 TODManager_* script commands, CreateTODManager / ReleaseTODManager (Sys::Main now calls them
  instead of the generic Create / Release by address), and 0x00572821 (ApplyColor's body after its first test, a
  function Ghidra made: source-only, ApplyColor's source).
  Findings: the day is two "systems" (system=1, and a sky system=2) of four states (morning -> afternoon -> evening
  -> night, a night ending counts a day); time passes only in career mode, unless tod_manager_inactive is 1, a
  temporary state (TODManager_SetTempTOD) is up, TODManager_SetActive 0 or the game is paused. While transitioning,
  each frame blends the two states (Lerp: integers truncated through doubles, floats in double, bloom colours byte
  by byte) and colours one part of one light group: (100 / parts)% of each scene's sector table (sectors of the
  group's light_group; reset_light_group's back to neutral 0x81808080), the level scene, and - for the outdoor group
  (0xE3714DC1 = "outdoor") or no group, on the first part only - the scenes' own colour and every model whose object
  has no sector of its name or an outdoor one. tod_manager_state_transition runs half-way through a transition
  (evening's at once), with level_specific_transition_script. Quirks kept: GetLevelColor / GetTODParams index past
  the end (system 2) when both are sky systems; SetTODInstant to a state that never comes loops forever; GetTODParams
  blends leave +0x14..+0x24 and fog_on unset in the original (stack garbage, ours: the "from" state's - fog_on is
  given back by GetCurrentTODParams); ResetLevelLights(state) reads its parameters into an uninitialised local (ours
  zeroed: only what the state gives is defined); SetSceneColor's colours' fourth byte is garbage (ours 0); the
  x87's own FSIN / FCOS for the light directions; RefreshAll's part size rounded to a float (UpdateSystem's isn't).
  Tests (src/hook/selftests_tod.cpp, stand-ins for every call out: structures, arrays, scripts, memory, engine):
  TestTODBlend (Lerp, GetTODParams, GetLevelColor, UpdateSun, ApplyTODParams, light directions, SetFogActive),
  TestTODStates (Construct, SetParams, ReadGroup, NextState, 90 frames of Update through states and transitions,
  SetTODInstant / AdvanceTODState, RefreshAll, ResetLevelLights, temporary states and bloom, EnableBloom, the other
  script commands, the dead objects, Reset, the destructor, the singleton), TestTODColor (ApplyColorToScene,
  ApplyColor, SetSceneColor on stand-in scenes, sectors and models). 64 deliberate bugs caught, 8 after
  strengthening (a bloom_on of 1, the manager after SetParams, a 128-sector level scene so a percentage off by one
  shows, temporary bloom over more frames, RefreshAll with the manager inactive, EnableBloom with bloom already off).
  What to check in the game: a career level with a day cycle (any THUG2 level - e.g. Boston), left in free skate for
  a few minutes: the sky, level light and fog should change through morning / afternoon / evening / night as before;
  TODManager_GetCurrentTODParams-driven menus (the pause menu's time of day, if shown).

- [x] Batch 111 (TOD-2; time-of-day agent, merged 2026-10-19; run in the game: docs/pending/time-of-day.md; NOT judged by a player yet): the victory conditions and the viewer module's pieces,
  0x00576610-0x005772E0 - 32 functions (6 Ghidra had missed: 0x005766E0 / 0x00576700 elimination,
  0x00576CB0 / 0x00576CF0 the viewer's script helpers, 0x00576F00 / 0x00577140 its handlers) plus 0x005772A0
  Mth::Matrix::RotateLocal(axis, angle). Game::CVictoryCondition and its 7 types (src/game/skate/victory_conditions.{h,cpp}:
  highest_score, target_score, complete_goals, best_combo, last_man_standing, elimination, collect_all_objects;
  CreateVictoryCondition, the scalar deleting destructors, IsHighestScore, ConditionComplete / IsWinner, the debug
  prints; 0x00576CA0 is a jump to complete_goals' ConditionComplete: source-only); Mdl::Viewer (src/game/sys/viewer.{h,cpp}:
  the input / shift handlers - buttons into command bits, sticks into velocities, the UserViewer* scripts -, the
  viewer object made from the bottom-up or debug heap, runnow.qb reloading, the script commands SetViewMode /
  SetMovementVelocity / SetRotateVelocity, MoveObjectLocal).
  Findings: target_score counts team totals in a team game, else each skater; complete_goals counts, per team or
  per skater, the goals that count (pointer word non-zero and not hidden) won by the skater; last_man_standing wins
  with a score above 0, elimination by not being eliminated (player +0x38); collect_all_objects needs one skater's
  score equal to every player's objects added up (0 objects: not complete). The viewer's input: R2 + select
  (0x200) sets command bit 0x4000.
  Tests (src/hook/selftests_tod2.cpp): TestVictoryConditions, TestViewer, TestMatrixRotateLocalAxis (the CRT's
  sin / cos patched to ours so both use the same). 39 deliberate bugs caught, 2 after strengthening (a skater
  above the objects' total for collect_all_objects; the manager's heaps told apart for the viewer object).
  What to check in the game: a network game mode's end (target score, elimination, scavenger hunt) and the
  viewer (debug builds only - not reachable in retail).

- [x] Batch 112 (TOD-3; time-of-day agent, merged 2026-10-19; run in the game: docs/pending/time-of-day.md; NOT judged by a player yet): the trick goals, 0x0056F090-0x00570F60, 0x005710B0-0x00571110 and
  0x00574960-0x00576520 - 49 functions (src/game/skate/trick_goals.{h,cpp}): Game::CTetrisGoal (Skatetris: 0x458
  bytes, 30 tricks of 0x20; constructor / destructors, Win, ClearTricks / ClearTricksAndMenu / Init, the combo /
  single combo / TrickTris variants, UpdateAlphas, Expire, Activate / Deactivate, RemoveTrick, AddTricks, CheckCombos,
  CheckTricks, Update), Game::CTimedScoreRunGoal (constructor, destructors, IsRunning), Game::CTrickBeatGoal
  (TrickBeat: 0x47C bytes; the beat pattern and tempo, the beat clock with its sounds and the trick window,
  TrickLanded / ClearLanded / PulseTricks / RemoveLandedTricks, AddTricks, Update), Game::CTrickObjectsGoal
  (CheckScore, GetObjectByCluster, ObjectHit). Only CGoal's first 0x70 bytes are described (GoalBase); CGoal itself
  (0x00550040-0x0056C920) isn't rewritten.
  Findings: a Skatetris trick comes every current_trick_time ms (shortened by acceleration_percent every
  acceleration_interval tricks); the stack turns red past three quarters of max_tricks and is lost when a new block
  would overflow it; key combos come from the skater's trick mapping (a special slot: the skater's own special),
  double taps through the trick's ExtraTricks; TrickTris adds blocks of tricktris_block_size until
  tricktris_total_to_win are done. TrickBeat's beat is 15000 / tempo ms (slowed by slow motion), each of the 16
  steps' sounds played on it, the window beat_window_time / 2 ms either side of a pulse step. Quirks kept: "Perfect "
  is written and then overwritten by the spin text; ClearTricks of TrickBeat stores free = true then false;
  GetObjectByCluster matches an entry without cluster_id against the last one read; RemoveTrick of TrickBeat reads
  two values it doesn't use; the beat's time compared as unsigned.
  Tests (src/hook/selftests_tod3.cpp): TestTetrisGoal, TestTrickBeatGoal, TestSmallGoals. 51 deliberate bugs caught,
  10 after strengthening (UpdateAlphas with no no_alpha_check given - our local now zeroed, as it's only read when
  given -, a stack exactly full and exactly three quarters full, the groups after CheckCombos, TrickTris at exactly
  its total, the trick interval reached exactly, a trick landed twice, the goal's words after AddTricks and the beat
  frames, the time limit with flag 0x10 and no time left).
  What to check in the game: a Skatetris goal (e.g. a career level's Skatetris), a TrickBeat goal, a trick objects
  goal (hitting the marked objects): tricks appear, turn red, disappear when done, and the goals win / lose as before.

- [x] Batch 113 (TOD-4; time-of-day agent, merged 2026-10-19; run in the game: docs/pending/time-of-day.md; NOT judged by a player yet): the skate module's network side, 0x0056C920-0x0056F090 - 12 functions
  (4 Ghidra had missed: 0x0056CDF0, 0x0056CEC0, 0x0056E380, 0x0056E470 - handlers it only saw as data) in
  src/game/skate/skate_net.{h,cpp} (Mdl::SkateNet: the module itself isn't rewritten): CreateNetworkTasks (the two
  Tsk::Task<Skate>s), StartServer (CFunc StartServer: the server app and its 36 handlers), AddClientHandlers
  (JoinServer: the client's 45 handlers - 48 for client 0 - and each own skater's 6), and the handlers HandleServerRemovedYou (0x51 /
  0x56), HandleScavengerHunt (0x91-0x95), HandleScavengerKillObject (0x90), HandleScavengerDropObjects (0x8F),
  HandleGraffitiColors (0x5C), HandleScores (0x40), HandleStartInfo (0x33), HandleRunScript (0x48),
  HandleObjectUpdates (0x23). The handlers they register outside the range (0x00569xxx-0x0056C8C0, 0x005xxxxx)
  are passed by address.
  Findings: the start info message (0x33) carries the network settings, level, game mode and its time / score limits
  (+0x20 / +0x24), the pro set geometry flags (+3) and the goal script variant (+0x4C: ElimiSkate's interval / break
  period, the scavenger hunt's interval / collect); victory_conditions are built from targetScoreArray /
  highestScoreArray. The scores message (0x40) is [0] a value for the game mode, [4] a checksum compared with the
  skate module's (notify_cheating), [8] the game's end, [9] count, then id + score per skater; in netfirefight a
  skater dropping to 0 is eliminated. Object updates (0x23) are a bit stream: a mask of 8 objects, then per object a
  16-bit time stamp, 9 field bits (x / y / z position as 16-bit halves - y quarters -, three angles in 1/4096,
  a state group of 4+6+1+1 bits, 5+3 bits, 32 bits), added to the skater's 20-entry state history (+0x27C). Quirks
  kept: the removed / moved-on dialogs are run without the structure made for them; with no local player the
  scavenger hunt's team message reads the name at address 0x38; the leader's LostGame goes to the leader's own
  skater; HandleStartInfo's goal script, when a type has no goal_script, is the skate module's address (the
  argument's slot reused); bit 7 of a 4-bit value (always 0) as the state entry's +0x74; graffiti colour arrays
  never freed.
  Tests (src/hook/selftests_tod4.cpp): TestSkateNetRegistration, TestSkateNetHandlers, TestSkateNetObjectUpdates
  (60 random bit-stream messages over 8 skaters, the histories compared). 64 deliberate bugs caught, 3 after
  strengthening (a skater present to be eliminated with a non-zero score; a single player; first updates without
  fields and the histories hashed after every message). The mutation builds
  needed the compiler / linker PDBs dropped each time (they got corrupt: C1033 / C2471 / LNK1201).
  The calls into code not rewritten go through a Call<R>(address, object, args...) helper, which
  audit_game_function_calls.py doesn't see: their argument sizes are checked by the tests instead (every one is
  stood in for with the original caller's stack size - a wrong size would unbalance the original's stack).
  What to check in the game: a network game (hosting a LAN / online game with a second instance -
  allow_multiple_instances=1 - joining): the level loads for both, scores and the leader messages show, other
  skaters move smoothly, the game ends with won / lost; a scavenger hunt / elimiskate / graffiti game if possible.
- [x] Batch 114 (FE-1; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): the animation controllers (new
      src/game/gfx/anim_controller.*), 0x4A4500-0x4A5730 - the range starts with Gfx code, not the front end: the
      controllers an object's animation blender stacks (made by the factory 0x00481FB0 from a structure's Type with
      its Priority; the base controller 0x00481710 / vtable 0x0064877C isn't rewritten). Vtable slots: 0 scalar
      deleting destructor, 1 GetPose(pose: 64 rotations, 64 translations at +0x400, more at +0x800), 2 0x005BAB90,
      3 CallMemberFunction(name, params, script) -> 0/1/2, 4 InitFromStructure, 5 Update. Rewritten:
      - the standard controller (type 0xCB4533FF, vtable 0x00649F50, 0x28 bytes, its own item pools 0x00701430):
        operator new 0x4A4F30 (`(size, int)`, cdecl) / delete 0x4A5600 (Mem::PoolPair), Construct 0x4A4500,
        DestroyBody 0x4A4590, Destroy 0x4A55E0, GetPose 0x4A4F70, CallMemberFunction 0x4A4670 (PlayAnim 0xAF2FAE19:
        AnimName / StartTime / EndTime / LoopType / BlendPeriod / Speed on the channel's timeline (0x00481430), the
        frame from the frame source by anim set + name; 0xC68365EB: a new frame source for Anim; 0x92267217;
        InvalidateCache: refetch, then 2 = "not mine"), Update 0x4A4620 (the timeline steps (0x004814B0), its events
        between the two times (0x00481E10); the frame refetched when the time went back);
      - the weighted controller (type 0x7E1940A1, vtable 0x00649F68, 0x24 bytes; name not recovered): Construct
        0x4A5010 (`(timeline, object)`: the channel's object, or `object` without a timeline), DestroyBody
        0x4A4820, Destroy 0x4A50B0, GetPose 0x4A5660 (nothing with no frame / source or a weight <= 0 - NaN goes on;
        the skeleton component read through null without one, as the original), CallMemberFunction 0x4A4890;
      - the wobble controller (wobble, vtable 0x00649F80, 0x38 bytes): Construct 0x4A49F0, DestroyBody 0x4A4A80,
        Destroy 0x4A50E0, GetPose 0x4A5110, CallMemberFunction 0x4A4AF0 (PlayAnim, 0xD0209498: the target as a
        0..1 fraction 0x4D747FA0 of the timeline, SetWobbleDetails: all five of wobbleAmpA/B, wobbleK1/K2,
        spazFactor or nothing), WobbleTime 0x4A5180 (amplitudes grow by spazFactor towards the timeline's ends,
        the target kept clear of the ends, two sines of the game clock + 0x00696D60 (x87 FSIN, the cycles' integer
        part through _ftol), clamped to the timeline), Update 0x4A52E0;
      - the pose controller (pose, vtable 0x00649F98, 0x854 bytes): Construct 0x4A5730 (64 identity rotations; the
        rest left), DestroyBody 0x4A4D20, Destroy 0x4A5320, GetPose 0x4A4D30 (0x840 bytes copied);
      - the partial animation controller (partialAnim, vtable 0x00649FB0, 0x24 bytes): Construct 0x4A5350 (a
        timeline of its own: Calloc + 0x00481650), DestroyBody 0x4A4D50, Destroy 0x4A5400, GetPose 0x4A5430,
        InitFromStructure 0x4A4DD0 (base's, AnimName's times / loop type (0x004877E0 / 0x00487770), Speed (1),
        Current through the timeline's PlayAnim slot), CallMemberFunction 0x4A54A0 (0x6AAEB76F: one step of
        0x06C9F278 frames, the speed held at 0 meanwhile; 0xBD4EDD44 Speed (per 1/60 s); GetPartialAnimParams;
        0xF5E2B871: direction 0 <-> -1; InvalidateCache), Update 0x4A4EF0;
      - 0x4A4610 (slot 4 of four of them): a jump to the base's InitFromStructure.
      Findings: a missing PlayAnim AnimName leaves the original's argument slot (the params' address) as the name -
      kept; other missing values were uninitialised locals in the original (0 here). 32 functions.
      Early test (src/hook/selftests_fe_anim.cpp, with the shared src/hook/fe_testkit.h: arena pair, stand-in
      structures, float-returning stand-ins): every function, original vs ours, on stand-in controllers, timelines,
      objects, skeletons, frame sources and parameter structures; every external call recorded (frame sources,
      timeline pieces, anim set, memory manager and pools, structure getters / adders); the member functions'
      every case incl. missing parameters, NaN / equal / backwards updates, the wobble's clamps with positive and
      negative amplitudes, the pool paths of new / delete. 41 deliberate bugs caught (2 after strengthening: the
      wobble's clamps).
- [x] Batch 115 (FE-2; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): CScreenElement, the base of every front-end
      element (src/game/front/screen_element.*, rewritten from the stub), 0x4A8610-0x4AAB30. The class now
      derives from Obj::CObject and has its whole layout: the four smart-pointer links (+0x4C parent, +0x58 first
      child, +0x64 next, +0x70 previous sibling), three 0x44-byte states (SElementState: alpha, pos3D, x / y, z,
      top left, scale, base scale, angle, rgba) - +0x7C the one shown, +0xC0 the one morphed to, +0x114 the summed
      one the layout pass leaves (the parents' included) -, the morph's start / length / last fractions
      (+0x104..+0x110), justification +0x158, size +0x160, z_priority +0x16C, remembered alpha +0x170, priority
      +0x174, and the flag bits (0x400 dirty, 0x800 children dirty, 0x1000 / 0x8000 z, 0x2000 dims locked, 0x4000
      the focus flag, 0x20000 / 0x40000 paused, 0x80000 pos3D, 0x100000 behind the camera, 0x200000
      block_events, 0x400000 hidden, 0x800000, 0x1000000, flip_h / flip_v, 0x10000000 priority). Rewritten:
      state copy 0x4A8730, Construct 0x4A8F90, DestroyBody 0x4AAB30 (out of the tree; id 0xDEADBEEF), Destroy
      0x4AAC90; GetJust 0x4A8610, GetPos 0x4A87C0, GetTopLeft 0x4A87E0, GetPriority 0x4A86C0 (source only, 7
      bytes), ManagerUsesPriorities 0x4A86B0, EventsBlocked / HasPriority / IsHidden 0x4A8F00 / 0x4A8F20 /
      0x4A8F40, SetPaused 0x4A8680, SetFocusSent 0x4A8940 (the element's 0x3056434F / 0x4A57316B event);
      PlaceTopLeft 0x4A8C30, SetPos 0x4A9100, SetPos3d 0x4A91F0, SetDims 0x4A92D0, SetJust 0x4A9390 (the position
      moves so the element stays put), SetScale 0x4A94A0 (relative: times the base scale; else it becomes the
      base), SetAlpha 0x4A8630, SetRotation 0x4A8800 (degrees -> radians plus 0.001, compared unrounded),
      SetRGBA 0x4A88C0 (the shown colour set without comparing), StartMorph 0x4A8870, SetZPriority 0x4AA390
      (+1 per generation; an element's own z_priority wins), SetChildrenPriority 0x4AA470; GetParent 0x4A8B80,
      GetWindow 0x4A8BB0, GetLastChild 0x4A95E0, CountChildren 0x4A9600, GetChild 0x4AA2A0 (by id, or by index
      when the id is 0), GetChildById 0x4AA690, GetChildByIndex 0x4AA6D0, SetParent 0x4AA8E0 (out of the old
      parent's list, last in the new one's; the second argument is unused); the layout pass UpdateLayout
      0x4A9D30 (pos3D projected through the first viewport's camera - behind it: -320, -224 -, the morph: four
      curves (linear, 0xF5B95CE4 ease out 2t - t^2, fast_in t^2, gentle (3 - 2t)t^2) - the position follows the
      curve, scale / angle / alpha / rgba the time, both as fractions of what was left last frame -, the summed
      state from the parent's (rotated by its angle: x87 FSIN / FCOS), slot 10, the children); ResolvePos 0x4A8C80,
      ResolveJust 0x4A8DC0, ResolveRGBA 0x4A8E90 (shifts past 31 bits wrap as SHL does); the vtable's
      SetProperties 0x4A96B0, CallMemberFunction 0x4A89B0 (SetProps, DoMorph - the script waits `time` -, Die,
      GetProps), GetProperties 0x4AA710, SetMorph 0x4A9AA0 (+ 0x4AEE50, a jump to it, source only); and
      Mth::Vector::operator!= 0x4A86D0 (any component differs or is NaN). 45 functions.
      Findings: the original's x87 products come in two operand orders for the top left (dims then scale, scale
      then dims) - kept, as they round differently; SetMorph's ResolvePos can return true without writing x / y
      (a Pos structure without relative / proportional): the original then passes its argument's slot (the
      params' address) as y - kept; CObject's +0x3C (m_aliasCount) is the element's use count during the layout
      pass. Element3d now calls CScreenElement's functions directly and uses the named fields.
      Early test (src/hook/selftests_fe_element.cpp on the shared world src/hook/fe_element_fixture.h: element
      trees in an arena with real smart-pointer links and patterned states, a recording vtable): every function,
      original vs ours, the trace of calls out (the object base and tags, the tracker's events, the manager, the
      camera's projection, the clock, structure getters / adders, arrays, memory) and the whole arena after each
      step: construction on dirty memory, destruction of inner / first / root elements, flags up parent chains,
      the focus flag's odd values, every placement setter (different / equal / NaN, now or not), the tree queries
      and SetParent moves (middle, first, last child, into an empty parent, out of the tree), the layout pass
      (each curve, time before / at / past the end, paused, pos3D in front / behind / keeping its scale, a
      changed parent, the root), the parameter helpers (each Pos form, each justification form, rgba arrays of
      2 / 4 / 6), properties / morph / GetProps / the member functions. 61 deliberate bugs caught (6 after
      strengthening: a NaN left in a position masked two; the object base's flags; an exactly finished morph;
      restore_alpha alone); 1 equivalent (DestroyBody's own release of the next-sibling link: SetParent already
      released it).
- [x] Batch 116 (FE-3; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): the menus and the container element (new
      src/game/front/menu_element.*), 0x4A74E0-0x4A8100 and 0x4AA530-0x4AACC0. CBaseMenu (vmenu / hmenu, vtable
      0x0064A048, 0x1A8 bytes; fields named: selected index / id, focus, controller, internal_just, vertical,
      regular_space_amount, padding_scale, spacing_between, pad handling, allow_wrap, grid index): Construct
      0x4A74E0, ConstructVMenu 0x4A75C0, ConstructHMenu 0x4A7630, DestroyBody 0x4A7620, Destroy 0x4A76C0,
      SetProperties 0x4A76F0, GetRegularSpace 0x4A7550, SetSelectionTags 0x4A7580, MoveSelection 0x4A7850 (twice
      the item count of tries; wrap or turn back at the ends; skips not_focusable / block_events items), Step
      0x4A7A40 (pad: unfocus, move, focus with controller and grid index), Arrange 0x4A7B80 (regular spacing or
      sizes x padding + spacing_between; locked dims only grow; the first item focused when nothing is
      selected), PassEventToScript 0x4A8100 (vtable slot 5: pad-event priorities, the focus flag event re-finds
      the selection and arranges, focus with grid_index / child_id, unfocus, the four pad directions).
      CContainerElement (ContainerElement, vtable 0x0064A0BC): Construct 0x4AAC20, DestroyBody 0x4AAC80,
      Destroy 0x4AACC0, SetProperties 0x4AA530 (focusable_child - a function Ghidra hadn't found),
      PassEventToScript 0x4AA560 (focus / unfocus passed to the focusable child with the controller). The
      screen elements' smart-pointer instances: ConstructFrom 0x4A7820, ConstructCopy 0x4A7690 (also not found
      by Ghidra), Assign 0x4A8F60 (CSmtPtr gained these three; Assign releases first, so self-assignment
      clears). 21 functions.
      Early test (src/hook/selftests_fe_menu.cpp on the element world): construction, properties, the moves over
      every end / wrap / skip case and empty menus, steps, arranging both orientations x regular / sized x
      locked / not, every event path, the container's events with and without priorities, the smart-pointer
      constructors and assignment. 47 deliberate bugs caught (4 after strengthening: refocusing without focus,
      the container's unfocus, self-assignment, a 1.1 padding scale); 2 equivalent / below float precision: the
      menu's dirty flag taken from its items (it is already set), and the unrounded vs rounded half item
      length (differences under a float's rounding of the offsets).
- [x] Batch 117 (FE-4; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): the screen element manager (new
      src/game/front/screen_element_manager.*), CScreenElementManager (0x00701440, 0x6A4 bytes, a CBaseManager with
      an event listener at +0x28, the root +0x44, 48 focus entries in two lists, 10 pad-event priorities, the
      pad_event_types). Construct 0x4AB990, DestroyBody 0x4ABB00, Destroy 0x4ACB40, the listener's Destroy
      0x4ABAF0, UnregisterObject 0x4AAFF0 (a removed root: the first other parentless element), GetObjectList
      0x4AAD40 (4 bytes: REPLACE_SOURCE_ONLY), GetElement 0x4AAE20, SetRoot 0x4AB730, SetParent 0x4ABBE0,
      DestroyTree 0x4AC400, DestroyElement 0x4AD100, FindByLocalId 0x4AB130, ResolveId 0x4AC120 (nested
      {id child = ...} structures, local ids, aliases, child indexes), ResolveElement 0x4AD1E0, Update 0x4ABD00
      (the layout pass with the tree locked), LockTree 0x4AAF90, SetPausedAll 0x4AAEE0, AddFocusEntry 0x4AB250
      (a new entry goes after the last entry looked at - list 1's tail when it has entries, whatever list it is
      for: as the original), RemoveFocusEntries 0x4AB330, ClearMarks 0x4AB4D0, ListenerReceive 0x4ABD40 (focus /
      unfocus / the focus flag's events, pad events to the focused elements with priorities and the device
      check), HasPriorities 0x4AAD80, IsPadEvent 0x4AAD50, IsTopPriority 0x4AB7C0, PushPriority 0x4AC570,
      UpdatePriorities 0x4AB5D0, CreateScreenElementManager 0x4ACAF0, DeleteScreenElementManager 0x4AACF0,
      SetScoreText 0x4AB910. 29 functions (CreateElement 0x4ACB70 is with the script commands, FE-6).
      The elements' own calls into the manager still go through the game's addresses (now ours when hooked).
      Early test (src/hook/selftests_fe_manager.cpp: a real manager over the element world): construction, the
      singleton's use counts, unregistering roots, SetParent with the root / no root, local ids, every ResolveId
      form, frames and the tree lock (counts checked between locks, a negative count), pausing, destruction with
      and without recursion / keeping the element, both focus lists (an element in the other list, entries from
      the parent on, marks, the entries full), every event path, priorities (pushes into free slots, the lowest
      replaced or refused, equal to the lowest, an inactive slot's priority), the score text. 46 deliberate bugs:
      44 caught (9 after strengthening); 2 equivalent: GetElement's assert argument (unused) in SetRoot, and
      ResolveId's own release of the element being resolved on the plain-id path (the caller releases it too).
      Note: thug2_check.exe crashed silently (exit -1) on 2 of 4 runs once, right after a build, in another
      area's tests (the last lines were the movie / script-trace tests); 12 later runs passed - not reproduced.
- [x] Batch 118 (FE-5; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): the scrolling menus (new
      src/game/front/scrolling_menu.*), CScrollingMenu (vscrollingmenu / hscrollingmenu, 0x1C0 bytes: a screen
      element with its own event listener at +0x178; the menu inside it, the window's start / length,
      scroll_speed, num_items_to_show). Construct 0x4AE050, ConstructVertical 0x4AE170, ConstructHorizontal
      0x4AE1C0, the three destructor bodies 0x4AE110 / 0x4AE1A0 / 0x4AE1F0 and deleting destructors 0x4AE240 /
      0x4AE270 / 0x4AE2A0, the listener's three slot-0 adapters 0x4AE210 / 0x4AE220 / 0x4AE230, FindMenu
      0x4AE2D0 (the last vmenu / hmenu child; the window from regular spacing x items shown, else the size),
      ItemPlace 0x4AE450 (an item's place and size in the menu's scale), SetProperties 0x4AE520
      (internal_just, scroll_speed, num_items_to_show, reset_window_top / reset_window_bottom / rearrange),
      PassEventToScript 0x4AE6A0 (the focus flag's event, focus / unfocus passed to the menu), ListenerReceive
      0x4AE7E0 (the menu's events: its focus flag re-arranges, an item's focus selects it), Update 0x4AE960 (the
      window follows the selection by scroll_speed with margins that keep it mid-window, the menu moved, the
      items outside hidden). 18 functions.
      Early test (src/hook/selftests_fe_scroll.cpp): construction / destruction of each kind, FindMenu (vertical /
      horizontal with both menu kinds present, 0 / -1 items shown), ItemPlace, every property, the events, Update
      over sizes and regular spacing, each margin case, scrolling back / forward and the clamps. 31 deliberate
      bugs, all caught (6 after strengthening). Several mutation builds failed with "cannot open program
      database" / LNK1318 RPC errors (the build's PDBs; deleting out\build\verify's vc140.pdb / thug2_check.pdb
      fixed it) - re-run.
- [x] Batch 119 (FE-6; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): the picture elements and the window element
      (new src/game/front/sprite_element.*) and the engine's platform calls at the range's end (new
      src/game/gfx/engine_glue.*). CTextureElement (TextureElement, vtable 0x0064A1F8, 0x180 bytes): Construct
      0x4AED90, DestroyBody 0x4AEDF0, Destroy 0x4AEE60, SetProperties 0x4AEE90 (Texture, rot_angle, Blend),
      GetProperties 0x4AEFD0 (only the summed colour as rgba), Update 0x4AEF40. CSpriteElement (SpriteElement,
      vtable 0x0064A228, 0x184 bytes): Construct 0x4AF1A0, DestroyBody 0x4AF0D0, Destroy 0x4AF210, SetTexture
      0x4AF130 (the element takes the texture's size), SetSpriteRotation 0x4AF240, SetProperties 0x4AF520
      (Texture, rot_angle, Blend, no_zwrite, the window's viewport), GetProperties 0x4AF4E0, SetMorph 0x4AF5E0,
      Update 0x4AF2C0 (hidden off the layout's -200..648 top band, position / size / flips / rotation / just /
      scale / z_priority or pos3D depth (+5000 to the float's bits with kKeepScale, as the original) / z write /
      colour). CWindowElement (WindowElement, vtable 0x0064A2C0): Construct 0x4B2060 (a 640x448 2D window),
      DestroyBody 0x4B2100, Destroy 0x4B2170, GetWindow2d 0x4B2160, Update 0x4B21A0. Nx platform calls:
      ToggleMemView 0x4B2270 (the MemViewToggle command's flag), StartRender 0x4B2280 (BeginScene unless the
      device is lost), DeleteObject 0x4B22A0, AddDebugShape 0x4B22C0, PlatformExtension 0x4B2320 ("Xbx"), and
      REPLACE_SOURCE_ONLY for the jumps SetScreenClip 0x4B22B0, FlagDebugLines 0x4B22F0, ClearDebugLines
      0x4B2300, SphereVisible 0x4B2310 (0x4B2230 / 0x4B2240 were already in singleton.cpp). 29 functions.
      Early test (src/hook/selftests_fe_sprite.cpp): construction / destruction with and without pieces,
      properties, rotation (target / shown equal, NaN, now or not), the morph, updates over the flag sets (pos3D,
      off screen, keep scale, flips, the 0x1000000 angle, hidden parents), tops and alphas at the band's edges and
      NaN; the platform calls against stand-ins of the PC engine. 40 deliberate bugs: 38 caught; 2 equivalent: the
      alpha threshold's >= vs > and the NaN case (the alpha byte is 0 either way: 255 x 0.0001 truncates to 0, and
      a NaN's _ftol is 0x80000000).
- [x] Batch 120 (FE-7; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): the screen element script commands (new
      src/game/front/screen_element_cfuncs.*) and the manager's CreateElement 0x4ACB70 (every element type on the
      heap the parameters name - Default: the current one, none given: the front end's -, its id, registered, its
      properties and morph, a pad-event Priority). CFunctions: ScreenElementSystemInit 0x4AAD90 (the root window),
      CreateScreenElement 0x4AD240, DestroyScreenElement 0x4AD300 (dont_recurse / preserve_parent),
      RunScriptOnScreenElement 0x4AD370 (a member function, else a spawned script with callback / Params /
      callback_params, run once), SetScreenElementProps 0x4AD4C0, DoScreenElementMorph 0x4AD540,
      SetScreenElementLock 0x4AD5C0, GetScreenElementDims 0x4AD650, GetScreenElementProps 0x4AC750 (an Element3d's
      angles too), SetRootScreenElement 0x4AC900, GetScreenElementChildren 0x4AC940, TextElementConcatenate
      0x4AD710, TextElementBackspace 0x4AD800, GetTextElementString 0x4AD8D0 (always false, as the original),
      GetTextElementLength 0x4AD9C0, GetScreenElementPosition 0x4ADA80, MenuSelectedIndexIs 0x4ADB20,
      ScreenElementExists 0x4ADC10, CreateMultipleTextElements 0x4ADC80 (word wrap into text elements; a last
      word that starts a new line is lost, as in the original). 20 functions. The text elements' own pieces are
      still called by address here (now ours when hooked, FE-8 / FE-9).
      Early test (src/hook/selftests_fe_cfuncs.cpp): creation of every type on every heap (and an unknown type),
      the root window; each command with resolved / unresolved ids and each option; the text commands on a block,
      a text element and another element; the selected index (none / by number / first / last); word wrapping
      (a string, an array of strings, a line end before the next string, a first word wider than the width,
      exactly 0x60 characters, the last word starting a line). 37 deliberate bugs, all caught (13 after
      strengthening: CreateElement's own stand-in had hidden the creation test at first).
- [x] Batch 121 (FE-8; front-end agent, merged 2026-10-19; run in the game: docs/pending/frontend-ui.md; NOT judged by a player yet): the text elements (new
      src/game/front/text_element.* and text_block.*). CBlurEffect (the blur_effect object, 0x47C bytes: 16 copies,
      target / current settings, a morph clock, blur_rgba): GetCopy 0x4A5770 and SetTarget 0x4A5CF0 (functions
      Ghidra hadn't found), StartMorph 0x4A5780, Update 0x4A57D0, SetFromParams 0x4A5D30. CTextElement
      (TextElement, vtable 0x0064A290, 0x1B0 bytes: the string, font, text objects with optional shadow objects,
      blur, internal_just, encoded-colour overrides): Construct 0x4B0920, DestroyBody 0x4B1830, Destroy 0x4B2030,
      SetProperties 0x4B18E0, SetMorph 0x4B09D0, Update 0x4AFE70 (text / shadow / 16 blur copies), GetLength
      0x4AF650, SetViewports 0x4AF670, SetText 0x4AF8C0 (\, \_ and \m<c> button glyphs), SetTextRotation
      0x4AFA00, SetCharProperties 0x4AFBB0, Concatenate 0x4AFD00, Backspace 0x4AFDD0, MakeTextObjects 0x4B0620,
      FreeTextObjects 0x4B0760, EnableBlur 0x4B0A20, DisableBlur 0x4B0AE0. CTextBlockElement (TextBlockElement,
      vtable 0x0064A264, 0x1C4 bytes: word-wrapped lines as text element children): Construct 0x4B0810,
      DestroyBody 0x4B08B0, Destroy 0x4B0B30, SetProperties 0x4B1B60 (a string or an array of up to 29 - the
      original overran its stack with more), SetMorph 0x4B0B60 (each line's blur aimed at the block's middle),
      Update 0x4B1EC0, SetFont 0x4AF6C0, AddWords 0x4AF720, SetText 0x4B0CD0 (the last lines that fit, or the block
      grows with allow_expansion), GetText 0x4B11C0, GetLength 0x4B1390, Backspace 0x4B14A0, Concatenate 0x4B16D0
      (enforce_max_width / last_line), UpdateLines 0x4B1540. 36 functions. Original quirks kept: a line break
      from an empty line clears the byte before the line buffer if it is a space; the blur's memory isn't
      initialised (its constructor 0x005902E0 only returns it); Update walks as many children as lines were made.
      Early test (src/hook/selftests_fe_text.cpp): the text element's construction, text escapes and sizes,
      concatenating / backspacing (escaped and single backslashes), text objects with and without shadow and blur,
      destruction; properties (each flag alone and together), rotation about each internal_just, per-character
      properties, morphs; updates over pos3D / off screen / hidden / the band's edges / NaN, with shadow and blur;
      the blur's clock (part way, past the end, gone back, a huge time left), targets and parameters. The text
      block against real text elements (the original's or ours) with the manager's registering / parenting /
      destroying stood in: fonts, wrapping (wider first word, line ends, more lines than fit, expansion with
      truncating line heights, a negative count), the text back, adding (widths at the edge), backspacing,
      properties, updates, morphs. 81 deliberate bugs: 79 caught (15 after strengthening); 2 equivalent: the half
      height rounded before or after halving (x0.5 is exact), and the blur copies' alpha without the 0.0001
      threshold (below it the alpha byte truncates to 0 anyway).
- [x] Batch 122 (NOT run in the game yet): a network game crash in the ported levels. The user's report
      (2026-10-19): a hosted network game loads in Boston but crashed loading a ported level (the log: THUG1's New
      Jersey). Crash: strlen(null) in CStruct::AddString from 0x005311F0 (the client's handler for the server's
      level change), whose level name comes from GameNet::Manager::GetLevelName 0x005369A0 - it looks in
      level_select_menu_level_info only (THUG2's own levels; the only code reading that list), and the ported
      levels are in tools/menu_level's own lists, so it gave null. Rewritten (new src/game/net/gamenet_manager.*,
      the first of GameNet::Manager 0x00533F20-0x0054EF60) with the ported levels' four lists searched after THUG2's
      (a change from the original, noted in the header; the other callers 0x0052DD40, 0x0053BA6A, 0x0053F960,
      0x0054CE80 get the ported names too). Quirk kept: an entry without `level` compares the value left from before
      (at first the manager's own address). Early test (src/hook/selftests_gamenet.cpp): original vs ours on stand-in
      lists - the quirk, a match, the created park's entry with / without the park name asked (valid / invalid map),
      not found, a one-element list; ours alone: a ported level, THUG2's list first, not found with a ported list.
      13 deliberate bugs, all caught. Also checked: New Jersey's peds / cars created at start are all
      AbsentInNetGames (the path manager doesn't exist in net games - see make_menu_level.py testlevel_nodes).
      New tools/symbolize.py: a crash report's DINPUT8.dll+0x... offsets to functions and lines (dbghelp + the
      build's PDB).
- [x] User's play-test report 2026-10-19: Minneapolis and Santa Monica load (fixed crashes), the Mall's glass sound
      works, peds and cars show in the ported levels, the front-end menus are fine (batches 114-121 judged).
      Time of day (110-113) still to be judged.
- [x] Batch 123 (run in the game: workspace start-up and SKATE NOW, no errors; the halfpipe NOT skated by a player
      yet): the trigger component's node lookup 0x00434A60, as Obj::CTriggerComponent::TriggerNode (new
      src/game/obj/trigger_component.*; __thiscall, 4 arguments: the three events, the face's object name, a node array
      or null, the other object). The node: GetNode, or the array searched from its end (none named so leaves element
      0 - kept); the other object's node's parameters structure 0x5180A4DB; the TriggerScript; the skate module's skip
      (0x00565BB0); the events' bits into +0x28; SpawnAndRunScript with the node's NetEnabled / Permanent flags. A
      deliberate change, the original kept in a comment: a trigger face whose object has no node returns false and is
      logged once per name - the original read the TriggerScript through the null (the user's crash 2026-10-19: SKATE
      NOW in the THPS4 shop, landing on the halfpipe's vert sections, whose trigger nodes the front end's node array
      didn't have). Also: tools/menu_level/make_menu_level.py front_end_triggers gives the front end the shop's trigger
      geometry nodes (SSO_Vert_Section01-03) and their gap scripts - the gaps score in SKATE NOW. Early test
      (src/hook/selftests_trigger.cpp): original vs ours with GetNode, 0x00565BB0 and SpawnAndRunScript stood in for -
      lookup with and without an array, found / not found (element 0 with and without a script), scripts, each flag
      alone and both, parameters, the skip, six event triples; ours alone: no node. 19 deliberate bugs: 18 caught (3
      after strengthening), 1 equivalent (the first event's bit added instead of set right after the clear).
- [x] Batch 124 (AM-1; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; the renderer's cached device state and the device's start-up objects):
  src/game/gfx/render_state.{h,cpp} (new), src/game/gfx/device.{h,cpp}.
  - 0x004DAB60 `Nx::SetState(state, value)` (cdecl; the renderer's own render state numbering, RenderStateId): z write
    / z test (LESSEQUAL or ALWAYS) / alpha ref (0 = alpha test off) / sampler addressing 4-7 (U low 16 bits, V high:
    wrap, clamp, border - clamp without border support 0x786AC0; other values cached, nothing sent) / mip LOD bias 8-11
    / cull / alpha blend / alpha test / specular (with the local viewer) / fog / depth bias (unsigned x the float
    0x64AAA4, x87 double then float) / lighting / filters 0x20-0x23 (bit 16 magnification, bit 0 minification linear)
    / local viewer; 0xC-0xF and 0x17-0x1F nothing. Every send through Nx::SetRenderState (0x44F870, ours), as the
    original's tail jumps. The cache is 0x786E30-0x786F34 / 0x787338-0x787348.
  - 0x004DA760 `SetBlendMode(mode)`: an override (0x786F34) wins; the low 24 bits pick (op, src, dest) from the table
    (switch 0x4DA9BC: 0/0xB, 1-4, 5/0xC, 6, 7/8, 9, 0xA, 0xD, 0xF, 0x11; 0xE, 0x10 and above use the ARGUMENT - not the
    override - for all three), black fog (via 0x44F870) under modes 1-4 and the fog colour (straight to the device)
    otherwise, a fix-up without blend factors (0x786AC1: BLENDFACTOR/INVBLENDFACTOR -> ONE/INVSRCALPHA, a lone
    BLENDFACTOR source -> ZERO, destinations -> SRCALPHA / INVSRCALPHA), and the top byte as the blend factor (doubled,
    0x80 and up saturating, in all four channels).
  - 0x004D9DB0 `SetVertexFormat` (unless locked 0x786F28), 0x004DA730 `SetTexture(stage, texture)`, 0x004D9DE0
    `SetFixedFunctionStages(shader, factor)` (the texture stages standing in for the fixed pixel shaders 0x7CA944-64
    without pixel shader support: tested in the original's order, 0x7CA958 and unknown ones = texture only),
    0x004DAA10 (the same with the material's colour +0x40 x 255 through _ftol, white without a material),
    0x004DAA80 `SetPixelShader(shader, material)` (or the stand-in stages without ps.1.1 0x786AB8; lock 0x786EC4;
    five constants 0x786ECC), 0x004DAAF0 (n constants, then constant 4 again), 0x004DA0E0 (returns 0x7CA978 in ST0),
    0x004DB620 `SetWorldTransform` (the object transform 0x7CA968 -> copy 0x7CA980 with w = 0, 0, 0, 1, or null).
  - 0x004D7D70 `ResetRenderState` (identity transforms 0x786B90/BD0/C10, cache defaults, 0xDEADBABE "no blend mode",
    the default light and ambient as bit patterns, fog -240 / -24600) and 0x004D80D0 `ApplyRenderState` (the cache
    to the device: render states, per sampler magnification / mip linear, anisotropic minification on sampler 0 at
    back buffers up to 480 lines (signed compare; min(anisotropy, 3)), wrap addressing, a zero material, material
    sources, 0x4CD940, 0x4DD7F0 (the shaders), fog, multisample antialias with the setting 0x7D6434).
  - 0x004D7C80 `CreateDevice(width, height, hardwareVertices, multisample)` (HAL, hardware or software vertex
    processing | MULTITHREADED; the adapter identifier zeroed and read; DirectSoundEnumerateA through the exe's import
    0x64502C; 0x786AB5 = "8500" in the adapter description (Radeon 8500); 0x67DA40 = 0 for an Intel 915G (8086:2582)
    with the last sound device "Realtek HD Audio rear output"; returns CreateDevice's HRESULT) and its callback
    0x004D7C20 `SoundDeviceFound` (not a Ghidra function: copies each description to 0x786AC8 - over 200 characters
    only the first 199, unterminated - returns TRUE so the last one stays).
  - 0x004D8420 `CreateRenderTargets` (dynamic buffers 0x5F4620(0x40000) / 0x5F4690(0x1000); nine X8R8G8B8 render
    target textures at 1, 1, 4, 4, 4, 8, 8, 16, 16ths and their level-0 surfaces; a depth surface; the back buffer and
    depth surface pointers kept (released at once); a 256x256 target for depth textures (0x786AB9); the capabilities:
    ps.1.1, stream offset, border addressing (only ever set), max anisotropy, blend factor on both sides; the back
    buffer size / format, depth bits 16 / 32, the first target's format; then ApplyRenderState) and 0x004D7AF0
    `ReleaseRenderTargets` (surface pointers left as they were; 0x5F4650, 0x5F4670, 0x5F52D0).
  - 0x004D7B60 `LoadGamma` (Settings\Gamma R/G/B, 0.25 each without; the key is never closed) -> 0x4D07B0.
  - 0x004D89C0 `ShowDiscError` (never returns: the timer killed, the device made if missing, the two lines of the
    disc error in French (language 3), German (5) or English, drawn forever with Present / Clear and fixed states).
  - InitDirect3D (0x4D8650, earlier batch) now calls these directly.
  Test (src/hook/selftests_appmain.cpp, Early): a recording stand-in device; TestRenderStateCache - 4000 random calls
  of every setter per variant from random caches made of the values used, capability flags and locks toggling, four
  handle variants (distinct, all null, 0x7CA944-58 equal); TestRenderStateDefaults (5 cases: heights 480 / 481 / -5,
  anisotropy, antialiasing, with and without the reset); TestCreateDevice (9 cases: vertex processing, multisampling,
  failing, "8500", Intel ids, Realtek last / not last / with a suffix, a 230- and a 200-character name, none);
  TestRenderTargets (5 cases: every caps flag on / off, D16 / D24, sizes that don't divide, depth textures, no
  surfaces, the first surface missing; then the release); TestLoadGamma (key missing, each value missing); TestShowDiscError
  (each language, timer, device missing; run 0-3 frames and ended from the stand-in Present with CallEscapable).
  Mutations: 55 deliberate bugs, all caught (52 at first; the handles-equal variant, the first-surface-missing case and
  a stand-in that changes the query size were added for the other 3).
- [x] Batch 125 (AM-2; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; occlusion polygons and view culling): src/game/gfx/occlusion.{h,cpp} (new).
  - Occlusion polygons (0x787360, 0x58 bytes: in use, enabled, checksum, four corners, normal) and occluders
    (0x792360, eight of 0x5C: the polygon, five planes, what each hid in the last two frames):
    0x004D8D70 `AddOcclusionPoly(p0, p1, p2, p3, checksum)` (normal (p1 - p0) x (p3 - p0) with p3.x - p0.x kept in
    double precision, then Mth::Vector::Normalize), 0x004D8C90 `EnableOcclusionPoly`, 0x004D8CC0
    `RemoveAllOcclusionPolys`, 0x004D8BB0 `CompareOccluders` (qsort's comparison - not a Ghidra function; disabled
    polygons' counts cleared, more hidden first), 0x004D8C10 `SortOccluders` (the game's qsort; occluders that hid
    nothing in two frames dropped from the end, this frame's count cleared), 0x004D8CE0 `AddOccluders` (up to four
    more enabled polygons a frame - counted even when the eight are full - carrying on from the last),
    0x004D8ED0 `BuildOccluders(eye, frame)` (which side the eye is on - not a number counts as behind - picks the
    corner order; ten inlined cross-product / normalise / distance sequences, each kept as the original rounds it:
    SetPlane's length order (x,z,y / z,y,x), one plane with the unrounded inverse for x, the distance products in
    z,y,x order for the polygon's own plane), 0x004D9AC0 `SphereOccluded(centre, radius, weight)` (behind all five
    planes of an occluder: a distance that isn't a number counts as behind; the occluder credited; the object
    transform's position added unrounded).
  - Culling against the camera (Engine glue's "SphereVisible" 0x4B2310 jumps to the first): 0x004DB570
    `SphereOutsideView` (int 1 / 0), 0x004DB790 `SphereInView` (bool; the object transform's position added and
    rounded; leaves -z - radius in 0x7CA978), 0x004DB6D0 `BeyondClipDistance` (with distance clipping 0x7D643A on:
    |view position| - radius past the far clip). Near plane 0x786CA8, far 0x786CAC, the side planes' factors
    0x786E20-2C. D3DXVec3Transform (0x5BF19F, a dispatch thunk) stays a library call.
  - Note for engine_glue.cpp (another range): its AddDebugShape / FlagDebugLines / ClearDebugLines are really
    AddOcclusionPoly (type 4 = four corners) / EnableOcclusionPoly / RemoveAllOcclusionPolys.
  Test (src/hook/selftests_appmain2.cpp, Early): TestOcclusion - four seeds of 15-18 polygons (quads at random
  places and turns; degenerate ones; reversed; one with a corner that isn't a number; one whose differences don't fit
  a float), 60 frames each of enabling by checksum, eyes moving (also on a polygon's plane, behind them all), 40
  spheres a frame (some not a number, weights -2..4, the object transform on / off), SortOccluders / AddOccluders
  called directly, RemoveAll part way; all the memory compared. TestViewCulling - exact edges first (radius - z ==
  near, radius == a + b, == c - d, and one float either side) then 3 x 3000 random spheres through all three tests
  with a stand-in D3DXVec3Transform, random view / clip values, the transform on / off.
  Mutations: 34 deliberate bugs; 29 caught (24 at first, 5 after adding the NaN polygon / NaN spheres, the
  wide-range polygon and the exact edges). The other 5 only change the order of a sum of squares or of products
  before a float rounding (length orders, distance orders) - below float precision for any input the test could
  find; they're kept as the original orders them.
- [x] Batch 126 (AM-3; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; shadows): src/game/gfx/shadow.{h,cpp} (new).
  - Shadow casters (0x10C bytes: render target, model, view, projection (near 5), the receivers' projection (near 80),
    the texture matrix) in the hash table at 0x7CA970, keyed by the target's address: 0x004DDC90 `AddShadowCaster`,
    0x004DB0A0 `RemoveShadowCaster`, 0x004DB100 `AimShadowCaster` (up is z when looking straight along y - x and z
    both equal - else y; D3DX LookAt / Ortho 120 x 120). The casters are walked with the table's own iterator: an
    item with no value ends the walk (as the original).
  - 0x004DBBD0 `RenderShadowTextures` (each caster with a model into its texture: fog off for it, the model's
    active meshes drawn with flag 8 and marked 0x200; casters without a model skipped), 0x004DBF50 `DrawShadows`
    (each caster's texture projected onto the receivers in view: 0x004DA0F0 `ProjectShadowVertices` (the clip
    flags from the float bits as the original), 0x004DA190 `ClipShadowTriangles` (dropped when all three are off
    one side, or facing > -1e-4 worked out in double), DrawPrimitiveUP, the draw / triangle counters 0x786EBC /
    0x786EC0), 0x004DB880 `ShadowTextureMatrix` (the view's inverse is worked out and not used), 0x004DB9D0
    `DrawShadowVolumes` (the scene's flagged meshes in view; two-sided stencil, or the two sides apart with
    blend modes 0x10000002 / 0x10000004; the vertex-colour pixel shader locked meanwhile; ZFUNC GREATEREQUAL
    without two-sided stencil).
  Test (src/hook/selftests_appmain3.cpp, Early): TestShadowProjection (random positions / matrices so every clip
  bit and both facings happen, degenerate and repeated indices, and a triangle facing exactly -1e-4);
  TestShadowPasses (6 seeds over a stand-in caster table: a model-less caster with more after it, an empty table,
  render skipped, fog / depth bias / border colour on and off, a render target without a depth surface, receivers
  hidden / in view / flagged; stand-in D3DX in double precision); TestShadowVolumes (two-sided or not, the blend
  override cleared, the pixel shader lock recorded during each draw); TestShadowCasters (add three, aim along every
  case incl. x equal with z not and -0 / 0, remove in buckets / chained / behind an empty value / missing).
  Mutations: 31 deliberate bugs, all caught (24 at first; the exact-limit triangle, the blend override cleared,
  the lock recorded in the draw, the model-less caster moved before others, a missing depth surface, the x-only
  aim case and near-identity caster matrices - so triangles are really drawn - were added for the other 7).
- [x] Batch 127 (AM-4; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; a scene's mesh list): src/game/gfx/mesh_list.{h,cpp} (new; Nx::sScene).
  - 0x004DDD50 `CompareMeshes` (qsort's: the material's sort key - not a number counts as different, greater
    later -, its +0xC, the same material, its flag 0x10, the mesh's +0x44, the material's address), 0x004DE300
    `sScene::Construct`, 0x004DE490 `DestroyBody` (the material table and its buckets, each mesh's first vertex
    buffer, the list, the records array, the COM object), 0x004DE340 `UpdateSphere` (x and y halves in double,
    z's size rounded first, as the original), 0x004DE3E0 `AllocateMeshList`, 0x004DDDF0 `CountMeshes`,
    0x004DDE20 `AddMeshes`, 0x004DDE60 `RemoveMeshes` (each gap closed; a null already in the list is taken for
    one - with none to be found the original never ends, and neither does this), 0x004DDFA0 `SortMeshes` (the
    game's qsort, then the transparent / flagged ranges recounted), 0x004DE020 `FindMesh` (by load index),
    0x004DE420 `FindMaterial` (by checksum, through the table's iterator), 0x004DE060 `BreakStrips` (records
    that match the mask mark a strip break in their mesh's indices; each mesh with breaks gets a rebuilt index
    buffer with the break's index repeated twice more; the original's 64-entry break table is overrun by load
    indices of 64 and up - here those are skipped), 0x004DE560 `DeleteScene` (each material destroyed and
    freed, then the scene).
  Test (src/hook/selftests_appmain4.cpp, Early): TestMeshList - 4 seeds of 24 meshes over 8 materials (sort keys
  equal / different / not a number, flags), CompareMeshes on every pair, then construct (state recorded), count
  (recorded), allocate, add, sort, find (also missing), find materials (also a checksum of 0, no table), the
  sphere from 300 bounds of every size, strip breaks (masks, records found at the first place looked or later,
  some not matching, none), remove (also absent and repeated), destroy or delete; the buffers / allocation /
  vertex buffers stood in; all memory compared. Load indices kept under 64 (see above).
  Mutations: 26 deliberate bugs, all caught (20 at first; the states recorded after construct / count, more
  bounds, a material checksum of 0 and records matching at once were added for the other 6).
- [x] Batch 128 (AM-5; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; pixel shaders and one-pass materials): src/game/gfx/pixel_shaders.{h,cpp},
  simple_material.{h,cpp} (new).
  - 0x004DD7F0 `CreatePixelShaders` (once: the handles cleared; without pixel shaders the stand-in numbers 1-10 the
    fixed-function stages tell apart, else the six fixed shaders assembled - their sources as literals - and
    created; the shadow caster table made), 0x004DD2C0 `GeneratedPixelShader` (a material's passes: the key from
    the pass count, modes, passes and per-pass alpha flags 0x004D1D90; the cache 0x7CA9C0 - the first item with
    the key and a shader -; else the ps.1.1 text written as the original sprintfs it, assembled, created and
    cached; the "texture only" branches are unreachable since every pass counts as present, as in the original),
    0x004DDC00 `MaterialPixelShader` (one pass: the fixed shaders by texture / brighten / alpha flags; more: the
    generated one).
  - 0x004D9B80 `sSimpleMaterial::Construct` (the texture dictionary's entry, direct or indirect; a 0x110-byte
    material: sort key rand() * rand() with the game's rand, one pass, blend mode by name with the fixed alpha in
    the top byte, colour 0.5, alpha fixed / 128; the shader by mode), 0x004D9CD0 `DestroyBody`, 0x004D9CF0
    `BlendModeFromName`, 0x004DA6D0 `SkipFileChunk`, 0x004DA710 `SkipMemoryChunk`.
  Test (src/hook/selftests_appmain5.cpp, Early): TestFixedPixelShaders (with / without pixel shaders, once only,
  stand-in D3DXAssembleShader recording the text), TestGeneratedPixelShaders (3 x 160 materials of 0-4 passes,
  every mode, alpha flags, failing assembly, cached keys later left without a shader), TestSimpleMaterial (every
  blend name and others, textures found / indirect / missing, fixed alphas incl. out of range, destroy, the chunk
  skippers).
  Mutations: 30 deliberate bugs; 29 caught (27 at first; cache items left without a shader and all 18 names in
  the constructor were added for 2). The other (gen_reg) changes the unreachable "texture only" branch.
- [x] Batch 129 (AM-6; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; screen effects and the 2D draw list): src/game/gfx/screen_fx.{h,cpp},
  draw2d.{h,cpp} (new); sprite.{h,cpp} and selftests_sprite.cpp (shared files, additive: the SSprite methods
  below; SSprite::Draw now calls Nx::SetVertexFormat directly and its tests stand in for ours too).
  - 0x004DE5E0 `DrawScreenQuad(blend, target, source, du, dv, colour, shader)` (fog off meanwhile; the target's size
    read unsigned; textured with half a texel of the target, or flat with the vertex-colour shader), 0x004DEA80
    `BeginScreenEffects` (motion blur or bloom: the screen copy as the target, cleared to the fog's colour or
    0x786CC0), 0x004DE980 `CopyScreenQuarter`, 0x004DEB70 `Bloom` (the cut-off shader into a copy, two blur passes
    at an eighth and a sixteenth, then both added over the frame as the original builds the quads).
  - SDraw2D (the 2D draw list at 0x7CC248, by priority: `priority <= head` goes first, equal ones after the others,
    not a number as the original's comparisons give): 0x004DF900 `Insert`, 0x004DF950 `Remove`, 0x004DFA70
    `Construct`, 0x004DFAA0 `DestroyBody`, 0x004DFC90 `Destroy`, 0x004DFAE0 `SetPriority`, 0x004DFB20 `SetZ`
    (above 0: priority 0), 0x004DFB70 `SetHidden`, 0x004DFB90 `Draw2D` (each shown item's begin / draw / end; the
    next frame's z test on if any is above z 0). SSprite: 0x004DFC50 `Construct`, 0x004DFC80 `DestroyBody`,
    0x004DFCC0 `Destroy`, 0x004DF990 `BeginDraw`, 0x004DFA40 `EndDraw`.
  Test (src/hook/selftests_appmain6.cpp, Early): TestScreenEffects (6 seeds: quads of every blend with / without a
  source and shader, sizes over 2^31, both screen effect modes, the fog / clip flags, bloom on / off, real screen
  sizes); TestDrawList (12 items and sprites, 300 random operations with priorities equal / -0 / not a number,
  then Draw2D with z only 0 / -0 / below / not a number, the z test from the last frame on and off, a listed item
  marked hidden).
  Mutations: 29 deliberate bugs, all caught (25 at first; the end phase of Draw2D and real screen sizes were added
  for the other 4).
- [x] Batch 130 (AM-7; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; a mesh's operations): src/game/gfx/mesh.{h,cpp} (new; Nx::sMesh 0xD8 bytes,
  sVertexBuffer, sIndexBuffer, sBillboard).
  - 0x004D6D40 `sMesh::AddVertexBuffers(count)` (copies of the first buffer), 0x004D6E00 `MoveTo(position)` (the
    transform made if needed; vertices, collision positions and the sphere moved by the difference; the
    transform's position all four), 0x004D6FB0 `TurnY(turns)` (quarter turns about the transform's position, the
    original's rounding per case; Ghidra splits it - REPLACE_SOURCE_ONLY(0x004D6FBA) registers the second
    "function"), 0x004D71F0 `Clone(share)` (shared: flag 1; else new vertex / index buffers copied - the original
    uploads the source's indices again too -, per-vertex data, the extra vertex buffers, per-vertex bytes; the
    collision triangles and positions always copied), 0x004D7580 `MakeBillboard(type, pivot, axis)` (the quad's
    plane from its first triangle; type 1 screen-aligned, 2 about the axis unless it's within 0.99 of vertical;
    the four vertices rewritten in the billboard's axes about the pivot).
  - Note: Mth::CrossProduct (0x00402550, vector.cpp, another range) computes in float; the original's products are
    double. mesh.cpp (and camera_util.cpp) use a local exact copy; vector.cpp's own may want the same fix.
  Test (src/hook/selftests_appmain7.cpp, Early): TestMeshOperations (stand-in buffers / allocation: move (also w),
  every turn count, clone shared / not with every optional part, more vertex buffers, billboards of each type and
  axis incl. vertical and degenerate quads).
  Mutations: 27 deliberate bugs; 23 caught (22 at first; a position with w other than 1 was added for 1). Of the
  other 4, two re-copy fields the memcpy already copied (no effect), one (cl_9c) didn't compile, and one only
  reorders a sum before a float rounding.
- [x] Batch 131 (AM-8; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; drawing a scene's meshes and the light glows): src/game/gfx/scene_render.{h,cpp},
  glow.{h,cpp} (new).
  - 0x004DC700 `RenderScene(scene, flags, viewport)` (flags: opaque 1, transparent 2, occlusion 4, no culling 8, near
    first 0x10 - not with 0x1000 or more opaque meshes -, shadow volumes only 0x20, statistics 0x40): each mesh shown
    and not hidden, in the viewport's bit, in view and not occluded; the material set when it changes; near first:
    meshes up to depth 600 now (not a number: never), the rest after; the meshes drawn remembered for the shadows
    (0x792640, 4096 at most, the last slot reused) with z writes off and the anisotropic filter dropped meanwhile;
    then the transparent ones, the flagged ones sorted by depth (0x004DA690 `CompareDistances`, the game's qsort)
    and the rest.
  - 0x004DCC70 `UpdateGlows(queries)` (the glow table 0x7CA9D8: with queries, each glow with fewer than three
    outstanding gets a free query of the 1024 - none free: the last, unmarked - and its quad written; else each
    glow's oldest query read (0x005F4C80; not on a Radeon 8500): answered - pixels or not -, still pending - the
    last answer -, the size grown toward its maximum or shrunk by its rate; its quad written at alpha 0x40. The
    quad goes to 0x7CA9F0 and nothing draws it in this build).
  Test (src/hook/selftests_appmain8.cpp, Early): TestRenderScene (24 seeds over every flag combination that
  matters: 40 meshes shown / hidden / culled / occluded in random viewports, depths around 600 (also exactly and not
  a number), many flagged ones with equal depths, two scenes of over 4096 meshes - the drawn list's cap, the
  transparent limit -, the unknown +0x40 set, shadows on / off, the anisotropic filter on / off); TestGlows (12
  seeds: queries handed out / all in use / three outstanding / a single glow with query 0 free, answers 0 / 1 /
  other, the Radeon flag, the camera's column of every kind incl. a zero z and a tiny one).
  - DrawShadowVolumes (batch AM-3) now takes the viewport the original passes it (unused).
  Mutations: 28 deliberate bugs; 27 caught (17 at first; the flag table, many flagged meshes, scenes of 4096 and
  more, a culling-free unknown +0x40 case, single glows and zero / tiny camera columns were added for 10). The
  other (gl_u) only changes the sign of a zero that later sums lose.
- [x] Batch 132 (AM-9; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; textures, image loaders, key names): src/game/gfx/texture.{h,cpp},
  texture_file.cpp, src/game/sys/key_names.{h,cpp} (new).
  - Nx::sTexture (0x20 bytes) and its list (0x7CC254, doubly linked): 0x004E04B0 `Construct` (listed first),
    0x004E0500 `ConstructCopy` (a managed Direct3D texture of the source's size, levels and format, each level's
    rows copied - 4x4 blocks for DXT1 (8 bytes) / others (16)), 0x004E06A0 `DestroyBody` (unlisted; released; off
    any of the first four stages it was cached on), 0x004E03F0 `Unlist`, 0x004E0380 `Set`, 0x004E03A0 `Lock`
    (null when it fails), 0x004E03D0 `Unlock`, 0x004E03E0 `PaletteOwner`, 0x004E0700 `CreateRenderTarget` (the
    surface asked for even when the creation fails, as the original), 0x004E0320 `ReleaseRenderTargetTextures`
    (the eight stages emptied straight on the device; every render target's texture and surface released),
    0x004E0440 `RestoreRenderTargetTextures`.
  - 0x004E0790 `LoadTextureFile(name)` / 0x004E0A90 `LoadTextureMemory(data)`: the engine's image format (a
    32-byte header, a palette for formats under 0x10 and 0x13, the texels swizzled unless a size isn't a power of
    two or - memory only - the id is 0x9B), into an A8R8G8B8 managed texture; unswizzling (0x005F5AF0) and the
    palette (0x005F45E0) are still the game's.
  - 0x004E0DA0 `SetKeyNames` / 0x004E0FE0 `SetGermanKeyNames`: the keyboard's short key names (the German keypad
    "ZB"), Main's language start-up.
  Test (src/hook/selftests_appmain9.cpp, Early): TestTextures (6 seeds: construct, render targets made and failing,
  copies of 1-3 levels in A8R8G8B8 / DXT1 / DXT5 with their own pitches, unlisting the head / the middle / a
  stranger, release / restore incl. a listed plain texture and a failing re-creation, destroyed while cached on
  stage 3, lock failing); TestTextureLoaders (every format incl. 0x10, sizes power of two or not or zero, the 0x9B
  id, a missing file, a failing texture); TestKeyNames (both languages' calls and texts).
  Mutations: 29 deliberate bugs; 28 caught (20 at first; textures with distinct contents and pitches, the head
  unlisted, a plain listed texture, a failing re-creation, the stage-3 cache and format 0x10 were added for 8).
  The other (lf_pal) changes how much palette is read where the test's file always has a whole one (a shorter one
  leaves the original's stack bytes in it).
- [x] Batch 133 (AM-10; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; the Avoid and CameraLookAround components, the cameras' helpers, the
  component types, the goal editor's helpers): src/game/obj/avoid.{h,cpp}, camera_lookaround.{h,cpp},
  camera_util.{h,cpp}, component_types.{h,cpp}, src/game/ed/goal_editor_util.{h,cpp} (new); shared test helpers in
  src/hook/appmain_components.h.
  - CAvoidComponent (type 0x6EB050ED "Avoid", 0xDC bytes: a ped jumping out of the local skater's way along a slerp,
    playing JumpLeft / JumpRight / JumpBack / JumpForward): 0x004E39E0 `Create`, 0x004E3310 `RegisterMemberFunctions`
    (not a Ghidra function), 0x004E3660 `Construct`, 0x004E3330 `DestroyBody`, 0x004E36C0 `Destroy`, 0x004E34C0
    `SideAwayFromSkater`, 0x004E3A40 `TryJump`, 0x004E3340 `PlayJump`, 0x004E3270 `AvoidDistance` (params in ESI:
    a naked stub), 0x004E3930 `LineCollides` (EDI / ESI: a naked stub), and its script member functions (the table at
    0x006839E8, names unknown): 0x004E3BD0 `Jump`, 0x004E36F0 `DebugBox`, 0x004E3EC0 `StepJump`, 0x004E37D0
    `JumpFinished`, 0x004E3810 `StorePosition`, 0x004E3850 `StoreTarget`, 0x004E38C0 `HoldMotion`, 0x004E38F0
    `RestoreMotion` (0x004E37D0 / 0x004E3810 / 0x004E3BD0 / 0x004E3EC0 aren't Ghidra functions). Kept from the
    original: the quarter from _ftol2's low half (not a number: ahead), the herd check passing for not a number, the
    fourth try's angle in radians where the others are degrees, the checksum / distance read into the parameters'
    own argument slot.
  - CCameraLookAroundComponent (type 0x81C7122D, 0x44 bytes): 0x004E42E0 `Create`, 0x004E3F60 `DestroyBody`,
    0x004E4010 `Destroy`, 0x004E3F70 `InitFromStructure`, 0x004E4040 `Finalize`, 0x004E4060 `Update` (the pad's
    sticks eased into heading and tilt, a button for a script, one to lock), 0x004E4220 `CallMemberFunction`,
    0x004E3FA0 `TurnTo` (not a Ghidra function), 0x004E4000 `SetLocked`; 0x004E3F90 (a shared vtable stub: a tail
    jump to slot 2; REPLACE_UNCHECKED).
  - Cameras: 0x004E4340 `FrameBlend(rate, seconds)` (returns double: the original leaves it on the x87 stack),
    0x004E4360 `AvoidCameraCollisions` (pulled in to the target, pushed off side walls, refocused).
  - 0x004E4860 `RegisterComponentTypes`: the 81 component types with their create / member-function-register
    functions, in the original's order (names where the checksum's is known).
  - Goal editor: 0x004E6600 `GoalObjectModel`, 0x004E66A0 `GoalObjectName` (fastcall), 0x004E6730
    `EditedGoalNodeName` (TRG_EditedGoal%d_Pro / _Restart / _Object_%d), 0x004E67A0 `GoalTypeStructure`, 0x004E6890
    `GoalTypeDefaults` (type in EAX: a naked stub), 0x004E69C0 (returns 0x82), 0x004E69D0 `InitEditedGoal` (its
    callers keep using ECX: a naked stub keeps it), 0x004E6AE0 `RunMarkerObjectScript`.
  Test (src/hook/selftests_appmain10.cpp, Early): TestAvoidComponent (6 seeds in a stand-in world: made / failing,
  the distance per quarter incl. borders and not a number, the side away for every skater heading, ten jumps tried,
  every jump animation case, eight jumps with steps and ends incl. a not-a-number progress, the points and motion
  flag), TestCameraLookAround (6 seeds x 40 frames: both pads, the globals, either object, sticks at rest / moved,
  turning with and without time, member functions), TestCameraUtil (30 blends, 24 collision cases with every probe
  outcome), TestComponentTypes, TestGoalEditorUtil (every type and index, node names, structures, defaults, the
  marker script).
  - 0x004E2D20 `CFlipRotateComponent::GetPose` (the FlipRotate animation controller, src/game/obj/flip_rotate.{h,cpp} -
    a shared file, additive): the bone 0x98971FAF turned half round when the object's SkaterFlipAndRotate component
    says so; flipped, each bone from its mirror bone with x mirrored. Test: src/hook/selftests_appmain12.cpp
    (TestFlipRotatePose: 12 seeds, the component missing / off / on, bone found / not / 0, flipped or not, 0 / 3 / 64
    bones, mirror bones of every kind).
  Mutations: 47 deliberate bugs; 42 caught (after recording the motion flags after each restore, a turn lasting
  exactly one frame and bone 0). The other 5 only move a comparison's border onto exact equality (fromSkater 30, the
  ease's 0.001, the minimum distance 13.9) or can't change the result (a quarter of exactly 0 and 4 are both ahead;
  a stick of exactly 0 scales to 0 either way).
  - Note for the lead: the verify build's PDB kept breaking under load (C1033 / C2471, LNK1102 out of memory in the
    32-bit linker); a private PDB server (`set _MSPDBSRV_ENDPOINT_=<name>`) and a clean rebuild fixed it.
- [x] Batch 134 (AM-11; appmain agent, merged 2026-10-19; run in the game: docs/pending/appmain.md; NOT judged by a player yet; the editor camera component): src/game/obj/editor_camera.{h,cpp} (new).
  - CEditorCameraComponent (type 0xAC8946E2, vtable 0x0064B2D0, 0x9C bytes; the create-a-goal / park editor camera: a
    cursor walked over the ground with the left stick or the d-pad, the camera orbiting it at a heading and tilt from
    the right stick, zoom and height buttons by controller type, a "Ped_Shadow" under the cursor): 0x004E5940
    `Create`, 0x004E5060 `Construct`, 0x004E5100 `DestroyBody`, 0x004E59A0 `Destroy`, 0x004E4ED0 `InitFromStructure`
    (min_height, the zoom range, SimpleCollision, 0xF5680DEF - no wall check), 0x004E51F0 `Finalize` (the Input
    component), 0x004E59D0 `Hide`, 0x004E5210 `CallMemberFunction` (0x69CB3AE1 cursor to Position= and update,
    0xF362BBBF placed quietly with zoom / tilt / height), 0x004E5C20 `Update`, 0x004E5170 `CreateShadow` (the scale
    written with no null check, as the original), 0x004E5A10 `UpdateShadow`, 0x004E5460 `GroundHeight` (no ground,
    too steep - normal y under 0.087 -, a flagged object's node script or one in the way: false), 0x004E5640
    `PlaceCursor` (up: the height lowered by the rise unless simple collision on another object with the global
    0x71EC75E9 at or over it; down: the drop added unless the camera's at most 1 up and the drop within the step).
  - The ground checks: 0x004E4F50 `ScriptFlagged` (a script, or one it calls by name - also the `name 0x42 name` form
    -, is one of the four at 0x0067EFD8; 0x0067EFE8's and those already seen are not; the seen list 0x007CCBE8 has 20
    places and no bound check, as the original), 0x004E5320 `FlagScriptedHit` (a feeler's callback: an object whose
    node's TriggerScript is flagged), 0x004E5390 `FlaggedObjectBetween` (`end` in ESI, `start` in EDI: a naked stub).
  - Kept from the original: the heading wrapped by comparing the unrounded sum, the camera's w from the cursor's
    pre-move w (then overwritten with 1), the x87 roundings where it stores and doesn't.
  Test (src/hook/selftests_appmain11.cpp, Early): TestEditorCamera (8 seeds in the components' stand-in world: made /
  out of memory, every InitFromStructure flag, 30 frames with the controller type changing every frame (pad, 4, 5,
  7, the 0x7ECA21E5 platform or not), random buttons over all 32 bits, sticks at rest / in the dead band / moved, the
  d-pad, headings past both ends, feelers hitting or not (ceilings, steep and 0.087 ground, walls, flagged node
  scripts), in and out of the park editor, hidden / shown, both member functions and an unknown one; the flag search
  over scripts calling scripts (short jumps with and without 0x39, the 0x42 forms, loops, the excluded script); 96
  directed PlaceCursor cases (drops under / at / over the step, rises, camera 0.5-2.5 up, simple collision, the
  editor's own camera, the global under / over the height); four frames lowering onto / past / under the minimum
  height).
  Mutations: 66 deliberate bugs; 65 caught (59 at first; the script short jumps with a call after them, controller
  types per frame with 32-bit buttons, zoom ranges under 100, the directed PlaceCursor cases and four lowering frames
  around the minimum height were added for 6). The other (up_cross) adds rather than subtracts atZ x the right axis's
  y, which is always 0.
  - Note for the lead: the session scratchpad is shared with another agent ("viewer", worktree agent-a12992d40fe5be1e3):
    its build.cmd replaced mine for a while, so a few of my mutation builds ran in its worktree (nothing of mine was
    written there; the results were re-run with a private build script).
- [x] Batch 135 (VW-1; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline): the rest of the viewer module (Mdl::Viewer,
      src/game/sys/viewer.*), 0x577380-0x577F20 - the constructor 0x577F20 (Module::Construct, vtable 0x0064EBC4, no
      commands; unless the debug checks 0x00684FA8 are on - they are in this exe - the PC tool's app server "THUG2
      Viewer" (port 10000, flags 0x10; a first inet_addr(local IP) and a 0x005B9760 call on the net manager are
      unused); the free camera task (0x20 bytes, vtable 0x006476F0), the input handler (0x2C, vtable 0x0064D7D8,
      controller = the script integer 0x702247A5), speeds 400 / 120, "Creating shift input handlers\n)", the Select
      combo task and the shift handler (priority 1000), the singleton 0x007CE4A8), v_start_cb 0x577DA0 (in the front-end
      heap: the PC tool's seven message handlers (0x20 ReloadScene, 0x21 0x0052DAC0, 0x22 the script reload, 0x2A
      AddToScene, 0x23 / 0x24 / 0x2B with the viewer as data, priority 0x80), the tasks onto the main loop's list, each
      handler onto its controller's list (+8), its control data (+0x44) and device (+4)); the tasks Ghidra never made
      into functions: the free camera 0x577870 (the frame's time from Tmr::GetTime into 0x007CE4AC; command 0x4000
      toggles the mode - to 1 only with a viewer object; dead zone: raw sticks within +-50 give 0, else (v -+ 50) / 78;
      on viewer_cam: mode 0 the left stick pitches about its own x (RotateLocal) and yaws about the world's y (its
      matrix times a y rotation), mode 1 the viewer object's 0x0058A2E0(leftX, leftY); the right stick moves it along
      its own axes (MoveObjectLocal, x and z); commands 2 / 4 up / down; unless the script integer 0xD8C91578, commands
      0x10000 / 0x20000 roll (a z rotation before its matrix, -+ half a degree a second times the speed); the camera
      unpaused; command 0x1000 the next of six settings (0x004A0710 / 0x004A0AD0, a table at 0x00649CC8); the commands
      cleared. Found: without viewer_cam the original goes on with a null object (the moves and the unpause write
      through it - a crash), kept as is) and the Select combo task 0x577380 (with select_shift, the first of
      UserSelectTriangle / Square / Circle / Start / X / L1 / L2 / R1 / R2 / Up / Down / Left / Right whose button was
      pressed with Select held, spawned if it exists); the PC tool's handlers 0x577CD0 (ReloadScene, scene = the
      message), 0x5774C0 (AddToScene, scene + Add at +0x80), 0x5775A0 (a viewer object from modelname = the message,
      skeletonName = the checksum at +0x84 - found: the viewer is taken from the message's first word, not the
      handler's data at +0x18, like the remove handler 0x577140 already rewritten; kept), 0x577670 (the script named by
      the message's first word, with the structure the script reader 0x0047A0C0 reads after it); and 0x577740
      (PlaceCamera, called by CFunc_LaunchViewer's neighbour 0x005A1C80 with the viewer: with the script global
      ResetCameraToViewerObject, viewer_cam's matrix the identity and its position ViewerObjPos + (0, 35, 100) - (0, 35,
      100) without one; RET 4, returns true). Names from the game's .qb checksum tables (UserSelect*, viewer_cam,
      AddToScene, ReloadScene, modelname, skeletonName). Debug mode's keyboard flying (Sys::DebugMode::KeyboardViewer in
      HandleViewerInput) is kept. Self-tests (new src/hook/selftests_viewer.cpp and the shared stand-ins
      src/hook/viewer_testkit.h): TestViewerFreeCamera (132 frames: every dead zone edge and NaN, modes 0 / 1 / 2, every
      command and combinations, with and without a viewer object and the roll lock; PlaceCamera off / no camera / with
      and without ViewerObjPos), TestViewerModule (the combos: 24 button sets x select_shift x scripts there or not; the
      four handlers; the start callback with and without the debug checks, handlers on either controller; the
      constructor both ways) - matrix builders and the D3DX product stood in for both alike, every call recorded, the
      whole arena compared. 41 deliberate bugs: 39 caught, 1 equivalent (the dead zone's edge: exactly 50 gives 0 both
      ways), 1 didn't build (rerun with batch VW-2).

- [x] Batch 136 (VW-2; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline): cars made in code, the King of the Hill crown
      and two list entry types of the skater's trick code (0x5780E0-0x578670, 0x578740, 0x5787F0, 0x579570-0x579820).
      New src/game/obj/game_objects.*: InitCar 0x5780E0 (the motion components (0x57B2F0 / 0x57B420 - later
      batch), the motion's speeds 216 / 384 / 528, 0x00423C90 / 0x00423510's 240, car_add_components, Finalize, the
      model's update 0x00420480) and CreateCar 0x578170 (OBJECT_TYPE_CAR, base components 0x57B250, one more 0x58-byte
      component 0x0041BAD0, at the node's position, registered); Obj::CCrownObject (0x114 bytes, vtable 0x0064EC44):
      Construct 0x578240 (type 0xD2156C50, id crown_object), PutOn / TakeOff / IsWorn 0x5782B0 / 0x5782D0 /
      0x5782E0, DestroyBody / Destroy 0x5782F0 / 0x578460, InitFromNode 0x578490 (registered with the given
      manager, a model component, never suspended (the suspend component's 1000000-1000003), crown.mdl through
      0x004879D0 (returns a String: made in raw storage), at the node), OwnUpdate 0x578310 (vtable +0x28: the skater's
      head bone matrix (0x004A36E0), turned 90 about x, raised 18 (0x0048A260), in the skater's frame, at its
      position). New src/game/skater/trick_lists.*: TrickListEntry (0x64 bytes, vtable 0x0064EC80: Construct
      0x578740 with its own script array, DestroyBody 0x579570, Destroy 0x579820, SetFlag 0x578670) and TrickNameEntry
      (0x34 bytes, vtable 0x0064EC84: Construct 0x5787F0, DestroyBody 0x579630, Destroy 0x579600). Found: our String
      constructor clears m_capacity where the original's leaves it (string.cpp says so) - the test zeroes that word.
      Self-tests (new src/hook/selftests_viewer2.cpp): TestGameObjects, TestTrickLists. 28 deliberate bugs: all caught
      (4 after strengthening: constructors now run on 0xCD-filled memory and their objects are compared right after
      construction).

- [x] Batch 137 (VW-3; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline): the gap checklist (new
      src/game/skate/gap_checklist.*, Game::CGapChecklist and CGapCheck), 0x578680-0x5796C0 incl. the missed
      0x578AA0, 0x578CF0, 0x578D50: CGapCheck (0x50 bytes, vtable 0x0064EC7C: Construct 0x5786E0, DestroyBody 0x579510,
      Destroy 0x5794E0) and its Lst::Node / Lst::Head destructors 0x578680 / 0x5786A0 / 0x5794B0 (the head's: vtable
      0x0064ED20); CGapChecklist: Construct 0x579690, Clear 0x578840, AddGap 0x578880 (found again by name (case
      counts) and score; else sorted after the last lower score / same score and name sorting first, ignoring case;
      script heap), AddGapFromNode 0x578A00 + its callback 0x578AA0 and Build 0x578AC0 (every EndGap the TriggerScripts
      reach; PrintGaps), FindGap 0x578B30, SetGapPositions 0x578BC0 (two Vectors by value), GotGap 0x578C70,
      ClearPending 0x578CF0, Count 0x578D20, AllGot 0x578D50, Reset 0x578D90, GetGapTotals 0x578DD0, AddGapsToMenu
      0x578E40, CreateGapList 0x579080, GetGap / GetGapName / GetGapCount / GetGapScore / GetGapPositions
      0x5793D0-0x579460, AwardPendingGaps 0x5796C0 (the goal manager told; got_gap_for_first_time,
      multiple_new_gaps from the second on - a signed byte count); the script commands AddGapsToMenu 0x5792E0,
      CreateGapList 0x579300, GetLevelGapTotals 0x579320 (found: level= uninitialised in the original if missing;
      ours 0), GiveAllGaps 0x579360. Names from the .qb checksum tables. Self-test (new src/hook/selftests_viewer3.cpp):
      TestGapChecklist (every add order case, found again, case-only differences, the queries, landing and awarding
      with the goal manager listening or not, the four commands with no / one / many gaps, building from stand-in
      node scripts with and without PrintGaps, a fresh build, the destructors). 32 deliberate bugs: all caught (3
      after strengthening: a fresh Build with no gaps, Reset with a gap out of the level, a gap landed twice).
      viewer_testkit.h: InstallCommon(s, deleteStruct) and g_printfArgs (shared kit, my files only).

- [x] Batch 138 (VW-4; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the balance of a balance
      trick (new src/game/skater/manual.*, Obj::CBalance - a manual, grind, lip or skitch; 0x58 bytes, vtable
      0x0064ED24), 0x579850-0x579CC0 incl. the missed 0x579900 / 0x579910 / 0x579940 / 0x5799A0 / 0x579A00:
      DestroyBody 0x579850, Init 0x579860 (SkaterBalanceTrick, SkaterScore, input), Reset 0x5798A0
      (reset_balance_indicator), ClearBestTime / HideDisplay / ShowDisplay / KeepBestTime / ShowMeter 0x5798F0-0x579940,
      Construct 0x579950, Destroy 0x5799A0, Stop 0x5799D0, Start 0x579A00 (grinds: Same_Grind_Add_Time /
      New_Grind_Sub_Time by the rail (0x0057C200), robot_rail_add_time / robot_rail_nudge for runs under half a
      second; the lean speed repeated (Repeat_Multiplier, at least Repeat_Min; manual / Nosemanual / random side), the
      lean repeated (Lean_Repeat_Multiplier + the cheese), kept within 0.9 x Lean_Bail_Angle), Update 0x579CC0
      (CheeseFrames, the score's trick time 0x00564270, the stats manager, the lean's growth (Lean_Gravity_Stat,
      Instable_Rate / Instable_Base), the buttons (Lean_Acc after BalanceIgnoreButtonPeriod or a release - found: the
      safe-period scale is an unsigned integer division, so 0 inside it), the random shake (Lean_Min_Speed /
      Lean_Rnd_Speed), the bail (OffMeterTop / OffMeterBottom), the anim blender's meter (lean + 4096 over 8192,
      flipped), the score's displays 0x00563370 / 0x00563390 (Up / Down buttons)). x87 rounding followed: sums kept
      unrounded where the original keeps them on the stack. Self-test (new src/hook/selftests_viewer4.cpp):
      TestBalance (starts for each balance type and state, 96 frames with every button / period / flip / display
      case; the physics values, GetFloat, the frame time and Sign stood in by C++ functions jumped to).

- [x] Batch 139 (VW-5; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the movie manager (new
      src/game/obj/movie_manager.*, Obj::CMovieManager: a CObject, vtable 0x0064ED34, 0x12C bytes - the skate module's
      +0x368), 0x57A180-0x57B240 incl. the missed 0x57B070 / 0x57B0B0, and the list / copy helpers there: UseKeptCamera
      0x57A180, CObject's and Gfx::Camera's compiler-made copy assignments 0x57A190 / 0x57A220, Lst::Node<CMovie> /
      Lst::Head<CMovie> destructors 0x57A200 / 0x57A2C0, the shared Lst::Head DeleteAllNodes 0x57A2F0 (11 callers),
      Count / GetMovie / FindMovie 0x57A330-0x57A3A0, KeepCamera 0x57A450, Construct 0x57A4B0, GetCamera 0x57A530,
      StopAll 0x57A5A0, ClearMovies 0x57A640, EndMovie 0x57A6B0 ("Details have been removed %ld" with the 64-bit
      render frame; the end script with a copy of its parameters), EndMovies 0x57A7E0 (skippable chains; a
      compiled-out walk kept), the by-name calls 0x57A940 / 0x57A9B0 / 0x57AA20 / 0x57AA70 / 0x57AF60, KillMovie
      0x57AAC0, AddMovie 0x57AB60 (asset camera animations of kinds 5 / 9, cutscenes, skater cameras clearing the
      queue; found: a missing asset returns without restoring the kept-camera flag; an unknown type is a null movie),
      Update 0x57AE60 (vtable slot 8), CallMemberFunction 0x57AFC0 (slot 3), GetPlayingName 0x57B030 (used by
      menu_backdrop.cpp), PlayingFlag24 0x57B070, AnyMovies 0x57B0B0, Playing2C / Playing30 / Playing34
      0x57B0E0-0x57B160 (tail jumps through the movie's vtable; found: 0x57B120 passes its caller's argument on),
      DestroyBody / Destroy 0x57B1A0 / 0x57B220. Self-test (new src/hook/selftests_viewer5.cpp): TestMovieManager
      (stand-in movies with a recording vtable; every add / query / end / kill / update case, the helpers, the
      destructors). viewer_testkit.h's debug output records %s arguments as text, the others raw.

- [x] Batch 140 (VW-6; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the rail manager (new
      src/game/obj/rail_manager.*, Obj::CRailManager and CRailNode - 0x54 bytes), 0x57C170-0x57E960 incl. the missed
      0x57C200: the box / interval helpers Span4 0x57C170, BoxPair::Swap 0x57C210, BoxOf 0x57C260, Overlap 0x57C360,
      NearlyEqual4 0x57C3A0, NodesTogether 0x57C5A0 (stdcall), Record24 / Record34 copies 0x57C500 / 0x57C540;
      Cleanup 0x57C330, MoveNode 0x57C3F0 (found: its previous-node fix-up tests the moved node's own link count
      times - never fires), SetObjectMatrix 0x57C4F0, RailClear 0x57C600 (cdecl, two Vectors by value: feelers across
      the rail and down 12 on each side; always clear in the park editor's level), ToWorld / GetWorldPos 0x57CB20 /
      0x57CB60 (both return their out), FindNode 0x57CBD0, Init 0x57CC10 (returns this), CRailNode::GetWallSide
      0x57CC30, GetClimbMatrix 0x57CFB0, AddNode 0x57D090 (Class / type / flags / TerrainType / climbing angles),
      RemoveOverlaps 0x57D260 + RemoveOverlapsIfNotPark 0x57D7A0, SetActive 0x57D7C0, IsActive 0x57D840, FindLadder
      0x57D880, FindLadderOnLine 0x57DA20 (Climb_Max_Snap; TRG_Ladder03 counted 4 times as far), FindRail 0x57DDA0,
      FindJoiningRail 0x57E170, FindGrindRail 0x57E2D0 (Rail_Max_Snap; found: a better-scoring rail out of reach still
      takes the best score and drops what was found), AddFromNodeArray 0x57E960 (RailNode / ClimbingNode, a name hash
      table with linear probing, TrickObject clusters, Links; DefaultLine wins). Every feeler's x87 rounding followed
      (some sums kept unrounded, as the original's stack). Self-test (new src/hook/selftests_viewer6.cpp):
      TestRailManager (a level of rails, an overlapping rail, a ladder, a lone node; feeler hits scripted call by
      call; the checkers stood in; building from a stand-in node array with a hash collision). The x87 original of
      ClosestPointsBetweenLines differs from ours by an ulp now and then: both runs use ours there.

- [x] Batch 141 (VW-7; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the component set-up
      helpers and peds (game_objects.*: AddBaseComponents 0x57B250 - suspend and a 0xB87C8E43 component; AddMotionComponents
      0x57B2F0 - motion, objectproximity, sound, stream, lockobj; AddNodeComponents 0x57B420 - found: with no motion
      it adds the motion components but goes on with a null motion; CreatePed 0x57B4E0 - OBJECT_TYPE_PED,
      ped_add_components / ped_init_model / ped_disable_bones, the motion's speeds) and the player profile manager (new
      src/game/skate/profile_manager.*, Obj::CPlayerProfileManager, 0x40 bytes: a Lst::LookupTable of template
      profiles - its generic code 0x557F10 / 0x561430 / 0x5628B0 / 0x562970 called - two profiles in use, the current
      index (what sio/device.cpp's MenuPort 0x553E90 reads), a lock flag, three extra profiles), 0x57B620-0x57C120:
      SetCurrentIndex / GetCurrentProfile / GetProfile / GetExtraProfile / SetLocked 0x57B620-0x57B680, Init 0x57B690
      (init_pro_skaters; the first player's profile is the first template itself, the second's a copy),
      GetTemplateName 0x57B770 (found: returns the key, not the profile), ResetSecondProfile 0x57B790, GetTemplate
      0x57B7E0, ResetTemplates 0x57B800, WritePros 0x57B840 (CurrentSkater, DisplayName), WriteCustomSkaters
      0x57B920, AddExtraProfile 0x57B970, the table's Clear 0x57B9E0 and destructor 0x57BBF0, SetCurrentSkater
      0x57BA20, ReadPros 0x57BAA0, AddTemplate 0x57BB40, Construct 0x57BC00, DestroyBody 0x57BC20, ReadCustomSkater
      0x57BD70 (version 2 saves only: the appearance, FaceTexture, the info; the ten stats and max_specials and the
      specials put back unless the save's are kept; returns the last lookup's result), Create / Release 0x57C0D0 /
      0x57C120 (0x7CE4B4 / 0x7CE4B8). CSkaterProfile's own methods (0x585xxx-0x586xxx) called through the game for now.
      Self-tests (new src/hook/selftests_viewer7.cpp, selftests_viewer9.cpp): TestProfileManager (the real lookup
      table, stand-in profiles; every path above), TestObjectSetup. selftests_viewer2.cpp's stand-ins for the helpers
      now cover ours too.

- [x] Batch 142 (VW-8; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the records (new
      src/game/skate/records.*, namespace Records), 0x57ECD0-0x57FE27 incl. the missed 0x57EF20: CRecord (0x10 bytes:
      initials buffer, value, number, new) Construct 0x57EF20 (XXX, 1000, 0), MaybeNewRecord 0x57ECD0,
      ReadFromStructure 0x57ED00; CRecordTable (count + a new[]-style array) Construct 0x57EF60, MaybeNewEntry 0x57EFE0
      (the later ones moved down, the last dropped), UpdateInitials 0x57ED80, ReadFromStructure 0x57F0C0 ("RecordTable",
      only if the sizes match), WriteToStructure 0x57F4E0; CLevelRecords (0x1C: high scores 5, best combos 5, arcade
      scores 10, the longest combo / grind / lip trick / manual - their values 0) Construct 0x57F120, DestroyBody
      0x57F670, UpdateInitials 0x57EDD0, ReadFromStructure 0x57F340 (found: one structure pointer for all - a missing
      one reads the one before again), WriteToStructure 0x57F7B0; CGameRecords (0xC) Construct 0x57EE70 (ABC),
      SetDefaultInitials 0x57EEA0, GetLevelRecords 0x57EEC0, InitLevels 0x57FBC0, ReadFromStructure 0x57F440
      ("GameRecords", "DefaultInitials"), WriteToStructure 0x57FC50, Create / Release 0x57EED0 / 0x57FDB0 (0x7CE4BC /
      0x7CE4C0). The CRT's vector constructor / destructor iterators written as loops (same order). Self-test (new
      src/hook/selftests_viewer8.cpp): TestRecords.
      Tests strengthened after VW-4/5/6's mutation runs: the balance (a repeated lean speed under the minimum, the
      meter shown before a start, the slow random shake, buttons from the start through the ignore period, the up/down
      displays on one side), the movie manager (the other by-name call on an empty queue, ending with nothing queued,
      one movie's counts and its last update), the rail manager (Cleanup clears the on-object flag).

- [x] Batch 143 (VW-9; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): pieces of the skater
      object (new src/game/skater/skater.*, Obj::CSkater - only the fields these use named), 0x57FE30-0x580BD8 incl.
      the missed 0x580830 / 0x5809C0: SetStatOverride 0x57FE30 (0-10 when 0x5A1180 says so), GetStat 0x580490 (the
      override or Skater_Default_Stats, the skate module's +0x3D0 if set, online: 1 / 6 / 10 by the player's rules,
      CHEAT_STATS_13: 15, +3 with the score's flag), GetScaledValue 0x5805B0 (the unnamed pair scaled by a stat, the
      switch pair clamped 0-1 when riding switch, the diff pair by difficulty (+1 in classic), the limit as a maximum
      for a rising range and a minimum otherwise), GetScaledPhysics 0x580790, GetScaledParam 0x5807F0,
      GetPhysicsStructure 0x5802E0 (skater_physics or the vehicle's: segway / mechbull / minikart / wheelchair /
      tricycle / gurney / chainsaw; slalom and shopping cart use the skater's), GetPhysicsFloat / GetPhysicsInt
      0x580400 / 0x580440 (90 / 7 callers), MatchesParams 0x57FED0 (is_pro, its opposite 0x5A3264BB, is_male /
      is_female, is_named, stance, style), SparksOn / SparksOff 0x57FFE0 / 0x580000, the getters 0x57FEB0 / 0x57FEC0 /
      0x580040 / 0x580050 / 0x580480 / 0x580880 (skatercam0 / 1), LoadCATInfo 0x580060, the +0x28C list's 70 entries
      0x580150, Write / ReadChapterInfo 0x5801B0 / 0x580210, 0x5802C0 (0x42BD60), PickRandomBoard 0x580830 (up to 20
      tries for a different one), SetPauseState 0x5808A0 ("Freezing game" / "UnPausing game" when the physics value
      0x9816E1E3 says the whole game pauses, else the physics control told), UpdateShadowLight 0x5809C0 (the model's
      light reversed, kept 0.65 below the horizon - found: z is read back as stored, x not -, faded over 16 frames
      towards straight down). Self-test (new src/hook/selftests_viewer10.cpp): TestSkater.

- [x] Batch 144 (VW-10; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): more of the skater
      (skater.*), 0x580BE0-0x581845: SetStatsFromProfile 0x580BE0 (the ten stats from the profile, or the classic
      stats in classic mode), UpdateFromProfile 0x580EB0 (in its skater heap: pushstyle, stance, pro, display name,
      is_male, skatestyle, first_name - copied unchecked into its 0x40-byte buffer, as the original -, name; a local
      skater's stance flag and flip-and-rotate component), ResetInput 0x580FF0 (24 input flags pressed then released,
      the 8th also mirrored at +0x1514's +0x1E), ResetState 0x5810E0, SetUpNetwork 0x5811E0, ShowBoardElement
      0x581260 (four board types: a texture element over a parent, its size, an optional quarter turn and overlay, the
      model's swap), Reset27C / HideSkater / ShowSkater 0x5814D0 / 0x581510 / 0x581550, GetDisplayName 0x581580
      (online: the player's name), MoveToNode 0x5815C0 (orientation reset first; then the node's position and angles,
      teleported, the physics' copies, the shadow), Pause / Unpause 0x581760 / 0x5817F0 (displays off, a script, the
      pad's vibration paused, the sounds). Self-test (new src/hook/selftests_viewer11.cpp): TestSkaterProfile.

- [x] Batch 145 (VW-11; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the skater's set-up,
      CSkater::Init 0x581850 (2.6 KB): a local skater's camera (skatercam0 / 1 found and told, or made from
      skatercam_composite_structure; Walk told; 0x59F890; the skatercamera mode), its script, the anim set
      (skater_asset_info's skater_skeleton, else thps6_human), the motion components, the components made from
      structures (animblender with SkaterAnimEventTable, skeleton, model - geometry loaded in the skater's geometry heap
      with the asset manager's flag off meanwhile -, shadow, and for a local skater vibration, SkaterStancePanel and
      skatersound), Finalize, the pad's settings from the skate module and SkaterInit (local), UpdateFromProfile and
      SetStatsFromProfile, Skater_Max_Max_Speed_Stat, the +0x258 / +0x270 pieces, the collision's flag, the model's
      update, the default anim, the eleven trick infos, spawn_add_premade_cats_to_skater, the switch to skating, the
      projectile names (CRCs of projectile_0-4), 0x42E380, the tag (a custom skater's own if has_custom_tag) and
      sticker textures. Self-test (new src/hook/selftests_viewer12.cpp): TestSkaterInit (four cases).

- [x] Batch 146 (VW-12; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): InitFromProfile
      0x5822A0 (Init; a local skater's six network message handlers when 0x5A11B0 says so; its suspend distances
      3000 / 5000 / 1e7), RunStateScript 0x5823A0 (script 0xE740F458 with Walk / 0x7B738580, run once if local),
      SwitchToScript 0x582510, WriteCATInfo 0x5825E0, the +0x28C list written 0x582740. Self-test:
      TestSkaterScripts (selftests_viewer12.cpp). Tests strengthened after the VW-6 / VW-7 mutation runs (the moved
      rail node's box seen before it moves back, the table's count cleared, the last release with a template, the
      ped motion's flags kept).

- [x] Batch 147 (VW-13; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): LaunchProjectile
      0x582800 (from Bone_Wrist_L / _R by stance and walking, through its matrix; the nearest projectileproximity
      object ahead within Physics_Projectile_Targeting_Dist / _Angle - a ped that's been shot left out, a ped aimed at
      the target_bone choose_projectile_target_bone picks; found: the target is only given when it's beyond 100) and
      the constructor 0x582D90 (vtable 0x64EF48; skater-list node; type 0xBAF9B341; tracker aliases for skaters 0 / 1;
      priority -500; the D: debug lines; a local skater's 24 components and a remote one's 8, from one structure;
      the controls told the skater number). Self-test (new src/hook/selftests_viewer13.cpp): TestSkaterLaunch.

- [x] Batch 148 (VW-14; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): ResetComponents
      0x583400, Restart 0x583470 (a walking skater switched back to skating first) and the skater's script member
      functions 0x5835D0 (KillSkater - by prefix checksum or string, node, a linked restart, else the skate module
      unless in the park editor; found: the RestartWalking flag is kept in the low byte of the params slot and the
      restart is passed that whole word -, AddSkaterToWorld / RemoveSkaterFromWorld, SkaterIsNamed, ShouldMongo
      (mongo_when_switch / always_mongo), LaunchDirtyProjectile, SparksOn / SparksOff, SetCustomRestart (Set: here,
      then skipped to; clear; asked) / SkipToCustomRestart, IsLocalSkater, ResetRigidBodyCollisionRadiusBoost,
      GetCameraId, GetStat, GetScriptedStat, MoveToRandomRestart, PlaceBeforeCamera (144 in front of the camera,
      turned to face it), die, GetSkaterNumber; the rest the composite object's). 0x580830 renamed MoveToRandomRestart
      (vtable +0x3C is SkipToRestart). Self-test (new src/hook/selftests_viewer14.cpp): TestSkaterMembers. Tests
      strengthened after the VW-6 / VW-8 / VW-9 runs (Init's fields, a level without best combos, a stat override of
      9.5, distinct initials moving down, a +0x288 state of 2).

- [x] Batch 149 (VW-15; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the rest of the
      skater: SkipToRestart 0x583DA0 (vtable +0x3C: a local skater's input / pad / online flag reset, vtable +0x40
      with the node, Restart, the camera updated unless paused, the node's TriggerScript spawned on it; teleported; in
      game modes 0x6C5FF266 / 0x6EF8FDA0 / 0x5D32129C the server sent message 0x57, sequenced, group 8), the
      destructor body 0x583FB0 (actuators unpaused and stopped for a paused local skater; script 0xF429E0AC; the trick
      infos deleted; skatercam1 marked; out of the skater list; its name) and the scalar deleting destructor 0x5840E0.
      Self-test (new src/hook/selftests_viewer15.cpp): TestSkaterRestart.

- [x] Batch 150 (VW-16; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the skater's career
      (new src/game/skate/career.*, Obj::CSkaterCareer, 0x6AC bytes: per level (21) 256 goal bits and 256 flag bits,
      their level-start copies, cash / a second amount (1000 each, total 6000) / a counter per level with totals, 256
      global flags (cheats), the current level, gap checklists, visited levels, an info structure), 0x584110-0x584CF3
      incl. the missed 0x584BC0: GetGapChecklist, GetLevel, Set / UnSet / Get / JustGot Goal and Flag (the unset ones
      also clear the level-start copy), CountGoals (the first 64), CountTotalGoals / CountLevelsWithGoals (levels left
      out at 0x7CE4E8), the global flags, Get / SetInfoInteger, the 0x471FC4DD info count, AddCash (returns a pointer
      to the level's cash), SetLevelAmount, IncrementCounter / GetCounter, +0x5EF, +0x634 (with its highest), Reset
      0x584680 (gaps; parts 0x4DA4937B - bits 0-199 of every level, cash, amounts, totals - and 0x4C10DE52 - bits
      200-255 of 18 levels, counters, the info's defaults 0x0486FD79), ResetPart 0x584BA0, AllGapsGot 0x584BC0 (the
      levels 0xEB1A4BC9 lists: visited and all their gaps), StartLevel 0x584C60, GetCheat 0x584CC0. Self-test (new
      src/hook/selftests_viewer16.cpp): TestCareer.

- [x] Batch 151 (VW-17; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the career's save half:
      ReadFromStructure 0x584D00 (the save's 0x4DA4937B structure: goals, flags, global flags, each level's gap
      checklist rebuilt - got gaps with their positions -, visited levels, cash / amounts and totals, counters - an
      older save without the counter total sums them, +0x5EF 0x8C once past it -, +0x634 / +0x638, the info; found:
      two totals and the info structure are left as the stack had them when a save lacks them - ours reads 0 / none),
      the constructor 0x585270 (vtable 0x64EFD4, 21 gap checklists, the info, a full reset), WriteToStructure
      0x585550 (the same, with the skate module's +0x378 and the game mode), GetCareerSaveInfo 0x585E20 (the skate
      module's career, +0x20), the destructors 0x585E40 / 0x585EA0, and CGapChecklist::DestroyBody 0x585520 (into
      gap_checklist.*). Self-test (new src/hook/selftests_viewer17.cpp): TestCareerSave.

- [x] Batch 152 (VW-18; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the skater profile's
      small methods (new src/game/skate/skater_profile.cpp; declarations in profile_manager.h, whose GameFunction
      wrappers for them are gone): CopyFrom 0x585ED0, ReadFromSave 0x585F00, GetStat / SetStat 0x585FB0 / 0x585FD0,
      the info counters 0x586000-0x5860A0, GetChecksumValue 0x5860C0 (stance / pushstyle only), 0x586110-0x586210
      (structure, specials, name, is_pro, a flag, an integer, a string with "Unimplemented" default, display_name),
      the special trick slots Get / Set 0x586630 / 0x586710 (Set tells the current skater's trick component,
      0x522410, if asked), SpecialTrick's constructor / IsEmpty 0x587100 / 0x587110, and a distinct-checksum list
      0x587120 / 0x587130 (ChecksumSet, filled from the global array 0x5004A967). Self-test (new
      src/hook/selftests_viewer18.cpp): TestProfileMethods; selftests_viewer7 / viewer11's profile stand-ins now also
      cover our methods (AddressOf). Values an asserted lookup leaves unset are garbage in both and not compared.
      Also fixed (found by the audit; from VW-10): GetScaledPhysics called 0x564D90 with a stray argument and
      0x559050 without its bool - it's 0x564D90(skate) then 0x559050(mode, false); the test stand-ins' pops fixed too.

- [x] Batch 153 (VW-19; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the rest of the skater
      profile, in skater_profile.cpp: SetFromParams 0x586230 (the appearance and info from params, and/or the
      specials, trick style and the global stat list's stats), Reset 0x586810 (now returns bool, as the original),
      WriteToSave 0x586B00, the constructor 0x586D60, and the network copy: ReadFromBuffer 0x586DC0 /
      ReadInfoFromBuffer 0x586A40 / WriteToBuffer 0x587050 / WriteInfoToBuffer 0x586E70 (only the info names it lists
      are sent). profile_manager.cpp's last wrappers for them are gone. Where the original leaves a value that a
      lookup didn't find in a reused argument slot, ours starts the variable with that argument (SetFromParams'
      integers, read with assert 0 for 0x27EB9B9D). Self-test (new src/hook/selftests_viewer19.cpp): TestProfileRest;
      selftests_viewer7's stand-ins for Construct / WriteToSave / Reset now also cover ours.

- [x] Batch 154 (VW-20; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the movie manager's two
      animation movies (new src/game/obj/anim_movies.*): the object animation movie (vtable 0x64EFE8: constructor
      0x5871B0, destructors 0x587210 / 0x587370, Start 0x587270, IsDone 0x587280, 0x5872A0, Play 0x5873A0, Load
      0x5875D0) and the camera animation movie (vtable 0x64F020: 0x5872C0, Start 0x587320, IsDone 0x587330, 0x587350,
      constructor 0x5873F0, 0x587450, LoadTarget 0x587480, Play 0x587540, CallMemberFunction 0x587590, Load 0x587F70,
      Update 0x588090); and the trick object pieces at the end of the range (new src/game/obj/trick_objects.*): the
      manager's Release 0x5880E0, a cluster-count callback 0x588120, a cluster's SetParams 0x588150, the trick
      component's list constructor / Clear 0x588170 / 0x588180 (0x588180 lies just past the range, but find_gaps showed
      it between rewritten ones). Self-test (new src/hook/selftests_viewer20.cpp): TestAnimMovies.

- [x] Batch 155 (VW-21; viewer agent, merged 2026-10-19; run in the game: docs/pending/viewer.md; NOT judged by a player yet; early tests pass offline; mutation run pending): the movies' updates, in
      anim_movies.cpp: the object movie's Update 0x5876A0 (and its jump 0x587F60): each object the asset names gets
      its frame's rotation (matrix and display matrix) and position, a missing one is reported; and the camera
      movie's PlaceCamera 0x587A50: the camera from the asset's frame or at rest (field of view: its own or the
      script global 0x99529205), then aimed at the target object (offsets in its frame) or the fixed point
      (0xC588EEBC), its axes rebuilt by cross products. The x87 arithmetic is done in double, rounded where the
      original stores. Self-test (new src/hook/selftests_viewer21.cpp): TestAnimMovieUpdates (the maths helpers run
      for real). find_gaps now shows no gap in the range.

- [x] Range done: progress_report shows "Viewer / gaps / records / career save info" 304 / 304 (100%), find_gaps
      none. Main merged at a4109cb (the fast mutation setup).
      Mutations, VW-18..21 (fast setup: TESTS filter, build without re-configure, ~4.5 s each): VW-18 14/14 caught;
      VW-19 23/24 caught (read_heap after the test gave the manager distinct heaps); the miss sfp_keep (the integer
      slot's starting value) only shows when asserted lookups fail - equivalent here. VW-20/21 45/48 caught after the
      tests were strengthened (each load / placing recorded before the next overwrites it; a release at count 2; a
      load without the offsets; one quaternion left as Update set it); equivalent: pc_look_start (a dead write),
      pc_right_w (row 0's w is already 0 from the identity), pc_row0_norm (row 0's y is always 0: row 1 is (0,1,0)).
      Game run (C:\Games\THUG2-3 under the lock, -Level BO, debug_mode=1): 2572/2572 replacements installed, late and
      engine-started self-tests pass, no exception / run-time check / CRT report; the level loads and plays, F9 free
      camera moves with the arrows (pictures differ), the level's intro camera ran. One early self-test failed in the
      game only: "skater init" (VW-12) - the arena manager's skater info heap word (+0x5D4) changed during our run
      (0xA0010000 vs 0x7B000000), passes offline every time: looks like the game touching the arena's stand-in manager
      while the early tests run (a race, as the known flaky ones); not chased further.
      Fixed since: the cause was batch 109's - the test's heap constant 0x7B000000 fell inside our run's arena at that
      start-up's DLL address (ASLR), so Rel made it an offset in our run only. viewer_testkit.h's Rel now maps a word in
      either run's arena (ArenaWord over the ArenaPair), as the other kits do. Three start-ups in THUG2-3: all 276 early
      tests pass each time.

- [x] Mutations finished (fast setup). VW-15..17: 54 mutations, 52 caught after the tests were strengthened (a
      destructor case not paused; a left-out level with one goal; the career recorded before the resets clear it;
      +0x634's max from 0; the career's visited bytes as constructed; some gap checklists built so their gaps are
      written); equivalent: start_visited (StartLevel's level is never -1 by then), read_old_total (the test's
      counters never make the running total land on 0x82 exactly - not reachable with the stand-in's arrays).
      The earlier batches' suspect CAUGHTs (they named a flaky test) rerun with TESTS: all 23 caught by our tests. The
      known misses: profile_style_zero, pause_flags, launch_best, restart_walking now caught (a profile without a
      skate style; the composite flag recorded after Pause; a farther target after the ped; walking with +0x118 set);
      input_mirror is equivalent (the byte set at i == 6 instead of 7 is cleared again at 7 and nothing reads it in
      between).

- [x] Batch 156 (OS-1; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; the controller pad): src/game/obj/control_pad.{h,cpp} (new).
  - The pad as the skater reads it (THPS's CSkaterPad / CSkaterButton; here Obj::CControlPad / CControlButton): 16
    buttons of 0x20 bytes (UP, DOWN, LEFT, RIGHT, L1, L2, L3, R1, R2, R3, CIRCLE, SQUARE, TRIANGLE, X, START,
    SELECT at +0x000-0x1E0), the raw sticks +0x200, the scaled sticks +0x210, each stick as angle + length
    +0x220, two timed holds +0x230.
  - Buttons: 0x0046B7E0 ClearTrigger, 0x0046B7F0 ClearRelease, 0x0046B800 SetName (the checksum), 0x0046B820
    Update(pressure) (down above 0x40; the press / release times are the game clock + its offset 0x696D60; while
    +0x1C (a Tmr::GetTime time as a float, 0 = none) is set, a press only counts once that time has passed - any
    other update clears it; compared unsigned, NaN holds), 0x0046B8B0 SetPressed(byte) (the byte itself is stored;
    a release here isn't timed), 0x0046B900 / 0x0046B920 the time since the release / press.
  - Pad: 0x0046B940 Init (the names; the holds cleared; returns this), 0x0046BB50 Reset (every button released, in
    the original's order; sticks zeroed), 0x0046BC60 Clear (Reset, then every button's events taken - the
    original's slip kept: R2's press is taken twice and its release kept, R3's release twice and its press kept),
    0x0046BDA0 Update(data) (PS2 pad layout: pressures 4-0xF as full 0xFF / 0, L3 / R3 / SELECT from bits 0x200 /
    0x400 / 0x100 of +0x1C; START isn't read; the scaled sticks: data 0-3 - 0x80 moved 50 towards 0 (crossing it:
    no dead band) / 77, -0x80 counts as -0x7F, exactly centred = 0; the raw sticks data 0x12-0x15 - 0x80; the
    angles atan2(x, -y) through the FPU's FPATAN; the second stick under 0.001 long stands in the d-pad's angle at
    length 127; the two holds end once the time has passed or the second stick is centred / leaves the up / down
    quarter), 0x0046C270 GetButton(name) (in the original's search order).
  - 0x0046BA50 DPadDirection and 0x0046BA80 DPadAngle (the d-pad's 16 combinations; the angle table is the
    original's static 0x6F2270, filled on first use - a constant table here), 0x0046C370 StickLength(x, y, angle)
    (__stdcall: x / 127 or 128 by sign, the length times |cos| of the angle brought into the quarter around up /
    down / left / right (FCOS) times 1/0.85, at most 1).
  - Test (selftests_objsys1.cpp): TestControlPad - 40 random pads (buttons pressed / not / holding a 2, locks
    none / 1500 / 1200.5 / NaN / 3e9; pad holds 0 / 1200 / 1500 / past 2^31) driven through 24 random calls each
    (updates from random data - centred sticks, the d-pad standing in, stick pointed up / down -, pressures around
    0x40, SetPressed with 0 / 1 / 2 / 0x55 / 0x100, the times, names, lookups with duplicate names, Reset, Clear),
    the clocks stood in (scripted, unsigned values past 2^31, exactly the lock time), the whole pad compared after
    every call; directed updates at exactly the holds' time with the stick up / down. TestControlPadMath: every
    d-pad combination, StickLength over each quarter / boundary / NaN / both signs, the clamp just over 1.
  - Mutations: 36 deliberate bugs; 32 caught (29 at first; the holds at exactly the time, duplicate names and the
    clamp were added for 3). The other 4 are equivalent: `value >= 0` for `> 0` in the scaling (0 returns
    earlier), `x >= 0` for `x > 0` in StickLength (0 / 127 = 0 / 128), `a >= -pi/4` for `a > -pi/4` (cos is even:
    the same length either way).

- [x] Batch 157 (OS-2; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; proximity nodes, restart node lists, preferences):
  src/game/obj/proxim.{h,cpp}, src/game/script/preferences.{h,cpp} (new).
  - Proximity nodes (THUG's Obj::Proxim_*; level nodes of class ProximNode; the list 0x006F2268 = {head, entering,
    in-script}): 0x0046AAC0 ProximShapeFromChecksum (0xE460ABDE box, 0x03F1E5A7 sector box, 0x64FBA415 none,
    0x6AADF154 sector collision, anything else sphere), 0x0046AB10 / 0x0046AB30 the manager made / emptied
    (Proxim_Init), 0x0046AB50 Proxim_Cleanup (every node freed), 0x0046AB90 Proxim_GetNode (no null check, as the
    original), 0x0046ABC0 ProximNode::SetActive (a node going inactive kills its objects (vtable slot 6), or runs its
    script as the exit for each skater it holds - entering flag clear, in-script set around it; everyone counts as
    outside after), 0x0046ACB0 Proxim_SetActive(name), 0x0046ACF0 SetSector (the sector's box; for the sector shapes
    also its world matrix, inverted), 0x0046AD50 IsInside (sphere: z, y, x squares summed unrounded <= radius^2; box;
    sector box: the point into the sector's frame about the box's centre; the sector's collision object's +0x48; the
    "none" shape and unknown ones: no), 0x0046AEC0 GetClosestPoint (the sphere's: w's product unrounded; the boxes'
    surfaces, the sector box through its frame and back by the world matrix; none: `out` left - the original copies
    uninitialised locals), 0x0046B1F0 Proxim_AddNode (name=, shape, sector, position, ProximType 0x7321A8D6
    (0x57525413 -> 2, 0xC4E311FA -> 1, else 0), radius and its square, script; ProximObject nodes keep their name as
    the objects' base id; plain nodes start holding the skaters flagged at +0xBC, end them (SetActive(false)) and
    take `active`), 0x0046B400 Proxim_Update(mask, object, point, type) (entering: plain nodes spawn their script on
    the object (or run it) with the entering flag; ProximObject nodes make a composite object (id = base + the
    skater's id, the node's position, components from proximobj_composite_structure with the sector's name
    0x08470F2E when there is one, finalised, its script switched to the node's); leaving: the exit script / the
    object killed).
  - Restart nodes: 0x0046B680 IsRestartNode (code Ghidra never made a function of), 0x0046B6C0 GetRestartNodes(type,
    out, start) (class Restart; type 0 = any, else those whose restart_types hold it), 0x0046B790 GetRestartNode(type,
    index, start) (wrapped past the count; a negative index reads before the list, as the original).
  - Prefs::Preferences (vtable 0x00647B34: GameNet::Manager's network / taunt preferences): 0x0046C470 Init,
    0x0046C490 Deinit, 0x0046C680 the scalar deleting destructor, 0x0046C4A0 GetPreference, 0x0046C4D0
    GetPreferenceArray, 0x0046C500 SetPreferences, 0x0046C510 / 0x0046C550 / 0x0046C590 GetPreferenceString / Value /
    Checksum (asserting getters: a missing field returns the preference's name - the original reads the value back
    from the argument's slot; the checksum getter asks for the preference with an assert), 0x0046C5D0 SetPreference,
    0x0046C610 SetPreferenceArray, 0x0046C650 Load (a global structure's components).
  - Tests (selftests_objsys2.cpp): TestProxim - 24 seeds of 3-6 nodes from random parameters (every shape name,
    type, radius or none, ProximObject, renamed by name=, sectors or none, skaters flagged or not), 40 random calls
    (updates of skaters / no object at points near and far, each mask, each type; nearest points; inside tests;
    activation by node and by name; lookups), then a directed pass (every node active, entered by two skaters at its
    centre, one leaving far away, the sphere's exact edge, deactivated), clean-up with and without a manager - the
    world stood in for (node parameters, objects and their vtables, sectors and collision, scripts, memory), the box
    / matrix / vector maths routed to ours in both runs, the whole arena and every call compared. TestRestartNodes:
    the shape names, the lists by type from a start node or the first, IsRestartNode, the n-th. TestPreferences:
    every method on stand-in structures whose getters find the field or not, the destructor with flags 0-3.
    objsys_testkit.h: globals saved and restored by the tests' cleanup (also after a crash).
  - Mutations: 47 deliberate bugs; 45 caught (37 at first; the directed pass, the sphere's edge, w values that round
    and the destructor flags were added for 8). The other 2 are equivalent: the manager's in-script flag cleared
    after Calloc (zeroed already), SetActive returning early only when activating (inactive nodes always have
    m_outside -1).
  - Also: src/game/core/singleton.{h,cpp} (shared, additive): 0x00469490 Spt::PointerMember::Get (3 bytes, source
    only; the folded one-pointer getter); tools/mutate.py lists every failing / crashing test of a mutation (an
    unrelated flaky test - "PC async handle" fails now and then on this busy machine - hid which test caught it).

- [x] Batch 158 (OS-3; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; script structures to and from byte buffers - the network / save
  serializer): src/game/script/struct_buffer.{h,cpp} (new).
  - 0x00479840 WriteNameAndType (a name as a one-byte index into the global name array 0x35115A20 (type | 0x80), a
    two-byte index into 0x25231F42 (type | 0x40), else the checksum), 0x004798F0 WriteInteger (the smallest form:
    zero, unsigned / signed byte or short, else four bytes; name in ECX, value in EBX - a naked stub), 0x00479A50
    WriteFloat (zero floats as their own type; buffer ECX, name EDX, size ESI - stub), 0x00479AC0 / 0x00479B20
    WriteString / WriteLocalString (buffer EAX, string ECX - stubs), 0x00479B80 WriteArrayToBuffer (type, two-byte
    count, elements; strings, pairs, vectors, nested structures and arrays), 0x00479E20 WriteToBuffer (every
    component; scripts as the placeholder name 0xB9C4F664; the callback per component; 0 when it doesn't fit),
    0x0047A070 CalculateBufferSize (a write into a scratch buffer), 0x0047A0C0 ReadFromBuffer (the structure
    cleared and rebuilt; unknown types: only the name read), 0x0047A440 ReadArrayFromBuffer (arrays made on the
    script heap).
  - Tests (selftests_objsys3.cpp): TestStructBuffer - 30 seeds of random nested structures (every component type,
    names from the two index arrays or not) written in full, measured, into short buffers of every length near the
    end, an array alone into 0-4 byte buffers, the 300-name array read back alone (a two-byte count), read back and
    written again, a crafted buffer of every short form and unknown types - on a stood-in memory manager over an
    arena, the whole arena and every call compared. TestStructBufferStubs: the register entry points against the
    originals into buffers big enough, exactly enough and too small.
  - Mutations: 41 deliberate bugs, all caught (the array-alone and 300-name cases added for 2).

- [x] Batch 159 (OS-4; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; sound emitter nodes, the follow trail): src/game/obj/emitter.{h,cpp} (new).
  - Emitter nodes (a Lst::HashTable at 0x006F2260 of EmitterNode, 0x60 bytes): 0x004680E0 Emitter_GetNode, 0x00468120
    IsInside (box: the sector's bounding box; sphere: the squared distance summed unrounded z, x, y against the
    radius squared; other shapes never), 0x004681A0 GetClosestPoint (the point itself inside; the box's closest
    surface point; the sphere's along the direction), 0x004682B0 Emitter_Cleanup (every node freed and flushed; the
    table stays), 0x00468340 Emitter_SetActive (the first node with that name), 0x004683B0 Emitter_Init (the table
    made with 4 bits, or emptied), 0x00468450 Emitter_AddNode (from the node's parameters: name= renames it, the
    shape names 0x03F1E5A7 other / 0xAA069978 sphere / 0xE460ABDE box, the sector of that name - its box for box
    emitters -, position, radius, script).
  - CFollowTrail (64 points spaced over 40 units behind an object): 0x00468AC0 Init, 0x004685D0 Reset, 0x00468610
    Update (the object's position added when it's moved more than 40 from the newest point), 0x00468730
    GetPosition (the point m_distance back along the trail, and the matrix turned to face along it: level or free).
  - Tests (selftests_objsys4.cpp): TestEmitters - 24 seeds of 2-9 nodes from random parameters (each shape name and
    none, renamed - some to the same new name -, sectors or not, radius or not), 30 lookups / activations (by old,
    new and unknown names) / inside tests and nearest points (points inside, outside and on a sphere's exact edge),
    the clean-up or Init on the full table - on a stood-in memory manager, the arena and every call compared.
    TestFollowTrail: 30 seeds of 10-259 updates (missing object, steps under, exactly at and over 40, the ring
    wrapping), positions and facings back along the trail at distances within, across and past it, both facing
    modes; the reset on dirty memory.
  - Mutations: 33 deliberate bugs, all caught (25 at first; the exact sphere edge, shared new names, the reset on
    dirty memory, longer trails and the exact spacing were added for 8).

- [x] Batch 160 (OS-5; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; objects made for level nodes, movie details, the class node list):
  src/game/obj/node_objects.{h,cpp} (new).
  - 0x00468B00 CreateParticleObject (moving_particle_composite_structure when the node has HasMotion, else
    particle_composite_structure), 0x00468B60 StartNodeObject (finalised, switched to the node's script 0x2CA8A299
    with its params 0x7031F10C, the creation script 0x413B766D run with the node's name and its integer 0x99A9B716
    if it has one), 0x00468CB0 CreateGameObject (the components of 0x735A20BA, two more around the script
    0xF3B84DA6, finalised, the creation script, component 0x286A8D26's set-up, the node script), 0x00468EB0
    CreateGameObjectFromNode, 0x00468EC0 CreateNodeObject (flag 0x69C257E9: a plain composite object from the
    structure with script 0xA98F5C63; else a game object).
  - CMovieDetails (a list node + settings + a camera): 0x00468FB0 Construct, 0x00469230 DestroyBody (the camera
    deleted unless a viewport uses it), 0x004692D0 / 0x00468F70 the deleting destructors, 0x00469050
    InitFromStructure, 0x00469180 GetSettings, 0x00468F20 CheckDone, 0x00468F40 / 0x00468F50 / 0x00468F60 setters.
  - CClassNodeList: 0x004693C0 Init, 0x00469300 Clear (0x004693D0, a jump to it: source only), 0x00469340
    SetTransform, 0x00469350 Transform, 0x004693E0 AddEntry, 0x00469430 SetNodes (an entry per node of class
    0xD18719D5).
  - Tests (selftests_objsys5.cpp): TestNodeObjects - 40 seeds of nodes with random parameters made into particle,
    game and plain objects (the object manager, components, scripts and structures stood in for; every call
    compared). TestMovieDetails: construct (the node as built), random settings / setters / CheckDone, the three
    destructors with flags 0-3 on a linked node; the class node list from random node arrays, transformed or not,
    cleared through both entry points - the arena compared.
  - Mutations: 35 deliberate bugs, all caught (the constructed node and destructor flags 2-3 added for 2).
  - Also: selftests_objsys3.cpp's arenas at fixed high addresses (a misaligned name in the buffers equal to a low
    arena's address was relocated in one run only) and its names changed to match.

- [x] Batch 161 (OS-6; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; the path manager and its object groups): src/game/obj/path_manager.{h,cpp}
  (new).
  - CObjectGroup (smart pointers to objects, new[]'d): 0x004694A0 Destruct (code Ghidra never made a function of:
    every slot released, the array freed), 0x004698C0 Init(size), 0x00469540 ReleaseAll, 0x00469580 Remove,
    0x004695D0 Add (the first free slot; nothing when full).
  - CPathManager (singleton 0x006F2264): 0x004694E0 Instance, 0x00469620 AddToPath (the node's PathNum group),
    0x00469670 AddToExtraGroup, 0x004696C0 NumberPaths (every node walked: classes 0x8E6B02AD / 0x30C19600 and
    0x4C23A77E with type 0xCBA10FFA skipped, nodes without links unless 0x4C23A77E, nodes numbered already; down the
    first links numbering, a numbered node giving its number to the whole chain from the start), 0x00469820 Free,
    0x00469960 Allocate (on the bottom-up heap), 0x00469A80 CFunc_AllocatePathManMemory (defaults 200 groups of 20,
    an extra group of 200).
  - Tests (selftests_objsys6.cpp): TestPathManager - 30 seeds of random node graphs (every skipped class and type,
    numbered nodes, chains running into numbered ones, loops), the manager made (as built) and allocated, 30 random
    adds / removes / releases on groups and the extra group, a group of its own destroyed while holding objects,
    numbering, freeing (twice), the script command with no sizes, some or all given - on a stood-in memory manager,
    the arena after the group operations, after numbering and at the end compared, and every call.
  - Mutations: 30 deliberate bugs; 29 caught (22 at first; the arena images, the manager as made, the defaults and
    the destroyed group added for 7). The other is equivalent: a slot cleared again after Release (which clears it).

- [x] Batch 162 (OS-7; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; following a path of linked nodes): src/game/obj/path_follower.{h,cpp}
  (new).
  - CPathFollower (vtable 0x00647A84; used by the path component): 0x0046A1D0 Construct, 0x00469B50 the deleting
    destructor, 0x00469B00 ReadNodePosition, 0x0046A120 NextNode (a random link when the owner's component
    0x0A015E17 says so, else the first), 0x00469F60 UpdatePosition (along the segment), 0x0046A000 CheckCorner (the
    cut short of the node: half the next segment when that's shorter than the corner distance), 0x0046A250
    StartSegment, 0x00469B80 ComputeArc (the arc through the corner: its centre on the bisector, radius, angle and
    frame; 0 = blend instead, 1 = arc, 2 = degenerate), 0x0046A320 ArcStep (the frame turned about Y via
    D3DXMatrixMultiply), 0x0046A550 StartCorner, 0x0046A3F0 Start (nodes within 1 unit skipped), 0x0046A640 Update
    (a distance, optionally weighed by a direction: straight / arc / blend / done; the component told of each node
    passed - at the arc's or blend's half), 0x0046A1B0 GetHeading. The original's x87 comparisons kept (unordered
    counts as nearer where it does).
  - Tests (selftests_objsys7.cpp): TestPathFollower - 60 seeds of random node graphs (close nodes, dead ends, random
    links), corner distances none / small / large, a start on, near or away from the path, 80 updates of random
    distances (some weighed by directions, perpendicular ones too), headings; the follower as constructed and after
    every step compared (a NaN's sign not compared: once a degenerate corner has made the state NaN, the x87's
    choice between two NaN operands differs from SSE's) - the vector maths routed to ours in both runs, acos the
    same for both.
  - Mutations: 36 deliberate bugs; 32 caught (the constructed state added for 1). The other 4 are equivalent: the
    carry added in StartSegment (always 0 there), the zero-distance arc (unreachable: FLAG_CORNER returns first),
    the along value set before StartSegment overwrites it, and the done state after a failed NextNode (a node
    without links is always the last node).

- [x] Batch 163 (OS-8; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; the sound effects manager, part 1: tables, volumes, parameters):
  src/game/sfx/sfx.{h,cpp} (new).
  - sVolume (2 channels, 5 for the surround type; 3D ones carry a position and range): 0x0047B030 Init (the
    manager's default type, silent; returns this), 0x0047B070 SetType, 0x0047AC50 PercentageAdjustment,
    0x0047ACF0 IsSilent (never for 3D), 0x0047AD60 SetSilent, 0x0047B080 GetLoudestChannel (signed; first of equals).
  - Free functions: 0x0047AC10 FalloffFromChecksum, 0x0047AFD0 GetMainVolume, 0x0047AFE0 CountPlayingVoices,
    0x0047B010 NoSound (no DirectSound device 0x007D6A38, or platform 2 / 6).
  - CSfxManager: 0x0047B160 Construct, 0x0047AD80 GetPlayingIndex (unique id first, then by name - the name path
    reads two debug script globals), 0x0047AE20 AddPositionalSound (the count raised even when no entry is free,
    as the original), 0x0047AE90 StopGroup / 0x0047AEA0 UnloadGroup / 0x0047AF10 UnloadAllGroups (no groups in this
    build), 0x0047AF40 InitPlaybackProps, 0x0047B1B0 CleanUp (default dropoff back to 1020), 0x0047B210
    SetDefaultDropoff, 0x0047B240 StopAllSounds, 0x0047B2A0 UpdateVoiceVolumes, 0x0047B2C0 GetWaveTableEntry (level
    table first), 0x0047B340 IsPaused, 0x0047B360 LoadSound (the name after the last \ or /; 256 per table),
    0x0047B480 AdjustVolumeAndPitch (the game's 50 % first), 0x0047B4E0 StopSound, 0x0047B520 SetSoundParams,
    0x0047B5D0 SetMainVolume (below 0.1 silent; the user soundtrack too), 0x0047B650 SetPitch (at most 0.01; NaN
    kept), 0x0047B6B0 PrintSounds (compiled out), 0x0047B6F0 IsSoundPlaying, 0x0047B760 GetDropoffDistance,
    0x0047B7C0 SetSoundParamsScaled. The driver's voice calls go through the game's until OS-9.
  - Tests (selftests_objsys8.cpp): TestSfxManager - 60 seeds of random sound state (wave tables with duplicate
    names and every count up to full, playing sounds, the positional pool and list, settings, pause flags, the
    device and platform), 30 random calls each on random ids (mostly ones in the tables), every result and driver
    call recorded and the whole state compared.
  - Mutations: 37 deliberate bugs; 35 caught (33 at first; ids drawn from the playing table added for 2). The other
    2 are equivalent: UnloadGroup's count stored only when positive (unchanged otherwise), and SetPitch's clamp as
    `pitch < 0.01` (differs only at exactly 0.01, which it sets anyway).
- [x] Batch 164 (OS-9; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; the sound effects manager, part 2: positional sounds and the PC
  driver's voices): src/game/sfx/sfx_voices.cpp (new); sfx.cpp now calls the voices directly.
  - CSfxManager: 0x0047B8E0 DopplerShift (the pitch moved by how fast the sound nears the camera; nothing past 360
    units a 60th - equal shifts), 0x0047BA00 GetPositionalProps, 0x0047BAB0 IsUniqueIdFree, 0x0047BB20
    GetUniqueId, 0x0047BB50 StopObjectSounds / 0x0047BC30 StopObjectSound, 0x0047BD20 PlaySound, 0x0047BE10
    GetVolume (falloff smooth / linear / squared / root; 5 % past the dropoff still "in range"; 3D; the
    stereo-positional branch is dead code - the type was just set to 3D), 0x0047C140 PlayObjectSound (a silent
    looping sound handed to the object to start later), 0x0047C360 UpdatePositionalSound (480 units past the
    dropoff: stopped, checked every 2 s; NaN dropoffs go on), 0x0047C560 UpdatePositionalSounds; 0x0047BA60
    CreateManager / 0x0047C6C0 ReleaseManager (reference counted).
  - The driver: 0x0047C720 GetFreeVoice, 0x0047C790 GroupCount (source only, 4 bytes), 0x0047C7A0 StopVoice,
    0x0047C7C0 / 0x0047C7D0 the voice volume and pitch, 0x0047C7E0 IsVoicePlaying, 0x0047C810 UpdateVoiceStates
    (0x0047B000, a jump to it: source only), 0x0047C870 PCWave::Load (the .wav chunks: fmt, data, smpl loops;
    the IMA ADPCM conversion 0x005F5A20 stays the game's; the samples buffer is the format's bytes-per-second long,
    as the original), 0x0047CA50 InitVoices (the voice count from the free hardware mixing buffers, 3D and the
    listener), 0x0047CBD0 StopAllVoices, 0x0047CC10 FreePermanentWaves, 0x0047CC70 LoadWave (data\sounds\*.snd),
    0x0047CD70 SetVoiceParameters (3D: the position relative to the camera, turned to the viewport's when another
    camera hears it; stereo: volume and pan in hundredths of a decibel; the frequency clamped 100-200000), 0x0047D130
    PlayVoice (a buffer made when the voice held another wave; locked, filled, played), 0x0047D390 SilenceVoices.
  - Found while testing: the CRT's _ftol is a 64-bit truncation (a frequency over 2^31 wraps instead of clamping to
    100; an infinite pan is 0) - kept; DopplerShift shifted at exactly the limit (the predecessor's `<=` was wrong);
    UpdatePositionalSound goes on for a NaN dropoff.
  - Tests (selftests_objsys9.cpp): TestSfxPositional - 80 seeds of random positional lists (duplicates too),
    playing sounds, wave tables, cameras and objects, 25 random calls each (every list entry's props first), with
    the cameras, the objects' sound parts, the clock, the voices, script globals and memory stood in for.
    TestSfxVoices - 100 seeds over recording DirectSound objects (device, buffers, 3D buffers, listener: creation,
    QueryInterface, Lock, Play, GetStatus and GetCaps answers scripted, failures too), every driver function at
    random. TestSfxWaveLoad - 60 seeds of random .wav files (chunk orders, sizes, loops, files cut short, a missing
    one) in a scratch folder, through LoadWave and PCWave::Load (the original's C runtime routed to ours). (States
    the original can't take - a voice with a wave but no buffer, a negative voice count, a failed primary buffer -
    left out.) The OS-8 test now stands in for our voice functions as well.
  - Mutations: 113 deliberate bugs; 107 caught (100 at first; two mutations rewritten to build; duplicates in the
    list, every entry's props, distances and pitches at the limits added). The other 6 are equivalent: the Doppler
    limit's `<=` (differs only at exactly the limit), the sign of a vector whose length is taken, a failed manager
    allocation (the instance was null already), GetVolume's `<=` on the loudest channel (always 0 there: nothing
    changes), the smpl read limit 0x20 (the loop count still read, the rest skipped alike), LoadWave freeing samples
    on failure (there are none yet).
- [x] Batch 165 (OS-10; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; blending animation poses): src/game/gfx/anim_blend.{h,cpp} (new).
  - Gfx::CPose (0x840 bytes: 64 rotations, 64 translations, 64 valid flags). 0x0047D470 InterpolateRotation (a
    normalised lerp of quaternions, the shorter way; as the original, the `to` rotation is overwritten with the
    result - the higher pose's rotations change while blending; x87: two components and the length unrounded),
    0x0047D680 BlendPoses (weights[0] = 1 - the others; from the top down each pose with a weight goes into the
    next lower one with a weight, by 1 - lower / (lower + this + the weight taken so far); bones only the higher
    pose has are copied), 0x0047D8F0 BlendPosesById (the same over the poses with an id; weights under 0.00001
    left out; copied bones marked valid; as the original, the search for a lower pose doesn't stop at the first
    unless its id matches). Both copy `bones` poses' worth (bones * 0x840 bytes) to `out` when it isn't the poses
    - the original's size, kept (its callers pass out == poses).
  - 0x0047D580 AddTimelineInfo (code Ghidra missed, nothing calls it; stdcall: a timeline's anim, speed, loop type
    name from 0x006486BC and start / current / end times from 0x004813B0 into a structure).
  - The blender channel's pieces (Gfx::CBlendChannel, the rest not rewritten): 0x0047D420 SetFlag4C, 0x0047D430
    BlendTo(target, period), 0x0047D620 GetValue / 0x0047D640 HasValue (0x0048CF50 at the channel's key + an
    offset), 0x0047D660 ClearFlag (code Ghidra missed: flag 0x5DB4115F out of the owner's parameters).
  - Tests (selftests_objsys10.cpp): TestAnimBlend - 300 seeds: the rotation blend alone (opposite, zero and
    right-angle rotations, t outside 0-1), both pose blends over 1-7 random poses with weights 0 / small / negative
    / NaN, ids, 0-64 bones, out the poses or another buffer; the weights, every pose and the output compared.
    TestBlendChannel - 60 seeds of random channels: every piece and the timeline info (the structure, the lookup
    and the timeline's times stood in for).
  - Mutations: 36 deliberate bugs; 34 caught (33 at first; right-angle rotations added for 1). The other 2: BlendTo's
    `!(period != 0)` (the same test, equivalent) and the dot product's summing order (changes only a sign that
    rounding decides - not reached by the test data).
- [x] Batch 166 (OS-11; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; the cutscene's objects, assets, lights, camera and poses):
  src/game/obj/cutscene_data.cpp (new); cutscene.h (CCutsceneData's layout filled in: the object entries +0x60,
  their count +0x2054, the animation entries +0x2058 / +0x2C58, the asset names +0x2C5C / +0x345C, +0x34B0...).
  - 0x00463AA0 QuatToMatrix (cdecl; the quaternion's x, y, z negated in place, as the original; x87: the squares
    and w*z unrounded, w*x and w*y rounded), 0x00464340 IdentityQuat::Set (code Ghidra missed; nothing calls it:
    source only), 0x00463BA0 SkaterModelName (the profile's model from array 0xCA1DB456, else "Male1" when
    0x3F813177 is 1, "Female1" otherwise), 0x00463DA0 ReadColour (percent; white when the structure is missing
    or 0x5A96985D is set; returns out), 0x00463E80 / 0x00463ED0 Add / RemoveCutsceneLights (the nodes of array
    0x2A79C44E: 0x00490570 makes a light from a node, 0x0048FCA0 removes it by node name), 0x004640D0
    SetUpCutsceneHeaps.
  - CCutsceneData: 0x00463C70 DestroyObjects (the objects that stood in for level objects give those back their
    model settings and the cutscene's colour; every object deleted), 0x00463F20 LoadAsset / 0x00463F70
    LoadAnimation (the asset manager 0x00408060; listed), 0x00464000 CreateObject (a composite object of type
    0xCDEB9229 from scripts 0x1F29C5A5 and 0x2E20EA32), 0x004641A0 ShowHiddenObjects, 0x00464220 UpdateCamera (the
    camera animation's rotation and position at the cutscene's time; the object animations updated unless
    skipped), 0x00464360 UpdateObjectPoses (each object's skeleton posed by its animation; within the first
    twelfth of a second without the bone list).
  - Found while testing: SkaterModelName's integer picks "Male1" when 1 (first written the other way round).
    Values read with the assert flag start at 0 in ours (the original's are uninitialised).
  - Tests (selftests_objsys11.cpp): TestCutsceneObjects - 120 seeds of random cutscene data (object entries,
    animations, times, flags) and a world of stood-in structures / arrays (answers from a hash of what's asked),
    objects, components, models, skeletons, the asset and memory managers, scripts and the camera animation; 10
    random calls each; every call, result, the data, the objects, the camera and the pose buffers compared. (The
    original's unchecked reads - a missing profiles array, objects or components in DestroyObjects - left out.)
  - Mutations: 40 deliberate bugs, all caught (37 at first; more integer values, the made object's memory and
    the twelfth-of-a-second boundary added for 3).
- [x] Game run (C:\Games\THUG2-4, this branch at OS-11 merged with main): BO free skate for 70 s - 2763 of 2763
  replacements installed, the level plays (skater, HUD, peds), late and engine-started self-tests all passed, no
  EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT. Two early self-tests failed in the game only (not offline):
  "skate net registration" / "skate net handlers" (selftests_tod4.cpp, the time-of-day agent's, from main: a handler
  address the original and ours record differently in the game) - not this range's.
- [x] Batch 167 (OS-12; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; the cutscene data's constructor and the objects a cutscene adds):
  src/game/obj/cutscene_load.cpp (new); cutscene.h (the object entries really start at +0x54: name, skeleton,
  part, active, animation, object, parent, animation index; the animation entries' +4 is an animation source;
  +0x10-0x28 the camera / object animation parts, sources and lists; +0x50 colour saved; +0x34A8 / +0x34AC the
  skater's appearance and profile; +0x34CC the parameters).
  - 0x004645A0 / 0x004645E0 AnimSourceAllocate / AnimSourceFree (the animation sources' pool pair 0x006FA7F4),
    0x00464640 CCutsceneData::Construct, 0x00464780 RefreshObjects (once the global 0x4AF50A5B is set: script
    0x2E20EA32 again on every object with a model and on the skater; the global cleared), 0x004648D0 AddObjects
    (3115 bytes: each structure of array 0x4DE5330C - version 2 and later name everything by the object's name
    and kind - becomes a cutscene object: its model asset (0x00407C00) or "no model"; the skater's model file from
    its face and sex; not made when the skater's appearance hides a part; a stand-in hides the level object it
    replaces (its model's settings and colour saved once, its offset, flags and a model value swapped); its
    animation source; then skeletons copied from the level objects, model flags merged with the parents', children
    linked to their parents, coloured from the skater's appearance (hue / saturation / value), and given their
    index in the object animations).
  - Tests (selftests_objsys11.cpp): TestCutsceneLoading - 150 seeds: the constructor, RefreshObjects or AddObjects
    on random data (objects in name families, animations, saved colour) with structures answering kinds, models,
    flags and integers from a hash (including the loader's special kinds and models), a manager arena, the asset
    manager, the skater's appearance and profile, models as memory; then the source pools' new / delete; every
    call, the data, the objects and the arena compared. TestCutsceneObjects (OS-11) now records the offset vectors
    passed to models and the components' argument counts properly.
  - Mutations: 51 deliberate bugs, all caught (38 at first; the loader's kinds / models in the answers, names in
    one domain so parents, animations and object animations match, models as memory, rarer hidden parts and the
    offsets' contents added for 13).
- [x] Batch 168 (OS-13; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; the cutscene's objects table, clock, stand-ins and transforms, the
  details' constructor and destructor): src/game/obj/cutscene_update.cpp (new); cutscene.h (+0x10-0x1C the
  camera / object animation part counts and sources, +0x3464 / +0x3468 the current parts, +0x348C the loop point
  (a float), +0x3490 the paused vblanks; the entries' part; CCutsceneDetails' +0x30-0x60).
  - 0x00465510 ObjectsFromTable (stdcall: a binary table of {name, model, skeleton, anim, flags} as the
    parameters' version and objects array; flag bits 30 / 31 the skater flags), 0x004656E0 UpdateTime (the clock:
    the frame counter when 0x0C00425A is set; a fixed step - looping at +0x348C with scripts 0x4C0FD1D7 and
    0xDEFF69FF - when +0x3474; else the vblanks since the start less the paused ones, as an unsigned 64-bit number
    the way the x87 does it; then past the camera animation's end the next "_CAM" / "_OBA" parts and each object's
    next animation part), 0x00465AB0 UpdateStandIn (a stand-in fades the level object out and moves it along; else
    the model fades in), 0x00465C00 UpdateObjectTransforms (the object animation applied to the level's animated
    objects and the cutscene's; children hang from their parents' bone 0x5A0E0860), 0x00466450 RestartObjects,
    0x00466490 / 0x00466560 CCutsceneDetails' constructor and destructor body (on the movie details').
  - Found while testing: D3DXMatrixMultiply (0x005BFA8C) is stdcall (first called as cdecl: the stack would have
    gone wrong in the game); +0x348C is a float; +0x5C keeps the third setting's low byte as it is.
  - Tests (selftests_objsys11.cpp): TestCutsceneUpdate - 200 seeds, one of the seven functions on random data
    (tables with every flag bit, clocks landing on the loop point exactly, NaN animation lengths, vblank counts
    past 2^63, parts, sources, parents, animation indices, owned and unowned details) with the world of OS-11/12
    plus arrays, vblanks, names, faders, bones and matrix products stood in for; every call, the data, the objects
    and the arena compared.
  - Mutations: 56 deliberate bugs, all caught (42 at first, 11 of the rest not built for a broken PDB; full flag
    bits, a loop point hit exactly, NaN lengths and the details' memory randomised added for the others).
  - The verify build now uses embedded debug information (/Z7: CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded in
    this agent's own configure) - the shared compile PDB kept breaking under the mutation runs.
- [x] Batch 169 (OS-14; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; starting and ending a cutscene, taking in its bundle, the data's
  destructor): src/game/obj/cutscene_package.cpp (new); cutscene.h (+0x08 the bundle - File::AssetBundle; the
  details' begin / end scripts +0x34 / +0x3C and the music volumes +0x44-0x4C).
  - 0x00466620 CCutsceneDetails::InitFromStructure (vtable slot 1: the cutscene 0xA1DC81F9 - an asset found by name,
    or, when the details own their data, its file: the loading screen, the music volumes kept and muted, the bundle
    loaded with flags 0xCDF2B13E / 0x9A4BDFF5 from "no music" and "force", start time and loop length in ms,
    <name>_params / _begin / _end / _subtitles; whether it's there), 0x00466A10 End (vtable slot 8), 0x00466C20
    CCutsceneData::DestroyBody (as the original, the animations' data pointers are cleared, not their sources'),
    0x00466DD0 LoadFromBundle (2.3 KB: every bundle entry taken in by type - parameters, objects table, .qb,
    "_CAM" / "_OBA" animation parts, animations, images, models with their textures and extras; a skinned
    model's colours from the skater's appearance, loaded only when the skin variant - name + 1..4 - matches the
    skater's face and sex; then the end time).
  - Tests (selftests_objsys11.cpp): TestCutscenePackage - 600 seeds: one of the four on random details and data
    over a stand-in bundle of up to 12 random entries (a skinned model group with 0-4 variants half the time,
    sizes at the 0x96 limit), with the asset manager, the game's sprintf (routed to ours), structures, volumes
    (x87 results), scripts and the loaders stood in for; every call, the data, the details and the skin colour
    globals compared. (The original's unchecked reads - asserted bundle entries, the loaded data - always there.)
  - Mutations: 64 deliberate bugs; 63 caught (56 at first; a skinned-model group, the two end-time animations apart,
    sizes at the limit, the skin globals reset per run and more seeds added for 7). The other one is equivalent:
    0x006F1158 cleared again (DestroyObjects clears it too).
- [x] Batch 170 (OS-15; objsys agent, merged 2026-10-19; run in the game: docs/pending/objsys.md; NOT judged by a player yet; NOT run in the game yet - see the game run below; loading a cutscene file and playing it):
  src/game/obj/cutscene_play.cpp (new); cutscene.h (+0x0C loaded, +0x3460 ready, +0x34B0 kept as the byte it's
  given; the details' +0x14 camera, +0x18-0x1B flags, +0x38 begun).
  - CCutsceneData: 0x00467710 MarkLoadedModels, 0x004677F0 FinishLoading (on the cutscene's or the bottom-up heap;
    a skater cutscene then waits - Pcm::Update / WaitForVblank - for its music stream), 0x004678A0 Load (the
    settings {appearance, profile, skater cutscene}; the music "<name>_<skater model>" or "<name>" preloaded, none:
    ready; the screenshot name; the cutscene heaps; the bundle made on its heaps and loaded), 0x00467B50 IsLoaded,
    0x00467BA0 Update (the first update starts the cutscene - script 0xE794A164, the camera's field of view from
    0x99529205 -; the music synced to the vblanks since; the clock, skipping, camera, subtitles, poses,
    transforms; the time shown / a screenshot a frame when asked), 0x00467EA0 Play, 0x00468080 the deleting
    destructor.
  - CCutsceneDetails: 0x00467F30 the deleting destructor, 0x00467F60 Update, 0x00468040 IsDone, 0x004680B0
    IsReady.
  - Tests (selftests_objsys11.cpp): TestCutscenePlay - 1000 seeds, one of the eleven on random data and details
    (loaded / ready / skater flags, raw flag bytes, frame counters, times, countdowns) with the cutscene's parts
    from OS-11..14 recorded (stood in for at both addresses), the bundle, music, clock, camera and screenshots
    stood in for; every call, the data, the details and the globals compared.
  - Mutations: 59 deliberate bugs, all caught (56 at first; the "cutscene loading" global reset per run and more
    seeds added for 3).
  - With this batch every function of 0x45EFD0-0x47DC40 has source: find_gaps.py lists nothing in the range.
- [x] Batch 171 (PM-1; parkmax agent, merged 2026-10-20; run in the game: docs/pending/parkmax.md; NOT judged by a player yet; the park editor's player-count script commands - the stock
  no-CD exe's damaged parameter names): src/game/ed/park_cfuncs.cpp (new); park.h (CParkGenerator::GetMaxPlayers
  reads the whole int at +0x20 - it had no callers and read a byte -, GetMaxPlayersPossible 0x00591D30 declared;
  the two CFunc declarations); selftest_runner.cpp (kReferenceBytes: the names' run).
  - 0x0058E120 CFunc_GetParkEditorMaxPlayers (`bool __cdecl (CStruct* params, CScript* script)`, params unused):
    the park manager (0x00595200 CParkManager::sInstance) -> its generator (+0) -> the int at +0x20, added to the
    script's parameters (script+0x14) by CStruct::AddInteger(const char*, int) 0x00477C30 as "MaxPlayers"
    (0xB7E39B53). Returns true.
  - 0x0058E140 CFunc_GetParkEditorMaxPlayersPossible: the same with the generator's 0x00591D30 (thiscall, no
    arguments, returns int: the players the park has room for, from the park memory's use 0x0059E730 and
    MaxPlayers; stays GameFunction) as "MaxPlayersPossible" (0x1FD6359C). Returns true.
  - **Why (not a no-op, not optional)**: the user's main install runs a stock, unwrapped no-CD THUG2.exe
    (C:\Games\THUG2\Game\THUG2.exe) unmodified. It differs from the reference THUG2PM.exe in 172 bytes
    (tools/game/exe_audit.py); one run, 0x0064F29C-0x0064F2B9, is these two commands' parameter names: THUG2PM.exe
    has "MaxPlayers\0\0MaxPlayersPossible", the stock exe 30 bytes of 0x90 filler (damage from the no-CD build's
    maker). The originals pass pointers to those strings, so on the stock exe both commands added their value
    under the checksum of the filler and the scripts' <MaxPlayers> / <MaxPlayersPossible> were never set (net.qb's
    created-park player counts, the level select's and memory card's "too many players for this park" checks).
    Ours write the names as string literals (the original call takes a name string; AddInteger(const char*)
    checksums it), so both commands behave as the original on an undamaged exe whichever exe runs. Said in a
    comment at the top of park_cfuncs.cpp.
  - kReferenceBytes (src/hook/selftest_runner.cpp): the 0x0064F29C run added (stock 0x90 x30 -> THUG2PM.exe's
    names), so on the stock exe the early tests compare ours with the undamaged original.
  - Tests (selftests_parkmax.cpp): TestParkMaxPlayers - a stand-in park manager (0x007CE604), two generators, two
    scripts and four parameter structures; six player counts (1, 8, 0, -3, 0x01020304, 2) through both commands,
    original vs ours, with AddInteger(const char*) (0x00477C30 and ours) and 0x00591D30 recording stand-ins: the
    structure, the name (text and checksum), the value, the generator asked, the return values and the stand-in
    block compared; and the original's names checked to be MaxPlayers / MaxPlayersPossible.
    TestParkMaxPlayersDamagedNames - 0x90 written over 0x0064F29C-0x0064F2B9 for the test (the stock exe's
    damage): ours must still add MaxPlayers / MaxPlayersPossible by checksum, and the original must not (the
    damage reached what it reads).
  - Mutations: 18 deliberate bugs, all caught at first (each name misspelt; each name read from the exe's address instead
    of a literal - caught only by TestParkMaxPlayersDamagedNames; the two values swapped / off by one; the
    command's params argument instead of the script's; returning false; the manager taken for the generator;
    +0x20 read as a byte / at +0x24; each AddInteger left out). Also by hand: the kReferenceBytes entry moved
    one byte (so it never applies) fails TestParkMaxPlayers on the stock exe (the original adds the filler's
    checksum).
  - Checks: thug2_check on THUG2PM.exe (C:\Games\THUG2_PM_BAK) and on the stock C:\Games\THUG2\Game\THUG2.exe:
    0 MISMATCHED argument sizes, all 302 early self-tests pass on both (the stock exe: "THUG2PM.exe's
    bytes in its 11 differing spots"). scan_register_assumptions: nothing new; audit_game_function_calls: no MISMATCH;
    check_code_writes: no overlaps.
  - Left: SetParkEditorMaxPlayers 0x0058E0F0 next to them (reads "num_players" by checksum - undamaged) has no
    source yet.
  - Switch off: `[replacements] disable=park_cfuncs.cpp`.
- [x] Batch 172 (AN-1; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the task lists and two angle helpers):
  src/game/core/task.h / task.cpp (new), src/game/mth/vector.cpp.
  - Mth: 0x00406460 GetAngle (degrees between two vectors; x87: lengths, unit vectors and the dot product in
    double, the cosine rounded to a float and limited to [-1, 1]; the result in ST0 unrounded - returned as a
    double), 0x00406630 GetAngleAbout (radians from a to b seen along an axis: both projected onto the plane
    across it, signed by (axis x a) . b). Vector::ProjectToPlane (0x00406080, rewritten earlier) made exact: the
    original keeps the dot product and x's part along the normal in double - the rounded version gave visibly
    different angles for vectors nearly along the axis.
  - Tsk::BaseTask: 0x00406890 constructor (node allocated with the priority), 0x00406930 destructor, 0x00406980
    scalar deleting, 0x004068F0 Remove, 0x00406850 InList. Tsk::List: 0x00406A50 constructor, 0x00406A20
    destructor, 0x004069B0 MarkChanged, 0x00406A80 AddTask (the task's stamp set to the list's - 1: it runs in a
    pass going on), 0x00406AC0 Process (the first argument unused; a pass restarts from the top when tasks are
    added / removed, skipping those already run by their stamp; disabled tasks and those sharing a mask bit
    skipped; nothing while locked), 0x00406BB0 RemoveAllTasks. Tsk::StackList (List + its node): 0x00406D60,
    0x00406C30. Tsk::Stack: 0x00406E80 constructor (the mask at +0x04 left unset), 0x004070E0 destructor,
    0x00406DE0 Push, 0x00406FB0 Pop (a list popped while the stack runs waits in +0x1C until the pass ends; an
    empty stack / a failed allocation crash as in the original), 0x00406C90 AddTask (the current list),
    0x00406CB0 RemoveAllTasks, 0x004070A0 Process, 0x00406F10 DeletePoppedLists. The nodes', heads' and local
    searches' destructors: 0x00406A00 / 0x00406860 (Lst::Node<BaseTask>), 0x00406D00 / 0x00406D20
    (Lst::Node<StackList>), 0x00406E50 (Lst::Head<StackList>), 0x004069C0 / 0x004069D0, 0x00406CA0 / 0x00406CD0
    (the Lst::Search vtables).
  - Tests (selftests_anim.cpp): TestAnimAngles - 3000 random triples (zero, tiny (< 1e-30), huge, parallel,
    opposite, along the axis), compared to 1e-13 (the C runtime's acos differs from the game's in the last bit);
    TestAnimTasks - a stack of 10 tasks with recording callbacks that remove the next / a later task / themselves,
    add tasks, push and pop lists while the stack runs, over 11 passes; a lone list with masks and a lock; the
    destructors in and out of lists; a second stack popped unlocked and locked and destroyed with lists waiting;
    the node / head / search destructors on their own. Allocation and frees stood in for; in our run the game's
    vtables point at ours (as with the hooks in). Every call, allocation, free and the whole arena compared.
  - Mutations: 46 deliberate bugs; 45 caught (39 at first: the stack's state right after construction, a lone
    removal outside a pass, the next task removed during a pass, a node destructor's vtable, freeing with flag 1
    and vectors shorter than 1e-30 added for 6). The other one is equivalent: RemoveAllTasks marking the list
    changed (each Remove marks it already).
- [x] Batch 173 (AN-2; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the asset base class and the asset manager):
  src/game/ass/asset.h / asset.cpp, assman.h / assman.cpp (new).
  - Ass::CAsset (0x4C bytes, vtable 0x00645568; the slots are listed in asset.h): 0x00407910 constructor,
    0x00407810 destructor, 0x004093E0 (a jump to it, the types' body; source only), 0x00407950 / 0x00407AF0 the
    scalar deleting ones, 0x004075F0 the base Load / LoadFromBuffer (loading; 0), 0x00407600 Unload, 0x00407610
    IsLoaded, 0x00407620 ProcessBuffer, 0x00407630 FinishLoading, 0x00407640 Abort (the read cancelled and closed),
    0x004076D0 GetName, the setters 0x004076E0 / 0x004072F0 / 0x004076F0 / 0x00407700 / 0x004075D0, the state
    setters 0x00407670-0x004076C0 (0 unloaded, 1 loading, 2 read, 3 processing, 4 loaded, 5 aborted),
    0x00407840 AddDependent, 0x00407890 RemoveDependent, 0x00407710 the async read's callback (not a function in
    Ghidra: found from its references), the dependents list's destructors 0x004077B0 / 0x004077D0 / 0x004078E0.
    The types' constructors: 0x004071A0 animation, 0x00409220 scene, 0x004095F0 scene collision, 0x00409970
    skeleton, 0x00409C40 skinned model, 0x0040A210 skin collision, 0x0040A580 texture dictionary, 0x00407A30
    cutscene, 0x00408BB0 node array, 0x00408BF0 (a node array made the node manager's owner). The type numbers
    from the extension table 0x0067C650 (BIN 1, SCN/GEOM 2, TEX 3, COL 4, SKA/FAM 5, SKE 6, MDL/SKIN 7, 8 skin
    collision, CUT 9, QB 10, 11 an owned node array).
  - Ass::CAssMan (0x3C bytes, a module; vtable 0x00645658): 0x00407F80 constructor, 0x00408930 / 0x00408A10
    destructors, 0x00407E90 / 0x00407AE0 start / stop (its task into the main loop's logic tasks), 0x00408060
    LoadAssetFromStream, 0x00408440 LoadAsset, 0x004087B0 LoadOrGetAsset, 0x00408870 LoadAnim, 0x00407EC0
    ReloadAsset, 0x00407B20 UnloadAsset, 0x00407CF0 / 0x00407D40 / 0x00407F20 / 0x00407F50 the unload-alls,
    0x00407B90 GetAsset (waits; IsLoaded asked twice first), 0x00407C00 / 0x00407C70 GetAssetData (by name),
    0x00407CA0 AssetExists, 0x00407A50 / 0x00407A60 (source only, 4 bytes) / 0x00407A70, 0x00407DD0
    ProcessPendingAssets and 0x00407EB0 its task callback (both not functions in Ghidra), 0x00407A80
    RemoveFromList, 0x004079C0 GetTypeFromExtension, 0x004088E0 CreateAssetManager, 0x00407980
    ReleaseAssetManager.
  - Findings: the script integer 0x25DC7904 set keeps a failed asset (not deleted; a debug leftover, LoadOrGet
    looks it up for nothing). LoadAssetFromStream has no cutscene case and a type that isn't one crashes (a null
    asset). The asset lists are circular, but ProcessPendingAssets' walk ends only at a null link and
    RemoveFromList's search only at null too: with an asset pending, or one in the other list, neither would end -
    async loading looks unused (every caller in the exe loads synchronously as far as seen). LoadAnim loads with
    the manager's +0x14 "permanent" flag.
  - Tests (selftests_anim2.cpp): TestAnimAssetBase - every constructor on dirty memory, the base methods through
    the vtable, abort with and without a file, the read callback, dependents added / removed (first, middle, last,
    missing, empty), the destructors; TestAnimAssetManager - made / started / stopped, loads of every type from files
    and memory (by extension and type, groups, async, failing with and without the debug integer, the one-shot and
    kept callbacks), lookups (waiting, a key-0 item behind a flushed bucket head), reload, LoadOrGet, LoadAnim,
    the pending list processed (a null-ended chain: loaded / read / processing that fails / loading / waiting on its
    group), the unload-alls, RemoveFromList, the extension table, construction on dirty memory, destroyed through
    the references. The types' Load / LoadFromBuffer / Unload / Reload / FinishLoading, the cutscene's IsLoaded,
    the memory manager, pool, async file calls, _stricmp and the script integer stood in for; in our run the asset
    vtables point at ours. Every call and the arena compared.
  - Mutations: 64 deliberate bugs; 63 caught (56 at first: a base destructor on a typed asset, the key-0 lookup,
    construction on dirty memory, the manager really destroyed (three references), a cutscene's IsLoaded counted,
    a name whose only "extension" is its first letters added for 7). The other one can't be tested: a cutscene
    from LoadAssetFromStream (the original crashes).
- [x] Batch 174 (AN-3; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the asset types): src/game/ass/asset_types.cpp (new), asset.h.
  - The load parameters' pool: 0x00407730 AllocLoadParams (4 blocks of 0x60 at 0x00691758; the size ignored),
    0x00407770 FreeLoadParams. 0x00408A70 the types' Reload (Unload, then Load(name, 0, 0, 0, 0) == 0).
  - Animation: 0x004072E0 / 0x004072D0, 0x004071C0 LoadFromBuffer, 0x00407300 Load ("<name>.<platform
    extension>", from a file or through Pip - then the lowercased name's checksum kept at +0x4C), 0x004074F0
    FinishLoading (Gfx's anim frame data, 0x3C bytes: 0x00482AF0 / 0x004856C0; Load is called on a failed
    allocation too), 0x004074A0 Unload, 0x00407290 FreeBuffer (also the node arrays').
  - Cutscene: 0x00408AC0 / 0x00408AB0, 0x00408AD0 Load (a CCutsceneData loaded; no StartLoading; the data leaks
    if Load fails), 0x00408A40 Unload (only the data deleted), 0x00408AA0 IsLoaded (the data's).
  - Node array: 0x00408BE0 GetType (10, or 11 when the node manager's owner) / 0x00408BD0, 0x00408EA0 /
    0x00408D10 (the file read on the top-down heap; a missing file returns with the heap context still pushed, as in
    the original), 0x00408C10 FinishLoading (0x005A5990 parses the script, the node array added to the node manager,
    the script array kept), 0x00408F00 Unload (each node's object found by its name and told - vtable slot 6 -, the
    node array removed, the script unloaded by 0x005A59B0), 0x004090D0 CreateNodeArray (a new array of empty
    structures made a global symbol).
  - Scene: 0x004093D0, 0x004092D0 SetUpParams (textures checksum 0xDC8116B2, four flags), 0x00409520 / 0x004093F0
    (the file name from 0x0048C390), 0x00409240 FinishLoading (0x0048CD80 with the group's textures), 0x00409380
    Unload (0x0048C690). Scene collision: 0x00409750, 0x00409690, 0x00409890 / 0x00409760 (0x0049CA20),
    0x00409610 (added to the group's scene: 0x0049D590 / 0x0049C9F0), 0x00409700 (0x0049CFB0).
  - Skeleton: 0x00409A40 / 0x00409A30, 0x00409990 LoadFromBuffer (never copies), 0x00409A50 Load (the buffer on
    the top-down heap), 0x00409B80 FinishLoading (the skeleton data, 0x598 bytes: 0x004A2C20 / 0x004A39C0; false and
    a leak if it fails), 0x004099E0 Unload, 0x00409A10 FreeBuffer.
  - Skinned model (0x60: +0x4C archive, +0x50 parameters, +0x54 / +0x58 the .cas file and its contents, +0x5C the
    files still being read): 0x00409E00 / 0x00409DF0, 0x00409E10 SetUpParams, 0x00409E90 the parameters' Clear,
    0x0040A140 / 0x00409EB0 Load ("<base>.skin|mdl.<ext>" - 0x0048BFE0 says whether the name has a .cas file -
    through Pip when it has one and the load isn't async; then "<base>.cas.<ext>"), 0x00409C70 FinishLoading
    (0x0048C030), 0x00409D30 Unload, 0x00409D70 Abort (the .cas read too), 0x00409DA0 FreeBuffer, 0x00409C60
    SetRead (the async read's: read only once both files are; not a function in Ghidra).
  - Skin collision: 0x0040A290 / 0x0040A280, 0x0040A400 / 0x0040A2A0 (0x00490AD0), 0x0040A510 (added to the
    group's model: 0x00490B80; the data its +0x8C), 0x0040A230 (0x00490A90). Texture dictionary: 0x0040A710,
    0x0040A620 SetUpParams, 0x0040A840 / 0x0040A720 (0x0049EF90), 0x0040A5A0 (0x0049F050), 0x0040A6C0 (0x0049EFD0).
  - Tests (selftests_anim3.cpp): TestAnimAssetTypes - for every type: 10 Loads (blocking / async, file / archive,
    parameters / a structure / neither, the file - or the .cas file - missing, a mixed-case name), each then unloaded;
    4 LoadFromBuffers (copied or not, parameters with the skin's heap flag set); FinishLoading on its own with the
    engine's loader failing and working, then Unload with a buffer still there; FreeBuffer with an archive, a buffer,
    nothing; the names and types. Also the owned node array's type, CreateNodeArray (an old symbol and none), the
    skin's Abort / SetRead / Clear, Reload, the parameter pool exhausted. Every engine, file, Pip, script, node
    manager and cutscene call stood in for (recorded); in our run the asset vtables point at ours; the asset, the
    parameter pool and the arena compared after each step.
  - Mutations: 70 deliberate bugs; 69 caught (62 at first: dirty file handles, distinct object vtable slots, a
    hashed flag answer, a clean parameter pool per case, FinishLoading's result and Unload with a buffer, the
    skin's copy with its heap flag added for 7). The other is equivalent: the archive name's lowercasing (the
    checksum ignores case).
- [x] Game run (2026-10-20, C:\Games\THUG2-1, stock THUG2.exe, AN-1..AN-3): free skate in Boston (-level BO) for
  90 s - running and responding, the skater in the level; 2953 of 2953 replacements installed, late and
  engine-started self-tests passed, shadow checks no mismatches; no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT
  REPORT. Two early self-tests failed that aren't this branch's: "anim controllers: partial" (fe_anim) and "text
  elements: updates" - the fe test kit's half-pointer mapping: the arenas landed near 0x00FC0000 in the game, where
  plain values 0x00FF0006 / 0x01000000 look like halves of arena pointers in our run only (also seen once offline).
- [x] Batch 175 (AN-4; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; math helpers and the collision caches): src/game/nx/coll_cache.h /
  coll_cache.cpp (new), src/game/mth/vector.cpp / matrix.cpp / geometry.cpp (+ headers).
  - Mth: 0x00406560 IsAngleBeyond (not a function in Ghidra; called by the skater code: whether two vectors are
    more than `degrees` apart - false if either is shorter than 0.5; x87 FCOS of the limit, the rest in double),
    0x0040AED0 Vector::operator==, 0x0040AF20 operator/(Vector, float) (the reciprocal rounded to a float for x, y, z,
    w multiplied by the unrounded one), Matrix 0x0040AF60 Transform / 0x0040B000 TransformAsPos / 0x0040B090
    RotateVector (each sum in the original's order, rounded once), 0x0040B100 ScaleRows, BBox 0x0040AB00
    operator=, 0x0040B170 Intersects (x, z, y; NaN doesn't rule it out), 0x0040B1D0 Contains (NaN does).
  - Nx::CCollCache (0xFCC bytes: a box, 500 (object, geometry) entries - the static ones first -, the counts at
    +0xFC0 / +0xFC4 / +0xFC8): 0x0040AB50 constructor (registered), 0x0040AB40 destructor, 0x0040ABA0 Update (the
    grid lists along the box's diagonal - 0x0048C980 -, each list's objects - 0x004188C0 - and the movable list
    0x00691984 (objects with flags 2 / 4 skipped) tested against the box grown by 0.5 - vtable slot 13; the
    geometry from slot 21 / 20), 0x0040A910 RemoveStatic (shifts), 0x0040A990 RemoveMovable (the last moved in);
    the registry (8 caches at 0x006918EC, bits 0x0069190C): 0x0040AA00 / 0x0040AA40 / 0x0040AA80 / 0x0040AAC0;
    0x0040ADD0 ShapeTypeFromChecksum (box and two others 1, sector 4).
  - Tests (selftests_anim4.cpp): TestAnimMathHelpers - 4000 random cases (zeros, -0, NaN, small integers, +-3e19,
    equal vectors, scaled copies; vectors 0.25-0.6 long; b exactly 0..179 degrees from a with limits on and around
    it; 81 matrices of cancelling columns), each helper's result bit for bit, in place too; TestAnimCollCaches -
    three caches and six more registered (the ninth refused), updated with no grid lists and with lists and
    movables (flags 2 / 4 / null data), removals from each part and from all caches, a double registration and
    unregistrations, the shape types; the grid / list queries and the objects' slots stood in for (recorded).
  - Mutations: 37 deliberate bugs; 36 caught (24 at first: lengths at the 0.5 limit, angles at their limit, the
    cancelling matrices, the whole grown box recorded, removals of the other part's objects, the stale registry
    pointer added for 12). The other is equivalent: the grown box's w (AddPoint ignores it).
- [x] Batch 176 (AN-5; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the collision object base and the multi-object): src/game/nx/coll_obj.h /
  coll_obj.cpp (new).
  - Nx::CCollObj (0x10 bytes, vtable 0x00645A30: +0x04 checksum, +0x08 flags - 0x8000 owns its geometry, 0x4000
    shares it -, +0x0A terrain, +0x0C geometry; the vtable slots as far as known are listed in coll_obj.h):
    0x0040B370 SetGeometry (the geometry's checksum kept), 0x00413390 the scalar deleting destructor (the base's
    body: an owned, unshared geometry destroyed - 0x00416970 - and freed).
  - The lists of collision objects: 0x004131D0 / 0x00411880 Lst::Node<CCollObj> (vtable 0x00645AA0), 0x00413180 /
    0x004131A0 Lst::Head<CCollObj> (0x00645D34).
  - Nx::CCollMulti (0x3C bytes, vtable 0x00645AA8: +0x10 its children's list, +0x14 their box, +0x34 dirty, +0x35
    the box valid, +0x38 a value): 0x004133E0 constructor, 0x00411D10 copy constructor, 0x00411B10 destructor
    (each child and its node deleted, the head, out of the caches, the base's body), 0x00413460 scalar deleting,
    0x00413490 Clone (the copy flagged 0x8000, a clone of each child), 0x00411BE0 AddChild, and the slots
    0x00411D90 (1: every child's, stopping at the first false), 0x00411CC0 / 0x00411C30 / 0x00411C80 (4, 5, 6:
    passed to the children; 4 and 5 mark the box dirty), 0x00411F60 / 0x00411F80 (7, 8: the first child's - a
    tail jump; no children crashes, as in the original), 0x00410960 (9: a zero vector), 0x0040AE20 / 0x0040AE10
    (10, 11: +0x38; the getter source only, 4 bytes), 0x0040AE30 (3: terrain 0; source only), 0x00411E80 (13:
    false only if the box is known and misses), 0x00411EC0 (14: every child asked, any hit), 0x00411F20 (19),
    0x00411E50 (20: the box, recomputed when dirty; null if a child has none), 0x00411DD0 RecomputeBBox.
  - Tests (selftests_anim5.cpp): TestAnimCollMulti - three multis on dirty memory, stand-in children (every slot
    recorded with its arguments), a nested multi, a child that answers false, one without a box; each slot (the
    dirty flag after each pass-down), the box tests far and near (also with no box), Clone, the copy, deletion with
    an owned / shared / unowned geometry and a registered cache holding them, the base's deleting destructor
    freeing and not, the list node / head functions; allocation and frees stood in for, our vtables in our run.
  - Mutations: 35 deliberate bugs, all caught (28 at first; the unowned geometry, freeing with flag 1, a registered
    cache, the clone's flags, the dirty flag after each call and the box test with no box added for 7).
- [x] Batch 177 (AN-6; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the movable collision objects and raw vertices): src/game/nx/
  coll_movable.h / .cpp, coll_tri.h / .cpp (new).
  - Nx::CCollObjTriData (the geometry; only what's used so far): 0x0040B230 / 0x0040B2D0 GetRawVertexPos (16-bit
    vertices in sixteenths from the box's minimum, or floats - the second copies those as raw words; w = 0).
  - Nx::CCollMovable (0xA4 bytes, vtable 0x00645B00: +0x10 position, +0x20 orientation, +0x60 its transpose,
    +0xA0 a value): 0x00411FA0 constructor, 0x00412660 copy, 0x004121A0 destructor (out of the caches' movables),
    0x004135B0 scalar deleting, 0x00412070 SetOrientation (rows 0-2, the transpose of the whole matrix; 0x00412FF0
    jumps to it - source only), 0x00412050 / 0x00412060 (source only: 4 bytes, shared with the scaled object),
    0x00412230 GetVertexPos, 0x004122C0 BoxToLocal, 0x0040AE50 / 0x0040AE40. 0x004120D0 / 0x00412150 the matrix
    transpose (from another / in place).
  - Nx::CCollMovBBox (0xD0 bytes, vtable 0x00645BC0: a box, one set of face flags and terrain): 0x00412550
    constructor (the default box (-50, -20, -90)..(30, 36, 90)), 0x00413700 copy, 0x004125F0 / 0x004136D0
    destructors, 0x00412600 SetGeometry (also its box; a null geometry crashes, as in the original), 0x0040AE60 /
    0x0040AE70 face flags / terrain, 0x004126E0 DebugRender (the box through 0x0048D090).
  - Nx::CCollScaled (0xA0 bytes, vtable 0x00645B58: scale, matrix, inverse): 0x004123C0 constructor, 0x00412460 /
    0x004135E0 destructors, 0x00413610 / 0x00413640 / 0x004136A0 set position / orientation / scale,
    0x00412490 UpdateInverse (each row's 1 / scale unrounded for its three products), 0x004109C0 (source only),
    0x004109D0 GetVertexPos (scaled, w the scale's w, through Matrix::Transform - row 3 counts by that w).
  - Tests (selftests_anim6.cpp): TestAnimCollMovables - every function on objects built on dirty memory with random
    transforms and compressed / plain geometry (each object recorded after its constructor), copies of objects
    with distinct fields, transposes in place and not, the debug box, the destructors with owned and shared
    geometry and a registered cache holding the movables.
  - Mutations: 35 deliberate bugs; 33 caught (26 at first: each object recorded right after construction, distinct
    terrain and +0xCA bytes for the copies added for 7). Equivalent: the movable vertex's w (always 0 + the
    position's), the box destructor's own vtable write (overwritten at once by the base's).
- [x] Batch 178 (AN-7; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the triangle collision objects - a sector's, an instance, a movable
  one; 37 functions): src/game/nx/coll_static.h / .cpp (new), coll_movable.cpp (CCollScaled copy), coll_tri.h.
  - Faces: 0x00410BF0 / 0x00411750 the face flags (u16 at face +0, zero-extended) / terrain (u16 at +2), faces of
    8 bytes when the geometry's +0x0A byte is set, else 10 (slots 2, 3 of all three objects).
  - Nx::CCollStatic (0x18 bytes, vtable 0x00645C18; +0x10 the "face-rejection optimization" buffer, a byte per
    face): 0x00412760 destructor (the buffer freed only if owned, flag 0x8000), 0x004139A0 Clone (hidden code:
    an instance - a CCollStaticInstance sharing the geometry - or a copy with the geometry copied by 0x00416BF0
    and the buffer copied on the bottom-up heap; null if the geometry copy fails), 0x00410C20 SetWorldPosition
    (the geometry moved by the difference from slot 7's, 0x00416400), 0x004123A0 / 0x004123B0 (the shared origin
    0x00691914 / orientation 0x00691928), 0x00410980 GetVertexPos (w = 1), 0x00410C70 / 0x00412750 the box.
  - Nx::CCollStaticInstance (0xC8 bytes, vtable 0x00645C78: a CCollScaled with a world box): 0x004127C0
    constructor, 0x00414C30 copy, 0x00412840 / 0x00413ED0 destructors, 0x00414CD0 Clone (shares the geometry),
    0x00411480 SetSharedGeometry (flags |= 0xC000, the geometry's box), 0x00413F00 / 0x00413F30 (hidden) /
    0x00413F50 set position / orientation / scale (the inverse and world box made again), 0x004113B0
    UpdateWorldBBox, 0x00411460, 0x00412830, 0x00411330 RescaleIfAt (only at its exact position), 0x00412F50
    DebugRender (the scaled matrix to 0x00414D60). CCollScaled's copy constructor 0x00413F80.
  - Nx::CCollMovTri (0xD4 bytes, vtable 0x00645CE0: radius, dirty flag, box): 0x00413000 constructor, 0x00413080
    copy, 0x00413070 / 0x004143D0 destructors, 0x00414400 Clone (geometry copied), 0x00411780 SetGeometry (the
    bounding radius from the raw vertices), 0x00412F90 SetWorldPosition (hidden code), 0x00411810 / 0x00412FC0
    (the box made again when dirty), 0x0040B390 UpdateBBox, 0x00413130 DebugRender. 0x00414D60 the geometry's
    debug render (empty; source only).
  - Tests (selftests_anim7.cpp): TestAnimCollTriObjects - a sector object built by hand on compressed and plain
    stand-in geometry: faces small and not, the slots, the vertex, the clones (instance, copy with and without
    its buffer, geometry copy failing); the instance's setters (recorded after each), world box, rescale at /
    not at / off only in y, clone, render (0x00414D60 stood in for, recording the matrix); the movable's geometry,
    radius, position, box (recorded after each step), clone, render; destructors with owned and unowned buffers.
  - Mutations: 42 deliberate bugs; 38 caught (28 at first: the origin's w, owned / unowned buffers in the
    destructors, the buffer before SetSharedGeometry, recording after each setter, a position off only in y and
    a stand-in for the debug render added for 10). Equivalent: the scaled vertex's w in UpdateWorldBBox (unused by
    the box), the constructor's dirty flag (SetGeometry sets it), the radius compare's > vs >= and its sum order
    (the same float after the square root).
- [x] Batch 179 (AN-8; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the collision geometry and its BSP tree; 27 functions): src/game/nx/
  coll_tri.h / .cpp, mth/matrix.cpp; coll_obj.cpp / coll_static.cpp now call the geometry's methods directly.
  - Nx::CCollBSPNode (8 bytes, new[]'d: +0 the axis in the low 2 bits - 3 a leaf - and the split in sixteenths
    above; a leaf's face count at +2 and face index list at +4; a split node's two children at +4, bit 0 swapping
    which is the "less" side): 0x00414D70 constructor (hidden code, zeroed), 0x00415C90 / 0x00415CE0 node /
    face counts, 0x00415D30 the first face list (the lowest pointer: the shared array), 0x00415D80 Translate,
    0x00415E00 RotateY (quarter turns about Y around a centre: x and z splits swap axis, some negated with their
    sides swapped), 0x00415F30 Scale, 0x00416000 Relocate (a file's offsets to pointers; cdecl), 0x00416A10
    Clone (nodes and face lists copied, pointers moved; the argument unused).
  - Nx::CCollObjTriData: +0x34 the tree, +0x38 one byte per vertex. 0x00414EE0 SetRawVertexPos (compressed:
    sixteenths toward zero, x's offset not rounded to a float first), 0x00414F90 GetRawVertices (0x00415C00
    jumps to it: source only), 0x00415C10 SetRawVertices (the box first), 0x00414D80 GetFaceNormal ((v1 - v0) x
    (v2 - v0); one edge's z unrounded in the x term, as the original's x87 code), 0x00415B70 CopyFrom, 0x00416BF0
    Clone (vertices, faces, vertex bytes, tree; null without vertices when asked, or when an array isn't
    allocated), 0x00416970 destructor, 0x00416080 DeleteBSP (delete[]'s element destructor 0x005480A0 is
    empty), 0x004169C0 Relocate (first argument unused), 0x00416400 Translate (plain vertices, box with w, tree),
    0x00416320 GetFacesInBox / 0x004160E0 its walk (globals 0x00691968 buffer, 0x0069196C count - -1 on
    overflow, then every face: the index table 0x0067C6B8 - and 0x00691970 the buffer size, only stored),
    0x004163B0 IsPointBehindFaces / 0x004161D0 its walk (down the "less" sides only; a leaf's faces counted until
    the point is in front of one; true when that's every face of the geometry).
  - Mth::Matrix::operator== 0x00416D80 (all 16 values; NaN unequal).
  - Tests (selftests_anim8.cpp): TestAnimCollGeometry - two hand-made geometries (plain with 10-byte faces,
    compressed with 8-byte faces) and trees with swapped sides: counts, first lists, every face's normal (one
    degenerate), raw vertices got and set (one offset not exact in a float), boxes reaching some leaves, all
    (overflowing at 2, 4, 8 faces; exactly 9 of 9), none, no tree; points down each path and on a face's plane;
    moves, each quarter turn with and without the centre, scaling (a split needing more than a float's bits), a
    file's geometry fixed up, copies with / without vertex bytes and vertices, the tree's copy and a leaf's, a
    16-bit leaf count, deletion; Matrix == with one value changed, NaN, -0 / +0. Our run's stand-ins also cover
    our own Destruct / Clone / Translate in the AN-5..AN-7 tests.
  - Mutations: 72 deliberate bugs; 68 caught (57 at first: the leaf count's high byte, an offset not exact in a
    float, vertex bytes on an empty copy, overflow early and exactly at the face count, a point on a face's plane,
    a big split added for 11). Not caught: the squared length's sum order (below a float), the buffer size
    global (stored and cleared, never read), the point's x offset kept unrounded (x87 detail too small for the
    test data), the nodes' constructor before the copy overwrites them.
- [x] Game run (2026-10-20, C:\Games\THUG2-1, stock THUG2.exe, AN-1..AN-8): free skate in Boston (-level BO), 90 s
  standing and 45 s skating (keys posted: forward, turns; the skater rode along the plaza and up a wall) - running
  and responding; 3087 of 3087 replacements installed (69 source only), early (314), late (17) and engine-started
  (2) self-tests passed; no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT.
- [x] Batch 180 (AN-9; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the geometry turned and scaled, the forwarding slots, the movable
  collision factory; 11 functions): src/game/nx/coll_tri.cpp, coll_movable.cpp, coll_static.h / .cpp.
  - Nx::CCollObjTriData: 0x004164F0 RotateY (quarter turns about Y around a centre: moved there, the tree turned,
    compressed vertices through a temporary array on the top-down heap, the box rebuilt from its turned corners,
    moved back, +0x3C cleared; a failed allocation of the array crashes, as in the original), 0x00416730 Scale
    (the same; the box's corners scaled - w kept, not re-sorted), 0x00414D30 SetUpAllFaces (the shared index
    table 0x0067C6B8 filled with 0..2047 while its first entry isn't 0; whether there's a tree).
  - Nx::CCollMovBBox::Clone 0x00414BE0 (slot 12: a copy, flag 0x8000, no geometry).
  - The slots that pass on to the geometry (Nx::CollGeometryForwarders): 0x0040AE80 / 0x0040AE90 (a sector's slots
    22, 23: turned / scaled), 0x0040AEA0 (its slot 19: the geometry's debug render 0x0048DCD0, compiled out),
    0x0040AEB0 (0x00414FE0 - not rewritten yet), 0x0040AEC0 (slot 18 of every collision class:
    IsPointBehindFaces).
  - Nx::CreateMovableCollision 0x00414990 (cdecl): type 1 a box around all the geometries' boxes, type 4 a movable
    triangle object (several: a multi-object of them; none: null), other types null; the value set through slot
    11 and the object registered (0x004177B0, not rewritten yet).
  - Tests (selftests_anim9.cpp): TestAnimCollTurnScale - plain and compressed geometry with and without a tree,
    each quarter turn 0-4 and a scale with a negative factor, around centres; the face table filled (first entry
    0xFFFF, another non-zero value) and not (0); the box's clone; each forwarding slot on a sector object (the
    debug render, 0x00414FE0 and the registration stood in for); the factory for types 1, 4, 2, 0 with 0, 1 and 3
    geometries.
  - Mutations: 36 deliberate bugs; 35 caught (33 at first: +0x3C set before the scale, a first table entry that's
    neither 0 nor 0xFFFF added for 2). Equivalent: the turn's move-there w (the box is rebuilt with w 1 before the
    move back).
- [x] Batch 181 (AN-10; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the geometry's occlusion polygons; 1 function, 2.9 KB):
  src/game/nx/coll_tri.cpp, coll_static.cpp.
  - Nx::CCollObjTriData::AddOcclusionPolys 0x00414FE0 (0x0040AEB0 passes on to it): each face paired with the first
    later face sharing exactly two corners (compared as 16 bits, each of face i's corners matched with the first
    equal corner of face j) whose normal ((v1 - v0) x (v2 - v0), normalised unless zero, rounded where the x87 code
    stores) is within 0.05 on each axis; the quad - face i's corners with face j's other corner between the shared
    ones - to Nx::AddDebugShape(4, ...) (0x0048BF80's jump target: an occlusion polygon, 0x004D8D70) with the
    geometry's checksum. Each face's flags are read and tested for a sign they can't have (no effect).
  - Tests (selftests_anim9.cpp): TestAnimCollOcclusion - a 4x4 grid split into triangle pairs (diagonals, corner
    orders and some windings random; vertices lifted within, near and beyond the tolerance), stray, repeated and
    degenerate triangles, shuffled; then in order a collinear and a degenerate face sharing two corners, a tilted
    quad's triangles with face i's shared corners at 0-1, 1-2, 0-2, corner indices above 255 differing only above
    the low byte, a last adjacent pair; plain (10-byte faces) and compressed (8-byte faces), and with 0-2 faces.
    AddDebugShape stood in for (each quad's 16 floats and the id recorded): 25 quads.
  - Mutations: 24 deliberate bugs; 22 caught (15 at first: the ordered cases above and the 0.16 lift added for 7).
    Not caught: one edge's z rounded to a float (x87 detail, below the tolerance), < for <= at the tolerance.
- [x] Batch 182 (AN-11; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the collision intersection tests; 3 functions): src/game/nx/
  coll_tests.h / .cpp (new), mth/vector.cpp.
  - Nx::RayHitsTriangle 0x0040B420 (cdecl: Moller-Trumbore from the front only - a determinant below 0.03
    misses -, the barycentric bounds allowing the determinant times 1.00001, t in 0..1 written once it's reached),
    Nx::LineHitsSphere 0x0040B610 (the line's distance from the centre against the radius, then the nearer crossing
    against a limit), Nx::SegmentsCross2D 0x0040B750 (2D segments crossing for s and u in 0..1; s = 0 below a
    1e-11 denominator; the first sign test uses the denominator before it's rounded to a float). The arithmetic
    rounds to floats only where the x87 code stores. LineHitsSphere and SegmentsCross2D are registered through
    naked adapters that keep EDX: their callers 0x0040EC90 / 0x0040D370 (not rewritten yet) keep a value in it,
    which the originals don't touch (scan_register_assumptions still lists those 6 sites as RISK until the callers
    are rewritten - it can't see the adapters; TestAnimCollIntersectionsKeepEdx proves it).
  - Mth::CrossProduct 0x00402550 made exact: each component's products kept unrounded and the difference rounded
    once, as the original (ours rounded each product to a float - last-bit differences; its old test only checked
    within a tolerance).
  - Tests (selftests_anim10.cpp): TestAnimCollIntersections - 4000 random rays at triangles (from either side,
    near edges and corners, along the plane, too short, going away, degenerate triangles, NaN / infinite / huge /
    tiny values) plus exact cases (t = 1, u = 0, inside and beyond the 1.00001 slack, from behind and below), 4000
    lines at spheres (each called again with the limit at the crossing found) and 4000 segment pairs (parallel,
    collinear, touching, nearly parallel, through the middle; a crossing with a 1e-12 denominator): results and
    outputs bit for bit (any NaN counted as one: x87 and SSE pass different NaNs on). TestAnimCollIntersections-
    KeepEdx: both adapters called with a marker in EDX.
  - Mutations: 35 deliberate bugs; 30 caught (24 at first: the exact ray cases, the second sphere call and the tiny
    denominator added for 6). Not caught: the determinant's sum order, the bound from the unrounded determinant, u
    kept unrounded (rounding below what the comparisons see), >= for > against the bound and the unrounded
    denominator's sign (exact equality / underflow only).
- [x] Batch 183 (AN-12; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the point / triangle distance; 1 function, 1.4 KB of x87):
  src/game/nx/coll_tests.h / .cpp.
  - Nx::PointTriangleDistanceSquared 0x0040B860 (cdecl; 16 callers in 0x0040BDC0-0x0040FAE1): Eberly's squared
    distance with its seven regions, the nearest point's s and t written when their pointers aren't null. Returned
    as a double: the original leaves the unrounded value in ST0 and some callers compare it before storing it. One
    region keeps the original's slip - below the first edge's start (s < 0, t >= 0) e is compared with c without
    its sign flipped (a missing FCHS), so it always clamps along the second edge. Most of its tests read the flags
    so that NaN counts as "less"; the C++ follows that ("below").
  - Tests (selftests_anim10.cpp): TestAnimCollPointTriangle - 12000 points over and beside random triangles (the
    parameters spread over -1..2, lifted off the plane; corners and edges; collinear, repeated and zero triangles;
    NaN / infinite / huge / tiny values; each output pointer left null at times) and every integer point of a
    9 x 9 x 3 grid around five integer triangles (exact ties); the distance's 64 bits, s and t compared.
  - Mutations: 34 deliberate bugs; 29 caught (28 at first: a triangle where d = 0 below both edges, found by a
    brute-force search over integer cases, added for 1). Not caught: the NaN-as-less reading in two places a NaN
    can't reach (it would have made s or t NaN first), s * (1 / det) for s / det (rounding below a float), >= for >
    and < for <= in region 2 (the same results either way - checked over all small integer cases).
- [x] Batch 184 (AN-13; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the line tests' hit record and the movable box's line test; 2
  functions): src/game/nx/coll_line.h / .cpp (new), coll_movable.h, mth/geometry.cpp.
  - Nx::CollisionHit (our name; 0x50 bytes: normal, point, face, distance, ignore / require flags, the face's flags
    and terrain, the object and its checksum, a callback (cdecl, given the record) and its data, hit, the face's
    flag 0x40; +0x38 / +0x44 / +0x4C not known) and its copy 0x004F8160 (fieldwise, the padding left; outside
    this range - written for ReportHit, not registered).
  - Nx::ReportHit 0x004131F0 (cdecl; the slot-14 line tests' report): a face hit at a distance along the line is
    taken unless its flags include an ignored one or lack a required one - the record filled (the point
    recomputed along the line, w = 1) and the callback called; but unless it's nearer than the record's distance
    (not nearer while 0x00691910 asks for the farthest), the record is copied back as it was and false returned,
    the callback having run.
  - Nx::CCollMovBBox::CollideWithLine 0x004137B0 (slot 14: the line, its direction - unused -, the record, the
    line's box - unused): the line taken into the box's space, its box against the object's, BBox::LineIntersect,
    the point and normal turned back (RotateVector) and placed, the distance |hit - start| / |end - start|. The
    face index handed on is left unset by the original (stack garbage); 0 here - the test leaves that word out.
  - Mth::BBox::LineIntersect 0x00403550 made exact: the crossing fraction and the crossing point in double,
    rounded to floats where the original stores them (ours did them in floats - last-bit differences).
  - Tests (selftests_anim11.cpp): TestAnimCollLineReport - 3000 lines at a movable box placed and turned at random
    (through it, beside it, starting / ending in it, zero length), records with distances to beat below / at /
    above the hit, ignored / required flags, a callback (which also changes the record), nearest and farthest
    wanted, each hit tried again at its own distance; then 400 ReportHit calls on a sector object's faces (each
    face's flags and terrain). Records (all but padding), callback calls and results compared.
  - Mutations: 35 deliberate bugs; 33 caught (26 + 4 at first: the callback changing more of the record added for
    the copy's fields). Equivalent: the box pre-check (LineIntersect rejects those lines anyway) and the placed
    point's w (ReportHit recomputes the point).
- [x] Batch 185 (AN-14; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the triangle objects' line tests; 4 functions): src/game/nx/coll_line.cpp,
  coll_static.h, coll_movable.h / .cpp, coll_tri.h.
  - Slot 14 of the three triangle classes: Nx::CCollStatic::CollideWithLine 0x00413B50 (the faces of the line's box
    - GetFacesInBox -, each skipped when its rejection byte - the sector's buffer for 10-byte faces, the face's last
    byte for 8-byte ones - is below a threshold: the line's distance from the geometry's minimum corner times the
    geometry's +0x3C read as a float, truncated; past 255 nothing is tested; each face tried with RayHitsTriangle,
    its normal (v1 - v0) x (v2 - v0) normalised, offered to ReportHit with the corner), CCollStaticInstance::
    CollideWithLine 0x00413FF0 (the same in the instance's space, no rejection; the normal normalised inline and
    turned by the matrix), CCollMovTri::CollideWithLine 0x00414460 (in the object's space; rejection only for 8-byte
    faces; the normal's length and inverse rounded to floats, its x kept unrounded for the turn). Each rounds where
    its x87 code stores - they differ. The corner each passes on as the hit point is dead (ReportHit recomputes the
    point from the line).
  - Nx::CCollScaled::LineToLocal 0x00410A30: both ends through the inverse (Transform, w included).
  - Tests (selftests_anim12.cpp): TestAnimCollTriangleLines - a 60-face random mesh, plain (10-byte faces) and
    compressed (8-byte faces), with rejection bytes (some 255) and scale; a sector over it, an instance (scaled,
    turned, placed) and a movable object (turned, placed), re-placed every 100 lines; 1500 lines per mesh through
    and around them, half with a BSP tree (a split on x: the line's box decides the faces), the record's distance
    and flags varied, nearest / farthest, scales rejecting nothing / everything; 200 lines at exactly threshold 255
    (only the 255 faces tested); LineToLocal on random lines. 559 hits of 3000.
  - Mutations: 32 deliberate bugs; 25 caught (22 at first: the threshold-255 lines, the tree and the movable's box
    added for 3). Not caught: the corner's scale, transform and placing (dead: see above), and four roundings in
    the rejection distance (below the threshold's integer steps).
- [x] Batch 186 (AN-15; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the animation blender component; 47 functions, 0x00419690-0x0041A6D0
  with the hidden runs 0x00419690, 0x00419750, 0x004198D0, 0x00419AD0, 0x00419BF0, 0x00419E90, 0x0041A1A0):
  src/game/obj/anim_blender.h / .cpp (new). Started by the previous session, finished here.
  - Obj::CAnimBlenderComponent (type 0x7149B753, 0x44 bytes, vtable 0x00645D8C): Create 0x0041A5B0, Construct
    0x0041A410, DestroyBody 0x0041A4C0 (the observer list emptied twice, as the original), Destroy 0x0041A610;
    member functions registered 0x00419690 (table 0x00682350); the vtable: Update 0x0041A260 (channels after the
    first below weight 0.00001 only reset; several channels: Gfx::BlendPosesById set by set up to the highest bone
    set; observers told; the skeleton given the pose), InitFromStructure 0x00419DD0, Finalize 0x004196D0,
    PassToChannels 0x00419980, CallMemberFunction 0x00419950; the script member functions AddChannelFromScript
    0x0041A1A0, WaitOnChannel 0x00419E90, RemoveChannel 0x00419750, BlendChannel 0x004197C0, ChannelExists
    0x00419F10, PassToChannel 0x004198D0; the channel walks FindChannel 0x00419D40, AddChannel 0x00419FD0,
    NotifyObservers 0x0041A220, VisitChannels 0x004199C0, PassToEachChannel 0x004199F0, ResetChannels 0x00419A20,
    AnyChannel 0x00419C10, AllChannelsFlag29 0x00419C70, ModelReady 0x00419CA0; the first channel's calls
    0x00419730, 0x00419A90, 0x00419B50, 0x00419B90, 0x00419BB0, 0x00419BD0, 0x00419BF0, SetFlag4C 0x00419B30,
    GetValue / HasValue 0x00419AF0 / 0x00419B10 (Gfx::CBlendChannel's, by their established names), GetAnimSet
    0x00419AB0, 0x00419AD0, IsAnimComplete 0x00419CD0, 0x00419F80, 0x0041A0A0, GetTimes 0x0041A0E0; the channel
    links' destructors 0x00419D70 / 0x00419D90 / 0x0041A170 and a channel's GetTimes 0x00419CF0; LargerOf
    0x004196B0; VectorAt1C::Get 0x0041A680. The original's crashes on missing channels kept (commented).
  - Script structures through CStruct's own methods (GetChecksum / GetFloat / AddChecksum / ContainsFlag), the pose
    blend through Gfx::BlendPosesById.
  - Tests (selftests_anim13.cpp): TestAnimBlenderComponent - stand-in channels (a fake vtable), observers, script
    structures and channel methods (ours and the originals' alike); every function with and without channels, ids
    found and not, channels at the front / back, the update with no change, one and several channels (weights
    below the threshold after the first, bone sets), the observers, destroyed. Calls out, results and the arena.
  - Mutations: 60 deliberate bugs, all caught.
  - The self-test runner got a watchdog (src/hook/selftest_runner.cpp, shared): a test still running after 45 s
    is named in the log ("SELFTEST HANG in ...") and the process ends (exit 3) - an earlier anim test once hung
    thug2_check. tools/mutate.py lists those lines too.
- [x] Batch 187 (AN-16; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the world's line queries, the movables' list and the collision feeler;
  20 functions): src/game/nx/coll_world.h / .cpp, feeler.h / .cpp (new), gfx/debug_lines.h (new), coll_line.h /
  .cpp (two fields named), skater/debug_collision.cpp / .h.
  - Nx::RegisterMovable 0x004177B0 / UnregisterMovable 0x00417800 (the movable objects' list at 0x00691978).
  - Nx::CollideWithStatics 0x004106C0 (the hit's distance to beat 1.01 / -0.01; through the cache when it holds
    the line's box - its statics whose boxes touch the line's box grown by 0.5 - else the scenes' grids along
    the line, each object's slot 13 first, the hit's scene noted), CollideWithStaticsFarthest 0x00410930,
    CollideWithMovables 0x004118C0 (the distance only reset when the hit has nothing; the cache's movables with a
    box, or the list's without flags 2 / 4; the object's owner on the hit while it's tested; the owner of the
    last object hit returned), CollideWithMovablesFarthest 0x00411AE0.
  - Nx::CFeeler (0xB0 bytes): Construct 0x00417760 and with a line 0x00417250, SetIgnore 0x004170F0, SetStart /
    SetEnd / SetLine 0x00417190 / 0x004171B0 / 0x004171D0, GetCollision 0x004172F0 (static then movables, the
    feeler's cache or the current one 0x00691974; the object hit in the smart pointer, its id; distance, point
    and normal copied; 1 / 0 in EAX), GetCollisionAlong 0x00417710, IsHitTriangleCollision 0x00417120,
    GetTestedOwner 0x00417140, HitObjectAlive 0x00417210, GetHitObject 0x00417240, DebugDraw 0x00417150
    (compiled out: Gfx::AddDebugLine), CallbackRelay 0x00417110 (hidden code: the feeler's callback given the
    feeler).
  - CollisionHit's +0x44 is the tested movable's owner (`owner`), +0x4C the scene of the static object hit
    (`scene`).
  - debug_collision: its REPLACE_FALLBACK wrapper on 0x004172F0 is gone; GetCollision calls
    DebugCollision::NoteFeelerQuery (NOT ORIGINAL, logs only with [features] debug_collision).
  - Other code still reaches the feeler through its addresses (local wrappers in park_floor, rail_manager,
    camera_util, editor_camera, avoid, composite, level_editor): left as they are.
  - Tests (selftests_anim14.cpp): TestAnimFeelerQueries - 12 stand-in collision objects (fake vtable answering
    slots 1, 10, 13, 14 from a per-query script: touching, hitting at random distances, owners; ReportHit's
    nearer / farther rule emulated, the callback called), the scenes' grids (none, 0-3, an object in two), a
    cache holding or not holding the line's box (entries without a box), the movables registered and
    unregistered (twice-registered, the first, an unknown one, an empty list); 300 feeler queries (callback or
    not, its cache / the current one / none, ignore flags, movables, nearest / farthest, the three ways of setting
    the line, the object gone afterwards) and 100 direct queries on a record with and without a hit.
  - Mutations: 57 deliberate bugs, all caught (55 at first: unregistering the list's first node and the line's
    w added for 2).
- [x] Batch 188 (AN-17; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; a scene's collision grid; 11 functions): src/game/nx/coll_grid.h / .cpp
  (new); coll_world.cpp and coll_cache.cpp call the grid directly; CCollObj's +0x0A renamed m_visitStamp (it's the
  grid queries' mark, not a terrain; coll_obj.h / .cpp, coll_movable.cpp, coll_static.cpp, two tests).
  - Nx::CCollGrid (our name; 0x4B30 bytes: the box, the cell width / depth, 20 x 20, the cells [x][z]) and
    Nx::CollGridCell (0x30: box, +0x24 count, +0x28 a block, +0x2C the list): CollGridCell::Construct 0x004183B0
    / Destruct 0x00418410; CCollGrid::Init 0x00418440 (the corners summed in double precision across the cells),
    Clear 0x004183D0, Fill 0x00418BD0 (two passes over the statics with faces - counts, Malloc'd lists, a
    top-down scratch block of fill counts; a float inverse cell width but a double inverse depth; indices clamped
    to 19 only), Update 0x00418F60 (cell by cell: removed / added / moved lists; gaps filled from the end while
    objects were taken out, extras waiting on a pending list - Lst nodes 0x00645D7C - one replacing each taken out
    later; ReallocateShrink / ReallocateUp else Realloc; then every list moved to a fresh Malloc block),
    ObjectsInBox 0x00418710 (box grown 0.5 on x / z; double inverse width, float inverse depth; each object once
    by the stamp 0x00693998; no limit), ObjectsAlongLine 0x004188C0 (the line made 0.5 longer each way along its
    unit direction, rounded as the x87 code does; one cell: its objects as they are; more: each once, 2047 at
    most; the count out); the pending list's node / head destructors 0x00418ED0 / 0x00418EF0 / 0x00418F30.
  - Quirk kept: in Update a removal that finds nothing in a cell still counts, and a later addition then writes
    before the cell's list (the game's lists are consistent, so it doesn't happen there).
  - Tests (selftests_anim15.cpp): TestAnimCollGrid - 40 random bounds (sizes not exact in floats) through Init;
    40 stand-in statics (a fake slot 21, flags 2 / 4, some without faces, some boxes at cell edges, some where the
    float and double inverse cell widths truncate differently - found by trying the edges' neighbouring floats)
    through Fill; 600 box and line queries (clamped, at edges +- 0.5 and nudges, shorter than a unit, zero length,
    single cell, the count wanted or not); 16 updates with consistent added / removed / moved lists (in-place
    resizing working or not); a line over 2400 objects in 7 cells (the 2047 limit); Clear; the cell and node
    pieces. A bump allocator records every allocation; the cells, lists, calls and the arena compared.
  - Mutations: 45 deliberate bugs; 43 caught (33 at first: aligned boxes and lines, more rounds and additions,
    the limit reached, distinct heaps, rounding-sensitive values added for 10). Not caught: two roundings of the
    line's unit direction in ObjectsAlongLine (float vs double inverse length, the x part rounded) - they change
    the 0.5-unit extension below a float's resolution at the line's ends.
  - Also: the self-test runner's watchdog; a flaky "vector insert iterator out-of-range" DEBUG CRT REPORT seen
    once in a full offline run (THUG2PM.exe) and not again in four more - not traced to a test.
- [x] Batch 189 (AN-18; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the rect queries and the rect feeler; 14 functions): src/game/nx/coll_tests.h /
  .cpp (RectHitsTriangle), coll_rect.h / .cpp (new), coll_static.h, coll_movable.h, coll_obj.h.
  - Nx::RectHitsTriangle 0x0040D370 (a parallelogram - corner and two sides, Nx::CollRect - against a triangle:
    the triangle's edges cut by the rect's plane give P Q (each on an edge kind 1-3, crossing points rounded in the
    original's two different ways); in the plane the normal is largest across, each end inside all four sides a
    point, then each rect side crossing P Q (kind 0); true only with two points - one may be written and false
    returned, as in the original).
  - Nx::RectHit (0x60: two ends - point, the triangle edge, on-edge flag -, normal, checksum, flag 0x40) and its
    copies 0x00417AD0 / 0x00417B60; Nx::RectHits (0xC08: 32 hits, count, ignore / require flags).
  - CCollStatic::CollideWithRect 0x00410C90 (slot 15) and CCollStaticInstance::CollideWithRect 0x004128B0 (in the
    instance's space, its own box; points scaled and placed, normals turned back), CCollScaled::RectToLocal
    0x00410AA0, the in-place vector turn 0x00411840; CollideRectWithStatics 0x0040FEE0 (cache or the scenes' grids
    along the rect's diagonal; the grid's ObjectsInBox called and its answer dropped, as the original).
  - Nx::CRectFeeler (0x1844): Construct 0x004182D0, Query 0x00417840 (its cache or 0x0069198C), JoinHits 0x00417C00
    (duplicates dropped, pieces lying along each other joined end to end), DebugDraw 0x00417860 (compiled out).
  - Small ones next to them: CCollObj::SetChecksum 0x0040FEC0, CCollStatic::TakeBuffer 0x00410BC0.
  - Tests (selftests_anim16.cpp): TestAnimCollRectTriangle - 20000 random rect / triangle pairs (axis planes,
    degenerate rects, corners on / near the plane, NaN) and 6000 exact ones (a unit rect in z = 0 with corners at
    0, +-1e-5, ...; rects whose normal has two equal largest parts); TestAnimCollRectQueries - a 60-face mesh plain
    and compressed (with and without a tree), sector and instance (scaled, turned, placed), 1600 rects with flags
    and a nearly full / full list; rects into the instance's space; the world query through a cache and through
    stand-in grids; the rect feeler built, queried and 400 crafted hit lists joined; the copies and small pieces.
  - Mutations: 80 deliberate bugs; 72 caught (66 at first: exact plane distances, normal ties, a tree, the
    rejection scale added for 6). Not caught: rounding changes below a float's resolution in RectHitsTriangle
    (the order of a plane distance's sum, Q's side values as double or float, the second side direction test as
    float or double - rounding can't change its sign), the w part of a scaled point (TransformAsPos ignores it),
    of a kind-1 edge (raw vertices have w 0) and of a hit's direction (never read).
- [x] Batch 190 (AN-19; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the sphere queries and the sphere feeler; 9 functions): src/game/nx/coll_sphere.h /
  .cpp (new), coll_static.h.
  - Nx::SegmentCrossesSphere 0x0040E280 (0-2 crossings along a segment), SweptSphereHitsEdge 0x0040E560 (a moving
    sphere against a segment - touching already, else the capsule round it against the centre's path; the edge's
    nearest point then; the edge's own move is (0, 0, 0, 1) in practice: the point's w counts the time),
    SweptSphereHitsCorner 0x0040E940 (a corner and the edge from / to it the path heads for; a corner inside the
    sphere sets *t 0 and misses), SweptSphereHitsTriangle 0x0040EC90 (the face's unit normal out; a sphere apart
    from the plane classified by its centre against the three edges - the face if the move's capsule holds its
    nearest point, an edge, a corner; one within reach by where its front meets the plane - the face up to the
    limit, an edge, a corner's line test), SphereHitsTriangle 0x0040F9E0 (standing, from the front only).
  - Nx::SphereHit (0x2C) / SphereHits (0x5A4: 32 hits, the move's direction and sweep, count, flags, mode);
    CCollStatic::CollideWithSphere 0x004110B0 (slot 16 of sectors and instances alike: an instance's faces taken
    unplaced, as the original); CollideSphereWithStatics 0x00410240 (the sphere's box and the moved sphere's; cache
    or grids along the grown box's diagonal); Nx::CSphereFeeler (0xB60): Construct 0x00418330, Query 0x00418300 (mode
    1, its cache or 0x00691990).
  - The original's Line::DistanceSqrToLine / DistanceSqrToPoint (0x00402640 / 0x00402D60, an earlier batch's
    rewrites, through Capsule::IntersectsSegment / ContainsPoint) keep doubles where ours round to floats - the
    sphere tests let both runs use the original's there. Worth making exact some time (outside this range).
  - Tests (selftests_anim17.cpp): TestAnimCollSphereTests - 6000 random cases (segments through / grazing / missing
    spheres, moving spheres against still and moving edges, corners with their edges, triangles from both sides,
    touching or not, moving or not, limits; standing spheres; NaN) and exact ones (ends and grazes on the surface,
    a corner on the surface, a zero divisor deciding the edge, a flat triangle touched exactly / reached at the
    limit, no area); TestAnimCollSphereQueries - a mesh's sector (plain / compressed; flags; full lists; modes; sweeps
    0, positive, negative), the world query through a cache and stand-in grids, the feeler.
  - Mutations: 74 deliberate bugs; 67 caught (57 at first: the exact cases, moving edges, negative sweeps added for
    10). Not caught: equivalent ones (the capsule test always gives the edge parameter within 0..1; a triangle with
    no area misses either way), NaN-only differences (2), and roundings below a float's resolution (3).
- [x] Batch 191 (AN-20; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the capsule queries; 3 functions): src/game/nx/coll_capsule.h / .cpp
  (new), coll_tests.h / .cpp, coll_static.h; Mth::Line::DistanceSqrToLine now returns double.
  - Nx::SegmentTriangleDistanceSquared 0x0040BDC0 (cdecl; Eberly's segment / triangle regions: the 3x3 system by
    Cramer's rule; a crossing off the segment - the nearest edges and the nearer end (PointTriangleDistanceSquared);
    on it - the nearest edges; a (nearly) parallel segment - all three edges and both ends; some results rounded
    to float before they're compared or returned, some not, as the original; a segment through the interior prints
    "uh oh..." and returns 0; the second argument, the face's normal, isn't read).
  - Nx::CapsuleHitsTriangle 0x0040FC00 (the face's unit normal (v1 - v0) x (v2 - v0), false for a degenerate face;
    the nearest points; true when the segment is in front of the face and within the radius), Nx::CapsuleHit
    (0x3C: point, offset from the segment's nearest point, a time never written, normal, checksum, flag 0x40) /
    CapsuleHits (0x7A4), CCollStatic::CollideWithCapsule 0x004114E0 (slot 17 of sectors and instances alike; only
    with a zero sweep - a moving capsule finds nothing).
  - Line::DistanceSqrToLine returns double: the original leaves its result in ST0 unrounded and 0x0040BDC0 compares
    it before storing. Ours is still computed in floats (an earlier batch's; outside this range) - the tests let
    both runs use the original's.
  - Tests (selftests_anim18.cpp): TestAnimCollCapsuleTests - 8000 random segments against triangles (ends below /
    inside / beyond each edge and corner, above or below the plane, parallel, nearly parallel, crossing, zero length,
    no area, NaN) with capsules of random radii, and exact ones on a flat triangle (crossing it, on an edge, through
    a corner, along an edge, parallel, of no length; radii touching exactly); TestAnimCollCapsuleQueries - a mesh's
    sector (plain / compressed; flags; full lists; modes; sweeps 0, -0, positive, negative).
  - Mutations: 73 deliberate bugs; 66 caught (63 at first: exact crossings on the edges, an exactly touching radius
    and a negative sweep added). Not caught: ties between equal candidates (2), roundings that only matter at a
    float's resolution (4) and one equivalent (the parallel case's t is 0 before it's set).
- [x] Batch 192 (AN-21; anim agent, merged 2026-10-20; run in the game: docs/pending/anim.md; NOT judged by a player yet; the range's last functions; 6 functions): src/game/obj/ride_tracker.h /
  .cpp (new), mth/matrix.h / .cpp, nx/coll_static.h / .cpp, nx/feeler.h / .cpp.
  - Mth::Matrix::InvertOrthonormal 0x00416DF0 (in place: translation -(t . each row), the 3x3 transposed; the fourth
    column untouched; returns this).
  - Obj::CRideTracker (0xB0, our name; what the skater rides, +0x152C's +0x18 - core_physics.cpp): Construct
    0x00416E70 (a smart pointer to the object, its matrix and position, nothing carried, not turned), Update
    0x00416F00 (the object's move since the last update, plus - when its orientation changed - the point turned
    about the old position by new x inverse(old) (D3DXMatrixMultiply, the library's, through its address); false
    when the object is gone).
  - Nx::CCollStatic::Construct 0x00412730 (hidden: the element constructor `new CCollStatic[n]` passes to the
    array iterator at 0x0049CC88; flags, geometry, buffer, +0x14 cleared; checksum and grid mark left).
  - Nx::CFeeler::GetHitVertexValue 0x004174A0 (the hit face's per-vertex bytes - geometry +0x38, / 255 - blended at
    the hit point: the line from the first corner through it meets the opposite edge's (ClosestPointsBetweenLines,
    unclamped), the edge's two bytes blended there, then towards the first corner; 0.5 without a hit; returned
    unrounded). What the bytes are isn't known yet (7 callers).
  - Mth::ClosestPointsBetweenLines (0x00402F00, an earlier batch's) isn't exact against the original: the feeler
    test lets both runs use the original's. Worth making exact some time (outside this range).
  - Tests (selftests_anim19.cpp): TestAnimMatrixInvertOrthonormal (3000 rotations / arbitrary matrices / NaN; the
    element constructor on dirty memory), TestAnimRideTracker (three trackers, 2000 updates: moves only, turns,
    small turns, w's, a vanished object), TestAnimFeelerHitVertexValue (2000 hits on plain / compressed faces:
    inside, on edges, off the face; no hit).
  - Mutations: 33 deliberate bugs; 31 caught. Not caught: two equivalent (the turned offset's w and the corners'
    w never reach a result).
- [x] The range 0x00406460-0x0041A6D0 is done: find_gaps lists nothing in it, the scratchpad survey has no function
  left without source, and the hidden runs between Ghidra's functions are all covered (the rest are jump tables,
  "RLD!" strings and the continuations of covered functions past a 0xCC byte inside them).
- [x] Batch 193 (SC-1; editor scale agent, merged 2026-10-21; run in the game: docs/pending/editor-scale2.md; NOT judged by a player yet; NOT run in the game yet): **the collision component and the matrix component** (src/game/obj/
  collision_component.{h,cpp}, matrix_component.{h,cpp}; 12 functions).
  - `Obj::CCollisionComponent` (type 0xA432DC41, vtable 0x00645EEC, 0x1C bytes): 0x0041C920 Create (Calloc 0x1C, the
    base's constructor, vtable, type), 0x0041C710 DestroyBody (UnregisterMovable 0x00417800, the collision's slot 0
    with 1, the base's), 0x0041C780 Destroy, 0x0041C7B0 InitFromStructure (CollisionMode -> ShapeTypeFromChecksum, none:
    return; Class LevelObject: Name asserted, GetSector 0x0048C510, its collision 0x005480F0's geometry; then slot 13
    virtually), 0x0041C890 InitCollision (slot 13, RET 8: CreateMovableCollision from the geometry (count 1) or the
    model's asset data (+0x88 count, +0x8C geometries; the model component not null-checked - a crash without one, as
    the original) or none), 0x0041C980 Update (slot 1: the bone matrix 0x00493040 of the model component's model -
    position = its translation + the object's position (not turned, w the object's), orientation = D3DX multiply
    (bone, display matrix); else the object's position and display matrix; slots 5 / 6 of the collision object).
    Quirk kept: InitFromStructure passes the Name slot's old value when Name is missing (ours: 0).
  - `Obj::CMatrixComponent` (type 0x208353AC, vtable 0x00645F24, 0x58 bytes - cutscene_update.cpp's kSoundComponent):
    0x0041CB20 Create, 0x0041CAD0 Construct (identity matrix), 0x0041CA60 DestroyBody, 0x0041CA70 Destroy, 0x0041CAA0
    Update (object's display matrix = its matrix), 0x0041CAC0 SetMatrix. (Closes the gap find_gaps showed between
    0x0041C980 and 0x0041CB80.)
  - Tests: TestCollisionComponent (16 seeds: made / out of memory, InitFromStructure every branch, InitCollision
    geometry / asset / none, Update none / no model / no bone / bone, DestroyBody / Destroy with and without collision,
    flags 0 / 1 / 2 / 3) and TestMatrixComponent (4 seeds), original vs ours with recording stand-ins. 30 mutations
    (out\scripts\mut_component.ts, mut_matrix.ts), 30 caught (2 after strengthening: free flags 2, the vtable before
    the destructor).
  - Scans: scan_register_assumptions (only the known 0x4EFF92 and the existing InitEditedGoalStub lines), audit (no
    MISMATCH), find_gaps (0x0041CA60-0x0041CB20 were listed: now done), check_code_writes "No overlaps".
  - Switch off: `[replacements] disable=collision_component.cpp,matrix_component.cpp`.
- [x] Batch 194 (EC1-1; engine commands agent 1, merged 2026-10-21; run in the game: docs/pending/engine-cmds-1.md; NOT judged by a player yet; lens flare, terrain, sun, gamma, fog, dynamic lighting, UV wibble and
  texture animation commands, 21 functions): src/game/script/cfuncs_lighting.cpp (new).
  - 0x00445B50 HideLensFlare / 0x00445B70 UnhideLensFlare (0x00442DF0 / 0x00443030 with the same arguments),
    0x00445B90 GetTerrainTypeParam (param=<name>, terrain=<checksum> -> 0x004442C0, or a number (name unknown,
    0x75D274BE) taken as a byte; 0x00444480 fills a structure added to the script's params by AddStructure; false
    if there's none), 0x00445C50 SetSunProps (theta / phi / radius / size floats, texture / corona_texture
    checksums, enable / disable: a 0x44-byte block and the mask of those given to 0x004426D0 - mask 0x10, a colour
    at +0x18, is never set by the command), 0x00445D50 SetProgressive (on > 0 -> 0x005B6090), 0x00445D90
    SetColorBufferClear (0x0048C1B0 is a jump to the empty 0x005480A0: an empty inline SetColorBufferClear(bool)).
  - Gamma: 0x00445DD0 WriteGamma (RegCreateKeyExA Settings\Gamma with KEY_READ|KEY_WRITE, R / G / B written as
    4-byte REG_DWORD values holding the floats, nothing when the key can't be created) and 0x00445E60
    ApplyChangeGamma (color=red|green|blue change=<f>: the channel changes only upwards below 1 / downwards above 0;
    SetGamma 0x0048C130, then WriteGamma TWICE as the original does).
  - 0x00446000 SetDynamicLightModulationFactor (nothing without value; ambient -> 0x0048E970(value, value2);
    directional=<i> -> 0x0048E990(i, value, value2)), 0x004460B0 EnableFog / 0x004460D0 DisableFog (0x00490DE0;
    nothing while distance clipping 0x007D643A is on), 0x004460F0 SetFogExponent (false with distance clipping).
  - Sectors by name (0x0048C510): 0x00446130 VCWibblePlayFromKeyframe (false without sector=; true if the scene has
    none; time in ms), 0x004461D0 SetUVWibbleParams (eight floats, u_vel..v_phase), 0x004462D0 / 0x00446300
    Enable / DisableExplicitUVWibble, 0x00446330 SetUVWibbleOffsets, 0x004463A0 ResetTextureAnim, 0x004463D0
    StartTextureAnim (reset flag), 0x00446420 StopTextureAnim, 0x00446450 ToggleRenderMode (0x0048C0D0). The sector
    methods (0x0049E820-0x0049E880, thiscall, forwarding to the sector's +0x14) are called on a null sector too, as
    the original does (they read +0x14 of it).
  - Quirks kept / noted: values a command reads without a default are uninitialised in the original (our zeros
    are only seen when the script leaves them out).
  - Test (selftests_engcmd1.cpp TestEngineCommandsLighting): every command, original vs ours, all called game
    functions and RegCreateKeyExA / RegSetValueExA / RegCloseKey stood in for by recording code (the registry is never
    touched); each branch: terrain by checksum / number (a number over 255) / found or not, the sun fields one at a
    time and both flags, the gamma over 5 start triples (including -0.0 and NaN) x 4 colours x 3 changes with the
    key failing for some, fog with distance clipping off / on, sectors missing from the scene. 40 deliberate bugs
    caught, 1 (change > 0 vs >= 0) after adding the -0.0 gamma start.
- [x] Batch 195 (EC1-2; engine commands agent 1, merged 2026-10-21; run in the game: docs/pending/engine-cmds-1.md; NOT judged by a player yet; render debugging, the animation system, the light manager, the screen,
  texture splats, gamma / light values, clipping and fog, render modes, bone positions, 26 functions):
  src/game/script/cfuncs_render.cpp (new); a fix in src/game/mth/vector.cpp.
  - 0x00446460 DebugRenderIgnore (0x0048C110(ignore_1, ignore_0)), 0x004464C0 InitAnimCompressTable ("<file>"
    copied with "%s"; t48 -> 0x00482A30, q48 -> 0x004829F0, other types nothing), 0x00446570 InitAnimSystem
    (AnimHeapSize, a second size (name unknown), DefCacheType -> 2 standard / 3 / 4 (two unnamed types) / 0 else for
    0x00480D10; two counts (one unnamed, MaxAnimSubsets) for 0x0047EB30), 0x00446650 UpdateAnimCache (enable /
    disable do nothing - compiled out; clear -> cache 0x00480DB0(0), defrag -> 0x00480F20, neither -> the cache's
    getter and a compiled-out report), 0x004466F0 FinishRendering and 0x00446810 AllowBoneLOD (0x0048C1B0 = the
    empty function: empty inlines), 0x00446700 InitLightManager (max_lights, an unnamed count), 0x00446760
    UninitLightManager, 0x00446770 PushWorldLights, 0x00446780 PopWorldLights (a 5-byte jump to 0x0048EA40: its
    bool), 0x00446790 SetScreen (aspect -> Nx::SetScreenAspectRatio, angle -> 0x004A0B70, letterbox ->
    Nx::SetLetterbox(n != 0)), 0x00446850 DebugLine (compiled out: Gfx::AddDebugLine), 0x004468E0
    GetPausePosByResolution (pos=(50 at a 640-wide back buffer / 20, 240)).
  - 0x00446930 KillAllTextureSplats: box={height dist width offset} - the box's corners at -/+ half of each size in
    inches around the BOX's offset (read from the box structure, not the params; x/y/z of a missing offset are
    uninitialised in the original); the low corner not above 0, the high not below 0; x87: the low half from the
    unrounded product, the high one from the product rounded to a float (they differ past FLT_MAX); w = offset.w + 1
    (1 without an offset); 0x00491630. texture= -> 0x004916F0, nothing -> 0x004915A0.
  - 0x00446C20 GetGammaValues (red / green / blue removed then added as truncated percentages - the product in
    double: 0.29 gives 28), 0x00446CD0 GetDynamicLightModulationFactor (ambient: 0x0048E9B0(1), (0); directional:
    0x0048E9D0(i, 1), (i, 0) - both read the same array in the original; mod_value_lo / mod_value_hi),
    0x00446D70 SetClippingDistances (both viewport cameras' +0xB8 / +0xBC; the defaults are swapped in the original:
    Near defaults to 0x006487D4 = 12000 and Far to 0x006487D0 = 12), 0x00446DF0 SetFogDistance (false with distance
    clipping; both cameras looked up and dropped), 0x00446E50 SetFogColor (r g b a 255/255/255/128; with distance
    clipping: 0x0078733C = 0xAARRGGBB and FALSE; else 0x00490E20(bytes r g b a)), 0x00446F40 SetRenderMode
    (0x006FA734), 0x00446F70 SetWireframeMode (0x006FA730), 0x00446FA0 SetLightAmbientColor and 0x004470A0
    SetLightDiffuseColor (alpha 0x80; they return the callee's AL), 0x00447010 GetLightDirection (0x0048E920's
    vector in degrees: heading = y, pitch = x), 0x00447130 GetLightCurrentColor (ambient_*, red_0.., red_1..),
    0x00447220 GetBonePosition (the skeleton component's 0x00432050; x / y / z added even without the object -
    uninitialised in the original).
  - Found and fixed: Vector::RadiansToDegrees (0x00405F20, rewritten earlier) used 57.2957795f = 0x42652EE1; the
    exe's constant (0x00645468) is 0x42652EE0 - one ulp lower. Its old self-test compares with a tolerance; this
    batch's test (GetLightDirection) compares bits.
  - Test (selftests_engcmd2.cpp TestEngineCommandsRender): every command, original vs ours, every callee stood in
    (the modulation getters run as the original over stand-in arrays; RadiansToDegrees / FeetToInches / the BBox
    constructor run as themselves); all 16 UpdateAnimCache flag combinations, each cache type, the splat box with
    positive / negative / -0 / NaN / 3e37 sizes and an offset with z -0, gamma fractions that round differently in
    float, distance clipping off / on, cameras missing, callee results passed back, bone found / not / no skeleton /
    no object. 45 deliberate bugs caught (6 after fixing the hand-built params' structure / array types, 3 more after
    an offset with -0, 1 after a fog alpha without bit 0, 1 after letterbox=1); 1 equivalent (0x0048E9D0's flag:
    both branches of the original read the same array).
- [x] Batch 196 (EC1-3; engine commands agent 1, merged 2026-10-21; run in the game: docs/pending/engine-cmds-1.md; NOT judged by a player yet; spawned polygons, the scenes' vertex colours, the light direction, metrics,
  8 functions): src/game/script/cfuncs_polys.cpp (new).
  - 0x004472D0 FreeSpawnedPolyDetails (the pool pair 0x00700500's Free - the details' operator delete),
    0x00447330 SpawnPoly (0xB8-byte details from the pool pair, constructed by 0x00491990 - zeroed, blend mode
    0x54628ED7; pos into +0, two vectors (names unknown; (0,0,1,1) / (0,1,0,1)), start_dims / end_dims pairs,
    lifetime, start_colors / end_colors arrays of 4 [r g b a] (per corner) or start_color / end_color (one for all
    corners) as bytes, an unnamed flag -> flags |= 2, just=[right|left|center, bottom|center|top] -> +0xB0 / +0xB4
    of 1 / -1 / 0, BlendMode, texture -> the texture dictionary 0x00700F20's 0x0049F540; then 0x00492450(details,
    &vector1, &vector2)). A failed allocation writes through null as in the original.
  - 0x00447710 CompressSceneVertexColours(scene, target, factor, lightGroup) / 0x00447980 CompressVC (target,
    percent -> factor (100 - percent) * 0.01 rounded once, LightGroup): each scene's sectors (the hash table at
    +0xD0, the sector's light group at +0xC, its geometry +0x14: 0x0048DF70 count, 0x0048E150 colours into a
    Mem::New buffer, 0x0048DFA0 back) - channels (c - target) * factor + target in double, limited to 0..255 and
    truncated, alpha kept, nothing when target and factor are both 0; skies (scene +0x8D) skipped in the list walk,
    then the first sky (0x0048C7B0) done. A chain item without a value ends a scene's walk (as in the original).
  - 0x00447A60 NudgeSceneVertexColours(scene, amount) / 0x00447D90 NudgeVC (percent * 0.01, every scene, skies
    too): per vertex a hash ((int(x*16) * int(y*16)) ^ int(z*16)) & 0x7FFF (positions 0x0048DF80) gives the scale
    float(h / 32768 * amount * 2 + 1 - amount); red and green products rounded to floats, blue's kept in double (as
    the x87 code does), each limited to 0..255 and truncated. The positions are freed before the colours.
  - 0x00447E00 SetLightDirection (index, direction=<vector> with w 0, or heading and pitch (both) turning (0, 0, 1):
    about x by pitch then about y by heading, in the x87's own FSIN / FCOS with the products against the
    extended-precision sine / cosine as the original keeps them (X87SinTimes / X87CosTimes) and the heading's
    sine / cosine stored as floats; returns 0x0048ED70's AL), 0x00447F30 GetMetrics (a new CStruct (struct pool,
    Init) filled by 0x0048CB00, added as Metrics, cleared and freed).
  - Test (selftests_engcmd3.cpp TestEngineCommandsPolys): stand-in heap (Manager::Allocate / Free), stand-in scenes
    (a ring through a scene -1 sentinel; two skies), hash tables with chains, an empty bucket with a chain, a chain
    item without a value, a sector without geometry, light groups; geometry stand-ins; SpawnPoly with per-corner /
    single colours / neither, each `just` value, texture or not; CompressVC over light groups, NaN target, percent
    above 100, a factor that differs when rounded twice; NudgeVC with a vertex hashing to 0 whose channels show the
    float / double rounding; SetLightDirection over angles and missing heading / pitch, both callee results;
    GetMetrics with the allocation failing; the details freed to the heap and into a stand-in pool. 41 deliberate
    bugs caught (7 after making the scene list reachable, 1 after adding a pool, 4 after the rounding cases);
    1 equivalent (IterateNext never returns true with no item).
- [x] Batch 197 (EC1-4; engine commands agent 1, merged 2026-10-21; run in the game: docs/pending/engine-cmds-1.md; NOT judged by a player yet; the memory manager's commands, the pool-usage dump, DumpHeaps, 15
  functions): src/game/script/cfuncs_memory.cpp (new).
  - Compiled-out commands with their arguments kept (empty inlines / Mem::PushMemProfile): 0x00447FF0
    PushMemProfile, 0x00448020 PopMemProfile, 0x00448030 SetMemThreadSafe (off), 0x00448060 DumpFragments (<size>
    0x3C00, K), 0x00448270 AnalyzeHeap and 0x004482C0 PrintMemInfo (a heap checksum, BottomUpHeap 0x0C7800B0 by
    default; params may be null; the heap looked up, PrintMemInfo copies its name). 0x00448090 MemViewToggle
    (Nx::ToggleMemView), 0x004480A0 GotExtraMemory (0x007CEFAC != 0).
  - 0x004480B0 MemPushContext (an integer: 0 = the bottom-up heap, any other number does nothing; else a heap
    name -> Manager::GetHeap), 0x00448130 MemPopContext, 0x00448140 MemInitHeap (name="<text>" size= ->
    InitNamedHeap(crc, size, text, 0)), 0x004481A0 MemDeleteHeap / 0x00448200 MemHeapExists (name as a string or a
    checksum; they return DeleteNamedHeap's result / GetNamedHeap != null).
  - 0x00448330 DumpPoolUsage (naked DumpPoolUsageStub: "to the dump file" in BL, "print" on the stack, caller pops):
    each line sprintf'd from the exe's own format texts, to Mem::DumpText and / or Dbg::Printf (the line as the
    format, as the original); the current pool of the pairs CStruct 0x006F3614, CComponent 0x006F2338,
    CSymbolTableEntry 0x006F362C, CVector 0x006F3654, CPair 0x006F3644, CArray 0x006F22B4, CScript 0x006F3548, the
    hash items (PoolManager::GetPool(0)), CAnimCachePredictor 0x006F92B8, CBlendChannel 0x006F92C8,
    CBonedAnimController 0x00701430, CAnimLookup 0x006FA7F4, sSpawnedPolyDetails 0x00700500,
    Obj::CEventReceiverList 0x006F0FC8 (used, count, peak; the pools aren't null-checked, as in the original).
  - 0x00448840 DumpHeaps (heap.cpp's DumpHeaps calls it with null params): current heap, then per heap (FirstHeap /
    NextHeap) name, used size, free size ("Frag"), region MemAvailable ("Free"), the region's minimum (read before
    MemAvailable, which can lower it), used blocks - in K (signed / 1024) or in bytes with an unnamed flag
    (0xD65B9AC7); the sound manager's free memory (0x0047AE10 on 0x006F85A8 - 0 in this build - called twice, the
    second divided); then the pool dump (print only).
  - Test (selftests_engcmd4.cpp TestEngineCommandsMemory): a stand-in manager (its methods stood in for, ours and
    the game's), three heaps with negative / odd sizes for the K rounding, a MemAvailable that lowers the minimum,
    13 pool pairs with the current pool alternating (the other one a different pool), every command with null / given
    params, named heaps there or not, the dump through its register convention with all four flag pairs (BL's upper
    bits dirty), DumpHeaps in K / bytes / null params / no heaps. 29 deliberate bugs caught (1 after giving the pairs'
    other pool).
- [x] Batch 198 (EC2-1; engine commands agent 2, merged 2026-10-21; run in the game: docs/pending/engine-cmds-2.md; NOT judged by a player yet; maths, DisplayFreeMem, Test): src/game/script/math_cfuncs.{h,cpp},
  src/game/script/debug_cfuncs.{h,cpp} (new).
  - Sin / Cos / Tan 0x00448AE0-0x00448B60 (degrees: the float times pi/180 unrounded into FSIN / FCOS / FPTAN - kept
    as x87 inline asm so the results match bit for bit), ASin / ACos 0x00448BA0 / 0x00448BE0 (the C runtime's asin /
    acos, times 180/pi), ATan 0x00448C20 (FPATAN with 1), Sqrt 0x00448C60; results go to the calling script's
    parameters as sin / cos / tan / asin / acos / atan / sqrt.
  - NormalizeVector 0x00448CA0 (norm = the vector times 1/length - all four components, w too; length summed from z
    down in double, rounded to a float; a zero vector left alone), LinearMap 0x00448D70 (result=<name>: the
    fraction (value - lowerbound) / (upperbound - lowerbound) stays unrounded, clamped to 0..1, mapped onto From..To),
    Clamp 0x00448E60 (max first, then min: min wins when min > max; NaN passes through), RotateVector 0x00448EF0 (the
    identity times Rx, Ry, Rz in front - D3DXMatrixMultiply(&m, R, &m) - then the point (w = 1) through it).
  - DisplayFreeMem 0x004489B0: for each heap, UpdateDisplayFreeMemory id=<CRC of the heap's name>
    free_mem=<its region's MemAvailable> min_free_mem=<the region's least>; one pooled structure, cleared each time.
    Test 0x00449110: prints the bytes of a hand-made symbol table entry (three times, flags 1 and 2 set in
    between; flag 4 then cleared, unprinted) and of a component.
  - Checksum names found from the name entries in the game's .qb files; LinearMap's input (0x08296457)
    stays unnamed.
  - Test TestMathCFuncs (src/hook/selftests_ec2.cpp): every command over several inputs (out-of-range, NaN, zero
    vectors, reversed / empty LinearMap ranges, cancelling LinearMap ranges, Clamp's NaN cases, each rotation alone
    and all three), DisplayFreeMem over two heaps, with the pool giving a structure and empty; the structure changes,
    heap iteration, pool, RunScript and Printf recorded; _CIasin / _CIacos answered by std::asin / std::acos in both
    runs. 37 deliberate bugs: 36 caught (2 after strengthening: LinearMap's upper clamp and its unrounded fraction; the
    rotation-order bug moved from the X rotation, where it is equivalent - the identity commutes - to the Y one);
    1 equivalent (RotateVector's w: rotations have no translation row).
- [x] Batch 199 (EC2-2; engine commands agent 2, merged 2026-10-21; run in the game: docs/pending/engine-cmds-2.md; NOT judged by a player yet; nodes, composite objects, .qb files, languages, proximity, ResetEngine):
  src/game/obj/object_cfuncs.{h,cpp} (new), src/game/script/debug_cfuncs.{h,cpp}.
  - Nodes: AppendNodeParams 0x00449280 (params={...} appended to the node's own structure - GetNode asserting),
    GetNode 0x00449850 (node = the node's structure; false if none), GetNodeName 0x00449890 (nodename = the id of
    the script's object; false with no object or id 0), GetTriggerNodeName 0x004498C0 (nodename = the script's
    m_node; false if 0).
  - The COIM heap: InitCOIM 0x004492E0 (Obj::InitCoim(0xBB9A31B5, size, 0xB0847D54, the float 0x03D6DE47 squared
    and truncated - the square unrounded), ResizeCOIMHeap 0x00449380 (newsize).
  - Composite objects: CreateCompositeObject 0x00449540 (Components=[...], params={...} optional; finalized),
    GetUniqueCompositeObjectID 0x004495A0 (UniqueID = the first free id from PreferredId, default 8 - the manager
    re-read each step), GetObjScreenCoords 0x00449610 (the first viewport's camera 0x004A06D0 projects the object's
    position, 0x004A1090; screen_x / screen_y; false with no object), RunScriptOnComponentType 0x004496B0 (vtable
    slot 12 - CallMemberFunction by name - on every component of the type), TransformLocalToWorld 0x00449740
    (world_offset = local_offset through the object's display matrix +0xC0; no null check),
    DestroyParticlesByGroupID 0x004497D0 (the renderer's particle manager 0x006FA7C0, if any, destroys the group
    - 0x0049ADE0 - and every live object whose particle component's system (+0x1C, its +0x0C) has the group is
    marked dead, vtable slot 6).
  - LoadQB 0x004493B0 / UnloadQB 0x00449450 (the LevelSpecific table 0x00696900, 15 checksums: noted in the first
    free slot - all taken: not noted, loaded anyway; unloading clears every matching slot), ScreenShot 0x004494A0
    (FileName, default "screen"; 0x0048C100), ToggleMetrics 0x004494E0 (0x0048C0F0 toggles the byte 0x006FA7D6),
    German / French / Spanish / Italian / English 0x004494F0-0x00449530 (the language 0x007CEFA0 is 5 / 3 / 4 / 6 /
    2), ProximCleanup 0x004498F0 (sound components' +0xBC and stream components' +0x28 cleared, then
    Proxim_Cleanup), Inside 0x00449950 (the proximity manager's m_entering - no null check), ResetEngine
    0x00449960 (Mlp::Manager::StopLoop).
  - Test TestObjectCFuncs: every command and branch (missing objects / nodes, dead objects, the other group, no
    particle manager, the .qb table free / full / two matching slots, all five languages over eight values, the
    proximity flag both ways), with the object and component lists, the projection (a fastcall stand-in leaving its
    result in ST0), the structure changes and the file / screenshot calls recorded; the components, objects and
    main loop manager compared after. 43 deliberate bugs, all caught (one InitCOIM input changed so that rounding
    instead of truncating shows).
  - The verify build is configured with CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded (/Z7) in this worktree: the
    shared compile PDB kept getting corrupted during mutation runs (C1033 / C2471).
- [x] Batch 200 (EC2-3; engine commands agent 2, merged 2026-10-21; run in the game: docs/pending/engine-cmds-2.md; NOT judged by a player yet; RunScriptOnObject, Orient, Change, SetEventHandlerOnObject, Wait, the
  comparisons, SetScriptString, GlobalExists): src/game/obj/object_cfuncs.{h,cpp}, src/game/script/script_cfuncs.{h,cpp}
  (new).
  - RunScriptOnObject 0x00449A10: UniqueID removed from the caller's parameters; the object (Obj::FindObject, the
    tracker) gets its own script's object set if it has none (0x0041D1D0 - CSmtPtr::Set) and runs the script as an
    interrupt (CObject::CallScript); false with no object.
  - Orient 0x00449AC0: angles (Angles, default (0,0,0,1); `degrees` scales all four) to the object's m_matrix
    (0x0041A730), offset as its position row; with bone, times the bone's matrix from RelativeTo's skeleton
    component (0x005480F0 its skeleton, 0x004A36E0 the bone's matrix; no RelativeTo: crashes, as in the original);
    with RelativeTo, times that object's display matrix with its position; then m_pos = the position row, the row
    reset, the display matrix = the matrix. No check that the object exists.
  - SetEventHandlerOnObject 0x00449F80: ex / scr / Group (default "default") / Exception / params; the object's
    script made from the script pool (CScript::operator new + Construct) if it has none; CScript::SetEventHandler.
  - Change 0x00449D00: <name>=<value> (the first component) or globalname(guess)=<name> newValue=<value>; scripts and C
    / member functions refused; the global removed (CleanUpAndRemoveSymbol) and made again (its +0x08 kept); the
    value copied into a fresh component (its flag bit kept); a name with `resolve` takes the global it names (type and
    value); integers, floats and names stored, structures copied into a new pooled structure, arrays into a new
    array (CopyArray); any other type (strings, vectors, pairs...) leaves the global made again but EMPTY - kept.
  - Wait 0x0044A0D0: seconds / second (x 1000) and frame / frames (x 16.666666) as ms of frame time (WaitTime) or,
    with the flag 0xC847F2B9 (name unknown), the game clock (WaitRealTime); gameframe / gameframes / game: Updates
    (Wait); one_per_frame: WaitOnePerFrame; a bare number: ms. Each truncated (_ftol).
  - IntegerEquals / ChecksumEquals 0x0044A200 / 0x0044A260 (A, B; missing 0), StringEquals 0x0044A2C0 (by checksum:
    case-insensitive), SetScriptString 0x0044A320 (index 0, string ""), GlobalExists 0x0044A380 (name; type names
    Structure / struct, checksum / name, float, array, string, vector, Integer / int, pair; none: type 0, never true).
  - Test TestScriptCFuncs: RunScriptOnObject over an object with no script, a script without / with an object, no
    object and no params; Orient (defaults, degrees with an offset, RelativeTo, a bone, RelativeTo missing);
    SetEventHandlerOnObject with every argument and with the script made; Change over every value type, resolve
    (resolved, structure, unresolvable), refused types, a string global, globalname / newValue, nothing; Wait over
    every unit with and without the real-time flag, an integer, a negative; the comparisons both ways and with missing
    arguments; GlobalExists over 8 symbol types x 13 type names. The symbol table, pools, smart pointers, the
    skeleton and the waits recorded; the objects and new symbols / structures / arrays compared after. 43 deliberate
    bugs: 42 caught, 1 equivalent (Orient's w angle: 0x0041A730 reads x, y, z only).
- [x] Batch 201 (EC2-4; engine commands agent 2, merged 2026-10-21; run in the game: docs/pending/engine-cmds-2.md; NOT judged by a player yet; FormatText's worker, EnableScriptPrintf, IfDebugOn, StructureContains,
  ClearStruct, ScriptExists, IsArray, Block, IsTrue, CleanUpAndRemoveSymbol): src/game/script/script_cfuncs.{h,cpp}.
  - FormatText 0x0044A480 (cdecl (results, params) -> int; called from 0x0044BE2D and 0x0044C8F5 in the next third):
    the unnamed format string; "\%" a literal '%'; "%x" (one character) or "%%name" ([A-Za-z0-9_]*) names a
    parameter, found with FindLastComponent (the last one wins), copied into the global value component 0x00696D2C
    (its flag bit kept) and ResolveNameComponent'd; integers "%d" / integer_width "%0*d" / UseCommas
    (Str::FormatWithSeparators), floats "%.<decimalplaces, 3>f", strings and local strings copied, pairs
    "(%.3f,%.3f)", vectors "(%.3f,%.3f,%.3f)", names their text (FindChecksumNameOrUnknown; result 2); other types
    skipped. The text (buffer 0x00696940, unbounded as in the original) goes into textname / ChecksumName (its CRC).
    0 (printed) with no format string or a missing parameter. sprintf -> snprintf with the buffer's size (the same text
    unless the original would have overflowed it).
  - EnableScriptPrintf 0x0044A900 (default 1; the flag 0x0067D6D4), IfDebugOn 0x0044A930 (IfDebugOn and IfDebugOff:
    a compiled-out call with the script - kept as an empty inline - then true), StructureContains 0x0044A940
    (Structure={...} <name>: a component or a flag of that name; false with no structure), ClearStruct 0x0044A9B0
    (struct=<global>: Script::GetStructure(...)->Clear()), ScriptExists 0x0044A9E0 (LookUpSymbol is a script), IsArray
    0x0044AA20, Block 0x0044AA50 (type, else UntilEvent - only if params aren't empty; CScript::Block), IsTrue
    0x0044AAB0 (non-zero integer), CleanUpAndRemoveSymbol 0x0044AAE0 (FindSymbol; false if none).
  - Test TestFormatCFuncs: FormatText over no format, every value type, integer_width, UseCommas, decimal places
    (given and default), one-letter and long names (digits, underscores), the escape and a lone backslash, a
    structure (skipped), a missing parameter, an empty format, a repeated parameter, the value's flag bit set, a name
    that resolves to an integer; the game's sprintf stood in by vsnprintf for the original; the buffer and value
    compared after each call. The other commands over each branch. 31 deliberate bugs, all caught.
- [x] Batch 202 (EC2-5; engine commands agent 2, merged 2026-10-21; run in the game: docs/pending/engine-cmds-2.md; NOT judged by a player yet; event handlers, stopping scripts, the warning level, integer casts,
  arrays, AddParams, RemoveComponent, GetScriptString): src/game/script/script_cfuncs.{h,cpp},
  src/game/script/array_cfuncs.{h,cpp} (new).
  - SetEventHandler 0x0044AB20 (ex, scr, Group - default "default" -, Exception, params), SetExceptionHandler
    0x0044ABD0 (SetExceptionHandler / SetException: the same, always an exception), ResetEventHandlersFromTable
    0x0044AC70 (the global array named, Group), ClearEventHandler 0x0044ACD0, ClearEventHandlerGroup 0x0044AD00
    (default "default"), OnExceptionRun / OnExitRun 0x0044AD30 / 0x0044AD60 (none: 0), PrintEventHandlerTable
    0x0044AD90, StopAllScripts 0x0044ADA0, FirstTimeThisIsCalled 0x0044ADB0 (the flag 0x0067D6D5),
    SetScriptWarningLevel 0x0044ADD0 (the level 0x006F3534: none 0 - the default -, 0xD6454EFA 1, 0x4E10B054 2 -
    "warn" / "assert" are guesses; other names leave it).
  - CastToInteger 0x0044AE30 / StringToInteger 0x0044AEB0 work on the CALLING script's parameter of the given name
    (float truncated / each character - '0', no checks; the string is read after its component was removed - freed
    memory, kept); false without a name or such a parameter.
  - ArrayContains 0x0044AF50 (integer, string - _stricmp against GetLocalString - and name arrays; the type read once,
    the size every pass), GetArraySize 0x0044B030 (Index1 / index2 / index3 lead into element arrays; array_size),
    Get2DArrayData / Get3DArrayData / GetNDArrayData 0x0044B0F0 / 0x0044B300 / 0x0044B530 (arrayName: the array in the
    calling script's parameters, else the global one; ND reads `indices` from the script's parameters too; val added
    by the element array's type: integer, float, string / local string - both through GetLocalString -, pair, vector,
    structure, array, name; other types nothing; an empty indices array runs off the end, as in the original).
  - AddParams 0x0044B720 (AppendStructureIfAny), RemoveComponent 0x0044B740 (name=, else the unnamed name asserted),
    GetScriptString 0x0044B790 (index -> string).
  - Test TestArrayCFuncs: every command and branch; hand-built arrays of every element type, arrays of arrays two and
    three deep, single-element (in place) and empty arrays, the script's own parameters for the casts and array
    lookups, global arrays, a script-typed row (nothing added), the warning level over all names; the game's
    _stricmp answered by ours. 40 deliberate bugs, all caught (3 by crashes in ours).
- After EC2-5 the range 0x004489B0-0x0044B7E0 is done: inspect survey shows every function REPLACED, find_gaps 0 gaps.
- [ ] Next: the anim range is finished; in-game testing of AN-15 ... AN-21 (and the earlier batches marked NOT run).
- [ ] Next: the range is done (find_gaps.py: nothing left in 0x45EFD0-0x47DC40); a final game run.
- [ ] Next: nothing left in the range; hand back.
- [ ] Next: check the network fix in the game (host a game, change to a ported level of each game). Small net
      follow-ups: App::m_manager (+0x1440) could become `Manager*` (it is `void*`), name the Manager's +0x2E6 flag
      (pref checksum 0xE5ADE642) and check +0x374 (modem baud rate?). Widescreen follow-up: if the user still sees
      the bottom-left band overlap, don't draw sprites wholly outside the layout in the widescreen front end.
      Then the gaps left (checked 2026-10-19; done since: Obj_GetCollision 0x45E9E0, 0x4172F0, 0x4AAD40, the screen
      element code, Mdl::Manager, the 0x4B2270 glue): the collision feeler 0x417760; the cutscene system
      0x463AA0-0x4680B0 (39 of 42 left: 0x463AA0 quaternion + translation to matrix, 0x463BA0, 0x463C70-0x464360,
      the pool pair 0x4645A0 / 0x4645E0, 0x464640-0x4680B0: loading 0x466620, the details' slots 0 / 2 / 5 / 7 /
      8, the big updates 0x4648D0 / 0x465C00); the task list 0x406850-0x406E00 (24 functions, incl. Tsk::Task
      0x406890 / 0x406C90 -> 0x406A80 and the list run 0x406AC0 the frame order rests on);
      CFlipRotateComponent::Update 0x4E2D20 and its base 0x481710-0x481740; 0x46C470-0x46C6A0 (a CStruct wrapper
      class, 12 functions); the sound functions 0x47AC10-0x47AD80 (5). The rest of GameNet::Manager
      (0x00533F20-0x0054EF60, 3%). GameSpy's host names become configuration when the SDK is linked.
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
  IsPS2 table entry - `node tools/check_code_writes.ts`.)
- Member functions are replaced via `REPLACE(addr, &Class::Method)`; constructors via a small adapter that
  placement-news the object (see `LineAdapter` in vector.cpp).

## PartyMod (2026-10-15)
- [x] Step 1 done, TESTED IN-GAME 2026-10-15 (game runs with PartyMod's DLL removed; log shows both PartyMod lines and all self-tests passing): PartyMod (v1.0.4) in its own folder (MIT, notice kept), built as a static lib with SDL2 2.30.9 fetched by CMake; our DllMain calls its attach function and redirects the exe's LoadLibraryA of its DLL to our own name. Builds cleanly.
- [x] Step 2 done (2026-10-16/17): every PartyMod-patched game function rewritten with its behaviour built in
  (batches 54-69), the byte patches dropped, then PartyMod's folder folded into the game's source and deleted (see
  "PartyMod no longer a separate thing" above). Left: GameSpy's host names become SDK configuration when it's linked.
