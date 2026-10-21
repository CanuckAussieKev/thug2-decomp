#pragma once

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960: the heaps' free
// memory display, a dump of the script value layouts, .qb loading, screenshots, metrics, the language tests,
// proximity cleanup, resetting the engine. All are cdecl (params, script) and return whether the script's `if`
// should be taken.
namespace CFuncs {

bool __cdecl CFunc_DisplayFreeMem(Script::CStruct* params, Script::CScript* script); // 0x004489B0
bool __cdecl CFunc_Test(Script::CStruct* params, Script::CScript* script);           // 0x00449110

} // namespace CFuncs
