#pragma once

#include "engine/script/names.h"
#include <array>

namespace script {

// The script Function table (script_tables.cpp, generated): 1254 {name, function} entries, and the 250 member-function
// names. AllocatePools registers both as script symbols.
std::array<NativeCommand, 1254>& cfunction_table();
std::array<const char*, 250>& member_function_names();

// The entry of the Function table with this name, or null.
NativeCommand* find_c_function(const char* name);
// Point an entry at another function (the debug mode's CD / NotCD, the platform's IsPS2).
void bind_native_command(NativeCommand& entry, NativeFunction function);

} // namespace script
