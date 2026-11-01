# Style guide

The code follows the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), with the
house rules below for what they leave open. `.clang-format` and `.clang-tidy` at the root enforce what tools can.

## Naming

| What | Style | Example |
|---|---|---|
| Types: class, struct, enum, alias | PascalCase | `RiderPhysics`, `ScriptValue` |
| Functions and methods | snake_case | `tick_frame()`, `find_entity()` |
| Variables, parameters | snake_case | `frame_time`, `target_pos` |
| Non-static data members | snake_case + `_` | `speed_`, `frame_count_` |
| Namespace-scope variables | `g_` + snake_case (`s_` for a file's alias of one) | `g_frame_count` |
| Constants (`constexpr` / `const`) and enumerators | `k` + PascalCase | `kMaxPlayers`, `kTimer` |
| Namespaces | snake_case, = the folder | `maths::`, `script::` |
| Template parameters | PascalCase | `template <typename Value>` |
| Macros (rare) | ALL_CAPS, `THUG2_` prefix | `THUG2_ASSERT` |
| Files | snake_case, `.h` / `.cpp` | `rider_physics.cpp` |

- No type prefixes (C, S, E, I) and no type-encoding (Hungarian) prefixes; the only markers are scope ones: a
  member's trailing `_`, a global's `g_`, a constant's `k`.
- Types are PascalCase so that `Rider* rider` reads naturally; everything else is snake_case like the standard library.
- An accessor that returns a thing is `get_<thing>()` when locals holding its result are called `<thing>`.
- The game's own versions of C runtime functions are `game_<name>` (`game_rand`, `game_qsort`) so they never hide the
  C runtime's.
- Names say what a thing is or does; abbreviate only the usual (pos, dir, min, max, num, id).
- The game's own names - script commands, checksum names, strings - stay as the game spells them in strings and
  tables. External APIs (Direct3D, Win32, SDL, ImGui, GameSpy) keep theirs.

## Formatting

- `clang-format` with the root `.clang-format`: 4-space indent, 120 columns, `int* p`. Includes keep the order they
  are written in (standard library, then the project's; some Windows headers must follow `windows.h`).
- `#pragma once` in headers; project includes by path from `src/`.
- Comments say what the code does and why. A function rewritten from the game carries the original's address in a
  comment above it (`// 0x0046B940`); code that isn't in the original game is marked `NOT ORIGINAL`.

## Folders

```
src/
  main.cpp
  platform/   window, input devices, timers, crash reports, settings, code addresses (the Win32 / Direct3D /
              DirectSound / SDL / GameSpy side)
  formats/    readers and writers of the game's files: archives, textures, scenes, collision, animations, fonts,
              sounds, saves, custom parks (library thug2_formats)
  wire/       the network messages and their framing (library thug2_wire)
  gpu/        the GPU's vertex and index buffers
  engine/     core, maths, lists, files, assets, script, objects, renderer, 2D, audio, sound, network, pad input
  game/       skating, the rider, goals, parks, saves
  frontend/   menus and screens
  editor/     the level editor
```

`engine/` doesn't include `game/`, `frontend/` or `editor/`; `formats/` and `wire/` include nothing of the game but
`engine/core`'s leaf headers (they build as libraries of their own). One folder is one namespace (formats and wire
keep the namespaces their headers had). The layers and their rules: docs/architecture.md.

## Code

- Prefer what the Guidelines prefer: `enum class`, `constexpr`, `nullptr`, RAII and `std::unique_ptr` for ownership,
  `std::array` / `std::span` over raw arrays, `std::bit_cast` over type punning, explicit readers / writers for
  file and network formats.
- Standard C++ only, buildable by MSVC and GCC: no inline assembly (the original's x87 maths is plain `<cmath>` -
  results may differ from the original's in the last bits, which is accepted), no compiler-specific keywords outside
  `src/platform/compiler.h`.
- Where a rule can't be kept (for example a layout a file format fixes), the code says so with a
  `// NOLINT(<check>)` naming the rule and a reason; the list of such places is kept short.

## Exceptions

`.clang-tidy` enables the Guidelines' checks (`cppcoreguidelines-*`) with `bugprone-*`, `misc-*`, `modernize-*`,
`performance-*` and the naming check, and switches off by name, each with its reason, the ones that conflict with
what the code is: a decompilation that keeps the original's data layouts, constants, formatting and pointer-walking
loops (magic numbers, pointer arithmetic, C arrays in layouts, non-const globals, printf
formatting, trailing return types and a few more). Everything else runs with zero warnings; the places that can't
keep a check carry a `NOLINT` with their reason:

- **Plain records** of file and network formats (`*_format.h`, the message structs, the format checks' own records)
  name their fields as plain data, without the member suffix (`NOLINTBEGIN(readability-identifier-naming)` around
  each record).
- **Outside names**: forward declarations of SDL, Win32 and Winsock types, the GameSpy SDK's globals, Winsock's
  `_WINSOCK_DEPRECATED_NO_WARNINGS`.
- **The original's behaviour, kept as written**: integer divisions where it divides integers, bitwise compares of
  floats (`memcmp`), repeated branches, assignments and increments inside conditions, switches that ignore other
  values, its random seed and `rand()`.
- **The scene's collider arrays** (`StaticCollider[n]`): indexed as arrays of exactly that type.
- **COM interfaces** (forward-declared Direct3D types): released, never deleted, so no virtual destructor.
- **The C runtime's allocator** where a library or the original's file reader wants `malloc` / `free`.
- **Macros**: libraries' configuration macros (`DIRECTINPUT_VERSION`) and `src/platform/compiler.h`, the one place
  for compiler specifics.
- **Plain enums** that stay plain say why: bit flags or-ed together, indexes into tables, values fixed by a file, a
  script, the wire or an SDK, or closed sets still held in the original's integer fields.
- **The script values' cells** (`engine/script/param_struct.h`): 32-bit unions tagged from outside, read by their
  tag - the script runtime's own files as a whole, single places elsewhere.
- **No `reinterpret_cast`, no integer-to-pointer cast** anywhere in `src/` (both checks on). Bytes from outside
  are read by the boundary layer's readers (`formats/`, `wire/`); casts between related classes are `static_cast`;
  what is left of the original's ways of reaching memory has a name, so it can be counted and removed:
  `cell::of` / `cell::pointer<T>` / `cell::address` (`engine/core/pointer_cell.h`: a pointer kept in a 32-bit
  cell - script values, callbacks' user words, addresses used as ids), `raw::as<T>` / `raw::field_at<T>` /
  `raw::bytes` / `raw::chars` / `raw::owner_of<T>` (`engine/core/raw_view.h`: one of the original's in-memory
  layouts read through another type or at a byte offset), `memory::bits_as<T>` (a field's bits as another type of
  its size), `code_address` / `function_at<F>` / `crt::library_function<F>` (`platform/`: code addresses).
  New code uses none of them: a field gets its member, a pointer its type. Byte layouts that stay (file records used
  in place, vertex buffers) are read through the boundary's accessors instead: `collision_format::FaceTable`
  (`formats/`), `gpu::VertexView` / `VertexWriter` (`gpu/`).
- **Downcasts** go through `core::down_cast<Derived>(base)` (`engine/core/down_cast.h`): code that has checked an
  object's type tag (or asked a manager for an object of a type) converts the base pointer with it; Debug builds
  assert the object really is a Derived (a class's own `is_instance(const Base&)` if it has one, else `dynamic_cast`).
  The `static_cast` inside it is the one downcast in `src/` (its NOLINT says so).
- **Arrays are `std::array`** (or `std::vector` / `std::string` / `std::span`); a pointer to the elements is asked
  for with `.data()` where a C API or a pointer-taking function wants one - never passed through `...` (printf-style
  calls take `.data()`). Indexing with anything but a constant goes through `core::at(container, index)`
  (`engine/core/checked_index.h`: `gsl::at` with any integer or plain-enum index): out of range ends the game with a
  report instead of reading past the end. Locals of `std::array` type are value-initialised (`{}`).
- **Ownership**: `gsl::owner<T*>` (Microsoft GSL, the Guidelines' own) marks a raw pointer that owns what it points
  to where `std::unique_ptr` can't be used yet.
- **The rule of five** is kept as the Guidelines' C.21 allows: a defaulted destructor alone, and classes that copy
  but don't move; a class with a destructor of its own deletes its copies unless it really copies.
- **Constructor bodies in the original's order**: an assignment stays in the body (not an initialiser) when moving it
  could change what the constructor does - set after a heap scope, a registration with `this`, a method call or an
  allocation made in that order, or set twice (`NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)` around those
  statements, the reason given). Everything else is initialised in the list or by a default member initialiser.
- **Members left unset** by a constructor have a default member initialiser (`{}`), as do locals of plain record
  types (`Vec4 box{};`): zero where the original left whatever the memory held.
- **The GameSpy SDK** takes non-const `char*` where it only reads: `const_cast` there with a NOLINT naming the SDK.
- Narrowing conversions are written as `static_cast` where the original narrows.
- **`const` wherever nothing changes** (`misc-const-correctness`): locals and what pointers point at. Placement new
  goes through `memory::construct_in` / `construct_value_in` / `construct_global_in` (`engine/core/mem.h`) and the
  original's unbounded inline copies through `text::copy_unbounded` / `append_unbounded` (`engine/core/str.h`), so
  the check sees the writes. The one exception: `src/tests/maths_check.cpp`'s x87 reference, whose locals inline
  assembly writes.
- **Mixed-sign comparisons** say which conversion they make: `std::cmp_less` and its kin where the answer is the same
  for every value (an int against a narrower unsigned, a container's `size()`), `ints::as_signed` /
  `ints::as_unsigned` (`engine/core/ints.h`) where the original compares a 32-bit count as an int or an int as a
  count - the same bits, so a negative index still reads huge.
- **Signed bytes** widened on purpose are written `int{byte}`; **text read as a number** goes through
  `text::to_int` / `text::to_double` (`engine/core/parse_number.h`: atoi's / atof's answers, over
  `std::from_chars`; `thug2_parse_check` compares them with the C runtime's); a `T**` passed as `void*` is cast
  explicitly.
- **No C-style variadics of our own**: printf-style functions are variadic templates over a plain-text function
  (`get_log`, `debug::printf`...). The one left is the run-time-check callback, whose type the C runtime gives.
- **Globals whose construction may throw** are function-local statics behind an accessor (made on first use);
  allocation failures don't count as an escaping exception (`IgnoredExceptions`: the game ends either way).
- File-local names live in **unnamed namespaces**; **`[[nodiscard]]`** marks results not to drop, and `(void)` the
  ones dropped on purpose; **enums** take the smallest type their values need, except where a value comes from
  outside as an int (the pad bindings' ini numbers); `std::numbers` only where a literal is exactly the constant
  (`DiffThreshold: 0`); the **ranges** algorithms and `std::erase_if` over iterator pairs.

No check is "off for now": each one is on with zero warnings or off with its reason.

## Tools

```
cmake --build out/build/<preset>          # also writes compile_commands.json
clang-tidy -p out/build/<preset> <file>   # naming and other checks
clang-format -i <file>
```
Visual Studio ships both (`VC\Tools\Llvm\x64\bin`).
