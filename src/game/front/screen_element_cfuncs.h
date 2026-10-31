#pragma once

#include <cstdint>

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

// The screen element script commands (the CFunction table's entries; cdecl).
namespace MenuSystem {

bool __cdecl CFunc_ScreenElementSystemInit(Scripts::CStruct* params, Scripts::CScript* script);   // 0x004AAD90
bool __cdecl CFunc_CreateScreenElement(Scripts::CStruct* params, Scripts::CScript* script);       // 0x004AD240
bool __cdecl CFunc_DestroyScreenElement(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AD300
bool __cdecl CFunc_RunScriptOnScreenElement(Scripts::CStruct* params, Scripts::CScript* script);  // 0x004AD370
bool __cdecl CFunc_SetScreenElementProps(Scripts::CStruct* params, Scripts::CScript* script);     // 0x004AD4C0
bool __cdecl CFunc_DoScreenElementMorph(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AD540
bool __cdecl CFunc_SetScreenElementLock(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AD5C0
bool __cdecl CFunc_GetScreenElementDims(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AD650
bool __cdecl CFunc_GetScreenElementProps(Scripts::CStruct* params, Scripts::CScript* script);     // 0x004AC750
bool __cdecl CFunc_SetRootScreenElement(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AC900
bool __cdecl CFunc_GetScreenElementChildren(Scripts::CStruct* params, Scripts::CScript* script);  // 0x004AC940
bool __cdecl CFunc_TextElementConcatenate(Scripts::CStruct* params, Scripts::CScript* script);    // 0x004AD710
bool __cdecl CFunc_TextElementBackspace(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AD800
bool __cdecl CFunc_GetTextElementString(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AD8D0
bool __cdecl CFunc_GetTextElementLength(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004AD9C0
bool __cdecl CFunc_GetScreenElementPosition(Scripts::CStruct* params, Scripts::CScript* script);  // 0x004ADA80
bool __cdecl CFunc_MenuSelectedIndexIs(Scripts::CStruct* params, Scripts::CScript* script);       // 0x004ADB20
bool __cdecl CFunc_ScreenElementExists(Scripts::CStruct* params, Scripts::CScript* script);       // 0x004ADC10
bool __cdecl CFunc_CreateMultipleTextElements(Scripts::CStruct* params, Scripts::CScript* script); // 0x004ADC80

extern uint32_t g_unassignedId;
} // namespace Front
