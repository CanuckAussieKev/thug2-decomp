#pragma once

#include <cstdint>

#include "game/script/struct.h"

// Loading .qb files: defining their global symbols (values, structures, arrays and compressed scripts) and
// restarting the running scripts whose code changed.
namespace Script {

class CScript;

// Define the global symbol `name` from the value at `token` (a .qb file's "name = value").
const uint8_t* ParseSymbolDefinition(const uint8_t* token, uint32_t name, const char* filename);
// Make the script symbol `name` from `size` bytes of compiled code (compressed when that's smaller).
CSymbolTableEntry* CreateScriptSymbol(uint32_t size, const uint8_t* code, uint32_t name, uint32_t checksum,
                                      const char* filename);
// Define everything in the .qb file `data` (old format). With `useQbNames`, the file's checksum names go in
// the .qb name table; with `temporary`, every change is undone by ProcessPendingSymbols.
void ParseQB(const char* filename, const uint8_t* data, int unused, int useQbNames, int temporary);
// Restart the scripts running (or calling) a script that was just (re)defined.
void RestartChangedScripts();
// Whether `script` is running `name` or has it on its call stack.
bool ScriptRefersTo(const CScript* script, uint32_t name);
// Define a .qb file already in memory (old or pre-compiled format), then restart changed scripts.
void LoadQBData(const char* filename, const uint8_t* data, int unused, int useQbNames, int temporary);
// Load and define the .qb file `filename`.
void LoadQB(const char* filename, int unused, int temporary);

// The entry points for the game's callers, which pass arguments in registers (also called by the self-tests).
void ParseSymbolDefinitionStub();
void CreateScriptSymbolStub();

} // namespace Script
