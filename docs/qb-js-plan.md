# QB to JavaScript implementation plan

The project translates QB into readable JavaScript generators, executed in an
embedded quickjs-ng runtime. Existing C++ game systems provide gameplay through
an explicit typed host boundary. Node is an offline development dependency.

## Implemented foundation

- Bounded QB reader, typed IR, deterministic generator emission and source sites.
- Format-1 scalar compatibility and format-2 values/expressions, structures,
  arrays, aliases, typed merges, defaults, calls/returns, switch and loops.
- Shared bounded numeric arithmetic with gameplay x87 precision 24; deterministic
  dual RNG streams and lazy random values. Two-pass random array construction
  and random command blocks remain separate compatibility work in this target.
- A pinned quickjs-ng core with portable GCC, Clang and MSVC CMake support.
  Numeric, RNG, values and scheduler helpers are embedded once per Runtime.
- Owning rich native inputs, callbacks, outputs and snapshots. The scalar API
  rejects native format-2 starts before executing them; use startRich instead.
- Per-resume fuel and task-local execution-interrupt faults. Uncaught engine OOM,
  stack overflow and native marshaling allocation failures are terminal.
- Public synthetic CTest coverage and an optional read-only original-data helper
  (ResetLevelFlags) with an independently modeled career-flag host.

The scripting target is optional and independent of both the hooked DLL and
standalone game executable. They continue using the game's QB interpreter.
There is no game-build or rendering integration in this change.

## Compatibility work

1. Extend supported syntax without silently dropping unsupported behavior:
   conditional NOT, object-qualified and dynamic commands, random command blocks
   and two-pass arrays. Unsupported tokens must retain original QB offsets.
2. Add script-level differential tests against the game's interpreter: same
   synthetic scripts, inputs and native host effects, compare full typed outputs
   and ordered host calls. Current isolated arithmetic/RNG evidence and synthetic
   scenarios establish only their explicitly tested subsets.
3. Implement timed waits and the real scheduler's ordering, clocks, pause,
   spawning, object ownership, event handlers, interrupts and on-exit behavior.
   Current Wait support is only explicit gameframe/gameframes/game update waits;
   frame/frames are timed waits and reject until a clock API exists.
4. Implement object handles/member calls and callback-scoped live references.
   The current by-value native model explicitly rejects reference tags 20/21.
5. Add real object/event and visible gameplay scenarios after the necessary
   services have concrete implementations and independent expected outcomes.

## Game integration gate

Before any translated path is wired into the game, add an opt-in INI feature
such as `[features] js_scripts=0`, disabled by default. The original QB path
must remain the default. Add per-script or per-file selection so each translated
scenario can be compared with its QB original independently. This is a future
integration requirement, not an enabled feature of the standalone scripting test
library.

## References and validation

Behavior references are this project's `src/game/script/{parse,eval,struct}.cpp`,
`cscript_exec.cpp`, `script_cfuncs.cpp` and [script-runtime.md](script-runtime.md).
Use synthetic inputs in public tests; local game files remain private. Original
executable probes are optional private checks outside tracked tools; public
project tools are TypeScript as specified in [typescript.md](typescript.md).

See [scripting/README.md](../scripting/README.md) for reproducible CMake/CTest
commands, transport/error policy and current limitations. Public CI tests Linux,
Intel macOS and Windows GCC/MSVC. Passing helper tests does not establish full
gameplay or scheduler compatibility.
