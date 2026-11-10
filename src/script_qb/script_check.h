#pragma once

// NOT ORIGINAL - the script check (docs/script-check.md): every script run on the real interpreter, headless, against
// stubbed commands, with each branch forced in turn - once as QB and once as TypeScript (the same plan, the same
// choices), and the two traces compared by tools/qbjs/script_check.ts.
//
// `thug2-decomp.exe -script-check <folder>` (a workspace, under its lock): at the first front-end frame - the engine
// started and the boot scripts loaded as in the game - it reads <folder>\plan.qb (written by script_check.ts: the
// modules, their scripts, the arguments to call each with, the budgets), runs the plan and quits. With [features]
// ts_scripts=0 it explores (qb.txt: every run's choices, its trace, the branches it took); with ts_scripts=1 it replays
// qb.txt's runs (ts.txt).
//
// What a run is: the script started on a record of its own (no object), its arguments one of the plan's sets, the
// random generator seeded per run, frames stepped (the game's clock with them) until it ends or runs out of frames.
// Scripts run for real - called ones too. Every C function and member function a script calls is a stub (a few that
// only read the script's own parameters or wait are real: kRealCommands in script_check.cpp): its call is traced and
// its result (true / false) is the run's next choice. object:Command is a stub too (there are no objects).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "engine/script/cell.h"

namespace ext {
class Extension;
} // namespace ext

namespace script {
class Script;
class Struct;
} // namespace script

namespace script::qb::check {

// Whether the check is running (never in the game).
[[nodiscard]] bool active();

// A command the interpreter is about to run natively (a C function or member function; `object`: object:Command's
// object, 0 for none): the stub's result - or none: run it (the check isn't running, or it's one of the real ones).
std::optional<bool> command(const Script* script, uint32_t name, uint32_t object, Struct* args);
// The same for a statement prepared to call a C function directly (its pointer).
std::optional<bool> native_function(const Script* script, NativeFunction function, Struct* args);

// Coverage (QB): an if's condition at `place` in the running script's code taken (true) or not; a switch at `place`
// went to the case at `chosen` (-1: none matched).
void note_if(const Script* script, std::ptrdiff_t place, bool taken);
void note_case(const Script* script, std::ptrdiff_t place, std::ptrdiff_t chosen);

// A statement about to run: true when the run is over (its statement or call budget spent) - the interpreter stops.
bool over();
// A TypeScript level's fault (its message): the run ends there (a runaway loop's fuel, or a real fault).
void ts_fault(const std::string& message);

// The extension that runs the check at the first front-end frame when the command line asks for it.
ext::Extension& extension();

} // namespace script::qb::check
