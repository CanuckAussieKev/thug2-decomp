#pragma once

#include <cstdint>

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
bool __cdecl CFunc_EnableScriptPrintf(Script::CStruct* params, Script::CScript* script);   // 0x0044A900
bool __cdecl CFunc_IfDebugOn(Script::CStruct* params, Script::CScript* script);            // 0x0044A930
bool __cdecl CFunc_StructureContains(Script::CStruct* params, Script::CScript* script);    // 0x0044A940
bool __cdecl CFunc_ClearStruct(Script::CStruct* params, Script::CScript* script);          // 0x0044A9B0
bool __cdecl CFunc_ScriptExists(Script::CStruct* params, Script::CScript* script);         // 0x0044A9E0
bool __cdecl CFunc_IsArray(Script::CStruct* params, Script::CScript* script);              // 0x0044AA20
bool __cdecl CFunc_Block(Script::CStruct* params, Script::CScript* script);                // 0x0044AA50
bool __cdecl CFunc_IsTrue(Script::CStruct* params, Script::CScript* script);               // 0x0044AAB0
bool __cdecl CFunc_CleanUpAndRemoveSymbol(Script::CStruct* params, Script::CScript* script); // 0x0044AAE0
bool __cdecl CFunc_SetEventHandler(Script::CStruct* params, Script::CScript* script);           // 0x0044AB20
bool __cdecl CFunc_SetExceptionHandler(Script::CStruct* params, Script::CScript* script);       // 0x0044ABD0
bool __cdecl CFunc_ResetEventHandlersFromTable(Script::CStruct* params, Script::CScript* script); // 0x0044AC70
bool __cdecl CFunc_ClearEventHandler(Script::CStruct* params, Script::CScript* script);         // 0x0044ACD0
bool __cdecl CFunc_ClearEventHandlerGroup(Script::CStruct* params, Script::CScript* script);    // 0x0044AD00
bool __cdecl CFunc_OnExceptionRun(Script::CStruct* params, Script::CScript* script);            // 0x0044AD30
bool __cdecl CFunc_OnExitRun(Script::CStruct* params, Script::CScript* script);                 // 0x0044AD60
bool __cdecl CFunc_PrintEventHandlerTable(Script::CStruct* params, Script::CScript* script);    // 0x0044AD90
bool __cdecl CFunc_StopAllScripts(Script::CStruct* params, Script::CScript* script);            // 0x0044ADA0
bool __cdecl CFunc_FirstTimeThisIsCalled(Script::CStruct* params, Script::CScript* script);     // 0x0044ADB0
bool __cdecl CFunc_SetScriptWarningLevel(Script::CStruct* params, Script::CScript* script);     // 0x0044ADD0
bool __cdecl CFunc_CastToInteger(Script::CStruct* params, Script::CScript* script);             // 0x0044AE30
bool __cdecl CFunc_StringToInteger(Script::CStruct* params, Script::CScript* script);           // 0x0044AEB0
bool __cdecl CFunc_AddParams(Script::CStruct* params, Script::CScript* script);                 // 0x0044B720
bool __cdecl CFunc_RemoveComponent(Script::CStruct* params, Script::CScript* script);           // 0x0044B740
bool __cdecl CFunc_GetScriptString(Script::CStruct* params, Script::CScript* script);           // 0x0044B790

// The script warning level (0x006F3534): 0 none, 1 warn, 2 assert (SetScriptWarningLevel).
int& ScriptWarningLevel();

// 0x0044A480 (cdecl): FormatText's work (its callers 0x0044BE2D and 0x0044C8F5). The unnamed string of `params` is
// the format: "%x" (one character) and "%%name" (letters, digits, _) stand for the parameter of that name, written
// as text - integers ("%d"; integer_width: "%0*d"; UseCommas: with the language's thousands separators), floats
// (decimalplaces, default 3), strings, pairs and vectors ("(%.3f,%.3f[,%.3f])"), names (their text - the result
// is then 2); "\%" is a literal '%'. The text goes to a shared buffer (0x00696940) and, as textname=<name> and
// ChecksumName=<name> (its checksum), into `results`. Returns 0 with no format or a missing parameter (printed),
// else 1 (2 if a name was written).
int FormatText(Script::CStruct* results, Script::CStruct* params);
// FormatText's text buffer (0x00696940; its end, 0x00696D2C, isn't checked).
char* FormatTextBuffer();

} // namespace CFuncs
