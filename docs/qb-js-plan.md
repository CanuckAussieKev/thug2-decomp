# QB to JavaScript implementation plan

The scripting port follows the upstream plan: translate QB scripts to JavaScript
and execute them as generators in embedded QuickJS. The existing C++ game
systems remain responsible for gameplay. JavaScript reaches them through an
explicit bridge. Node is an offline development tool, not a game dependency.

## Branches and current implementation

`main` matches upstream commit `2d82ed2b54f27b79cbda9acb63e9dc7a57f409bf`.
`script/qb-to-js` starts there. The previous standalone C++ QB interpreter,
readers and tests are preserved at `a23d587` in `archive/qb-vm` and the existing
`native/qb-runtime` branch. They are not part of the new game's runtime.

Upstream already provides a behavior specification and reconstructed C++
implementations. At this base there is no QuickJS integration or QB to JS
translator. The milestones below describe work to implement, not working code.

## Format and behavior references

- [qb.h](../src/game/script/qb.h) and
  [qb.cpp](../src/game/script/qb.cpp): token layout, skipping, checksums and
  preprocessing. An on-disk checksum is not a cached native function pointer.
- [script-runtime.md](script-runtime.md): scheduling, calls, waits, events,
  interrupts and object ownership. Its test/code labels identify the strength
  of evidence; section 13 records open questions.
- `src/game/script/parse.cpp`, `eval.cpp`, `struct.cpp` and `cscript_*.cpp`:
  value parsing, operators, typed parameter lookup and execution details.
- [neverscript.md](neverscript.md): an existing community compiler/decompiler
  and its round-trip results and limitations. It emits NeverScript text, not
  JavaScript, and is not a replacement for the runtime behavior specification.

These are reconstructed references, not a complete official Neversoft language
standard. Resolve disagreements against the reconstructed function and, where
possible, an isolated original-function test before choosing JS behavior.

## First milestone

Execute a small translated script in actual QuickJS without THUG2.exe. It must
call a second translated script, pass typed parameters, return a value, suspend
for a frame and resume at the correct point. Use synthetic QB fixtures first;
then choose an unmodified game helper whose dependencies are implemented.

1. Define a typed intermediate representation for values, statements, source
   offsets, checksum names and calls. Read binary QB with explicit bounds and
   diagnostics. Build the offline translator in TypeScript to fit the existing
   tools. Reuse token-reader test cases from the archive rather than building a
   second general C++ QB executor. Reject unsupported tokens with QB offsets.
2. Emit readable JS generator functions and a module manifest. Preserve numeric
   checksum keys and QB source offsets so unknown names remain usable and errors
   can be traced to QB. Generation must be deterministic.
3. Implement only the needed JS compatibility operations initially: distinct
   Integer/Float/Name values, typed parameter merge, defaults, local assignment,
   QB calls/returns, conditions and frame waits. Ordinary JS truthiness, numeric
   coercion and object property lookup must not silently replace QB rules.
4. Embed a pinned official QuickJS release as an optional standalone CMake
   target. Lock dependency hashes, release JS handles on every error path and
   impose memory/execution bounds. Keep the existing x86 DLL build working.
5. Add a C++ test host with explicit clocks and real argument/result transport.
   Test in QuickJS itself; running generated JS under Node alone does not
   validate the engine integration. Unknown game commands must fail visibly.

Completion means the synthetic call/wait scenario passes through the translator,
QuickJS and C++ bridge with expected values and frame timing, with no C++ QB VM
executing the script and no game files needed for unit tests.

## Compatibility milestones

Expand the translator and JS compatibility library in tested increments:

1. Structures, arrays, aliases, parameter spread, expression operators,
   switch/loops and typed lookup. Specify mutable references and identity;
   the archived immutable C++ values are not a complete reference model.
2. Original RNG streams, range rounding, no-repeat/permutation state and
   two-pass random array construction. Preserve float32/int32 behavior instead
   of relying on JS Number arithmetic. Adapt the existing differential vectors.
3. Scheduling and waits from sections 2, 3 and 5 of script-runtime.md: object
   scripts before spawned scripts, newest-first walks, spawn timing, pause,
   clock selection and one-per-frame gating. Do not copy the archived task
   runner's deliberately simplified update order.
4. Object handles, member dispatch, object lifetime, WaitForEvent, targeted
   events, handlers, interrupts and on-exit calls. Define how generator frames
   suspend/unwind before selecting a JS exception strategy. Add reentrancy and
   deletion-during-dispatch tests before connecting mutable game objects.
5. Run one real object/event QB scenario through generated JS and implemented
   C++ services without graphics. Then connect one visible gameplay scenario;
   that additionally requires the relevant level, physics and renderer systems.

## What to reuse from the archived work

Recover individual files or cases from `archive/qb-vm` when their consumer is
implemented. Do not cherry-pick the whole native-core history.

- `native/tests/qb_tests.cpp` and `precompiled_tests.cpp`: token bounds,
  malformed inputs and precompiled/token correspondence for translator tests.
- `runtime_tests.cpp`, `switch_tests.cpp`, `gotparam_tests.cpp` and data-host
  suites: parameter, control-flow and typed-search cases to adapt to JS tests.
- `wait_tests.cpp`, `random_tests.cpp`, `range_tests.cpp`,
  `inline_random_tests.cpp` and `array_random_tests.cpp`: timing, RNG and numeric
  cases. Recheck native-only policy choices against upstream before adopting.
- `tools/verify_native_*.py`: optional original-x86 harness patterns. Replace
  the C++ VM probe with the new QuickJS bridge; the old native oracle library
  is not a dependency of the product. Use a project venv if modules are needed.
- `profile_corpus.cpp` and `menu_callback_corpus.cpp`: recipes for real-data
  integration tests and independent expected profile results. Load scripts
  from the user's copy; do not publish scripts, EXEs or extracted game data.
- PRX readers and asset-store tests only if the translator needs them. Existing
  upstream tooling and NeverScript should be evaluated before duplicating it.

Only the local build/fixture ignore rules are carried over at this stage.

## First upstream proposal

Propose the translator/runtime contract and the first QuickJS milestone as a
small optional addition. Include reproducible synthetic tests and a supported
token list. Verify the actual QuickJS target on Linux, Windows and macOS and
review provenance/dependency notices before opening a PR. Keep asset readers,
runtime features and game-service implementations in separate reviewable steps.

The first proposal should demonstrate QB to JS execution, a C++ call and a
wait/resume boundary. Full object events and a standalone game are later
milestones. No maintainer message or PR is sent as part of preparing this plan.
