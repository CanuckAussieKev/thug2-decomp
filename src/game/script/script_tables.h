#pragma once

#include "game/script/names.h"
#include "hook/game_global.h"

namespace Script {

// The script CFunction table (script_tables.cpp, generated): 1254 {name, function} entries, and the 250 member-function
// names. AllocatePools registers both as script symbols.
GAME_GLOBAL_EXTERN(CFunctionEntry[1254], g_cfunctionTable);
GAME_GLOBAL_EXTERN(const char*[250], g_memberFunctionNames);

// The entry of the CFunction table with this name, or null.
CFunctionEntry* FindCFunction(const char* name);
// Point an entry at another function (the debug mode's CD / NotCD, the platform's IsPS2).
void SetCFunction(CFunctionEntry& entry, void* function);

} // namespace Script
