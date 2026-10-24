#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B5770-0x005B5BE0: the skaters' set-up between levels (input reset and taken out
// of the world, shown again in their heaps), their scores (reset, refreshed, the score pot), and the nearest
// skater in reach of another team (or carrying the flag). All are cdecl (params, script) and return whether the
// script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_InitializeSkaters(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ReinsertSkaters(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ResetScore(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_UpdateScore(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ResetScorePot(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_GetCollidingPlayerAndTeam(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
