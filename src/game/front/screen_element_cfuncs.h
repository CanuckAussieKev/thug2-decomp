#pragma once

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The screen element script commands (the CFunction table's entries; cdecl).
namespace Front {

bool __cdecl CFunc_ScreenElementSystemInit(Script::CStruct* params, Script::CScript* script);   // 0x004AAD90
bool __cdecl CFunc_CreateScreenElement(Script::CStruct* params, Script::CScript* script);       // 0x004AD240
bool __cdecl CFunc_DestroyScreenElement(Script::CStruct* params, Script::CScript* script);      // 0x004AD300
bool __cdecl CFunc_RunScriptOnScreenElement(Script::CStruct* params, Script::CScript* script);  // 0x004AD370
bool __cdecl CFunc_SetScreenElementProps(Script::CStruct* params, Script::CScript* script);     // 0x004AD4C0
bool __cdecl CFunc_DoScreenElementMorph(Script::CStruct* params, Script::CScript* script);      // 0x004AD540
bool __cdecl CFunc_SetScreenElementLock(Script::CStruct* params, Script::CScript* script);      // 0x004AD5C0
bool __cdecl CFunc_GetScreenElementDims(Script::CStruct* params, Script::CScript* script);      // 0x004AD650
bool __cdecl CFunc_GetScreenElementProps(Script::CStruct* params, Script::CScript* script);     // 0x004AC750
bool __cdecl CFunc_SetRootScreenElement(Script::CStruct* params, Script::CScript* script);      // 0x004AC900
bool __cdecl CFunc_GetScreenElementChildren(Script::CStruct* params, Script::CScript* script);  // 0x004AC940
bool __cdecl CFunc_TextElementConcatenate(Script::CStruct* params, Script::CScript* script);    // 0x004AD710
bool __cdecl CFunc_TextElementBackspace(Script::CStruct* params, Script::CScript* script);      // 0x004AD800
bool __cdecl CFunc_GetTextElementString(Script::CStruct* params, Script::CScript* script);      // 0x004AD8D0
bool __cdecl CFunc_GetTextElementLength(Script::CStruct* params, Script::CScript* script);      // 0x004AD9C0
bool __cdecl CFunc_GetScreenElementPosition(Script::CStruct* params, Script::CScript* script);  // 0x004ADA80
bool __cdecl CFunc_MenuSelectedIndexIs(Script::CStruct* params, Script::CScript* script);       // 0x004ADB20
bool __cdecl CFunc_ScreenElementExists(Script::CStruct* params, Script::CScript* script);       // 0x004ADC10
bool __cdecl CFunc_CreateMultipleTextElements(Script::CStruct* params, Script::CScript* script); // 0x004ADC80

} // namespace Front
