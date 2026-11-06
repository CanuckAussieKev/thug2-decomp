# A 64-bit (x64) build

Status: in progress. Done: X1 (the x64 presets and configure), X2 (`base/` on x64), X3 (formats / wire, collision links as offsets), X4 (the pointer-wide script cell), X5 (numbered call tokens, unpacked precompiled images), X6 (the layout asserts), X7 (the regions and heaps sized for x64; predicted, to be measured on x64 by X15), X8 (`platform/` on x64), X9 (the GameSpy SDK on x64; its protocol test is by hand), X10a / X10b (FFmpeg's Bink video and sound), X11 (the renderers compile on x64), X12a / X12b (engine), X13 (the tools read x64), X14 (floating point measured: the corpus). The design was written against
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
  `SOCKET` is pointer-sized on Win64. (Since X8 the glue's sockets are `uintptr_t`.)

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
  - the C runtime's `sqrt` of a double (the x86 runtime's is the x87's FSQRT, so it rounds as the control word says;
    measured by X14: no other function of `<cmath>` the game calls depends on it - below);
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
  CRT's transcendentals. Measured by X14 (below); the accepted differences are listed there.
- Online: messages carry positions and states quantised by the wire encoders (`wire/`). Peers simulate their own
  skaters and send states; they don't run in lockstep. A 64-bit peer should therefore play with 32-bit peers and with
  the original. To confirm by a local session test, as the GameSpy work was.

### Floating point: measured (X14)

`thug2_maths_check --write-corpus <file> [53|24]` on the 32-bit build writes a corpus: every checked function's inputs
and results, bit for bit, with the x87 at 53 bits or at 24 (as Direct3D 9 leaves the game's thread). `thug2_maths_check
--compare-corpus <file>` on another build reads the same inputs back, works the functions out again and reports, per
function, how many results differ, the largest and mean difference in ulps of the stored type and the largest absolute
one. The corpus (`src/tests/maths_corpus.h`, `maths_corpus_cases.cpp`) covers the C runtime's `<cmath>` as the game
calls it (doubles and floats), every function in `base/maths` (maths.h, trig.h, random.h, vec4.h, mat4.h,
quat_rotation.h, rotation_blend.h, shape_tests.h, view_transforms.h), the game files' formulas that replaced their x87
code (the functions `thug2_maths_check` checks against the x87 reference) and D3DX9's maths. 20,000 inputs per case
(114 MB; not committed - written by the 32-bit build when wanted). The 32-bit build compared with its own corpus: no
differences (the cases are deterministic).

Results, the 64-bit build against the 32-bit build's corpus at 53 bits (151 of the 193 functions have no difference):

| what | differs | why | decision |
|---|---|---|---|
| all of `base/maths` that returns floats: vectors, matrices, quaternions, the rotation blender, shape tests, view transforms, matrix product, random | 0 | the same SSE2 operations in the same order on both builds | - |
| `base/maths`' float trigonometry (`make_*_matrix`, `create_rotate_2d`, `set_from_euler_angles`, `get_angle_in_plane`, `matrix_rotation_y`...) | 0.03-1.9% by 1 float ulp before; 0 now | the 64-bit runtime's `sinf` / `cosf` / `acosf` are float routines of their own; the 32-bit runtime's are the double functions rounded once (corecrt_math.h) | fixed: `maths::runtime32::` (trig.h) works a float as the 32-bit runtime does; on 32-bit x86 it is the runtime's own function (the objects compare identical but for line numbers) |
| the runtime's float functions called as `std::` (outside `base/maths`) | `asin(float)` 6.4%, `acos(float)` 2.1%, `sin` / `cos` / `tan(float)` 0.03-0.11%, `exp` / `log` / `pow(float)` < 0.1%; 1 float ulp | as above | float maths outside `base/maths` is to call `maths::runtime32::` too (a mechanical sweep: the x86 objects show the callers - their calls to `?sin@@YAMM@Z` and the other float overloads) |
| the runtime's doubles | `sin` / `cos` / `tan` 3-4.4%, `asin` / `acos` 6.8-7.4%, `atan2` 0.09%, `exp` 0.7%, `pow` 0.1%, `hypot` 34% (2 ulps); 1 double ulp | the 32-bit and 64-bit runtimes are different implementations (the 64-bit one's FMA3 paths off: the same picture) | accepted: 1 double ulp, 1e-16 relative; rounded to float (what the game stores) the results agree - `sin -> float`, `cos -> float`, `atan2 -> float`, `turn_flat`, `turn_move`, `probe_tip`, the view camera's fov: 0 differences. Matching them would mean one maths library for both builds, which changes the 32-bit build's results |
| doubles worked from them: `maths::spin_by`, `neg_asin_cos`' 1 / cos, `read_turn_angle`, `get_turn_around_axis` | 3.4% (up to 8192 ulps: a difference of two products), 7.2%, 6.9%, 6.3% | the runtime's doubles, carried | accepted (relative size as above; the large ulps are cancellation near zero) |
| D3DX9's maths, its 64-bit DLL against its 32-bit one | the matrix product 11.8%, the vector transforms 14-73%, normalize 59%, the inverse 59%, look-at 38%, perspective / ortho 2-3% | D3DX's 64-bit code is another implementation (other sums, its own reciprocal estimates; its perspective and ortho builders work in float, as the 32-bit one does at 24 bits) | no consequence: the game calls none of D3DX's maths (our C++ replaced it, and that is identical on both builds); this explains the 64-bit "plain C++ against D3DX" differences X2 measured |

The x87's precision on the 32-bit build (the corpus at 24 bits against the 32-bit build at 53): of the runtime's
functions only `sqrt` of a double depends on it (the x86 runtime's is the x87's FSQRT): at 24 bits it rounds to a
float's 24 bits, in every result. A Direct3D 9 device made without `D3DCREATE_FPU_PRESERVE` sets 24 bits on its
thread (measured: the x87 control word goes from 53 to 24 bits, MXCSR unchanged, nothing on x64), so on the 32-bit
build with `renderer=d3d9` every double square root on the game's thread is rounded to 24 bits; with Vulkan (the
default) it isn't. `make_unit`, `set_length` and the rotations built from lengths (`square_up_axes`, `get_rotation_axis_angle`,
the quaternions, the rotation blender) differ by 1 float ulp in 3-27% of results between the two, `get_distance` in
all of them (a 24-bit double). The 64-bit build has no x87 precision: it computes as the 32-bit build with Vulkan. D3DX9's
32-bit look-at, perspective and ortho builders depend on the precision too (none of them is called). Accepted: the
64-bit build matches the 32-bit build's default renderer; the 32-bit Direct3D 9 path is the one that differs, and
`base/maths` documents its double intermediates as the x87's at 53 bits.

Other observations:
- The SSE reciprocal estimates (`maths::estimate_reciprocal`, `estimate_reciprocal_sqrt`: `rcpss` / `rsqrtss`) give the
  same results on both builds on one processor, but they are the processor's own: another vendor's can differ. They
  are used only by the renderer (`pc_mesh_render`, `pc_texture_projection`, `shadow_base`, `texture_splats`).
- No integer / float truncation, uninitialised value or order-of-evaluation difference showed up: every difference
  above has its cause in the C runtime or in D3DX.

What X15 compares (traces of the same input on the 32-bit build with `renderer=vulkan` and on the 64-bit build, a
fixed frame time, the same seeds), per frame: the skater's position, velocity and orientation matrix; its state (on the
ground, rail, lip, wall, in the air; walking) and the feelers' hits; the trick, score and combo state; the camera's
matrix; the script's numeric results (`RandomRange`, the float cells); the animation blend's matrices. The first frame
that differs and by how much, against the noise floor of the 32-bit build with `renderer=d3d9` against itself with
Vulkan (the square root above): a divergence from the 64-bit build no larger than that one is the accepted kind.

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

Since X7: the sizes of the records the regions, pools and fixed allocations hold were measured with the x64 compiler
(a syntax-only probe over their headers: `ProbeSize_<sizeof(T)>`). The block header (`memory::ChunkPrefix`) is 32
bytes on x64 (two pointers, padded to keep blocks 16-byte aligned), a list node 32, a script component 24, a
structure 16, an array 16, a symbol 32, a script 400 (0xF4 on x86); `PcPrimitive` is 0x1A0 bytes, inside the lens
flare's 0x200 (now `render::kStackPrimitiveBytes`, asserted by the backend). The rules:
- every fixed allocation of a record that grows is `sizeof` (the original's count asserted on x86): the animation
  frames, the imposter manager, network messages and connections, bone volumes and rigs, the online player records,
  the park editor's records and pools, the skater's score, the index stores, texture animation keys, the script
  structures made outside their pool, the save's reserve pools, and the pools of animation controllers, spawned
  polygons and event receiver lists; the pointer arrays (`count * 4`) are `count * memory::kPointerBytes`; script
  structures made in stack bytes take `sizeof` and its alignment.
- every fixed region and heap made with a size - the skater, skater geometry, skater info, theme, front end, network
  and online regions, the named heaps (the texture heaps, the animation heaps, `MemInitHeap`'s), the flexible
  particles', the park editor's and the COIM heap - is `memory::sizes::region_bytes(original)`: the original's on
  x86, twice it on x64. The bound behind it: on x64 a pointer and a block header double and nothing else grows, so no
  allocation more than doubles (a record's members can be laid out at twice their x86 offsets), and whatever fits in
  a region on x86 fits in twice the bytes on x64. The cutscene heaps take what is left of the top-down heap, as before.
- the main block grows on x64 by what the original's 64 MB held beside the script region (~60 MB: the level's data
  and the other regions, each at most doubling), the script region by its room beside the pools (~2 MB); the pools
  themselves were already counted at their x64 item sizes (X4).
The 32-bit build's sizes are unchanged (asserted). After a level loads the log lists every heap
(`memory heap in <level>: region=... used=... peak=... blocks=... peak_blocks=... name=...`);
`node tools/game/heap_report.ts <x86 log> <x64 log>` compares two builds heap by heap (task X15 runs it on x64).
The park editor's memory meter reads the main and park heaps' free bytes, which are larger on x64: to be settled
with X15's measurement.

### Platform

- `platform/crash.cpp` reads `CONTEXT::Eip / Esp / Ebp / Eax...` and dumps the stack in 4-byte slots. On x64 it
  needs the `Rip / Rsp...` registers, 8-byte slots and x64 unwinding (or `RtlCaptureStackBackTrace`).
  `tools/standalone/first_fault.py` and the symbolizers read x86 contexts too (x64: Since X13, below).
- `net::SocketId` (`engine/net/transport.h`, a `uint32_t`: Winsock's `SOCKET` value) and `net_winsock/winsock_transport.cpp`: `SOCKET` is 64-bit on Win64. The socket must stay in a `SOCKET`-wide id (widen `SocketId`, `kNoSocket` and the records that hold it), not in a 32-bit field.
- GameSpy callbacks are registered by code address (`code_address`, 32 lines in game/online). That is fine with
  64-bit pointers, as long as nothing narrows them (the `int` user word in NAT negotiation does).

Since X13: the tools read x64. `tools/symbolize.ts` names the offsets of every module the report names that has a file
in `--pdb-dir` (the game's exe, a test program, our DLLs; the system's are skipped), from 32-bit and 64-bit reports and
PDBs alike. `tools/standalone/first_fault.py` picks the target's debug API by the exe's PE machine: a 32-bit target
through WOW64 as before; a 64-bit one with the AMD64 `CONTEXT` (RAX-R15, Dr0-Dr7 for `--watch`, `--watch-size 8` for a
pointer), the calls unwound by dbghelp's `StackWalk64` from the modules' unwind data, and the stack scan over 8-byte
slots. `tools/game` deploys a build with `deploy_build.ps1 -Game <folder> -Arch x86|x64` under the game lock: the same
file names for either machine (thug2-decomp.exe, d3dx9_43.dll, SDL2.dll, FFmpeg's DLLs), the build going IN PLACE of the
one in the folder, and `game_lock.ps1`'s release giving the folder back as acquired; `run_game.ps1` / `load_check.ps1`
take `-Arch` to refuse a folder holding the other machine's exe. `tools/game/read_dump.ts` (WER dumps) still reads
32-bit contexts only.

Since X8: `platform` is x64-ready. The crash report reads the context through `instruction_pointer` /
`stack_pointer`; on x64 it logs `RAX`-`R15`, the top of the stack in 8-byte slots (as many slots as x86's), the calls
unwound from the context (`RtlLookupFunctionEntry` / `RtlVirtualUnwind`, guarded: `[rsp=...] <module>+0x...` lines)
and the same return-address scan as x86 over 8-byte slots. The x86 report is unchanged, line for line;
`thug2_crash_check [kind]` (src/tests/crash_check.cpp) causes any of the test faults a few calls deep for either
build. `net::SocketId` is `uintptr_t` (`kNoSocket` all ones, asserted equal to `INVALID_SOCKET`), and `AppHost`
keeps it at that width; the GameSpy glue's sockets (`nat_begin_with_socket`, `peer_start_reporting_with_socket`,
NAT completion) are `uintptr_t` too. NAT negotiation's integer user word is no longer carried in the SDK's `void*`:
the glue keeps the integers in a table (a `std::deque`, emptied with the SDK's negotiation list) and hands the SDK a
pointer to one - the same value back on either build. `code_address.h`'s 32-bit `function_at(uint32_t)` is gone (X5
left it unused). The GameSpy glue (`gamespy.cpp`) is built only where the SDK is (`THUG2_GAMESPY_SDK`): online is off
on x64 until X9 lists `gamespy_sdk`. `net_winsock` compiles for the SOCKET width now but links the engine, so it is
listed with it (X12c).

Since X9: `gamespy_sdk` is in `THUG2_X64_READY`, so `platform` builds its GameSpy glue on x64 too. The SDK's x64
build was surveyed at /W4: the one real 64-bit bug was the peer's callback queue, which kept every callback address in
an `int` (`PEERCBType`) - `intptr_t` now, by `cmake/gamespy_sdk.cmake`, as is GP's index carried through a `void*`
(a harmless round trip, changed so that C4311 / C4312 can be errors in the SDK's build). The x86 SDK's code is the
same before and after (every object's disassembly compared). Two of the game's callbacks had the wrong argument
widths: the news download's ghttp length (two 32-bit halves - on x64 that moved its `param`) and the persistent-data
callback's `time_t` (32-bit in the record: wrong on x86 as well, its `data` read the time's high half); both take the
SDK's types now and `gamespy.cpp` asserts them, with the GP callback arguments' offsets the game's records assume.
`thug2_gamespy_check` (src/tests) runs the SDK and the glue without the network - the helpers, the peer's queued
callbacks, qr2's heartbeat and query answer on 127.0.0.1 and a NAT negotiation against fake servers on the loopback
- and prints the same on both builds. Untested until a real session: the protocol against a server (a local
OpenSpy server, then online by hand) - the chat login, game listing, hosting, NAT with a real peer, GP, stats, HTTP.

### Third-party pieces

| piece | today | on x64 |
|---|---|---|
| Bink (the player's `binkw32.dll`, 15 exports through `media::Movie`) | 32-bit DLL from the player's install | **It can't load into a 64-bit process.** Plan: an FFmpeg-based `media::Movie` (`src/media_ffmpeg`): FFmpeg's Bink demuxer, Bink video decoder and Bink audio decoders, linked as DLLs under the LGPL (tasks X10a-c) |
| GameSpy SDK (IGN's 2011 BSD-3 release, built from source) | 32-bit, built by `cmake/gamespy_sdk.cmake` | built from source for x64 since X9: the peer's callback queue kept addresses in an `int` (`PEERCBType`, now `intptr_t`), the other pointer <-> int casts read; pointer cuts are errors in its build; the shared structures and the callbacks' argument widths checked (docs/gamespy.md "64-bit builds"). The protocol on x64 is to be tested against a local OpenSpy server and online by hand |
| SDL2 2.30.9 (official VC package) | `lib/x86` | the same package has `lib/x64` |
| Dear ImGui 1.91.9b | source | source; nothing to change |
| LZHAM (source) | 32-bit | builds for x64 (its own `LZHAM_64BIT` paths; to confirm the defines the CMake list needs) |
| stb_image_write | source | nothing to change |
| D3DX9 (Microsoft.DXSDK.D3DX NuGet) | `release/lib/x86`, `bin/x86/D3DX9_43.dll` | the package has x64 (`lib/x64`, `bin/x64`; confirmed in X11) |
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

**Done in X10a** (`src/media_ffmpeg`, `[features] movie_codec=ffmpeg`; the picture - the sound is X10b's):
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

**Done in X10b** (the sound: `media_ffmpeg/ffmpeg_sound.*`, `audio::Device`'s streaming calls):
- **The path:** binkw32 played its sound into the game's DirectSound object through buffers of its own; the FFmpeg
  codec plays the first sound track of a movie or stream (binkaudio_dct, its float blocks rounded to 16-bit PCM) into a
  looping buffer it makes through `audio::Device` and keeps filled ahead of the play position each time the movie is
  serviced (`wait`, `do_frame`: the movie player asks in a loop, the stream thread every 5 ms) - the samples decoded,
  then silence, so a buffer the decoding falls behind never plays old samples again. Three device calls were added
  for it: `buffer_position`, `write_buffer`, `resume_buffer` (DirectSound's GetCurrentPosition, Lock / Unlock, Play
  without a rewind; the muted output forwards them and keeps the buffers at silence). Any output the device is plays
  it, not only DirectSound.
- **What binkw32 does, measured** (`thug2_movie_codec_check --sound`: binkw32 given a stand-in DirectSound object, the
  FFmpeg codec a stand-in `audio::Device`, both buffers played by the clock and what passes the play position kept):
  the buffer's flags (volume, pan, frequency, GETCURRENTPOSITION2, GLOBALFOCUS) and size (0.6 s rounded up to 256 sample
  frames: 115,712 bytes at 48 kHz stereo, 106,496 at 44.1 kHz stereo, 53,248 at 44.1 kHz mono), written about 0.59 s
  ahead; the track volume (0 ... 32768) as 10000/3 hundredths of a decibel a decade below 32767, rounded, never below
  -10000; the pan (0 ... 65536) a straight line to -10000 ... 10000, truncated - both equal for every level and pan
  swept; the frames paced by the buffer's play position; a packet's samples cut to the size its first word declares
  (the decoder gives whole blocks: a loop then lasts exactly its frames, 55,860 samples for a 19-frame stream at
  15 fps); after running dry the samples go on 61-76 ms past the play position (65 here); a go-to starts the sound one
  decoded block before the block holding the frame's time (binkw32 plays the frame's own time, not the sound stored
  with it, which is about 0.7 s ahead); a go-to to the first frame, like the end of a loop, plays on (keeping about half
  a buffer of what was written ahead); a pause stops the buffer and the resume plays on.
- **Equal:** the samples heard from both codecs, compared window by window, are within 1 of each other (mean 0.03 -
  the float rounding) on the start-up movies, the music streams and a sample of pcm.wad's sounds, SNR 61-97 dB; the
  only larger differences are a handful of samples at full scale (binkw32 limits the blocks' overlap there) and the
  last hundredths of a sound's tail, which binkw32 makes silent (both under -70 dB).
- **The paused pcm.wad stream:** `BinkStream::tick_frame` reopens a paused stream from `from_where_`; for a stream opened
  from memory that is the 4 bytes "BIKi" the original copies there - binkw32 refuses to open it (the check shows it),
  so a paused pcm.wad stream stays silent after its pause. That is the original's behaviour and is kept; the FFmpeg
  codec refuses it too (it reads the header's size word, zero, and no further). Only the names were wrong (the flag
  0x04000000 is Bink's from-memory flag, not a file handle).
- **Left for X10c:** the A/B listening on the 32-bit build, the THIRD_PARTY_NOTICES entry and the licence notes.

**Done in X11** (`render_pc`, `gpu_d3d9`, `gpu_vulkan`): every translation unit of the three compiles to objects with
the x64 compiler and the build's flags with no warning (C4267 / C4244 / C4311 / C4312 included) and no error. They are
not on `THUG2_X64_READY` yet: they link `thug2_engine`, which becomes x64-ready with X12. What changed or was checked:
- **The loading bar's timer callback** (`draw_loading_screen_frame`) takes `LPTIMECALLBACK`'s `DWORD_PTR` words (the
  loading console's own timer already did).
- **Pointer arrays sorted by qsort:** the billboard batches and the visible instances were sorted as 4-byte elements;
  they are `sizeof` their pointer now (on x64 a 4-byte element would have sorted halves of pointers).
- **Handles:** a graphics handle is its object's address on both backends (`gpu_d3d9/d3d9_handles.h`,
  `gpu_vulkan/vulkan_objects.h`), and the UI's texture ids are 64-bit on both. Vulkan's non-dispatchable handles are
  `uint64_t` on x86 and pointers on x64; nothing stores one in a 32-bit field or a cell (the compiler would refuse the
  pointer on x64).
- **The dynamic vertex and index buffers:** offsets and sizes are Direct3D 9's `UINT` (32 bits on both builds); the
  pointers into a lock are pointers.
- **`make_primitive_at`:** `PcPrimitive` is asserted against `render::kStackPrimitiveBytes` (since X7).
- **The Vulkan caches:** the 64-bit build keeps its shader and pipeline caches in files of their own
  (`thug2-decomp-vulkan-shaders-x64.bin`, `thug2-decomp-vulkan-pipelines-x64.bin`): a 32-bit and a 64-bit driver are
  different binaries, so an x86 driver's pipeline cache is never given to x64's.
- **Libraries:** D3DX9's package has `lib/x64/d3dx9.lib` (x64, exporting `D3DXAssembleShader` and
  `D3DXSaveSurfaceToFileA`) and `bin/x64/D3DX9_43.dll`; Direct3D 9's `d3d9.lib` and winmm come from the Windows SDK for
  each architecture; volk opens the system's 64-bit Vulkan loader.

**Done in X12a** (`engine/` memory, lists, file, assets, sys): every translation unit of the five folders compiles to
an x64 object with the build's flags with no warning and no error, and clang-tidy with the x86_64 target finds nothing
in them. With these fixes the whole of `engine/` (304 translation units) compiles to x64 objects with no warning and no
error; what is left for X12b / X12c is the review the compiler can't do and their folders' x86_64 clang-tidy. What
changed or was checked:
- **The memory manager:** the code and data bounds it prints are addresses, printed at the pointer's width
  (`%08zX`); the static storage the manager, its main region and its two heaps are made in is aligned for them.
  Block offsets in the heaps' memory are 32-bit and checked against it (the main block's size is 32-bit on both
  builds); the block header is 32 bytes on x64 (since X7).
- **The asset loaders' parameter blocks:** a caller's parameters are copied into a pool block as words. The skinned
  model's hold the `.cas` contents' address, so they are copied by `sizeof` (5 words on x86, 6 on x64); the other
  asset types' parameters hold no pointers and keep their fixed sizes on both builds (asserted).
- **Checked, nothing to change:** the file system's handles are pointers (`fs::Handle`, the disk's `DiskFile*` is
  Win32's `HANDLE`); file sizes and offsets are the game's 32-bit values (`long` stream positions are 32-bit on Win64
  too); the asset manager and the hash tables key by 32-bit checksums, never by address; the lists' nodes, the pools'
  free lists and the item pools hold pointers and are sized by `sizeof`; the timers are 64-bit counters.
- `engine/util/lzss.cpp`'s match tree takes a 32-bit byte count (its runs are small).

**Done in X12b** (`engine/` gfx, objects, render, ui): all 190 translation units compile to x64 objects with the
build's flags with no warning and no error, and clang-tidy with the x86_64 target finds no narrowing in them (its
narrowing and widening checks and clang's 64-to-32 and pointer <-> integer warnings). What changed or was checked:
- **Products widened before they become a size or an offset** (bugprone-implicit-widening-of-multiplication-result /
  misplaced-widening-cast on x86_64): a particle's place in the history positions, the animation keys' word offsets
  and their count table's index, an animation cache entry's translation keys, the collision tree copy's face bytes and
  the grid's scratch counts, the bone volume table's clearing (`sizeof` the pointer), the model particles' position
  count, the model widget's corner array, the script array's count word. The same code on x86.
- **The feeler observer's caller** (`render::FeelerResult::caller`, a return address for the collision log) is
  pointer-sized; it was narrowed to 32 bits. Its reader, the collision log in `game/rider`, prints it in X12d.
- **Checked, nothing to change:** the components are made by `new` (sizeof; their byte constants are only asserted);
  the sets of objects known by their address (flashes, spawned polygons, splats, shadow edges) hold the pointers
  themselves (`lists::ObjectSet`); objects, screen elements and the scripts' references to them are 32-bit checksums,
  never addresses; placement storage for objects with pointers is aligned for them (the scene load's description,
  the lens flare's primitive); the animation files are read through byte readers, their sections' addresses kept in
  the animation's own object; the fixed-size copies (`memcpy` of 4 bytes, the particle configurations' copy up to a
  field) are of floats, colours and records without pointers. The script methods that read a float into the stack slot of
  their parameters' pointer when the float is missing (the original's leftover: a meaningless default) read the
  pointer's low 32 bits on x64 - as meaningless.

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
| X13 | Tools on x64: `first_fault.py` and the symbolizers read x64 contexts and PDBs; the game scripts (`tools/game`) find the x64 exe | X8 | done |
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

Since X5: a prepared call token (0x43 a C function, 0x46 a member function table entry) holds its target's number, not
its address, on both builds (`engine/script/names.h`: the targets numbered in the order they are installed, a
function or entry installed again keeping its number; `prepare_script_tokens` writes the number, `native_function_at` /
`member_entry_at` read it back by one indexed read). The QB files on disk are unchanged; only the cached, prepared
copy of a script differs. Precompiled images (magic 0xB075EF6F) are no longer used in place: `precompiled.cpp` reads
the image as a file (its 32-bit fields at their offsets) and unpacks it into the runtime's pooled records, which own
their values like a .qb's symbols. No game file is a precompiled image.

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
6. **The GameSpy SDK on x64** (X9): built and checked offline (docs/gamespy.md "64-bit builds"); the protocol on x64
   still needs a local OpenSpy test before anyone goes online with it.

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
