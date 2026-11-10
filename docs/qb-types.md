# QB types, proven

`node tools/qbjs/typecheck.ts --game <folder> [--json out.json] [--doc docs/qb-types-unknowns.md]` works out, for every
parameter read in every script of the game, the set of kinds (int, float, string, localised string, pair, vector,
script, name, structure, array, none) it can hold - on every path, run or not. It is an abstract interpretation of the
whole program's IR (tools/qbjs/reader.ts) to a fixed point: deterministic - the same scripts give the same answer - and
sound - types only grow, so a proven type covers every value that can reach the read.

What it can't see it never assumes: an **unknown**, carried with the type as its reason, so a read is either proven or
names what keeps it from being proven. Every unknown is listed in [qb-types-unknowns.md](qb-types-unknowns.md)
(generated) and its artifact.

## What flows

- A script's parameters: its defaults, every call's arguments (a spread `<...>` passes the caller's whole list), what a
  called script returns, its own assignments, and what commands write back.
- A name read inside an expression stands for the global of that name when there is one (`values.js` `resolved`), so the
  checker tracks which names a value can hold; past 64 names it treats the value as any name (a precision loss,
  reported as such).
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
- **The C++ starting scripts.** A script the C++ names, or one named in data (a node's TriggerScript, a goal's script, a
  menu item's handler...), may be started by the engine with parameters it makes. Each mechanism that starts scripts is
  catalogued from the C++ with the parameters it passes.
- **Dynamic calls.** A command named by a parameter whose names can't be told reaches every script that can be named: its
  arguments go to all of them (one shared inbox), and what it writes back is an unknown.

## How it's used

The natural TypeScript steps simplify a form only where the type is proven: a parameter proven to be only an integer can
be a plain number, an operator proven to see only numbers can be `+`, a command proven never to wait needs no `yield*`.
Where a read has an unknown, the exact form stays. In the game the natural forms check their types where values enter -
arguments, command results, global reads: a value outside the proven set is a fault naming the site, never a wrong
computation. The in-game recording (`[debug] ts_types=1`, docs/qb-ts.md "Types") and the script check are tests the
proof must pass: a recorded type outside the proven set is a bug in the checker or a model.
