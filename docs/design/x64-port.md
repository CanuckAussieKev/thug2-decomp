# A 64-bit (x64) build

Status: in progress. Done: X1 (the x64 presets and configure), X2 (`base/` on x64), X4 (the pointer-wide script cell), X6 (the layout asserts), X10a (FFmpeg's Bink video). The design was written against
the `stage11` branch (2026-11-05); its numbers come from scripts over `src/` and are approximate where this says so.
The "What the code assumes today" sections describe the code before the port.

## The goal

`thug2-decomp.exe` builds and runs as a 64-bit Windows program (MSVC x64; a GCC / MinGW-w64 x64 build later), from the
same source as the 32-bit build, on the player's own data files. The 32-bit build stays: it stays the reference that
the 64-bit build is compared with, and it stays the build for players who keep using the player's own `binkw32.dll`.

Why: the address space (large ported levels, THP8's town and bigger, high-resolution textures, the level editor's
undo history and caches), current toolchains and libraries, and ray tracing in the Vulkan renderer
(docs/design/vulkan.md), whose acceleration structures want the 64-bit address space.

What "functional equivalence" means for this build: the game plays the same. It loads the same files, runs the same
scripts in the same order, sends the same network messages and writes the same save files. Floating-point results
can differ in their last bits (see "Floating point" below). The 64-bit build does not promise bit-identical
trajectories with the 32-bit build. The project already accepts last-bit differences where plain C++ replaced the
original's x87 code (docs/style-guide.md).

## What the code assumes today

The 32-bit build is deliberate. The root `CMakeLists.txt` stops a non-x86 configure
(`message(FATAL_ERROR "Build with the x86 preset: the game is 32-bit.")`), and `base/core/pointer_cell.h` asserts
`sizeof(void*) == sizeof(uint32_t)`. The layering (docs/architecture.md, "The original's layouts stay in the
boundary") already puts most of what depends on the pointer size in a few named places. The x64 port is mostly the
work of finding and changing those places.

### Layout asserts

There are 357 `static_assert` statements in `src/` that name `sizeof` or `offsetof`, over 340 types. Each was put in
by the script below and classified by the worst type it names:

| class | asserts | types | what it means on x64 |
|---|---:|---:|---|
| record with a pointer (directly, through a member record, or a vtable) | 230 | 189 | the asserted size / offsets are 32-bit: they fail on x64 |
| record with no pointer found | 82 | 87 | should hold on x64 too |
| boundary format (`formats/`, `wire/`, `src/tests`, the QB loader, network message records) | 24 | 47 | must keep its 32-bit layout: these are bytes in files and on the wire |
| SDK / OS structure (DirectSound, Direct3D, GameSpy) | 6 | 8 | the SDK's size changes by itself; the constant beside it must follow |
| the pointer width itself (`sizeof(void*) == 4`, `sizeof(F) == sizeof(void*)`) | 4 | - | must be rewritten |
| unknown (templates, expressions) | 11 | 9 | to be read by hand |

By folder, the pointer-holding asserts are mostly in `engine/gfx` (47), `engine/objects` (27), `engine/script` (27),
`game/park_editing` (19), `engine/render` (14), `render_pc` / `gpu_d3d9` (about 40 over their files), `game/world` (8),
`engine/sys` (8), `engine/file` (7) and `engine/audio` (6).

Most of these records are the original's in-memory objects (heap blocks, pool items, singletons). The asserts only
prove that the 32-bit build lays them out as the original did. One file already shows the x64-safe form:
`game/park_editing/park.h` asserts `sizeof(void*) != 4 || (sizeof(...) == ...)`. The port applies that form everywhere
(task X6). The asserts that guard bytes the game reads in place stay unconditional, and those are the real work (next
section).

Since X6: such an assert reads `static_assert(core::layout_32bit(sizeof(T) == ...), ...)` (`base/core/layout_32bit.h`:
the condition on a 32-bit build, true on any other). The x64 compiler chose them, not the survey's classes: every
translation unit of `src/` compiled syntax-only (`/Zs`) by the x64 `cl.exe` with the x86 build's flags, and each
assert that failed there was wrapped - 259 of them (11 of the earlier `sizeof(void*) != 4 ||` form among them), all
the original's in-memory objects. The asserts that hold on x64 too stay unconditional. Still failing on x64, for
their tasks: the collision file's object record `TriMeshSource` (`engine/render/coll_tri.h`, used in place: X3 - it holds since X3, its arrays 32-bit links), the
script value `Component` / `Struct` and the expression stack (`engine/script`: X4), and `SOCKET`'s width
(`platform/gamespy.cpp`, `net_winsock`: X8). DirectSound's buffer description is the one SDK structure whose size
changes (0x24 / 0x28 bytes): its constants (`audio_dsound`, `engine/sound`) follow the pointer size.

### Pointers in 32-bit cells and in data used in place

Counted over 335,402 non-blank source lines, with comments stripped:

- **`cell::` (base/core/pointer_cell.h): 67 lines in 13 files.** `cell::of` / `cell::pointer` are the 32-bit
  pointer <-> integer conversions. Their users:
  - `engine/script/cell.h` (17): the script value. `script::Cell` is 32 bits and holds a `Struct*`, `Array*`,
    `Vector*`, `Pair*`, text, code or member-function entry pointer, or a name, integer or float. This is the biggest
    single item (task X4). The holders are pooled records of the original's sizes: a component is 0x10 bytes, a
    struct 8, an array 0xC, a symbol 0x14 (`engine/memory/memory_sizes.cpp`, which hard-codes these byte sizes).
    105 lines read or make a cell's raw word (`word()`, `of_word`), in 29 files.
  - `engine/script/token_reader.cpp`, `qb.cpp`: the QB loader rewrites a call's name token in the bytecode into a
    native function's or member function's address (`prepare_script_tokens`: `store_word32(name + 1,
    symbol->value().word())`), and the interpreter reads it back (`native_function_at`, `member_entry_at`). A
    token's operand is 4 bytes, so a pointer doesn't fit there on x64 (task X5).
  - `engine/script/precompiled.cpp`: precompiled script images are fixed up in place (`relocate_struct`,
    `relocate_component`, `relocate_array`). The image's records are the runtime's own `Struct` / `Component` /
    `Array`, with 32-bit pointers made from offsets (task X5).
  - `formats/collision_format.h` / `.cpp` and `engine/render/coll_tri.h`: a collision file's object records (0x40
    bytes each, `object_record<TriMeshSource>`) and its BSP tree are fixed up in place. Since X3 the fix-up writes
    32-bit links, not addresses: a record's array fields and a tree node's link word hold the distance in bytes from
    the record or node to what they link to (`collision_format::link_from` / `linked`; a record's arrays 0 for none:
    `record_link` / `record_array`; a node's `tree_children(node, link)` and `tree_leaf_faces(node, link)`), on both
    builds. Resolving one is an add (and a mask for the children), as the hot path needs. A tree copied as one block
    keeps its split nodes' links unchanged; a copied `TriMeshSource` makes its links from the copy (its copy
    constructor and assignment). On x64 a target more than 2 GB away stops the game (GSL's contract check): the
    file's sections are always within the file, and the replicated copies within the game's main block.
  - `formats/anim_format.h` / `.cpp`: the animation sections' addresses, kept in `uintptr_t` (x64-safe as they are),
    over files used in place. X3's survey: nothing is written into the file - the addresses go into the
    animation's own in-memory object (`JointClipFrames`, engine/gfx: X6's / X12's), as do the other formats'
    (scene, texture, skeleton, font, image, pre, save, park map, wave: read through `util::ByteReader` or viewed as
    bytes and characters). `wire/` reads and writes every message through the byte readers: no pointers.
  - `platform/crash.cpp` (10, the crash report: below), `platform/gamespy.cpp` (2: NAT negotiation's `int` user word
    holds a pointer), `platform/run_log.cpp`, `base/core/all_ones.h`, `gpu_d3d9/imgui_texture_id.h` (64-bit
    already) and `tests/format_check_files.cpp` (9).
- **`raw::` (raw_view.h): 42 lines** (platform 15, engine/script 8, render_pc / gpu_d3d9 8, formats 6). They read memory as
  another type. Most are layout-neutral. Each one has to be read.
- **`function_at` / `code_address`: 53 lines** (game/online 32: GameSpy callbacks registered as code addresses;
  platform 16). `platform/code_address.h` asserts that a function pointer has the size of `void*` and, in one place,
  of `uint32_t` ("a 32-bit build").
- **`uintptr_t` / `intptr_t`: 91 lines** (formats 42, tests 19, platform 16, base/core 9, engine/memory 3). Most
  are already width-correct.
- **`DWORD_PTR`: 2** (the loading console's timer callback): already correct.
- **`std::bit_cast`: 193 lines.** Almost all are float <-> bit conversions. The pointer ones are inside
  `pointer_cell.h` and `code_address.h`.
- **No `reinterpret_cast` in `src/`** (`.clang-tidy` keeps it out). Pointer <-> integer conversions go through the
  helpers above, so they can be found.
- **`static_cast<uint32_t / int>(... size() / sizeof ...)`: about 180 lines.** Sizes narrowed to 32 bits. They are
  harmless while no object reaches 4 GB, but each turns into a C4267 warning on x64. The fix is mechanical.
- **`long`: 142 lines.** It is 32 bits on Win64 too (LLP64), so it doesn't matter for an MSVC x64 build, only for a
  later LP64 (Linux GCC) build.
- **Pointer-sized SDK types fixed at 32 bits:** `platform/gamespy.cpp` asserts `sizeof(SOCKET) == sizeof(uint32_t)`.
  `SOCKET` is pointer-sized on Win64.

### Inline assembly and calling conventions

- **Inline assembly: 47 lines, all in `src/tests/maths_check.cpp`**, the x87 reference that the plain C++ maths was
  proven against (3 `__declspec(naked)` functions). It already compiles only under `_MSC_VER && _M_IX86`. MSVC x64
  has no inline assembly, so the check runs its reference half on the 32-bit build only (task X14).
- `__stdcall`: 38 lines (thread procedures, the Bink wrapper's exports). On x64 they are ignored, which is harmless.
- `_M_IX86` / `_M_X64` checks: 6 (`base/maths/view_transforms.cpp` already selects its SSE estimate path for x64).

### Floating point

- `_controlfp`: only in `src/tests/maths_check.cpp`, which runs at 53 and at 24 bits. Direct3D 9 sets 24 bits on
  the thread that makes the device, because the game makes it without `D3DCREATE_FPU_PRESERVE`
  (`gpu_d3d9/d3d9_context.cpp`).
- The game's own code never sets the precision. The 32-bit build compiles with MSVC's default `/arch:SSE2` (the
  CMake files set no `/arch`), so its `float` / `double` arithmetic is already SSE2 and doesn't depend on the x87
  control word. The x87 remains in these places in the 32-bit build:
  - the C runtime's maths functions, where the x86 CRT picks an x87 path (to confirm per function);
  - `float` return values in `ST(0)`. This is exact at 24 bits for floats;
  - `d3dx9_43.dll`'s own code. Only the backend calls it now: shader assembly and the JPEG.
- On x64, MSVC uses SSE2 only and the x64 CRT's `sin` / `cos` / `atan2` / `pow` / `sqrt` are different
  implementations. Results can differ from the 32-bit build in the last bit. Places to watch:
  - `pc_texture_projection.cpp`, `pc_weather.cpp`, `texture_splats.cpp`: "the maths follows the original's x87
    code: double precision where it keeps values in registers". These keep the original's mix of double and float,
    which carries over unchanged;
  - the physics thresholds (rail / lip / wall checks, the feelers): a value that lands on a threshold can tip the
    other way;
  - the QB numeric profile (`docs/qb-numeric-precision.md`: the gameplay profile is x87 precision 24) and RandomRange.
- What equivalence can mean here: the same results bit for bit where the code does integer, fixed-point or IEEE
  `+ - * /` work at the same widths (SSE2 on both builds), and the same results within a last-bit tolerance for the
  CRT's transcendentals. Task X14 turns `thug2_maths_check` into a two-build check: the 32-bit build writes a corpus
  of inputs and results, and the 64-bit build compares with it and lists every function that differs, with its
  largest difference in ulps. The accepted differences are recorded in docs/qb-numeric-precision.md and the style
  guide. No replay harness is proposed; play-testing confirms the feel.
- Online: messages carry positions and states quantised by the wire encoders (`wire/`). Peers simulate their own
  skaters and send states; they don't run in lockstep. A 64-bit peer should therefore play with 32-bit peers and with
  the original. To confirm by a local session test, as the GameSpy work was.

### Memory: pools, heaps and fixed regions

- **Pools grow by themselves.** `memory::PoolPair` / `SlabBank` (148 lines) are sized by `sizeof` of what they hold,
  and a pool refuses an object bigger than its items: on x64 an object that outgrew its pool stops the game with a
  report instead of overwriting its neighbour. That safety net is what makes the sweep safe.
- **Hard-coded byte sizes:** `engine/memory/memory_sizes.cpp` sizes the script region from item sizes written as
  constants (`kComponentBytes = 0x10`, `kStructBytes = 8`, `kArrayBytes = 0xC`, `kSymbolBytes = 0x14`, `kScriptBytes
  = 0xF4`...). They become `sizeof`. With the default pools (360,000 components, 64,000 structs, 48,000 vectors,
  26,000 arrays, 45,000 symbols), a 64-bit cell adds about 8 bytes to each component, symbol and array element, a few
  MB in all.
- **Fixed regions in bytes** (`engine/memory/manager.cpp`): the skater regions (0x1AFA4 each), the skater geometry
  regions (0x73F78), the theme region (0x3C00), the permanent and unloadable texture heaps (0x16800, 0x49C00), the
  cutscene temp heap (0x25800), the flexible particles' default heap (0x5000) and the 64 MB main block. Regions that
  hold objects with pointers need room on x64. Task X7 measures them with the heap report on x64 and sets 64-bit sizes;
  the 32-bit sizes don't change.
- **Stack storage sized in bytes:** `render::Device::make_primitive_at` builds a primitive drawer in 0x200 bytes of
  the lens flare's stack. That size must be checked against the x64 object.

### Platform

- `platform/crash.cpp` reads `CONTEXT::Eip / Esp / Ebp / Eax...` and dumps the stack in 4-byte slots. On x64 it
  needs the `Rip / Rsp...` registers, 8-byte slots and x64 unwinding (or `RtlCaptureStackBackTrace`).
  `tools/standalone/first_fault.py` and the symbolizers read x86 contexts too.
- `net::SocketId` (`engine/net/transport.h`, a `uint32_t`: Winsock's `SOCKET` value) and `net_winsock/winsock_transport.cpp`: `SOCKET` is 64-bit on Win64. The socket must stay in a `SOCKET`-wide id (widen `SocketId`, `kNoSocket` and the records that hold it), not in a 32-bit field.
- GameSpy callbacks are registered by code address (`code_address`, 32 lines in game/online). That is fine with
  64-bit pointers, as long as nothing narrows them (the `int` user word in NAT negotiation does).

### Third-party pieces

| piece | today | on x64 |
|---|---|---|
| Bink (the player's `binkw32.dll`, 15 exports through `media::Movie`) | 32-bit DLL from the player's install | **It can't load into a 64-bit process.** Plan: an FFmpeg-based `media::Movie` (`src/media_ffmpeg`): FFmpeg's Bink demuxer, Bink video decoder and Bink audio decoders, linked as DLLs under the LGPL (tasks X10a-c) |
| GameSpy SDK (IGN's 2011 BSD-3 release, built from source) | 32-bit, built by `cmake/gamespy_sdk.cmake` | built from source for x64. The fork is used in 64-bit builds elsewhere; the SDK's own pointer <-> int casts and the structures the game touches (`GPBuddyStatus`'s asserted size) must be checked (to confirm) |
| SDL2 2.30.9 (official VC package) | `lib/x86` | the same package has `lib/x64` |
| Dear ImGui 1.91.9b | source | source; nothing to change |
| LZHAM (source) | 32-bit | builds for x64 (its own `LZHAM_64BIT` paths; to confirm the defines the CMake list needs) |
| stb_image_write | source | nothing to change |
| D3DX9 (Microsoft.DXSDK.D3DX NuGet) | `release/lib/x86`, `bin/x86/D3DX9_43.dll` | the package has x64 (`lib/x64`, `bin/x64`; to confirm the paths) |
| Direct3D 9, DirectInput 8, DirectSound | system | x64 versions ship with Windows |
| GSL (headers), the C++ standard library | - | nothing to change |

**Bink through FFmpeg.** The game plays Bink 1 movies (the intro, cutscenes) and sound-only Bink streams (music and
sound streams, opened from a path, a file handle or memory). FFmpeg's libavformat has a Bink demuxer and libavcodec has
a Bink video decoder and Bink audio decoders (RDFT and DCT). Licence: FFmpeg is LGPL 2.1 or later when it is built
without `--enable-gpl` / `--enable-nonfree`. Linked as DLLs (avformat, avcodec, avutil, swresample), the project
keeps its own licence. The obligations, all to confirm before the first release that ships the DLLs:
- ship FFmpeg's licence text and a notice in THIRD_PARTY_NOTICES.md;
- offer the exact FFmpeg source used (or the build recipe and a pinned release);
- leave the DLLs replaceable;
- don't use GPL-only parts.

What the wrapper has to provide that `binkw32.dll` did:
- the frame timing that `BinkWait` gave;
- `BinkCopyToBuffer`'s pixel formats;
- sound output: Bink played into the game's DirectSound object (`BinkOpenDirectSound` / `BinkSetSoundSystem`). The
  wrapper decodes PCM and feeds a streaming buffer through `audio::Device`, with volume and pan per track
  (`set_volume`, `set_pan`);
- opening from a file handle at an offset (pcm.wad's streams).

Which Bink revisions the game's files use (`BIK` + revision letter), and that FFmpeg decodes each of them, is to be
confirmed by a header survey of the player's data in task X10a. The FFmpeg backend can be built and compared on the
32-bit build first, against `binkw32.dll`, before any x64 work. That makes it the best first task.

**Done in X10a** (`src/media_ffmpeg`, `[features] movie_codec=ffmpeg`; picture only, the sound is X10b):
- **The survey** (`node tools/ffmpeg/bink_survey.ts <game folder>`): every Bink file of the game is Bink 1 revision
  `BIKi` with no video flags (no alpha) and Bink audio's DCT transform - the 26 movies (640x480 but Intel_intro
  320x240, Dolby_Digital 720x486, the credits 480x480; 29.97 fps but Intel_intro 15; stereo 44.1 / 48 kHz, pccredits
  silent), the 137 music streams and the 2,771 Bink files inside pcm.wad (4x4 sound-only movies at 15 fps; music
  stereo 44.1 / 48 kHz, pcm.wad's mono 11.025-44.1 kHz). FFmpeg's demuxer takes `BIKi` and its video decoder decodes
  it; binkaudio_dct is the only sound decoder needed (binkaudio_rdft is built too, for other data).
- **FFmpeg:** 7.1.5 (FFmpeg's signed release tarball, pinned by SHA-256), LGPL 2.1 or later - built with
  `--disable-everything` and only the Bink demuxer, the Bink video / audio decoders and swscale enabled, no GPL or
  non-free parts, no external libraries: avutil-59, avcodec-61, avformat-61 and swscale-8 (about 2 MB together). No
  trustworthy prebuilt 32-bit LGPL DLLs exist (the common Windows builds are 64-bit, and most are GPL), so
  `tools/ffmpeg/build_ffmpeg.ts` builds them from source with FFmpeg's own cross-compiling path: configure and make with
  Ubuntu's mingw-w64 GCC ("win32" threads, libgcc linked statically: the DLLs import only Windows' own), on Linux or in
  WSL. The build is repeatable (the same DLLs byte for byte from any folder, any day) and the same tool builds the x64
  DLLs (`--arch x64`). The codec loads them by name at run time - nothing is linked, the exe starts without them
  (movie_codec=ffmpeg then falls back to Bink, logged) and any LGPL build of the same majors can replace them. The
  headers come from the same release's tag (cmake/ffmpeg.cmake).
- **Frames:** swscale makes BINKSURFACE32 (blue, green, red, unused) from the decoder's YUV 4:2:0 (BT.601 video range,
  accurate rounding): ~1.1 ms a 640x480 frame, any build type (a plain C++ loop took 45 ms a frame in Debug). Against
  binkw32 on the start-up movies (`thug2_movie_codec_check`): every colour byte within 2-5 of 255, 50-53 dB PSNR (the
  unused fourth byte is 255, binkw32's 0: the X8R8G8B8 texture ignores it); the frame counters (after the open, steps, go-tos, across the end) equal; 60 frames paced by
  `wait` in 2.00 s at 29.97 fps (binkw32 1.92-2.00 s).
- **Left for X10b / X10c:** the sound (decoded, but read past); the sound-only streams' pace follows Bink's audio clock
  in binkw32 (60 frames of a music stream: 4.2 s with Bink's sound off, 4.0 s here); the reopen of a paused pcm.wad
  stream from `from_where_` (the original passes its 4-byte copy of the header as the memory to open); the
  THIRD_PARTY_NOTICES entry (FFmpeg's LGPL text, the exact source - the release URL and hash -, the build recipe,
  that the DLLs are replaceable).

### CMake and presets

- `CMakePresets.json` has `x86-debug` / `x86-release` only. The root `CMakeLists.txt` refuses other architectures and
  imports SDL2 and D3DX9 from their `x86` folders. `src/app/CMakeLists.txt` and `src/tests/CMakeLists.txt` copy the
  x86 DLLs. Nothing else in CMake is architecture-specific.
- The checks that run in `npm test` (`tools/layers.ts`, `cmake_layers.ts`, the header tests) don't depend on the
  architecture.
- Since X1: `x64-debug` / `x64-release` presets (and `tools\verify_build.cmd x64`); `THUG2_ARCH` picks the
  packages' folders; an x64 configure adds only the libraries named in `THUG2_X64_READY` (root `CMakeLists.txt`;
  `gamespy_sdk` there builds the GameSpy SDK) and the checks whose libraries are all built. The fetched libraries the
  build compiles (ImGui, stb_image_write, LZHAM, volk, VMA, glslang) compile on x64 as they are (LZHAM picks its
  64-bit paths from `_WIN64` itself); they are built only when a ready library links them.

## Order of work

Each task is one PR. "Start after stage 11" means after branch `stage11` is merged into main (see "Conflicts").

| # | task | depends on | notes |
|---|---|---|---|
| X1 | x64 presets and the third-party imports by architecture; the x64 configure builds only the libraries listed as x64-ready (`THUG2_X64_READY`), starting with none | - | CMake only |
| X2 | `base/` on x64: `pointer_cell.h` split into the width-neutral helpers (`address`, `at_address`) and the 32-bit cell conversions (kept for the boundary, and asserting there); `format.h`'s pointer sizes; `base` x64-ready; `thug2_maths_check` (plain half) runs on x64 | X1 | |
| X3 | `formats/` and `wire/` on x64: the collision tree's links and the object records' array fields as 32-bit offsets resolved by accessors (`tree_children(base, link)`) instead of addresses written in place, and the same for any other in-place fix-up the survey finds; `thug2_format_check` / `parse_check` / `printf_check` logs identical on both builds | X2 | touches `engine/render` collision (hot path: measure) |
| X4 | Script cells: `script::Cell` holds a pointer-width value (`uintptr_t`) with 32-bit accessors for names, integers and floats; the 105 `word()` users sorted into "the 32 bits" and "the whole cell"; `memory_sizes.cpp`'s item sizes as `sizeof`; the pools' counts unchanged | X2 | design choice below |
| X5 | QB bytecode and precompiled images: native-function and member-function tokens carry the command table's index, not an address (both builds); precompiled images unpacked into pooled records (or relocated as offsets) instead of being used in place | X4 | |
| X6 | The layout asserts: the 230 asserts on pointer-holding records made 32-bit-only (`sizeof(void*) != 4 \|\| ...`, through one helper in `base/core`); boundary asserts left unconditional; SDK asserts' constants by architecture; the 11 unknowns read | X2 | mechanical, many files |
| X7 | Fixed regions and heaps on x64: the heap report on x64, 64-bit sizes for the skater / geometry / theme / texture regions and the main block where they overflow; the 0x200-byte primitive storage checked | X4, X6 | |
| X8 | `platform/` on x64: the crash report (x64 context, 8-byte stack slots, unwinding), `SOCKET` width, GameSpy's narrowed user word through a handle table, `code_address`'s 32-bit assert; `platform` x64-ready | X2 | |
| X9 | The GameSpy SDK for x64: built from source, its structures checked against what the game reads, a local protocol test as for the 32-bit swap | X1, X8 | |
| X10a | `src/media_ffmpeg`: FFmpeg's Bink 1 decoding behind `media::Movie` (video: open, frames, copy to buffer, wait, go to, pause), FFmpeg fetched as pinned LGPL DLLs; a header survey of the game's Bink files; chosen at start-up beside Bink's (`[features] movie_codec=bink\|ffmpeg`) | - | pure addition, 32-bit first |
| X10b | FFmpeg's Bink audio into the game's sound output: a streaming buffer through `audio::Device`, track volume and pan, the sound-only streams from a path, a file handle at an offset, and memory | X10a | |
| X10c | A/B on the 32-bit build: the movies and the music / sound streams through both codecs (frames, timing, levels); THIRD_PARTY_NOTICES and the licence notes | X10b | |
| X11 | `render_pc` / `gpu_d3d9` on x64 (D3D9 x64; handles, the dynamic buffers, `make_primitive_at`'s storage, the loading console's timer) | X6 | land before the Vulkan split (V5) or coordinate |
| X12a-e | Compile and fix by library on x64, one PR each: (a) `engine/` memory, lists, file, assets, sys; (b) `engine/` gfx, objects, render, ui; (c) `engine/` script, net, audio, sound, pad_input, media; (d) `game/`; (e) `editor/`, `frontend/`, `devtools/`, the backends (`audio_dsound`, `input_dinput`, `fs_win32`, `media_ffmpeg`), `app/` | X3-X8, X10c, X11 | C4267 / C4244 / C4311 / C4312 at 0 |
| X13 | Tools on x64: `first_fault.py` and the symbolizers read x64 contexts and PDBs; the game scripts (`tools/game`) find the x64 exe | X8 | |
| X14 | Floating-point equivalence: `thug2_maths_check` writes a corpus on the 32-bit build and compares with it on x64 (ulps per function); the accepted differences documented | X2 | |
| X15 | The first x64 run: the smoke levels, the front end, the editor, a local online session with a 32-bit peer; deploy (x64 DLLs: SDL2, D3DX9, FFmpeg); README "Running" for x64 | X9, X12, X13 | |

```
X1 -> X2 -> X3 ----------------------------------\
         -> X4 -> X5 ----------------------------+-> X12a-e -> X15
         -> X6 -> X7, X11 -----------------------+      ^
         -> X8 -> X9, X13 -----------------------/      |
         -> X14                                         |
X10a -> X10b -> X10c -----------------------------------/
```

### The script cell: widen it, or keep 32 bits?

- **Widen (proposed).** `Cell` stores a `uintptr_t`. Pools grow by `sizeof`: a few MB. Names, integers and floats
  are read through 32-bit accessors, so their bits don't change. The QB tokens never held a pool pointer except the
  native / member function operands (X5). The risk is the code that copies "the cell's 32 bits" between holders of
  different kinds: the 105 `word()` sites each have to be classified.
- **Keep 32 bits (fallback).** A cell holds a 32-bit offset from the base of one reserved region that every script
  pool, the string heap and the loaded QB files come from, plus table indices for the static entries (member
  functions, native functions). Layouts and pool byte sizes stay as the original's. The costs: every pointee must
  come from that region (text from the C runtime, the stack or the data overlay would not), and each access pays an
  add.

The QuickJS replacement of the interpreter (planned later) replaces the interpreter, not the data model, so this
choice stays either way.

Since X4: `script::Cell` holds a `uintptr_t`. Names, integers and floats are its low 32 bits (`word`, `integer`,
`real`; `low_word` where the game reads a component of a kind it doesn't check as a name), pointers the whole cell,
and a copy of any kind copies the `Cell`; `empty`, `same_bits` and `holds_word` test it whatever its kind. On x64
`word()` of a cell holding a pointer above 4 GB stops the game. The pools' item sizes are `sizeof` (qb.cpp,
`memory_sizes.cpp`, the HashItem pool), with the original's sizes asserted on x86; the script region grows by what the
pools take more than the original's pools at the original's sizes. Left for X5: the call tokens' 32-bit operands
(`prepare_script_tokens` stores the function's address through `cell::of`, which stops on x64) and the precompiled
images' records, which are the runtime's own `Struct` / `Component` / `Array` / `SymbolTableEntry` laid out in
32 bits. The layout asserts of the records that hold a cell are X6's.

## Conflicts and where PRs go

- **Stage 11 is still restructuring the code on branch `stage11`**: the script host and the command signatures, the
  skater physics, walking, online hub and vehicle classes, the editor and front-end file splits and their API, and the
  docs. When it is finished it is merged straight into main and pushed. Until then main is far behind `stage11`, and
  a PR against main would conflict at once.
- **Every task that changes game code, the engine, the editor, the front end, the script runtime or the backends
  starts after stage 11 is merged to main, and targets main.** The port touches almost every library, so that
  covers X2-X9 and X11-X15.
- **Can start earlier:** X10a-c (a new library, `src/media_ffmpeg`, plus a start-up choice in `app/`) and X1 (CMake
  only, built with nothing new marked ready). Until stage 11 merges they target the `stage11` branch. Their small
  shared edits (the root `CMakeLists.txt`, `tools/layers.json`, `app/main.cpp`'s install line) are additive and are
  rebased often.
- **Between the tasks:**
  - X6 and X12a-e both touch most headers. X6 goes first and lands quickly.
  - X4 / X5 own `engine/script`. Nothing else edits it at the same time.
  - X3 and X12b both touch the collision code. X3 goes first.
  - X11 and the Vulkan split (vulkan.md, V5) both touched `render_d3d9` (now `render_pc` and `gpu_d3d9`). X11 is small and goes first.

## Risks

1. **The script runtime's cells** (X4 / X5): a misclassified `word()` copy runs, but drops the top half of a
   pointer, so it crashes later somewhere else. Mitigation: the pools' item checks, `Cell`'s accessors asserting the
   holder's kind in debug builds, and the smoke levels with their scripts.
2. **Data used in place** (X3, X5): the collision BSP, precompiled scripts and anything else the survey finds. The
   collision tree is on the hot path, so the replacement accessors must stay as cheap as a load (an add per link).
3. **Layout dependencies no assert covers**: offset arithmetic over a record (`raw::` views, memcpy of whole records)
   and code that assumes 4-byte pointers in a computed size. The pools' size checks catch some at run time. The
   `raw::` sweep (42 lines) catches the rest.
4. **Bink through FFmpeg** (X10): audio / video sync, the sound-only streams' looping and seeking, and Bink revisions
   FFmpeg may not decode. The licence obligations are to confirm.
5. **Floating point**: last-bit differences can tip a physics threshold. They are measured (X14) and play-tested.
   Bit-exact equality with the 32-bit build isn't promised.
6. **The GameSpy SDK on x64** (X9): an untested configuration for this game's use of it. It needs a local protocol
   test before anyone goes online with it.

## Estimate

In rounds of the size this project has worked in (30-90 minutes of an agent's work each): X1 1, X2 1, X3 2, X4 3,
X5 2, X6 2, X7 1, X8 1, X9 1, X10a-c 3, X11 1, X12a-e 5-7, X13 1, X14 1, X15 1-2. About **26-30 rounds**, of which
X10 (3) can run before stage 11 merges and in parallel with the rest.

## How the numbers were measured

Three scripts (Python, run with `python -I`; they read `src/` only):
- the assert classifier (struct bodies found by regex and brace matching; a record "holds a pointer" if a member is a
  pointer, reference, owner, span, string, container, function pointer, pointer-sized type or vtable, directly or
  through a member record);
- a pattern count for the pointer, assembly, floating-point and pool sites;
- the Direct3D scan used by docs/design/vulkan.md.

They are approximate where regexes are: the counts are for planning, and the PRs work from the compiler's warnings
and the asserts.
