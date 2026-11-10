# QB types, proven

`node tools/qbjs/typecheck.ts --game <folder> [--json out.json] [--doc docs/qb-types-unknowns.md]` works out, for every
parameter read in every script of the game, the set of kinds (int, float, string, localised string, pair, vector,
script, name, structure, array, none) it can hold - on every path, run or not. It is an abstract interpretation of the
whole program's IR (tools/qbjs/reader.ts) to a fixed point: deterministic - the same scripts give the same answer - and
sound - types only grow, so a proven type covers every value that can reach the read.

What it can't see it never assumes: an **unknown**, carried with the type as its reason, so a read is either proven or
names what keeps it from being proven. Every unknown is listed in [qb-types-unknowns.md](qb-types-unknowns.md)
(generated) and its artifact.

**Result (2026-11-11): all 28,664 parameter reads of the game's 51,299 scripts are proven - no unknowns** (8,965 of a
single kind, the rest unions; ~15 minutes, under 4 GB). Tested against the script check's runs:
`node tools/qbjs/check_types_against_runs.ts out/scripts/types.json <game>/thug2-decomp-check/qb.txt` reads each
run's final parameters (384,325 runs, 83,586 members) and every kind seen is in the proven set - a kind outside it is
a bug in the checker or a model. (That test found two: RandomRange of whole endpoints is an integer; an unnamed
structure whose members are below the depth limit still gives the callee members.)

What the proof assumes, stated once:
- the catalogues are complete: start_mechanisms.json (every way the C++ starts a script from a name it was given),
  entry_models.json (by a fixed name), the command models (what each command writes back) - each record names the C++
  it was taken from;
- a name FormatText makes from a literal format is one whose text matches the format (each %-parameter any text)
  among the names the game's name tables spell;
- a value made outside the scripts (a command's output, a catalogue's member) that is a name may be any name, unless a
  catalogue says which.

## What flows

- A script's parameters: its defaults, every call's arguments (a spread `<...>` passes the caller's whole list), what a
  called script returns, its own assignments, and what commands write back.
- A member is always there only when every start gives it or the script's defaults have it; otherwise its type includes
  none (read when it was left out). Structures joined from values with different members likewise: a member one of them
  lacks may be none.
- A name read inside an expression stands for the global of that name when there is one (`values.js` `resolved`), so the
  checker tracks which names a value can hold. Past 2,048 names a value may be any name: sound, not an unknown - a call
  through it reaches every script that can be named. A name that may be any name read in an expression is the value of
  any global: all the globals joined.
- Shapes (members of structures, elements of arrays) are kept two levels deep. Below that a value keeps the kinds held
  there and the script and global names (a member or element read there is one of those kinds), so nothing below the
  limit is lost - only told apart less finely.
- Structures several things feed are shared, not copied: the inbox of dynamic calls' arguments, the pools
  (start_mechanisms.json), the union of all globals, of all commands' outputs, of the returns of every script that can be
  named. A value includes them by reference and a read goes through them; a script is walked again when one it reads
  grows. The inbox and the union of globals keep one level less of shape.
- `a.b` looks a member up only when `b` is a name; any other pair of kinds is the operator's (a pair's or vector's dot
  product, a float). RandomRange is an integer when both endpoints are whole (random.js `range`), else a float.
- Operators: each operator's result kinds for each pair of operand kinds are taken from `scripting/values.js` itself (run
  on samples), the runtime the script check proves against the game's interpreter - the checker and the runtime can't
  disagree. A fault is no value (none), as in the game.
- Globals: a module's globals are reduced to their types as it is read (a ported level's node arrays are millions of
  nodes).

## Where the unknowns come from

- **Commands.** A command writes into its caller's parameters (`GetArraySize`: `array_size`, an int). Each command's
  model comes from our C++: `node tools/qbjs/command_models.ts` reads the C functions mechanically (a body that never
  reaches the calling script has none; one that writes members under constant names has those), and the rest - helpers
  handed the context, names taken from arguments, structures merged in, and the member functions - are reviewed into
  `tools/qbjs/command_models_reviewed.json`, each model with its source. A model also says which scripts a command starts
  and with what (SpawnScript), and which globals it changes.
- **The C++ starting scripts.** Scripts the C++ starts by a fixed name are catalogued with the parameters it passes
  (`tools/qbjs/entry_models.json`). Scripts whose name the C++ gets from the scripts or the data are catalogued by
  mechanism in `tools/qbjs/start_mechanisms.json`: every call of the script host's start functions (spawn, launch,
  run once, bind / goto, preempt, event handlers, on-exit) in src/, traced back to where the name comes in - a command's
  argument path (SpawnScript's unnamed argument; `GoalManager_AddGoal params.init`; `SetQueueTricks` naming a global
  trick array whose elements' `Scr` run with their `Params`), a global the C++ reads, a node member, a member of any
  structure (a component's `UpdateScript`), a name pattern - with the parameters the started script gets. The checker
  follows script names through the scripts (arguments, structures, globals - kept below the depth limit too) to those
  paths. A structure several things feed (a goal's structure: AddGoal's params, a difficulty tier, EditGoal, the park
  editor's goal globals, the members the C++ writes over the goal's life) is a pool: every script started with it gets
  all of it. A script named in a way no mechanism starts isn't started by the C++: the catalogue is the claim, and each
  record lists the C++ sites it was taken from (debug links, test automation and dead code are listed as excluded).
- **Dynamic calls.** A call through a value whose names can't be told reaches every script that can be named - its
  arguments go to all of them, through one shared inbox - and every command: its arguments go through every command's
  start mechanisms, and what it writes back is what any command writes (the union of every model's outputs) or any
  script returns.
- **Events.** An event handler gets its row's params with the payload of the event that fires it: by event type, from
  the C++'s raisers (start_mechanisms.json `events`, 144 types), LaunchEvent / FireEvent's data under their type= names,
  animation event tables' rows, and EndGap's arguments under `<GapID>_Success` (the GapID's checksum folded on).
- **Command outputs from elsewhere.** A command that hands back a structure from elsewhere - the goal's (GetGoalParams),
  an object's tags (GetTags), a profile's info, an appearance part, an element of an argument's array, a global the C++
  names - says so in its model (`outputsFrom`); event handlers and on-exit scripts run as interrupts, so the returns of
  the scripts a command starts are merged into the caller.

## How it's used

The natural TypeScript steps simplify a form only where the type is proven: a parameter proven to be only an integer can
be a plain number, an operator proven to see only numbers can be `+`, a command proven never to wait needs no `yield*`.
Where a read has an unknown, the exact form stays. In the game the natural forms check their types where values enter -
arguments, command results, global reads: a value outside the proven set is a fault naming the site, never a wrong
computation. The in-game recording (`[debug] ts_types=1`, docs/qb-ts.md "Types") and the script check are tests the
proof must pass: a recorded type outside the proven set is a bug in the checker or a model.
