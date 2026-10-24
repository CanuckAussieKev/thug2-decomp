#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B34A0-0x005B3A90: two objects swapping their models (renderable models,
// skeletons, shadows - a skater wearing another object's model), all swaps undone, and whether a skater's model is
// swapped. The swaps are recorded in a table of five entries at 0x007CEDE0 (0x005AA930 keeps it). All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_SwapObjectModels(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_RestoreAllSwappedObjectModels(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetSkaterModelIsSwapped(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
