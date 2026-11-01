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
  platform/   window, input devices, timers, crash reports, settings
  engine/     core, maths, lists, files, assets, script, objects, renderer, 2D, audio, sound, network, pad input
  game/       skating, the rider, goals, parks, saves
  frontend/   menus and screens
  editor/     the level editor
```

`engine/` doesn't include `game/`, `frontend/` or `editor/`. One folder is one namespace.

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
loops (magic numbers, pointer arithmetic, `reinterpret_cast` views, C arrays in layouts, non-const globals, printf
formatting, trailing return types and a few more). Everything else runs with zero warnings; the places that can't
keep a check carry a `NOLINT` with their reason:

- **Plain records** of file and network formats (`*_format.h`, the message structs, the format checks' own records)
  name their fields as plain data, without the member suffix (`NOLINTBEGIN(readability-identifier-naming)` around
  each record).
- **Outside names**: forward declarations of SDL, Win32 and Winsock types, the GameSpy SDK's globals, Winsock's
  `_WINSOCK_DEPRECATED_NO_WARNINGS`; `memory::owner` is named as the Guidelines' `gsl::owner` it stands for.
- **The original's behaviour, kept as written**: integer divisions where it divides integers, bitwise compares of
  floats (`memcmp`), repeated branches, assignments and increments inside conditions, switches that ignore other
  values, its random seed and `rand()`.
- **The scene's collider arrays** (`StaticCollider[n]`): indexed as arrays of exactly that type.
- **COM interfaces** (forward-declared Direct3D types): released, never deleted, so no virtual destructor.
- **The C runtime's allocator** where a library or the original's file reader wants `malloc` / `free`.
- **Macros**: libraries' configuration macros (`DIRECTINPUT_VERSION`) and `src/platform/compiler.h`, the one place
  for compiler specifics.
- Checks still being worked through are listed as such in `.clang-tidy` (narrowing conversions, member and variable
  initialisation, the rule of five, the script values' unions, plain enums, internal linkage, `const_cast`).

## Tools

```
cmake --build out/build/<preset>          # also writes compile_commands.json
clang-tidy -p out/build/<preset> <file>   # naming and other checks
clang-format -i <file>
```
Visual Studio ships both (`VC\Tools\Llvm\x64\bin`).
