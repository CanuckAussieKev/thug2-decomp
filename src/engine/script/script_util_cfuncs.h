#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// The engine's general script commands (CFunctions), 0x0044B7E0-0x0044CFFF: the script's own parameters, random
// values, arrays (stepping through, random elements, ForEachIn, building and changing them), jumping to other
// scripts, checksums and text. All are cdecl (params, script) and return whether the script's `if` should be
// taken; results are left in the calling script's parameters (CScript::m_args).
namespace ScriptCommands {

bool __cdecl CFunc_RemoveParameter(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0044B7E0
bool __cdecl CFunc_RemoveFlag(Scripts::CStruct* params, Scripts::CScript* script);             // 0x0044B820
bool __cdecl CFunc_GetRandomValue(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0044B850
bool __cdecl CFunc_SizeOf(Scripts::CStruct* params, Scripts::CScript* script);                 // 0x0044B9A0
bool __cdecl CFunc_GetNextArrayElement(Scripts::CStruct* params, Scripts::CScript* script);    // 0x0044B9E0
bool __cdecl CFunc_GetRandomArrayElement(Scripts::CStruct* params, Scripts::CScript* script);  // 0x0044BBD0
bool __cdecl CFunc_GotParam(Scripts::CStruct* params, Scripts::CScript* script);               // 0x0044BD30
bool __cdecl CFunc_Goto(Scripts::CStruct* params, Scripts::CScript* script);                   // 0x0044BD60
bool __cdecl CFunc_GotoRandomScript(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044BDC0
bool __cdecl CFunc_FormatText(Scripts::CStruct* params, Scripts::CScript* script);             // 0x0044BE20
bool __cdecl CFunc_MangleChecksums(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0044BE40
bool __cdecl CFunc_AppendSuffixToChecksum(Scripts::CStruct* params, Scripts::CScript* script); // 0x0044BEC0
bool __cdecl CFunc_GetUpperCaseString(Scripts::CStruct* params, Scripts::CScript* script);     // 0x0044BF20
bool __cdecl CFunc_AddFlag(Scripts::CStruct* params, Scripts::CScript* script);                // 0x0044BFA0
bool __cdecl CFunc_AppendSuffix(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044BFD0
bool __cdecl CFunc_IsObjectScript(Scripts::CStruct* params, Scripts::CScript* script);         // 0x0044C120
bool __cdecl CFunc_ForEachIn(Scripts::CStruct* params, Scripts::CScript* script);              // 0x0044C290
bool __cdecl CFunc_GetElement(Scripts::CStruct* params, Scripts::CScript* script);             // 0x0044C430
bool __cdecl CFunc_PermuteArray(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044C5A0
bool __cdecl CFunc_GotoPreserveParams(Scripts::CStruct* params, Scripts::CScript* script);     // 0x0044C730
bool __cdecl CFunc_Dump(Scripts::CStruct* params, Scripts::CScript* script);                   // 0x0044C9B0
bool __cdecl CFunc_ScriptAssert(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044CB70
bool __cdecl CFunc_CreateIndexArray(Scripts::CStruct* params, Scripts::CScript* script);       // 0x0044CBC0
bool __cdecl CFunc_AppendStruct(Scripts::CStruct* params, Scripts::CScript* script);           // 0x0044CCC0
bool __cdecl CFunc_StringToCharArray(Scripts::CStruct* params, Scripts::CScript* script);      // 0x0044CDB0
bool __cdecl CFunc_SetArrayElement(Scripts::CStruct* params, Scripts::CScript* script);        // 0x0044CEC0

// 0x0044C840: the text a printf-like command shows, into `out` (followed by a newline): an unnamed string put
// together as FormatText would (with TextName=PrintfText, the parameters added; "" if that fails), else an
// unnamed integer; nothing is written without either. (`script` unused.) Always true.
bool __cdecl FormatPrintfText(char* out, Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
