# Style guide

The code follows the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), with the
house rules below for what they leave open. `.clang-format` and `.clang-tidy` at the root enforce what tools can.

## Naming (standard-library style)

| What | Style | Example |
|---|---|---|
| Types: class, struct, enum, alias | snake_case | `rider_physics`, `script_value` |
| Functions and methods | snake_case | `tick_frame()`, `find_entity()` |
| Variables, parameters | snake_case | `frame_time`, `target_pos` |
| Non-static data members | snake_case + `_` | `speed_`, `frame_count_` |
| Constants (`constexpr` / `const`) | snake_case | `max_players` |
| Enumerators (`enum class`) | snake_case | `wait_reason::timer` |
| Namespaces | snake_case, = the folder | `maths::`, `script::` |
| Template parameters | PascalCase | `template <typename Value>` |
| Macros (rare) | ALL_CAPS, `THUG2_` prefix | `THUG2_ASSERT` |
| Files | snake_case, `.h` / `.cpp` | `rider_physics.cpp` |

- No type prefixes (C, S, E, I) and no Hungarian prefixes (m_, p_, g_, s_, k).
- Names say what a thing is or does; abbreviate only the usual (pos, dir, min, max, num, id).
- The game's own names - script commands, checksum names, strings - stay as the game spells them in strings and
  tables. External APIs (Direct3D, Win32, SDL, ImGui, GameSpy) keep theirs.

## Formatting

- `clang-format` with the root `.clang-format`: 4-space indent, 120 columns, `int* p`, includes grouped (standard
  library, then the project's).
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
- Where a rule can't be kept (for example exact x87 floating point), the code says so with a
  `// NOLINT(<check>)` naming the rule and a reason; the list of such places is kept short.

## Tools

```
cmake --build out/build/<preset>          # also writes compile_commands.json
clang-tidy -p out/build/<preset> <file>   # naming and other checks
clang-format -i <file>
```
Visual Studio ships both (`VC\Tools\Llvm\x64\bin`).
