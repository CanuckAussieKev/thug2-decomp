# The tools in TypeScript

The project's tools (`tools/`) are being rewritten from Python into TypeScript run by Node (the user's preference,
2026-10-20), one area at a time; each port is proven **output-equivalent** to its Python twin before the Python file
is deleted. This page is the conventions for that work and for every new tool. What stays Python: the pyghidra scripts
(`tools/ghidra/*.py` - pyghidra has no Node equivalent), `tools/capstone_disasm.py` (for now), and `tools/neverscript`
(not ours: Go).

## Running

- Node 24+ (installed: v24.11.1). It runs `.ts` directly (type stripping) - no build step:
  `node tools/level_port/col_check.ts <file.col.xbx>`.
- `npm install` once (devDependencies only: TypeScript, @types/node, Biome; `node_modules/` is ignored).
- `npm run typecheck` (tsc, no output), `npm run lint` (Biome: lint + format check), `npm run format` (Biome fixes),
  `npm test` (node --test on `tools/**/*.test.ts`). All three must pass before a commit.

## Erasable TypeScript only

Node strips the types and runs what is left, so only syntax that erases to plain JavaScript is allowed (tsconfig's
`erasableSyntaxOnly` enforces it):

- no `enum` - use `as const` objects or union types (`type Context = "object" | "skater"`);
- no `namespace`, no parameter properties (`constructor(readonly x: number)`) - declare the field and assign it;
- ESM only (`"type": "module"`); imports name the file with its `.ts` extension (`import { Reader } from "./binio.ts"`);
  type-only imports say so (`import type { Struct } from "./qb.ts"`, or `type` inside the braces) -
  `verbatimModuleSyntax` checks it;
- tsconfig is strict, with `noUncheckedIndexedAccess` (an index may be undefined: `xs[i]!` where it can't be) and
  `exactOptionalPropertyTypes` (an optional property is left out, not set to undefined).

## Layout and style

- One `.ts` per Python file, in the same folder, same module boundaries: `tools/level_port/qb.py` -> `qb.ts`. Python's
  `snake_case` functions become `camelCase` (`read_scene` -> `readScene`, `node_array_ns` -> `nodeArrayNs`), classes
  stay `PascalCase`, constants `UPPER_CASE`. Named exports only.
- The Python files' header comments carry real knowledge (formats, game addresses, why things are done): keep them,
  adapted; keep the `NOT ORIGINAL` headers. Say "(TypeScript twin of x.py)" while the twin exists.
- Small typed interfaces for the file formats (`Scene`, `Mesh`, `ColObject`, `TexFile`...) with camelCase fields where
  Python had dicts with snake_case keys. Binary data: `Buffer` (`Uint8Array`), read / written through
  `tools/level_port/binio.ts` (`Reader` / `Writer` with Python struct format strings: `r.read("8I")`,
  `w.write("IHHHBBI", ...)`) or `tools/lib/struct.ts` (`pack`, `unpackFrom`, `"<3f"`, `">I"`, `"<8sIIII"`).
- Node built-ins with the `node:` prefix: `node:fs`, `node:path`, `node:zlib` (`crc32`), `node:util` (`parseArgs` for
  CLIs), `node:child_process`.
- A CLI keeps its Python twin's commands, arguments, defaults and output exactly (users, docs and other tools rely on
  them). Its entry: `if (isMain(import.meta.url)) await runMain(() => main());` (tools/lib/py.ts): `main` returns the
  exit code; `pyExit("message")` is `sys.exit("message")`.
- Imports are relative (`import * as qb from "../level_port/qb.ts"`), not Python's `sys.path.insert` + `import qb`.
  Options that were Python keyword arguments become an options object when there are many
  (`new Translator(sources, thug2, translatable, nodeNames, allNodeNames, names, { full: true, objects })`); a Python
  subclass (thug1_port's `Thug1Translator`) is a TypeScript subclass overriding the same methods.
- `tools/lib/` holds what every port needs: `py.ts` (Python's semantics: below), `struct.ts`, `difflib.ts`
  (SequenceMatcher, get_close_matches, unified_diff), `re.ts` (search / sub / finditer with Python's match object).
- Formatting: Biome, 4 spaces, 120 columns, double quotes (`npm run format`).

## Python -> JavaScript traps

Each of these changed (or would have changed) an output somewhere; `tools/lib/py.ts` has the helper.

**Numbers**
- `round(x)` is banker's rounding (halves to even: `round(2.5) == 2`) - `roundHalfEven`, never `Math.round`.
  `round(x, n)` rounds the exact binary value (`round(2.675, 2) == 2.67`) - `pyRound`.
- `repr(float)` / `str(float)`: exponent below 1e-4 and from 1e16 (`1e-05`, `1e+16`), always a `.0` - `pyFloatRepr`.
  JavaScript's `String(n)` switches at 1e-7 / 1e21 and drops `.0`. `"%.6f" % x` rounds the exact value halves-to-even
  (`"%.9f" % 0.0009765625 == "0.000976562"`) and keeps `-0.000` - `formatFixed`; `toFixed` rounds halves up and goes
  exponential from 1e21. `%g` / `{:g}`: not ported yet - write a helper, don't approximate.
- Python can't tell 1.0 from 1 apart only in JavaScript: where the type shows in the output (qb values: NeverScript
  writes `1` and `1.0`; json.dump writes `1.0`) a float is a `Float` (py.ts), an int a plain number.
- `sum()` of floats (3.12+) is Neumaier-compensated: `sum([0.1] * 10) == 1.0`, plain addition gives
  0.9999999999999999 - `pySum`. (`sum` of ints: plain addition.)
- `math.dist` / `math.hypot` are near-correctly rounded (scaled double-double sums): not `Math.sqrt(dx*dx + ...)`, not
  `Math.hypot` - `pyDist` / `pyHypot` (CPython's algorithm; checked on 30,000 random cases, all equal).
- `x ** 2` on Windows is the C library's `pow`, which differs from `x * x` in the last bit for ~1 value in 2,000; the
  ports use `x * x` (the only uses - bounding spheres, nearest restarts - write float32s or compare distances, where
  that bit has never shown; the parity runs agree).
- `struct.pack("<f", x)`: the same IEEE round-to-nearest-even as `Math.fround` / `DataView.setFloat32` (checked), but
  Python raises on overflow - `f32()` / struct.ts do too. Python's struct raises for an out-of-range or non-integer
  value where DataView wraps silently: struct.ts and binio's Writer check.
- Bit operations give signed 32-bit results in JavaScript: `flags | 0x20000` with bit 31 set is negative - write
  `(a | b) >>> 0` for u32s. Checksums always `>>> 0` (qb.ts `checksum` does). `(point << 2) | axis` with a negative
  point: `point * 4 + axis` (no 32-bit wrap).
- Integers past 2^53: JavaScript numbers lose them - use `BigInt` (struct.ts refuses `q` / `Q` values past 2^53).
- `//` and `%` floor (the result takes the divisor's sign: `-7 // 2 == -4`, `-7 % 2 == 1`) - `floorDiv`, `pyMod`.
- `max()` / `min()` keep the first of equals and skip NaN unless it's first; `Math.max(...a)` gives NaN for any NaN and
  overflows the stack for big arrays - `pyMax`, `pyMin`, `pyMinBy` (`min(items, key=...)`).
- `int(text)`, `int(text, 16)`, `float(text)` are strict (no trailing junk, underscores allowed, `float("")` raises);
  `parseInt` / `Number` aren't - `pyParseInt`, `pyIntHex`, `pyParseFloat`.

**Strings and bytes**
- `bytes.decode("latin-1")` = `Buffer.toString("latin1")`, but `str.encode("latin-1")` raises past U+00FF where
  `Buffer.from(s, "latin1")` keeps the low byte - `encodeLatin1`. `encode("ascii")` raises - `encodeAscii` (the qb
  checksum of a non-ASCII name raises in both).
- `decode("ascii", "replace")` gives U+FFFD per high byte; Buffer's `"ascii"` drops the top bit (0xE9 -> "i") -
  `decodeAsciiReplace`. `"utf-8"` keeps a BOM as U+FEFF; TextDecoder drops it unless `ignoreBOM` - `decodeUtf8`.
- `str.isalnum()` / `isdigit()` / `isalpha()` are Unicode-aware ("é", "²") - `isAlnumChar` etc.; `\w`, `\d`, `\b` in
  JavaScript regexes are ASCII. `str.strip()` / `split()` use Python's whitespace (not quite `trim`'s) - `pyStrip`.
  `str.splitlines()` also splits on `\r`, `\v`, `\f`, `\x1c-\x1e`, `\x85`, U+2028/9 - `pySplitlines`.
- `lower()` / `upper()`: the same as `toLowerCase` / `toUpperCase` for ASCII (both apply Unicode special casing; e.g.
  "ß".upper() is "SS" in both). Compare and sort strings by code point: JavaScript's `<` compares UTF-16 units - the
  same below U+10000.
- `repr(str)` / printing a dict, list or tuple: `pyRepr` (`{'same': 0}`, `(1, 'b')` - mark tuples with `pyTuple`).
- `"%s" % value` of a float is its repr, `"%d" % 3.7` truncates.

**Files, output, processes**
- Python's text mode on Windows writes `"\n"` as `"\r\n"` (`open(p, "w")`, `json.dump` to such a file) - write those
  files with `writeTextPy`. Reading (`open(p).read()`) turns `"\r\n"` and `"\r"` into `"\n"` - `readTextPy`.
- `print()` on Windows writes `"\r\n"`; `console.log` / `print()` (py.ts) write `"\n"` (no visible difference in a
  console; the parity harness compares stdout with `"\r\n"` read as `"\n"`). Python's stdout to a pipe is cp1252 unless
  `PYTHONIOENCODING` says otherwise; Node writes UTF-8. `console.log("%s")` formats its first argument when more
  follow - use `print` (py.ts).
- `subprocess.run(..., text=True)` decodes with the locale's code page (cp1252) and universal newlines -
  `common.textOutput` / `decodeCp1252`.
- `json.dump` / `json.dumps`: `pyJsonDumps` (ASCII escapes, `1.0` for floats, `", "` / `": "`, the `indent` layout).
  `json.load` reads `NaN` / `Infinity`, JSON.parse doesn't - `pyJsonLoads` (also keeps floats as `Float`).
- `sys.exit("message")` prints to stderr and exits 1; it is an exception (`SystemExit`) that `except Exception` doesn't
  catch - `pyExit` throws `SystemExit`; a ported catch-all must rethrow it.
- `os.path.join` (ntpath) keeps the parts' own slashes (`"C:/x\\levels"`); node:path normalises them - `pyJoin`
  where a path is printed or compared as text. `os.walk` / `os.listdir` / `glob` order is the file system's: sort
  explicitly (Python's glob on Windows matches case-insensitively and skips names starting with ".").
- `shutil.copytree` copies file times (`fs.cpSync(..., { preserveTimestamps: true })`) and refuses an existing target.

**Containers and regexes**
- dict / set iteration is insertion order in both - but a JavaScript object puts integer-like keys first, in numeric
  order: use a `Map` for dicts keyed by numbers (checksums) or by arbitrary text. `JSON.parse` of an object with
  integer-like keys reorders them too.
- A Python **set of strings** iterates in hash order, which changes from run to run (`PYTHONHASHSEED`): where Python
  code depends on that order its output wasn't deterministic (script_diff's first N examples; the script translator's
  context order) - the TypeScript keeps insertion order; compare such output sorted or in full.
- `sorted(list_of_numbers)`: `Array.sort()` without a comparator sorts as text (`[10, 9]` stays) - `sortedNumbers` or
  `(a, b) => a - b`. Both sorts are stable. Tuples sort element by element: write the comparator.
- An empty list / dict / bytes is false in Python, true in JavaScript; NaN the other way round - `pyTruthy` where
  `if x:` / `x or y` meet such values.
- Negative indexes (`a[-1]`) and out-of-range slices / indexes: Python's `a[i]` raises past the end where JavaScript
  gives `undefined` (and arithmetic on it NaN) - check where the Python relied on the exception.
- Identity: Python's `id(node)` sets become `Set<object>` of the objects themselves. Equality: a Python class with
  `__eq__` / `__hash__` (qb's `Name`) compares and hashes by value - in TypeScript `===` and Map / Set keys are by
  identity: compare `.value` (or `name.equals(other)`), key Maps by the checksum.
- Python's tuples used as dict keys (`labels[("else", n)]`) become string keys (`` `else:${n}` ``).
- Regexes: `(?P<name>...)` -> `(?<name>...)`, `(?P=name)` -> `\k<name>`, `\Z` -> `(?![\s\S])`; re.S is the `s` flag,
  re.M `m`, re.I `i`. In `m` mode JavaScript's `^` / `$` also break at `\r` and U+2028/9 (Python's only at `\n`);
  without `m`, Python's `$` also matches before a final `"\n"`. `re.sub` replacement strings read `\1`, JavaScript's
  `$1` / `$&` - pass a function or a literal through `re.ts`'s `sub`. Group offsets (`m.start(2)`): `re.ts`'s
  `Match` (the `d` flag). `str.replace(a, b)` with a string pattern replaces the first only in JavaScript - use
  `replaceAll`, or a function where `b` may contain `$`.

## Proving equivalence

Every port is compared with its Python twin on real data before the Python file goes:

- `tools/parity/parity.ts` runs each case's Python command (`python -I ...`) and its TypeScript twin (`node ...`) on the
  same inputs, each writing into its own scratch folder, and compares exit codes, stdout and every file written, byte
  for byte. `node tools/parity/parity.ts tools/parity/<area>/cases.ts [--only <text>] [--jobs 6] [--keep]
  [--scratch <folder>]`; the report is `<scratch>\report.txt`, the outputs of differing cases are kept.
- A cases module lists `{ name, python, ts, stdout?: "exact" | "sorted" | "ignore", setup?, nonzeroOk? }`; `{out}` is
  the side's output folder, `{in}` an input folder a `setup` Python command fills for both.
- CLIs are compared as they are. Library modules get a pair of drivers (`tools/parity/<area>/driver.py` / `.ts`) whose
  commands run the functions on the games' files and write canonical dumps (`tools/parity/canon.py` / `canon.ts`: the
  same JSON text from both languages - numbers as `"n:<repr>"`, qb values tagged int / float, bytes as hashes) plus
  every file the functions write.
- Outputs that aren't deterministic in Python itself are compared in a stable form: ns.exe (Go) writes a .qb's 0x2B
  checksum names in map order, different every run (the drivers compare the code bytes and the sorted names); set
  orders (above). Python runs with `PYTHONHASHSEED=0`.
- `npm test` runs node:test unit tests of the trickiest pure functions without game data; tests needing the game skip
  when it isn't installed.

## Migration tracker

| Python file | TypeScript twin | Status | Stage |
| --- | --- | --- | --- |
| tools/level_port/binio.py | binio.ts | ported + verified | 1 |
| tools/level_port/prx.py | prx.ts | ported + verified | 1 |
| tools/level_port/tex.py | tex.ts | ported + verified | 1 |
| tools/level_port/thug2_scene.py | thug2_scene.ts | ported + verified | 1 |
| tools/level_port/thug2_col.py | thug2_col.ts | ported + verified | 1 |
| tools/level_port/geometry.py | geometry.ts | ported + verified | 1 |
| tools/level_port/qb.py | qb.ts | ported + verified | 1 |
| tools/level_port/nodes.py | nodes.ts | ported + verified | 1 |
| tools/level_port/peds_cars.py | peds_cars.ts | ported + verified | 1 |
| tools/level_port/script_port.py | script_port.ts | ported + verified | 1 |
| tools/level_port/common.py | common.ts | ported + verified | 1 |
| tools/level_port/ported_levels.py | ported_levels.ts | ported + verified | 1 |
| tools/level_port/col_check.py | col_check.ts | ported + verified (CLI) | 1 |
| tools/level_port/script_diff.py | script_diff.ts | ported + verified (CLI) | 1 |
| tools/compat/compat.py | compat.ts | ported + verified | 1 |
| tools/thps3_levels/thps3.py, thps3_levels.py | - | pending | 2 |
| tools/thps4_levels/thps4.py, thps4_levels.py, thps4_scripts.py | - | pending | 2 |
| tools/thug1_levels/thug1.py, thug1_levels.py, thug1_port.py | - | pending | 2 |
| tools/thaw_levels/thaw.py, thaw_levels.py, thaw_scripts.py, migrate_layouts.py, thugpro_compare.py | - | pending | 2 |
| tools/menu_level/make_menu_level.py, level_select.py, level_select_icons.py, career_complete.py, qb_walk.py | - | pending | 3 |
| tools/anteara/anteara.py | - | pending | 3 |
| tools/game/boot_level.py, exe_audit.py, install_manifest.py, make_automation_tables.py | - | pending | 4 |
| tools/bps_dump.py, check_code_writes.py, extract_cfuncs.py, find_missed_functions.py, fold_pending.py, map_units.py, mutate.py, progress_report.py, symbolize.py | - | pending | 4 |
| tools/capstone_disasm.py | - | stays Python (for now) | - |
| tools/ghidra/*.py (apply_cfuncs, audit_game_function_calls, create_functions, export_functions, find_gaps, find_stubs, inspect_program, inspect_range, list_project, scan_register_assumptions) | - | stays Python (pyghidra) | - |
| tools/parity/canon.py, tools/parity/level_port/driver.py, mutate_staging.py | (the drivers' Python halves) | go with the last Python twin they compare | - |

The Python files of stage 1 stay until the converters that import them (stage 2: thps3/thps4/thug1/thaw_levels; stage
3: make_menu_level.py imports ported_levels and the level_port modules) are ported; until then a change to one of them
is made in its TypeScript twin too, and its parity cases run again.
