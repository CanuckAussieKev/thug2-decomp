# Optional QB JavaScript runtime

The project's offline TypeScript tools translate QB into JavaScript generators.
This optional C++ library runs them in quickjs-ng with explicit typed native
callbacks. The hooked DLL and standalone game still use their existing QB path;
this target does not change gameplay or rendering.

## Build and test

Requires CMake 3.25+, C++20 and a C11 compiler. GCC, Clang and MSVC are supported.
The build downloads quickjs-ng v0.17.0 with a pinned SHA-256. Only its core is
linked: no quickjs-libc filesystem/OS modules are exposed. Node 24+ is required
only when BUILD_TESTING is enabled.

```sh
npm ci --ignore-scripts --no-audit --no-fund
npx tsc -p tools/qbjs/tsconfig.json
npx biome check tools/qbjs tools/qb_checksum.ts
cmake -S scripting -B work/qb-js-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build work/qb-js-build --parallel 2
ctest --test-dir work/qb-js-build --output-on-failure
```

With MSVC, configure using `-G "Visual Studio 17 2022" -A x64`, build with
`--config Release`, and pass `-C Release` to CTest. A library-only configure
with `-DBUILD_TESTING=OFF` requires no Node. CI also runs Windows GCC using the
actual `setup-msys2` installation directory, rather than a hardcoded path.

The optional original-data check reads the complete local
`scripts/game/misc/level_flags.qb` module and executes its ResetLevelFlags
through a bounded native career-flag host. No game assets are in public tests:

```sh
cmake -S scripting -B work/qb-js-build -DQB_GAME_DATA_DIR="/path/to/Game/Data"
cmake --build work/qb-js-build --parallel 2
ctest --test-dir work/qb-js-build --output-on-failure
```

## Translation and helpers

`node tools/qbjs/reader.ts INPUT.qb OUTPUT.ir.json` produces format-2 IR;
`node tools/qbjs/emitter.ts INPUT.qb OUTPUT.js` emits generators. Native Runtime
embeds numeric.js, random.js, values.js and runtime.js once before loading any
module. Generated production modules contain no helper copies. Manually loading
values.js or numeric.js again rejects, so loaded scripts cannot silently switch
their helper implementation. Test-only helper-sources.ts initializes isolated
JS test contexts without replacing installed helpers.

Supported format-2 syntax includes typed scalars/raw strings/coordinates,
structures, homogeneous literal arrays (numeric promotion), globals/aliases,
parameter spread, expressions, switch, loops, calls/defaults/returns and random
values. Completed expression results become owning values; intermediate aliases
remain references. Anonymous structure expressions flatten into argument lists.
Empty operands propagate an empty expression: conditions are false and absent
arguments/assignments are omitted. QScript names are values in expressions and
comparisons; they are never executed merely by comparison. Literal near-zero
`angles` argument vectors are omitted as AddParameter does; ordinary assignment
and expression-result vectors retain their separate behavior.

Conditional NOT (0x39) is currently unsupported in both translator formats;
object-qualified/dynamic commands, expression C-function calls, cached native
pointers, random command blocks and two-pass random arrays are also unsupported
in this target. Unsupported input rejects with its original QB offset. These
limits must not be interpreted as support for the full game script corpus.

## Native API and lifetime

`Runtime::bind/start/update` retain the format-1 scalar transport: Value stores
type 1/2/13 and raw uint32 bits. Native format-2 starts reject immediately with
an actionable error, before a task is created. Use `bindRich/startRich/updateRich`
with RichValue, RichParameters, RichHostResult and RichSnapshot for format-2.
Rich values transport raw Integer/Float/Name bits, String/LocalString bytes,
Pair/Vector words, ordered Structures and homogeneous Arrays. Tags 20/21 reject
explicitly at any depth; no hidden reference-to-copy conversion occurs at this
boundary. Returned models and saved callback arguments outlive cancellation and
Runtime destruction, with no JSValue handles in the public model.

A task keeps its native transport mode. A mismatched update rejects before JS
runs or the frame advances. Both modes can coexist, but each host callback must
match its task. Each checksum has one slot: the latest scalar/rich registration
replaces either earlier binding. Defaults and typed merge semantics live in JS;
the codec only transports the representation. Host result conditions are bools,
outputs are merged before an optional suspension, and QB-script calls count as
true independently of return fields. A missing command or data-symbol command
returns false, matching ExecuteCommand; a registered host exception faults the
task with its QB site. Native callbacks must return; JS limits cannot preempt C++.

Load modules before starting tasks and cancel retained tasks (including finished
or faulted ones) to release capacity. Loading with retained tasks and callback
reentrancy reject. This API is single-threaded. Loading is not transactional;
discard a Runtime after a partially failed module load.

## Waits, budgets and failures

`install_frame_wait()` installs the scalar host and `installRichFrameWait()` the
rich equivalent. Only `Wait <Integer> gameframe(s)` / `game` count updates.
`frame`/`frames` mean N times approximately 16.67 milliseconds in the game;
they reject here, as do seconds/realtime/one-per-frame waits, until timed clocks
are implemented. Wait 0 resumes within the same update. Frame arguments are
monotonic observation ticks; duplicate observations of one frame do not rerun
code, consume waits or refill/resume execution.

Fuel is a per-resume budget selected at start (default 10,000, maximum 1,000,000),
refilled before each distinct update. A long-running yielding script does not
exhaust a lifetime allowance. Statement and value operations remain bounded.
Field construction uses a typed insertion-order index and linear work charging;
4096-field literals fit the operation limit, though callers must supply enough
execution fuel for their data. Depth is capped at 128 and per-operation work at
250,000 nodes. Rich transport additionally caps aggregate payload at 4 MiB,
including field CRCs, and strings/arrays at 250,000 bytes/elements.

Runtime defaults are 16 MiB heap, 512 KiB JS stack (4 MiB under ASan) and 1000
interrupt checkpoints per C++ entry. An update interrupt discards only that
runaway generator, explicitly restores scheduler state and returns a fault;
other tasks remain usable. Uncaught engine OOM/stack overflow, native bad_alloc
while executing/marshaling and host output allocation failures are terminal.
Input validation/allocation failure before task creation permits reuse. Ordinary
host/data errors are recoverable task faults. Native snapshot codec validation
throws after draining pending exceptions; it never silently drops rich fields.
These are bounded execution tools, not a complete security sandbox.

## Numeric profile and validation scope

Both values.js and random.js use numeric.js. Runtime arithmetic models gameplay
x87 precision 24 (FPCW 0x007f), with explicit nearest-even rounding at each
arithmetic instruction and float32 stores; extended exponents are retained until
the store. Offline 53/64 precision profiles are explicit helper arguments and
are not the gameplay default. Nonfinite arithmetic remains outside the verified
subset. See [qb-numeric-precision.md](../docs/qb-numeric-precision.md).

CTest covers the reader/emitter, shared numeric/value/RNG helpers, the product
scheduler, legacy scalar bridge, owning rich codec/bridge, maintainer-review
regressions and optional original-data helper. Synthetic expectations compare
raw types/bits, fields, host effects, waits, limits and recovery. This is partial
behavioral compatibility, not a claim that a complete original game can execute
on the JS path. Sources: src/game/script, [the implementation plan](../docs/qb-js-plan.md)
and [the contract](../docs/qb-js-contract.md). Public tools use TypeScript;
optional executable-dependent Python/Unicorn experiments are private, untracked
validation and are not distributed project tools.
