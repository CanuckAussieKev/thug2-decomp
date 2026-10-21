#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The engine's platform and timer script commands (CFunctions), 0x0044E3A0-0x0044E4D4: which platform this is,
// seeding the random numbers, a stopwatch (ResetTimer / TimeGreaterThan) and the game clock's times. All are cdecl
// (params, script) and return whether the script's `if` should be taken.
namespace CFuncs {

bool __cdecl CFunc_IsWIN32(Script::CStruct* params, Script::CScript* script);         // 0x0044E3A0
bool __cdecl CFunc_GetPlatform(Script::CStruct* params, Script::CScript* script);     // 0x0044E3B0
bool __cdecl CFunc_Randomize(Script::CStruct* params, Script::CScript* script);       // 0x0044E3D0
bool __cdecl CFunc_ResetTimer(Script::CStruct* params, Script::CScript* script);      // 0x0044E3F0
bool __cdecl CFunc_TimeGreaterThan(Script::CStruct* params, Script::CScript* script); // 0x0044E400
bool __cdecl CFunc_GetStartTime(Script::CStruct* params, Script::CScript* script);    // 0x0044E460
bool __cdecl CFunc_GetElapsedTime(Script::CStruct* params, Script::CScript* script);  // 0x0044E490

} // namespace CFuncs
