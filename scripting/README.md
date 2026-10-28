# QB scripts in QuickJS

This optional standalone prototype translates a small binary QB subset into
readable JS generators and executes it in QuickJS with an explicit C++ host
bridge. It is the first synthetic milestone in
[the port plan](../docs/qb-js-plan.md). It does not execute QB bytecode at
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
npm ci --ignore-scripts --no-audit --no-fund
npx tsc -p tools/qbjs/tsconfig.json
npx biome check tools/qbjs
```

Translator tests and fixture generation need only Node's built-in modules;
the existing npm dependencies provide development type/style checks.

## Supported QB subset

The translator accepts script definitions, first-line scalar defaults,
Integer/hex Integer, Float and Name literals, named/unnamed parameters,
`<parameter>` reads, `<...>` forwarding, local assignment, named calls, return,
numbered/plain EOLs and compiled if/else/endif. Conditions can be named calls or
a single parenthesized numeric scalar/parameter. Checksum-valued expressions
would dispatch commands in the original and currently reject explicitly.
Jump metadata is checked against the
parsed branches. IR and generated manifests preserve source labels, checksum
keys and original file offsets. Generation is deterministic.

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

Globals, containers, expressions with operators, switch/loops, random operations,
dynamic command names, cached native pointers, member dispatch, events,
interrupts, spawning and engine-wide scheduling remain unsupported. Unsupported
input is rejected rather than translated into guessed JS semantics. Limits are
4 MiB input/source, 100,000 IR nodes, 64 nested conditional blocks, 4,096 fields,
32 nested calls, 256 retained tasks and 10,000 statement/call fuel by default.

## C++ boundary and lifetime

`thug2::js::Runtime` loads generated JS, binds checksum-named C++ callbacks,
starts tasks, updates them at explicit frames and cancels retained tasks.
`Value` transports type (1/2/13) and raw uint32 bits; ordered `Field` vectors
transport parameters and output fields. A host returns a condition and optional
fields/frame wait. Unknown commands fault with the original QB source site.
Hosts must return; JS interrupt limits cannot preempt C++ callbacks.

Load all modules before starting tasks. Defaults initialize on the first update.
Retained tasks, including finished/faulted ones, must be canceled to release
capacity. Cancel drops the generator; on-exit script semantics are not yet
implemented. Loading into a runtime with retained tasks, duplicate definitions
and callback reentrancy are rejected. Loading is not transactional: discard a
runtime after a failed load. This API is single-threaded.

The engine defaults to a 16 MiB memory cap, 512 KiB JS stack cap and 1,000
QuickJS interrupt checkpoints per C++ entry. These are bounded execution tools,
not a complete security sandbox or a gameplay scheduler. Fatal QuickJS allocation
or execution exceptions can escape as C++ errors; discard the runtime after
such an update failure. Normal QB/host faults are terminal task snapshots.

## Validation and provenance

Two CTest suites run five translator tests and a C++ integration executable over
16 synthetic script definitions. They check malformed/truncated input, jump
targets, deterministic code, typed bits, calls/defaults/returns, isolation,
spread, both host-condition branches, QB-call conditions, frame waits, duplicate
frame updates, host errors, source offsets, fuel, recursion, cancellation,
reentrancy and QuickJS memory/interrupt bounds. All pass in Release and
ASan/UBSan on macOS ARM64. The CI matrix additionally builds Linux, Intel macOS
and Windows GCC; local results do not establish those jobs' outcomes.

No original game file or EXE is needed for these tests. An original-data helper
scenario is the next milestone, not demonstrated by this synthetic test.
Format/behavior references are upstream `src/game/script/qb.*`, `struct.cpp`,
`cscript_exec.cpp` and [script-runtime.md](../docs/script-runtime.md).
The translator, typed IR, generator runtime, bridge and synthetic tests are
new code, marked NOT ORIGINAL. Third-party QuickJS retains its MIT license;
see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
