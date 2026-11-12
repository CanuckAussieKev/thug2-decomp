# QB as readable TypeScript

`node tools/qbjs/emit_ts.ts INPUT.qb OUTPUT.ts` translates a QB module into TypeScript meant to be read and edited
as the scripts' source, and its globals into `OUTPUT.json` beside it ("Data" below; `--inline`: the globals in the
`.ts` too, as `global(...)`). It reads the same IR as the JavaScript emitter ([qb-js-plan.md](qb-js-plan.md)); names come
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
| `Name = value` (top level) | `global("Name", value);` - in the game's files the `.json` beside the `.ts` ("Data") |
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

The game runs translated scripts in QuickJS with `[features] ts_scripts=1` (default 0: the `.qb` files; `[debug]
ts_profile=1` logs where the time goes), in the game unpacked: each file the game loads is defined by its `.ts` - its
scripts - and its `.json` - its globals ("Data" below) - in place of its `.qb`, and no `.qb` is ever read: a file with
no `.ts`, whose `.ts` fails, or whose globals have no `.json`, is an error in the log (`ts: ERROR - ...`) and isn't
loaded (`unpack_game.ts` fails on a `.qb` that doesn't
translate; only the script check's own plan files are read as `.qb`). A module is compiled to
QuickJS bytecode the first time it loads and cached in `thug2-decomp-ts\cache` (keyed by the QuickJS version, the word
size and the module's own bytes); later starts read the bytecode. (Until 2026-11-13 there was also a mode with the
modules beside the `.qb` files - `install_ts.ts`, `thug2-decomp-ts\by-hash` - and the unpacked game was
`ts_scripts=2`; any value but 0 now means the unpacked game.)

**Unpacked** (`node tools/qbjs/unpack_game.ts --game <folder>`): the game with no archive and no `.qb`. The tool moves every archive in `Data\pre` (`.prx`, the French and German `.prf` / `.prd` and their
patches `.ppf` / `.ppd`) and every `.qb` (Data's, the data overlay's, the ported levels' overlays) to
`<game>\thug2-decomp-backup` at the same paths, writes each `.qb` as a `.ts` (and its globals as a `.json` beside it)
and puts every file of the archives loose
where its name says in Data (`Data\levels\bo\BO.scn.xbx`, `Data\levels\bo\BO_scripts.ts`, `Data\sounds\pcm\...`). The
PC game already ships loose copies of nearly all of them (6,453 of 6,806 names; 351 sounds and two `.bin` files were
only in archives); the archive's copy goes over a loose one that differs - the game read a loaded archive's copy
first, and loads each archive before its files - which is only the scripts (the archives' copies without the loose
ones' debug prints) and two of Training's textures. With no archive file and no folder, both archive loaders load
nothing (`archive.cpp`, `packs.cpp`) and the game reads every file loose; the loose-file table holds 4,096 files, not
200 (a resident archive's files - `unloadableanims`' 1,006 - are held one by one). `unpack_game.ts --check` keeps every
archive in its folder (the script check runs each version of a file). `--restore` puts everything back (its record:
`thug2-decomp-backup\unpacked.json`, the `.ts` and `.json` it made among them); `--refresh` puts back and unpacks again - after `make_menu_level.ts` or a
converter rewrote an overlay (until then the game reads such a file's new `.qb`, not its old `.ts`: the `.ts` names the
hash of the `.qb` it was made from).

- **Languages** (`tools/qbjs/localize.ts`): one set of scripts for every language, its text as tokens -
  `tr("bo_scripts.smash_5_tea_barrels")` - with each language's text in its table, English's too: `Data\lang\en.json`,
  `fr.json`, `de.json`. Every string a player may read is a token, the same in every language today or not ("Boston"):
  each literal with letters in it but file names and paths, names (one word with an underscore: `loadscrn_boston`) and
  the debug commands' arguments (printf, PrintStruct, ScriptAssert) - 11,641 tokens, ours (the front end, the ported
  levels) included but Anteara Mod (English only: its text stays in its scripts) - a file no language has (our
  overlay's copies of the game's menus) takes the translated token of its text; one text and its translations share a token across files. The French and
  German archives differ from the English ones only in their scripts' text (172 and 167 files; every other file the
  same), so a script's versions are the same code with other strings: their literals pair up in order and give
  `fr.json` / `de.json` their text (7,394 tokens each; keyboard.qb, which the game patches, paired through its unpatched
  English). The game reads `Data\lang\<code>.json` and `en.json` before the first module (`ts_bridge.cpp
  load_text_tables`; the code: `[features] language=` - en, fr, de or any other table's -, else the registry's
  language) and `tr` gives the token's text in the language, else English's. The language archives aren't unpacked; a
  new language is a table. `Data\scripts\localtext*.qb` - the translators' tables, English with French, German,
  Spanish and Italian, which no game code reads - are left as they are.
- **Archives as folders** (`src/engine/file/archive_folder.h`; the check's): when an archive file isn't there and its
  folder is, both loaders (the pre manager's, the packs') build the same archive in memory from the folder - every file in it,
  stored unpacked - so an archive is in and out of memory exactly when it was, and everything after reads it as before.
- **A file's TypeScript** (`ts_bridge.cpp load_standalone`, asked first by `load_qb` and `load_qb_data` - a level's
  node array is read as an asset, `NodeTableResource::read_in`, and with no `.qb` parsed from nothing): `<name>.ts`
  read the way the game reads any file (`packs::read_in`: the data overlay, then the loaded archives, then Data), so it
  is the version the `.qb` would have been - the boot scripts' copies in `qb_scripts` (without the loose files' debug
  prints), a level's `*_scripts` copy once its archive is in, the loose file before. The `.ts`'s last line,
  `// qb-file {...}`, is its manifest (its source name, the hash of the `.qb` it was made from, how many globals it
  has - in its `.json` -, the file's checksum names); the game turns the `import ... from "qb"` into a read of the
  runtime, wraps the file as `compile_ts.ts` does - each line where it was - and caches its bytecode
  (`thug2-decomp-ts\cache\ts-<hash>.qbc`). The emitted files have no type syntax; a type annotation added by hand isn't
  stripped in the game yet (a syntax error: the file isn't defined). Its globals: `<name>.json` read the same way, after
  the module has run ("Data").
- A file's globals go to the game's own table only (read back through it as a `.qb` file's): the runtime keeps no
  copy, which would outlive the file. A module runs with the game's interrupt budget, never the script check's lower
  one.
- The script check proves it: `script_check_parallel.ps1` runs its TypeScript pass in the game unpacked
  (`unpack_game.ts --check`), every version of every file (docs/script-check.md), and compares every global each file
  defines as well as the runs.

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
TypeScript beside the `.qb` 6.6 s, standalone 8.6 s (unpacked, the same day: 9.9 s - the archives built from loose
files; 13.4 s the first start, nothing in the disk cache). Standalone's extra 2 s are its 234 modules building the globals
in JavaScript (the CAS tables, animevents, terrain - 189 ms the largest) where the hybrid game reads them from the
binary `.qb`; the runtime copies each global once more before the game converts it (`defineGlobal`), a copy the game
mode no longer needs - the first thing to drop, with the check run again.

2026-11-13 (the same measure, a workspace, warm): the globals moved to `.json`, read by C++ - Boston 11.5 / 12.0 s
before (13.9 s the first start), 8.4 / 8.6 s after (10.8 s the first); the 234 modules' load time 3,549 ms -> 312 ms
(Boston's own file 1,001 ms -> 30, of which its 1 MB of data 3 ms).

## Data

A script file's globals are data, not code: in the game unpacked each `<name>.ts` has its scripts and a `<name>.json`
beside it its globals (`tools/qbjs/emit_data.ts` writes it, `unpack_game.ts` and `emit_ts.ts` put it beside the
`.ts`; a file with no globals has none). Tools read a file's data without running anything (the level converters'
z-fighting gates read a level's node array from it), the game reads it straight into its cells, and BO.ts is its
scripts (75 KB; the node array in BO.json).

**The values** are the ones the game builds for each global - what the TypeScript defined before, `qb_ts.js` value()
over `values.js`: a structure's members merged as they're added (a later member of the same name and kind replaces
the earlier and moves to the end), an unnamed structure's members spread in, an `angles` vector of near-zero literal
coordinates left out, integers among floats made floats. So the file holds the values themselves, and reading it is
building nothing. A name defined twice in a file: its last definition (none of the game's files has one).

**The format** (`tools/qbjs/qb_data.ts`: the reference - its comment is the spec -, with the reader `readQbData` and
the writer; dependency-free, for other repositories to import or copy - thug2-level-converter-core has a copy):

```json
{
    "format": "qb-data 1",
    "source": "Data\\levels\\bo\\bo.qb",
    "from": "d699a832",
    "globals": {
        "BO_NodeArray": [
            {"Pos": {"$vec": [-143.25702, 35.131844, 948.5483]}, "Name": "TRG_Rail02", "ncomp_RailNode_1": true},
            ...
```

| value | JSON |
| --- | --- |
| integer | a number with no fraction or exponent: `5`, `-3` |
| float | a number that isn't whole (`1.25`, `1e-7`: its shortest spelling that reads back to the same 32 bits), else `{"$float": 2}`, `{"$float": -0}`, `{"$float": "0x7fc00000"}` (infinities, NaN: the bits) |
| name | a string: `"Class"` (its text where the file's checksum names spell it), `"_0x1a2b3c4d"` |
| string / localised string | `{"$string": "text"}` / `{"$local": "text"}` - bytes, `\u00XX` past ASCII |
| translated text | `{"$tr": "bo_scripts.boston"}`, `{"$local": {"$tr": ...}}` - the token's text from `Data\lang` (Languages above) |
| pair / vector | `{"$pair": [x, y]}`, `{"$vec": [x, y, z]}` - floats, `"0x..."` bits for infinities and NaN |
| structure | an object, members in order, `"Flag": true` an unnamed name; `{"$struct": [["a", 1], [null, 2]]}` when the members don't fit one (an unnamed member that isn't a name, a key twice - two kinds of one name -, a key that starts with `$` or reads as an index) |
| array | a list (one kind of element) |
| a structure's script | `{"$script": "Masks#1"}` - the code stays in the `.ts`: `scriptValue("Masks#1", code(function* (p) {...}))` (the global's name and its scripts' count) |

Floats are exact: a JSON number is read through a double and rounded to 32 bits (JavaScript's `Math.fround(Number(x))`,
the game's `(float)strtod`), and the writer picks the shortest spelling that comes back to the same bits.

**In the game** (`src/script_qb/ts_data.cpp`): `<name>.json` read as the `.ts` is (the data overlay, the archives,
Data; the script check's chosen version: its `.ts` path with `.json`), after the module has run, parsed straight into
the script heap's cells - the allocations `js_to_cell` makes for a runtime value - and each global defined as the
loader defines a `.qb` file's. A `$tr` string is the token's text in the language (the tables are read before the first
module), a `$script` the code value its `.ts` gave that key (`qb_ts.js` scriptValue / scriptValueOf). Its `from` must be
the `.ts`'s.

**Proof**: `node tools/qbjs/qb_data_check.ts --game <folder>` builds every global of every version of every `.qb` (Data,
the archives, the overlays, the ported levels), writes and reads it back, and runs the module's TypeScript with its
globals in it in the game's mode (`compile_ts.ts`, `qb_ts.js`, `values.js`) - all three the same (2026-11-13, a
workspace: 48,001 globals in 1,119 file versions, 0 different; 148 files' localised text read back through English's
table the same as the file's own); `qb_data.test.ts` covers every kind of value. The script check compares every
global the game defines from the `.json` with the one it defines from the `.qb` (its `global` lines,
docs/script-check.md).

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
