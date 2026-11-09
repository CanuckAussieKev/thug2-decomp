# QB as readable TypeScript

`node tools/qbjs/emit_ts.ts INPUT.qb OUTPUT.ts` translates a QB module into TypeScript meant to be read and edited
as the scripts' source. It reads the same IR as the JavaScript emitter ([qb-js-plan.md](qb-js-plan.md)); names come
from the module's own checksum-name records. Status: the output format only - the `"qb"` module it imports is not
written yet, so the output does not run (next: that module over `scripting/values.js` / `runtime.js`, and a
differential test running each script's TypeScript and its generated JavaScript against the same recording host).

## Conventions

| QB | TypeScript |
| --- | --- |
| `script Foo a = 1 ... endscript` | `script("Foo", { a: 1 }, function* (p) { ... });` |
| `Name = value` (top level) | `global("Name", value);` |
| `Foo a = 1 Flag` | `yield* Foo({ a: 1 }, N.Flag);` - commands are bound by `const { Foo } = cmd;` |
| a name as a value: `Flag` | `N.Flag` (unknown checksums: `N._0x1234abcd`) |
| a global in an expression | `G.Name` |
| `<foo>`, `<foo> = x` | `p.foo`, `p.foo = x` |
| `<...>` (all parameters) | `all(p)` as an argument, `params(p)` inside an expression |
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
| `RandomRange(a, b)` / `RandomRange2` | `randomRange(a, b)` / `randomRange2(a, b)` |

Differences from the JavaScript emitter's output, to be settled by the `"qb"` module: no per-statement fuel steps
(QuickJS's interrupt handler bounds a runaway script) and no QB offsets in faults (a fault names the TypeScript line).
