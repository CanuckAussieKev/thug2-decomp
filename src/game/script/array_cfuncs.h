#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for arrays, 0x0044AF50-0x0044B530: searching, sizes, and elements of arrays of arrays.
// All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_ArrayContains(Script::CStruct* params, Script::CScript* script);  // 0x0044AF50
bool __cdecl CFunc_GetArraySize(Script::CStruct* params, Script::CScript* script);   // 0x0044B030
bool __cdecl CFunc_Get2DArrayData(Script::CStruct* params, Script::CScript* script); // 0x0044B0F0
bool __cdecl CFunc_Get3DArrayData(Script::CStruct* params, Script::CScript* script); // 0x0044B300
bool __cdecl CFunc_GetNDArrayData(Script::CStruct* params, Script::CScript* script); // 0x0044B530

} // namespace CFuncs
