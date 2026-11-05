#pragma once

#include "engine/script/names.h"
#include <array>

namespace script {

constexpr size_t kCFunctionCount = 1254;
constexpr size_t kMemberFunctionNameCount = 250;
// The script Function table (app/command_table.cpp, generated): 1254 {name, function} entries, and the 250
// member-function names. AllocatePools registers both as script symbols. The app makes them, in the original's order,
// and hands them over once at start-up, before anything reads them (app::install_command_tables); the engine only
// holds the pointers.
void install_command_tables(std::array<NativeCommand, kCFunctionCount>& commands,
                            std::array<const char*, kMemberFunctionNameCount>& member_names);
std::array<NativeCommand, kCFunctionCount>& cfunction_table();
std::array<const char*, kMemberFunctionNameCount>& member_function_names();

// The entry of the Function table with this name, or null.
NativeCommand* find_c_function(const char* name);
// Point an entry at another function (the debug mode's CD / NotCD, the platform's IsPS2).
void bind_native_command(NativeCommand& entry, NativeFunction function);

} // namespace script
