# QB scripts in QuickJS

This optional standalone prototype translates a bounded binary QB subset into
readable JS generators and executes it in QuickJS with an explicit C++ host
bridge. Synthetic scenarios and an optional original-data helper implement
the first milestones in [the port plan](../docs/qb-js-plan.md). It does not execute QB bytecode at
runtime, link the archived C++ QB VM or change the x86 game's build.

## Build and test

Node 24+, CMake 3.25+, Ninja and a GCC/Clang C/C++20 toolchain are required for
development. Node runs the offline translator and tests; the C++ runtime has
no Node dependency. On Windows use MSYS2 UCRT64 GCC, not MSVC: official QuickJS
uses compiler extensions. The dependency is downloaded inside the build tree.

```sh
cmake -S scripting -B work/qb-js-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build work/qb-js-build --parallel 2
ctest --test-dir work/qb-js-build --output-on-failure
```

The default build downloads official QuickJS 2026-06-04 and checks SHA-256
`b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a`.
For offline development, CMake accepts `FETCHCONTENT_SOURCE_DIR_QUICKJS` pointing
to a previously verified extracted source tree. That override bypasses download
verification, so verify the archive yourself first. No dependencies are installed
globally. Only the core engine is linked; `quickjs-libc` and its OS modules are
not registered.

```sh
node tools/qbjs/translate.ts input.qb output.js
node tools/qbjs/emitter.ts input.qb output-format2.js
npm ci --ignore-scripts --no-audit --no-fund
npx tsc -p tools/qbjs/tsconfig.json
npx biome check tools/qbjs
```

Translator tests and fixture generation need only Node's built-in modules;
the existing npm dependencies provide development type/style checks.

## Supported QB subset

The original format-1 `translate.ts` accepts script definitions, first-line scalar defaults,
Integer/hex Integer, Float and Name literals, named/unnamed parameters,
`<parameter>` reads, `<...>` forwarding, local assignment, named calls, return,
numbered/plain EOLs and compiled if/else/endif. Conditions can be named calls or
a single parenthesized numeric scalar/parameter. Checksum-valued expressions
would dispatch commands in the original and currently reject explicitly.
Jump metadata is checked against the
parsed branches. IR and generated manifests preserve source labels, checksum
keys and original file offsets. Generation is deterministic.

The format-2 `emitter.ts` additionally accepts ordered global definitions and
aliases, byte strings/LocalStrings, pairs/vectors, structures/arrays, typed
expressions, local begin/repeat/break and structured compiled switch/case/default.
It preserves original operator priorities and grouped-label evaluation. Value
operations use `scripting/values.js`, which must be loaded through `Runtime::load`
before generated format-2 modules. The
[contract](../docs/qb-js-contract.md) records supported operations and reference
identity, numeric rounding, loading and control-flow boundaries. Format-2
definitions replace earlier symbols only during loading; format 1 still rejects
duplicate scripts. Mixing formats in one call stack is unsupported.

Float values retain their raw float32 bits, including negative zero; Integer
and Name remain distinct. Parameter merge follows CStruct::AddComponent's typed
replacement rule, including numeric type replacement and unnamed Name flags.
Calls evaluate arguments in the caller, overlay callee defaults and isolate
locals. Only explicit return fields merge back; root execution retains its own
parameters. A QB-call condition counts as true after the callee returns. Raw
numeric scalar bits determine expression truth, so float negative zero is true here.

`Runtime::install_frame_wait()` opts into only `Wait <Integer> frame(s)` or
`gameframe(s)` / `game`. A newly set wait counts in the same Update: Wait 0
continues immediately; Wait 1 resumes on the next Update; Wait 2 on the second
following Update. Negative integers wrap to uint32. Float arguments, millisecond,
second, real-clock and one-per-frame modes reject explicitly. The task runner
accepts explicit nondecreasing uint32 frame numbers; duplicate updates to a task
in the same frame do nothing. Skipped frames do not synthesize updates.

Random operations, dynamic command names, cached native pointers, member dispatch, events,
interrupts, spawning and engine-wide scheduling remain unsupported. Unsupported
input is rejected rather than translated into guessed JS semantics. Limits are
4 MiB input/source, 100,000 IR nodes, 64 nested conditional blocks, 4,096 fields,
32 nested calls, 256 retained tasks and 10,000 statement/call fuel by default.
Format 2 also charges value work to execution fuel, limits value nesting to 128,
local loop nesting to ten and generated JS to 16 MiB. The C++ loader caps each
loaded JS source at 4 MiB. Cross-call loop breaks, precompiled QB images and
arbitrary pre-case switch statements are not supported.

## C++ boundary and lifetime

`thug2::js::Runtime` loads generated JS, binds checksum-named C++ callbacks,
starts tasks, updates them at explicit frames and cancels retained tasks.
`Value` transports type (1/2/13) and raw uint32 bits; ordered `Field` vectors
transport parameters and output fields. A host returns a condition and optional
fields/frame wait. Unknown commands fault with the original QB source site.
Hosts must return; JS interrupt limits cannot preempt C++ callbacks.
Containers can execute inside format-2 JS, but the native host/input/snapshot
bridge remains scalar-only. Crossing that boundary with rich values explicitly
fails; it never silently flattens or drops fields.

Load all modules before starting tasks. Defaults initialize on the first update.
Retained tasks, including finished/faulted ones, must be canceled to release
capacity. Cancel drops the generator; on-exit script semantics are not yet
implemented. Loading into a runtime with retained tasks and callback reentrancy
are rejected. Format-1 duplicate definitions are also rejected. Loading is not transactional: discard a
runtime after a failed load. This API is single-threaded.

The engine defaults to a 16 MiB memory cap, 512 KiB JS stack cap (4 MiB under ASan) and 1,000
QuickJS interrupt checkpoints per C++ entry. These are bounded execution tools,
not a complete security sandbox or a gameplay scheduler. Fatal QuickJS allocation
or execution exceptions can escape as C++ errors; discard the runtime after
such an update failure. Interrupt exhaustion, host `std::bad_alloc` and OOM
marshaling host outputs make the runtime terminal. An ordinary JS OOM caught
and unwound by the scheduler is a task fault and permits runtime reuse, as do
normal QB/host faults. Loading is not transactional, even for recoverable errors.

## Validation and provenance

Four public CTest suites run all `tools/qbjs/*.test.ts` tests, the legacy
16-script scalar C++ bridge regression, QuickJS value/generator/scheduler
assertions, and a separate native format-2 integration driver. They check malformed/truncated input, jump
targets, deterministic code, typed bits, calls/defaults/returns, isolation,
spread, both host-condition branches, QB-call conditions, frame waits, duplicate
frame updates, host errors, source offsets, fuel, recursion, cancellation,
reentrancy and QuickJS memory/interrupt bounds. The value suite includes at least
175 assertions. Release and Debug ASan/UBSan are checked locally on macOS ARM64.
The CI matrix also builds these public targets on Linux, Intel macOS and Windows
GCC; results of earlier bridge checks do not establish the expanded suites' outcomes.

No original game file or EXE is needed for public tests. To enable the optional
fifth CTest suite using your local English game data:

```sh
cmake -S scripting -B work/qb-js-build -DQB_GAME_DATA_DIR="/path/to/Game/Data"
cmake --build work/qb-js-build --parallel 2
ctest --test-dir work/qb-js-build --output-on-failure
```

It translates the complete original `scripts/game/misc/level_flags.qb` and runs
`ResetLevelFlags` through QuickJS and a concrete bounded C++ `UnSetFlag` host.
Six scenarios compare flags across 21 levels and their start copy, verifying 64
ordered calls and the expected low-64-bit reset while preserving all other bits.
This is a test career state without graphics or a game instance. RNG is not
needed. The exact input fingerprint, host subset and negative checks are in the
contract. Game files are read only; generated game-derived JS stays in the
local build tree and must not be published. Clear `QB_GAME_DATA_DIR` to disable
this target; public CI never sets it.

Format/behavior references are upstream `src/game/script/qb.*`, `struct.cpp`,
`cscript_exec.cpp` and [script-runtime.md](../docs/script-runtime.md).
The translator, typed IR, generator runtime, bridge and synthetic tests are
new code, marked NOT ORIGINAL. Third-party QuickJS retains its MIT license;
see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
