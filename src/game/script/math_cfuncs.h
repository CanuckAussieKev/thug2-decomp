#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for maths, 0x00448AE0-0x00448EF0: trigonometry in degrees, square roots, vector
// normalising and rotating, linear mapping and clamping. Each reads its arguments from `params` and leaves its
// result in the calling script's parameters (CScript::m_params) under a fixed name. All are cdecl (params,
// script) and return true. A missing asserted argument leaves its value undefined in the original (the getter's
// compiled-out assert); here it is 0.
namespace CFuncs {

bool __cdecl CFunc_Sin(Script::CStruct* params, Script::CScript* script);             // 0x00448AE0
bool __cdecl CFunc_Cos(Script::CStruct* params, Script::CScript* script);             // 0x00448B20
bool __cdecl CFunc_Tan(Script::CStruct* params, Script::CScript* script);             // 0x00448B60
bool __cdecl CFunc_ASin(Script::CStruct* params, Script::CScript* script);            // 0x00448BA0
bool __cdecl CFunc_ACos(Script::CStruct* params, Script::CScript* script);            // 0x00448BE0
bool __cdecl CFunc_ATan(Script::CStruct* params, Script::CScript* script);            // 0x00448C20
bool __cdecl CFunc_Sqrt(Script::CStruct* params, Script::CScript* script);            // 0x00448C60
bool __cdecl CFunc_NormalizeVector(Script::CStruct* params, Script::CScript* script); // 0x00448CA0
bool __cdecl CFunc_LinearMap(Script::CStruct* params, Script::CScript* script);       // 0x00448D70
bool __cdecl CFunc_Clamp(Script::CStruct* params, Script::CScript* script);           // 0x00448E60
bool __cdecl CFunc_RotateVector(Script::CStruct* params, Script::CScript* script);    // 0x00448EF0

} // namespace CFuncs
