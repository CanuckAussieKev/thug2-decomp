# QB as readable TypeScript

`node tools/qbjs/emit_ts.ts INPUT.qb OUTPUT.ts` translates a QB module into TypeScript meant to be read and edited
as the scripts' source. It reads the same IR as the JavaScript emitter ([qb-js-plan.md](qb-js-plan.md)); names come
from the module's own checksum-name records (a spelling is used only when it checksums back to the name, else the
name is written `_0x<checksum>`).

`node tools/qbjs/compile_ts.ts INPUT.ts OUTPUT.js` makes the JavaScript the embedded runtime loads: types stripped,
the `"qb"` import read from `scripting/qb_ts.js` (`globalThis.qbTs`), which maps every form below onto the exact value
helpers (`values.js`) and task runner (`runtime.js`) - so the TypeScript is the same program as the generated
JavaScript, bit for bit, not a lookalike.

**Proof:** `tools/qbjs/differential.ts` runs every script of a module both ways against the same recording host
(answers from each command's checksum and call count, three seeds so branches differ, now and then a one-frame wait)
and compares each command call with its arguments, each ending and the final parameters. `differential.test.ts` runs
it on the synthetic fixture modules; `node tools/qbjs/differential.ts <folder>` on a game's scripts.
2026-11-09: THUG2's loose scripts 218 of 222 modules and level scripts 153 of 154 translate, and all agree. Not
translated: 3 CAS part tables holding inline script bodies as values (cas_skater_f / _m / _shared - a script-body
value type the game's host can run is still to come), a Random whose branch jumps past its loop's repeat
(SK6Ped_UniqueStateLogic) and an if / switch nested out of order (CA_Scripts) - neither has a structured form.

## Conventions

| QB | TypeScript |
| --- | --- |
| `script Foo a = 1 ... endscript` | `script("Foo", { a: 1 }, function* (p) { ... });` - defaults that read parameters or draw a random value are a function, `(p) => ({ ... })`, given empty parameters (defaults are parsed without the caller's) |
| `Name = value` (top level) | `global("Name", value);` |
| `Foo a = 1 Flag` | `yield* Foo({ a: 1 }, N.Flag);` - commands are bound by `const { Foo } = cmd;` |
| a name as a value: `Flag` | `N.Flag` (unknown checksums: `N._0x1234abcd`) |
| a global in an expression | `G.Name` |
| `<foo>`, `<foo> = x` | `p.foo`, `p.foo = x` |
| `<...>` (all parameters) | `all(p)` as an argument (spread) or a value (`params = <...>`: a copy), `params(p)` inside an expression |
| `<script> args`, `<...> args` (a parameter names the command) | `yield* call(p.script, ...)`, `yield* call(all(p), ...)`; a statement naming nothing (it starts with a value) `yield* call(0, ...)` |
| `object:Command`, `<id>:Command`, `object:<cmd>`, `(expression):Command` | `yield* on(N.object).Command(...)`, `on(p.id)...`, `yield* callOn(N.object, p.cmd, ...)`, `on(() => expression)...` - a script run so has that object for its own commands |
| a C function in an expression: `(F)`, `(F arg = 1)` | `G.F` (a global's value, or a C function's result), `fn.F({ arg: 1 })` - its result 0 / 1, its outputs dropped |
| `(expression) args` as a statement or an if's condition | `yield* run(expression, ...)` (a name is called, else its truth); `test(...)` where the expression can't give a name |
| `if Name = 1` | `p.Name = 1;` then `if (true)` - an assignment counts as true |
| integer / float | `5` / `1.25`; a whole-number float `f(2)`; -0, NaN and infinities `f(-0)`, `fbits(0x7fc00000)` |
| string / localised string | `"text"` (bytes outside ASCII as `\xNN`) / `L("text")` |
| pair / vector | `pair(x, y)` / `vec(x, y, z)` |
| structure / array | `{ a: 1 }` (with unnamed or repeated members: `s({ a: 1 }, N.Flag)`) / `[1, 2]` |
| `(<a> + 1)`, `=`, `<`, `>`, `-`, `*`, `/`, `and`, `or` | `add(p.a, 1)`, `eq`, `lt`, `gt`, `sub`, `mul`, `div`, `and`, `or` |
| `a[i]`, `a.b` | `at(a, i)`, `dot(a, N.b)` |
| `(<x>)` (a single value in parentheses) | `val(p.x)` - an expression's result, names resolved |
| `if Foo` / `if (expr)` | `if (yield* Foo())` / `if (test(expr))`; `else if` chains |
| `begin ... repeat 5` | `const loop = repeat(); do { ... } while (loop.again(5));` - a count that isn't a literal is passed as a function (`() => p.n`): it is read once, at the end of the first pass |
| `begin ... repeat` | `while (true) { ... }` |
| `switch <x> case 1 case 2 ... default` | `if (is(p.x, 1)) ... else if (is(p.x, 2) \|\| is(p.x, 3)) ... else ...` (a selector that isn't a plain value is read into `const onN` first) |
| `Random(@a @b)`, `Random2`, `RandomNoRepeat`, `RandomPermute` | `random(k, [a, b])`, ... - `k` numbers the call site in its module (the no-repeat / permute state is kept per site); weights first when not all 1 |
| a Random among statements | `const pickN = random(k, [0, 1, 2]);` then `if (pickN === 0) {...} else if ...` |
| a Random among a call's fields: `Foo Random(@a = 1 @a = 2 Flag)` | `yield* Foo(...random(k, [[{ a: 1 }], [{ a: 2 }, N.Flag]]))` |
| `RandomRange(a, b)` / `RandomRange2` | `randomRange(a, b)` / `randomRange2(a, b)` |

Commands and objects: the host gets `(name, args, parameters, object, qualified)` - the object the command runs on
(a task's current object; `object:Command` names one, and a script run so keeps it for its own commands) and whether
it was named (only member functions run on a named object). `__qbFindObject(name)` answers whether an object exists
(a missing one is a fault: the original hangs there), `__qbSymbolKind(name)` whether a name is a C function.

Differences from the JavaScript emitter's output: a TypeScript script charges the task's statement budget once per
command call and loop pass, not per statement (QuickJS's interrupt handler is the backstop for a runaway one), and a
fault names the TypeScript line rather than a QB offset. A random choice's no-repeat state is kept per call site as in
the original (the site is the script and the call's number `k` instead of its QB offset).

## In the game

The game runs translated scripts in QuickJS with `[features] ts_scripts=1` (default 0; `[debug] ts_profile=1` logs
where the time goes). `node tools/qbjs/install_ts.ts --game <folder> [--clear]` translates the folder's `.qb` files -
`Data\scripts`, `Data\levels` and the `thug2-decomp-*` overlays - into `thug2-decomp-ts`: `by-hash\<hash>.js`, named by
the FNV-1a hash of the `.qb` file's bytes, and the readable `source\<folder>\<path>.ts` beside. After the loader reads a
`.qb` file it hashes the bytes it read and loads the module of that name, if there is one - so a module only ever
replaces the very file it was translated from (an overlay's or a mod's version of a file has its own), and a file
without one stays QB. The module's scripts and globals replace that file's.

The C++ interpreter stays the host: the record, its Update, waits, events, spawning, objects and the call stack are all
its own, so the scheduling is the original's by construction. A translated script's symbol has a body of one token
(`kTsBody`, 0x4A + its name); when Update reaches it on a call-stack level, the interpreter resumes that level in
QuickJS (`src/script_qb/cscript_ts.cpp`, `ts_bridge.cpp`):

- each command call comes back as a request `{call, args, on}`, carried out exactly as `execute_command` carries out a
  statement's (`run_command` / `run_command_on`, the arguments as the statement's); a script it calls returns to the
  token, and the level is resumed with the command's result - after the callee's return, or after the wait the command
  set;
- the level's end is a return: its OnExitRun script, then `return_from_script` with its return values;
- the record's parameter list is the level's: sent to it when it changed since the level last saw it (a 64-bit
  fingerprint), and the level's own assignments (or, once, its defaults merged with the arguments) sent back;
- symbols are the game's (`__qbGame.symbol`), C functions in expressions are called natively, random numbers come from
  the game's generator; a module's globals and scripts are defined as the loader defines a file's (tagged with the file,
  set aside for a temporary load).

A JavaScript fault is logged (`ts: <script>: ...`) and ends the level. Where the original hangs - an object-qualified
call on a missing object - a TypeScript level is logged and told false instead.

2026-11-09: with all of THUG2's translatable modules installed (502, overlays and ported levels included) the game boots
to the main menu and loads Boston in free skate with no fault; a release build boots in 10.4 s against 6.7 s with QB
only (module loading, and texture-list scripts that loop over big arrays - next to optimise).
