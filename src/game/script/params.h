#pragma once

#include <cstdint>

#include "hook/game.h"

// Script::CStruct - a script's parameter structure - and the parts of it script functions (CFuncs) use. Not
// decompiled yet: these call the game's code. Parameter names are checksums of the names used in the scripts;
// 0 means the unnamed (first) parameter.
namespace Script {

class CStruct;
class CScript;

// A script function as the game calls it: its parameters and the calling script; returns true to continue.
using CFunction = bool(__cdecl*)(CStruct* params, CScript* script);

// The string parameter `name`, or false if there isn't one. With `assert`, a missing parameter is an error
// (the game reports it).
inline bool GetString(CStruct* params, uint32_t name, const char** out, bool assert = false)
{
    return GameFunction<bool(__thiscall*)(CStruct*, uint32_t, const char**, bool)>(0x00476C60)(params, name, out,
                                                                                            assert);
}

// Whether the flag `name` (a bare name among the parameters) is present.
inline bool ContainsFlag(CStruct* params, uint32_t name)
{
    return GameFunction<bool(__thiscall*)(CStruct*, uint32_t)>(0x00476B40)(params, name);
}
inline bool ContainsFlag(CStruct* params, const char* name)
{
    return GameFunction<bool(__thiscall*)(CStruct*, const char*)>(0x00476BA0)(params, name);
}

} // namespace Script
