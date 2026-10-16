#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

namespace Mc {

// 0x005A6E40: script function GetMemCardSpaceAvailable.
bool GetMemCardSpaceAvailable(Script::CStruct* params, Script::CScript* script);

} // namespace Mc
