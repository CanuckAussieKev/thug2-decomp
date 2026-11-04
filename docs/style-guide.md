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
- A file includes what it uses (standard headers too: `<cstdint>`, `<string>`, `<array>`... never relied on through another
  header) and nothing else - `misc-include-cleaner` checks it. An include the check can't see a use of keeps
  `// IWYU pragma: keep - <why>`; `<new>` is left to the file (placement new).
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
  base/       the OS-free helpers under everything: core (checksums, formatting, text, checked indexing, the enum and
              pointer helpers), maths, two containers (lists), the bit stream and BPS patches (util) (library
              thug2_base)
  engine/     memory, lists, files, assets, script, objects, renderer, 2D, audio, sound, network, pad input
  game/       skating, the rider, goals, parks, saves
  frontend/   menus and screens
  editor/     the level editor
```

`engine/` doesn't include `game/`, `frontend/` or `editor/`; `formats/` and `wire/` include nothing of the game but
`base/` (they build as libraries of their own, on `thug2_base`); `base/` includes nothing but the standard library
and GSL. One folder is one namespace (formats and wire
keep the namespaces their headers had). The layers and their rules: docs/architecture.md.

## Code

- Prefer what the Guidelines prefer: `enum class`, `constexpr`, `nullptr`, RAII and `std::unique_ptr` for ownership,
  `std::array` / `std::span` over raw arrays, `std::bit_cast` over type punning, explicit readers / writers for
  file and network formats.
- Standard C++ only, buildable by MSVC and GCC: no inline assembly (the original's x87 maths is plain `<cmath>` -
  results may differ from the original's in the last bits, which is accepted), no compiler-specific keywords outside
  `src/base/core/compiler.h`.
- Conditions say what they test (readability-implicit-bool-conversion): a pointer stands alone (`if (p)`, `if (!p)` -
  ES.87), everything else is compared: `n != 0`, `(flags & kBit) != 0U`, `*text != '\0'`, a Win32 BOOL `!= FALSE`,
  an SDL_bool `!= SDL_FALSE`, a flag enum `core::any(flags & Flags::kA)`; a bool counted or stored as a number is
  `static_cast<int>(b)`.
- Where a rule seems not to fit, the code changes so that it does; no `NOLINT` (see "Exceptions").

## Exceptions

`.clang-tidy` enables the Guidelines' checks (`cppcoreguidelines-*`) with `bugprone-*`, `misc-*`, `modernize-*`,
`performance-*`, `readability-*` (taken whole) and the clang static analyzer (`clang-analyzer-*`), and runs them with
zero warnings.
**There is no `NOLINT` in `src/`** (`npm test` fails on one: `tools/no_nolint.test.ts`). A place that doesn't keep a
check is changed so that it does; where the original's behaviour can't be kept without what a check flags, the check is
configured by its own options or switched off in a `.clang-tidy`, the reason written beside it - those are the project's
exceptions, all in one place:

- **Off** (`.clang-tidy`): `readability-magic-numbers` (an alias of `cppcoreguidelines-avoid-magic-numbers`, which is
  on with the same options), trailing return types (a style), recursion (the interpreter and the trees recurse by
  design), `misc-predictable-rand` (the game's numbers are the C runtime's `rand()` sequence, as the original's),
  `cppcoreguidelines-pro-type-static-cast-downcast` (every downcast goes through `core::down_cast`, below),
  `clang-analyzer-optin.core.EnumCastOutOfRange` (flag enums hold combinations no enumerator names, and a value from
  a file or the wire that an enum doesn't list stays representable - "Enums", below).
  `src/tests/.clang-tidy`: the reference programs call the C runtime's `atoi` / `sscanf_s` / `snprintf` to compare
  with, so the string-to-number and vararg checks are off there.
- **Configured** (`CheckOptions`): public data is named as plain data, without the member suffix (the format
  records' and messages' fields; `PublicMemberCase`); the libraries' own names (`SDL_*`, `sockaddr`, the GameSpy
  SDK's host-name globals) by the naming check's `IgnoredRegexp`s; `THUG2_MSVC_WARNING` / `THUG2_RETURN_ADDRESS`
  (`base/core/compiler.h`: a pragma inside a statement, the caller's return address - neither can be a function) by
  the macro check's `AllowedRegexp`; the game's time seed (`DisallowedSeedTypes`); the x87 reference's locals that
  inline assembly writes (`AsmFloat`... in `src/tests/maths_check.cpp`: clang-tidy can't see an `__asm` block's
  writes; `AllowedTypes`); `readability-function-cognitive-complexity` at a `Threshold` of 50, not the default 25,
  with `IgnoreMacros` (the original's long switch-and-loop functions and the editor's UI functions are split into named
  steps down to 50; most of the functions between 26 and 50 are one loop of the original or one ImGui panel, and
  splitting them further would scatter a decompiled routine without making it clearer; the conditionals inside GSL's
  `Expects` and the `THUG2_*` macros are not the reader's, so not counted); `readability-simplify-boolean-expr`
  without De Morgan rewrites (`SimplifyDeMorgan: false`: `!(a < b)` is not `a >= b` for floats - a NaN fails every
  comparison); `readability-implicit-bool-conversion` with `AllowPointerConditions` (ES.87); the shortest names
  `readability-identifier-length` accepts (two letters, plus the coordinates `x y z w u v` and the operands `a` / `b`);
  and the options listed in `.clang-tidy` for the other checks.
- **What changed instead of an exception**, for the record: the original's integer divisions keep their quotient in
  a named integer; bit-for-bit float compares are `core::has_bits` / `same_bits` (`base/core/same_bits.h`);
  repeated branches are merged; assignments and counts come out of conditions; switches say `default: break`;
  `value + 0.5` truncations are `maths::round_non_negative`; Direct3D's interfaces come from `<d3d9.h>` (a forward
  declaration of a COM interface is a second declaration of the SDK's class); the C runtime heap is
  `crt::alloc_block` / `dealloc`; configuration macros (`DIRECTINPUT_VERSION`, Winsock's deprecation switch) are
  CMake definitions; plain enums held as integers (table indexes, values fixed by a file, a script, the wire or an
  SDK, the original's integer fields used as numbers) are named `constexpr` constants with an alias for the field's
  type, and the closed sets among them are scoped enums (below);
  the original's whole-object clears and copies of objects that aren't trivially copyable are `raw::zero_object` /
  `raw::copy_object` (`base/core/raw_view.h`, with the other named views).
- **The script values' cells** (`engine/script/cell.h`): one 32-bit word, read as the kind its holder says
  (`std::bit_cast`, `cell::pointer`) - a boundary file for the layering rule, like the QB readers.
- **No `reinterpret_cast`, no integer-to-pointer cast** anywhere in `src/` (both checks on). Bytes from outside
  are read by the boundary layer's readers (`formats/`, `wire/`); casts between related classes are `static_cast`;
  what is left of the original's ways of reaching memory has a name, so it can be counted and removed:
  `cell::of` / `cell::pointer<T>` / `cell::address` (`base/core/pointer_cell.h`: a pointer kept in a 32-bit
  cell - script values, callbacks' user words, addresses used as ids), `raw::as<T>` / `raw::field_at<T>` /
  `raw::bytes` / `raw::chars` / `raw::owner_of<T>` (`base/core/raw_view.h`: one of the original's in-memory
  layouts read through another type or at a byte offset), `memory::bits_as<T>` (a field's bits as another type of
  its size), `code_address` / `function_at<F>` / `crt::library_function<F>` (`platform/`: code addresses).
  Code outside the boundary uses none of them: a field gets its member, a pointer its type (`npm test` fails on any
  use outside the boundary - `tools/layering.ts`). A script value is a `script::Cell`, read by its
  holder's kind (`value.structure()`, `value.word()`...); a missing script parameter's default is written out (the
  original's reads of an argument's stack slot are 0 for a name, on for a flag, 0 for a number). Byte layouts that stay (file records used
  in place, vertex buffers) are read through the boundary's accessors instead: `collision_format::FaceTable`
  (`formats/`), `gpu::VertexView` / `VertexWriter` (`gpu/`).
- **Downcasts** go through `core::down_cast<Derived>(base)` (`base/core/down_cast.h`): code that has checked an
  object's type tag (or asked a manager for an object of a type) converts the base pointer with it; Debug builds
  assert the object really is a Derived (a class's own `is_instance(const Base&)` if it has one, else `dynamic_cast`).
  The `static_cast` inside it is the one downcast in `src/` (so the static-cast-downcast check is off).
- **Arrays are `std::array`** (or `std::vector` / `std::string` / `std::span`), tables `std::to_array` (rows of a
  named struct, never an anonymous one); a pointer to the elements is asked for with `.data()` where a C API or a
  pointer-taking function wants one - never passed through `...` (printf-style calls take `.data()`). A C API
  struct's own char array (`WIN32_FIND_DATA::cFileName`, `ImFontConfig::Name`...) is passed with `std::data()`.
  **Array parameters are `std::span<T, N>`** (a fixed extent: the caller passes its `std::array`; a pointer needs an
  explicit span). Every subscript goes through `core::at(container, index)` (`base/core/checked_index.h`:
  `gsl::at` with any integer or plain-enum index; constant indexes too, or `std::get<N>` / `.front()` / `.back()`):
  out of range ends the game with a report instead of reading past the end. `maths::Vec4` / `Mat4` check their own
  `operator[]` (the unchecked-container check's `ExcludeClasses`). Locals of `std::array` type are value-initialised
  (`{}`). All five array / index checks are on.
- **Ownership**: an owner is a smart pointer by type - `std::unique_ptr<T>` for objects made with `new`
  (`std::make_unique`), `memory::HeapUnique<T>` (`engine/memory/mem.h`) for objects constructed in a block from the
  game's heaps (`memory::construct_in`: its destructor, then the block given back). The singletons' globals are
  owners and so are members whose class frees what they hold (`std::unique_ptr` / `StructPtr` members); an accessor
  or getter returns `.get()` - never the owner itself. Teardown in the original's
  order goes through `memory::destroy_then_clear` (the object destroyed while its owner still points at it); a
  custom teardown takes the object out with `release()`. `memory::construct_in` returns `gsl::owner<T*>` (a factory:
  the caller ends the object). Script structures and arrays: `script::new_struct()` / `free_struct()` (and the array
  pair), `script::StructPtr` / `ArrayPtr` for a scope's or member's own - never a per-file copy of the pool
  factory. `gsl::owner<T*>` (Microsoft GSL) marks the raw owners left where a smart pointer can't be used yet: a
  holder of what it made (a local, a member, a function's result) is spelt `gsl::owner<T*>`; one made with `new` or
  a cast is made by a call instead - `memory::make_owned<T>(args)` (`new T` / `new T(args)`), `make_owned_value<T>()`
  (`new T()`), `memory::adopt<T*>(block)` (an allocator's void*) - held with `auto`, which keeps the owner;
  `memory::take_unique<T, Deleter>(made)` takes a just-made owner into a `std::unique_ptr`, `memory::let_go(owner)`
  lets go of one without freeing it (where the original handed it on or leaked it). A function that makes something
  returns `gsl::owner<T*>`; one that hands back what a list, table or object keeps returns a plain pointer.
- **What containers own**: lists, tables, rings, pools and records free their elements, so the element slots are plain
  pointers (links, `void*` values, template arguments can't carry `gsl::owner`). The container's own code gives an
  element up where it frees it: `gsl::owner<T*> owned = memory::owned_element(element); delete owned;` (or passes
  `memory::owned_element(element)` to the freeing function). Records that merely point at something (a sound's wave
  slot, an instance's source mesh) aren't owners. A delete-expression takes a declared owner, never a call's result.
- cppcoreguidelines-owning-memory is on (0 warnings).
- **Sets of objects known by their address** (what the original kept in a KeyMap keyed by each object's address) are
  `lists::ObjectSet<T>` (`base/lists/object_set.h`): exact pointer keys, in insertion order, a walk that may drop the
  object it visits.
- **Pools** (`memory::PoolPair`, `SlabBank`) are sized by `sizeof` of what they hold; a pool's `carve` refuses an
  object bigger than its items (a class that grew past the original's byte size ends the game with a report).
- **The rule of five** is kept as the Guidelines' C.21 allows: a defaulted destructor alone, and classes that copy
  but don't move; a class with a destructor of its own deletes its copies unless it really copies.
- **Constructor bodies in the original's order**: members are initialised in the list or by a default member
  initialiser; where the original's order matters (a value read after a call that sets it, allocations made in
  another order than the members are declared) the constructor's later part is a named private step
  (`PileUp::open_base_list`, `StageBests::make_longest_records`) or the value is computed from what the initialiser
  already has.
- **Members left unset** by a constructor have a default member initialiser (`{}`), as do locals of plain record
  types (`Vec4 box{};`): zero where the original left whatever the memory held.
- **The GameSpy SDK** takes non-const `char*` where it only reads (and copies what it keeps): it gets copies
  (`SdkText` in `platform/gamespy.cpp`), never a `const_cast`.
- Narrowing conversions are written as `static_cast` where the original narrows.
- **The original's unchecked pointers** (the static analyzer's null paths): where the original goes on through a
  pointer that can be null - a failed allocation used without a check, a lookup that found nothing - the use is
  preceded by `Expects(pointer != nullptr)` (GSL: the game ends there with a report, where the original crashed;
  placed at the statement that goes through the pointer, so it ends on the same paths and no earlier). The original's
  integer divisions by a count that can be 0 state it the same way. A one-line helper taking such a pointer checks it
  itself. Texts copied into the game's buffers go through `text::copy_into` / `append_into` (a text that doesn't fit
  ends the game) or, for texts from outside the game (other players, the online service), `text::copy_cut` (cut to
  fit) - `base/core/str.h`; no `strcpy` / `strcat`.
- **`const` wherever nothing changes** (`misc-const-correctness`): locals and what pointers point at. Placement new
  goes through `memory::construct_in` / `construct_value_in` / `construct_global_in` (`engine/memory/mem.h`) and the
  original's unbounded inline copies through `text::copy_unbounded` / `append_unbounded` (`base/core/str.h`), so
  the check sees the writes. `src/tests/maths_check.cpp`'s x87 reference declares the locals its inline assembly
  writes with `AsmFloat` / `AsmDouble` / ... (the check's `AllowedTypes`); the ones it only reads are const.
- **Mixed-sign comparisons** say which conversion they make: `std::cmp_less` and its kin where the answer is the same
  for every value (an int against a narrower unsigned, a container's `size()`), `ints::as_signed` /
  `ints::as_unsigned` (`base/core/ints.h`) where the original compares a 32-bit count as an int or an int as a
  count - the same bits, so a negative index still reads huge.
- **Signed bytes** widened on purpose are written `int{byte}`; **text read as a number** goes through
  `text::to_int` / `text::to_double` (`base/core/parse_number.h`: atoi's / atof's answers, over
  `std::from_chars`; `thug2_parse_check` compares them with the C runtime's); a `T**` passed as `void*` is cast
  explicitly.
- **No C-style variadics of our own**: printf-style functions are variadic templates over a plain-text function
  (`get_log`, `debug::printf`...). The one left is the run-time-check callback, whose type the C runtime gives.
- **State, not globals** (`cppcoreguidelines-avoid-non-const-global-variables`): what the original kept in globals is a
  subsystem's state record, reached through one accessor backed by a function-local `core::NoDestroy<T>`
  (`base/core/no_destroy.h`):

  ```cpp
  SoundState& sound_state()
  {
      static core::NoDestroy<SoundState> state;
      return state.get();
  }
  ```

  - Made on first use, value-initialised: it starts as the original's zeroed (or initialised) globals did, before
    anything reads it, with no static-initialisation order between files. Never destroyed: the game ends by
    `sys::fast_exit` / `crt::exit_fatal`, and nothing is torn down under its other threads. What the original tore
    down (a level unload, a module's shut-down, a device reset) is done where the original did it, in its order - a
    reset of the state's members, or the manager owner's `destroy_then_clear`.
  - A trivially destructible record is held directly with a constexpr constructor - a constant-initialised static, no
    guard; any other is constructed in place in static storage (the function-local static's guard). Its address is
    stable, so members may be registered elsewhere. Static storage, as the globals were: the game's heaps don't change.
  - What one file uses is that file's record in its unnamed namespace: a plain struct (data members only, `name_`, its
    comment naming the original's addresses where known), private by linkage. What several files share - or other
    subsystems reach - is a **state class** of the subsystem with private members and the methods its users need
    (`render::renderer()`, `render::light_sets()`, `online::net_game_state()`, `script::script_state()`,
    `park_editing::park_editing_state()`, `objects::objects_shared()`...), one header each, its members grouped under a
    comment naming the file that uses them. A getter for what is only read, a `set_` setter for what is assigned, a
    reference accessor only for what is used in place (a container, an owner, an out-parameter), and a named operation
    where one reads better (`renderer().count_draw(triangles)`, `save_view()` / `restore_view()`). Classes stay
    cohesive: a subsystem with unrelated parts gets one class per part (gfx: the renderer, the lights, the 2D layer,
    the effects, the views, the mesh pass, the engine's parts), and the hot per-frame state stays in trivially
    destructible classes (constant-initialised: no guard). The older single-record folder headers (`sound/sound_state.h`,
    `audio/audio_state.h`, `render/collision_state.h`) follow the same rule. A file's single global is its accessor's
    function-local static (`core::NoDestroy` when the type has a destructor). The state of the original's start-up
    initialisers (`sys::run_startup_initialisers`) is still built there, in that order, over its record.
  - A manager's owner (`std::unique_ptr` / `memory::HeapUnique`) is a member of its record; the record's file defines
    the getter the other files call (`pad_input::get_input_manager()`, `assets::get_main_loop_manager()`). Raw owners
    become `std::unique_ptr` members: freed with `memory::destroy_then_clear` where the original deleted them, given up
    with `memory::let_go` where the original left them.
  - What other subsystems use is a method of the state class or a function in the defining file's header (where the
    `extern` was) - no `bits_as` views of another subsystem's state. A singleton's existing accessor (`sole()`,
    `node_manager_instance()`, `gfx::tod_manager()`) holds the storage itself, out of the header into the defining
    .cpp. No file keeps a one-line wrapper of another file's state.
  - Hot paths take the reference once (`auto& voices = voice_state();`) rather than calling the accessor per element.
  - Tables that never change are `constexpr` / `const` (`k` names), not state; a cache of a pure result is a
    function-local static of its one function.
  - Globals whose construction may throw are behind an accessor the same way; allocation failures don't count as an
    escaping exception (`IgnoredExceptions`: the game ends either way).
- **Private data** (`misc-non-private-member-variables-in-classes`, on - with `IgnoreClassesWithAllMemberVariablesBeingPublic`,
  the same rule as its cppcoreguidelines alias: a class mixing public and private data is flagged): a class with an
  invariant keeps its data private; what other components, script commands and helpers read is a const getter, what they assign
  a `set_` setter (`assign_` where a script member function already has the `set_` name), what they use in place a
  reference accessor beside the const one, and a lookup a method (`MoveExecutor::mapped_slot()`) rather than handing
  out an owning pointer. Members keep their order and size (pools are sized by `sizeof`). A value type with no
  invariant keeps all its data public, with no trivial getters / setters (C.2, C.131): the maths types (`Vec4`,
  `Mat4`, `QuatRotation`, `BBox`, `Capsule`, `Seg`) keep their operators, `operator[]` and constructors as members
  (the language requires them there); a plain record's other helpers are free functions taking the record (C.4 -
  `copy_from(hit, other)`, `get_time(message)`; a nested record's after its outer class - `best_two(leader, count)`),
  and records built with designated initialisers (the editor's painters and undo steps, the front end's draw
  context) stay aggregates. Which is which: a record whose member functions construct, tear down
  or keep state between calls (a list walker, a recorder, a ring) is a class; one whose functions only read or copy its
  fields is a plain record. Private members take the `_` suffix (so the accessor of `next_` is `next()`). A file
  helper that takes the class by pointer is a private member function where the class is its natural owner
  (`SkaterPhysicsModule::onto_rail`); a one-line wrapper over a member is inlined at its calls. An owner is not
  handed out: the getter is a view (`static_cast<T*>(owner_)`) and the class deletes it itself
  (`delete_observer_task()`). The original's `offsetof` checks of private members are in the class's private
  `static void check_layout()` (a base's members in the base's own check).
- **Enums**: the values of one closed thing (kinds, states, ids, flags) are an `enum class` with the underlying type
  the value always had, so layouts and code don't change; a value from a file or the wire that the enum doesn't list
  stays representable - the boundary layer converts the byte once as it reads it and writes it with
  `core::to_underlying`. Bit sets are flag enums (`enable_flag_operators` beside them gives |, &, ~; a test reads
  `core::any(flags & Flags::kX)`) - `base/core/enum_ops.h`. A switch over an enum lists every case and has no
  `default:` then. Checksums, sizes, offsets, tunings and values used as numbers stay `constexpr` constants.
- **Named constants**: a number that means something has a name, defined once by its owner - a file's in its unnamed
  namespace, a subsystem's in that subsystem's header, a value two layers share in the lower one (no central constants
  file). Which header holds which kind: [constants.md](constants.md).
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
