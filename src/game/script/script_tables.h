#pragma once

#include "game/script/names.h"

namespace Script {

// The script CFunction table (script_tables.cpp, generated): 1254 {name, function} entries, and the 250 member-function
// names. AllocatePools registers both as script symbols.
extern CFunctionEntry g_cfunctionTable[1254];
extern const char* g_memberFunctionNames[250];

// The entry of the CFunction table with this name, or null.
CFunctionEntry* FindCFunction(const char* name);
// Point an entry at another function (the debug mode's CD / NotCD, the platform's IsPS2).
void SetCFunction(CFunctionEntry& entry, void* function);

} // namespace Script
