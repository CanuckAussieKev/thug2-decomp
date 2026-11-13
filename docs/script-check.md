# The script check

Every script of the game run headless on the game's own interpreter - each branch forced in turn - once as QB and once
as the translated TypeScript, with the same choices; the two traces must be the same, call for call. It proves the
TypeScript against the decompiled interpreter itself (`src/script_qb`), not against a port of its rules, and reports
which branches the runs reached.

```
node tools/qbjs/script_check.ts plan --game C:\Games\THUG2-1 [--only scripts\game\x.qb,...] [--runs 64]
tools/game/game_lock.ps1 -Action acquire -Owner <you> -Game C:\Games\THUG2-1
tools/game/deploy_build.ps1 -Game C:\Games\THUG2-1 -Arch x64 -BuildDir out\build\<build> -NoEditorTools
tools/game/script_check.ps1 -Game C:\Games\THUG2-1          # both passes (the ts one unpacked: unpack_game.ts --check); each quits by itself
node tools/qbjs/script_check.ts compare --game C:\Games\THUG2-1
tools/game/game_lock.ps1 -Action release -Owner <you> -Game C:\Games\THUG2-1
```

The whole plan split across workspaces (each holding its lock) - each gets the build, the TypeScript and a share of the
modules (`shares --game a,b,...` then `plan --share`: each module to one folder that has it - their data overlays differ -, about as many scripts each; the argument sets still come from every module's calls), the
shares run side by side, and `compare --game a,b,...` writes one report in the first folder:

```
tools/game/script_check_parallel.ps1 -Games C:\Games\THUG2-1,C:\Games\THUG2-2,C:\Games\THUG2-3,C:\Games\THUG2-4
```

Each pass has a time limit (the game is closed at it, and `script_check.ps1` says how far the plan got:
`ts pass NOT FINISHED - it got to <module> (n of m)`); the parallel run gives each share 1.5 s a script, 6 hours at the
least. The TypeScript pass is the long one - about a second a script against QB's ~0.04 s (QuickJS running the values
layer; a subset's TypeScript took as long before the globals moved to `.json` as after): four workspaces take ~4 hours,
two ~8.

`plan --log-runs` logs each run's header before it runs and its end and time after (finding a run that crashes or
hangs the game).

## How it runs

`thug2-decomp.exe -script-check <folder>` starts the game as always - the engine, its pools, the command table, the boot
scripts - and at the first front-end frame runs the plan instead of the game, then quits (`src/script_qb/script_check.cpp`).

- **The plan** (`thug2-decomp-check\plan.qb` and one part per module, written as QB globals so the game reads them with
  its own loader): every .qb the game may load - Data's loose files, the copies in the Data\pre archives and the data
  overlay's; each script with the argument sets to
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
  set) is spent. Then every branch no run has taken is forced (below). Coverage is recorded by the interpreter itself:
  each if's arm, each switch's arm, each Random's branch, and each statement run (`qb.txt`'s `cover` lines).
- **TypeScript's coverage** is measured in its own runs, not taken from QB's: the check's translation (`unpack_game.ts
  --check`) starts each statement with `reach(place)` and passes each decision's place to `branch` / `arm` / `repeat` /
  a Random (`[k, place]`) - the place in the script's QB code, so the counts are QB's. The game records them only for
  code that ran as TypeScript (`ts.txt`'s `cover` lines; a module that ran as QB counts nothing) and the report gives
  TypeScript's branches, statements and scripts beside QB's, and what it didn't reach.
- **Versions**: the game looks for a file in the data overlay, then in the archives it has loaded, then in Data, and 130
  of the archives' .qb differ from the loose file of the same name (a level's scripts with the Printfs taken out). Every
  version is installed as TypeScript (each module is named by its bytes' hash) and written to
  `thug2-decomp-check-sources`; each module's section of the traces says which version ran (`loaded <hash>`, and
  `no-typescript` when the TypeScript pass found no module for it - the report lists those: they ran as QB, untested),
  and coverage is counted against that version. That is the version the game reads at the front end; so that every
  version is run, a file with more than one is also planned once per version (`<file>@<hash>`, its own part and
  coverage): the check loads that version's bytes as a temporary load (`load_qb_data`), and with ts_scripts=1 that
  version's `.ts` where unpack_game.ts put it (`ts::read_version_next`). Versions the game never runs aren't planned:
  the unpatched keyboard.qb beside its patched one. The French and German archives' copies aren't either.
- **Pools**: the check runs with four times the default pools (`script_check.ps1` writes `[memory]`): a level's file is
  loaded on top of the boot scripts, and a converted one can be large (THPG's whole world ran the arrays dry). A run that fills a quarter of what a script pool (structures, components, arrays, vectors) had free at its
  start ends `exhausted` - an array doubled pass after pass; the game would run out there and crash. TypeScript's values
  aren't in the pools, so these runs aren't replayed; the report counts them.
- **Waits** run for real but end at the next frame: nothing else in a run sees time pass (the commands are stubs), so a
  `Wait 45 seconds` costs one frame and the code after it is reached within the frame budget.
- **Replaying** (TypeScript, `ts_scripts=1`): every QB run again with the same seed and choices, its trace compared with
  QB's there and then; `ts.txt` has each module's count and every run that differed, with its first differing line.
- **Globals**: every symbol a file defines but its scripts, as its value's text hashed (`global` lines) - QB's from the
  `.qb`, TypeScript's from the file's `.json` (docs/qb-ts.md "Data") - compared by `compare`.
- **Trace**: each stub call (the frame, the name, the object, the arguments - floats by their bits, a long argument list as
  its length and 64-bit hash), each forced decision, and how the run ended: the number of decisions and a hash of what
  each would have been and took, then the script's final parameters.

## Branches

A *decision* is an if (two arms: true, false), a switch (one arm per case label in order, and one more: the default, or
none matched), a Random (one arm per branch), the end of a pass through a `repeat <count>` loop (again, or out - the
count may be a parameter no argument set gives; forced "again" makes one more pass), or a script call (run it, or skip it: it returns at once). Every decision in a run is counted in order; a run's job can force a
decision, by its number in the run, to an arm whatever its condition or draw gave. The condition is still evaluated (its
calls traced) and what it gave goes into the run's decision hash, so QB's and TypeScript's conditions are compared even
where the arm is forced.

- QB forces in the interpreter (`run_conditional`, `run_multi_branch`, `resolve_random_jumps` - each NOT ORIGINAL there and
  doing nothing outside the check). TypeScript forces through `branch(truth)` and `arm(natural, arms)`, which the check's
  translation (`unpack_game.ts --check`, `emitTS(..., { check: true })`) writes around each if's condition and each
  switch; every Random goes through the same native (`__qbGame.decide`). The game's translation has neither.
- Exploring forces each arm no run of the module has taken from the first run that reached its decision: that run's seed,
  its stub choices and forced decisions up to there - the same path - then the arm. Each arm is tried at most three times.
  The run header is `run <script> <argument set> <seed> <stub choices> <forced decisions: number:arm,...>`.
- A script call is skipped only to reach the code after a call that never returned within a run's budget (a callee
  that loops for ever): once per call site. Skips aren't coverage.
- An if whose condition stops before its line's end (an expression the evaluator gave up on, e.g. a member function
  in an expression) isn't forced: its condition comes out the same every time, and its true part would start in the
  middle of the line. The arm it can't take is listed in the report, not counted, and the code of its true part counts
  as unreachable (the original never takes it, or goes on mid-line and stays at the expression's `)` for ever - THP8's
  converted `if NOT (GMan_GoalIsActive goal=...)`), as does a false part never taken.
- A run that makes its call budget's calls while still reaching statements it hadn't (a long straight run: a level's
  650 animation loads) is made once more as a *long run*: on until 600 calls go by with nothing new (at most 100,000).
  Its replay gets QB's number of calls as its budget.
- Each run starts with no stored `RandomNoRepeat` / `RandomPermute` state (both languages share the game's pool), so a
  job made again takes the same path - forcing a decision by its number needs that.
- A boot file whose scripts the game has since redefined or removed (the front end's level file) is loaded again for
  its module, as a level's is. With `new_frontend=1` / `anteara_mod=1` the overlay's `front_end` / `anteara` copy is
  the version the game reads, and is planned as one. keyboard.qb is loaded with PartyMod's binary patch applied when
  it is the file the patch was made for (src/engine/script/patched.cpp): the patched bytes are a version of their own
  (`<origin>+partymod`, game_scripts.ts `qbVersions`), so the game has a TypeScript module for them and the check plans them.
- A run's first 300 calls are written to its trace; the rest (up to the `calls` budget, 600) are counted and hashed on
  its end line.
- A run that ends at a budget (`runaway`: QB's statement budget, 50,000, and TypeScript's interrupt budget - one check
  per 100 of QB's statements - stop a loop without calls at different statements; `overlong`: the call budget) is
  compared up to its last stub call: its frame, its decisions then, its trace to there (the forced decisions after it
  aren't written). Replaying a run QB ended runaway, TypeScript is compared up to QB's last call and then runs on as
  QB's did - for its coverage: the loops QB went round after its last call - until a call QB didn't make or QB's
  statement budget counted in its own statements (`reach`; past it `reach` throws, which ends a loop without calls) -
  or, sooner, once 300 of them in a row reach no statement or branch arm not reached before (a loop going round
  again: running all 50,000 of every runaway replay made the TypeScript pass hours long; 2,000 still spent ~45% of a
  level's TypeScript time in loops that read a big global each pass, with the same coverage at 300). An if QB found fixed (its
  condition stops before its line's end: the original stays on that line) counts for TypeScript once it reaches the
  line - its run ends there, at the call the condition makes past QB's last. So a TypeScript run that would have gone on to make a further call, or finished, after QB's last one isn't caught
  there; any difference up to that call is.
- A temporary load (a level's file, or a boot file loaded again for its module) defines its TypeScript under an owner
  of its own; undoing it lets go of only those definitions - the file's own load's stay, as the symbols put back name
  them (Anteara Mod's files redefine each other's scripts: letting go of all of them faulted every later caller).
- The report counts branches and statements against every one the scripts have, less those no run can reach: the
  statements after a `return`, or after a loop that never ends (no count, no `break` out of it), in the same block - the
  original can't run them either (`script_check.ts unreachable`).

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

Forcing every branch found two more translation differences, both on paths no earlier run reached:

- **A negative number after a name that isn't a C function is a subtraction** (`globe_vel -0.1`, parse_expression);
  the reader had taken it as the name's arguments. Only a C function reads it as its argument (push_name).
- **`(expression):Command` where the expression gives no name** is the statement's truth alone; the next statement
  starts at the `:` and runs `Command` naming no object (the game then stays there for ever - it finds no object 0; the
  check, with no objects, runs it). The translation names no object (0) there instead of failing.
- **A script's defaults are parsed with no parameter list**, not an empty one: `params = <...>` there adds nothing
  (add_parameter with no args), where an empty list would add an empty structure.

## Limits

The stubs are the world: what a command would really return, and the state it would change, isn't modelled. A branch
that no input reaches is forced, so its code runs on whatever state the run has there - the same for QB and TypeScript,
but not necessarily a state the game could be in. Scripts
run on no object. Script values (`code(...)`, the CAS tables) aren't run as roots: the model builder runs them.
