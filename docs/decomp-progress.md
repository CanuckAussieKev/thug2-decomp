# Decompilation progress

Restart rule: continue the first batch not marked done. A batch is done when it builds cleanly and is committed
(one commit per batch). In-game testing happens later; a batch that breaks the game gets reverted by its commit.
Batches marked (untested) haven't been run in-game yet.

**2026-10-16 (user):** the end goal is to compile the game itself, not only to inject a DLL - so no game
function is skipped. Functions noted below as "left as original", "too small" or "PartyMod-patched, keep
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
      through pipLoadWrapper (guarded for when PartyMod's patch list isn't set up). check_partymod_overlap.py
      only looked at a function's first 16 bytes; it now also fails on any PartyMod patch inside a rewritten
      function's body unless the patch is listed with how our source does it (3 so far), skips commented-out
      patches and reads "addr - n". New tools/ghidra/partymod_patches.py lists PartyMod's patches by game
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
            without a card. 17 deliberate bugs, all caught. Register scan, call audit and PartyMod overlap check
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

- [ ] Next: the pre-compiled .qb format (0x46FBA0, 0x46CA50, 0x46C970, 0x46C830, 0x46C7F0, ...), the script
      cache (0x475FD0 ...), then CScript (0x472900.. / 0x473000..).
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
