#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B34A0-0x005B3A90: two objects swapping their models (renderable models,
// skeletons, shadows - a skater wearing another object's model), all swaps undone, and whether a skater's model is
// swapped. The swaps are recorded in a table of five entries at 0x007CEDE0 (0x005AA930 keeps it). All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace ScriptCommands {

bool __cdecl CFunc_SwapObjectModels(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_RestoreAllSwappedObjectModels(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetSkaterModelIsSwapped(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
