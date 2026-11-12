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
2026-11-10: every module translates - THUG2's 222 loose script modules and 154 level script modules, and with the
overlays (the new front end, Anteara Mod, the ported levels) 531 of 531 - and all agree. What the last ones needed:

- **Scripts as values** - the CAS part tables hold `script name ... endscript` in their structures (a part's
  `disqualify_script`, run by the model builder): `name: code(function* (p) { ... })`. In the game such a value is the
  8 bytes `defineScript` gives a script, naming a function kept under a made-up name (`runtime.js` `inline`), so the
  interpreter runs it like any TypeScript script; a QB file's own crosses to TypeScript as its bytes (`{type: 7, bytes}`).
- **A script's tail** - jumps into the closers just before a script's endscript (endifs, endswitches, line ends: the
  statement loop steps over them, so the script just ends there) are a `return`, and closers there close whatever is
  still open. Three scripts were compiled so: an if inside a loop whose false jump leaves for the end (z_el), a switch
  inside two ifs with its endswitch after their endifs (CA_Scripts), a Random's branches jumping past their loop's
  repeat (SK6Ped_UniqueStateLogic). One difference stays: CA_Scripts' last case jumps straight to the endscript, so a
  `group` no case matches holds the original in its switch for ever (run_multi_branch never gets past an endscript);
  the TypeScript ends the script.
- What the game's loader does anyway: bytes after the end-of-file token are ignored, and `<x>` / `<...>` in a global's
  structure add nothing (a global is parsed without parameters). The reader's size limits now fit a ported level's
  node arrays (a module up to 64 MB).

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
| a Random right after a bare name (`Foo Random(...)`) | `random.twice(k, ...)` - the bare name's look-ahead for `=` draws the Random once and drops it; the statement draws again |
| a Random among statements | `const pickN = random(k, [0, 1, 2]);` then `if (pickN === 0) {...} else if ...` |
| a Random among a call's fields: `Foo Random(@a = 1 @a = 2 Flag)` | `yield* Foo(...random(k, [[{ a: 1 }], [{ a: 2 }, N.Flag]]))` |
| `RandomRange(a, b)` / `RandomRange2` | `randomRange(a, b)` / `randomRange2(a, b)` |
| `script name ... endscript` inside a structure | `name: code(function* (p) { ... })` (defaults first, as `script`'s) |

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
without one stays QB. The module's scripts replace that file's; its globals are left out of the module the game loads
(the source keeps them): they would only be the ones the loader has just made from the same bytes - making them again
cost the most (a ported level's node arrays are megabytes), and a global drawing a random number would draw twice.
A module is compiled to QuickJS bytecode the first time it loads and cached in `thug2-decomp-ts\cache` (keyed by the
QuickJS version, the word size and the module's own bytes); later starts read the bytecode.

**Standalone** (`[features] ts_scripts=2`): the game reads no `.qb` for a file that has a standalone module - its
scripts, its globals and its checksum names all come from the module, so the script archives (`qb_scripts.prx`, the
levels' `*_scripts.prx`) aren't needed for it. `node tools/qbjs/install_ts.ts --game <folder> --standalone` writes one
module for each file the game would load, laid out as the game finds it: `thug2-decomp-ts\Data\<path>.ts` / `.js` (the
loose file's version, else an archive's, with PartyMod's keyboard patch where the game applies it) and
`thug2-decomp-ts\thug2-decomp-data\<path>` (the data overlay's, with its `front_end`, `anteara` and
`anteara\front_end` sets). A file whose archive copy differs from the loose one (the boot scripts in `qb_scripts.prx`
lack debug prints the loose files have; a level's `*_scripts` / `*_sfx`) also gets `<path>@<archive>.js`, named in the
base module's manifest (`archives`): the game reads the archive's copy while that archive is loaded - the preloaded
ones (packs: `qb_scripts` at boot) first, then the pre manager's (a level's) - and the loose file otherwise, so the
loader picks the same way (`archive_holding`). `load_qb` asks first (`load.h` QbStandaloneHook, `ts_bridge.cpp
load_standalone`): the module for the path where the overlay would give the file, else the game's; none - the `.qb` is
read as before. The `.ts` is the readable source beside; the game loads the `.js` (compiled by `compile_ts.ts`, whose
manifest carries the hash of the `.qb` it was made from, the archives and the file's names - last, the loader reads
the first 4 KB). A module's globals go to the game's own table only (read back through it as a `.qb` file's): the
runtime keeps no copy, which would outlive the file. A module runs with the game's interrupt budget, never the script
check's lower one (a level's node arrays take far more than a script run). The script check proves it:
`script_check_parallel.ps1 -Standalone` runs its TypeScript pass so, and compares every global each file defines, as
well as the runs.

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

In the game the value helpers count no work (QB has no such bound - a level's whole node array may be copied): a
runaway script is stopped by the bridge's interrupt budget.

2026-11-09: with all of THUG2's translatable modules installed (502, overlays and ported levels included) the game boots
to the main menu and loads Boston in free skate with no fault; a release build boots in 10.4 s against 6.7 s with QB
only. 2026-11-10 (x64 release, workspace, 525 modules): 7.3-7.8 s against 6.5-6.7 s - modules load in 48 ms (344 ms
compiling the first time), strings are shared by the copies of a value rather than copied byte by byte (the
texture-list script that concatenates fourteen global arrays: 295 ms -> 58 ms), and the parameter list's fingerprint
hashes a word at a time. What remains (~0.75 s at boot) is QuickJS running the exact value layer, ~0.1 ms a resume -
for the natural-TypeScript steps to shrink.

2026-11-12 (x64 release, workspace, `tools/game/load_time.ps1`: start to free skate in Boston, 3 runs each): QB 4.4 s,
TypeScript beside the `.qb` 6.6 s, standalone 8.6 s. Standalone's extra 2 s are its 234 modules building the globals
in JavaScript (the CAS tables, animevents, terrain - 189 ms the largest) where the hybrid game reads them from the
binary `.qb`; the runtime copies each global once more before the game converts it (`defineGlobal`), a copy the game
mode no longer needs - the first thing to drop, with the check run again.

## Types

`[debug] ts_types=1` (with ts_scripts=1) records what each site of the TypeScript sees: each parameter read's type (by
module, script and name), each operator's and switch comparison's operand types (by how the operands are written:
`p.x`, a number, `N.x`, an expression), each global read, each command's argument types (by command and argument) and
whether each call came back in a later frame (it waited). The counts accumulate over sessions in
`thug2-decomp-ts\types-recorded.json`. `node tools/qbjs/types.ts --game <folder>` adds what the `.qb` files say (every
call's literal arguments, the globals, the scripts' defaults) and writes `thug2-decomp-ts\types\commands.d.ts` - each
command the scripts call, its arguments with every type seen, whether it is a script / C function and whether it was
seen waiting - and `report.md`: per kind of site how many were seen with one type, for operators how many only with
numbers / only integers, and every site seen with more than one type.

These are the facts the natural-TypeScript steps rest on: a parameter only ever an integer can be a plain number, an
operator only ever given numbers can be `+`, a command never seen waiting needs no `yield*`, a name that is only ever a
name can be a string. A first recording (boot, main menu, Boston free skate; 1,055 sites) found 20 sites with more than
one type - e.g. `load_array_of_textures`'s `textures`, a name standing for a global or the array itself, and `Wait`'s
count, an integer or a float - the cases a natural form must keep exact.
