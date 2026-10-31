#pragma once

#include <cstdint>

#include "engine/script/param_struct.h"

// Loading .qb files: defining their global symbols (values, structures, arrays and compressed scripts) and
// restarting the running scripts whose code changed.
namespace script {

class Script;

// Define the global symbol `name` from the value at `token` (a .qb file's "name = value").
const uint8_t* parse_symbol_definition(const uint8_t* token, uint32_t name, const char* filename);
// Make the script symbol `name` from `size` bytes of compiled code (compressed when that's smaller).
SymbolTableEntry* define_script_symbol(uint32_t size, const uint8_t* code, uint32_t name, uint32_t checksum,
                                       const char* filename);
// Define everything in the .qb file `data` (old format). With `use_qb_names`, the file's checksum names go in
// the .qb name table; with `temporary`, every change is undone by process_pending_symbols.
void read_qb_file(const char* filename, const uint8_t* data, int unused, int use_qb_names, int temporary);
// Restart the scripts running (or calling) a script that was just (re)defined.
void restart_changed_scripts();
// Whether `script` is running `name` or has it on its call stack.
bool script_refers_to(const Script* script, uint32_t name);
// Define a .qb file already in memory (old or pre-compiled format), then restart changed scripts.
void load_qb_data(const char* filename, const uint8_t* data, int unused, int use_qb_names, int temporary);
// Load and define the .qb file `filename`.
void load_qb(const char* filename, int unused, int temporary);
// Remove every symbol defined by the .qb file whose name's checksum is `file_checksum` (nothing for 0), stop the
// scripts running code that's gone, and unload the file if it's still loaded (pre-compiled ones are).
void unload_qb(uint32_t file_checksum);

// The entry points for the game's callers, which pass arguments in registers (also called by the self-tests).
void parse_symbol_definition_stub();
void create_script_symbol_stub();

} // namespace script
