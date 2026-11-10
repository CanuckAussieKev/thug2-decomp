# The script check

Every script of the game run headless on the game's own interpreter - each branch forced in turn - once as QB and once
as the translated TypeScript, with the same choices; the two traces must be the same, call for call. It proves the
TypeScript against the decompiled interpreter itself (`src/script_qb`), not against a port of its rules, and reports
which branches the runs reached.

```
node tools/qbjs/script_check.ts plan --game C:\Games\THUG2-1 [--only scripts\game\x.qb,...] [--runs 64]
tools/game/game_lock.ps1 -Action acquire -Owner <you> -Game C:\Games\THUG2-1
tools/game/deploy_build.ps1 -Game C:\Games\THUG2-1 -Arch x64 -BuildDir out\build\<build> -NoEditorTools
node tools/qbjs/install_ts.ts --game C:\Games\THUG2-1 --clear
tools/game/script_check.ps1 -Game C:\Games\THUG2-1          # both passes; each quits by itself
node tools/qbjs/script_check.ts compare --game C:\Games\THUG2-1
tools/game/game_lock.ps1 -Action release -Owner <you> -Game C:\Games\THUG2-1
```

## How it runs

`thug2-decomp.exe -script-check <folder>` starts the game as always - the engine, its pools, the command table, the boot
scripts - and at the first front-end frame runs the plan instead of the game, then quits (`src/script_qb/script_check.cpp`).

- **The plan** (`thug2-decomp-check\plan.qb` and one part per module, written as QB globals so the game reads them with
  its own loader): every module of Data\scripts, Data\levels and the data overlay; each script with the argument sets to
  call it with - none, then up to three of the literal ones its callers pass. A level's file is loaded for its module as a
  temporary load and undone after (the boot definitions it replaced come back); a boot file is run as loaded.
- **A run**: the script on a record of its own (no object), the random generator seeded per run, frames stepped - the
  game's clock with them - until it ends, runs out of frames (`frames`), makes too many calls (`calls`) or runs too many
  statements without waiting (`steps`). Scripts run for real, called ones too.
- **Stubs**: every C function and member function is a stub - its call traced with its arguments, its result (true /
  false) the run's next *choice*. Only `Wait`, `WaitOneGameFrame` and `GotParam` run for real (they only wait or test the
  script's own parameters). `object:Command` is a stub too: there are no objects. Names that share one C function are
  traced by one name (a statement the interpreter prepared to call the function directly no longer says which).
- **Exploring** (QB, `[features] ts_scripts=0`): the first run makes every choice "true"; each choice seen is then forced
  the other way in a later run, until every choice has been made both ways or the budget (`runs` per script and argument
  set) is spent. Each Random takes the branches its seed gives; varying seeds reach the others. Coverage is recorded by
  the interpreter itself: each if taken or not, each switch's case, each Random's branch (`qb.txt`'s `cover` lines).
- **Replaying** (TypeScript, `ts_scripts=1`): every QB run again with the same seed and choices, its trace compared with
  QB's there and then; `ts.txt` has each module's count and every run that differed, with its first differing line.
- **Trace**: each stub call (the frame, the name, the object, the arguments - floats by their bits, a long argument list as
  its length and 64-bit hash), and how the run ended with the script's final parameters.

## What it found

The first full run (465 modules, 52,854 scripts, 486,349 runs) is what made the following exact:

- **An evaluator fault is no value, not an error.** QB's expression evaluator (a precedence stack machine,
  `src/engine/script/expression.cpp`) clears itself on a fault - a type it can't apply, an index out of range, a name
  times a number - and the expression has no value; the script goes on. A missing `<x>` pushes nothing, which leaves the
  expression without a value too. `values.js` now gives no value for a fault (`binary`), and every operand is still read
  in order (JavaScript evaluates the arguments first) - the same calls happen.
- **Each TypeScript definition has its own number**: a translated script's body (`kTsBody`) names its definition, not its
  name, so the symbol table alone says which runs - as for QB (a temporary load undone puts the older definition back).
  The functions of an unloaded file are let go of (`set_qb_unloaded_hook`): without that, playing level after level
  filled QuickJS's heap.

2026-11-10: a module run alone agrees in every run; the full run's result is in the progress notes.

The full run then found these (each now matched by the translation, or the engine changed where the original's result
depended on the memory allocator - marked NOT ORIGINAL in the source):

- **A command's arguments read on past a Random's join.** `parse_arguments` follows a Random's jumps, so a command at
  the end of one branch reads its arguments on from where the branches join (the reader continues the line there).
- **A bare name before a Random draws it twice.** The look-ahead for `=` after a bare name evaluates (draws) a Random
  that follows; the statement then draws it again. `random.twice(k, ...)` draws once more and drops that pick.
- **A name with arguments inside an expression that isn't a C function** stops the evaluator; the statement runs the name
  with the leftover arguments, then loops for ever at `)`. The translation runs the same call, then ends the run as the
  statement budget would.
- **`RandomRange` of floats rounds each step to a float** - the original's x87 runs at single precision under Direct3D.
  Our C++ computes in `float` (a compiler that contracts `a * b + c` into one fused step, as GCC may, needs
  `-ffp-contract=off`).
- **`RandomNoRepeat` / `RandomPermute` state lasts as long as the script's definition** (NOT ORIGINAL). The original
  keys it by the address of the Random in memory: when the code is freed the entry stays, and whatever code is
  allocated there next takes it over; the pool keeps the newest 100. Here the script cache parks a script's entries when
  it evicts its code and puts them back when the script is fetched again; they are dropped when the definition changes
  or goes (with an unloaded file, its cached code too); a script value's running copies share its entries. TypeScript
  keys each site by module, script and call number and drops a module's sites when it is unloaded - so both pools hold
  the same entries in the same order, and the limit evicts the same ones.
- **A removed script isn't run from the script cache** (NOT ORIGINAL). Undoing a temporary load removes the scripts it
  added, then refreshes the cache by a name read from the freed symbol, as the original does - which misses, so the
  code stayed cached and a later call by that name ran it (a front-end script after a level had defined and dropped
  one of the same name). `fetch_script` now gives nothing for a name with no definition.

## Limits

The stubs are the world: what a command would really return, and the state it would change, isn't modelled - a branch
that only opens after a real sequence of events is reached only as far as forcing the stubs' results reaches it. Scripts
run on no object. Script values (`code(...)`, the CAS tables) aren't run as roots: the model builder runs them.
