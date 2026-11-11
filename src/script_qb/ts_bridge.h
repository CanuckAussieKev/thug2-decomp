#pragma once

// NOT ORIGINAL - TypeScript scripts in the game (docs/qb-ts.md "In the game"). A translated module (tools/qbjs:
// emit_ts.ts, compile_ts.ts) is loaded after the .qb file it translates and replaces that file's scripts (its globals
// stay the ones the loader made from the same bytes): each of its scripts gets a symbol whose body is one kTsBody
// token, and when the interpreter reaches it on a call-stack
// level it runs that level in QuickJS (cscript_ts.cpp). Everything else - the record, its Update, waits, events,
// spawning, objects, the call stack - stays the interpreter's: a level's command calls are carried out by it.
//
// [features] ts_scripts=1 (default 0) turns it on; the modules are the game folder's thug2-decomp-ts\by-hash\<the .qb
// file's FNV-1a>.js (tools/qbjs/install_ts.ts), their compiled bytecode cached in thug2-decomp-ts\cache. [debug]
// ts_types=1 records the types each site sees into thug2-decomp-ts\types-recorded.json (added to over sessions; read
// by tools/qbjs/types.ts); ts_profile=1 logs where the time goes.

#include <cstdint>
#include <optional>
#include <string>

namespace script {
class Struct;
} // namespace script

namespace script::qb::ts {

// Read the setting and, when on, start QuickJS and hook the .qb loader (app/ calls it at start-up, before any .qb).
void install();
[[nodiscard]] bool enabled();
// QuickJS's interrupt budget for each run of it (0: the game's own) - the script check lowers it.
void set_interrupt_budget(uint32_t budget);
// The functions the TypeScript module of .qb file `file_checksum` defined let go of (as when the file is unloaded);
// `temporary_only`: only those its temporary loads defined - a temporary load undone (the script check), the file's
// own load's kept: the symbols it put back still name them.
void forget_file(uint32_t file_checksum, bool temporary_only = false);
// The .qb file `file_checksum` (its name's checksum) as last loaded: its bytes' hash (FNV-1a, as tools/qbjs/install_ts.ts
// names modules) and whether its TypeScript module ran - none if it hasn't been loaded. (The script check reports which
// version of a file the game ran, and fails a TypeScript pass whose file had no module.)
struct LoadedFile {
    uint32_t hash = 0;
    bool module = false;
};
[[nodiscard]] std::optional<LoadedFile> loaded_file(uint32_t file_checksum);

// What a level asks for when it stops running.
struct Answer {
    bool done = false;          // it ended: its return values are in the statement arguments given
    uint32_t call = 0;          // else the command to run (its arguments in the statement arguments given)
    std::optional<uint32_t> on; // object:Command - the object's name
    std::string error;          // a JavaScript fault (the level is to end)
};

// A level of the script defined as `definition` (the number its kTsBody names; `title`: its name, for the profile)
// begun with the record's parameters `args` (owner: the record's unique id - Random's no-repeat state is kept per
// script, as the original's). 0 on failure (`error` says why).
uint32_t begin(uint32_t definition, uint32_t title, Struct* args, uint32_t owner, std::string& error);
// The level runs on: `args` is the record's parameter list (the level's view is refreshed from it, and its own changes
// written back into it), `result` the last command's result; `stmt` gets the next command's arguments or the return
// values.
Answer resume(uint32_t level, Struct* args, bool result, Struct* stmt);
// A level gone (its record reset or torn down).
void drop(uint32_t level);

// The game's frames, counted where the spawned scripts are updated (cscript_manager.cpp tick_child_scripts): a level
// recording types tells a call that waited from one that didn't.
inline uint32_t& frame_clock()
{
    static uint32_t frames = 0;
    return frames;
}

} // namespace script::qb::ts
