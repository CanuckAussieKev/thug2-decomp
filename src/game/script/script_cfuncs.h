#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for the script system itself, 0x00449D00-0x0044B7E0: changing globals, waiting,
// comparisons, the script strings, checking globals and structures, blocking, event handlers, stopping scripts,
// the warning level, integer conversions, adding and removing parameters. All are cdecl (params, script) and return
// whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_Change(Script::CStruct* params, Script::CScript* script);          // 0x00449D00
bool __cdecl CFunc_Wait(Script::CStruct* params, Script::CScript* script);            // 0x0044A0D0
bool __cdecl CFunc_IntegerEquals(Script::CStruct* params, Script::CScript* script);   // 0x0044A200
bool __cdecl CFunc_ChecksumEquals(Script::CStruct* params, Script::CScript* script);  // 0x0044A260
bool __cdecl CFunc_StringEquals(Script::CStruct* params, Script::CScript* script);    // 0x0044A2C0
bool __cdecl CFunc_SetScriptString(Script::CStruct* params, Script::CScript* script); // 0x0044A320
bool __cdecl CFunc_GlobalExists(Script::CStruct* params, Script::CScript* script);    // 0x0044A380

} // namespace CFuncs
