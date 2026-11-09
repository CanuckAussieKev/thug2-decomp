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
