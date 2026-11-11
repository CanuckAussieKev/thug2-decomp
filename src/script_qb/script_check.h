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

// Decisions (docs/script-check.md "Branches"): an if at `place` in the running script's code whose condition came out
// `condition` - the truth to go on (the check may force the other way); a switch whose case label `natural` of
// `arms` - 1 matched (arms - 1: none, the default or past the endswitch) - the arm to take; a TypeScript script's
// decision (kind 0 if, 1 switch, 2 Random: its arm `natural` of `arms`). Outside the check: as they came.
// (`forceable` false: the if's condition stopped before its line's end - taken as it came, and reported.)
bool decide_if(const Script* script, std::ptrdiff_t place, bool condition, bool forceable);
int decide_case(const Script* script, std::ptrdiff_t place, int natural, int arms);
// (TypeScript's: `script` / `place` - the running script and the decision's place in its QB code, as QB's places - make
// it TypeScript coverage; the check's translation passes them, script 0 none)
uint32_t decide_ts(uint32_t kind, uint32_t natural, uint32_t arms, uint32_t script = 0, int64_t place = -1);
// A TypeScript statement at `place` in script `script`'s QB code about to run (TypeScript coverage - the check's
// translation's reach()).
// False: the run is over - the translation's code is to stop (a loop without calls ends there).
bool reach_ts(uint32_t script, int64_t place);
// The end of a pass through a `repeat <count>` loop at `place`: whether to go round again (the count's answer: `again`).
bool decide_repeat(const Script* script, std::ptrdiff_t place, bool again);
// A script about to be called from a script at call depth `depth` (QB's or TypeScript's - both call through the
// interpreter): whether to run it (the check may skip it: the call returns at once, true).
bool decide_call(const Script* script, int depth);
// A statement at `place` in the running script's code about to run (QB coverage).
void note_statement(const Script* script, std::ptrdiff_t place);

// A statement about to run: true when the run is over (its statement or call budget spent) - the interpreter stops.
bool over();
// True when a run is going and has ended (a budget spent): TypeScript stops at its next interrupt check - a level's
// loop that only calls stubs and C functions would otherwise go on until its interrupt budget runs out.
[[nodiscard]] bool ended();
// A TypeScript level's fault (its message): the run ends there (a runaway loop's fuel, or a real fault).
void ts_fault(const std::string& message);

// The extension that runs the check at the first front-end frame when the command line asks for it.
ext::Extension& extension();

} // namespace script::qb::check
